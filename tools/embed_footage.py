#!/usr/bin/env python3
"""Embed the complete three clock videos as baseline JPEG frames in ESP32 flash.

Run this script from any directory. The generated files are
DigitalClock/EmbeddedAnimations.h and .cpp. No microSD card is needed to play
these frames. Dependencies stay local to this project when installed with:

    python -m pip install --target tools/vendor Pillow imageio-ffmpeg

The supplied source filenames are shifted relative to their visible scenery:
Sunset.mp4 -> day, Night Time.mp4 -> sunset, Day Time.mp4 -> night. The originals
are read only. Every source clip is retained at full duration, center-cropped
to 320x240; the last frame is padded to the next output frame boundary.

By default, try JPEG quality 75, 70, 65, then 60 at 10 FPS. Only if none meets
the 1,700,000-byte target, try the same quality range at 8 FPS. The total JPEG
payload must never exceed 1,900,000 bytes. The generated const arrays reside in
flash, including the frame tables; frames do not occupy writable static RAM.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile

TOOLS = Path(__file__).resolve().parent
try:
    from PIL import Image, __version__ as PILLOW_VERSION
except ModuleNotFoundError:
    sys.path.insert(0, str(TOOLS / "vendor"))
    try:
        from PIL import Image, __version__ as PILLOW_VERSION
    except ModuleNotFoundError as error:
        raise SystemExit(
            "Pillow was not found. Install local dependencies with:\n"
            "python -m pip install --target tools/vendor Pillow imageio-ffmpeg"
        ) from error

from convert_footage import CLIPS, HEIGHT, WIDTH, find_ffmpeg, sha256, source_metadata

FRAME_BYTES = WIDTH * HEIGHT * 3
SCENES = ("day", "sunset", "night")
HARD_MAX_PAYLOAD = 1_900_000


def read_exact(stream, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        block = stream.read(size - len(result))
        if not block:
            break
        result.extend(block)
    return bytes(result)


def extract_frames(ffmpeg: str, source: Path, fps: int) -> list:
    command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
               "-i", str(source), "-map", "0:v:0", "-an", "-sn", "-dn",
               "-vf", f"fps=fps={fps}:round=up,"
               f"scale={WIDTH}:{HEIGHT}:force_original_aspect_ratio=increase:flags=lanczos,"
               f"crop={WIDTH}:{HEIGHT},setsar=1",
               "-pix_fmt", "rgb24", "-c:v", "rawvideo", "-f", "rawvideo", "pipe:1"]
    frames = []
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=errors)
        assert process.stdout is not None
        try:
            while raw := read_exact(process.stdout, FRAME_BYTES):
                if len(raw) != FRAME_BYTES:
                    raise RuntimeError(f"Partial decoded frame in {source.name}")
                frames.append(Image.frombytes("RGB", (WIDTH, HEIGHT), raw))
            process.stdout.close()
            if process.wait():
                errors.seek(0)
                raise RuntimeError(errors.read().decode("utf-8", errors="replace"))
        except BaseException:
            if process.poll() is None:
                process.kill()
                process.wait()
            raise
    if not frames or len(frames) > 65535:
        raise RuntimeError(f"Unsupported frame count in {source.name}: {len(frames)}")
    return frames


def compress_frames(frames: list, quality: int) -> list[bytes]:
    encoded = []
    for frame in frames:
        target = io.BytesIO()
        frame.save(target, format="JPEG", quality=quality, subsampling=2,
                   optimize=True, progressive=False)
        encoded.append(target.getvalue())
    return encoded


def jpeg_frame_info(data: bytes) -> tuple[int, int, int]:
    """Check the JPEG header independently of Pillow: baseline SOF0, 8-bit 420."""
    if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
        raise RuntimeError("JPEG frame is missing its SOI/EOI marker")
    position = 2
    found = None
    while position < len(data):
        if data[position] != 0xFF:
            raise RuntimeError("Invalid JPEG marker alignment")
        while data[position] == 0xFF:
            position += 1
        marker = data[position]
        position += 1
        if marker == 0xDA:  # Start of entropy-coded scan; SOF must precede it.
            break
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            continue
        size = int.from_bytes(data[position:position + 2], "big")
        if size < 2 or position + size > len(data):
            raise RuntimeError("Invalid JPEG segment length")
        segment = data[position + 2:position + size]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            if marker != 0xC0:
                raise RuntimeError("Only baseline SOF0 JPEG frames are supported")
            if len(segment) != 15 or segment[0] != 8 or segment[5] != 3:
                raise RuntimeError("Expected 8-bit JPEG with three color components")
            height = int.from_bytes(segment[1:3], "big")
            width = int.from_bytes(segment[3:5], "big")
            if [segment[7], segment[10], segment[13]] != [0x22, 0x11, 0x11]:
                raise RuntimeError("Expected JPEG 4:2:0 chroma subsampling")
            found = (width, height, segment[0])
        position += size
    if found is None:
        raise RuntimeError("JPEG baseline frame header was not found")
    return found


def validate_frames(clips: list[list[bytes]]) -> None:
    for scene, frames in zip(SCENES, clips):
        for number, data in enumerate(frames):
            if jpeg_frame_info(data) != (WIDTH, HEIGHT, 8):
                raise RuntimeError(f"Wrong frame dimensions: {scene}/{number}")
            with Image.open(io.BytesIO(data)) as decoded:
                decoded.load()  # Decode every byte/frame, not only its header.
                if decoded.size != (WIDTH, HEIGHT) or decoded.mode != "RGB":
                    raise RuntimeError(f"Invalid decoded frame: {scene}/{number}")
                if decoded.info.get("progressive") or decoded.info.get("progression"):
                    raise RuntimeError(f"Progressive JPEG rejected: {scene}/{number}")


def write_sources(destination: Path, clips: list[list[bytes]], fps: int, quality: int) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    header = '''// Generated by tools/embed_footage.py. All frame data lives in flash.
#pragma once
#include <stdint.h>

struct AnimationFrame {
  const uint8_t *data;
  uint32_t size;
};

struct AnimationClip {
  const AnimationFrame *frames;
  uint16_t frameCount;
  uint16_t fps;
};

// Scene order: DAY, SUNSET, NIGHT. 320x240 baseline JPEG, 8-bit YCbCr 4:2:0.
extern const AnimationClip CLOCK_ANIMATIONS[3];
'''
    (destination / "EmbeddedAnimations.h").write_text(header, encoding="utf-8")
    cpp = destination / "EmbeddedAnimations.cpp"
    with cpp.open("w", encoding="utf-8", newline="\n") as target:
        target.write("// Generated by tools/embed_footage.py; do not hand-edit.\n")
        target.write(f"// Complete clips, {fps} FPS, baseline JPEG quality {quality}.\n")
        target.write("// Source names differ from scenery; mapping is in the manifest.\n")
        target.write('#include <Arduino.h>\n#include "EmbeddedAnimations.h"\n\n')
        for scene, frames in zip(SCENES, clips):
            for number, data in enumerate(frames):
                target.write(f"static const uint8_t {scene}_{number:03d}[] PROGMEM = {{\n")
                for offset in range(0, len(data), 20):
                    target.write("  " + ",".join(f"0x{value:02x}" for value in data[offset:offset + 20]) + ",\n")
                target.write("};\n\n")
            target.write(f"static const AnimationFrame {scene}_frames[] PROGMEM = {{\n")
            for number in range(len(frames)):
                name = f"{scene}_{number:03d}"
                target.write(f"  {{{name}, sizeof({name})}},\n")
            target.write("};\n\n")
        target.write("const AnimationClip CLOCK_ANIMATIONS[3] PROGMEM = {\n")
        for scene, frames in zip(SCENES, clips):
            target.write(f"  {{{scene}_frames, {len(frames)}, {fps}}},\n")
        target.write("};\n")


def main() -> int:
    project = TOOLS.parent
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=project.parent / "Normal-64Bits")
    parser.add_argument("--destination", type=Path, default=project / "DigitalClock")
    parser.add_argument("--ffmpeg")
    parser.add_argument("--fps", type=int, choices=(8, 10), default=10)
    parser.add_argument("--quality", type=int, default=75)
    parser.add_argument("--min-quality", type=int, default=60)
    parser.add_argument("--target-bytes", type=int, default=1_700_000)
    args = parser.parse_args()
    if not 1 <= args.min_quality <= args.quality <= 95:
        parser.error("Use 1 <= --min-quality <= --quality <= 95")
    if not 1 <= args.target_bytes <= HARD_MAX_PAYLOAD:
        parser.error(f"--target-bytes must be between 1 and {HARD_MAX_PAYLOAD}")
    ffmpeg = find_ffmpeg(args.ffmpeg)
    source_folder = args.source.expanduser().resolve()
    sources = [source_folder / source for source, _ in CLIPS]
    for source in sources:
        if not source.is_file():
            raise RuntimeError(f"Source video not found: {source}")
    metadata = [source_metadata(ffmpeg, source) for source in sources]
    qualities = list(range(args.quality, args.min_quality - 1, -5))
    if qualities[-1] != args.min_quality:
        qualities.append(args.min_quality)
    fps_options = [10, 8] if args.fps == 10 else [8]
    chosen = None
    attempts = []
    fallback = None
    for fps in fps_options:
        print(f"Decoding complete clips at {fps} FPS ...", flush=True)
        raw = [extract_frames(ffmpeg, source, fps) for source in sources]
        for quality in qualities:
            clips = [compress_frames(frames, quality) for frames in raw]
            payload = sum(len(frame) for clip in clips for frame in clip)
            attempts.append({"fps": fps, "quality": quality, "payload_bytes": payload})
            print(f"  Quality {quality}: {payload:,} JPEG payload bytes", flush=True)
            if payload <= HARD_MAX_PAYLOAD and fallback is None:
                fallback = (clips, fps, quality, payload)
            if payload <= args.target_bytes:
                chosen = (clips, fps, quality, payload)
                break
        for frames in raw:
            for frame in frames:
                frame.close()
        if chosen is not None:
            break
    if chosen is None:
        chosen = fallback
    if chosen is None:
        raise RuntimeError(f"No encoding fits the {HARD_MAX_PAYLOAD:,}-byte hard budget")
    clips, fps, quality, payload = chosen
    validate_frames(clips)
    for source, source_info in zip(sources, metadata):
        if sha256(source) != source_info["sha256"]:
            raise RuntimeError(f"Source changed while converting: {source.name}")
    write_sources(args.destination.expanduser().resolve(), clips, fps, quality)
    results = []
    preview = project / "previews"
    preview.mkdir(exist_ok=True)
    for scene, frames, source_info in zip(SCENES, clips, metadata):
        results.append({"scene": scene, "source": source_info,
                        "frame_count": len(frames), "fps": fps,
                        "duration_seconds": len(frames) / fps,
                        "jpeg_bytes": sum(map(len, frames)),
                        "frame_bytes_min": min(map(len, frames)),
                        "frame_bytes_max": max(map(len, frames)),
                        "frames_sha256": [hashlib.sha256(frame).hexdigest() for frame in frames]})
        (preview / f"embedded-{scene}.jpg").write_bytes(frames[0])
    manifest = {"format": "Baseline JPEG SOF0, 8-bit YCbCr 4:2:0",
                "width": WIDTH, "height": HEIGHT, "fps": fps,
                "quality": quality, "pillow_version": PILLOW_VERSION,
                "ffmpeg": ffmpeg, "payload_bytes": payload,
                "target_bytes": args.target_bytes, "hard_max_bytes": HARD_MAX_PAYLOAD,
                "frame_tables_bytes_esp32": sum(map(len, clips)) * 8 + 3 * 8,
                "mapping_note": "Source filenames differ from visible scenery. "
                                "Scene order follows actual daylight, sunset, moonlit night.",
                "validation": "Every JPEG marker header checked; every frame decoded with Pillow; "
                              "dimensions and baseline 420 format verified; source SHA256 unchanged.",
                "attempts": attempts, "clips": results}
    (TOOLS / "embedded_animation_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {sum(map(len, clips))} verified frames at {fps} FPS / quality {quality}.")
    print(f"Payload: {payload:,} bytes; frame tables: {manifest['frame_tables_bytes_esp32']:,} bytes.")
    for result in results:
        print(f"{result['scene']}: {result['frame_count']} frames, "
              f"{result['duration_seconds']:.3f} s, {result['jpeg_bytes']:,} bytes")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
