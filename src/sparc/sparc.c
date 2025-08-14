#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "sparc.h"
#include "uarch.h"
#include "udev.h"
#include "../common/udev.h"
#include "../common/global.h"

static char *hv_vendors_name[] = {
  [HV_VENDOR_KVM]       = "KVM",
  [HV_VENDOR_QEMU]      = "QEMU",
  [HV_VENDOR_VBOX]      = "VirtualBox",
  [HV_VENDOR_HYPERV]    = "Microsoft Hyper-V",
  [HV_VENDOR_VMWARE]    = "VMware",
  [HV_VENDOR_XEN]       = "Xen",
  [HV_VENDOR_PARALLELS] = "Parallels",
  [HV_VENDOR_PHYP]      = "pHyp",
  [HV_VENDOR_BHYVE]     = "bhyve",
  [HV_VENDOR_APPLEVZ]   = "Apple VZ",
  [HV_VENDOR_INVALID]   = STRING_UNKNOWN
};

struct cache* get_cache_info(struct cpuInfo* cpu) {
  struct cache* cach = emalloc(sizeof(struct cache));
  init_cache_struct(cach);

  cach->L1i->size = get_l1i_cache_size(0);
  cach->L1d->size = get_l1d_cache_size(0);
  cach->L2->size = get_l2_cache_size(0);
  cach->L3->size = get_l3_cache_size(0);

  if(cach->L1i->size > 0) {
    cach->L1i->exists = true;
    cach->L1i->num_caches = get_num_caches_by_level(cpu, 0);
    cach->max_cache_level = 1;
  }
  if(cach->L1d->size > 0) {
    cach->L1d->exists = true;
    cach->L1d->num_caches = get_num_caches_by_level(cpu, 1);
    cach->max_cache_level = 2;
  }
  if(cach->L2->size > 0) {
    cach->L2->exists = true;
    cach->L2->num_caches = get_num_caches_by_level(cpu, 2);
    cach->max_cache_level = 3;
  }
  if(cach->L3->size > 0) {
    cach->L3->exists = true;
    cach->L3->num_caches = get_num_caches_by_level(cpu, 3);
    cach->max_cache_level = 4;
  }

  return cach;
}

struct topology* get_topology_info(struct cache* cach) {
  struct topology* topo = emalloc(sizeof(struct topology));
  init_topology_struct(topo, cach);

  if((topo->total_cores = sysconf(_SC_NPROCESSORS_ONLN)) == -1) {
    printWarn("sysconf(_SC_NPROCESSORS_ONLN): %s", strerror(errno));
    topo->total_cores = 1;
  }

  int* core_ids = emalloc(sizeof(int) * topo->total_cores);
  int* package_ids = emalloc(sizeof(int) * topo->total_cores);

  if(!fill_core_ids_from_sys(core_ids, topo->total_cores)) {
    printWarn("fill_core_ids_from_sys failed, output may be incomplete/invalid");
    for(int i=0; i < topo->total_cores; i++) core_ids[i] = 0;
  }

  if(!fill_package_ids_from_sys(package_ids, topo->total_cores)) {
    printWarn("fill_package_ids_from_sys failed, output may be incomplete/invalid");
    for(int i=0; i < topo->total_cores; i++) package_ids[i] = 0;
    topo->sockets = get_num_sockets_package_cpus(topo);
    if (topo->sockets == UNKNOWN_DATA) {
      printWarn("get_num_sockets_package_cpus failed: assuming 1 socket");
      topo->sockets = 1;
    }
  }
  else {
    int *package_ids_count = emalloc(sizeof(int) * topo->total_cores);
    for(int i=0; i < topo->total_cores; i++) {
      package_ids_count[i] = 0;
    }
    for(int i=0; i < topo->total_cores; i++) {
      package_ids_count[package_ids[i]]++;
    }
    for(int i=0; i < topo->total_cores; i++) {
      if(package_ids_count[i] != 0) {
        topo->sockets++;
      }
    }
    free(package_ids_count);
  }

  int *core_ids_unified = emalloc(sizeof(int) * topo->total_cores);
  for(int i=0; i < topo->total_cores; i++) {
    core_ids_unified[i] = -1;
  }
  bool found = false;
  for(int i=0; i < topo->total_cores; i++) {
    for(int j=0; j < topo->total_cores && !found; j++) {
      if(core_ids_unified[j] == core_ids[i]) found = true;
    }
    if(!found) {
      core_ids_unified[topo->physical_cores] = core_ids[i];
      topo->physical_cores++;
    }
    found = false;
  }

