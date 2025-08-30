#include <errno.h>
#include <string.h>

#include "../common/udev.h"
#include "../common/global.h"
#include "udev.h"

#define _PATH_TOPO_CORE_ID         "topology/core_id"
#define _PATH_TOPO_PACKAGE_ID      "topology/physical_package_id"
#define CPUINFO_FREQ_MHZ_STR       "cpu MHz\t\t: "
#define CPUINFO_MODEL_STR          "model\t\t: "
#define CPUINFO_CPU_STR            "cpu\t\t: "
// Cache fields printed by show_cache_info() on parisc
#define CPUINFO_ICACHE_STR         "I-cache\t\t: "
#define CPUINFO_DCACHE_STR         "D-cache\t\t: "
#define CPUINFO_L2CACHE_STR        "ITLB entries\t: " /* placeholder to keep alignment; L2 parsed differently if present */

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

// Prefer sysfs for frequency; fall back to Cpu0ClkTck in /proc/cpuinfo
long get_frequency_from_cpuinfo(void) {
  // PA-RISC exposes "cpu MHz\t\t: <float>" in /proc/cpuinfo
  char* mhz_str = get_field_from_cpuinfo(CPUINFO_FREQ_MHZ_STR);
  if(mhz_str == NULL) {
    return UNKNOWN_DATA;
  }

  char* end;
  errno = 0;
  double mhz_d = strtod(mhz_str, &end);
  free(mhz_str);
  if(errno != 0) {
    printWarn("strtod: %s", strerror(errno));
    return UNKNOWN_DATA;
  }
  if (mhz_d <= 0.0) return UNKNOWN_DATA;
  long mhz = (long)(mhz_d + 0.5);
  if(mhz > 10000 || mhz < 100) return UNKNOWN_DATA;
  return mhz;
}

// Helper: parse a size like "64 KB" or "512 KB, 4-way, 32 byte line" and return bytes
static long parse_size_kb_field(const char* s) {
  if(s == NULL) return -1;
  // Accept first integer token and unit; default KB
  long value = -1;
  char unit[8];
  unit[0] = '\0';
  errno = 0;
  // Copy to temp to avoid modifying input
  char buf[128];
  memset(buf, 0, sizeof(buf));
  strncpy(buf, s, sizeof(buf)-1);
  char* p = buf;
  // Skip leading spaces
  while(*p == ' ') p++;
  // Read number
  char* endptr = NULL;
  long num = strtol(p, &endptr, 10);
  if(errno != 0 || endptr == p) return -1;
  value = num;
  // Read optional unit
  while(*endptr == ' ') endptr++;
  // Extract up to whitespace or comma
  int i = 0;
  while(endptr[i] && endptr[i] != ' ' && endptr[i] != '\t' && endptr[i] != ',') {
    if(i < (int)sizeof(unit)-1) unit[i] = endptr[i];
    i++;
  }
  unit[i < (int)sizeof(unit) ? i : (int)sizeof(unit)-1] = '\0';
  // Normalize unit
  for(int j=0; unit[j]; j++) {
    if(unit[j] >= 'A' && unit[j] <= 'Z') unit[j] = (char)(unit[j] - 'A' + 'a');
  }
  if(unit[0] == '\0' || strcmp(unit, "kb") == 0) {
    return value > 0 ? value * 1024 : -1;
  }
  if(strcmp(unit, "mb") == 0) {
    return value > 0 ? value * 1024 * 1024 : -1;
  }
  if(strcmp(unit, "b") == 0 || strcmp(unit, "bytes") == 0) {
    return value > 0 ? value : -1;
  }
  // Unknown unit; assume KB
  return value > 0 ? value * 1024 : -1;
}

static long get_cache_size_from_cpuinfo_key(const char* key) {
  char* line = get_field_from_cpuinfo((char*)key);
  if(line == NULL) return -1;
  long bytes = parse_size_kb_field(line);
  free(line);
  return bytes;
}

// Try PARISC-specific fields first; fallback to generic sysfs
long get_l1i_cache_size_parisc(uint32_t core) {
  long v = get_cache_size_from_cpuinfo_key(CPUINFO_ICACHE_STR);
  if(v > 0) return v;
  return get_l1i_cache_size(core);
}

long get_l1d_cache_size_parisc(uint32_t core) {
  long v = get_cache_size_from_cpuinfo_key(CPUINFO_DCACHE_STR);
  if(v > 0) return v;
  return get_l1d_cache_size(core);
}

long get_l2_cache_size_parisc(uint32_t core) {
  // PA-RISC /proc/cpuinfo often lacks explicit L2 line; rely on sysfs if present
  long v = get_l2_cache_size(core);
  return v;
}

long get_l3_cache_size_parisc(uint32_t core) {
  long v = get_l3_cache_size(core);
  return v;
}

