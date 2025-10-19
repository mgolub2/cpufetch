#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

// Fallback frequency getter for Alpha via /proc/cpuinfo
// Parse: "cpu MHz\t\t: 600.000" → 600
long get_frequency_from_cpuinfo_alpha(void) {
  char* mhz = get_field_from_cpuinfo("cpu MHz\t\t: ");
  if (mhz == NULL) return UNKNOWN_DATA;
  errno = 0;
  char* end = NULL;
  double mhz_d = strtod(mhz, &end);
  free(mhz);
  if (errno != 0) return UNKNOWN_DATA;
  long v = (long)(mhz_d + 0.5);
  if (v < 50 || v > 10000) return UNKNOWN_DATA;
  return v;
}

// Try multiple Alpha-specific keys and return first non-empty value (caller frees)
char* alpha_cpuinfo_get_value_for_key(const char* key) {
  char* v = get_field_from_cpuinfo((char*)key);
  if (v && v[0] != '\0') return v;
  if (v) free(v);
  return NULL;
}


