#ifndef __CPUFETCH_SPARC_UDEV__
#define __CPUFETCH_SPARC_UDEV__

#include <stdbool.h>

bool fill_core_ids_from_sys(int *core_ids, int total_cores);
bool fill_package_ids_from_sys(int* package_ids, int total_cores);
long get_frequency_from_cpuinfo(void);

#endif
