#include <errno.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

#define _PATH_TOPO_CORE_ID         "topology/core_id"
#define _PATH_TOPO_PACKAGE_ID      "topology/physical_package_id"
#define CPUINFO_FREQUENCY_STR_HEX  "Cpu0ClkTck\t: "
// Common cpuinfo cache fields (Linux SPARC)
#define CPUINFO_L1I_KEY            "I$"
#define CPUINFO_L1D_KEY            "D$"
#define CPUINFO_L2_KEY             "L2$"
#define CPUINFO_L3_KEY             "L3$"

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

static long parse_cache_bytes_from_cpuinfo_any(const char* key) {
  int filelen;
  char* buf = read_file(_PATH_CPUINFO, &filelen);
  if(buf == NULL) return -1;

  // Find line containing the key at the beginning of a field
  char* p = strstr(buf, key);
  if(p == NULL) { free(buf); return -1; }
  // Move to colon after the key
  char* nl = strchr(p, '\n');
  char* colon = strchr(p, ':');
  if(colon == NULL || (nl != NULL && colon > nl)) { free(buf); return -1; }
  p = colon + 1;
  // Skip whitespace
  while(*p == ' ' || *p == '\t') p++;
  // Parse integer
  errno = 0;
  long val = strtol(p, NULL, 10);
  if(errno != 0 || val <= 0) { free(buf); return -1; }
  // Find unit char near after the number
  // Scan up to end of line for K/M
  long multiplier = 1024; // default to KB
  for(; *p && *p != '\n'; p++) {
    if(*p == 'M' || *p == 'm') { multiplier = 1024L * 1024L; break; }
    if(*p == 'K' || *p == 'k') { multiplier = 1024L; break; }
  }
  free(buf);
  return val * multiplier;
}

// SPARC-specific cache size getters. Prefer cpuinfo on SPARC; fall back to sysfs via common helpers.
long get_l1i_cache_size_sparc(uint32_t core) {
  long v = parse_cache_bytes_from_cpuinfo_any(CPUINFO_L1I_KEY);
  if(v > 0) return v;
  return get_l1i_cache_size(core);
}

long get_l1d_cache_size_sparc(uint32_t core) {
  long v = parse_cache_bytes_from_cpuinfo_any(CPUINFO_L1D_KEY);
  if(v > 0) return v;
  return get_l1d_cache_size(core);
}

long get_l2_cache_size_sparc(uint32_t core) {
  long v = parse_cache_bytes_from_cpuinfo_any(CPUINFO_L2_KEY);
  if(v > 0) return v;
  return get_l2_cache_size(core);
}

long get_l3_cache_size_sparc(uint32_t core) {
  long v = parse_cache_bytes_from_cpuinfo_any(CPUINFO_L3_KEY);
  if(v > 0) return v;
  return get_l3_cache_size(core);
}

