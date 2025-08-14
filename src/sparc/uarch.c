#include <stdlib.h>
#include <string.h>

#include "../common/global.h"
#include "../common/udev.h"
#include "uarch.h"

struct uarch {
  char* name;
  char* process;
};

static struct uarch* make_uarch(const char* name, const char* process) {
  struct uarch* ua = emalloc(sizeof(struct uarch));
  ua->name = NULL;
  ua->process = NULL;
  if (name != NULL) {
    ua->name = emalloc(strlen(name) + 1);
    strcpy(ua->name, name);
  }
  if (process != NULL) {
    ua->process = emalloc(strlen(process) + 1);
    strcpy(ua->process, process);
  }
  return ua;
}

static char* strdup_or_null(const char* s) {
  if (!s) return NULL;
  char* d = emalloc(strlen(s)+1);
  strcpy(d, s);
  return d;
}

static char* map_pmu_to_uarch(const char* pmu) {
  if (!pmu) return NULL;
  if (strstr(pmu, "ultra3i")) return strdup_or_null("Cheetah+");
  if (strstr(pmu, "ultra3+")) return strdup_or_null("Cheetah+");
  if (strstr(pmu, "ultra3"))  return strdup_or_null("Cheetah");
  if (strstr(pmu, "ultra4+")) return strdup_or_null("UltraSPARC IV+");
  if (strstr(pmu, "ultra12")) return strdup_or_null("UltraSPARC I/II");
  return NULL;
}

struct uarch* get_uarch(struct cpuInfo* cpu) {
  UNUSED(cpu);
  // Prefer MMU Type (e.g., Cheetah+) as microarchitecture
  char* mmu = get_field_from_cpuinfo("MMU Type\t\t: ");
  char* pmu = NULL;
  char* uarch_name = NULL;
  if (mmu && strlen(mmu) > 0) {
    uarch_name = strdup_or_null(mmu);
  } else {
    pmu = get_field_from_cpuinfo("pmu\t\t: ");
    uarch_name = map_pmu_to_uarch(pmu);
  }

  if (!uarch_name) {
    uarch_name = strdup_or_null("Unknown");
  }

  if (mmu) free(mmu);
  if (pmu) free(pmu);

  return make_uarch(uarch_name, NULL);
}

char* get_str_uarch(struct cpuInfo* cpu) {
  if (cpu->arch == NULL) return NULL;
  return cpu->arch->name;
}

char* get_str_process(struct cpuInfo* cpu) {
  if (cpu->arch == NULL || cpu->arch->process == NULL) return NULL;
  return cpu->arch->process;
}

void free_uarch_struct(struct uarch* arch) {
  if (arch == NULL) return;
  free(arch->name);
  free(arch->process);
  free(arch);
}

