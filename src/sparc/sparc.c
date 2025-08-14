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
    // L1I is per core on UltraSPARC-IIIi; use online cores as a robust default
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    cach->L1i->num_caches = (online > 0 && online < 256) ? (uint8_t)online : 1;
    cach->max_cache_level = 1;
  }
  if(cach->L1d->size > 0) {
    cach->L1d->exists = true;
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    cach->L1d->num_caches = (online > 0 && online < 256) ? (uint8_t)online : 1;
    cach->max_cache_level = 2;
  }
  if(cach->L2->size > 0) {
    cach->L2->exists = true;
    // L2 is private per core on USIIIi; fall back to online cores
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    cach->L2->num_caches = (online > 0 && online < 256) ? (uint8_t)online : 1;
    cach->max_cache_level = 3;
  }
  if(cach->L3->size > 0) {
    cach->L3->exists = true;
    // Most UltraSPARC-IIIi systems have no L3; if present assume one per socket
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    cach->L3->num_caches = (online > 1 && online < 256) ? 2 : 1;
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

// Lightweight parser for cpucaps to detect VIS/VIS2 presence
static bool sparc_has_vis_level(int level) {
  char* caps = get_cpucaps_from_cpuinfo();
  if(caps == NULL) return false;
  bool has = false;
  if(level >= 2) {
    if(strstr(caps, "vis2") != NULL || strstr(caps, "VIS2") != NULL)
      has = true;
  }
  if(!has) {
    if(strstr(caps, "vis") != NULL || strstr(caps, "VIS") != NULL)
      has = true;
  }
  free(caps);
  return has;
}

// Prefer measuring VIS/VIS2 packed arithmetic throughput when enabled.
// Falls back to scalar FP32 loop if VIS is unavailable.
// Enabled only if accurate-pp was requested or CPUFETCH_MEASURE_SP_FLOPS=1.
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
  uint64_t iters = 0;

#if 0
  // If VIS is available, use packed arithmetic to stress VIS pipelines
  if(sparc_has_vis_level(1)) {
    // Types from GCC VIS built-ins documentation
    typedef int v2si __attribute__ ((vector_size (8)));
    typedef short v4hi __attribute__ ((vector_size (8)));
    typedef unsigned char v8qi __attribute__ ((vector_size (8)));
    typedef unsigned char v4qi __attribute__ ((vector_size (4)));

    volatile v4hi a16 = (v4hi){1,2,3,4};
    volatile v4hi b16 = (v4hi){5,6,7,8};
    volatile v2si a32 = (v2si){11,12};
    volatile v2si b32 = (v2si){13,14};
    volatile v4qi qa4 = (v4qi){1,2,3,4};
    volatile v4qi qb4 = (v4qi){8,7,6,5};
    volatile v8qi acc8 = (v8qi){0,0,0,0,0,0,0,0};

    int ops_per_iter = 0;
    if(gettimeofday(&t0, NULL) != 0) return -1;
    for(;;) {
      // VIS 1.0 packed add/sub (each op works on lanes)
      v4hi t0_16 = __builtin_vis_fpadd16(a16, b16);   // 4 ops
      v4hi t1_16 = __builtin_vis_fpsub16(b16, a16);   // 4 ops
      v2si t0_32 = __builtin_vis_fpadd32(a32, b32);   // 2 ops
      v2si t1_32 = __builtin_vis_fpsub32(b32, a32);   // 2 ops
      v8qi t2_8  = __builtin_vis_fpack32(t0_32, acc8); // 8 ops
      v8qi mrg8  = __builtin_vis_fpmerge(qa4, qb4);   // 8 ops
      v4qi pk16  = __builtin_vis_fpack16(t0_16);      // 4 ops
      // Mix results to reduce dependencies and keep live
      a16 = __builtin_vis_fpadd16(t0_16, t1_16);      // 4 ops
      b16 = __builtin_vis_fpsub16(t1_16, t0_16);      // 4 ops
      a32 = __builtin_vis_fpadd32(t0_32, t1_32);      // 2 ops
      b32 = __builtin_vis_fpsub32(t1_32, t0_32);      // 2 ops
      acc8 = t2_8;
      qa4 = pk16;                                     // keep pk16 live
      qb4 = (v4qi){(unsigned char)mrg8[0], (unsigned char)mrg8[2], (unsigned char)mrg8[4], (unsigned char)mrg8[6]};

      // Count per-iteration lane-ops conservatively once
      ops_per_iter = 4+4 + 2+2 + 8 + 8 + 4 + 4+4 + 2+2; // = 42

  /*
       Note: Avoid using VIS2-only builtins (e.g., bshuffle/bmask) that
       require assembling for v9b (e.g., -mcpu=ultrasparc3). Some toolchains
       crash at runtime when forcing that target. Keep the loop VIS1-only.
  */

      iters++;
      if((iters & 0x3FF) == 0) {
        if(gettimeofday(&t1, NULL) != 0) break;
        double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_usec - t0.tv_usec) / 1e6;
        if(elapsed >= target_seconds) {
          double ops_per_core = ((double)iters * ops_per_iter) / elapsed;
          double total_ops = ops_per_core * (double)(topo->physical_cores * topo->sockets);
          if(total_ops <= 0.0) return -1;
          return (int64_t) total_ops;
        }
      }
    }
  }
#endif

  // Fallback: scalar FP32 loop if VIS is not present
  volatile float a = 1.0f, b = 1.0001f, c = 0.9997f, d = 1.0003f;
  volatile float e = 0.5f, f = 1.5f, g = 2.0f, h = -0.25f;
  int ops_per_iter = 32; // scalar FLOPs per loop body

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
  // Prefer VIS/VIS2 packed throughput if measurement is enabled
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

