// Ensure PARISC-specific fields are visible to static analysis
#ifndef ARCH_PARISC
#define ARCH_PARISC 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdbool.h>
#include <errno.h>

#include "parisc.h"
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

struct cache* get_cache_info(struct cpuInfo* cpu) {
  struct cache* cach = emalloc(sizeof(struct cache));
  init_cache_struct(cach);

  // Prefer PARISC-specific /proc/cpuinfo keys; fallback to generic sysfs
  cach->L1i->size = get_l1i_cache_size_parisc(0);
  cach->L1d->size = get_l1d_cache_size_parisc(0);
  cach->L2->size = get_l2_cache_size_parisc(0);
  cach->L3->size = get_l3_cache_size_parisc(0);

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

  // 1. Total cores
  if((topo->total_cores = sysconf(_SC_NPROCESSORS_ONLN)) == -1) {
    printWarn("sysconf(_SC_NPROCESSORS_ONLN): %s", strerror(errno));
    topo->total_cores = 1; // fallback
  }

  // 2. Sockets and core/thread breakdown
  int* core_ids = emalloc(sizeof(int) * topo->total_cores);
  int* package_ids = emalloc(sizeof(int) * topo->total_cores);

  if(!fill_core_ids_from_sys(core_ids, topo->total_cores)) {
    printWarn("fill_core_ids_from_sys failed, output may be incomplete/invalid");
    for(int i=0; i < topo->total_cores; i++) core_ids[i] = i;
  }

  if(!fill_package_ids_from_sys(package_ids, topo->total_cores)) {
    printWarn("fill_package_ids_from_sys failed, output may be incomplete/invalid");
    // Use package_cpus bitmaps via common helper when available
    topo->sockets = get_num_sockets_package_cpus(topo);
    if (topo->sockets == UNKNOWN_DATA || topo->sockets <= 0) {
      topo->sockets = 1;
    }
  }
  else {
    int *package_ids_count = emalloc(sizeof(int) * topo->total_cores);
    for(int i=0; i < topo->total_cores; i++) package_ids_count[i] = 0;
    for(int i=0; i < topo->total_cores; i++) package_ids_count[package_ids[i]]++;
    for(int i=0; i < topo->total_cores; i++) if(package_ids_count[i] != 0) topo->sockets++;
    free(package_ids_count);
  }

  // Count unique (package_id, core_id) pairs
  int unique_pairs = 0;
  int max_pairs = topo->total_cores;
  long long *seen_pairs = emalloc(sizeof(long long) * max_pairs);
  for(int i=0; i < max_pairs; i++) seen_pairs[i] = -1;
  for(int i=0; i < topo->total_cores; i++) {
    long long key = (((long long)package_ids[i]) << 32) | (unsigned long long)(uint32_t)core_ids[i];
    bool found_pair = false;
    for(int j=0; j < unique_pairs && !found_pair; j++) {
      if(seen_pairs[j] == key) found_pair = true;
    }
    if(!found_pair) {
      seen_pairs[unique_pairs++] = key;
    }
  }

  if (topo->sockets <= 0) topo->sockets = 1;
  topo->physical_cores = (unique_pairs > 0) ? (unique_pairs / topo->sockets) : (topo->total_cores > 0 ? topo->total_cores / topo->sockets : 1);
  if (topo->physical_cores <= 0) topo->physical_cores = 1;
  topo->logical_cores  = (topo->total_cores > 0) ? (topo->total_cores / topo->sockets) : topo->physical_cores;
  if (topo->logical_cores <= 0) topo->logical_cores = topo->physical_cores;
  topo->smt_supported = topo->logical_cores / topo->physical_cores;
  if (topo->smt_supported <= 0) topo->smt_supported = 1;

  free(core_ids);
  free(package_ids);
  free(seen_pairs);

  return topo;
}

static char* get_cpu_name_from_cpuinfo(void) {
  // PA-RISC exposes model and cpu lines; prefer model which includes PA8900
  char* model = get_field_from_cpuinfo("model\t\t: ");
  if (model != NULL && strlen(model) > 0) return model;
  char* cpu = get_field_from_cpuinfo("cpu\t\t: ");
  return cpu; // may be NULL
}

struct frequency* get_frequency_info(void) {
  struct frequency* freq = emalloc(sizeof(struct frequency));

  freq->measured = false;
  freq->max = get_max_freq_from_file(0);
  freq->base = get_min_freq_from_file(0);

  if(freq->max == UNKNOWN_DATA) {
    // PA-RISC often lacks cpufreq sysfs; use Cpu0ClkTck
    freq->max = get_frequency_from_cpuinfo();
  }

  return freq;
}

