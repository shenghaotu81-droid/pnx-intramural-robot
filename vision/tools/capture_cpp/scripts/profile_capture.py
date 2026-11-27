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


def report_vpss(frames, details):
    first = details[0]
    if "queue_capacity" in first:
        print(f"queue capacity={first['queue_capacity']}   discarded at start={first['startup_discarded']}"
              f"   ISP configured fps x1000={first['isp_fps_milli']}   start exposure (us)={first['exposure_us']}")
    jumps = []
    for i in range(1, len(details)):
        step = (details[i]["vpss_time_ref"] - details[i - 1]["vpss_time_ref"]) & 0xffffffff
        if step != 1:
            jumps.append((i, step))
    print(f"non-unit VPSS TimeRef steps={len(jumps)}/{len(details)-1}")
    for i, step in jumps[:10]:
        print(f"  {i-1} -> {i}   TimeRef step={step}   software gap="
              f"{(frames[i]['t_us']-frames[i-1]['t_us'])/1000:.3f} ms"
              f"   VPSS delta={frames[i]['vpss_pts']-frames[i-1]['vpss_pts']} ticks")
    if len(frames) > 1:
        print(f"first interval: software={frames[1]['t_us']/1000:.3f} ms"
              f"   VPSS delta={frames[1]['vpss_pts']-frames[0]['vpss_pts']} ticks")
    stalls = sorted(details, key=lambda r: r["video_write_us"], reverse=True)[:3]
    print("longest video writes (frame, ms): " + ", ".join(
        f"{r['frame_index']} {r['video_write_us']/1000:.3f}" for r in stalls))


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
        vpss_fields = {"vpss_time_ref", "cpu_mapped", "pixel_map_us", "camera_buffers",
                       "queue_wait_us", "queue_depth"}
        if vpss_fields.intersection(details[0]):
            if not vpss_fields.issubset(details[0]):
                raise ValueError("incomplete VPSS timing header")
            stages += ["pixel_map_us", "queue_wait_us"]
            print(f"VPSS get timed separately; camera buffers={details[0]['camera_buffers']}   "
                  f"CPU-mapped frames={sum(d['cpu_mapped'] for d in details)}/{len(details)}   "
                  f"maximum queued packets={max(d['queue_depth'] for d in details)}")
            startup_fields = {"queue_capacity", "startup_discarded", "isp_fps_milli", "exposure_us"}
            if startup_fields.intersection(details[0]) and not startup_fields.issubset(details[0]):
                raise ValueError("incomplete recording-start timing header")
            report_vpss(frames, details)
        else:
            print("pop_us includes VPSS get, CPU mapping and cache invalidation.")
        print("stage                nonzero/total       median       p95       max   (ms, all frames)")
        for stage in stages:
            values = [row[stage] for row in details]
            print(f"{stage:<20} {sum(v > 0 for v in values):5}/{len(values):<5}"
                  f" {median(values)/1000:10.3f} {p95(values)/1000:9.3f} {max(values)/1000:9.3f}")
        for preview in (False, True):
            pairs = [(a, b) for a, b in zip(details, details[1:]) if bool(a["preview_us"]) == preview]
            if pairs:
                gap = median(b["t_us"] - a["t_us"] for a, b in pairs)
                pop = median(b["pop_us"] for _, b in pairs)
                print(f"previous preview={preview}: {len(pairs)} gaps, median gap={gap/1000:.3f} ms"
                      f", following pop={pop/1000:.3f} ms")
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
