#ifndef __CPUFETCH_ALPHA_UARCH__
#define __CPUFETCH_ALPHA_UARCH__

struct uarch;

struct uarch* get_uarch(struct cpuInfo* cpu);
char* get_str_uarch(struct cpuInfo* cpu);
char* get_str_process(struct cpuInfo* cpu);
void free_uarch_struct(struct uarch* arch);

#endif


