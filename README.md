# Installation Instructions for timsread

`timsread` is a C++ tool for reading Bruker TimsTOF (TDF) files

```
g++ -O3 -march=native -std=c++17 -I../timsdata_5_0_2/timsdata/include/c   -I../timsdata_5_0_2/timsdata/examples/timsdataSampleCpp/timsdataSampleCpp   -o timsread timsread.cpp   -L../timsdata_5_0_2/timsdata/linux64 -ltimsdata -lsqlite3
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread 230317_SIGRID_10_Slot1-41_1_4086.d
```

## Table of Contents
1. [Download Required SDK and Libraries](#1-download-required-sdk-and-libraries)
2. [Extract Files](#2-extract-files)
3. [Install System Dependencies](#3-install-system-dependencies-on-debianubuntuwsl2)
4. [Set Up Include Paths](#4-set-up-include-paths)
5. [Compile](#5-compile)
6. [Run](#6-run)
#### TBD
7. [Nextflow Pipeline](#7-nextflow-pipeline)
8. [Performance Notes](#performance-notes)
9. [Quality Comparison](#quality-comparison)

## 1. Download Required SDK and Libraries

- **Bruker timsdata SDK**: [TDF-SDK 5_0_2  (~7 MB)](https://www.bruker.com/protected/en/services/software-downloads/mass-spectrometry/raw-data-access-libraries.html?scrollToFormContent=tdf)
    - [Download](https://customer-download-bbio.bruker.com/d/eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE3Nzk2OTQ0MjIsImlhdCI6MTc3OTY5NDQyMiwiZmlsZSI6Ii9CREFML0xTTVMvUmF3RGF0YUFjZXNzTGlicmFyaWVzL3RkZl9zZGtfNS4wL3RpbXNkYXRhXzVfMF8yLnppcCJ9.mnTfmEwByeDYcXx5kG6NPiJi_pD181koZelcJqI04Jg) the file: `timsdata_5_0_2.zip` (or recent?)

## 2. Extract Files

- Extract `timsdata_5_0_2.zip` to a directory, e.g., `../timsdata_5_0_2`.

## 3. Install System Dependencies (on Debian/Ubuntu/WSL2)

```
sudo apt-get update
sudo apt-get install -y g++ sqlite3 libsqlite3-dev
```

## 4. Set Up Include Paths

You will need the following include directories:
- `../timsdata_5_0_2/timsdata/include/c` (contains `timsdata.h`)
- `../timsdata_5_0_2/timsdata/examples/timsdataSampleCpp/timsdataSampleCpp` (contains `timsdata_cpp.h`)

## 5. Compile

From the project directory, run:

```
g++ -O3 -march=native -std=c++17 -I../timsdata_5_0_2/timsdata/include/c   -I../timsdata_5_0_2/timsdata/examples/timsdataSampleCpp/timsdataSampleCpp   -o timsread timsread.cpp   -L../timsdata_5_0_2/timsdata/linux64 -ltimsdata -lsqlite3
```

- Adjust the `../timsdata_5_0_2/timsdata/` paths if your files are located elsewhere.
- On Windows, use the appropriate path format and a compatible compiler (e.g., MSVC).

## 6. Run

The program converts Bruker TDF files to MGF format for MS/MS spectra and optionally extracts MS1 data.

### Usage:

#### Data Sources:
- [PXD045439](https://www.ebi.ac.uk/pride/archive/projects/PXD045439): `230317_SIGRID_10_Slot1-41_1_4086.d`

```bash
# Download example dataset
wget https://ftp.pride.ebi.ac.uk/pride/data/archive/2024/06/PXD045439/230317_SIGRID_10_Slot1-41_1_4086.d.tar
tar xvf 230317_SIGRID_10_Slot1-41_1_4086.d.tar
# sanity check
sudo apt install sqlite3
sqlite3 "230317_SIGRID_10_Slot1-41_1_4086.d/analysis.tdf" "SELECT COUNT(*) as null_mz FROM Precursors WHERE MonoisotopicMz IS NULL; SELECT COUNT(*) as negative_mz FROM Precursors WHERE MonoisotopicMz < 0;"
5314
0
```

**set library path and run:**
```bash
# Extract chromatogram data 
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -chrom 230317_SIGRID_10_Slot1-41_1_4086.d 
# Extract SQL data only (faster)
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -sql 230317_SIGRID_10_Slot1-41_1_4086.d
# Extract SQL data only (default)
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -mgf 230317_SIGRID_10_Slot1-41_1_4086.d
# Extract both MS/MS and MS1 data sampled at 10th frame (little faster)
LD_LIBRARY_PATH=/mnt/z/Download/timsdata-2.21.0.4/timsdata/linux64 ./timsread "230317_SIGRID_10_Slot1-41_1_4086.d" -ms1
# Extract ALL of TDF data
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -tdf 260506_peptid_p10_Slot2-1_1_13559.d
# Extract ALL of TDF_bin data
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -tdfbin 260506_peptid_p10_Slot2-1_1_13559.d
```

**Takes few minutes and expected default MGF output:**
```
time LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread 230317_SIGRID_10_Slot1-41_1_4086.d
# Calibration     : instrument default
Loading metadata...
# TDF file          : 230317_SIGRID_10_Slot1-41_1_4086.d
# Total frames      : 66477
# Precursors loaded : 291844
# MS/MS frames      : 56501
# MGF output        : 230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf
Processing frames...
Progress: 100% (66477/66477) MS1:0 MS2:56501 
Total frames processed : 66477
MS1 frames             : 0
MS/MS frames           : 56501
Skipped (invalid m/z)  : 8268
MGF extraction completed.

real    6m47.663s
user    3m54.451s
sys     0m16.221s
```


**Expected output with `-ms1` switch:**
```
time LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread2 -ms1 230317_SIGRID_10_Slot1-41_1_4086.d
# Calibration     : instrument default
Loading metadata...
# TDF file          : 230317_SIGRID_10_Slot1-41_1_4086.d
# Total frames      : 66477
# Precursors loaded : 291844
# MS/MS frames      : 56501
# MGF output        : 230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf
# MS1 output        : 230317_SIGRID_10_Slot1-41_1_4086.d_ms1.txt
Processing frames...
Progress: 100% (66477/66477) MS1:9975 MS2:56501 
Total frames processed : 66477
MS1 frames             : 9976
MS/MS frames           : 56501
Skipped (invalid m/z)  : 8268
MGF extraction completed.

real    16m24.490s
user    11m0.906s
sys     0m38.002s
```

### Output Files:
- **`*.d_msms.mgf`**: MS/MS spectra in MGF format for protein identification
- **`*.d_ms1.txt`**: MS1 spectra (if `-ms1` flag used) with format: `Frame_ID RT_seconds Scan_Number m/z Intensity Mobility`


**Expected output with `-tdf` switch:**
```
time LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread2 -tdf 230317_SIGRID_10_Slot1-41_1_4086.d
Dumping all TDF tables from 230317_SIGRID_10_Slot1-41_1_4086.d ...

--- analysis.tdf tables (230317_SIGRID_10_Slot1-41_1_4086.d/analysis.tdf) ---
  Found view: Properties
  Found table: CalibrationInfo
  Found table: CollisionEnergySweepingInfo
  Found table: DiaFrameMsMsInfo
  Found table: DiaFrameMsMsWindowGroups
  Found table: DiaFrameMsMsWindows
  Found table: ErrorLog
  Found table: FrameMsMsInfo
  Found table: FrameProperties
  Found table: Frames
  Found table: GlobalMetadata
  Found table: GroupProperties
  Found table: MzCalibration
  Found table: PasefFrameMsMsInfo
  Found table: Precursors
  Found table: PrmFrameMeasurementMode
  Found table: PrmFrameMsMsInfo
  Found table: PrmTargets
  Found table: PropertyDefinitions
  Found table: PropertyGroups
  Found table: Segments
  Found table: TimsCalibration

Dumping 22 tables/views...
  Properties                        28651587 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_Properties.txt
  CalibrationInfo                         27 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_CalibrationInfo.txt
  CollisionEnergySweepingInfo              0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_CollisionEnergySweepingInfo.txt
  DiaFrameMsMsInfo                         0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_DiaFrameMsMsInfo.txt
  DiaFrameMsMsWindowGroups                 0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_DiaFrameMsMsWindowGroups.txt
  DiaFrameMsMsWindows                      0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_DiaFrameMsMsWindows.txt
  ErrorLog                                 0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_ErrorLog.txt
  FrameMsMsInfo                            0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_FrameMsMsInfo.txt
  FrameProperties                    9145262 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_FrameProperties.txt
  Frames                               66477 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_Frames.txt
  GlobalMetadata                          36 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_GlobalMetadata.txt
  GroupProperties                        267 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_GroupProperties.txt
  MzCalibration                            1 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_MzCalibration.txt
  PasefFrameMsMsInfo                  542469 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PasefFrameMsMsInfo.txt
  Precursors                          291844 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_Precursors.txt
  PrmFrameMeasurementMode                  0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PrmFrameMeasurementMode.txt
  PrmFrameMsMsInfo                         0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PrmFrameMsMsInfo.txt
  PrmTargets                               0 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PrmTargets.txt
  PropertyDefinitions                    431 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PropertyDefinitions.txt
  PropertyGroups                           1 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_PropertyGroups.txt
  Segments                                 1 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_Segments.txt
  TimsCalibration                          1 rows -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_TimsCalibration.txt

--- analysis.tdf_bin SDK calibration ---
  Frame 1 (MS1)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame1_MS1.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame1_MS1.txt
  Frame 8 (MS1)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame8_MS1.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame8_MS1.txt
  Frame 19 (MS1)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame19_MS1.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame19_MS1.txt
  Frame 2 (MS2)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame2_MS2.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame2_MS2.txt
  Frame 3 (MS2)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame3_MS2.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame3_MS2.txt
  Frame 4 (MS2)  mz_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_mz_frame4_MS2.txt
             1k0_calib -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_1k0_frame4_MS2.txt
  Calibration summary -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdf/230317_SIGRID_10_Slot1-41_1_4086.d_calib_summary.txt

Output directory: 230317_SIGRID_10_Slot1-41_1_4086.d_tdf

real    3m54.514s
user    1m4.579s
sys     0m10.615s
```

**`-tdfbin` switch:**
note that `timsread` warns about the sizes, default is to NOT proceed, press `y` to continue...
```
time LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread2 -tdfbin 230317_SIGRID_10_Slot1-41_1_4086.d

=== analysis.tdf_bin full extraction ===
  MS1 : 9976 frames, 2183426604 peaks  ->  ~131.0 GB  (250 chunk(s))
  MS2 : 56501 frames, 302769315 peaks  ->  ~18.2 GB  (35 chunk(s))
  Total                     ->  ~149.2 GB

  Output: 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/
  Format: Frame_ID  RT_seconds  MsMsType  Scan  mz  Intensity  1_over_K0

Proceed? This will write ~149.2 GB to disk. [y/N] y
# Calibration: instrument default
Extracting...
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk001.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk001.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk002.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk003.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk004.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk005.txt
  3% (2000/66477)  written: 37993500 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk006.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk007.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk008.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk009.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk010.txt
  6% (4000/66477)  written: 87909012 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk011.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk012.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk013.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk014.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk015.txt
  9% (6000/66477)  written: 137840553 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk002.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk016.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk017.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk018.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk019.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk020.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk021.txt
  12% (8000/66477)  written: 195101362 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk003.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk022.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk023.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk024.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk025.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk026.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk027.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk004.txt
  15% (10000/66477)  written: 260785761 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk028.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk029.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk030.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk031.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk032.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk005.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk033.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk034.txt
  18% (12000/66477)  written: 334044735 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk035.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk036.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk037.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk006.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk038.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk039.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk040.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk041.txt
  21% (14000/66477)  written: 413693242 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk042.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk043.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk007.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk044.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk045.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk046.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk047.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk048.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk008.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk049.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk050.txt
  24% (16000/66477)  written: 500062311 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk051.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk052.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk053.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk009.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk054.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk055.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk056.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk057.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk058.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk059.txt
  27% (18000/66477)  written: 595191853 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk010.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk060.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk061.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk062.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk063.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk064.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk011.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk065.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk066.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk067.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk068.txt
  30% (20000/66477)  written: 693250874 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk069.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk012.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk070.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk071.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk072.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk073.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk074.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk075.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk013.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk076.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk077.txt
  33% (22000/66477)  written: 790609480 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk078.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk079.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk080.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk014.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk081.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk082.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk083.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk084.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk085.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk086.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk015.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk087.txt
  36% (24000/66477)  written: 893505174 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk088.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk089.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk090.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk091.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk016.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk092.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk093.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk094.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk095.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk096.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk017.txt
  39% (26000/66477)  written: 996375270 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk097.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk098.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk099.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk100.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk101.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk018.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk102.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk103.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk104.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk105.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk106.txt
  42% (28000/66477)  written: 1102324455 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk107.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk019.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk108.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk109.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk110.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk111.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk112.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk020.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk113.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk114.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk115.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk116.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk117.txt
  45% (30000/66477)  written: 1212696272 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk021.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk118.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk119.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk120.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk121.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk122.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk123.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk022.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk124.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk125.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk126.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk127.txt
  48% (32000/66477)  written: 1322887796 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk128.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk023.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk129.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk130.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk131.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk132.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk133.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk134.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk024.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk135.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk136.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk137.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk138.txt
  51% (34000/66477)  written: 1435017981 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk139.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk140.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk025.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk141.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk142.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk143.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk144.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk145.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk026.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk146.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk147.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk148.txt
  54% (36000/66477)  written: 1540412319 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk149.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk150.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk151.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk027.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk152.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk153.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk154.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk155.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk156.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk157.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk028.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk158.txt
  57% (38000/66477)  written: 1644256584 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk159.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk160.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk161.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk162.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk163.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk029.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk164.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk165.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk166.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk167.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk168.txt
  60% (40000/66477)  written: 1754867096 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk169.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk030.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk170.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk171.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk172.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk173.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk174.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk175.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk176.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk031.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk177.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk178.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk179.txt
  63% (42000/66477)  written: 1863583404 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk180.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk181.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk182.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk183.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk032.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk184.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk185.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk186.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk187.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk188.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk189.txt
  66% (44000/66477)  written: 1966560084 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk190.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk191.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk192.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk193.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk194.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk033.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk195.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk196.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk197.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk198.txt
  69% (46000/66477)  written: 2056741779 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk199.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk200.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk201.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk202.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk203.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk204.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk205.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk206.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk034.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk207.txt
  72% (48000/66477)  written: 2137333048 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk208.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk209.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk210.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk211.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk212.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk213.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk214.txt
  75% (50000/66477)  written: 2205757158 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk215.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk216.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk217.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk218.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk219.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk220.txt
  78% (52000/66477)  written: 2262182638 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk221.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk222.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk223.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk224.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk225.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms2_chunk035.txt
  81% (54000/66477)  written: 2308207166 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk226.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk227.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk228.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk229.txt
  84% (56000/66477)  written: 2345439571 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk230.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk231.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk232.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk233.txt
  87% (58000/66477)  written: 2380134380 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk234.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk235.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk236.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk237.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk238.txt
  90% (60000/66477)  written: 2423251002 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk239.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk240.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk241.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk242.txt
  93% (62000/66477)  written: 2463280687 peaks     -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk243.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk244.txt
  -> 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/230317_SIGRID_10_Slot1-41_1_4086.d_ms1_chunk245.txt
  100% (66477/66477)  written: 2486195849 peaks   

Done.
  Total peaks written : 2486195919
  MS1 chunks          : 245
  MS2 chunks          : 35
  Output              : 230317_SIGRID_10_Slot1-41_1_4086.d_tdfbin/

real    90m7.122s
user    69m55.285s
sys     1m3.252s
```

# TBD

## 7. Nextflow Pipeline

This repository includes a Nextflow pipeline (`nextflow.nf`) for automated processing of TDF files using the `timsread` tool.

### Prerequisites for Nextflow Pipeline:
- [Nextflow](https://www.nextflow.io/) installed
- Bruker timsdata SDK available at `timsdata/linux64`
- Compiled `timsread` executable in the project directory

### Pipeline Usage:

```bash
# Extract MS/MS spectra only (default, faster)
nextflow run nextflow.nf --input 230317_SIGRID_10_Slot1-41_1_4086.d

# Extract both MS/MS and MS1 spectra
nextflow run nextflow.nf --input 230317_SIGRID_10_Slot1-41_1_4086.d --ms2_only False

# Custom output directory
nextflow run nextflow.nf --input <tdf_directory> --publishdir custom_output
```

### Pipeline Features:
- **Automated Processing**: Handles library path configuration and tool execution
- **Scalable**: Can process multiple TDF files in parallel
- **Flexible Output**: Optional MS1 extraction with `--ms2_only False`
- **Quality Control**: Generates summary statistics for processed files
- **Resume Capability**: Use `-resume` to continue interrupted runs

### Pipeline Output:
- **MGF Files**: MS/MS spectra ready for protein identification
- **MS1 Files**: Optional MS1 spectra data (if requested)
- **Summary Report**: `results_file.tsv` with processing statistics

### Example Pipeline Run:
```bash
# Download and extract test dataset
wget https://ftp.pride.ebi.ac.uk/pride/data/archive/2024/06/PXD045439/230317_SIGRID_10_Slot1-41_1_4086.d.tar
tar xf 230317_SIGRID_10_Slot1-41_1_4086.d.tar

# Run Nextflow pipeline
nextflow run nextflow.nf --input 230317_SIGRID_10_Slot1-41_1_4086.d

# Check results
ls nf_output/
# Output: 230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf  results_file.tsv
```

## Notes
- Ensure all required header and source files are present in the specified directories.
- If you encounter missing dependencies, verify the include paths and that all SDK/library files are extracted.
- The program automatically skips precursors with invalid (NULL) m/z values from the database.
- Use `-ms1` flag only when needed as MS1 extraction significantly increases processing time and output file size.
- **For automated processing**: Use the included Nextflow pipeline (see [Section 7](#7-nextflow-pipeline)) which handles library paths and batch processing automatically.
> If you see an error about `libtimsdata.so` not being found, you must set the `LD_LIBRARY_PATH` environment variable to the directory containing `libtimsdata.so` as shown above. This only affects the current terminal session.

## Performance Notes

### Technical Details:
- **Zero-filter approach**: Removes only zero-intensity peaks for maximum data retention
- **Optimized extraction**: ~83% more spectra than Bruker's default export
- **Batch processing**: Efficient handling of large datasets (66K+ frames)
- **Memory management**: 4MB I/O buffers with periodic flushing 

## Quality Comparison
Our timsread extraction finds **~83% more spectra** than Bruker's default MGF export due to:
- More comprehensive PASEF precursor extraction
- Less aggressive intensity filtering (zero-filter only)
- Complete processing of all MS/MS frames and precursors

Example comparison:
- Bruker export: 291,843 spectra
- timsread extraction: 534,186 spectra (+83% more data)


### why?

```
grep -A 30 "PEPMASS=1221.98873 17379" "230317_SIGRID_10_Slot1-41_1_4086.d/230317_SIGRID_10_Slot1-41_1_4086_6.0.313.mgf"
grep -A 50 "PEPMASS=1221.988770" "230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf"
```

>Key Differences in Ion Handling? Bruker's Export (1 spectrum):
```
PEPMASS=1221.98873 17379
```
- 15 peaks total
- Peak at 1221.98649: 11508 intensity
- Peak at 1222.99170: 4364 intensity
- Clean, merged spectrum

Our timsread (Multiple spectra for same m/z):
```
PEPMASS=1221.988770 28038  (First occurrence)
PEPMASS=1221.988770 10418  (Second occurrence) 
PEPMASS=1221.988770 10418  (Third occurrence)
PEPMASS=1221.988770 21162  (Fourth occurrence)
PEPMASS=1221.988770 3872   (Fifth occurrence)
PEPMASS=1221.988770 3872   (Sixth occurrence)
PEPMASS=1221.988770 10789  (Seventh occurrence)
PEPMASS=1221.988770 10789  (Eighth occurrence)
PEPMASS=1221.988770 18410  (Ninth occurrence)
```

#### Critical Discovery:
Bruker merges multiple PASEF precursors with the same m/z into a single spectrum, while our method creates separate spectra for each PASEF precursor instance. This probably explains:

- Why we have 83% more spectra - We're not merging duplicate m/z precursors
- Different intensity values - Each instance has its own intensity
- Duplicate peaks - Same m/z fragments appear multiple times across different scans
##### Bruker's Approach:
- Groups precursors by m/z
- Merges peaks from multiple PASEF instances
- Results in fewer, but "cleaner" spectra
##### Our Approach:
- Each PASEF precursor gets its own spectrum
- Preserves scan-level detail and timing information
- More comprehensive but with redundancy

## License

This repository is a fork of [gtluu/timsconvert](https://github.com/gtluu/timsconvert),
originally licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).

All original code is © the original authors and remains under Apache 2.0.

Modifications and additional contributions by [Animesh Sharma](https://github.com/animesh)  
are © 2023–2025 and are available under the same Apache 2.0 license.  
To the extent possible, Animesh Sharma’s contributions may also be reused under the MIT License.  
See [`NOTICE`](./NOTICE) and [`LICENSE-ANIMESH.md`](./LICENSE-ANIMESH.md) for details.
