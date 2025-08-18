#include <stdlib.h>
#include <string.h>

#include "../common/global.h"
#include "../common/udev.h"
#include "uarch.h"

// Keep structure simple as on SPARC: name and optional process string
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

// Map model strings to a stable microarchitecture name
// Linux PA-RISC /proc/cpuinfo commonly exposes a line like "model\t\t: PA8900"
static char* get_uarch_name_from_cpuinfo(void) {
  // Prefer CPU line which includes PA8900/PA8800, etc.
  char* cpu = get_field_from_cpuinfo("cpu\t\t: ");
  if (cpu != NULL && strlen(cpu) > 0) {
    return cpu; // e.g., "PA8900 (PCX-U+)"
  }
  // Fallback: model may be a chassis like "9000/800/..."; still show something if CPU missing
  char* model = get_field_from_cpuinfo("model\t\t: ");
  if (model != NULL && strlen(model) > 0) {
    return model;
  }
  return NULL;
}

struct uarch* get_uarch(struct cpuInfo* cpu) {
  UNUSED(cpu);
  char* name = get_uarch_name_from_cpuinfo();
  if (!name) {
    name = strdup_or_null("Unknown");
  }
  return make_uarch(name, NULL);
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

