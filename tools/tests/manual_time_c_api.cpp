// Host-only C ABI for test_manual_time.py; it exercises the firmware header.
#include "../../DigitalClock/ManualTime.h"
#include <string.h>

extern "C" {

int manual_test_zone_index(const char *name) {
  for (size_t index = 0; index < TIME_ZONE_COUNT; ++index) {
    if (strcmp(TIME_ZONES[index].name, name) == 0) return static_cast<int>(index);
  }
  return -1;
}

int manual_test_days_in_month(int year, int month) {
  return daysInMonth(year, month);
}

int manual_test_convert(size_t zone, const int *fields, int64_t *early, int64_t *late) {
  const ManualDateTime local = {fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]};
  time_t first = static_cast<time_t>(*early);
  time_t last = late ? static_cast<time_t>(*late) : 0;
  const LocalTimeResult result = manualLocalToUtc(zone, local, first, late ? &last : nullptr);
  *early = static_cast<int64_t>(first);
  if (late) *late = static_cast<int64_t>(last);
  return static_cast<int>(result);
}

int manual_test_from_utc(size_t zone, int64_t utc, int *fields) {
  ManualDateTime local = {fields[0], fields[1], fields[2], fields[3], fields[4], fields[5]};
  const bool result = manualUtcToLocal(zone, static_cast<time_t>(utc), local);
  fields[0] = local.year;
  fields[1] = local.month;
  fields[2] = local.day;
  fields[3] = local.hour;
  fields[4] = local.minute;
  fields[5] = local.second;
  return result ? 1 : 0;
}

}
