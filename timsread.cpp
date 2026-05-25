/*
 * timsread.cpp
 *
 * High-performance Bruker timsTOF TDF -> MGF/MS1 converter.
 *
 * Usage:
 *   ./timsread <file.d>          -- extract MS/MS to .mgf
 *   ./timsread <file.d> -ms1     -- also extract MS1 binary data to _ms1.txt
 *   ./timsread <file.d> -sql     -- fast: dump Frames table from analysis.tdf
 *                                   (no SDK binary read). RT, MsMsType,
 *                                   MaxIntensity, SummedIntensities per frame.
 *   ./timsread <file.d> -chrom   -- extract all traces from both
 *                                   chromatography-data.sqlite (run) and
 *                                   chromatography-data-pre.sqlite (pre-run).
 *   ./timsread <file.d> -tdf     -- dump EVERY table in analysis.tdf as TSV
 *                                   (dynamic: ddaPASEF, diaPASEF, any version)
 *                                   + SDK-derived mz and 1/K0 calibration tables
 *                                   from analysis.tdf_bin (one per frame).
 *
 * Build:
 *   g++ -O3 -march=native -std=c++17 \
 *     -I<sdk>/timsdata/include/c \
 *     -I<sdk>/timsdata/examples/timsdataSampleCpp/timsdataSampleCpp \
 *     -o timsread timsread.cpp \
 *     -L<sdk>/timsdata/linux64 -ltimsdata -lsqlite3
 *
 * Dependencies: timsdata SDK, libsqlite3-dev (system package only)
 */

#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

#include <sqlite3.h>
#include "timsdata_cpp.h"

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct FrameInfo {
    int    id;
    double time;
    int    numScans;
    int    msMsType;
};

struct PrecursorInfo {
    double mz;
    int    charge;
    double intensity;
};

struct PasefInfo {
    int    frame;
    int    scanBegin;
    int    scanEnd;
    double collisionEnergy;
    int    precursorId;
};

// ---------------------------------------------------------------------------
// SQLite helpers
// ---------------------------------------------------------------------------

static void sqlCheck(int rc, sqlite3* db, const char* context)
{
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        std::cerr << "SQLite error in " << context << ": "
                  << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        std::exit(1);
    }
}

static sqlite3* openDb(const std::string& path)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    sqlCheck(rc, db, "open");
    return db;
}

static std::vector<FrameInfo> loadFrames(sqlite3* db)
{
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db,
        "SELECT Id, Time, NumScans, MsMsType FROM Frames ORDER BY Id;",
        -1, &stmt, nullptr);
    sqlCheck(rc, db, "loadFrames");

    std::vector<FrameInfo> frames;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FrameInfo f;
        f.id       = sqlite3_column_int(stmt, 0);
        f.time     = sqlite3_column_double(stmt, 1);
        f.numScans = sqlite3_column_int(stmt, 2);
        f.msMsType = sqlite3_column_int(stmt, 3);
        frames.push_back(f);
    }
    sqlite3_finalize(stmt);
    return frames;
}

static std::unordered_map<int, PrecursorInfo> loadPrecursors(sqlite3* db)
{
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db,
        "SELECT Id, MonoisotopicMz, Charge, Intensity FROM Precursors;",
        -1, &stmt, nullptr);
    sqlCheck(rc, db, "loadPrecursors");

    std::unordered_map<int, PrecursorInfo> precursors;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        PrecursorInfo p;
        p.mz        = sqlite3_column_double(stmt, 1);
        p.charge    = sqlite3_column_int(stmt, 2);
        p.intensity = sqlite3_column_double(stmt, 3);
        precursors[id] = p;
    }
    sqlite3_finalize(stmt);
    return precursors;
}

static std::unordered_map<int, std::vector<PasefInfo>> loadPasef(sqlite3* db)
{
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db,
        "SELECT Frame, ScanNumBegin, ScanNumEnd, CollisionEnergy, Precursor "
        "FROM PasefFrameMsMsInfo ORDER BY Frame;",
        -1, &stmt, nullptr);
    sqlCheck(rc, db, "loadPasef");

    std::unordered_map<int, std::vector<PasefInfo>> pasef;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PasefInfo p;
        p.frame           = sqlite3_column_int(stmt, 0);
        p.scanBegin       = sqlite3_column_int(stmt, 1);
        p.scanEnd         = sqlite3_column_int(stmt, 2);
        p.collisionEnergy = sqlite3_column_double(stmt, 3);
        p.precursorId     = sqlite3_column_int(stmt, 4);
        pasef[p.frame].push_back(p);
    }
    sqlite3_finalize(stmt);
    return pasef;
}

// ---------------------------------------------------------------------------
// -chrom mode: decode all TraceChunks from chromatography-data.sqlite
// Blob encoding: Times = little-endian float64, Intensities = little-endian float32
// Both chromatography-data.sqlite (run) and chromatography-data-pre.sqlite (pre-run)
// are processed. One output TSV per trace, named by trace description.
// ---------------------------------------------------------------------------

// Unit codes observed in TraceSources.Unit
static const char* unitName(int unit)
{
    switch (unit) {
        case 2: return "mL_per_min";
        case 3: return "bar";
        case 4: return "uL";
        case 5: return "degC";
        case 6: return "counts";
        case 7: return "deg";
        default: return "unknown";
    }
}

// Sanitise a trace description for use as a filename component
static std::string sanitiseDesc(const std::string& desc)
{
    std::string out;
    for (char c : desc) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
            out += c;
        else if (c == ' ' || c == ',' || c == '/' || c == '\\' || c == '+')
            out += '_';
        // skip everything else (e.g. ± unicode)
    }
    // collapse consecutive underscores
    std::string clean;
    bool last_under = false;
    for (char c : out) {
        if (c == '_') {
            if (!last_under) clean += c;
            last_under = true;
        } else {
            clean += c;
            last_under = false;
        }
    }
    // trim trailing underscore
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    return clean;
}

