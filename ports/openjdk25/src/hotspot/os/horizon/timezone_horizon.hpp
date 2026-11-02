// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Java Switch contributors. All rights reserved.
 * Modified 2026-11-02: calculate the active local UTC offset from converted
 * calendar values because Horizon newlib's struct tm has no tm_gmtoff.
 */

#ifndef OS_HORIZON_TIMEZONE_HORIZON_HPP
#define OS_HORIZON_TIMEZONE_HORIZON_HPP

#include <cstdint>
#include <ctime>

namespace horizon {

// Gregorian era/day arithmetic follows Howard Hinnant's civil calendar
// algorithms, placed in the public domain: https://howardhinnant.github.io/date_algorithms.html
// Convert a broken-down Gregorian date to seconds on a UTC-like calendar.
// This deliberately does not apply timezone rules; the difference between a
// localtime() result and a gmtime() result is the active offset at that instant.
inline bool calendar_seconds(const std::tm& value, int64_t* result) {
  if (result == nullptr || value.tm_mon < 0 || value.tm_mon >= 12) {
    return false;
  }
  const int64_t year = static_cast<int64_t>(value.tm_year) + 1900;
  const int month = value.tm_mon + 1;
  if (month < 1 || month > 12 || value.tm_hour < 0 || value.tm_hour > 23 ||
      value.tm_min < 0 || value.tm_min > 59 || value.tm_sec < 0 || value.tm_sec > 60) {
    return false;
  }
  const bool leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
  const int month_lengths[] = {31, leap ? 29 : 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
  if (value.tm_mday < 1 || value.tm_mday > month_lengths[month - 1]) {
    return false;
  }

  const int64_t adjusted_year = year - (month <= 2 ? 1 : 0);
  const int64_t era = adjusted_year >= 0 ? adjusted_year / 400
                                         : (adjusted_year - 399) / 400;
  const int64_t year_of_era = adjusted_year - era * 400;
  const int adjusted_month = month + (month > 2 ? -3 : 9);
  const int64_t day_of_year = (153 * adjusted_month + 2) / 5 + value.tm_mday - 1;
  const int64_t day_of_era = year_of_era * 365 + year_of_era / 4 -
                             year_of_era / 100 + day_of_year;
  const int64_t days = era * 146097 + day_of_era - 719468;
  *result = ((days * 24 + value.tm_hour) * 60 + value.tm_min) * 60 + value.tm_sec;
  return true;
}

// Return local time minus UTC in seconds. The bound rejects corrupt conversion
// results and keeps the caller's ISO-8601 hour field representable.
inline bool timezone_offset_seconds(const std::tm& local, const std::tm& utc,
                                    int64_t* result) {
  if (result == nullptr) {
    return false;
  }
  int64_t local_seconds;
  int64_t utc_seconds;
  if (!calendar_seconds(local, &local_seconds) || !calendar_seconds(utc, &utc_seconds)) {
    return false;
  }
  const int64_t offset = local_seconds - utc_seconds;
  if (offset < -86400 || offset > 86400) {
    return false;
  }
  *result = offset;
  return true;
}

}  // namespace horizon

#endif  // OS_HORIZON_TIMEZONE_HORIZON_HPP
