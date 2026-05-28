#!/usr/bin/env python3
"""
timsrerun.py - MZmine-style timsTOF visualization in Rerun (rerun-sdk >= 0.23)

Panels (matching MZmine raw data overview):
  BPC             : base peak chromatogram  (RT vs MaxIntensity)
  TIC             : total ion chromatogram  (RT vs SummedIntensities)
  Frame heatmap   : m/z vs 1/K0, intensity as colour, animated over RT
  Mobilogram      : 1/K0 vs summed intensity per RT (line)
  Summed spectrum : m/z vs summed intensity per RT (line)

Data sources:
  -sql     : BPC + TIC from _frames.txt                      (instant)
  -ms1     : + frame heatmap/mobilogram/spectrum from _ms1.txt (~min)
  -tdfbin  : same but full-resolution from _tdfbin/ chunks

Install:  pip install rerun-sdk pandas numpy

Usage:
  python timsrerun.py run.d -sql                 # chromatograms only
  python timsrerun.py run.d -sql -ms1            # recommended start
  python timsrerun.py run.d -tdfbin --topn 500   # full 5D
  python timsrerun.py run.d -ms1 --save out.rrd  # save for later

Controls in viewer:
  - Drag timeline scrubber to see frame heatmap change over RT
  - Press play to animate
  - Double-click heatmap to zoom
"""

import sys, os, argparse, glob
import numpy as np
import pandas as pd
import rerun as rr
import rerun.blueprint as rrb

# ---------------------------------------------------------------------------
# Heatmap helpers
# ---------------------------------------------------------------------------

MZ_MIN,  MZ_MAX,  MZ_BINS  = 200.0, 1700.0, 500
MOB_MIN, MOB_MAX, MOB_BINS = 0.60,  1.80,   200

# Dilation kernel size: each peak becomes DILATE x DILATE pixels.
# MZmine uses full IMS; -ms1 samples every 10th scan so we enlarge to compensate.
_DILATE = 5   # pixels; increase to 7 if peaks still invisible

def make_heatmap_rgb(mz: np.ndarray, mob: np.ndarray,
                     inten: np.ndarray) -> np.ndarray:
    """
    Bin peaks into (MOB_BINS, MZ_BINS) grid.
    - Black background (t=0 -> RGB 0,0,0)
    - Plasma colourmap: purple -> red -> yellow (matches MZmine's default LUT)
    - Peaks dilated by _DILATE pixels so they are visible at full zoom-out
    """
    from scipy.ndimage import maximum_filter

    img = np.zeros((MOB_BINS, MZ_BINS), dtype=np.float32)
    if len(mz) == 0:
        return np.zeros((MOB_BINS, MZ_BINS, 3), dtype=np.uint8)

    xi = np.clip(
        ((mz  - MZ_MIN)  / (MZ_MAX  - MZ_MIN)  * MZ_BINS).astype(np.int32),
        0, MZ_BINS  - 1)
    yi = np.clip(
        ((mob - MOB_MIN) / (MOB_MAX - MOB_MIN) * MOB_BINS).astype(np.int32),
        0, MOB_BINS - 1)

    np.maximum.at(img, (yi, xi), inten)

    # Dilate so single-pixel peaks become visible
    img = maximum_filter(img, size=_DILATE)

    mx = img.max()
    if mx <= 0:
        return np.zeros((MOB_BINS, MZ_BINS, 3), dtype=np.uint8)

    t = np.log1p(img) / np.log1p(mx)  # 0-1 log-normalised

    # Plasma colormap: guaranteed black at t=0, yellow-white at t=1
    r = np.clip(t * 3.0 - 0.5,       0, 1)   # fires above t~0.17
    g = np.clip(t * 2.5 - 1.2,       0, 1)   # fires above t~0.48
    b = np.clip(0.8 - t * 1.5,       0, 1)   # fades out as t rises
    # Hard mask: any bin with zero raw intensity stays black
    zero = img == 0
    r[zero] = 0; g[zero] = 0; b[zero] = 0

    # Flip vertically so low 1/K0 = bottom of image (matching MZmine)
    r = np.flipud(r); g = np.flipud(g); b = np.flipud(b)

    return np.stack(
        [(r * 255).astype(np.uint8),
         (g * 255).astype(np.uint8),
         (b * 255).astype(np.uint8)], axis=2)


