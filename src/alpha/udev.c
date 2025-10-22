#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

// Parse Alpha's "cycle frequency [Hz]" field
// Example: "cycle frequency [Hz]    : 616541423 est."
long get_frequency_from_cpuinfo_alpha(void) {
  // Try the canonical tab-separated format first
  char* hz_str = get_field_from_cpuinfo("cycle frequency [Hz]\t: ");
  if (hz_str == NULL) {
    // Fallback: Alpha kernels sometimes use spaces instead of tabs before the colon
    // Search for the field name without the trailing whitespace and parse manually
    hz_str = get_field_from_cpuinfo("cycle frequency [Hz]");
    if (hz_str == NULL) return UNKNOWN_DATA;
    // Skip any combination of spaces, tabs and colon characters until we reach the first digit
    char* ptr = hz_str;
    while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == ':')) ptr++;
    errno = 0;
    char* end = NULL;
    unsigned long long hz = strtoull(ptr, &end, 10);
    free(hz_str);
    if (errno != 0 || hz == 0) return UNKNOWN_DATA;
    long mhz = (long)(hz / 1000000ULL);
    if (mhz < 50 || mhz > 10000) return UNKNOWN_DATA;
    return mhz;
  }

  // If the first attempt succeeded we can parse directly (string already starts with the number)
  errno = 0;
  char* end = NULL;
  unsigned long long hz = strtoull(hz_str, &end, 10);
  free(hz_str);
  if (errno != 0 || hz == 0) return UNKNOWN_DATA;
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


