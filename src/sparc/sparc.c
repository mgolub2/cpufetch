// Ensure SPARC-specific fields are visible to static analysis
#ifndef ARCH_SPARC
#define ARCH_SPARC 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#include "sparc.h"
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

static char* get_cpucaps_from_cpuinfo(void) {
  char* caps = get_field_from_cpuinfo("cpucaps\t\t: ");
  if (!caps) return NULL;
  // Keep as-is; could map/format in the future
  return caps;
}

struct cache* get_cache_info(struct cpuInfo* cpu) {
  struct cache* cach = emalloc(sizeof(struct cache));
  init_cache_struct(cach);

  cach->L1i->size = get_l1i_cache_size_sparc(0);
  cach->L1d->size = get_l1d_cache_size_sparc(0);
  cach->L2->size = get_l2_cache_size_sparc(0);
  cach->L3->size = get_l3_cache_size_sparc(0);

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

  bool core_ids_ok = fill_core_ids_from_sys(core_ids, topo->total_cores);
  if(!core_ids_ok) {
    printWarn("fill_core_ids_from_sys failed, output may be incomplete/invalid");
    for(int i=0; i < topo->total_cores; i++) core_ids[i] = i; // assume one core per CPU, no SMT
  }

  if(!fill_package_ids_from_sys(package_ids, topo->total_cores)) {
    printWarn("fill_package_ids_from_sys failed, output may be incomplete/invalid");
    for(int i=0; i < topo->total_cores; i++) package_ids[i] = i; // assume each CPU is its own socket
    topo->sockets = topo->total_cores;
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

  // Count unique (package_id, core_id) pairs to determine cores per socket robustly
  int unique_pairs = 0;
  int max_pairs = topo->total_cores;
  long long *seen_pairs = emalloc(sizeof(long long) * max_pairs);
  for(int i=0; i < max_pairs; i++) {
    seen_pairs[i] = -1;
  }
  for(int i=0; i < topo->total_cores; i++) {
    // Pack ids into a 64-bit key: high 32 bits package, low 32 bits core
    long long key = (((long long)package_ids[i]) << 32) | (unsigned long long)(uint32_t)core_ids[i];
    bool found_pair = false;
    for(int j=0; j < unique_pairs && !found_pair; j++) {
      if(seen_pairs[j] == key) found_pair = true;
    }
    if(!found_pair) {
      seen_pairs[unique_pairs++] = key;
    }
  }

  topo->physical_cores = (topo->sockets > 0) ? (unique_pairs / topo->sockets) : 0;
  topo->logical_cores  = (topo->sockets > 0) ? (topo->total_cores / topo->sockets) : 0;
  // UltraSPARC IIIi systems have no SMT (1 thread/core).
  topo->smt_supported = 1;

  free(core_ids);
  free(package_ids);
  free(seen_pairs);

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
  // UltraSparc -> UltraSPARC (keep vendor prefix like "TI ")
  char* pos = strstr(model, "UltraSparc");
  if (pos != NULL) {
    memcpy(pos, "UltraSPARC", strlen("UltraSPARC"));
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

// Try to estimate single-precision FLOPs using a short FP loop on one core,
// then scale by total cores. Enabled only if CPUFETCH_MEASURE_SP_FLOPS=1.
static int64_t measure_peak_performance_f32(struct topology* topo) {
  const char* env = getenv("CPUFETCH_MEASURE_SP_FLOPS");
  bool enabled = accurate_pp() || (env != NULL && env[0] == '1');
  if(!enabled) return -1;

  struct timeval t0, t1;
  // Short runtime to avoid long blocking; increase via env if needed
  double target_seconds = 0.6;
  if(env != NULL && env[0] == '1') {
    const char* dur = getenv("CPUFETCH_MEASURE_SP_FLOPS_SECS");
    if(dur != NULL) {
      double v = atof(dur);
      if(v > 0.05 && v < 30.0) target_seconds = v;
    }
  }
  volatile float a = 1.0f, b = 1.0001f, c = 0.9997f, d = 1.0003f;
  volatile float e = 0.5f, f = 1.5f, g = 2.0f, h = -0.25f;
  uint64_t iters = 0;
  int ops_per_iter = 32; // scalar FLOPs per loop body

  if(gettimeofday(&t0, NULL) != 0) return -1;
  for(;;) {
#if defined(__sparc__)
    // Inline SPARC v9 single-precision math to encourage FPU throughput
    register float ra = a, rb = b, rc = c, rd = d, re = e, rf = f, rg = g, rh = h;
    __asm__ __volatile__(
      "fadds %0, %1, %0\n\t"
      "fmuls %1, %2, %1\n\t"
      "fadds %2, %3, %2\n\t"
      "fmuls %3, %0, %3\n\t"
      "fadds %4, %5, %4\n\t"
      "fmuls %5, %6, %5\n\t"
      "fadds %6, %7, %6\n\t"
      "fmuls %7, %4, %7\n\t"
      "fadds %0, %1, %0\n\t"
      "fmuls %1, %2, %1\n\t"
      "fadds %2, %3, %2\n\t"
      "fmuls %3, %0, %3\n\t"
      "fadds %4, %5, %4\n\t"
      "fmuls %5, %6, %5\n\t"
      "fadds %6, %7, %6\n\t"
      "fmuls %7, %4, %7\n\t"
      : "+f"(ra), "+f"(rb), "+f"(rc), "+f"(rd), "+f"(re), "+f"(rf), "+f"(rg), "+f"(rh)
      :
      : "memory");
    a = ra; b = rb; c = rc; d = rd; e = re; f = rf; g = rg; h = rh;
    // 32 FLOPs in the asm block
    ops_per_iter = 32;
#else
    // Unrolled FP ops to keep FPU busy (portable C fallback)
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
    a = a + b; b = b * c; c = c + d; d = d * a;
    e = e + f; f = f * g; g = g + h; h = h * e;
#endif
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

int64_t get_peak_performance(struct cpuInfo* cpu, struct topology* topo, int64_t freq) {
  // Optional measured SP FLOPS estimate
  int64_t measured = measure_peak_performance_f32(topo);
  if(measured > 0) return measured;

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

char* get_str_features(struct cpuInfo* cpu) {
  UNUSED(cpu);
  return get_cpucaps_from_cpuinfo();
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

