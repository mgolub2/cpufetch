#ifndef __CPUFETCH_SPARC__
#define __CPUFETCH_SPARC__

#include "../common/cpu.h"

struct cpuInfo* get_cpu_info(void);
char* get_str_topology(struct topology* topo, bool dual_socket);
void print_debug(struct cpuInfo* cpu);
void free_topo_struct(struct topology* topo);
char* get_str_features(struct cpuInfo* cpu);

#endif