static void writeChromFile(sqlite3* db, int traceId,
                           const std::string& desc, int unitCode,
                           const std::string& instrument,
                           const std::string& outDir,
                           const std::string& prefix)
{
    // Collect all chunks for this trace, ordered by rowid
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db,
        "SELECT Times, Intensities FROM TraceChunks WHERE Trace=? ORDER BY rowid;",
        -1, &stmt, nullptr);
    sqlCheck(rc, db, "writeChromFile:prepare");

    sqlite3_bind_int(stmt, 1, traceId);

    std::vector<double> times;
    std::vector<float>  intens;
    times.reserve(8192);
    intens.reserve(8192);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* tBlob = sqlite3_column_blob(stmt, 0);
        int         tBytes = sqlite3_column_bytes(stmt, 0);
        const void* iBlob = sqlite3_column_blob(stmt, 1);
        int         iBytes = sqlite3_column_bytes(stmt, 1);

        if (!tBlob || !iBlob || tBytes <= 0 || iBytes <= 0) continue;

        // Times: float64 (8 bytes each)
        size_t nT = static_cast<size_t>(tBytes) / 8;
        const double* tPtr = reinterpret_cast<const double*>(tBlob);
        times.insert(times.end(), tPtr, tPtr + nT);

        // Intensities: float32 (4 bytes each)
        size_t nI = static_cast<size_t>(iBytes) / 4;
        const float* iPtr = reinterpret_cast<const float*>(iBlob);
        intens.insert(intens.end(), iPtr, iPtr + nI);
    }
    sqlite3_finalize(stmt);

    if (times.empty()) return;  // trace has no data - skip silently

    // Use the smaller count in case of any mismatch
    size_t nPts = std::min(times.size(), intens.size());

    std::string safeName = sanitiseDesc(desc);
    std::string outPath  = outDir + "/" + prefix + "_trace_" + safeName + ".txt";

    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "Warning: cannot open " << outPath << " - skipping trace " << traceId << std::endl;
        return;
    }

    // Header
    out << "# Trace_ID: "   << traceId    << "\n"
        << "# Description: " << desc       << "\n"
        << "# Instrument: "  << instrument << "\n"
        << "# Unit: "        << unitName(unitCode) << "\n"
        << "# Points: "      << nPts       << "\n"
        << "Time_seconds\tTime_minutes\tValue\n";

    out << std::fixed << std::setprecision(6);
    for (size_t k = 0; k < nPts; ++k) {
        out << times[k]       << "\t"
            << times[k]/60.0  << "\t"
            << intens[k]      << "\n";
    }
    out.close();

    std::cout << "  [" << std::setw(2) << traceId << "] "
              << std::left << std::setw(32) << desc
              << std::right
              << std::setw(6) << nPts << " pts  "
              << unitName(unitCode) << "  -> "
              << outPath << "\n";
}

static void writeChromFromFile(const std::string& chromFile,
                               const std::string& outDir,
                               const std::string& prefix)
{
    if (!std::ifstream(chromFile).good()) {
        std::cout << "  (not found: " << chromFile << " - skipping)\n";
        return;
    }

    sqlite3* db = openDb(chromFile);

    // Read TraceSources metadata
    sqlite3_stmt* src = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT Id, Description, Instrument, Unit FROM TraceSources ORDER BY Id;",
        -1, &src, nullptr);

    struct TraceMeta { int id; std::string desc, instrument; int unit; };
    std::vector<TraceMeta> sources;
    while (sqlite3_step(src) == SQLITE_ROW) {
        TraceMeta m;
        m.id         = sqlite3_column_int(src, 0);
        m.desc       = reinterpret_cast<const char*>(sqlite3_column_text(src, 1));
        m.instrument = reinterpret_cast<const char*>(sqlite3_column_text(src, 2));
        m.unit       = sqlite3_column_int(src, 3);
        sources.push_back(m);
    }
    sqlite3_finalize(src);

    // Get set of traces that actually have data
    sqlite3_stmt* have = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT DISTINCT Trace FROM TraceChunks;", -1, &have, nullptr);
    std::unordered_map<int,bool> hasData;
    while (sqlite3_step(have) == SQLITE_ROW)
        hasData[sqlite3_column_int(have, 0)] = true;
    sqlite3_finalize(have);

    // Write one file per populated trace
    for (const TraceMeta& m : sources) {
        if (hasData.count(m.id))
            writeChromFile(db, m.id, m.desc, m.unit, m.instrument, outDir, prefix);
    }

    // Also write a summary TSV: one row per trace with statistics
    std::string summaryPath = outDir + "/" + prefix + "_chrom_summary.txt";
    sqlite3_stmt* stats = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT ts.Id, ts.Description, ts.Unit, "
        "       COALESCE(tst.Count,0), COALESCE(tst.MinValue,0), "
        "       COALESCE(tst.MaxValue,0), COALESCE(tst.ArithmeticMean,0) "
        "FROM TraceSources ts "
        "LEFT JOIN TraceStatistics tst ON ts.Id = tst.Trace "
        "ORDER BY ts.Id;",
        -1, &stats, nullptr);

    std::ofstream sumOut(summaryPath);
    sumOut << "Trace_ID\tDescription\tUnit\tCount\tMinValue\tMaxValue\tMean\n";
    sumOut << std::fixed << std::setprecision(4);
    while (sqlite3_step(stats) == SQLITE_ROW) {
        sumOut << sqlite3_column_int(stats, 0)    << "\t"
               << sqlite3_column_text(stats, 1)   << "\t"
               << unitName(sqlite3_column_int(stats, 2)) << "\t"
               << sqlite3_column_int(stats, 3)    << "\t"
               << sqlite3_column_double(stats, 4) << "\t"
               << sqlite3_column_double(stats, 5) << "\t"
               << sqlite3_column_double(stats, 6) << "\n";
    }
    sqlite3_finalize(stats);
    sumOut.close();
    std::cout << "  Summary -> " << summaryPath << "\n";

    sqlite3_close(db);
}

static void writeChrom(const std::string& tdfDirectory)
{
    // chromatography-data.sqlite and chromatography-data-pre.sqlite
    // live inside the .d directory alongside analysis.tdf
    std::string runFile  = tdfDirectory + "/chromatography-data.sqlite";
    std::string preFile  = tdfDirectory + "/chromatography-data-pre.sqlite";
    std::string baseName = tdfDirectory.substr(tdfDirectory.find_last_of("/\\") + 1);

    // Output goes next to the .d directory, prefixed with the run name
    // e.g. 260506_peptid_p10_Slot2-1_1_13559_chrom/
    std::string outDir = tdfDirectory + "_chrom";

    // Create output directory via system() - avoids <filesystem> dependency
    std::string mkdirCmd = "mkdir -p \"" + outDir + "\"";
    if (std::system(mkdirCmd.c_str()) != 0) {
        std::cerr << "Error: cannot create output directory: " << outDir << std::endl;
        std::exit(1);
    }

    std::cout << "\n--- Run chromatography: " << runFile << " ---\n";
    writeChromFromFile(runFile,  outDir, baseName + "_run");

    std::cout << "\n--- Pre-run chromatography: " << preFile << " ---\n";
    writeChromFromFile(preFile, outDir, baseName + "_pre");

    std::cout << "\nOutput directory: " << outDir << "\n";
}

