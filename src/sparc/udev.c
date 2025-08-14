#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

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

// Prefer OpenPROM/Device Tree for SPARC cache sizes; fall back to sysfs.

static long read_be_cells_from_file(const char* path) {
  // DT properties here are usually 32-bit big-endian cells holding sizes
  // Some firmwares may provide 64-bit, but most SPARC cache sizes fit 32-bit
  uint8_t buf[8];
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  ssize_t n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n < 4) return -1;

  // Use the last 4 bytes read in case of 64-bit cell arrays
  uint32_t v = ((uint32_t)buf[n-4] << 24) | ((uint32_t)buf[n-3] << 16) |
               ((uint32_t)buf[n-2] << 8)  | ((uint32_t)buf[n-1]);
  return (long)v; // bytes
}

static long read_cache_from_dt(const char* prop) {
  const char* bases[] = {
    "/sys/firmware/devicetree/base/cpus/cpu@0/",
    "/proc/device-tree/cpus/cpu@0/",
    "/proc/openprom/cpus/cpu@0/",
    // Some platforms expose cpu node directly at root
    "/sys/firmware/devicetree/base/cpu@0/",
    "/proc/device-tree/cpu@0/",
    "/proc/openprom/cpu@0/"
  };

  char path[256];
  for(size_t i = 0; i < sizeof(bases)/sizeof(bases[0]); i++) {
    memset(path, 0, sizeof(path));
    // Build: base + prop
    size_t blen = strlen(bases[i]);
    size_t plen = strlen(prop);
    if(blen + plen + 1 >= sizeof(path)) continue;
    memcpy(path, bases[i], blen);
    memcpy(path + blen, prop, plen);
    long v = read_be_cells_from_file(path);
    if(v > 0) return v;
  }
  return -1;
}

// SPARC-specific cache size getters. Prefer cpuinfo on SPARC; fall back to sysfs via common helpers.
long get_l1i_cache_size_sparc(uint32_t core) {
  UNUSED(core);
  long v = read_cache_from_dt("i-cache-size");
  if(v > 0) return v;
  return get_l1i_cache_size(0);
}

long get_l1d_cache_size_sparc(uint32_t core) {
  UNUSED(core);
  long v = read_cache_from_dt("d-cache-size");
  if(v > 0) return v;
  return get_l1d_cache_size(0);
}

long get_l2_cache_size_sparc(uint32_t core) {
  UNUSED(core);
  long v = read_cache_from_dt("l2-cache-size");
  if(v > 0) return v;
  return get_l2_cache_size(0);
}

long get_l3_cache_size_sparc(uint32_t core) {
  UNUSED(core);
  long v = read_cache_from_dt("l3-cache-size");
  if(v > 0) return v;
  return get_l3_cache_size(0);
}

int get_num_caches_by_level_sparc(struct cpuInfo* cpu, uint32_t level) {
  // Prefer sysfs shared_cpu_map like other arches
  return get_num_caches_by_level(cpu, level);
}

