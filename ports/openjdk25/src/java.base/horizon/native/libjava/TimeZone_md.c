/* SPDX-License-Identifier: GPL-2.0-only */
/* Horizon implementation of the TimeZone_md.h contract from pinned OpenJDK 25.
 * Newlib's POSIX TZ strings are the only system-zone source used here; Horizon
 * firmware does not expose a region identifier through this java.base layer.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "TimeZone_md.h"

static char *copy_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (copy != NULL) memcpy(copy, value, length);
    return copy;
}

static int parse_digits(const char **cursor, int maximum, int *value) {
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return 0;
    int result = 0;
    while (*p >= '0' && *p <= '9') {
        int digit = *p++ - '0';
        if (result > (maximum - digit) / 10) return 0;
        result = result * 10 + digit;
    }
    *cursor = p;
    *value = result;
    return 1;
}

static int parse_hms(const char **cursor, int max_hour, int signed_value) {
    const char *p = *cursor;
    if (signed_value && (*p == '+' || *p == '-')) ++p;
    int hours;
    if (!parse_digits(&p, max_hour, &hours)) return 0;
    int sub_hour = 0;
    if (*p == ':') {
        ++p;
        if (!parse_digits(&p, 59, &sub_hour)) return 0;
        if (*p == ':') {
            ++p;
            int seconds;
            if (!parse_digits(&p, 59, &seconds)) return 0;
            if (max_hour == 24 && hours == 24 && seconds != 0) return 0;
        }
    }
    if (max_hour == 24 && hours == 24 && sub_hour != 0) return 0;
    *cursor = p;
    return 1;
}

static int parse_abbreviation(const char **cursor) {
    const char *p = *cursor;
    size_t length = 0;
    if (*p == '<') {
        ++p;
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
               (*p >= '0' && *p <= '9') || *p == '+' || *p == '-') {
            ++p;
            ++length;
        }
        if (length < 3 || *p != '>') return 0;
        ++p;
    } else {
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            ++p;
            ++length;
        }
        if (length < 3) return 0;
    }
    *cursor = p;
    return 1;
}

static int parse_rule(const char **cursor) {
    const char *p = *cursor;
    int first, second, third;
    if (*p == 'J') {
        ++p;
        if (!parse_digits(&p, 365, &first) || first == 0) return 0;
    } else if (*p == 'M') {
        ++p;
        if (!parse_digits(&p, 12, &first) || first == 0 || *p++ != '.') return 0;
        if (!parse_digits(&p, 5, &second) || second == 0 || *p++ != '.') return 0;
        if (!parse_digits(&p, 6, &third)) return 0;
    } else {
        if (!parse_digits(&p, 365, &first)) return 0;
    }
    if (*p == '/') {
        ++p;
        if (!parse_hms(&p, 167, 1)) return 0;
    }
    *cursor = p;
    return 1;
}

/* Accept the POSIX TZ grammar implemented by newlib. In particular, reject
 * zoneinfo paths, arbitrary labels and incomplete DST forms that tzset may
 * silently interpret as UTC or with implementation-defined transition rules.
 */
static int valid_native_tz(const char *value) {
    if (value == NULL || *value == '\0' || *value == ':') return 0;
    const char *p = value;
    if (!parse_abbreviation(&p) || !parse_hms(&p, 24, 1)) return 0;
    if (*p == '\0') return 1;
    if (!parse_abbreviation(&p)) return 0;
    if (*p != ',') {
        if (!parse_hms(&p, 24, 1)) return 0;
    }
    if (*p++ != ',' || !parse_rule(&p) || *p++ != ',' ||
        !parse_rule(&p) || *p != '\0') return 0;
    return 1;
}

static int64_t days_from_civil(const struct tm *value) {
    int year = value->tm_year + 1900;
    unsigned month = (unsigned)value->tm_mon + 1;
    unsigned day = (unsigned)value->tm_mday;
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned shifted_month = month > 2 ? month - 3 : month + 9;
    unsigned day_of_year = (153 * shifted_month + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
                          year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

static char *current_gmt_offset_id(void) {
    time_t now = time(NULL);
    struct tm local_value;
    struct tm utc_value;
    if (now == (time_t)-1 || localtime_r(&now, &local_value) == NULL ||
        gmtime_r(&now, &utc_value) == NULL) return NULL;

    int64_t seconds = (days_from_civil(&local_value) -
                       days_from_civil(&utc_value)) * 86400 +
                      (local_value.tm_hour - utc_value.tm_hour) * 3600 +
                      (local_value.tm_min - utc_value.tm_min) * 60 +
                      (local_value.tm_sec - utc_value.tm_sec);
    if (seconds < -86400 || seconds > 86400 || seconds % 60 != 0) return NULL;
    if (seconds == 0) return copy_string("GMT");

    char buffer[32];
    int64_t magnitude = seconds < 0 ? -seconds : seconds;
    int written = snprintf(buffer, sizeof(buffer), "GMT%c%02lld:%02lld",
                           seconds < 0 ? '-' : '+',
                           (long long)(magnitude / 3600),
                           (long long)((magnitude % 3600) / 60));
    if (written < 0 || (size_t)written >= sizeof(buffer)) return NULL;
    return copy_string(buffer);
}

char *findJavaTZ_md(const char *java_home_dir) {
    (void)java_home_dir;
    const char *tz = getenv("TZ");
    /* A missing or unsupported native TZ leaves the system zone unknown.
     * TimeZone.setDefaultZone() then applies its documented GMT fallback.
     */
    if (!valid_native_tz(tz)) return NULL;
    tzset();
    /* The native runtime supplies only the current offset, not a Java region
     * ID or portable future DST rules. Return that explicit offset form.
     */
    return current_gmt_offset_id();
}

char *getGMTOffsetID(void) {
    const char *tz = getenv("TZ");
    if (!valid_native_tz(tz)) return NULL;
    tzset();
    return current_gmt_offset_id();
}