def make_mobilogram(mob: np.ndarray, inten: np.ndarray) -> tuple:
    """Returns (mob_axis, summed_intensity) binned to MOB_BINS."""
    y = np.zeros(MOB_BINS, dtype=np.float64)
    if len(mob) == 0:
        return np.linspace(MOB_MIN, MOB_MAX, MOB_BINS), y
    yi = np.clip(
        ((mob - MOB_MIN) / (MOB_MAX - MOB_MIN) * MOB_BINS).astype(np.int32),
        0, MOB_BINS - 1)
    np.add.at(y, yi, inten)
    return np.linspace(MOB_MIN, MOB_MAX, MOB_BINS), y


def make_spectrum(mz: np.ndarray, inten: np.ndarray) -> tuple:
    """Returns (mz_axis, summed_intensity) binned to MZ_BINS."""
    y = np.zeros(MZ_BINS, dtype=np.float64)
    if len(mz) == 0:
        return np.linspace(MZ_MIN, MZ_MAX, MZ_BINS), y
    xi = np.clip(
        ((mz - MZ_MIN) / (MZ_MAX - MZ_MIN) * MZ_BINS).astype(np.int32),
        0, MZ_BINS - 1)
    np.add.at(y, xi, inten)
    return np.linspace(MZ_MIN, MZ_MAX, MZ_BINS), y


# ---------------------------------------------------------------------------
# Log one frame's heatmap + mobilogram + spectrum at a given RT
# ---------------------------------------------------------------------------

def log_frame(rt: float, mz: np.ndarray, mob: np.ndarray,
              inten: np.ndarray) -> None:
    rr.set_time("rt_seconds", duration=rt)

    # Frame heatmap (m/z vs 1/K0)
    rgb = make_heatmap_rgb(mz, mob, inten)
    rr.log("frame_heatmap",
           rr.Image(rgb,
                    # Map pixel (col, row) -> (m/z, 1/K0) for axis labels in viewer
                    draw_order=0.0))

    # Mobilogram: 1/K0 on Y, intensity on X (matching MZmine orientation)
    mob_ax, mob_i = make_mobilogram(mob, inten)
    # Points2D: (x=intensity, y=1/K0)
    valid = mob_i > 0
    if valid.any():
        pts_mob = np.column_stack([mob_i[valid],
                                   mob_ax[valid]]).astype(np.float32)
        rr.log("mobilogram", rr.Points2D(pts_mob, radii=1.5))

    # Summed spectrum: m/z on X, intensity on Y
    mz_ax, spec_i = make_spectrum(mz, inten)
    valid = spec_i > 0
    if valid.any():
        pts_spec = np.column_stack([mz_ax[valid],
                                    spec_i[valid]]).astype(np.float32)
        rr.log("spectrum", rr.Points2D(pts_spec, radii=1.5))


# ---------------------------------------------------------------------------
# BPC / TIC from _frames.txt  (-sql output)
# Also reads chromatography-data.sqlite for the IMS-accumulated BPC (Trace 18)
# which matches MZmine. Falls back to Frames.MaxIntensity with a warning.
# ---------------------------------------------------------------------------

def _read_chrom_trace(d_path: str, trace_id: int) -> tuple | None:
    """
    Read a single trace from chromatography-data.sqlite inside the .d directory.
    Returns (rt_seconds, intensity) arrays, or None if file not found.
    Blob encoding: Times=float64, Intensities=float32.
    """
    import sqlite3
    chrom_db = os.path.join(d_path, "chromatography-data.sqlite")
    if not os.path.exists(chrom_db):
        return None
    conn = sqlite3.connect(f"file:{chrom_db}?mode=ro", uri=True)
    rows = conn.execute(
        "SELECT Times, Intensities FROM TraceChunks WHERE Trace=? ORDER BY rowid",
        (trace_id,)
    ).fetchall()
    conn.close()
    if not rows:
        return None
    all_t, all_i = [], []
    for t_blob, i_blob in rows:
        all_t.extend(np.frombuffer(t_blob, dtype=np.float64))
        all_i.extend(np.frombuffer(i_blob, dtype=np.float32))
    return np.array(all_t), np.array(all_i)


