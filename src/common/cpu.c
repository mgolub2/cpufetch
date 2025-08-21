#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "../common/global.h"
#include "cpu.h"

#ifdef ARCH_X86
  #include "../x86/uarch.h"
  #include "../x86/apic.h"
#elif ARCH_PPC
  #include "../ppc/uarch.h"
#elif ARCH_ARM
  #include "../arm/uarch.h"
#elif ARCH_RISCV
  #include "../riscv/uarch.h"
#elif ARCH_SPARC
  #include "../sparc/uarch.h"
#elif ARCH_PARISC
  #include "../parisc/uarch.h"
#endif

#define STRING_YES        "Yes"
#define STRING_NO         "No"
#define STRING_NONE       "None"
#define STRING_MEGAHERZ   "MHz"
#define STRING_GIGAHERZ   "GHz"
#define STRING_KILOBYTES  "KB"
#define STRING_MEGABYTES  "MB"

VENDOR get_cpu_vendor(struct cpuInfo* cpu) {
  return cpu->cpu_vendor;
}

int64_t get_freq(struct frequency* freq) {
  return freq->max;
}

#ifdef ARCH_X86
int64_t get_freq_pp(struct frequency* freq) {
  return freq->max_pp;
}
#endif

#if defined(ARCH_X86) || defined(ARCH_PPC) || defined(ARCH_SPARC) || defined(ARCH_PARISC)
char* get_str_cpu_name(struct cpuInfo* cpu, bool fcpuname) {
  #ifdef ARCH_X86
  if(!fcpuname) {
    return get_str_cpu_name_abbreviated(cpu);
  }
  #elif ARCH_PPC
  UNUSED(fcpuname);
  #endif
  return cpu->cpu_name;
}

char* get_str_sockets(struct topology* topo) {
  // support multi-digit sockets count
  char* string = emalloc(sizeof(char) * 6);
  int32_t sanity_ret = snprintf(string, 6, "%d", topo->sockets);
  if(sanity_ret < 0) {
    printBug("get_str_sockets: snprintf returned a negative value for input: '%d'", topo->sockets);
    return NULL;
  }
  return string;
}

uint32_t get_nsockets(struct topology* topo) {
  return topo->sockets;
}
#endif

int32_t get_value_as_smallest_unit(char ** str, uint32_t value) {
  int32_t ret;
  int max_len = 10; // Max is 8 for digits, 2 for units
  *str = emalloc(sizeof(char)* (max_len + 1));

  if(value/1024 >= 1024)
    ret = snprintf(*str, max_len, "%.4g"STRING_MEGABYTES, (double)value/(1<<20));
  else
    ret = snprintf(*str, max_len, "%.4g"STRING_KILOBYTES, (double)value/(1<<10));

  return ret;
}

// String functions
char* get_str_cache_two(int32_t cache_size, uint32_t physical_cores) {
  char* tmp1;
  char* tmp2;
  int32_t tmp1_len = get_value_as_smallest_unit(&tmp1, cache_size);
  int32_t tmp2_len = get_value_as_smallest_unit(&tmp2, cache_size * physical_cores);

  // tmp1_len for first output, 2 for ' (', tmp2_len for second output and 7 for ' Total)'
  uint32_t size = tmp1_len + 2 + tmp2_len + 7 + 1;
  char* string = emalloc(sizeof(char) * size);

  if(tmp1_len < 0) {
    printBug("get_value_as_smallest_unit: snprintf failed for input: %d\n", cache_size);
    return NULL;
  }
  if(tmp2_len < 0) {
    printBug("get_value_as_smallest_unit: snprintf failed for input: %d\n", cache_size * physical_cores);
    return NULL;
  }

  if(snprintf(string, size, "%s (%s Total)", tmp1, tmp2) < 0) {
    printBug("get_str_cache_two: snprintf failed for input: '%s' and '%s'\n", tmp1, tmp2);
    return NULL;
  }

  free(tmp1);
  free(tmp2);

  return string;
}

char* get_str_cache_one(int32_t cache_size) {
  char* string;
  int32_t str_len = get_value_as_smallest_unit(&string, cache_size);

  if(str_len < 0) {
    printBug("get_value_as_smallest_unit: snprintf failed for input: %d", cache_size);
    return NULL;
  }

  return string;
}

char* get_str_cache(int32_t cache_size, int32_t num_caches) {
  if(num_caches > 1)
    return get_str_cache_two(cache_size, num_caches);
  else
    return get_str_cache_one(cache_size);
}

char* get_str_l1i(struct cache* cach) {
  return get_str_cache(cach->L1i->size, cach->L1i->num_caches);
}

char* get_str_l1d(struct cache* cach) {
  return get_str_cache(cach->L1d->size, cach->L1d->num_caches);
}

char* get_str_l2(struct cache* cach) {
  if(!cach->L2->exists)
    return NULL;
  return get_str_cache(cach->L2->size, cach->L2->num_caches);
}

char* get_str_l3(struct cache* cach) {
  if(!cach->L3->exists)
    return NULL;
  return get_str_cache(cach->L3->size, cach->L3->num_caches);
}

