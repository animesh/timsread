import os
import sqlite3
import ctypes
import struct
from collections import defaultdict

import numpy as np

# ============================================================
# CONFIG
# ============================================================

TDF_DIR = r"230317_SIGRID_10_Slot1-41_1_4086.d"
#SDK_DLL = r"..\\timsdata_5_0_2\\timsdata/win64\\timsdata.dll"
SDK_DLL = r"..\\timsdata-2.21.0.4\\timsdata\\win64\\timsdata.dll"
OUTPUT_MGF = "230317_SIGRID_10_Slot1-41_1_4086.consensus_output.mgf"


# ============================================================
# SDK LOAD
# ============================================================

sdk = ctypes.cdll.LoadLibrary(SDK_DLL)

MSMS_CALLBACK = ctypes.CFUNCTYPE(
    None,
    ctypes.c_int64,
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_float),
)

sdk.tims_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
sdk.tims_open.restype = ctypes.c_uint64

sdk.tims_close.argtypes = [ctypes.c_uint64]
sdk.tims_close.restype = None

sdk.tims_read_pasef_msms.argtypes = [
    ctypes.c_uint64,
    ctypes.POINTER(ctypes.c_int64),
    ctypes.c_uint32,
    MSMS_CALLBACK
]
sdk.tims_read_pasef_msms.restype = ctypes.c_uint32

# ============================================================
# OPEN TDF
# ============================================================

handle = sdk.tims_open(TDF_DIR.encode(), 0)
if handle == 0:
    raise RuntimeError("Failed to open TDF")

db = sqlite3.connect(os.path.join(TDF_DIR, "analysis.tdf"))
cur = db.cursor()

# ============================================================
# PRECURSORS (SAFE SCHEMA)
# ============================================================

cur.execute("""
SELECT
    p.Id,
    COALESCE(p.MonoisotopicMz, p.AverageMz, p.LargestPeakMz),
    p.Charge,
    p.Intensity,
    p.Parent
FROM Precursors p
ORDER BY p.Id
""")

precursors = cur.fetchall()
print("precursors:", len(precursors))

# ============================================================
# FRAMES (ONLY VALID RT SOURCE)
# ============================================================

cur.execute("""
SELECT Id, Time
FROM Frames
""")

frame_rt = {r[0]: r[1] for r in cur.fetchall()}

# ============================================================
# STORAGE
# ============================================================

spectra = {}

@MSMS_CALLBACK
def cb(pid, npeaks, mz_ptr, int_ptr):
    mzs = np.ctypeslib.as_array(mz_ptr, shape=(npeaks,)).copy()
    intens = np.ctypeslib.as_array(int_ptr, shape=(npeaks,)).copy()
    spectra[int(pid)] = (mzs, intens)

# ============================================================
# HELPERS
# ============================================================

def clean_spectrum(mzs, intens):
    mask = intens > 0
    mzs = mzs[mask]
    intens = intens[mask]

    if len(mzs) == 0:
        return mzs, intens

    order = np.argsort(mzs)
    return mzs[order], intens[order]

# ============================================================
# RUN
# ============================================================

written = 0

with open(OUTPUT_MGF, "w") as f:

    for pid, mono, charge, inten, frame_id in precursors:

        spectra.clear()

        arr = (ctypes.c_int64 * 1)(pid)

        sdk.tims_read_pasef_msms(handle, arr, 1, cb)

        if pid not in spectra:
            continue

        mzs, intens = spectra[pid]

        mzs, intens = clean_spectrum(mzs, intens)

        if len(mzs) == 0:
            continue

        # ====================================================
        # NO re-clustering (critical correctness fix)
        # ====================================================

        # ====================================================
        # RT (correct vendor-consistent source)
        # ====================================================
        rt = frame_rt.get(frame_id, 0.0)

        # ====================================================
        # WRITE MGF
        # ====================================================

        f.write("BEGIN IONS\n")
        f.write(f"TITLE=Precursor_{pid}\n")
        f.write(f"RTINSECONDS={rt:.6f}\n")

        if mono is None:
            mono = 0.0

        f.write(f"PEPMASS={mono:.6f} {inten if inten else 0:.0f}\n")

        if charge and charge > 0:
            f.write(f"CHARGE={charge}+\n")

        for m, i in zip(mzs, intens):
            f.write(f"{m:.6f} {int(round(i))}\n")

        f.write("END IONS\n\n")

        written += 1

print("written:", written)

sdk.tims_close(handle)
db.close()