"""Replay the archived camera streams through the C++ recorder on a Linux host."""
import csv
import os
from pathlib import Path
import resource
import shlex
import signal
import subprocess
import tempfile

APP = Path(__file__).resolve().parents[1]
EVIDENCE = APP.parent / "evidence" / "capture_cpp"
DEFAULT_SDK = APP.parents[3] / "MaixCDK"
SDK = Path(os.environ.get("MAIXCDK_PATH", DEFAULT_SDK))
MIDDLEWARE = SDK / "components/3rd_party/sophgo-middleware/sophgo-middleware/v2"


def output(*args):
    return subprocess.check_output(args)


def replay(work, binary, sample, width):
    stem = sample.stem
    dest = work / f"{stem}-{width}"
    dest.mkdir()
    sizes = work / f"{stem}.sizes"
    sizes.write_bytes(output("ffprobe", "-v", "error", "-show_packets", "-select_streams", "v:0",
                             "-show_entries", "packet=size", "-of", "csv=p=0", sample))
    cmd = [binary, sample, sizes, sample.with_suffix(".csv"), dest, str(width)]
    subprocess.run(cmd, check=True, timeout=60)
    video, = dest.glob("*.h265")
    assert video.with_suffix(".csv").read_bytes() == sample.with_suffix(".csv").read_bytes()
    with video.with_suffix(".timing.tsv").open() as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    with sample.with_suffix(".csv").open() as source:
        original = list(csv.DictReader(source))
    assert [(r["frame_index"], r["t_us"]) for r in rows] == [(r["frame_index"], r["t_us"]) for r in original]
    buffers = 1 if width == 1440 else 2
    assert all(r["cpu_mapped"] == "0" and int(r["camera_buffers"]) == buffers
               and int(r["queue_capacity"]) == 16 and 1 <= int(r["queue_depth"]) <= 16 for r in rows)
    fields = ("open_us", "startup_discarded", "isp_fps_milli", "exposure_us")
    assert tuple(int(rows[0][k]) for k in fields) == (156855, buffers, 30000, 18000)
    assert all(tuple(int(r[k]) for k in fields) == (0, 0, -1, -1) for r in rows[1:])
    profile = output("python3", "-B", APP / "scripts/profile_capture.py", video.with_suffix(".csv")).decode()
    assert f"queue capacity=16   discarded at start={buffers}" in profile
    assert f"non-unit VPSS TimeRef steps=0/{len(rows)-1}" in profile
    pixels = output("ffmpeg", "-v", "error", "-xerror", "-i", video, "-f", "hash", "-hash", "sha256", "-")
    with (EVIDENCE / "media-decode.tsv").open() as source:
        expected = next(r["decoded_sha256"] for r in csv.DictReader(source, delimiter="\t")
                        if EVIDENCE / r["file"] == sample)
    assert pixels.strip().decode() == f"SHA256={expected}"
    assert not list(dest.glob("*.part"))
    # Surface a real file write failure while both workers are running.
    full = dest / "storage-error"
    full.mkdir()
    def limit_file_size():
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (1024 * 1024, 1024 * 1024))
    failed = subprocess.run([*cmd[:4], full, str(width)], preexec_fn=limit_file_size,
                            capture_output=True, timeout=10)
    assert failed.returncode == 1 and all(p.suffix == ".jpg" for p in full.iterdir()), failed.stderr
    # Missing VENC output must reach finish() or the next push without hanging or publishing a pair.
    bad = dest / "error"
    bad.mkdir()
    sizes.write_text("")
    result = subprocess.run([binary, sample, sizes, sample.with_suffix(".csv"), bad, str(width)],
                            capture_output=True, timeout=10)
    assert result.returncode == 1 and b"no HEVC access unit" in result.stderr
    assert all(p.suffix == ".jpg" for p in bad.iterdir())
    depth = max(int(r["queue_depth"]) for r in rows)
    print(f"{sample.parent.name}, encoder width {width}, {buffers} startup buffers discarded, "
          f"{len(rows)} frames verified, peak FIFO {depth}/16")


def main():
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        binary = work / "host_replay"
        flags = shlex.split(output("pkg-config", "--cflags", "--libs", "opencv4", "libavcodec", "libavutil").decode())
        subprocess.run(["g++", "-std=c++17", "-O2", "-pthread", "-Wall", "-Wextra", "-Werror",
                        "-I", APP / "tests/stubs", "-I", APP / "main/include", "-I", MIDDLEWARE / "include",
                        "-I", MIDDLEWARE / "uapi", APP / "tests/host_replay.cpp",
                        APP / "main/src/nv21_frame.cpp", APP / "main/src/recording.cpp", *flags, "-o", binary],
                       check=True)
        samples = sorted(EVIDENCE.glob("v[0-9]*/*.h265"))
        if not samples:
            raise FileNotFoundError(f"No board recordings in {EVIDENCE}")
        for sample in samples:
            for width in (1440, 1472):
                replay(work, binary, sample, width)


if __name__ == "__main__":
    main()