def _read_frame_metadata(tdf_path: str) -> pd.DataFrame:
    """
    Read Frames + first Precursor m/z per frame from analysis.tdf.
    Returns DataFrame with Frame_ID, RT_seconds, MsMsType, MaxIntensity,
    SummedIntensities, NumScans, NumPeaks, PrecursorMz (nullable).
    """
    import sqlite3
    conn = sqlite3.connect(f"file:{tdf_path}?mode=ro", uri=True)
    frames = pd.read_sql(
        "SELECT Id AS Frame_ID, Time AS RT_seconds, MsMsType, "
        "MaxIntensity, SummedIntensities, NumScans, NumPeaks "
        "FROM Frames ORDER BY Id;", conn)

    # First precursor m/z per MS2 frame (for scan table metadata)
    try:
        prec = pd.read_sql(
            "SELECT p.Id, p.MonoisotopicMz, p.Charge, "
            "       pf.Frame AS Frame_ID "
            "FROM Precursors p "
            "JOIN PasefFrameMsMsInfo pf ON p.Id = pf.Precursor "
            "GROUP BY pf.Frame "
            "HAVING pf.Precursor = MIN(pf.Precursor);", conn)
        frames = frames.merge(
            prec[["Frame_ID","MonoisotopicMz","Charge"]],
            on="Frame_ID", how="left")
    except Exception:
        frames["MonoisotopicMz"] = np.nan
        frames["Charge"]         = np.nan

    conn.close()
    return frames


def log_chromatogram(frames_txt: str, d_path: str) -> float:
    """
    Log BPC, TIC, and per-frame scan metadata.
    Returns rt_offset (seconds) so downstream callers can normalise to t=0.
    """
    print(f"Loading: {frames_txt}")
    df  = pd.read_csv(frames_txt, sep="\t", comment="#")
    ms1 = df[df["MsMsType"] == 0].sort_values("RT_seconds")

    rt_raw = ms1["RT_seconds"].values.astype(np.float64)
    rt_offset = float(rt_raw[0])           # first data point = t=0
    rt = rt_raw - rt_offset                # normalise to 0-based

    # --- BPC ---
    # Prefer chromatography-data.sqlite Trace 18 (IMS-accumulated BPC,
    # matches MZmine). Falls back to Frames.MaxIntensity with a warning.
    bpc_source = "Frames.MaxIntensity (NOT IMS-accumulated; ~41x lower than MZmine)"
    bpc = ms1["MaxIntensity"].values.astype(np.float64)

    chrom = _read_chrom_trace(d_path, trace_id=18)   # BPC +-MS
    if chrom is not None:
        chrom_t, chrom_bpc = chrom
        # Interpolate chrom BPC onto our MS1 frame RT grid
        chrom_t_norm = chrom_t - chrom_t[0]
        bpc = np.interp(rt, chrom_t_norm, chrom_bpc.astype(np.float64))
        bpc_source = "chromatography-data.sqlite Trace 18 (IMS-accumulated BPC)"

    # --- TIC ---
    tic_source = "Frames.SummedIntensities"
    tic = ms1["SummedIntensities"].values.astype(np.float64)

    chrom_tic = _read_chrom_trace(d_path, trace_id=20)   # TIC +-MS
    if chrom_tic is not None:
        tic_t, tic_i = chrom_tic
        tic_t_norm = tic_t - tic_t[0]
        tic = np.interp(rt, tic_t_norm, tic_i.astype(np.float64))
        tic_source = "chromatography-data.sqlite Trace 20 (TIC +-MS)"

    print(f"  BPC source : {bpc_source}")
    print(f"  TIC source : {tic_source}")
    print(f"  Frames: {len(rt)}  RT: 0 - {rt[-1]/60:.1f} min")

    # Style hints
    rr.log("chromatogram/BPC",
           rr.SeriesLines(colors=[0, 180, 255], widths=1.0), static=True)
    rr.log("chromatogram/TIC",
           rr.SeriesLines(colors=[255, 140, 0], widths=1.0), static=True)

    rr.send_columns("chromatogram/BPC",
        indexes=[rr.TimeColumn("rt_seconds", duration=rt)],
        columns=[*rr.Scalars.columns(scalars=bpc)])
    rr.send_columns("chromatogram/TIC",
        indexes=[rr.TimeColumn("rt_seconds", duration=rt)],
        columns=[*rr.Scalars.columns(scalars=tic)])

    # --- Per-frame scan metadata as TextDocument ---
    # Read full metadata (all frames, not just MS1) from analysis.tdf
    tdf_path = os.path.join(d_path, "analysis.tdf")
    if os.path.exists(tdf_path):
        all_frames = _read_frame_metadata(tdf_path)
        all_frames["RT_norm"] = all_frames["RT_seconds"] - rt_offset
        all_frames["RT_min"]  = (all_frames["RT_seconds"] / 60).round(3)
        all_frames["MSn"]     = all_frames["MsMsType"].apply(
            lambda x: 1 if x == 0 else 2)

        # Sample every 5th frame to keep log size manageable
        sampled = all_frames.iloc[::5]
        rts_meta = sampled["RT_norm"].values.astype(np.float64)

        texts = []
        for _, r in sampled.iterrows():
            prec = f"{r['MonoisotopicMz']:.4f}" if pd.notna(r.get("MonoisotopicMz")) else "-"
            chg  = f"{int(r['Charge'])}+" if pd.notna(r.get("Charge")) else "-"
            texts.append(
                f"Scan {int(r['Frame_ID'])}  "
                f"RT {r['RT_min']} min  "
                f"MS{int(r['MSn'])}  "
                f"BPeak {r['MaxIntensity']:.2E}  "
                f"Prec {prec} ({chg})"
            )

        rr.send_columns("scan_metadata",
            indexes=[rr.TimeColumn("rt_seconds", duration=rts_meta)],
            columns=[*rr.TextDocument.columns(text=texts)])

        print(f"  Scan metadata: {len(sampled)} entries logged")

    return rt_offset