// ---------------------------------------------------------------------------
// -tdf mode: dump all analysis.tdf tables + SDK calibration from tdf_bin
//
// analysis.tdf  : SQLite, schema varies by acquisition mode and TDF version.
//                 We introspect sqlite_master at runtime so every table is
//                 written regardless of version (ddaPASEF / diaPASEF / DIA-PASEF).
//                 Known tables include:
//                   GlobalMetadata, Frames, Precursors, PasefFrameMsMsInfo,
//                   MzCalibration, TimsCalibration, PropertyDefinitions,
//                   Properties, Segments, SampleInfo, DiaFrameMsMsInfo,
//                   DiaFrameMsMsWindows, DiaPasefFrameMsMsInfo, FrameMsMsInfo
//                 Blob columns are rendered as "[BLOB N bytes]" (not useful as text).
//
// analysis.tdf_bin : pure binary, only accessible via SDK.
//                 The SDK adds two things not in SQLite:
//                   1. indexToMz   : raw TOF index -> calibrated m/z (per frame)
//                   2. scanNumToOneOverK0 : scan number -> 1/K0 (per frame)
//                 We write one calibration-sample table covering the full
//                 index/scan range for the first MS1 and first MS2 frame,
//                 plus a per-frame summary of min/max m/z and 1/K0.
// ---------------------------------------------------------------------------

// Return a printable string for a SQLite column value.
// BLOBs are rendered as "[BLOB N bytes]" so the TSV stays valid.
static std::string colToString(sqlite3_stmt* stmt, int col)
{
    int type = sqlite3_column_type(stmt, col);
    switch (type) {
        case SQLITE_NULL:    return "";
        case SQLITE_INTEGER: return std::to_string(sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g", sqlite3_column_double(stmt, col));
            return buf;
        }
        case SQLITE_TEXT:
            return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)));
        case SQLITE_BLOB: {
            int n = sqlite3_column_bytes(stmt, col);
            return "[BLOB " + std::to_string(n) + " bytes]";
        }
        default: return "";
    }
}

static void dumpOneTable(sqlite3* db, const std::string& tableName,
                         const std::string& outDir, const std::string& prefix)
{
    // Build a SELECT * for this table
    std::string sql = "SELECT * FROM [" + tableName + "];";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "  Warning: cannot query table [" << tableName << "]: "
                  << sqlite3_errmsg(db) << "\n";
        return;
    }

    int ncols = sqlite3_column_count(stmt);
    if (ncols == 0) { sqlite3_finalize(stmt); return; }

    std::string outPath = outDir + "/" + prefix + "_" + tableName + ".txt";
    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "  Warning: cannot open " << outPath << "\n";
        sqlite3_finalize(stmt);
        return;
    }

    // Header
    for (int c = 0; c < ncols; ++c) {
        out << sqlite3_column_name(stmt, c);
        if (c < ncols - 1) out << "\t";
    }
    out << "\n";

    // Rows
    long long nrows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int c = 0; c < ncols; ++c) {
            out << colToString(stmt, c);
            if (c < ncols - 1) out << "\t";
        }
        out << "\n";
        ++nrows;
    }
    sqlite3_finalize(stmt);
    out.close();

    std::cout << "  " << std::left << std::setw(34) << tableName
              << std::right << std::setw(8) << nrows << " rows -> "
              << outPath << "\n";
}

static void dumpAllTdfTables(const std::string& tdfFile,
                             const std::string& outDir,
                             const std::string& prefix)
{
    sqlite3* db = openDb(tdfFile);

    // Enumerate all tables and views from sqlite_master
    sqlite3_stmt* master = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT name, type FROM sqlite_master "
        "WHERE type IN ('table','view') ORDER BY type DESC, name;",
        -1, &master, nullptr);

    std::vector<std::string> tables;
    while (sqlite3_step(master) == SQLITE_ROW) {
        std::string name  = reinterpret_cast<const char*>(sqlite3_column_text(master, 0));
        std::string ttype = reinterpret_cast<const char*>(sqlite3_column_text(master, 1));
        tables.push_back(name);
        std::cout << "  Found " << ttype << ": " << name << "\n";
    }
    sqlite3_finalize(master);

    std::cout << "\nDumping " << tables.size() << " tables/views...\n";
    for (const auto& t : tables)
        dumpOneTable(db, t, outDir, prefix);

    sqlite3_close(db);
}

// Write SDK-derived calibration tables from analysis.tdf_bin.
// For each of the first N_CALIB_FRAMES MS1 frames and first N_CALIB_FRAMES MS2
// frames, write:
//   - mz calibration: TOF index 0..maxIndex step 100 -> m/z
//   - mobility calibration: scan 0..numScans step 1 -> 1/K0
// Also writes a per-frame summary (frame_id, rt, min_mz, max_mz, min_k0, max_k0).

