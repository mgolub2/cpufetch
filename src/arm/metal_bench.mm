#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include "metal_bench.h"

static NSString* const kMetalSource = @"\
#include <metal_stdlib>\n\
using namespace metal;\n\
kernel void bench_ops(device uint* sink [[buffer(0)]],\n\
                      uint3 gid [[thread_position_in_grid]]) {\n\
  const uint idx = gid.x;\n\
  uint acc = idx * 1664525u + 1013904223u;\n\
  #pragma unroll 16\n\
  for (uint i=0;i<4096;i++){\n\
    acc ^= (acc << 1);\n\
    acc += 0x9E3779B9u;\n\
    acc ^= (acc >> 3);\n\
    acc *= 2654435761u;\n\
  }\n\
  // Prevent the compiler from removing the computation; unique write per thread\n\
  sink[idx] = acc;\n\
}\n";

static int64_t now_us() {
  struct timeval tv; if (gettimeofday(&tv, NULL) != 0) return -1;
  return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

int64_t measure_metal_gpu_ops_total(void) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { fprintf(stderr, "[cpufetch] Metal GPU device not available\n"); return -1; }

    NSError* error = nil;
    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    id<MTLLibrary> lib = [device newLibraryWithSource:kMetalSource options:opts error:&error];
    if (!lib) { fprintf(stderr, "[cpufetch] Metal library compile failed: %s\n", error.localizedDescription.UTF8String); return -1; }
    id<MTLFunction> fn = [lib newFunctionWithName:@"bench_ops"]; if (!fn) { fprintf(stderr, "[cpufetch] Metal function not found\n"); return -1; }
    id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:fn error:&error];
    if (!pso) { fprintf(stderr, "[cpufetch] Metal pipeline creation failed: %s\n", error.localizedDescription.UTF8String); return -1; }

    id<MTLCommandQueue> q = [device newCommandQueue]; if (!q) { fprintf(stderr, "[cpufetch] Metal command queue creation failed\n"); return -1; }

    // Choose threadgroup and grid based on pipeline properties
    uint32_t tew = (uint32_t)pso.threadExecutionWidth;
    uint32_t maxTPT = (uint32_t)pso.maxTotalThreadsPerThreadgroup;
    uint32_t tpt = tew * 4; if (tpt > maxTPT) tpt = maxTPT; if (tpt == 0) tpt = tew;
    const uint32_t threadgroups = 16384; // increase workload
    MTLSize tgSize = MTLSizeMake(tpt, 1, 1);
    MTLSize gridSize = MTLSizeMake((NSUInteger)tpt * threadgroups, 1, 1);

    id<MTLBuffer> buf = [device newBufferWithLength:gridSize.width * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!buf) { fprintf(stderr, "[cpufetch] Metal buffer creation failed\n"); return -1; }

    const double target_seconds = 0.6;
    uint64_t iters = 0;
    const int ops_per_thread_iteration = 4096 /* loop iters */ * 4 /* ops per inner body */; // 16384 ops/thread
    const int64_t t0 = now_us(); if (t0 < 0) return -1;
    int64_t last_us = t0;

    while (1) {
      id<MTLCommandBuffer> cb = [q commandBuffer];
      id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:pso];
      [enc setBuffer:buf offset:0 atIndex:0];
      [enc dispatchThreads:gridSize threadsPerThreadgroup:tgSize];
      [enc endEncoding];
      [cb commit];
      [cb waitUntilCompleted];
      iters++;

      if ((iters & 0xF) == 0) {
        const int64_t now = now_us(); if (now < 0) break;
        const double elapsed = (now - t0) / 1e6;
        if (elapsed >= target_seconds) {
          // Total ops = iterations * threads * ops_per_thread_iteration / elapsed
          const double threads = (double)gridSize.width;
          const double total_ops = ((double)iters) * threads * (double)ops_per_thread_iteration / elapsed;
          if (total_ops <= 0.0) return -1;
          return (int64_t)total_ops;
        }
        last_us = now;
      }
    }
    return -1;
  }
}
#endif

