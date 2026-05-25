/*
 * timsread.cpp
 *
 * High-performance Bruker timsTOF TDF -> MGF/MS1 converter.
 *
 * Usage:
 *   ./timsread <file.d>          -- extract MS/MS to .mgf
 *   ./timsread <file.d> -ms1     -- also extract MS1 binary data to _ms1.txt
 *   ./timsread <file.d> -sql     -- fast: dump Frames table from analysis.tdf
 *                                   (no SDK binary read). Gives RT, MsMsType,
 *                                   MaxIntensity, SummedIntensities per frame.
 *   ./timsread <file.d> -chrom   -- extract all traces from both
 *                                   chromatography-data.sqlite (run) and
 *                                   chromatography-data-pre.sqlite (pre-run):
 *                                   BPC, TIC, nanoElute flow/pressure/gradient/
 *                                   temperature/valves. One TSV per trace.
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
    if (argc < 2 || argc > 5) {
        std::cerr << "Usage: " << argv[0] << " <file.d> [-ms1] [-sql] [-chrom]" << std::endl;
        std::cerr << "  (no flag)  Extract MS/MS to _msms.mgf"                   << std::endl;
        std::cerr << "  -ms1       Also extract MS1 binary data to _ms1.txt"     << std::endl;
        std::cerr << "  -sql       Dump Frames table from analysis.tdf (no SDK)" << std::endl;
        std::cerr << "  -chrom     Extract all traces from chromatography-data"  << std::endl;
        std::cerr << "             .sqlite and -pre.sqlite (BPC/TIC/flow/etc)"   << std::endl;
        return -1;
    }

    bool extractMS1 = false;
    bool sqlOnly    = false;
    bool chromOnly  = false;
    std::string tdfDirectory;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg == "-ms1")   extractMS1 = true;
        else if (arg == "-sql")   sqlOnly    = true;
        else if (arg == "-chrom") chromOnly  = true;
        else if (arg[0] != '-')   tdfDirectory = arg;
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            return -1;
        }
    }

    if (tdfDirectory.empty()) {
        std::cerr << "Error: TDF directory not specified" << std::endl;
        return -1;
    }

    // Remove trailing slash
    while (!tdfDirectory.empty() &&
           (tdfDirectory.back() == '/' || tdfDirectory.back() == '\\'))
        tdfDirectory.pop_back();

    std::string tdfFile = tdfDirectory + "/analysis.tdf";

    // -sql mode: SQLite only, no SDK required, fast
    if (sqlOnly) {
        std::cout << "Loading frame metadata from " << tdfFile << " ..." << std::endl;
        writeSqlFrames(tdfFile, tdfDirectory + "_frames.txt");
        return 0;
    }

    // -chrom mode: decode chromatography-data.sqlite + pre file
    if (chromOnly) {
        std::cout << "Extracting chromatography traces from " << tdfDirectory << " ..." << std::endl;
        writeChrom(tdfDirectory);
        return 0;
    }

    // Full extraction mode
    try {
        timsdata::TimsData data(tdfDirectory);

        auto calId = data.getCalibrationId();
        std::cout << "# Calibration     : "
                  << (calId.has_value() ? calId.value() : "instrument default") << std::endl;

        sqlite3* db = openDb(tdfFile);
        std::cout << "Loading metadata..." << std::endl;

        auto frames     = loadFrames(db);
        auto precursors = loadPrecursors(db);
        auto pasefData  = loadPasef(db);
        sqlite3_close(db);

        std::string mgfPath = tdfDirectory + "_msms.mgf";
        std::ofstream mgfOutput(mgfPath);
        if (!mgfOutput.is_open()) {
            std::cerr << "Error: cannot open: " << mgfPath << std::endl;
            return -1;
        }

        std::ofstream ms1Output;
        std::string ms1Path;
        if (extractMS1) {
            ms1Path = tdfDirectory + "_ms1.txt";
            ms1Output.open(ms1Path);
            if (!ms1Output.is_open()) {
                std::cerr << "Error: cannot open: " << ms1Path << std::endl;
                return -1;
            }
        }

        std::cout << "# TDF file " << tdfDirectory
                  << " contains " << frames.size() << " frames." << std::endl;
        std::cout << "# Precursors loaded : " << precursors.size() << std::endl;
        std::cout << "# MS/MS frames      : " << pasefData.size()  << std::endl;
        std::cout << "# MS/MS output      : " << mgfPath           << std::endl;
        if (extractMS1)
            std::cout << "# MS1 output        : " << ms1Path        << std::endl;

        mgfOutput << "# MS/MS spectra from TDF file: " << tdfDirectory             << "\n"
                  << "# ZERO-FILTER ONLY - Remove only zero intensity peaks\n"
                  << "# MGF format for protein identification\n\n";

        if (extractMS1)
            ms1Output << "# MS1 spectra from TDF file: " << tdfDirectory           << "\n"
                      << "# Format: Frame_ID RT_seconds Scan_Number m/z Intensity Mobility\n\n";

        processAllFrames(data, frames, precursors, pasefData,
                         mgfOutput, extractMS1 ? &ms1Output : nullptr);

        mgfOutput.close();
        if (extractMS1) ms1Output.close();
        std::cout << "Processing completed!" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
