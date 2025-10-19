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


