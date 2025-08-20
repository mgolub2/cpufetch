#pragma once

#ifdef __APPLE__
#include <stdint.h>

// Measures approximate GPU integer byte-lane operations per second using a Metal compute kernel.
// Returns -1 on failure.
#ifdef __cplusplus
extern "C" {
#endif
int64_t measure_metal_gpu_ops_total(void);
#ifdef __cplusplus
}
#endif
#endif