// Roughly infer FLOPs-per-cycle from known PA-RISC models.
// Many PA-8xxx parts support fused multiply-add throughput (≈2 FLOPs/cycle).
// Fall back to 1 FLOP/cycle if unknown.
static int parisc_flops_per_cycle(struct cpuInfo* cpu) {
  if(cpu == NULL || cpu->cpu_name == NULL) return 1;
  const char* name = cpu->cpu_name;

  // Simple heuristic: treat PA-8xxx as having FMA-class throughput
  // (e.g., PA-8700/8800/8900). Keep conservative elsewhere.
  if(strstr(name, "PA8") != NULL || strstr(name, "PA-8") != NULL || strstr(name, "PA 8") != NULL)
    return 2;

  // Specific matches in case the string lacks the generic patterns
  if(strstr(name, "PA8700") != NULL || strstr(name, "PA-8700") != NULL) return 2;
  if(strstr(name, "PA8800") != NULL || strstr(name, "PA-8800") != NULL) return 2;
  if(strstr(name, "PA8900") != NULL || strstr(name, "PA-8900") != NULL) return 2;

  return 1;
}

static int64_t get_peak_performance(struct cpuInfo* cpu, struct topology* topo, int64_t freq) {
  if(freq == UNKNOWN_DATA) return -1;
  // Estimate similar to x86 approach: cores * freq * flops-per-cycle
  int flops_per_cycle = parisc_flops_per_cycle(cpu); // default 1, PA-8xxx → 2
  int64_t flops = topo->physical_cores * topo->sockets * (freq * 1000000) * (int64_t)flops_per_cycle;
  return flops;
}

struct hypervisor* get_hp_info(void) {
  struct hypervisor* hv = emalloc(sizeof(struct hypervisor));
  hv->present = false;
  hv->hv_vendor = HV_VENDOR_INVALID;
  hv->hv_name = hv_vendors_name[hv->hv_vendor];
  return hv;
}

// Accurate peak performance using runtime measurement (scalar FP32)
static int64_t measure_peak_performance_f32(struct topology* topo) {
  // Enable when --accurate-pp is passed, or when CPUFETCH_MEASURE_SP_FLOPS=1
  const char* env = getenv("CPUFETCH_MEASURE_SP_FLOPS");
  bool enabled = accurate_pp() || (env != NULL && env[0] == '1');
  if(!enabled) return -1;

  struct timeval t0, t1;
  // Default run time; can be overridden with CPUFETCH_MEASURE_SP_FLOPS_SECS
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
  int ops_per_iter = 32; // scalar FLOPs per loop body (see 4x blocks below)
  uint64_t iters = 0;
  if(gettimeofday(&t0, NULL) != 0) return -1;
  for(;;) {
    // 1st 8 FLOPs
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    // 2nd 8 FLOPs
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    // 3rd 8 FLOPs
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    // 4th 8 FLOPs → 32 total per iteration
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;

    iters++;
    if((iters & 0x3FF) == 0) {
      if(gettimeofday(&t1, NULL) != 0) break;
      double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_usec - t0.tv_usec) / 1e6;
      if(elapsed >= target_seconds) {
        double flops_per_core = ((double)iters * ops_per_iter) / elapsed;
        double total_flops = flops_per_core * (double)(topo->physical_cores * topo->sockets);
        if(total_flops <= 0.0) return -1;
        return (int64_t) total_flops;
      }
    }
  }
  return -1;
}

// Measure integer packed-like operations throughput (approximate)
static int64_t measure_int_ops_throughput(struct topology* topo) {
  if(!accurate_pp_with_ops()) return -1;
  struct timeval t0, t1;
  volatile uint64_t x = 0x0102030405060708ULL;
  volatile uint64_t y = 0x1122334455667788ULL;
  volatile uint64_t z = 0xFFEEDDCCBBAA9988ULL;
  int ops_per_iter = 24; // bitwise/add/sub/shift ops per iteration
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
        double ops_per_core = ((double)iters * ops_per_iter) / elapsed;
        double total_ops = ops_per_core * (double)(topo->physical_cores * topo->sockets);
        if(total_ops <= 0.0) return -1;
        return (int64_t) total_ops;
      }
    }
  }
  return -1;
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
  printf("Model: %s\n", cpu->cpu_name != NULL ? cpu->cpu_name : "Unknown");
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
  cpu->cpu_vendor = CPU_VENDOR_UNKNOWN;
  cpu->hv = get_hp_info();
  cpu->arch = get_uarch(cpu);
  cpu->cach = get_cache_info(cpu);
  cpu->topo = get_topology_info(cpu->cach);
  cpu->freq = get_frequency_info();
  // If accurate-pp requested, measure; else estimate conservatively
  int64_t measured = measure_peak_performance_f32(cpu->topo);
  if (measured > 0) cpu->peak_performance = measured;
  else cpu->peak_performance = get_peak_performance(cpu, cpu->topo, get_freq(cpu->freq));
  cpu->vis_ops_performance = measure_int_ops_throughput(cpu->topo);

  return cpu;
}

void free_topo_struct(struct topology* topo) {
  free(topo);
}

