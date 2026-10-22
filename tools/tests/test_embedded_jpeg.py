#!/usr/bin/env python3
"""Decode every generated flash JPEG with the actual TJpg_Decoder C decoder.

Compile jpeg_decoder_c_api.c with the installed library's ORIGINAL tjpgd.c and
headers into a host shared library. Example on Linux/macOS:

  cc -O2 -std=c11 -Wall -Wextra -fPIC -shared \\
    -I /path/to/TJpg_Decoder/src tools/tests/jpeg_decoder_c_api.c \\
    /path/to/TJpg_Decoder/src/tjpgd.c -o /tmp/libjpegverify.so
  python3 tools/tests/test_embedded_jpeg.py /tmp/libjpegverify.so

Uses only the Python standard library. Checks actual generated C++ bytes,
manifest hashes, decoder return codes, output size, complete pixel coverage,
callback bounds, duplicate pixels, and workspace/output buffer guard bytes.
This is a host compatibility test; it does not measure ESP32 playback speed.
"""

from __future__ import annotations

import ctypes
import hashlib
import json
from pathlib import Path
import re
import sys


class DecoderReport(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in (
        "prepare_result", "decode_result", "width", "height", "coverage_pixels",
        "rectangles", "output_checksum", "canaries_ok", "callbacks_ok",
        "workspace_remaining")]


def run(library: Path) -> None:
    project = Path(__file__).resolve().parents[2]
    manifest = json.loads((project / "tools/embedded_animation_manifest.json").read_text())
    cpp = (project / "DigitalClock/EmbeddedAnimations.cpp").read_text()
    arrays = re.findall(r"static const uint8_t (\w+)\[\] PROGMEM = \{([^}]+)\};", cpp)
    frames = {name: bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", body))
              for name, body in arrays}
    assert len(arrays) == len(frames), "Duplicate generated array name"
    lib = ctypes.CDLL(str(library.resolve()))
    lib.test_decode_jpeg.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.POINTER(DecoderReport)]
    lib.test_decode_jpeg.restype = ctypes.c_int
    total_frames = 0
    total_bytes = 0
    total_rectangles = 0
    min_workspace_remaining = 0xFFFFFFFF
    checksum = hashlib.sha256()
    for clip in manifest["clips"]:
        clip_bytes = 0
        assert len(clip["frames_sha256"]) == clip["frame_count"]
        for index, expected in enumerate(clip["frames_sha256"]):
            name = f"{clip['scene']}_{index:03d}"
            data = frames[name]
            assert hashlib.sha256(data).hexdigest() == expected, name
            data_buffer = ctypes.create_string_buffer(data)
            report = DecoderReport()
            ok = lib.test_decode_jpeg(data_buffer, len(data), ctypes.byref(report))
            details = {field: getattr(report, field) for field, _ in report._fields_}
            assert ok == 1, (name, details)
            assert report.prepare_result == report.decode_result == 0, (name, details)
            assert (report.width, report.height) == (320, 240), (name, details)
            assert report.coverage_pixels == 76800, (name, details)
            assert report.rectangles == 300, (name, details)  # Full 16x16 MCUs, 20x15.
            assert report.canaries_ok == report.callbacks_ok == 1, (name, details)
            assert data_buffer.raw[:len(data)] == data, "Decoder modified compressed input"
            checksum.update(report.output_checksum.to_bytes(4, "little"))
            min_workspace_remaining = min(min_workspace_remaining, report.workspace_remaining)
            total_rectangles += report.rectangles
            total_frames += 1
            clip_bytes += len(data)
        assert clip_bytes == clip["jpeg_bytes"]
        total_bytes += clip_bytes
        print(f"PASS {clip['scene']}: {clip['frame_count']} frames, {clip_bytes:,} bytes, "
              "all jd_prepare/jd_decomp results JDR_OK")
    assert total_frames == len(frames)
    assert total_bytes == manifest["payload_bytes"]
    print(f"PASS: {total_frames} complete 320x240 frames; {total_rectangles:,} output blocks; "
          f"{total_frames * 76800:,} pixels checked")
    print(f"All guard bytes intact; minimum decoder workspace remaining: {min_workspace_remaining} bytes")
    print(f"Decoded RGB565 checksum fingerprint: {checksum.hexdigest()}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: test_embedded_jpeg.py /path/to/libjpegverify.so")
    run(Path(sys.argv[1]))
