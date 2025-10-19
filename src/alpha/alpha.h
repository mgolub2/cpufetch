#ifndef __CPUFETCH_ALPHA__
#define __CPUFETCH_ALPHA__

#include "../common/cpu.h"

struct cpuInfo* get_cpu_info(void);
char* get_str_topology(struct topology* topo, bool dual_socket);
void print_debug(struct cpuInfo* cpu);
void free_topo_struct(struct topology* topo);

#endif


