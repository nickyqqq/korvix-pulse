#!/usr/bin/env python3
"""Build the clock's complete IANA catalog and exact local-time lookup tables.

Python 3.9+; no third-party packages required. The Python Software Foundation's
tzdata wheel contains IANA's compiled TZif files. The names are cross-checked
against IANA's original source archive, including all backward aliases.

    python tools/generate_timezones.py

The firmware uses deduplicated transitions because POSIX TZ strings alone
cannot express every IANA rule (for example Palestine's year-specific dates),
and ESP32 newlib cannot parse negative transition hours used by Greenland.
The .rule member is retained as an informational / configTzTime fallback, but
timeZoneLocalTime() is the authoritative clock conversion within its range.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import datetime as dt
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import tarfile
import urllib.request
import zipfile
from zoneinfo import ZoneInfo


REGIONS = (
    "Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic",
    "Australia", "Europe", "Indian", "Pacific", "Etc", "Other",
)
SOURCE_FILES = (
    "africa", "antarctica", "asia", "australasia", "europe", "northamerica",
    "southamerica", "etcetera", "backward", "factory",
)
UTC = dt.timezone.utc
NAYAR_URL = "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv"


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "ESP32-Digital-Clock-tz-generator/1"})
    with urllib.request.urlopen(request, timeout=90) as response:
        return response.read()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_tzif(data: bytes) -> tuple[str, list[int]]:
    """Read RFC 9636's 64-bit block and footer; validate rather than guessing."""
    if data[:4] != b"TZif" or data[4:5] not in (b"2", b"3", b"4"):
        raise ValueError("Expected a TZif version 2, 3, or 4 file")

    def block_end(header: int, width: int) -> tuple[int, tuple[int, ...]]:
        counts = struct.unpack_from(">6I", data, header + 20)
        utc_count, std_count, leap_count, time_count, type_count, char_count = counts
        size = time_count * (width + 1) + type_count * 6 + char_count
        size += leap_count * (width + 4) + std_count + utc_count
        return header + 44 + size, counts

    second, _ = block_end(0, 4)
    if data[second:second + 4] != b"TZif":
        raise ValueError("Missing TZif 64-bit header")
    footer, counts = block_end(second, 8)
    times = list(struct.unpack_from(f">{counts[3]}q", data, second + 44))
    tail = data[footer:]
    if not tail.startswith(b"\n") or not tail.endswith(b"\n"):
        raise ValueError("Missing TZif POSIX footer")
    rule = tail[1:-1].decode("ascii")
    # All current IANA tails match this syntax. Reject future changes for review.
    abbr = r"(?:[A-Za-z]{3,10}|<[-+0-9A-Za-z]{3,10}>)"
    offset = r"[-+]?\d{1,3}(?::\d{1,2}(?::\d{1,2})?)?"
    transition = r"(?:M\d{1,2}\.[1-5]\.[0-6]|J?\d{1,3})(?:/" + offset + r")?"
    syntax = abbr + offset + r"(?:" + abbr + r"(?:" + offset + r")?," + transition + "," + transition + r")?"
    if not rule or not re.fullmatch(syntax, rule):
        raise ValueError(f"Unexpected POSIX rule: {rule!r}")
    return rule, times


def clock_state(zone: ZoneInfo, stamp: int) -> tuple[int, int]:
    local = dt.datetime.fromtimestamp(stamp, UTC).astimezone(zone)
    return int(local.utcoffset().total_seconds()), int(bool(local.dst()))


def transitions(data: bytes, start: int, end: int) -> tuple[tuple[int, int, int], ...]:
    """Use exact TZif transitions, then resolve its recurring tail via ZoneInfo.

    TZif explicit transitions are authoritative, including close or irregular
    pairs. Once those end, IANA's POSIX footer has two seasonal transitions per
    year; a 12-hour scan finds their interval, then binary search the exact second.
    """
    zone = ZoneInfo.from_file(io.BytesIO(data))
    _, explicit = parse_tzif(data)
    current = clock_state(zone, start)
    result = [(0, *current)]
    last = start
    for stamp in explicit:
        if start < stamp < end:
            state = clock_state(zone, stamp)
            if state != current:
                result.append((stamp - start, *state))
                current = state
            last = stamp
    # Explicit tables can extend beyond the requested end, requiring no tail.
    if explicit and explicit[-1] >= end:
        return tuple(result)
    last = max(start, explicit[-1] if explicit else start)
    current = clock_state(zone, last)
    cursor = last
    while cursor < end - 1:
        probe = min(cursor + 43200, end - 1)
        state = clock_state(zone, probe)
        if state != current:
            low, high = cursor + 1, probe
            while low < high:
                mid = (low + high) // 2
                if clock_state(zone, mid) == current:
                    low = mid + 1
                else:
                    high = mid
            current = clock_state(zone, high)
            result.append((high - start, *current))
        cursor = probe
    if any(a[0] >= b[0] for a, b in zip(result, result[1:])):
        raise ValueError("Non-monotonic transition sequence")
    return tuple(result)


