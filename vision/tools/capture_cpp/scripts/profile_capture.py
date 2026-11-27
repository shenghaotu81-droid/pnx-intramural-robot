"""Summarize an exported capture on the host. Uses only the Python standard library."""

import argparse
import csv
import math
from pathlib import Path
from statistics import median


def read_rows(path, delimiter, required, exact=False):
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source, delimiter=delimiter)
        header = reader.fieldnames or []
        if (exact and header != required) or not set(required).issubset(header):
            raise ValueError(f"{path.name}: unexpected header {header}")
        rows = []
        for index, row in enumerate(reader):
            if None in row or any(row[name] is None for name in header):
                raise ValueError(f"{path.name}: incomplete row {index}")
            values = {name: int(row[name]) for name in header}
            if values["frame_index"] != index:
                raise ValueError(f"{path.name}: expected frame_index {index}")
            rows.append(values)
        return rows


def p95(values):
    return sorted(values)[math.ceil(len(values) * 0.95) - 1]


def report(path):
    frames = read_rows(path, ",", ["frame_index", "t_us", "vpss_pts"], exact=True)
    if not frames or frames[0]["t_us"] != 0:
        raise ValueError("expected a recording starting at t_us=0")
    gaps = [b["t_us"] - a["t_us"] for a, b in zip(frames, frames[1:])]
    if any(gap <= 0 for gap in gaps):
        raise ValueError("non-increasing t_us")
    print(f"{path.name}   frames={len(frames)}")
    if gaps:
        elapsed = frames[-1]["t_us"]
        print(f"elapsed={elapsed/1e6:.6f} s   fps={len(gaps)*1e6/elapsed:.2f}")
        print(f"capture intervals (ms)   median={median(gaps)/1000:.3f}"
              f"   p95={p95(gaps)/1000:.3f}   max={max(gaps)/1000:.3f}")
        near_target = sum(abs(gap - 1e6 / 30) <= 2000 for gap in gaps)
        print(f"within 33.333 +/- 2 ms: {near_target}/{len(gaps)} capture intervals")
        pts_gaps = [b["vpss_pts"] - a["vpss_pts"] for a, b in zip(frames, frames[1:])]
        print(f"VPSS intervals (raw ticks)   median={median(pts_gaps):g}"
              f"   p95={p95(pts_gaps)}   max={max(pts_gaps)}")
    slowest = sorted(range(1, len(frames)), key=lambda i: gaps[i - 1], reverse=True)[:5]
    stages = ["pop_us", "retry_us", "open_us", "writer_wait_us", "push_us", "preview_us",
              "photo_us", "release_us", "hold_us", "display_us", "enc_pop_us", "packet_us",
              "crop_us", "video_write_us", "csv_write_us"]
    trace = path.with_suffix(".timing.tsv")
    details = None
    if trace.exists():
        details = read_rows(trace, "\t", ["frame_index", "t_us", "pop_start_us", "pop_timeouts", *stages])
        if len(details) != len(frames) or any(d["t_us"] != f["t_us"] for d, f in zip(details, frames)):
            raise ValueError("timing rows do not match the calibration CSV")
        print(f"{trace.name}   pop timeouts={sum(d['pop_timeouts'] for d in details)}")
        print("stage                nonzero/total       median       p95       max   (ms, all frames)")
        for stage in stages:
            values = [row[stage] for row in details]
            print(f"{stage:<20} {sum(v > 0 for v in values):5}/{len(values):<5}"
                  f" {median(values)/1000:10.3f} {p95(values)/1000:9.3f} {max(values)/1000:9.3f}")
    else:
        print("No matching .timing.tsv; showing capture timestamps only.")
    for i in slowest:
        a, b = frames[i - 1], frames[i]
        print(f"{i-1} -> {i}   gap={gaps[i-1]/1000:.3f} ms   VPSS delta={b['vpss_pts']-a['vpss_pts']}")
        if details:
            prev, curr = details[i - 1], details[i]
            print(f"  previous capture to next successful pop start={(curr['pop_start_us']-a['t_us'])/1000:.3f} ms"
                  f"   previous hold={prev['hold_us']/1000:.3f} ms"
                  f"   current retry={curr['retry_us']/1000:.3f} ms   current pop={curr['pop_us']/1000:.3f} ms")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="video_*.csv calibration timestamps")
    args = parser.parse_args()
    try:
        report(args.csv)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
