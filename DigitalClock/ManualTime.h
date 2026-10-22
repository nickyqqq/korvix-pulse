#pragma once

#include "TimeZones.h"

// A Gregorian wall-clock date in the selected timezone. Months are 1..12.
struct ManualDateTime {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};

enum class LocalTimeResult {
  Valid,
  Ambiguous,    // A backward clock change repeats this wall-clock time.
  Nonexistent,  // A forward clock change skips this wall-clock time.
  Invalid       // Invalid fields, zone index, or outside the bundled UTC range.
};

// Returns zero for an invalid month/year. Gregorian century rules apply.
static inline int daysInMonth(int year, int month) {
  if (year < 1 || month < 1 || month > 12) return 0;
  static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return lengths[month - 1] + ((month == 2 && leap) ? 1 : 0);
}

namespace manual_time_detail {

static inline bool validFields(const ManualDateTime &value) {
  return value.year >= 2020 && value.year <= 2100 &&
         value.month >= 1 && value.month <= 12 &&
         value.day >= 1 && value.day <= daysInMonth(value.year, value.month) &&
         value.hour >= 0 && value.hour <= 23 &&
         value.minute >= 0 && value.minute <= 59 &&
         value.second >= 0 && value.second <= 59;
}

// Treat wall-clock fields as UTC solely to obtain a comparable integer.
// All input has already been validated; no libc timezone state is consulted.
static inline int64_t civilSeconds(const ManualDateTime &value) {
  static const uint16_t beforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const int64_t priorYear = static_cast<int64_t>(value.year) - 1;
  const int64_t leapsThroughPriorYear = priorYear / 4 - priorYear / 100 + priorYear / 400;
  // There were 477 Gregorian leap years through 1969.
  int64_t days = 365LL * (value.year - 1970) + leapsThroughPriorYear - 477;
  days += beforeMonth[value.month - 1] + value.day - 1;
  if (value.month > 2 && daysInMonth(value.year, 2) == 29) ++days;
  return days * 86400LL + value.hour * 3600LL + value.minute * 60LL + value.second;
}

static inline bool matches(const ManualDateTime &value, const struct tm &roundTrip) {
  return roundTrip.tm_year + 1900 == value.year && roundTrip.tm_mon + 1 == value.month &&
         roundTrip.tm_mday == value.day && roundTrip.tm_hour == value.hour &&
         roundTrip.tm_min == value.minute && roundTrip.tm_sec == value.second;
}

// Find the minimum and maximum local seconds supported by this zone's table.
// This distinguishes an out-of-range boundary date from a DST gap without
// assuming that the first/last offset is the zone's smallest/largest offset.
static inline bool insideLocalCoverage(size_t zone, int64_t localSeconds) {
  const TimeZonePattern &pattern = TIME_ZONE_PATTERNS[TIME_ZONE_PATTERN_IDS[zone]];
  int64_t minimum = INT64_MAX;
  int64_t maximum = INT64_MIN;
  const size_t last = static_cast<size_t>(pattern.first) + pattern.count;
  for (size_t index = pattern.first; index < last; ++index) {
    const int32_t offset = TIME_ZONE_STATES[TIME_ZONE_TRANSITION_STATES[index]].offset;
    const int64_t begin = TIME_ZONE_VALID_FROM + TIME_ZONE_TRANSITION_SECONDS[index] + offset;
    const int64_t until = index + 1 < last
        ? TIME_ZONE_VALID_FROM + TIME_ZONE_TRANSITION_SECONDS[index + 1] + offset
        : TIME_ZONE_VALID_UNTIL + offset;
    if (begin < minimum) minimum = begin;
    if (until > maximum) maximum = until;
  }
  return localSeconds >= minimum && localSeconds < maximum;
}

}  // namespace manual_time_detail

// Convert a local date to UTC using the exact bundled IANA transitions.
// Valid: utc and, when supplied, *laterUtc receive the unique UTC timestamp.
// Ambiguous: utc receives the earlier occurrence, *laterUtc the later one.
// Invalid/Nonexistent: neither output is modified. Leap seconds are rejected.
// No allocation, TZ environment changes, mktime(), or system-clock writes.
static inline LocalTimeResult manualLocalToUtc(size_t zone, const ManualDateTime &local,
                                               time_t &utc, time_t *laterUtc = nullptr) {
  static_assert(sizeof(time_t) >= 8, "Manual time requires a 64-bit time_t");
  if (zone >= TIME_ZONE_COUNT || !manual_time_detail::validFields(local)) {
    return LocalTimeResult::Invalid;
  }
  const int64_t naive = manual_time_detail::civilSeconds(local);
  int64_t earliest = INT64_MAX;
  int64_t latest = INT64_MIN;
  bool found = false;
  const size_t stateCount = sizeof(TIME_ZONE_STATES) / sizeof(TIME_ZONE_STATES[0]);
  for (size_t index = 0; index < stateCount; ++index) {
    const int32_t offset = TIME_ZONE_STATES[index].offset;
    // The generated table sorts by (offset, DST), so equal offsets are adjacent.
    if (index > 0 && offset == TIME_ZONE_STATES[index - 1].offset) continue;
    const int64_t candidate = naive - offset;
    if (candidate < TIME_ZONE_VALID_FROM || candidate >= TIME_ZONE_VALID_UNTIL) continue;
    struct tm roundTrip{};
    if (!timeZoneLocalTime(zone, static_cast<time_t>(candidate), &roundTrip) ||
        !manual_time_detail::matches(local, roundTrip)) continue;
    if (candidate < earliest) earliest = candidate;
    if (candidate > latest) latest = candidate;
    found = true;
  }
  if (!found) {
    return manual_time_detail::insideLocalCoverage(zone, naive)
        ? LocalTimeResult::Nonexistent : LocalTimeResult::Invalid;
  }
  if (laterUtc) *laterUtc = static_cast<time_t>(latest);
  utc = static_cast<time_t>(earliest);
  return earliest == latest ? LocalTimeResult::Valid : LocalTimeResult::Ambiguous;
}

// Initialize the manual editor from a UTC timestamp. Failure leaves local intact.
static inline bool manualUtcToLocal(size_t zone, time_t utc, ManualDateTime &local) {
  struct tm converted{};
  if (!timeZoneLocalTime(zone, utc, &converted)) return false;
  const ManualDateTime candidate = {
      converted.tm_year + 1900, converted.tm_mon + 1, converted.tm_mday,
      converted.tm_hour, converted.tm_min, converted.tm_sec};
  if (!manual_time_detail::validFields(candidate)) return false;
  local = candidate;
  return true;
}
