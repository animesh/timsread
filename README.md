# Installation Instructions for timsread

`timsread` is a C++ tool for reading Bruker TimsTOF (TDF) files

```
g++ -O3 -march=native -std=c++17 -I../timsdata_5_0_2/timsdata/include/c   -I../timsdata_5_0_2/timsdata/examples/timsdataSampleCpp/timsdataSampleCpp   -o timsread timsread.cpp   -L../timsdata_5_0_2/timsdata/linux64 -ltimsdata -lsqlite3
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -sql 230317_SIGRID_10_Slot1-41_1_4086.d
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
# Extract SQL data only (faster)
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread -sql 230317_SIGRID_10_Slot1-41_1_4086.d
# Extract SQL data only (default)
LD_LIBRARY_PATH=../timsdata_5_0_2/timsdata/linux64 ./timsread 230317_SIGRID_10_Slot1-41_1_4086.d
# Extract both MS/MS and MS1 data
LD_LIBRARY_PATH=/mnt/z/Download/timsdata-2.21.0.4/timsdata/linux64 ./timsread "230317_SIGRID_10_Slot1-41_1_4086.d" -ms1
```

**Takes about couple of minutes and expected default output:**
```
Loading metadata...
# TDF file 230317_SIGRID_10_Slot1-41_1_4086.d contains 66477 frames.
# Loaded 291844 precursors and 56501 MS/MS frames.
# MS/MS data written to: 230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf

Processing frames...
Progress: 100% (66477/66477) MS1:0 MS2:56501     
Total frames processed: 66477
MS1 frames: 0
MS/MS frames: 56501
Skipped precursors with invalid m/z: 8268
Processing completed!
```

**Expected output with `-ms1` switch:**

```
LD_LIBRARY_PATH=/mnt/z/Download/timsdata-2.21.0.4/timsdata/linux64 ./timsread "230317_SIGRID_10_Slot1-41_1_4086.d" -ms1
Loading metadata...
# TDF file 230317_SIGRID_10_Slot1-41_1_4086.d contains 66477 frames.
# Loaded 291844 precursors and 56501 MS/MS frames.
# MS/MS data written to: 230317_SIGRID_10_Slot1-41_1_4086.d_msms.mgf
# MS1 data written to: 230317_SIGRID_10_Slot1-41_1_4086.d_ms1.txt

Processing frames...
Progress: 100% (66477/66477) MS1:9975 MS2:56501     
Total frames processed: 66477
MS1 frames: 9976
MS/MS frames: 56501
Skipped precursors with invalid m/z: 8268
Processing completed!
```

### Output Files:
- **`*.d_msms.mgf`**: MS/MS spectra in MGF format for protein identification
- **`*.d_ms1.txt`**: MS1 spectra (if `-ms1` flag used) with format: `Frame_ID RT_seconds Scan_Number m/z Intensity Mobility`

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
