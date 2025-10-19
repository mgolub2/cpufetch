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

struct uarch* get_uarch(struct cpuInfo* cpu) {
  UNUSED(cpu);
  // Alpha exposes model strings like "cpu\t\t: Alpha EV56"; try several keys
  char* model = get_field_from_cpuinfo("cpu\t\t: ");
  if (!model) model = get_field_from_cpuinfo("model name\t: ");
  if (!model) model = get_field_from_cpuinfo("cpu model\t: ");
  if (!model) model = strdup_or_null("Unknown");
  return make_uarch(model, NULL);
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


