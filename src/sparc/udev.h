#ifndef __CPUFETCH_SPARC_UDEV__
#define __CPUFETCH_SPARC_UDEV__

#include <stdbool.h>

bool fill_core_ids_from_sys(int *core_ids, int total_cores);
bool fill_package_ids_from_sys(int* package_ids, int total_cores);
long get_frequency_from_cpuinfo(void);

// SPARC-specific cache size helpers (parse /proc/cpuinfo with sysfs fallback)
long get_l1i_cache_size_sparc(uint32_t core);
long get_l1d_cache_size_sparc(uint32_t core);
long get_l2_cache_size_sparc(uint32_t core);
long get_l3_cache_size_sparc(uint32_t core);

#endif