static void dumpSdkCalibration(const std::string& tdfDirectory,
                               const std::string& tdfFile,
                               const std::string& outDir,
                               const std::string& prefix)
{
    const int N_CALIB_FRAMES = 3;  // first N MS1 and first N MS2 frames

    timsdata::TimsData data(tdfDirectory);

    // Load frame metadata to know numScans and MsMsType
    sqlite3* db = openDb(tdfFile);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT Id, Time, NumScans, MsMsType FROM Frames ORDER BY Id;",
        -1, &stmt, nullptr);

    struct FrInfo { int64_t id; double time; int numScans; int type; };
    std::vector<FrInfo> ms1frames, ms2frames;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FrInfo f;
        f.id       = sqlite3_column_int64(stmt, 0);
        f.time     = sqlite3_column_double(stmt, 1);
        f.numScans = sqlite3_column_int(stmt, 2);
        f.type     = sqlite3_column_int(stmt, 3);
        if (f.type == 0 && (int)ms1frames.size() < N_CALIB_FRAMES) ms1frames.push_back(f);
        if (f.type != 0 && (int)ms2frames.size() < N_CALIB_FRAMES) ms2frames.push_back(f);
        if ((int)ms1frames.size() >= N_CALIB_FRAMES &&
            (int)ms2frames.size() >= N_CALIB_FRAMES) break;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    // For each selected frame write mz calibration and mobility calibration
    for (int pass = 0; pass < 2; ++pass) {
        auto& frames = (pass == 0) ? ms1frames : ms2frames;
        std::string msTag = (pass == 0) ? "MS1" : "MS2";

        for (const FrInfo& fr : frames) {
            // -- m/z calibration: sample 500 evenly-spaced TOF indices --
            // Typical timsTOF TOF index range: 0 - ~400,000
            const int N_MZ_STEPS = 500;
            const double maxIdx  = 400000.0;
            std::vector<double> idxIn(N_MZ_STEPS), mzOut;
            for (int k = 0; k < N_MZ_STEPS; ++k)
                idxIn[k] = (maxIdx / (N_MZ_STEPS - 1)) * k;
            data.indexToMz(fr.id, idxIn, mzOut);

            std::string mzPath = outDir + "/" + prefix
                + "_calib_mz_frame" + std::to_string(fr.id)
                + "_" + msTag + ".txt";
            std::ofstream mzOut2(mzPath);
            mzOut2 << "# Frame_ID=" << fr.id
                   << "  RT_seconds=" << fr.time
                   << "  MsMsType=" << fr.type << "\n"
                   << "TOF_index\tmz\n";
            mzOut2 << std::fixed << std::setprecision(6);
            for (int k = 0; k < N_MZ_STEPS; ++k)
                mzOut2 << idxIn[k] << "\t" << mzOut[k] << "\n";
            mzOut2.close();

            // -- 1/K0 calibration: every scan in the frame --
            int ns = fr.numScans;
            std::vector<double> scanIn(ns), k0Out;
            for (int k = 0; k < ns; ++k) scanIn[k] = static_cast<double>(k);
            data.scanNumToOneOverK0(fr.id, scanIn, k0Out);

            std::string k0Path = outDir + "/" + prefix
                + "_calib_1k0_frame" + std::to_string(fr.id)
                + "_" + msTag + ".txt";
            std::ofstream k0Out2(k0Path);
            k0Out2 << "# Frame_ID=" << fr.id
                   << "  RT_seconds=" << fr.time
                   << "  MsMsType=" << fr.type << "\n"
                   << "Scan_number\t1_over_K0\n";
            k0Out2 << std::fixed << std::setprecision(6);
            for (int k = 0; k < ns; ++k)
                k0Out2 << k << "\t" << k0Out[k] << "\n";
            k0Out2.close();

            std::cout << "  Frame " << fr.id << " (" << msTag << ")"
                      << "  mz_calib -> " << mzPath << "\n"
                      << "           "
                      << "  1k0_calib -> " << k0Path << "\n";
        }
    }

    // Per-frame calibration summary: min/max mz and 1/K0 for every frame
    // Re-open full frame list for summary
    db = openDb(tdfFile);
    sqlite3_prepare_v2(db,
        "SELECT Id, Time, NumScans, MsMsType FROM Frames ORDER BY Id;",
        -1, &stmt, nullptr);

    std::string sumPath = outDir + "/" + prefix + "_calib_summary.txt";
    std::ofstream sumOut(sumPath);
    sumOut << "Frame_ID\tRT_seconds\tMsMsType\tNumScans\t"
           << "mz_at_idx0\tmz_at_idx400k\t1k0_scan0\t1k0_scanN\n";
    sumOut << std::fixed << std::setprecision(6);

    std::vector<double> two_idx = {0.0, 400000.0};
    std::vector<double> two_mz;
    std::vector<double> two_scan_mz, two_k0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t fid  = sqlite3_column_int64(stmt, 0);
        double  time = sqlite3_column_double(stmt, 1);
        int     ns2  = sqlite3_column_int(stmt, 2);
        int     mstype = sqlite3_column_int(stmt, 3);

        data.indexToMz(fid, two_idx, two_mz);

        std::vector<double> edge_scans = {0.0, static_cast<double>(ns2 - 1)};
        data.scanNumToOneOverK0(fid, edge_scans, two_k0);

        sumOut << fid     << "\t" << time   << "\t"
               << mstype  << "\t" << ns2    << "\t"
               << two_mz[0] << "\t" << two_mz[1] << "\t"
               << two_k0[0] << "\t" << two_k0[1] << "\n";
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    sumOut.close();
    std::cout << "  Calibration summary -> " << sumPath << "\n";
}

static void writeTdf(const std::string& tdfDirectory)
{
    std::string tdfFile = tdfDirectory + "/analysis.tdf";
    std::string baseName = tdfDirectory.substr(tdfDirectory.find_last_of("/\\") + 1);
    std::string outDir   = tdfDirectory + "_tdf";

    std::string mkdirCmd = "mkdir -p \"" + outDir + "\"";
    if (std::system(mkdirCmd.c_str()) != 0) {
        std::cerr << "Error: cannot create output directory: " << outDir << std::endl;
        std::exit(1);
    }

    std::cout << "\n--- analysis.tdf tables (" << tdfFile << ") ---\n";
    dumpAllTdfTables(tdfFile, outDir, baseName);

    std::cout << "\n--- analysis.tdf_bin SDK calibration ---\n";
    try {
        dumpSdkCalibration(tdfDirectory, tdfFile, outDir, baseName);
    }
    catch (const std::exception& e) {
        std::cerr << "  Warning: SDK calibration failed: " << e.what() << "\n";
        std::cerr << "  (tdf_bin may be unavailable - SQL tables still written)\n";
    }

    std::cout << "\nOutput directory: " << outDir << "\n";
}

// ---------------------------------------------------------------------------
// -tdfbin mode: dump complete binary data from analysis.tdf_bin via SDK.
//
// SIZE WARNING: a typical 90-min ddaPASEF run produces ~330M peaks:
//   MS1 (~5,775 frames, all IMS scans)    : ~15 GB as TSV
//   MS2 (~44,799 PASEF frames, all scans) :  ~5 GB as TSV
//   Total                                 : ~20 GB
//
// Output is split into:
//   <run>_tdfbin/<run>_ms1_chunk001.txt ...   (MS1 frames, ~500 MB each)
//   <run>_tdfbin/<run>_ms2_chunk001.txt ...   (MS2 frames, ~500 MB each)
// Format: Frame_ID\tRT_seconds\tMsMsType\tScan\tmz\tIntensity\t1_over_K0
//
// Differences vs existing modes:
//   -ms1 : MS1 only, every 10th IMS scan sampled -> ~1.5 GB
//   -tdfbin: ALL frames, ALL IMS scans, calibrated -> complete record
//
// Requires explicit confirmation because of output size.
// ---------------------------------------------------------------------------

