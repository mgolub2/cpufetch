#ifndef __CPUFETCH_POWERPC__
#define __CPUFETCH_POWERPC__

#include <stdbool.h>

/* Ensure 'bool' is available even if altivec.h undefines it. */
#if !defined(__cplusplus) && !defined(bool)
  typedef _Bool bool;
  #ifndef true
    #define true 1
  #endif
  #ifndef false
    #define false 0
  #endif
#endif

#include "../common/cpu.h"

struct cpuInfo* get_cpu_info(void);
char* get_str_altivec(struct cpuInfo* cpu);
char* get_str_topology(struct topology* topo, bool dual_socket);
void print_debug(struct cpuInfo* cpu);

#endif
