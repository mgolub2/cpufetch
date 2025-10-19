#ifndef __UDEV_ALPHA__
#define __UDEV_ALPHA__

#include <stdint.h>

long get_frequency_from_cpuinfo_alpha(void);
char* alpha_cpuinfo_get_value_for_key(const char* key);

#endif