static void estimateTdfBinSize(const std::string& tdfFile,
                                int64_t& ms1Peaks, int64_t& ms2Peaks,
                                int64_t& ms1Frames, int64_t& ms2Frames)
{
    ms1Peaks = ms2Peaks = ms1Frames = ms2Frames = 0;
    sqlite3* db = openDb(tdfFile);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT MsMsType, COUNT(*), SUM(NumPeaks) FROM Frames GROUP BY MsMsType;",
        -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int     type   = sqlite3_column_int(stmt, 0);
        int64_t nframe = sqlite3_column_int64(stmt, 1);
        int64_t npeak  = sqlite3_column_int64(stmt, 2);
        if (type == 0) { ms1Frames = nframe; ms1Peaks = npeak; }
        else           { ms2Frames = nframe; ms2Peaks = npeak; }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// Open the next chunk file for writing, closing the previous one if open.
// Returns the new chunk number.
static int openNextChunk(std::ofstream& out,
                         const std::string& outDir,
                         const std::string& prefix,
                         const std::string& tag,
                         int chunkNum,
                         const std::string& header)
{
    if (out.is_open()) out.close();
    ++chunkNum;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", chunkNum);
    std::string path = outDir + "/" + prefix + "_" + tag + "_chunk" + buf + ".txt";
    out.open(path);
    if (!out.is_open()) {
        std::cerr << "Error: cannot open chunk file: " << path << "\n";
        std::exit(1);
    }
    out << header;
    std::cout << "  -> " << path << "\n";
    return chunkNum;
}

static void writeTdfBin(const std::string& tdfDirectory)
{
    const std::string tdfFile  = tdfDirectory + "/analysis.tdf";
    const std::string baseName = tdfDirectory.substr(tdfDirectory.find_last_of("/\\") + 1);
    const std::string outDir   = tdfDirectory + "_tdfbin";

    // Size estimate from SQLite
    int64_t ms1Peaks, ms2Peaks, ms1Frames, ms2Frames;
    estimateTdfBinSize(tdfFile, ms1Peaks, ms2Peaks, ms1Frames, ms2Frames);
    int64_t totalPeaks = ms1Peaks + ms2Peaks;

    const double bytesPerPeak = 60.0;  // ~60 bytes per TSV row
    const double ms1GB  = ms1Peaks  * bytesPerPeak / 1e9;
    const double ms2GB  = ms2Peaks  * bytesPerPeak / 1e9;
    const double totGB  = totalPeaks * bytesPerPeak / 1e9;

    const int64_t CHUNK_BYTES    = 500LL * 1024 * 1024;  // 500 MB per chunk
    const int64_t peaksPerChunk  = static_cast<int64_t>(CHUNK_BYTES / bytesPerPeak);
    int ms1Chunks = static_cast<int>((ms1Peaks  + peaksPerChunk - 1) / peaksPerChunk);
    int ms2Chunks = static_cast<int>((ms2Peaks  + peaksPerChunk - 1) / peaksPerChunk);

    std::cout << "\n=== analysis.tdf_bin full extraction ===\n"
              << "  MS1 : " << ms1Frames << " frames, "
              << ms1Peaks  << " peaks  ->  ~" << std::fixed << std::setprecision(1)
              << ms1GB << " GB  (" << ms1Chunks << " chunk(s))\n"
              << "  MS2 : " << ms2Frames << " frames, "
              << ms2Peaks  << " peaks  ->  ~" << ms2GB << " GB  (" << ms2Chunks << " chunk(s))\n"
              << "  Total                     ->  ~" << totGB << " GB\n\n"
              << "  Output: " << outDir << "/\n"
              << "  Format: Frame_ID  RT_seconds  MsMsType  Scan  mz  Intensity  1_over_K0\n\n";

    // Confirmation prompt - require explicit 'y'
    std::cout << "Proceed? This will write ~" << std::setprecision(1) << totGB
              << " GB to disk. [y/N] " << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    if (answer.empty() || (answer[0] != 'y' && answer[0] != 'Y')) {
        std::cout << "Aborted.\n";
        return;
    }

    std::string mkdirCmd = "mkdir -p \"" + outDir + "\"";
    if (std::system(mkdirCmd.c_str()) != 0) {
        std::cerr << "Error: cannot create output directory: " << outDir << "\n";
        std::exit(1);
    }

    // Open SDK
    timsdata::TimsData data(tdfDirectory);
    auto calId = data.getCalibrationId();
    std::cout << "# Calibration: "
              << (calId.has_value() ? calId.value() : "instrument default") << "\n";

    // Load all frames from SQLite
    sqlite3* db = openDb(tdfFile);
    auto frames = loadFrames(db);
    sqlite3_close(db);

    const std::string colHeader =
        "# Frame_ID\tRT_seconds\tMsMsType\tScan\tmz\tIntensity\t1_over_K0\n";

    // Chunk file handles
    std::ofstream ms1Out, ms2Out;
    int ms1ChunkNum = 0, ms2ChunkNum = 0;
    int64_t ms1PeaksSoFar = 0, ms2PeaksSoFar = 0;

    // Pre-allocated reuse buffers
    std::vector<double> indices, mzVec, scanVec(1), k0Vec;
    indices.reserve(4096);
    mzVec.reserve(4096);

    std::ostringstream buf;
    {   // pre-size the underlying string buffer to 8 MB to reduce reallocations
        std::string pre(8 * 1024 * 1024, '\0');
        buf.str(std::move(pre));
        buf.str("");   // reset position to 0, capacity retained
        buf.clear();
    }
    const int FLUSH_EVERY = 200;   // flush buffer every N frames

    int64_t totalPeaksWritten = 0;
    int totalFrames = static_cast<int>(frames.size());

    std::cerr << "Extracting...\n";

    for (int i = 0; i < totalFrames; ++i) {
        const FrameInfo& fr = frames[i];
        bool isMS1 = (fr.msMsType == 0);

        // Progress
        if ((i + 1) % 2000 == 0 || i == totalFrames - 1) {
            int pct = ((i + 1) * 100) / totalFrames;
            std::cerr << "\r  " << pct << "% (" << (i + 1) << "/" << totalFrames
                      << ")  written: " << totalPeaksWritten << " peaks   ";
            std::cerr.flush();
        }

        // Open first chunk or roll to next if size exceeded
        if (isMS1) {
            if (!ms1Out.is_open() || ms1PeaksSoFar >= peaksPerChunk) {
                ms1ChunkNum = openNextChunk(ms1Out, outDir, baseName, "ms1",
                                            ms1ChunkNum, colHeader);
                ms1PeaksSoFar = 0;
            }
        } else {
            if (!ms2Out.is_open() || ms2PeaksSoFar >= peaksPerChunk) {
                ms2ChunkNum = openNextChunk(ms2Out, outDir, baseName, "ms2",
                                            ms2ChunkNum, colHeader);
                ms2PeaksSoFar = 0;
            }
        }

        std::ofstream& out = isMS1 ? ms1Out : ms2Out;

        // Read all scans in this frame
        auto scans = data.readScans(fr.id, 0, fr.numScans);

        // Batch-convert ALL scan numbers to 1/K0 for this frame at once
        int ns = static_cast<int>(scans.getNbrScans());
        std::vector<double> allScans(ns);
        for (int s = 0; s < ns; ++s) allScans[s] = static_cast<double>(s);
        std::vector<double> allK0(ns);
        data.scanNumToOneOverK0(fr.id, allScans, allK0);

        buf.str(""); buf.clear();
        buf << std::fixed << std::setprecision(6);

        int64_t framePeaks = 0;
        for (int scan = 0; scan < ns; ++scan) {
            auto nPeaks = scans.getNbrPeaks(scan);
            if (nPeaks == 0) continue;

            auto xAxis = scans.getScanX(scan);
            auto yAxis = scans.getScanY(scan);

            indices.assign(xAxis.first, xAxis.second);
            mzVec.resize(indices.size());
            data.indexToMz(fr.id, indices, mzVec);

            double k0 = allK0[scan];

            for (size_t k = 0; k < nPeaks; ++k) {
                uint32_t rawInt = yAxis.first[k];
                if (rawInt == 0) continue;
                buf << fr.id       << "\t"
                    << fr.time     << "\t"
                    << fr.msMsType << "\t"
                    << scan        << "\t"
                    << mzVec[k]    << "\t"
                    << rawInt      << "\t"
                    << k0          << "\n";
                ++framePeaks;
            }
        }

        out << buf.str();

        if (isMS1) ms1PeaksSoFar  += framePeaks;
        else       ms2PeaksSoFar  += framePeaks;
        totalPeaksWritten          += framePeaks;

        // Periodic OS-level flush to avoid huge OS buffer
        if ((i + 1) % FLUSH_EVERY == 0) {
            ms1Out.flush();
            ms2Out.flush();
        }
    }

    if (ms1Out.is_open()) ms1Out.close();
    if (ms2Out.is_open()) ms2Out.close();

    std::cerr << "\n";
    std::cout << "\nDone.\n"
              << "  Total peaks written : " << totalPeaksWritten << "\n"
              << "  MS1 chunks          : " << ms1ChunkNum << "\n"
              << "  MS2 chunks          : " << ms2ChunkNum << "\n"
              << "  Output              : " << outDir << "/\n";
}

// ---------------------------------------------------------------------------
// -sql mode: frame metadata only, no SDK binary read
// Output: Frame_ID  RT_seconds  MsMsType  MaxIntensity  SummedIntensities
//         NumScans  NumPeaks
// Sufficient for BPC/TIC plots in timsplot.py without loading _ms1.txt.
// ---------------------------------------------------------------------------

static void writeSqlFrames(const std::string& tdfFile, const std::string& outPath)
{
    sqlite3* db = openDb(tdfFile);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db,
        "SELECT Id, Time, MsMsType, MaxIntensity, SummedIntensities, NumScans, NumPeaks "
        "FROM Frames ORDER BY Id;",
        -1, &stmt, nullptr);
    sqlCheck(rc, db, "writeSqlFrames");

    std::ofstream out(outPath);
    if (!out.is_open()) {
        std::cerr << "Error: cannot open output file: " << outPath << std::endl;
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        std::exit(1);
    }

    out << "Frame_ID\tRT_seconds\tMsMsType\tMaxIntensity\tSummedIntensities\tNumScans\tNumPeaks\n";

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int     id         = sqlite3_column_int(stmt, 0);
        double  time       = sqlite3_column_double(stmt, 1);
        int     msMsType   = sqlite3_column_int(stmt, 2);
        int64_t maxInt     = sqlite3_column_int64(stmt, 3);
        int64_t sumInt     = sqlite3_column_int64(stmt, 4);
        int     numScans   = sqlite3_column_int(stmt, 5);
        int     numPeaks   = sqlite3_column_int(stmt, 6);

        out << id       << "\t"
            << std::fixed << std::setprecision(6) << time << "\t"
            << msMsType << "\t"
            << maxInt   << "\t"
            << sumInt   << "\t"
            << numScans << "\t"
            << numPeaks << "\n";
        ++count;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    out.close();

    std::cout << "# Frames written : " << count   << std::endl;
    std::cout << "# Output         : " << outPath << std::endl;
}

