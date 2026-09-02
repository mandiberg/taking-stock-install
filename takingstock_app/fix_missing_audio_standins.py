#!/usr/bin/env python3
"""Repair missing audio stand-ins for video cluster rows in installation.csv.

This script reads the CSV exported by the video pipeline, extracts each cluster's
video timestamp, checks whether an audio file already exists in the audio folder,
and copies a generic stand-in WAV when it is missing.

It also marks the row in the CSV with an `audio_copy` flag and adds a `_copy`
marker to the copied filename so the stand-in is obvious in the folder.
"""

from __future__ import annotations

import csv
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
AUDIO_DIR = ROOT / "audio"
VIDEO_DIR = ROOT / "videos"
CSV_PATH = VIDEO_DIR / "installation.csv"
TEMPLATE_FILE = "multitrack_mixdown_offset_clustercc2_p25_t0_1787511631.699627_quad.wav"


def parse_cluster_and_timestamp(video_name: str):
    """Return (cluster_no, timestamp) from a video filename.

    Example input:
      output_folder__..._clustercc101_p1_t0_om1_1787508223.5895429_p30_st1_ct8_fr120.mp4
    Output:
      ("cc101_p1_t0_om1", "1787508223.5895429")
    """
    match = re.search(
        r"cluster(?P<cluster>.*?)(?P<timestamp>\d+\.\d+)_p\d+_st\d+_ct\d+_fr\d+\.mp4$",
        video_name,
    )
    if not match:
        return None, None

    cluster = match.group("cluster").rstrip("_")
    timestamp = match.group("timestamp")
    return cluster, timestamp


def expected_audio_name(cluster_no: str, timestamp: str) -> str:
    return f"multitrack_mixdown_offset_cluster{cluster_no}_{timestamp}_quad.wav"


def expected_copy_name(cluster_no: str, timestamp: str) -> str:
    # return f"multitrack_mixdown_offset_cluster{cluster_no}_{timestamp}_copy_quad.wav"
    return f"multitrack_mixdown_offset_cluster{cluster_no}_{timestamp}_quad.wav"


def main() -> int:
    if not CSV_PATH.exists():
        raise FileNotFoundError(f"CSV not found: {CSV_PATH}")
    if not AUDIO_DIR.exists():
        raise FileNotFoundError(f"Audio folder not found: {AUDIO_DIR}")

    template_path = AUDIO_DIR / TEMPLATE_FILE
    if not template_path.exists():
        raise FileNotFoundError(
            f"Template audio not found in {AUDIO_DIR}: {TEMPLATE_FILE}"
        )

    rows = []
    with CSV_PATH.open("r", newline="", encoding="utf-8-sig") as csv_file:
        reader = csv.DictReader(csv_file)
        fieldnames = list(reader.fieldnames or [])
        if "audio_copy" not in fieldnames:
            fieldnames.append("audio_copy")

        for row in reader:
            rows.append(row)

    copied_count = 0
    for row in rows:
        cluster_no = (row.get("cluster_no") or "").strip()
        file_name = (row.get("file_name") or "").strip()

        if not cluster_no and file_name:
            cluster_no, _ = parse_cluster_and_timestamp(file_name)

        if not cluster_no:
            row["audio_copy"] = "n/a"
            continue

        timestamp = None
        if file_name:
            _, timestamp = parse_cluster_and_timestamp(file_name)
        if not timestamp:
            row["audio_copy"] = "missing-timestamp"
            continue

        target_name = expected_audio_name(cluster_no, timestamp)
        target_path = AUDIO_DIR / target_name

        if target_path.exists():
            row["audio_copy"] = "no"
            continue

        copy_name = expected_copy_name(cluster_no, timestamp)
        copy_path = AUDIO_DIR / copy_name
        if copy_path.exists():
            row["audio_copy"] = "copy-existing"
            continue

        shutil.copy2(template_path, copy_path)
        row["audio_copy"] = "copy"
        copied_count += 1
        print(f"Created stand-in: {copy_path.name} for cluster {cluster_no}")

    with CSV_PATH.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Finished. Created {copied_count} stand-in audio file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
