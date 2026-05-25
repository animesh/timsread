/*
 * timsread.cpp
 *
 * High-performance Bruker timsTOF TDF -> MGF/MS1 converter.
 *
 * Usage:
 *   ./timsread <file.d>          -- extract MS/MS to .mgf
 *   ./timsread <file.d> -ms1     -- also extract MS1 binary data to _ms1.txt
 *   ./timsread <file.d> -sql     -- fast: dump frame RT/intensity table from
 *                                   SQLite only, no SDK binary read needed.
 *                                   Use for BPC/TIC plots via timsplot.py.
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
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " <file.d> [-ms1] [-sql]" << std::endl;
        std::cerr << "  (no flag)  Extract MS/MS to _msms.mgf"         << std::endl;
        std::cerr << "  -ms1       Also extract MS1 binary data"        << std::endl;
        std::cerr << "  -sql       Fast: frame RT/intensity from SQLite only" << std::endl;
        return -1;
    }

    bool extractMS1 = false;
    bool sqlOnly    = false;
    std::string tdfDirectory;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg == "-ms1")  extractMS1 = true;
        else if (arg == "-sql")  sqlOnly    = true;
        else if (arg[0] != '-')  tdfDirectory = arg;
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
