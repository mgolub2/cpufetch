#include <errno.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

#define _PATH_TOPO_CORE_ID         "topology/core_id"
#define _PATH_TOPO_PACKAGE_ID      "topology/physical_package_id"
#define CPUINFO_FREQUENCY_STR_HEX  "Cpu0ClkTck\t: "
// No cache sizes in cpuinfo on many SPARC Linux systems. Do not guess.

static bool fill_array_from_sys(int *ids, int total_cores, char* SYS_PATH) {
  int filelen;
  char* buf;
  char* end;
  char path[128];
  memset(path, 0, sizeof(char) * 128);

  for(int i=0; i < total_cores; i++) {
    sprintf(path, "%s%s/cpu%d/%s", _PATH_SYS_SYSTEM, _PATH_SYS_CPU, i, SYS_PATH);
    if((buf = read_file(path, &filelen)) == NULL) {
      printWarn("fill_array_from_sys: %s: %s", path, strerror(errno));
      return false;
    }

    errno = 0;
    ids[i] = strtol(buf, &end, 10);
    if(errno != 0) {
      printWarn("fill_array_from_sys: %s:", strerror(errno));
      return false;
    }
    free(buf);
  }

  return true;
}

bool fill_core_ids_from_sys(int *core_ids, int total_cores) {
  return fill_array_from_sys(core_ids, total_cores, _PATH_TOPO_CORE_ID);
}

bool fill_package_ids_from_sys(int* package_ids, int total_cores) {
  bool status = fill_array_from_sys(package_ids, total_cores, _PATH_TOPO_PACKAGE_ID);
  if(status) {
    for(int i=0; i < total_cores; i++) {
      if(package_ids[i] == -1) {
        printWarn("fill_package_ids_from_sys: package_ids[%d] = -1", i);
        return false;
      }
      else if(package_ids[i] >= total_cores || package_ids[i] < 0) {
        printBug("fill_package_ids_from_sys: package_ids[%d] = %d", i, package_ids[i]);
        return false;
      }
    }

    return true;
  }
  return false;
}

long get_frequency_from_cpuinfo(void) {
  // Parse Cpu0ClkTck hex value and convert to MHz
  char* clk_str = get_field_from_cpuinfo(CPUINFO_FREQUENCY_STR_HEX);
  if(clk_str == NULL) {
    return UNKNOWN_DATA;
  }

  // Expect a 16-hex-digit value
  char* end;
  errno = 0;
  unsigned long long hz = strtoull(clk_str, &end, 16);
  free(clk_str);
  if(errno != 0) {
    printWarn("strtoull: %s", strerror(errno));
    return UNKNOWN_DATA;
  }
  if (hz == 0ULL) return UNKNOWN_DATA;

  long mhz = (long)(hz / 1000000ULL);
  if(mhz > 10000 || mhz < 100) return UNKNOWN_DATA;
  return mhz;
}

// Read cache sizes directly from new-style sysfs CPU attributes
static long read_long_from_path(const char* path) {
  int filelen;
  char* buf;
  if((buf = read_file((char*)path, &filelen)) == NULL) {
    return -1;
  }
  buf[filelen] = '\0';
  errno = 0;
  char* end = NULL;
  long v = strtol(buf, &end, 10);
  free(buf);
  if(errno != 0) return -1;
  return v;
}

static long read_cpu_attr_bytes(uint32_t core, const char* attr) {
  char path[_PATH_CACHE_MAX_LEN];
  // e.g. /sys/devices/system/cpu/cpu0/l1_icache_size
  snprintf(path, sizeof(path), "%s%s/cpu%d/%s", _PATH_SYS_SYSTEM, _PATH_SYS_CPU, core, attr);
  return read_long_from_path(path);
}

// SPARC-specific cache size getters. Prefer cpuinfo on SPARC; fall back to sysfs via common helpers.
long get_l1i_cache_size_sparc(uint32_t core) {
  long v = read_cpu_attr_bytes(core, "l1_icache_size");
  if(v > 0) return v;
  return get_l1i_cache_size(core);
}

long get_l1d_cache_size_sparc(uint32_t core) {
  long v = read_cpu_attr_bytes(core, "l1_dcache_size");
  if(v > 0) return v;
  return get_l1d_cache_size(core);
}

long get_l2_cache_size_sparc(uint32_t core) {
  long v = read_cpu_attr_bytes(core, "l2_cache_size");
  if(v > 0) return v;
  return get_l2_cache_size(core);
}

long get_l3_cache_size_sparc(uint32_t core) {
  long v = read_cpu_attr_bytes(core, "l3_cache_size");
  if(v > 0) return v;
  return get_l3_cache_size(core);
}

int get_num_caches_by_level_sparc(struct cpuInfo* cpu, uint32_t level) {
  // Prefer sysfs shared_cpu_map like other arches
  return get_num_caches_by_level(cpu, level);
}

