#ifndef __UARCH_PARISC__
#define __UARCH_PARISC__

#include <stdint.h>
#include "parisc.h"

struct uarch;

struct uarch* get_uarch(struct cpuInfo* cpu);
char* get_str_uarch(struct cpuInfo* cpu);
char* get_str_process(struct cpuInfo* cpu);
void free_uarch_struct(struct uarch* arch);

#endif