  topo->physical_cores = topo->physical_cores / topo->sockets;
  topo->logical_cores = topo->total_cores / topo->sockets;
  topo->smt_supported = topo->logical_cores / topo->physical_cores;

  free(core_ids);
  free(package_ids);
  free(core_ids_unified);

  return topo;
}

static char* get_cpu_name_from_cpuinfo(void) {
  // Debian sparc64 uses "cpu\t\t: UltraSparc ..." or similar; fall back to model name if present
  char* model = get_field_from_cpuinfo("cpu\t\t: ");
  if (model == NULL) {
    model = get_field_from_cpuinfo("model name\t: ");
  }
  if (model == NULL) return NULL;

  // Normalize some common SPARC strings
  // 1) UltraSparc -> UltraSPARC
  char* pos = strstr(model, "UltraSparc");
  if (pos != NULL) {
    memcpy(pos, "UltraSPARC", strlen("UltraSPARC"));
  }
  // 2) TI UltraSPARC* -> Sun UltraSPARC*
  if (strncmp(model, "TI ", 3) == 0) {
    size_t len = strlen(model);
    char* fixed = ecalloc(len + 1, sizeof(char));
    strcpy(fixed, "Sun ");
    strcpy(fixed + 4, model + 3);
    free(model);
    model = fixed;
  }
  return model;
}

struct frequency* get_frequency_info(void) {
  struct frequency* freq = emalloc(sizeof(struct frequency));

  freq->measured = false;
  freq->max = get_max_freq_from_file(0);
  freq->base = get_min_freq_from_file(0);

  if(freq->max == UNKNOWN_DATA) {
    freq->max = get_frequency_from_cpuinfo();
  }

  return freq;
}

int64_t get_peak_performance(struct cpuInfo* cpu, struct topology* topo, int64_t freq) {
  if(freq == UNKNOWN_DATA) {
    return -1;
  }
  // Conservative: 1 FLOP per cycle per core (no VIS accounted)
  int64_t flops = topo->physical_cores * topo->sockets * (freq * 1000000);
  return flops;
}

struct hypervisor* get_hp_info(void) {
  struct hypervisor* hv = emalloc(sizeof(struct hypervisor));
  hv->present = false;
  hv->hv_vendor = HV_VENDOR_INVALID;
  hv->hv_name = hv_vendors_name[hv->hv_vendor];
  return hv;
}

char* get_str_topology(struct topology* topo, bool dual_socket) {
  char* string;
  if(topo->smt_supported > 1) {
    uint32_t size = 3+3+17+1;
    string = emalloc(sizeof(char)*size);
    if(dual_socket)
      snprintf(string, size, "%d cores (%d threads)", topo->physical_cores * topo->sockets, topo->logical_cores * topo->sockets);
    else
      snprintf(string, size, "%d cores (%d threads)",topo->physical_cores,topo->logical_cores);
  }
  else {
    uint32_t size = 3+7+1;
    string = emalloc(sizeof(char)*size);
    if(dual_socket)
      snprintf(string, size, "%d cores",topo->physical_cores * topo->sockets);
    else
      snprintf(string, size, "%d cores",topo->physical_cores);
  }
  return string;
}


void print_debug(struct cpuInfo* cpu) {
  printf("Name: %s\n", cpu->cpu_name != NULL ? cpu->cpu_name : "Unknown");
}

struct cpuInfo* get_cpu_info(void) {
  struct cpuInfo* cpu = emalloc(sizeof(struct cpuInfo));
  struct features* feat = emalloc(sizeof(struct features));
  cpu->feat = feat;

  bool *ptr = &(feat->AES);
  for(uint32_t i = 0; i < sizeof(struct features)/sizeof(bool); i++, ptr++) {
    *ptr = false;
  }

  cpu->cpu_name = get_cpu_name_from_cpuinfo();
  cpu->hv = get_hp_info();
  cpu->arch = get_uarch(cpu);
  cpu->cach = get_cache_info(cpu);
  cpu->topo = get_topology_info(cpu->cach);
  cpu->freq = get_frequency_info();
  cpu->peak_performance = get_peak_performance(cpu, cpu->topo, get_freq(cpu->freq));

  return cpu;
}

void free_topo_struct(struct topology* topo) {
  free(topo);
}

