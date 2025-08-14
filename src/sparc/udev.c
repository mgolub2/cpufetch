#include <errno.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

#define _PATH_TOPO_CORE_ID         "topology/core_id"
#define _PATH_TOPO_PACKAGE_ID      "topology/physical_package_id"
#define CPUINFO_FREQUENCY_STR_HEX  "Cpu0ClkTck\t: "
// Common cpuinfo cache fields (Linux SPARC)
#define CPUINFO_L1I_STR            "I$\t\t\t: "
#define CPUINFO_L1D_STR            "D$\t\t\t: "
#define CPUINFO_L2_STR             "L2$\t\t\t: "
#define CPUINFO_L3_STR             "L3$\t\t\t: "

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

static long parse_cache_kb_from_cpuinfo(char* field) {
  char* line = get_field_from_cpuinfo(field);
  if(line == NULL) return -1;
  // Expect values like: "64K" or "1024K" possibly with text
  // Trim at first space
  for(int i=0; line[i]; i++) { if(line[i] == ' ' || line[i] == '\t') { line[i] = '\0'; break; } }
  char* end;
  errno = 0;
  long val = strtol(line, &end, 10);
  free(line);
  if(errno != 0) return -1;
  return val * 1024; // convert KB to bytes
}

// SPARC-specific cache size getters. Prefer cpuinfo on SPARC; fall back to sysfs via common helpers.
long get_l1i_cache_size_sparc(uint32_t core) {
  long v = parse_cache_kb_from_cpuinfo(CPUINFO_L1I_STR);
  if(v > 0) return v;
  return get_l1i_cache_size(core);
}

long get_l1d_cache_size_sparc(uint32_t core) {
  long v = parse_cache_kb_from_cpuinfo(CPUINFO_L1D_STR);
  if(v > 0) return v;
  return get_l1d_cache_size(core);
}

long get_l2_cache_size_sparc(uint32_t core) {
  long v = parse_cache_kb_from_cpuinfo(CPUINFO_L2_STR);
  if(v > 0) return v;
  return get_l2_cache_size(core);
}

long get_l3_cache_size_sparc(uint32_t core) {
  long v = parse_cache_kb_from_cpuinfo(CPUINFO_L3_STR);
  if(v > 0) return v;
  return get_l3_cache_size(core);
}

