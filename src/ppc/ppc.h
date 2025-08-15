#ifndef __CPUFETCH_POWERPC__
#define __CPUFETCH_POWERPC__

#include "../common/cpu.h"

/* Ensure 'bool' is available even on toolchains where AltiVec headers
 * or unusual include orders interfere with stdbool. Define a safe
 * fallback before any dependent headers are parsed. */
#if !defined(__cplusplus) && !defined(__bool_true_false_are_defined)
  typedef _Bool bool;
  #ifndef true
    #define true 1
  #endif
  #ifndef false
    #define false 0
  #endif
  #define __bool_true_false_are_defined 1
#endif

struct cpuInfo* get_cpu_info(void);
char* get_str_altivec(struct cpuInfo* cpu);
char* get_str_topology(struct topology* topo, bool dual_socket);
void print_debug(struct cpuInfo* cpu);

#endif
