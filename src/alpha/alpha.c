// Ensure ALPHA-specific fields are visible to static analysis
#ifndef ARCH_ALPHA
#define ARCH_ALPHA 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdbool.h>
#include <errno.h>

#include "alpha.h"
#include "uarch.h"
#include "udev.h"
#include "../common/udev.h"
#include "../common/global.h"
#include "../common/args.h"

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

static char* get_cpu_name_from_cpuinfo(void) {
  // Try several Alpha cpuinfo keys
  char* s = get_field_from_cpuinfo("cpu\t\t: ");
  if (!s) s = get_field_from_cpuinfo("cpu model\t: ");
  if (!s) s = get_field_from_cpuinfo("model name\t: ");
  return s;
}

struct cache* get_cache_info(struct cpuInfo* cpu) {
  struct cache* cach = emalloc(sizeof(struct cache));
  init_cache_struct(cach);

  // Alpha exposes limited cache info in modern kernels; keep conservative defaults
  cach->max_cache_level = 2;
  for(int i=0; i < cach->max_cache_level + 1; i++) {
    cach->cach_arr[i]->exists = true;
    cach->cach_arr[i]->num_caches = 1;
    cach->cach_arr[i]->size = 0;
  }
  return cach;
}

struct topology* get_topology_info(struct cache* cach) {
  struct topology* topo = emalloc(sizeof(struct topology));
  init_topology_struct(topo, cach);

  long online = sysconf(_SC_NPROCESSORS_ONLN);
  if (online <= 0) online = 1;
  topo->total_cores = (int32_t)online;
  topo->sockets = 1;
  topo->physical_cores = topo->total_cores;
  topo->logical_cores = topo->total_cores;
  topo->smt_supported = 1;
  return topo;
}

struct frequency* get_frequency_info(void) {
  struct frequency* freq = emalloc(sizeof(struct frequency));
  freq->measured = false;
  freq->max = get_max_freq_from_file(0);
  freq->base = get_min_freq_from_file(0);
  if(freq->max == UNKNOWN_DATA) {
    long mhz = get_frequency_from_cpuinfo_alpha();
    if (mhz != UNKNOWN_DATA) freq->max = mhz;
  }
  return freq;
}

// Accurate peak performance using runtime measurement (scalar FP32)
static int64_t measure_peak_performance_f32(struct topology* topo) {
  const char* env = getenv("CPUFETCH_MEASURE_SP_FLOPS");
  bool enabled = accurate_pp() || (env != NULL && env[0] == '1');
  if(!enabled) return -1;

  struct timeval t0, t1;
  double target_seconds = 2.0;
  if(env != NULL && env[0] == '1') {
    const char* dur = getenv("CPUFETCH_MEASURE_SP_FLOPS_SECS");
    if(dur != NULL) {
      double v = atof(dur);
      if(v > 0.05 && v < 30.0) target_seconds = v;
    }
  }

  volatile float a = 1.0f, b = 1.0001f, c = 0.9997f, d = 1.0003f;
  volatile float e = 0.5f, f = 1.5f, g = 2.0f, h = -0.25f;
  int ops_per_iter = 32;
  uint64_t iters = 0;
  if(gettimeofday(&t0, NULL) != 0) return -1;
  for(;;) {
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    iters++;
    if((iters & 0x3FF) == 0) {
      if(gettimeofday(&t1, NULL) != 0) break;
      double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_usec - t0.tv_usec) / 1e6;
      if(elapsed >= target_seconds) {
        double per_core = ((double)iters * ops_per_iter) / elapsed;
        double total = per_core * (double)(topo->physical_cores * topo->sockets);
        if(total <= 0.0) return -1;
        return (int64_t) total;
      }
    }
  }
  return -1;
}

// Measure integer ops throughput (approximate)
static int64_t measure_int_ops_throughput(struct topology* topo) {
  if(!accurate_pp_with_ops()) return -1;
  struct timeval t0, t1;
  volatile uint64_t x = 0x0102030405060708ULL;
  volatile uint64_t y = 0x1122334455667788ULL;
  volatile uint64_t z = 0xFFEEDDCCBBAA9988ULL;
  int ops_per_iter = 24;
  uint64_t iters = 0;
  if(gettimeofday(&t0, NULL) != 0) return -1;
  for(;;) {
    x ^= y; y += z; z ^= x; x = (x << 1) | (x >> 63);
    y = (y << 2) | (y >> 62); z = (z << 3) | (z >> 61);
    x += y; y ^= z; z -= x;
    iters++;
    if((iters & 0x3FF) == 0) {
      if(gettimeofday(&t1, NULL) != 0) break;
      double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_usec - t0.tv_usec) / 1e6;
      if(elapsed >= 2.0) {
        double per_core = ((double)iters * ops_per_iter) / elapsed;
        double total = per_core * (double)(topo->physical_cores * topo->sockets);
        if(total <= 0.0) return -1;
        return (int64_t) total;
      }
    }
  }
  return -1;
}

static int64_t get_peak_performance_estimate(struct cpuInfo* cpu, struct topology* topo, int64_t freq) {
  if(freq == UNKNOWN_DATA) return -1;
  int flops_per_cycle = 1; // conservative default for Alpha scalar FP
  return topo->physical_cores * topo->sockets * (freq * 1000000) * (int64_t)flops_per_cycle;
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
  cpu->hv = emalloc(sizeof(struct hypervisor));
  cpu->hv->present = false;
  cpu->hv->hv_vendor = HV_VENDOR_INVALID;
  cpu->hv->hv_name = hv_vendors_name[cpu->hv->hv_vendor];
  cpu->arch = get_uarch(cpu);
  cpu->cach = get_cache_info(cpu);
  cpu->topo = get_topology_info(cpu->cach);
  cpu->freq = get_frequency_info();

  int64_t measured = measure_peak_performance_f32(cpu->topo);
  if(measured > 0) cpu->peak_performance = measured;
  else cpu->peak_performance = get_peak_performance_estimate(cpu, cpu->topo, get_freq(cpu->freq));

  cpu->vis_ops_performance = measure_int_ops_throughput(cpu->topo);

  return cpu;
}

void free_topo_struct(struct topology* topo) {
  free(topo);
}