char* get_str_freq(struct frequency* freq) {
  //Max 3 digits and 3 for '(M/G)Hz' plus 1 for '\0'
  uint32_t size = (1+5+1+3+1);
  assert(strlen(STRING_UNKNOWN)+1 <= size);
  char* string = ecalloc(size, sizeof(char));

  if(freq->max == UNKNOWN_DATA || freq->max < 0) {
    snprintf(string,strlen(STRING_UNKNOWN)+1,STRING_UNKNOWN);
  }
  else if(freq->max >= 1000) {
    if (freq->measured)
      snprintf(string,size,"~%.3f "STRING_GIGAHERZ,(float)(freq->max)/1000);
    else
      snprintf(string,size,"%.3f "STRING_GIGAHERZ,(float)(freq->max)/1000);
  }
  else {
    if (freq->measured)
      snprintf(string,size,"~%d "STRING_MEGAHERZ,freq->max);
    else
      snprintf(string,size,"%d "STRING_MEGAHERZ,freq->max);
  }

  return string;
}

// SI unit constants for FLOPS
#define FLOPS_MEGA  1000000LL
#define FLOPS_GIGA  (FLOPS_MEGA * 1000LL)
#define FLOPS_TERA  (FLOPS_GIGA * 1000LL)

char* get_str_peak_performance(int64_t flops) {
  if(flops == -1) {
    size_t len = strlen(STRING_UNKNOWN) + 1;
    char* str = emalloc(sizeof(char) * len);
    strcpy(str, STRING_UNKNOWN);
    return str;
  }

  // Buffer size for "XXXX.XX XFLOP/s" format (max 16 chars + null terminator)
  const size_t max_size = 17;
  char* str = ecalloc(max_size, sizeof(char));

  // Use integer comparisons to avoid unnecessary floating point conversion
  if(flops >= FLOPS_TERA)
    snprintf(str, max_size, "%.2f TFLOP/s", (double)flops / FLOPS_TERA);
  else if(flops >= FLOPS_GIGA)
    snprintf(str, max_size, "%.2f GFLOP/s", (double)flops / FLOPS_GIGA);
  else
    snprintf(str, max_size, "%.2f MFLOP/s", (double)flops / FLOPS_MEGA);

  return str;
}

// SI unit constants for operations per second
#define OPS_KILO  1000LL
#define OPS_MEGA  (OPS_KILO * 1000LL)
#define OPS_GIGA  (OPS_MEGA * 1000LL)
#define OPS_TERA  (OPS_GIGA * 1000LL)

char* get_str_ops(int64_t ops) {
  if(ops == -1) {
    size_t len = strlen(STRING_UNKNOWN) + 1;
    char* str = emalloc(sizeof(char) * len);
    strcpy(str, STRING_UNKNOWN);
    return str;
  }

  // Buffer size for worst-case integer path "-9223372036854775808 OPS" (26 chars) + null
  // Use a safe static buffer size to silence -Wformat-truncation on some compilers
  const size_t max_size = 32;
  char* str = ecalloc(max_size, sizeof(char));
  
  // Use integer comparisons to avoid unnecessary floating point conversion
  if(ops >= OPS_TERA)
    snprintf(str, max_size, "%.2f TOPS", (double)ops / OPS_TERA);
  else if(ops >= OPS_GIGA)
    snprintf(str, max_size, "%.2f GOPS", (double)ops / OPS_GIGA);
  else if(ops >= OPS_MEGA)
    snprintf(str, max_size, "%.2f MOPS", (double)ops / OPS_MEGA);
  else if(ops >= OPS_KILO)
    snprintf(str, max_size, "%.2f KOPS", (double)ops / OPS_KILO);
  else
    snprintf(str, max_size, "%lld OPS", (long long)ops);

  return str;
}

void init_topology_struct(struct topology* topo, struct cache* cach) {
  topo->total_cores = 0;
  topo->cach = cach;
#if defined(ARCH_X86) || defined(ARCH_PPC) || defined(ARCH_SPARC) || defined(ARCH_PARISC)
  topo->physical_cores = 0;
  topo->logical_cores = 0;
  topo->smt_supported = 0;
  topo->sockets = 0;
#ifdef ARCH_X86
  topo->smt_available = 0;
  topo->apic = ecalloc(1, sizeof(struct apic));
#endif
#endif
}

void init_cache_struct(struct cache* cach) {
  cach->L1i = emalloc(sizeof(struct cach));
  cach->L1d = emalloc(sizeof(struct cach));
  cach->L2 = emalloc(sizeof(struct cach));
  cach->L3 = emalloc(sizeof(struct cach));

  cach->cach_arr = emalloc(sizeof(struct cach*) * 4);
  cach->cach_arr[0] = cach->L1i;
  cach->cach_arr[1] = cach->L1d;
  cach->cach_arr[2] = cach->L2;
  cach->cach_arr[3] = cach->L3;

  cach->max_cache_level = 0;
  cach->L1i->exists = false;
  cach->L1d->exists = false;
  cach->L2->exists = false;
  cach->L3->exists = false;
}

void free_cache_struct(struct cache* cach) {
  for(int i=0; i < 4; i++) free(cach->cach_arr[i]);
  free(cach->cach_arr);
  free(cach);
}

void free_freq_struct(struct frequency* freq) {
  free(freq);
}

void free_hv_struct(struct hypervisor* hv) {
  free(hv);
}

void free_cpuinfo_struct(struct cpuInfo* cpu) {
  free_uarch_struct(cpu->arch);
  free_hv_struct(cpu->hv);
  #ifdef ARCH_X86
  free(cpu->cpu_name);
  #endif
  free(cpu);
}
