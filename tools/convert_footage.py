#!/usr/bin/env python3
"""Convert the clock's three MP4 clips to streaming ESP32 RGB565LE assets.

No Python dependencies are required. FFmpeg must be on PATH, specified with
--ffmpeg, or bundled in tools/vendor/imageio_ffmpeg/binaries. To obtain a local
Windows FFmpeg executable without modifying your global Python installation:

    python -m pip install --target tools/vendor imageio-ffmpeg

The CLK1 file consists of a 16-byte little-endian <4sHHHHI header followed by
width * height * 2 bytes per frame. RGB565 pixels are little-endian uint16 values.
The entire source clip is converted; frame rate conversion quantizes its end to
a 100 ms frame boundary at the default 10 FPS. Audio is intentionally excluded.

The supplied filenames do not match their visible scenes. For this collection,
Sunset.mp4 contains daylight, Night Time.mp4 contains sunset, and Day Time.mp4
contains moonlit night. CLIPS below maps by visible scenery; originals are never
renamed or modified. Edit CLIPS if replacing these videos with another collection.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HEADER = struct.Struct("<4sHHHHI")
WIDTH = 320
HEIGHT = 240
MAX_FPS = 30  # Must match the firmware's CLK1 validation limit.
FRAME_BYTES = WIDTH * HEIGHT * 2
# Verified against both the first frame and the frame at two seconds.
CLIPS = (("Sunset.mp4", "day.clk"),
         ("Night Time.mp4", "sunset.clk"),
         ("Day Time.mp4", "night.clk"))


def find_ffmpeg(explicit: str | None) -> str:
    if explicit:
        executable = shutil.which(explicit)
        if executable:
            return executable
        candidate = Path(explicit).expanduser()
        if candidate.is_file():
            return str(candidate.resolve())
        raise RuntimeError(f"FFmpeg executable does not exist: {explicit}")
    system = shutil.which("ffmpeg")
    if system:
        return system
    binaries = Path(__file__).resolve().parent / "vendor/imageio_ffmpeg/binaries"
    if binaries.is_dir():
        for binary in sorted(binaries.glob("ffmpeg-*")):
            if binary.is_file():
                return str(binary)
    raise RuntimeError(
        "FFmpeg was not found. Pass --ffmpeg PATH or install a local copy with:\n"
        "python -m pip install --target tools/vendor imageio-ffmpeg"
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def source_metadata(ffmpeg: str, source: Path) -> dict:
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-i", str(source)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="replace", check=False,
    )
    # FFmpeg -i exits nonzero when no output is given. Its stderr still contains
    # source metadata; the actual conversion below validates decoding success.
    duration = re.search(r"Duration: (\d+):(\d+):(\d+(?:\.\d+)?)", result.stderr)
    dimensions = re.search(r"Video:.*?\b(\d{2,5})x(\d{2,5})\b", result.stderr)
    frame_rate = re.search(r"Video:.*?([\d.]+) fps", result.stderr)
    details = {"file": source.name, "bytes": source.stat().st_size,
               "sha256": sha256(source)}
    if duration:
        details["duration_seconds_reported"] = (
            int(duration[1]) * 3600 + int(duration[2]) * 60 + float(duration[3]))
    if dimensions:
        details["width"], details["height"] = map(int, dimensions.groups())
    if frame_rate:
        details["fps"] = float(frame_rate[1])
    return details


def validate_asset(path: Path) -> dict:
    with path.open("rb") as asset:
        header = asset.read(HEADER.size)
    if len(header) != HEADER.size:
        raise RuntimeError(f"Incomplete CLK1 header: {path}")
    magic, width, height, fps, reserved, frames = HEADER.unpack(header)
    expected_size = HEADER.size + width * height * 2 * frames
    if (magic != b"CLK1" or width != WIDTH or height != HEIGHT or not 1 <= fps <= MAX_FPS
            or reserved != 0 or frames == 0 or path.stat().st_size != expected_size):
        raise RuntimeError(f"CLK1 validation failed: {path}")
    return {"file": path.name, "magic": magic.decode("ascii"),
            "width": width, "height": height, "fps": fps,
            "reserved": reserved, "frame_count": frames,
            "duration_seconds": frames / fps, "frame_bytes": width * height * 2,
            "header_bytes": HEADER.size, "bytes": expected_size,
            "sha256": sha256(path)}


def convert(ffmpeg: str, source: Path, output: Path, fps: int) -> dict:
    video_filter = (
        f"fps=fps={fps}:round=up,"
        f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=increase:flags=lanczos,"
        f"crop={WIDTH}:{HEIGHT},setsar=1"
    )
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
               "-i", str(source), "-map", "0:v:0", "-an", "-sn", "-dn",
               "-vf", video_filter, "-pix_fmt", "rgb565le",
               "-c:v", "rawvideo", "-f", "rawvideo", "pipe:1"]
    output.parent.mkdir(parents=True, exist_ok=True)
    # Use a temporary file and rename only after complete validation, so failed
    # conversions never replace the user's previous usable output.
    with tempfile.NamedTemporaryFile(
            dir=output.parent, prefix=output.stem + "-", suffix=".partial",
            delete=False) as temporary:
        temporary_path = Path(temporary.name)
    process = None
    try:
        with tempfile.TemporaryFile() as error_log, temporary_path.open("wb+") as target:
            target.write(HEADER.pack(b"CLK1", WIDTH, HEIGHT, fps, 0, 0))
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=error_log)
            assert process.stdout is not None
            payload_bytes = 0
            while block := process.stdout.read(1024 * 1024):
                target.write(block)
                payload_bytes += len(block)
            process.stdout.close()
            return_code = process.wait()
            if return_code:
                error_log.seek(0)
                error = error_log.read().decode("utf-8", errors="replace")
                raise RuntimeError(f"FFmpeg failed for {source.name}:\n{error}")
            if payload_bytes == 0 or payload_bytes % FRAME_BYTES:
                raise RuntimeError(f"Incomplete frame data for {source.name}")
            frames = payload_bytes // FRAME_BYTES
            if frames > 0xFFFFFFFF:
                raise RuntimeError("Clip is too long for the CLK1 frame counter")
            target.seek(0)
            target.write(HEADER.pack(b"CLK1", WIDTH, HEIGHT, fps, 0, frames))
        validate_asset(temporary_path)
        temporary_path.replace(output)
    except BaseException:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        temporary_path.unlink(missing_ok=True)
        raise
    result = validate_asset(output)
    result["source"] = source_metadata(ffmpeg, source)
    result["ffmpeg_command"] = command
    return result


def main() -> int:
    project = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=project.parent / "Normal-64Bits",
                        help="Folder containing Day Time.mp4, Sunset.mp4, Night Time.mp4")
    parser.add_argument("--destination", type=Path, default=project / "sdcard/clock",
                        help="Output folder (default: this project's sdcard/clock)")
    parser.add_argument("--ffmpeg", help="Path to an FFmpeg executable")
    parser.add_argument("--fps", type=int, default=10,
                        help=f"Output frames per second, 1-{MAX_FPS} (default: 10)")
    parser.add_argument("--validate-only", action="store_true",
                        help="Validate existing CLK1 files without converting the videos")
    args = parser.parse_args()
    if not 1 <= args.fps <= MAX_FPS:
        parser.error(f"--fps must be between 1 and {MAX_FPS}")
    source_folder = args.source.expanduser().resolve()
    destination = args.destination.expanduser().resolve()
    if args.validate_only:
        results = [validate_asset(destination / name) for _, name in CLIPS]
    else:
        ffmpeg = find_ffmpeg(args.ffmpeg)
        for source_name, _ in CLIPS:
            if not (source_folder / source_name).is_file():
                raise RuntimeError(f"Source clip not found: {source_folder / source_name}")
        results = []
        for source_name, output_name in CLIPS:
            print(f"Converting {source_name} -> {output_name} ...", flush=True)
            result = convert(ffmpeg, source_folder / source_name,
                             destination / output_name, args.fps)
            results.append(result)
            print(f"  {result['frame_count']} frames, {result['duration_seconds']:.1f} s, "
                  f"{result['bytes']:,} bytes; verified", flush=True)
        manifest = {"format": "CLK1 RGB565LE",
                    "mapping_note": "Source filenames differ from their visible scenes. "
                                    "Outputs are named by actual visible scenery.",
                    "clips": results}
        (destination / "conversion_manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