# ---------------------------------------------------------------------------
# MS1 scatter + heatmap from _ms1.txt
# Cols: Frame_ID  RT_seconds  Scan_Number  mz  Intensity  Mobility
# ---------------------------------------------------------------------------

def log_ms1(ms1_txt: str, topn: int = 2000, maxframes: int = 0,
            chunksize: int = 1_000_000, rt_offset: float = 0.0) -> None:
    print(f"Loading MS1: {ms1_txt}  topn={topn}")
    col_names = ["Frame_ID","RT_seconds","Scan_Number","mz","Intensity","Mobility"]
    frames_done = 0
    buf_rt, buf_mz, buf_mob, buf_i = [], [], [], []
    prev_fid = None

    def flush(rt, mz, mob, inten):
        arr_mz   = np.array(mz,   dtype=np.float32)
        arr_mob  = np.array(mob,  dtype=np.float32)
        arr_i    = np.array(inten, dtype=np.float32)
        if len(arr_i) > topn:
            idx = np.argpartition(arr_i, -topn)[-topn:]
            arr_mz, arr_mob, arr_i = arr_mz[idx], arr_mob[idx], arr_i[idx]
        log_frame(rt - rt_offset, arr_mz, arr_mob, arr_i)

    for chunk in pd.read_csv(ms1_txt, sep=r"\s+", names=col_names,
                              comment="#", header=None,
                              engine="python", chunksize=chunksize):
        if maxframes > 0 and frames_done >= maxframes:
            break
        for fid, grp in chunk.groupby("Frame_ID", sort=True):
            if maxframes > 0 and frames_done >= maxframes:
                break
            if prev_fid is not None and fid != prev_fid:
                flush(buf_rt[0], buf_mz, buf_mob, buf_i)
                buf_rt.clear(); buf_mz.clear()
                buf_mob.clear(); buf_i.clear()
                frames_done += 1
                if frames_done % 200 == 0:
                    print(f"  {frames_done} frames...", end="\r", flush=True)
            buf_rt.append(grp["RT_seconds"].iloc[0])
            buf_mz.extend(grp["mz"].tolist())
            buf_mob.extend(grp["Mobility"].tolist())
            buf_i.extend(grp["Intensity"].tolist())
            prev_fid = fid

    if buf_mz:
        flush(buf_rt[0], buf_mz, buf_mob, buf_i)
        frames_done += 1

    print(f"\n  {frames_done} MS1 frames logged")


