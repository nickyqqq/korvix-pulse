#!/usr/bin/env python3
"""Run actual ManualTime.h C++ code against pinned IANA 2026d ZoneInfo data.

Linux/macOS host example (g++ or clang++):
  c++ -std=c++11 -O2 -fPIC -shared tools/tests/manual_time_c_api.cpp -o /tmp/libmanual_time.so
  python3 tools/tests/test_manual_time.py /tmp/libmanual_time.so

Python 3.9+ only. The reference wheel is downloaded in memory, hash-checked,
and not installed. Tests do not change the device clock or timezone state.
"""

from __future__ import annotations

import ctypes
import datetime as dt
import hashlib
import io
import json
from pathlib import Path
import random
import sys
import urllib.request
import zipfile
from zoneinfo import ZoneInfo

VALID, AMBIGUOUS, NONEXISTENT, INVALID = range(4)
UTC = dt.timezone.utc
START, UNTIL = 1577836800, 4133980800
Fields = ctypes.c_int * 6
I64 = ctypes.c_int64


def stamp(*fields: int) -> int:
    return int(dt.datetime(*fields, tzinfo=UTC).timestamp())


def run(library: Path) -> None:
    lib = ctypes.CDLL(str(library.resolve()))
    lib.manual_test_zone_index.argtypes = [ctypes.c_char_p]
    lib.manual_test_zone_index.restype = ctypes.c_int
    lib.manual_test_days_in_month.argtypes = [ctypes.c_int, ctypes.c_int]
    lib.manual_test_days_in_month.restype = ctypes.c_int
    lib.manual_test_convert.argtypes = [ctypes.c_size_t, ctypes.POINTER(ctypes.c_int),
                                        ctypes.POINTER(I64), ctypes.POINTER(I64)]
    lib.manual_test_convert.restype = ctypes.c_int
    lib.manual_test_from_utc.argtypes = [ctypes.c_size_t, I64, ctypes.POINTER(ctypes.c_int)]
    lib.manual_test_from_utc.restype = ctypes.c_int

    def index(name: str) -> int:
        value = lib.manual_test_zone_index(name.encode())
        assert value >= 0, name
        return value

    def convert(name: str, fields: tuple, expected: int, early: int | None = None,
                late: int | None = None) -> None:
        first, last = I64(-123456), I64(-654321)
        actual = lib.manual_test_convert(index(name), Fields(*fields), ctypes.byref(first), ctypes.byref(last))
        assert actual == expected, (name, fields, actual, expected)
        if expected in (VALID, AMBIGUOUS):
            assert first.value == early, (name, fields, first.value, early)
            assert last.value == (early if late is None else late), (name, fields, last.value, late)
        else:
            assert (first.value, last.value) == (-123456, -654321), "Failure modified outputs"

    assert lib.manual_test_days_in_month(2000, 2) == 29
    assert lib.manual_test_days_in_month(2024, 2) == 29
    assert lib.manual_test_days_in_month(2100, 2) == 28
    assert lib.manual_test_days_in_month(1900, 2) == 28
    assert lib.manual_test_days_in_month(2026, 0) == 0
    assert lib.manual_test_days_in_month(2026, 13) == 0
    assert lib.manual_test_days_in_month(0, 1) == 0
    convert("Asia/Bangkok", (2024, 2, 29, 12, 0, 0), VALID, stamp(2024, 2, 29, 5, 0, 0))
    for fields in ((2026, 2, 29, 0, 0, 0), (2100, 2, 29, 0, 0, 0),
                   (2019, 12, 31, 23, 59, 59), (2101, 1, 1, 0, 0, 0),
                   (2026, 4, 31, 0, 0, 0), (2026, 0, 1, 0, 0, 0),
                   (2026, 1, 0, 0, 0, 0), (2026, 1, 1, 24, 0, 0),
                   (2026, 1, 1, 0, 60, 0), (2026, 1, 1, 0, 0, 60)):
        convert("Etc/UTC", fields, INVALID)
    convert("America/New_York", (2026, 3, 8, 2, 30, 0), NONEXISTENT)
    convert("America/New_York", (2026, 11, 1, 1, 30, 0), AMBIGUOUS,
            stamp(2026, 11, 1, 5, 30, 0), stamp(2026, 11, 1, 6, 30, 0))
    convert("Asia/Kathmandu", (2026, 9, 25, 12, 34, 56), VALID, stamp(2026, 9, 25, 6, 49, 56))
    convert("Australia/Lord_Howe", (2026, 10, 4, 2, 15, 0), NONEXISTENT)
    convert("Australia/Lord_Howe", (2026, 4, 5, 1, 45, 0), AMBIGUOUS,
            stamp(2026, 4, 4, 14, 45, 0), stamp(2026, 4, 4, 15, 15, 0))
    convert("Australia/Lord_Howe", (2026, 4, 5, 2, 0, 0), VALID, stamp(2026, 4, 4, 15, 30, 0))
    convert("Etc/UTC", (2020, 1, 1, 0, 0, 0), VALID, START)
    convert("Etc/UTC", (2100, 12, 31, 23, 59, 59), VALID, UNTIL - 1)
    convert("Pacific/Kiritimati", (2020, 1, 1, 0, 0, 0), INVALID)
    convert("Pacific/Kiritimati", (2020, 1, 1, 14, 0, 0), VALID, START)
    convert("Pacific/Pago_Pago", (2100, 12, 31, 23, 59, 59), INVALID)
    convert("Pacific/Pago_Pago", (2100, 12, 31, 12, 59, 59), VALID, UNTIL - 1)
    first, last = I64(123), I64(456)
    assert lib.manual_test_convert(9999, Fields(2026, 1, 1, 0, 0, 0), ctypes.byref(first), ctypes.byref(last)) == INVALID
    assert (first.value, last.value) == (123, 456)
    assert lib.manual_test_convert(index("America/New_York"), Fields(2026, 11, 1, 1, 30, 0), ctypes.byref(first), None) == AMBIGUOUS
    assert first.value == stamp(2026, 11, 1, 5, 30, 0)
    for zone, utc in ((9999, START), (index("Etc/UTC"), START - 1), (index("Etc/UTC"), UNTIL)):
        values = Fields(1, 2, 3, 4, 5, 6)
        assert lib.manual_test_from_utc(zone, utc, values) == 0
        assert list(values) == [1, 2, 3, 4, 5, 6]

    # Check the C++ implementation against independently decoded IANA TZif data.
    provenance = json.loads((Path(__file__).parents[1] / "timezone_catalog_provenance.json").read_text())
    source = provenance["sources"]["python_tzdata"]
    data = urllib.request.urlopen(source["url"], timeout=90).read()
    assert hashlib.sha256(data).hexdigest() == source["sha256"]
    wheel = zipfile.ZipFile(io.BytesIO(data))
    names = wheel.read("tzdata/zones").decode().splitlines()
    rng = random.Random(2432028)
    checked = 0
    for name in names:
        zone = ZoneInfo.from_file(io.BytesIO(wheel.read("tzdata/zoneinfo/" + name)))
        zone_index = index(name)
        for _ in range(40):
            utc = rng.randrange(START + 86400, UNTIL - 86400)
            reference = dt.datetime.fromtimestamp(utc, UTC).astimezone(zone)
            fields = (reference.year, reference.month, reference.day, reference.hour, reference.minute, reference.second)
            candidates = set()
            for fold in (0, 1):
                candidate = int(reference.replace(fold=fold).timestamp())
                reverse = dt.datetime.fromtimestamp(candidate, UTC).astimezone(zone)
                if reverse.replace(tzinfo=None) == reference.replace(tzinfo=None):
                    candidates.add(candidate)
            expected = sorted(candidates)
            convert(name, fields, AMBIGUOUS if len(expected) > 1 else VALID, expected[0], expected[-1])
            values = Fields()
            assert lib.manual_test_from_utc(zone_index, utc, values) == 1
            assert tuple(values) == fields, (name, utc, tuple(values), fields)
            checked += 1
    print(f"PASS: manual converter edge cases and {checked:,} IANA-reference round trips across {len(names)} zones")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: test_manual_time.py /path/to/libmanual_time.so")
    run(Path(sys.argv[1]))