def validate_sequence(data: bytes, sequence: tuple, start: int, end: int) -> None:
    """Compare lookup results to ZoneInfo before/at/after every transition.

    Monthly interior samples additionally exercise both hemispheres, leap
    years, half-hour DST, irregular rules, and permanent-offset changes.
    """
    zone = ZoneInfo.from_file(io.BytesIO(data))
    stamps = [record[0] for record in sequence]
    probes = {start, end - 1}
    for relative, _, _ in sequence:
        probes.update(start + relative + delta for delta in (-1, 0, 1))
    first_year = dt.datetime.fromtimestamp(start, UTC).year
    last_year = dt.datetime.fromtimestamp(end - 1, UTC).year
    for year in range(first_year, last_year + 1):
        for month in range(1, 13):
            probes.add(int(dt.datetime(year, month, 15, 12, tzinfo=UTC).timestamp()))
    for stamp in probes:
        if start <= stamp < end:
            record = sequence[bisect.bisect_right(stamps, stamp - start) - 1]
            expected = clock_state(zone, stamp)
            if record[1:] != expected:
                raise ValueError(f"Transition validation failed at {stamp}: {record[1:]} != {expected}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-version", default="2026.4")
    parser.add_argument("--iana-version", default="2026d")
    parser.add_argument("--start-year", type=int, default=2020)
    parser.add_argument("--end-year", type=int, default=2101,
                        help="Exclusive end year (2101 includes all of 2100)")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "DigitalClock" / "TimeZones.h")
    args = parser.parse_args()
    if not (1970 <= args.start_year < args.end_year <= 2101):
        parser.error("Require 1970 <= start < end <= 2101")
    start = int(dt.datetime(args.start_year, 1, 1, tzinfo=UTC).timestamp())
    end = int(dt.datetime(args.end_year, 1, 1, tzinfo=UTC).timestamp())
    if end - start > 0xFFFFFFFF:
        parser.error("Transition span must fit uint32_t seconds")

    meta_url = f"https://pypi.org/pypi/tzdata/{args.package_version}/json"
    meta = json.loads(fetch(meta_url))
    wheel_meta = next(item for item in meta["urls"] if item["filename"].endswith(".whl"))
    wheel_bytes = fetch(wheel_meta["url"])
    if sha256(wheel_bytes) != wheel_meta["digests"]["sha256"]:
        raise ValueError("tzdata wheel SHA-256 mismatch")
    wheel = zipfile.ZipFile(io.BytesIO(wheel_bytes))
    package_init = wheel.read("tzdata/__init__.py").decode()
    match = re.search(r'IANA_VERSION\s*=\s*[\'"]([^\'"]+)', package_init)
    if not match or match.group(1) != args.iana_version:
        raise ValueError("Python package does not contain the requested IANA release")
    names = wheel.read("tzdata/zones").decode().splitlines()
    if len(names) != len(set(names)):
        raise ValueError("Duplicate names in the package's zone catalog")

    source_url = f"https://data.iana.org/time-zones/releases/tzdata{args.iana_version}.tar.gz"
    source_bytes = fetch(source_url)
    archive = tarfile.open(fileobj=io.BytesIO(source_bytes), mode="r:gz")
    if archive.extractfile("version").read().decode().strip() != args.iana_version:
        raise ValueError("Unexpected IANA source archive version")
    expected_names = set()
    for source_file in SOURCE_FILES:
        for line in archive.extractfile(source_file).read().decode().splitlines():
            fields = line.split()
            if fields and fields[0] == "Zone":
                expected_names.add(fields[1])
            elif fields and fields[0] == "Link":
                expected_names.add(fields[2])
    if expected_names != set(names):
        raise ValueError(f"IANA source / wheel name mismatch: {expected_names ^ set(names)}")
    # Ensure every entry from the commonly used Arduino POSIX catalog is covered.
    reference_bytes = fetch(NAYAR_URL)
    reference_names = {row[0] for row in csv.reader(io.StringIO(reference_bytes.decode())) if row}
    if reference_names - set(names):
        raise ValueError(f"Reference catalog names missing: {reference_names - set(names)}")

    def region(name: str) -> int:
        prefix = name.split("/", 1)[0]
        return REGIONS.index(prefix) if prefix in REGIONS else len(REGIONS) - 1

    names.sort(key=lambda name: (region(name), name))
    entries = []
    patterns = []
    pattern_index = {}
    data_cache = {}
    negative_rule_names = []
    for name in names:
        data = wheel.read("tzdata/zoneinfo/" + name)
        rule, _ = parse_tzif(data)
        if "/-" in rule:
            negative_rule_names.append(name)
        if data not in data_cache:
            data_cache[data] = transitions(data, start, end)
            validate_sequence(data, data_cache[data], start, end)
        sequence = data_cache[data]
        if sequence not in pattern_index:
            pattern_index[sequence] = len(patterns)
            patterns.append(sequence)
        entries.append((name, rule, region(name), pattern_index[sequence]))
    if len(patterns) > 65535:
        raise ValueError("Too many patterns for uint16_t")

    states = sorted({(offset, dst) for seq in patterns for _, offset, dst in seq})
    if len(states) > 256:
        raise ValueError("Too many states for uint8_t")
    state_index = {state: index for index, state in enumerate(states)}
    stamps, state_ids, pattern_bounds = [], [], []
    for seq in patterns:
        pattern_bounds.append((len(stamps), len(seq)))
        for stamp, offset, dst in seq:
            stamps.append(stamp)
            state_ids.append(state_index[offset, dst])
    if len(stamps) > 65535:
        raise ValueError("Too many transitions for uint16_t indexes")

    def quoted(text: str) -> str:
        return json.dumps(text, ensure_ascii=True)

    built = dt.datetime.now(UTC).date().isoformat()
    out = [
        "// Generated by tools/generate_timezones.py; do not hand-edit.",
        f"// IANA {args.iana_version}; python/tzdata {args.package_version}; built {built}.",
        f"// {len(entries)} names (all IANA zones + backward aliases); {len(patterns)} deduplicated schedules.",
        f"// Exact IANA conversions: {args.start_year}-01-01 <= UTC < {args.end_year}-01-01.",
        "// Use timeZoneLocalTime(), not localtime_r(): POSIX tails omit irregular",
        "// transitions and ESP32 newlib does not support signed transition hours.",
        "// IANA data is public domain. See tools/timezone_catalog_provenance.json.",
        "#pragma once", "#include <stddef.h>", "#include <stdint.h>", "#include <time.h>", "",
        "struct TimeZoneEntry { const char *name; const char *rule; uint8_t region; };",
        "static const char *const TIME_ZONE_REGIONS[] = {",
        *[f"  {quoted(label)}," for label in REGIONS], "};",
        "static constexpr size_t TIME_ZONE_REGION_COUNT = sizeof(TIME_ZONE_REGIONS) / sizeof(TIME_ZONE_REGIONS[0]);",
        f"static const char TIME_ZONE_DATABASE_VERSION[] = {quoted(args.iana_version)};",
        "static const TimeZoneEntry TIME_ZONES[] = {",
        *[f"  {{{quoted(name)}, {quoted(rule)}, {reg}}}," for name, rule, reg, _ in entries],
        "};",
        "static constexpr size_t TIME_ZONE_COUNT = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);", "",
        "// Compact, flash-resident state and transition tables; no heap allocation.",
        "struct TimeZoneClockState { int32_t offset; int8_t daylight; };",
        "struct TimeZonePattern { uint16_t first; uint16_t count; };",
        "static const TimeZoneClockState TIME_ZONE_STATES[] = {",
        *[f"  {{{offset}, {dst}}}," for offset, dst in states], "};",
        "static const TimeZonePattern TIME_ZONE_PATTERNS[] = {",
        *[f"  {{{first}, {count}}}," for first, count in pattern_bounds], "};",
        "static const uint16_t TIME_ZONE_PATTERN_IDS[] = {",
    ]
    for i in range(0, len(entries), 20):
        out.append("  " + ", ".join(str(item[3]) for item in entries[i:i + 20]) + ",")
    out += ["};", "static const uint32_t TIME_ZONE_TRANSITION_SECONDS[] = {"]
    for i in range(0, len(stamps), 10):
        out.append("  " + ", ".join(str(value) + "UL" for value in stamps[i:i + 10]) + ",")
    out += ["};", "static const uint8_t TIME_ZONE_TRANSITION_STATES[] = {"]
    for i in range(0, len(state_ids), 30):
        out.append("  " + ", ".join(str(value) for value in state_ids[i:i + 30]) + ",")
    out += ["};", "", f"static constexpr int64_t TIME_ZONE_VALID_FROM = {start}LL;",
            f"static constexpr int64_t TIME_ZONE_VALID_UNTIL = {end}LL;", "",
            "// Returns false outside the bundled range or for an invalid zone index.",
            "// UTC from NTP remains unchanged. UTC+offset is decoded with gmtime_r.",
            "static inline bool timeZoneLocalTime(size_t index, time_t utc, struct tm *result) {",
            "  if (!result || index >= TIME_ZONE_COUNT) return false;",
            "  const int64_t stamp = static_cast<int64_t>(utc);",
            "  if (stamp < TIME_ZONE_VALID_FROM || stamp >= TIME_ZONE_VALID_UNTIL) return false;",
            "  const uint32_t relative = static_cast<uint32_t>(stamp - TIME_ZONE_VALID_FROM);",
            "  const TimeZonePattern &pattern = TIME_ZONE_PATTERNS[TIME_ZONE_PATTERN_IDS[index]];",
            "  size_t low = pattern.first;",
            "  size_t high = low + pattern.count;",
            "  while (low + 1 < high) {",
            "    const size_t middle = low + (high - low) / 2;",
            "    if (TIME_ZONE_TRANSITION_SECONDS[middle] <= relative) low = middle;",
            "    else high = middle;",
            "  }",
            "  const TimeZoneClockState &state = TIME_ZONE_STATES[TIME_ZONE_TRANSITION_STATES[low]];",
            "  const time_t shifted = static_cast<time_t>(stamp + state.offset);",
            "  if (!gmtime_r(&shifted, result)) return false;",
            "  result->tm_isdst = state.daylight;",
            "  return true;",
            "}", ""]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(out), encoding="utf-8")

    provenance = {
        "generated_utc_date": built,
        "iana_version": args.iana_version,
        "python_tzdata_version": args.package_version,
        "catalog_names": len(entries),
        "iana_zones_and_links_verified": len(expected_names),
        "reference_csv_entries_covered": len(reference_names),
        "regions": list(REGIONS),
        "valid_utc_start_inclusive": f"{args.start_year}-01-01T00:00:00Z",
        "valid_utc_end_exclusive": f"{args.end_year}-01-01T00:00:00Z",
        "unique_schedules": len(patterns),
        "transition_records": len(stamps),
        "verification": "Every distinct TZif schedule checked against Python ZoneInfo monthly and one second before/at/after each transition.",
        "sources": {
            "iana": {"url": source_url, "sha256": sha256(source_bytes)},
            "python_tzdata": {"url": wheel_meta["url"], "sha256": sha256(wheel_bytes)},
            "reference_csv_names_only": {"url": NAYAR_URL, "sha256": sha256(reference_bytes)},
            "newlib_parser": "https://github.com/espressif/newlib-esp32/blob/esp-4.3.0/newlib/libc/time/tzset_r.c",
        },
        "limitations": [
            "Firmware is a snapshot of IANA rules, not an automatic timezone database updater. Regenerate and reflash after law changes.",
            "POSIX .rule strings describe indefinite future rules only; they are not historical IANA transition tables.",
            "Use timeZoneLocalTime for clock/calendar conversion. It includes exact bundled IANA transitions within the validity window, including Palestine and Greenland.",
            "Factory is IANA's unspecified-local-time placeholder, included only to preserve catalog completeness.",
            "Etc/GMT offsets follow IANA/POSIX's reversed sign convention: Etc/GMT+7 means UTC minus 7 hours.",
            "tm_zone/tm_gmtoff, where provided by a host libc, are not populated with zone metadata; clock firmware uses calendar fields and tm_isdst.",
        ],
        "posix_rules_incompatible_with_newlib_signed_transition_parser": negative_rule_names,
        "data_license": "IANA tz database data is public domain; Python tzdata package Apache-2.0. Only derived IANA data is included in the generated header.",
    }
    provenance_path = Path(__file__).with_name("timezone_catalog_provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output}: {len(entries)} names, {len(patterns)} schedules, {len(stamps)} transitions")
    print(f"Verified all {len(expected_names)} IANA names and {len(reference_names)} reference CSV names")


if __name__ == "__main__":
    main()