# ---------------------------------------------------------------------------
# Full-resolution from _tdfbin chunks
# Cols: Frame_ID  RT_seconds  MsMsType  Scan  mz  Intensity  1_over_K0
# ---------------------------------------------------------------------------

def log_tdfbin(chunk_dir: str, topn: int = 2000, maxframes: int = 3000,
               rt_offset: float = 0.0) -> None:
    ms1_chunks = sorted(glob.glob(os.path.join(chunk_dir, "*_ms1_chunk*.txt")))
    print(f"tdfbin: {len(ms1_chunks)} MS1 chunks  topn={topn}  max={maxframes}")
    col_names = ["Frame_ID","RT_seconds","MsMsType","Scan","mz","Intensity","1_over_K0"]
    frames_done = 0

    for cfile in ms1_chunks:
        if frames_done >= maxframes:
            break
        for chunk in pd.read_csv(cfile, sep="\t", names=col_names,
                                  comment="#", header=None,
                                  engine="c", chunksize=1_000_000):
            if frames_done >= maxframes:
                break
            for _, grp in chunk.groupby("Frame_ID", sort=True):
                if frames_done >= maxframes:
                    break
                rt   = float(grp["RT_seconds"].iloc[0])
                mz   = grp["mz"].values.astype(np.float32)
                mob  = grp["1_over_K0"].values.astype(np.float32)
                inten = grp["Intensity"].values.astype(np.float32)
                if len(inten) > topn:
                    idx  = np.argpartition(inten, -topn)[-topn:]
                    mz, mob, inten = mz[idx], mob[idx], inten[idx]
                log_frame(rt - rt_offset, mz, mob, inten)
                frames_done += 1
                if frames_done % 200 == 0:
                    print(f"  {frames_done} frames...", end="\r", flush=True)

    print(f"\n  {frames_done} MS1 frames logged")


# ---------------------------------------------------------------------------
# Blueprint - conditional layout: only include panels that have data
# ---------------------------------------------------------------------------

def _full_time_range() -> rr.VisibleTimeRange:
    """
    Show the entire run regardless of where the cursor sits.
    Use cursor_relative ±2h: covers any realistic LC run (most are <120 min)
    and is robust because it doesn't require knowing the data end time upfront.
    """
    return rr.VisibleTimeRange(
        "rt_seconds",
        start=rr.TimeRangeBoundary(rr.TimeInt(seconds=-7200), kind="cursor_relative"),
        end=rr.TimeRangeBoundary(rr.TimeInt(seconds=7200),    kind="cursor_relative"),
    )