// ---------------------------------------------------------------------------
// Frame processing (MS1 + MS/MS binary extraction)
// ---------------------------------------------------------------------------

static void processAllFrames(
    timsdata::TimsData& data,
    const std::vector<FrameInfo>& frames,
    const std::unordered_map<int, PrecursorInfo>& precursors,
    const std::unordered_map<int, std::vector<PasefInfo>>& pasefData,
    std::ofstream& mgfFile,
    std::ofstream* ms1File)
{
    int ms1Count        = 0;
    int ms2Count        = 0;
    int skipped         = 0;
    int totalFrames     = static_cast<int>(frames.size());

    // 4 MB I/O buffers
    static char mgfBuf[4 * 1024 * 1024];
    static char ms1Buf[4 * 1024 * 1024];
    mgfFile.rdbuf()->pubsetbuf(mgfBuf, sizeof(mgfBuf));
    if (ms1File)
        ms1File->rdbuf()->pubsetbuf(ms1Buf, sizeof(ms1Buf));

    // Pre-allocated reusable containers
    std::vector<double> mobilityVec(1), scanVec(1);
    std::vector<double> xAxisMasses, indices;
    std::vector<std::pair<double, double>> peaks;
    xAxisMasses.reserve(2000);
    indices.reserve(2000);
    peaks.reserve(5000);

    std::ostringstream mgfBufStr, ms1BufStr;
    const int FLUSH_INTERVAL = 500;

    std::cerr << "Processing frames..." << std::endl;

    for (int i = 0; i < totalFrames; ++i) {
        const FrameInfo& frame = frames[i];

        if ((i + 1) % 1000 == 0 || i == totalFrames - 1) {
            int pct = ((i + 1) * 100) / totalFrames;
            std::cerr << "\rProgress: " << pct << "% ("
                      << (i + 1) << "/" << totalFrames
                      << ") MS1:" << ms1Count << " MS2:" << ms2Count << " ";
            std::cerr.flush();
        }

        if (frame.msMsType != 0) {
            // --- MS/MS frame ---
            auto it = pasefData.find(frame.id);
            if (it == pasefData.end()) goto flush;

            {
                auto scans = data.readScans(frame.id, 0, frame.numScans);

                for (const PasefInfo& pasef : it->second) {
                    auto precIt = precursors.find(pasef.precursorId);
                    if (precIt == precursors.end()) continue;
                    const PrecursorInfo& prec = precIt->second;
                    if (prec.mz <= 0.0) { ++skipped; continue; }

                    peaks.clear();
                    int scanStart = std::max(pasef.scanBegin, 0);
                    int scanEnd   = std::min(pasef.scanEnd,
                                            static_cast<int>(scans.getNbrScans()) - 1);

                    for (int scan = scanStart; scan <= scanEnd; ++scan) {
                        auto nPeaks = scans.getNbrPeaks(scan);
                        if (nPeaks == 0) continue;

                        auto xAxis = scans.getScanX(scan);
                        auto yAxis = scans.getScanY(scan);

                        indices.assign(xAxis.first, xAxis.second);
                        xAxisMasses.resize(indices.size());
                        data.indexToMz(frame.id, indices, xAxisMasses);

                        for (size_t k = 0; k < nPeaks; ++k)
                            if (yAxis.first[k] > 0.0)
                                peaks.emplace_back(xAxisMasses[k], yAxis.first[k]);
                    }

                    if (peaks.empty()) continue;

                    unsigned midScan = static_cast<unsigned>((pasef.scanBegin + pasef.scanEnd) / 2);
                    scanVec[0] = static_cast<double>(midScan);
                    data.scanNumToOneOverK0(frame.id, scanVec, mobilityVec);

                    mgfBufStr << "BEGIN IONS\n"
                              << "TITLE=Frame_" << frame.id
                              << "_Precursor_" << pasef.precursorId
                              << "_Scans_"     << pasef.scanBegin << "-" << pasef.scanEnd << "\n"
                              << "RTINSECONDS=" << std::fixed << std::setprecision(6) << frame.time << "\n"
                              << "MOBILITY="    << mobilityVec[0] << "\n";

                    if (prec.charge > 0)
                        mgfBufStr << "CHARGE=" << prec.charge << "+\n";

                    mgfBufStr << "PEPMASS=" << std::setprecision(6) << prec.mz;
                    if (prec.intensity > 0) {
                        if (prec.intensity == static_cast<int64_t>(prec.intensity))
                            mgfBufStr << " " << static_cast<int64_t>(prec.intensity);
                        else
                            mgfBufStr << " " << std::setprecision(1) << prec.intensity;
                    }
                    if (pasef.collisionEnergy > 0)
                        mgfBufStr << "\nCOLLISION_ENERGY=" << pasef.collisionEnergy;
                    mgfBufStr << "\n";

                    for (const auto& pk : peaks) {
                        mgfBufStr << std::setprecision(6) << pk.first << " ";
                        if (pk.second == static_cast<int64_t>(pk.second))
                            mgfBufStr << static_cast<int64_t>(pk.second) << "\n";
                        else
                            mgfBufStr << std::setprecision(1) << pk.second << "\n";
                    }
                    mgfBufStr << "END IONS\n\n";
                }
            }
            ++ms2Count;

        } else if (ms1File) {
            // --- MS1 frame (binary, only with -ms1) ---
            auto scans = data.readScans(frame.id, 0, frame.numScans);

            for (unsigned scan = 0; scan < scans.getNbrScans(); scan += 10) {
                auto nPeaks = scans.getNbrPeaks(scan);
                if (nPeaks == 0) continue;

                auto xAxis = scans.getScanX(scan);
                auto yAxis = scans.getScanY(scan);

                indices.assign(xAxis.first, xAxis.second);
                xAxisMasses.resize(indices.size());
                data.indexToMz(frame.id, indices, xAxisMasses);

                scanVec[0] = static_cast<double>(scan);
                data.scanNumToOneOverK0(frame.id, scanVec, mobilityVec);

                for (size_t k = 0; k < nPeaks; ++k) {
                    if (yAxis.first[k] <= 0.0) continue;
                    ms1BufStr << frame.id << "\t"
                              << std::fixed << std::setprecision(6) << frame.time << "\t"
                              << scan << "\t"
                              << xAxisMasses[k] << "\t";
                    if (yAxis.first[k] == static_cast<int64_t>(yAxis.first[k]))
                        ms1BufStr << static_cast<int64_t>(yAxis.first[k]) << "\t";
                    else
                        ms1BufStr << std::setprecision(1) << yAxis.first[k] << "\t";
                    ms1BufStr << std::setprecision(6) << mobilityVec[0] << "\n";
                }
            }
            ++ms1Count;
        }

        flush:
        if ((i + 1) % FLUSH_INTERVAL == 0) {
            if (!mgfBufStr.str().empty()) {
                mgfFile << mgfBufStr.str();
                mgfBufStr.str(""); mgfBufStr.clear();
            }
            if (ms1File && !ms1BufStr.str().empty()) {
                *ms1File << ms1BufStr.str();
                ms1BufStr.str(""); ms1BufStr.clear();
            }
        }
    }

    // Final flush
    if (!mgfBufStr.str().empty()) mgfFile << mgfBufStr.str();
    if (ms1File && !ms1BufStr.str().empty()) *ms1File << ms1BufStr.str();

    std::cerr << std::endl;
    std::cout << "Total frames processed : " << totalFrames << std::endl;
    std::cout << "MS1 frames             : " << ms1Count    << std::endl;
    std::cout << "MS/MS frames           : " << ms2Count    << std::endl;
    if (skipped > 0)
        std::cout << "Skipped (invalid m/z)  : " << skipped << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <file.d> [flags...]\n\n"
                  << "Flags (combinable except where noted):\n"
                  << "  -mgf       Extract MS/MS to _msms.mgf  [default if no flags given]\n"
                  << "  -ms1       Extract MS1 (every 10th IMS scan) to _ms1.txt\n"
                  << "             (implies -mgf)\n"
                  << "  -sql       Dump Frames table from analysis.tdf (no SDK)\n"
                  << "  -chrom     Extract all nanoElute + MS traces from\n"
                  << "             chromatography-data.sqlite and -pre.sqlite\n"
                  << "  -tdf       Dump ALL analysis.tdf tables as TSV + SDK\n"
                  << "             calibration tables from analysis.tdf_bin\n"
                  << "  -tdfbin    Dump complete binary data: ALL frames, ALL IMS\n"
                  << "             scans, calibrated mz + 1/K0. ~20 GB for 90-min\n"
                  << "             run. Chunked ~500 MB files. Interactive confirm.\n"
                  << "\nExamples:\n"
                  << "  " << argv[0] << " run.d -mgf          # MGF only\n"
                  << "  " << argv[0] << " run.d -mgf -ms1     # MGF + MS1\n"
                  << "  " << argv[0] << " run.d -sql -chrom   # metadata only, no SDK\n"
                  << "  " << argv[0] << " run.d -tdfbin       # full binary dump\n";
        return -1;
    }

    bool extractMGF = false;
    bool extractMS1 = false;
    bool sqlOnly    = false;
    bool chromOnly  = false;
    bool tdfOnly    = false;
    bool tdfBin     = false;
    std::string tdfDirectory;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg == "-mgf")    extractMGF = true;
        else if (arg == "-ms1")    extractMS1 = true;
        else if (arg == "-sql")    sqlOnly    = true;
        else if (arg == "-chrom")  chromOnly  = true;
        else if (arg == "-tdf")    tdfOnly    = true;
        else if (arg == "-tdfbin") tdfBin     = true;
        else if (arg[0] != '-')    tdfDirectory = arg;
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return -1;
        }
    }

    if (tdfDirectory.empty()) {
        std::cerr << "Error: TDF directory not specified\n";
        return -1;
    }

    // Remove trailing slash
    while (!tdfDirectory.empty() &&
           (tdfDirectory.back() == '/' || tdfDirectory.back() == '\\'))
        tdfDirectory.pop_back();

    // -ms1 implies -mgf (existing behaviour preserved)
    if (extractMS1) extractMGF = true;

    // Backward compat: no flags at all -> default to -mgf
    if (!extractMGF && !extractMS1 && !sqlOnly &&
        !chromOnly && !tdfOnly && !tdfBin)
        extractMGF = true;

    std::string tdfFile = tdfDirectory + "/analysis.tdf";

    // --- Non-SDK modes (no SDK needed, run first so they work standalone) ---

    if (sqlOnly) {
        std::cout << "Loading frame metadata from " << tdfFile << " ...\n";
        writeSqlFrames(tdfFile, tdfDirectory + "_frames.txt");
    }

    if (chromOnly) {
        std::cout << "Extracting chromatography traces from " << tdfDirectory << " ...\n";
        writeChrom(tdfDirectory);
    }

    if (tdfOnly) {
        std::cout << "Dumping all TDF tables from " << tdfDirectory << " ...\n";
        writeTdf(tdfDirectory);
    }

    // -tdfbin runs standalone (has its own confirmation + SDK init)
    if (tdfBin) {
        writeTdfBin(tdfDirectory);
    }

    // --- SDK modes: MGF and/or MS1 ---

    if (extractMGF) {
        try {
            timsdata::TimsData data(tdfDirectory);

            auto calId = data.getCalibrationId();
            std::cout << "# Calibration     : "
                      << (calId.has_value() ? calId.value() : "instrument default") << "\n";

            sqlite3* db = openDb(tdfFile);
            std::cout << "Loading metadata...\n";

            auto frames     = loadFrames(db);
            auto precursors = loadPrecursors(db);
            auto pasefData  = loadPasef(db);
            sqlite3_close(db);

            std::string mgfPath = tdfDirectory + "_msms.mgf";
            std::ofstream mgfOutput(mgfPath);
            if (!mgfOutput.is_open()) {
                std::cerr << "Error: cannot open: " << mgfPath << "\n";
                return -1;
            }

            std::ofstream ms1Output;
            std::string ms1Path;
            if (extractMS1) {
                ms1Path = tdfDirectory + "_ms1.txt";
                ms1Output.open(ms1Path);
                if (!ms1Output.is_open()) {
                    std::cerr << "Error: cannot open: " << ms1Path << "\n";
                    return -1;
                }
            }

            std::cout << "# TDF file          : " << tdfDirectory << "\n"
                      << "# Total frames      : " << frames.size()     << "\n"
                      << "# Precursors loaded : " << precursors.size() << "\n"
                      << "# MS/MS frames      : " << pasefData.size()  << "\n"
                      << "# MGF output        : " << mgfPath           << "\n";
            if (extractMS1)
                std::cout << "# MS1 output        : " << ms1Path        << "\n";

            mgfOutput << "# MS/MS spectra from TDF file: " << tdfDirectory  << "\n"
                      << "# ZERO-FILTER ONLY - Remove only zero intensity peaks\n"
                      << "# MGF format for protein identification\n\n";

            if (extractMS1)
                ms1Output << "# MS1 spectra from TDF file: " << tdfDirectory << "\n"
                          << "# Format: Frame_ID RT_seconds Scan_Number"
                             " m/z Intensity Mobility\n\n";

            processAllFrames(data, frames, precursors, pasefData,
                             mgfOutput, extractMS1 ? &ms1Output : nullptr);

            mgfOutput.close();
            if (extractMS1) ms1Output.close();
            std::cout << "MGF extraction completed.\n";
        }
        catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << "\n";
            return -1;
        }
    }

    return 0;
}