#ifndef __UARCH_SPARC__
#define __UARCH_SPARC__

#include <stdint.h>
#include "sparc.h"

struct uarch;

struct uarch* get_uarch(struct cpuInfo* cpu);
char* get_str_uarch(struct cpuInfo* cpu);
char* get_str_process(struct cpuInfo* cpu);
void free_uarch_struct(struct uarch* arch);

#endif
