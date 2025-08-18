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

