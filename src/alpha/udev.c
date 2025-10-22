#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

// Parse Alpha's "cycle frequency [Hz]" field
// Example: "cycle frequency [Hz]    : 616541423 est."
long get_frequency_from_cpuinfo_alpha(void) {
  char* hz_str = get_field_from_cpuinfo("cycle frequency [Hz]    : ");
  if (!hz_str) return UNKNOWN_DATA;
  errno = 0;
  char* end = NULL;
  unsigned long long hz = strtoull(hz_str, &end, 10);
  free(hz_str);
  if (errno != 0) return UNKNOWN_DATA;
  if (hz == 0) return UNKNOWN_DATA;
  long mhz = (long)(hz / 1000000ULL);
  if (mhz < 50 || mhz > 10000) return UNKNOWN_DATA;
  return mhz;
}

// Parse Alpha cache sizes from /proc/cpuinfo
// Example: "L1 Icache               : 64K, 2-way, 64b line"
static long parse_alpha_cache_kb(const char* field) {
  char* line = get_field_from_cpuinfo((char*)field);
  if (!line) return -1;
  errno = 0;
  char* end = NULL;
  long kb = strtol(line, &end, 10);
  free(line);
  if (errno != 0 || kb <= 0) return -1;
  return kb * 1024; // convert KB to bytes
}

long get_l1i_cache_size_alpha(void) {
  return parse_alpha_cache_kb("L1 Icache\t\t: ");
}

long get_l1d_cache_size_alpha(void) {
  return parse_alpha_cache_kb("L1 Dcache\t\t: ");
}

long get_l2_cache_size_alpha(void) {
  return parse_alpha_cache_kb("L2 cache\t\t: ");
}


