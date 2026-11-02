#include "../ports/openjdk25/src/hotspot/os/horizon/timezone_horizon.hpp"

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <limits>

static int64_t offset_at(const char* zone, std::time_t instant) {
  assert(setenv("TZ", zone, 1) == 0);
  tzset();
  std::tm local{};
  std::tm utc{};
  assert(localtime_r(&instant, &local) != nullptr);
  assert(gmtime_r(&instant, &utc) != nullptr);
  int64_t offset = 0;
  assert(horizon::timezone_offset_seconds(local, utc, &offset));
  return offset;
}

int main() {
  assert(offset_at("UTC0", 1710000000) == 0);
  assert(offset_at("JST-5:30", 1710000000) == 5 * 3600 + 30 * 60);
  assert(offset_at("PST8", 1710000000) == -8 * 3600);

  // This POSIX zone advances by 30 minutes at the March transition.
  const std::time_t before = 1710055800;  // 2024-03-10 07:30:00 UTC
  const std::time_t after = 1710057600;   // 2024-03-10 08:00:00 UTC
  assert(offset_at("XXX5:45YYY5:15,M3.2.0/2,M11.1.0/2", before) == -(5 * 3600 + 45 * 60));
  assert(offset_at("XXX5:45YYY5:15,M3.2.0/2,M11.1.0/2", after) == -(5 * 3600 + 15 * 60));

  std::tm invalid{};
  invalid.tm_year = 124;
  invalid.tm_mon = 1;
  invalid.tm_mday = 30;
  invalid.tm_hour = 12;
  int64_t offset = 0;
  int64_t calendar_value = 0;
  assert(!horizon::timezone_offset_seconds(invalid, invalid, &offset));
  assert(!horizon::timezone_offset_seconds(invalid, invalid, nullptr));
  assert(!horizon::calendar_seconds(invalid, nullptr));
  invalid.tm_mon = std::numeric_limits<int>::max();
  assert(!horizon::calendar_seconds(invalid, &calendar_value));
  std::puts("OpenJDK 25 timezone host test: PASS");
  return 0;
}