def make_blueprint(has_chrom: bool, has_frames: bool) -> rrb.Blueprint:
    """
    has_chrom : BPC/TIC from -sql
    has_frames: frame heatmap/mobilogram/spectrum from -ms1 or -tdfbin
    """
    vtr = _full_time_range()

    chrom_row = rrb.Horizontal(
        rrb.TimeSeriesView(
            name="BPC  (Base Peak Chromatogram)",
            origin="chromatogram/BPC",
            time_ranges=vtr,
        ),
        rrb.TimeSeriesView(
            name="TIC  (Total Ion Chromatogram)",
            origin="chromatogram/TIC",
            time_ranges=vtr,
        ),
        column_shares=[1, 1],
    )

    frame_panels = rrb.Vertical(
        rrb.Spatial2DView(
            name="Frame Heatmap  (x=m/z  y=1/K0  colour=log(intensity)  scrub RT-->)",
            origin="frame_heatmap",
        ),
        rrb.Horizontal(
            rrb.Spatial2DView(
                name="Mobilogram  (x=intensity  y=1/K0)",
                origin="mobilogram",
            ),
            rrb.Spatial2DView(
                name="Summed Spectrum  (x=m/z  y=intensity)",
                origin="spectrum",
            ),
            column_shares=[1, 2],
        ),
        row_shares=[3, 1],
    )

    if has_chrom and has_frames:
        root = rrb.Vertical(chrom_row, frame_panels, row_shares=[1, 3])
    elif has_chrom:
        root = chrom_row
    else:
        root = frame_panels

    return rrb.Blueprint(root, collapse_panels=True)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("run_d")
    ap.add_argument("-sql",    action="store_true", help="BPC+TIC from _frames.txt")
    ap.add_argument("-ms1",    action="store_true", help="Heatmap from _ms1.txt")
    ap.add_argument("-tdfbin", action="store_true", help="Heatmap from _tdfbin/")
    ap.add_argument("--topn",      type=int, default=2000,
                    help="Top N peaks/frame for heatmap (default 2000)")
    ap.add_argument("--maxframes", type=int, default=0,
                    help="Max frames (0=all)")
    ap.add_argument("--save", type=str, default="",
                    help="Write .rrd instead of live viewer")
    args = ap.parse_args()

    d = args.run_d.rstrip("/\\")
    if not args.sql and not args.ms1 and not args.tdfbin:
        if os.path.exists(d + "_frames.txt"): args.sql = True
        if os.path.exists(d + "_ms1.txt"):    args.ms1 = True

    rr.init("timsread/" + os.path.basename(d),
            spawn=(not bool(args.save)))
    if args.save:
        rr.save(args.save)
        print(f"Saving to {args.save}")

    rr.log("world", rr.ViewCoordinates.RIGHT_HAND_Y_UP, static=True)

    has_chrom  = args.sql  and os.path.exists(d + "_frames.txt")
    has_frames = (args.ms1 and os.path.exists(d + "_ms1.txt")) or \
                 (args.tdfbin and os.path.isdir(d + "_tdfbin"))

    # Send blueprint BEFORE data so layout is ready when first data arrives
    rr.send_blueprint(make_blueprint(has_chrom, has_frames))

    # Log a static explanation of the layout (visible in viewer's text panel)
    rr.log("info/layout",
           rr.TextDocument(
               text=(
                   "## timsTOF 5D Data  --  Rerun Viewer\n\n"
                   "**BPC / TIC** (top row): full-run chromatograms.  "
                   "Source: chromatography-data.sqlite Traces 18+20.\n\n"
                   "**Frame Heatmap** (centre): one MS1 frame at the current timeline position.\n"
                   "  - X axis = m/z  (200 → 1700 Da, left → right)\n"
                   "  - Y axis = 1/K0 (0.60 → 1.80 Vs/cm², bottom → top)\n"
                   "  - Colour = log(intensity): black=0, purple=low, red=mid, yellow=high\n"
                   "  → **Drag the scrubber** at the bottom to the chromatographic peak "
                   "  (try the 30-60 min region) to see peptide signal.\n\n"
                   "**Mobilogram** (bottom-left): 1/K0 vs summed intensity for this frame.\n"
                   "**Summed Spectrum** (bottom-right): m/z vs summed intensity for this frame.\n\n"
                   "⚠️  `-ms1` samples every 10th IMS scan.  "
                   "For a denser heatmap matching MZmine, use `-tdfbin` (needs disk space)."
               ),
               media_type="text/markdown",
           ),
           static=True)

    if args.sql:
        f = d + "_frames.txt"
        if os.path.exists(f):
            rt_offset = log_chromatogram(f, d)
        else:
            print(f"Not found: {f}  ->  timsread {d} -sql")
            rt_offset = 0.0
    else:
        rt_offset = 0.0

    if args.ms1:
        f = d + "_ms1.txt"
        if os.path.exists(f):
            log_ms1(f, topn=args.topn,
                    maxframes=args.maxframes,
                    rt_offset=rt_offset)
        else:
            print(f"Not found: {f}  ->  timsread {d} -ms1")

    if args.tdfbin:
        td = d + "_tdfbin"
        if os.path.isdir(td):
            log_tdfbin(td, topn=args.topn,
                       maxframes=args.maxframes if args.maxframes else 3000,
                       rt_offset=rt_offset)
        else:
            print(f"Not found: {td}/  ->  timsread {d} -tdfbin")

    if args.save:
        print(f"Done.  Open with:  rerun {args.save}")
    else:
        print("Viewer running — drag the RT scrubber to animate the frame heatmap")
        print("Ctrl+C to exit")
        import time
        try:
            while True: time.sleep(1)
        except KeyboardInterrupt:
            pass

if __name__ == "__main__":
    main()
