#include <metal_stdlib>
using namespace metal;

// One instance per splat, four vertices per instance (triangle strip quad). The same
// math as the Vulkan shader: the reference rasterizer of Kerbl et al. 2023 and
// Zwicker's EWA projection, ported from MetalSplatter's SplatProcessing.metal.

struct Camera {
  float4x4 view;
  float4x4 proj;
  float2 focal;        // pixels: screenSize * proj[0][0] / 2, screenSize * proj[1][1] / 2
  float2 tanHalfFov;   // 1 / proj[0][0], 1 / proj[1][1]
  float2 screenSize;   // pixels
  uint outputLinear;   // 1 when the target is sRGB and expects linear values
  uint pad;
  float4 cameraPosition;  // world space, for the view direction the SH is evaluated along
};

// Spherical harmonics degree of the world, 0 to 3: one pipeline per degree. With
// degree 0 the SH buffer is never read.
constant uint SH_DEGREE [[function_constant(0)]];
constant uint SH_COEFFICIENTS = (SH_DEGREE + 1) * (SH_DEGREE + 1) - 1;
// Coefficients are rgb halves, channel fastest, two halves per uint, no padding.
constant uint SH_STRIDE = (SH_COEFFICIENTS * 3 + 1) / 2;

struct Splat {
  float px, py, pz;  // world position
  uint rgba8;        // colour and alpha, 8 bits each
  uint cov0;         // halves: xx, xy
  uint cov1;         // halves: xz, yy
  uint cov2;         // halves: yz, zz
  uint lodAlpha;     // float bits of an opacity above 1 (level of detail nodes), else 0
};

struct SplatVertex {
  float4 position [[position]];
  float2 relativePosition;  // in units of sigma
  float4 color;
};

constant float kBoundsRadius = 3.0;     // draw out to 3 sigma; beyond that nothing is visible
constant float kLodBoundsRadius = 5.0;  // an opacity of 1000 stays solid out past 3 sigma
constant float2 kCorners[4] = {float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)};

static float2 unpackHalf2(uint packed) {
  return float2(as_type<half2>(packed));
}

static float4 unpackUnorm4x8(uint packed) {
  return float4(packed & 0xffu, (packed >> 8) & 0xffu, (packed >> 16) & 0xffu, packed >> 24) /
         255.0;
}

// Projects the 3D covariance to screen space: Sigma' = J W Sigma W^T J^T.
static float3 projectCovariance(constant Camera& cam, float3 viewPos, float4 covA, float2 covB) {
  float invZ = 1.0 / viewPos.z;
  float invZ2 = invZ * invZ;

  // Clamp the projected center so the Jacobian stays finite at the frustum edges.
  float2 lim = 1.3 * cam.tanHalfFov;
  viewPos.x = clamp(viewPos.x * invZ, -lim.x, lim.x) * viewPos.z;
  viewPos.y = clamp(viewPos.y * invZ, -lim.y, lim.y) * viewPos.z;

  // Columns, as in GLSL: J = mat3(c0, c1, c2).
  float3x3 J = float3x3(float3(cam.focal.x * invZ, 0.0, 0.0),
                        float3(0.0, cam.focal.y * invZ, 0.0),
                        float3(-cam.focal.x * viewPos.x * invZ2, -cam.focal.y * viewPos.y * invZ2,
                               0.0));
  float3x3 W = float3x3(cam.view[0].xyz, cam.view[1].xyz, cam.view[2].xyz);
  float3x3 T = J * W;
  float3x3 Vrk = float3x3(float3(covA.x, covA.y, covA.z), float3(covA.y, covA.w, covB.x),
                          float3(covA.z, covB.x, covB.y));
  float3x3 cov = T * Vrk * transpose(T);
  // Low pass filter: every splat is at least about a pixel wide, so none flicker.
  return float3(cov[0][0] + 0.3, cov[0][1], cov[1][1] + 0.3);
}

// Eigen decomposition of the symmetric 2x2 (a, b; b, d): the ellipse axes in pixels.
static void ellipseAxes(float3 cov2D, thread float2& axis1, thread float2& axis2) {
  float a = cov2D.x, b = cov2D.y, d = cov2D.z;
  float det = a * d - b * b;
  float mean = 0.5 * (a + d);
  float dist = max(0.1, sqrt(max(mean * mean - det, 0.0)));
  float lambda1 = mean + dist;
  float lambda2 = mean - dist;
  float2 e1 = (b == 0.0) ? ((a > d) ? float2(1, 0) : float2(0, 1)) : normalize(float2(b, d - lambda2));
  float2 e2 = float2(e1.y, -e1.x);
  axis1 = e1 * sqrt(lambda1);
  axis2 = e2 * sqrt(max(lambda2, 0.0));
}

// Half number h of splat `base` in the SH buffer.
static float shHalf(const device uint* shData, uint base, uint h) {
  float2 pair = unpackHalf2(shData[base + h / 2u]);
  return (h & 1u) == 0u ? pair.x : pair.y;
}

static float3 shCoefficient(const device uint* shData, uint base, uint k) {
  return float3(shHalf(shData, base, k * 3u), shHalf(shData, base, k * 3u + 1u),
                shHalf(shData, base, k * 3u + 2u));
}

// Colour change along the unit direction `d` from the camera to the splat, from the
// real spherical harmonics bands 1 to 3 in the 3DGS reference convention. The base
// colour already contains the band 0 term (0.5 + C0 * dc).
static float3 shColor(const device uint* shData, uint index, float3 d) {
  const float C1 = 0.4886025119;
  const float C2[5] = {1.0925484306, -1.0925484306, 0.3153915653, -1.0925484306, 0.5462742153};
  const float C3[7] = {-0.5900435899, 2.8906114426, -0.4570457995, 0.3731763326,
                       -0.4570457995, 1.4453057213, -0.5900435899};
  uint base = index * SH_STRIDE;
  float x = d.x, y = d.y, z = d.z;
  float3 c = -C1 * y * shCoefficient(shData, base, 0u) + C1 * z * shCoefficient(shData, base, 1u) -
             C1 * x * shCoefficient(shData, base, 2u);
  if (SH_DEGREE >= 2u) {
    float xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
    c += C2[0] * xy * shCoefficient(shData, base, 3u) + C2[1] * yz * shCoefficient(shData, base, 4u) +
         C2[2] * (2.0 * zz - xx - yy) * shCoefficient(shData, base, 5u) +
         C2[3] * xz * shCoefficient(shData, base, 6u) +
         C2[4] * (xx - yy) * shCoefficient(shData, base, 7u);
    if (SH_DEGREE >= 3u) {
      c += C3[0] * y * (3.0 * xx - yy) * shCoefficient(shData, base, 8u) +
           C3[1] * xy * z * shCoefficient(shData, base, 9u) +
           C3[2] * y * (4.0 * zz - xx - yy) * shCoefficient(shData, base, 10u) +
           C3[3] * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) * shCoefficient(shData, base, 11u) +
           C3[4] * x * (4.0 * zz - xx - yy) * shCoefficient(shData, base, 12u) +
           C3[5] * z * (xx - yy) * shCoefficient(shData, base, 13u) +
           C3[6] * x * (xx - 3.0 * yy) * shCoefficient(shData, base, 14u);
    }
  }
  return c;
}

vertex SplatVertex splatVertex(uint vertexId [[vertex_id]], uint instanceId [[instance_id]],
                               constant Camera& cam [[buffer(0)]],
                               const device Splat* splats [[buffer(1)]],
                               const device uint* order [[buffer(2)]],
                               const device uint* shData [[buffer(3)]]) {
  SplatVertex out;
  uint index = order[instanceId];
  Splat s = splats[index];

  float4 viewPos4 = cam.view * float4(s.px, s.py, s.pz, 1.0);
  float3 viewPos = viewPos4.xyz;
  // Behind the camera: emit a vertex outside clip space so the quad is discarded.
  if (viewPos.z >= 0.0) {
    out.position = float4(0.0, 0.0, 2.0, 1.0);
    out.relativePosition = float2(0.0);
    out.color = float4(0.0);
    return out;
  }

  float4 clip = cam.proj * viewPos4;
  float bounds = 1.2 * clip.w;
  if (clip.z < 0.0 || clip.z > clip.w || clip.x < -bounds || clip.x > bounds ||
      clip.y < -bounds || clip.y > bounds) {
    out.position = float4(0.0, 0.0, 2.0, 1.0);
    out.relativePosition = float2(0.0);
    out.color = float4(0.0);
    return out;
  }

  float2 c0 = unpackHalf2(s.cov0);
  float2 c1 = unpackHalf2(s.cov1);
  float2 c2 = unpackHalf2(s.cov2);
  float3 cov2D = projectCovariance(cam, viewPos, float4(c0, c1), c2);
  float2 axis1, axis2;
  ellipseAxes(cov2D, axis1, axis2);

  // Draw only out to where this splat's contribution drops below 1/255, which is
  // where the fragment stage would discard anyway: exp(-r^2 / 2) * alpha = 1 / 255.
  // Faint splats, the majority, get a much smaller quad; opaque ones keep 3 sigma.
  float4 rgba = unpackUnorm4x8(s.rgba8);
  // A level of detail node stands in for many overlapping splats: its opacity exceeds
  // one and its solid core reaches further out before the falloff takes it under 1/255.
  float alpha = s.lodAlpha != 0u ? as_type<float>(s.lodAlpha) : rgba.a;
  float radius = min(s.lodAlpha != 0u ? kLodBoundsRadius : kBoundsRadius,
                     sqrt(2.0 * log(max(alpha * 255.0, 1.0))));

  float2 corner = kCorners[vertexId];
  float2 delta = (corner.x * axis1 + corner.y * axis2) * 2.0 * radius / cam.screenSize;
  out.position = float4(clip.xy + delta * clip.w, clip.z, clip.w);
  out.relativePosition = radius * corner;

  float3 rgb = rgba.rgb;
  if (SH_DEGREE >= 1u) {
    float3 dir = normalize(float3(s.px, s.py, s.pz) - cam.cameraPosition.xyz);
    rgb = max(rgb + shColor(shData, index, dir), float3(0.0));
  }
  if (cam.outputLinear == 1u) rgb = pow(rgb, float3(2.2));
  out.color = float4(rgb, alpha);
  return out;
}

fragment float4 splatFragment(SplatVertex in [[stage_in]]) {
  float r2 = dot(in.relativePosition, in.relativePosition);
  // The Gaussian falloff evaluated at this pixel, scaled by the splat's opacity. The
  // vertex stage sized the quad so that nothing outside it would pass the threshold.
  // Level of detail nodes carry an opacity above one: a solid core that fades at the edge.
  float alpha = min(exp(-0.5 * r2) * in.color.a, 1.0);
  if (alpha < 1.0 / 255.0) discard_fragment();
  return float4(in.color.rgb, alpha);
}

// The render scale pass: one triangle over the whole drawable, sampling the offscreen
// target with linear filtering.
struct BlitVertex {
  float4 position [[position]];
  float2 uv;
};

vertex BlitVertex blitVertex(uint vertexId [[vertex_id]]) {
  const float2 corners[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
  BlitVertex out;
  out.position = float4(corners[vertexId], 0, 1);
  out.uv = float2(corners[vertexId].x * 0.5 + 0.5, 0.5 - corners[vertexId].y * 0.5);
  return out;
}

fragment float4 blitFragment(BlitVertex in [[stage_in]], texture2d<float> source [[texture(0)]]) {
  constexpr sampler linearSampler(filter::linear, address::clamp_to_edge);
  return source.sample(linearSampler, in.uv);
}

// Visibility on the GPU: which splats of the slab ranges to draw, ordered back to front.
//
// `visibility` projects every splat of the ranges named, keeps the ones inside the view
// and appends a (key, slab index) pair per survivor; key is the bit pattern of the
// squared distance to the camera, inverted, so an ascending sort puts the farthest
// first, the same order the CPU DistanceSorter produces. `prepareSort` turns the count
// into the dispatch and draw arguments, and the radix kernels sort the pairs by key:
// a least significant digit radix sort, 4 bits per pass, 8 passes, each pass a block
// histogram, a scan of the histograms per digit, and a stable scatter. Stability comes
// from a fixed element order: thread t of a block owns elements t*16 to t*16+15, and
// ranks are the prefix over threads, then over a thread's own elements.

struct Range {
  uint offset;
  uint count;
};

constant uint kSortThreads = 256;
constant uint kSortPerThread = 16;
constant uint kSortBlock = kSortThreads * kSortPerThread;
constant uint kSortDigitBits = 8;
constant uint kSortBins = 1u << kSortDigitBits;
constant uint kSortPerSimdgroup = 32 * kSortPerThread;
constant uint kSortSimdgroups = kSortThreads / 32;

struct SortDispatch {
  uint threadgroupsX, threadgroupsY, threadgroupsZ;  // MTLDispatchThreadgroupsIndirectArguments
  uint blocks;  // the same number, for the kernels
};

struct DrawArguments {  // MTLDrawPrimitivesIndirectArguments
  uint vertexCount, instanceCount, vertexStart, baseInstance;
};

static uint findRange(const device uint* starts, uint rangeCount, uint t) {
  uint lo = 0, hi = rangeCount;  // starts[r] <= t < starts[r + 1]
  while (hi - lo > 1) {
    uint mid = (lo + hi) / 2;
    if (starts[mid] <= t) lo = mid; else hi = mid;
  }
  return lo;
}

kernel void visibility(uint t [[thread_position_in_grid]],
                       constant Camera& cam [[buffer(0)]],
                       const device Splat* splats [[buffer(1)]],
                       const device Range* ranges [[buffer(2)]],
                       const device uint* rangeStarts [[buffer(3)]],
                       constant uint& rangeCount [[buffer(4)]],
                       device uint* keys [[buffer(5)]],
                       device uint* values [[buffer(6)]],
                       device atomic_uint* count [[buffer(7)]]) {
  // Every thread of the simdgroup takes part in the reductions below, so a thread past
  // the end goes through with nothing to add instead of returning.
  const uint total = rangeStarts[rangeCount];
  bool visible = false;
  uint key = 0;
  uint index = 0;
  if (t < total) {
    uint r = findRange(rangeStarts, rangeCount, t);
    index = ranges[r].offset + (t - rangeStarts[r]);
    Splat s = splats[index];
    float4 world = float4(s.px, s.py, s.pz, 1.0);
    float4 viewPos = cam.view * world;
    if (viewPos.z < 0.0) {
      float4 clip = cam.proj * viewPos;
      float bounds = 1.2 * clip.w;
      visible = clip.z >= 0.0 && clip.z <= clip.w && clip.x >= -bounds && clip.x <= bounds &&
                clip.y >= -bounds && clip.y <= bounds;
    }
    float3 d = world.xyz - cam.cameraPosition.xyz;
    key = ~as_type<uint>(dot(d, d));
  }
  uint rank = simd_prefix_exclusive_sum(visible ? 1u : 0u);
  uint survivors = simd_sum(visible ? 1u : 0u);
  uint base = 0;
  if (simd_is_first() && survivors > 0) {
    base = atomic_fetch_add_explicit(count, survivors, memory_order_relaxed);
  }
  base = simd_broadcast_first(base);
  if (visible) {
    keys[base + rank] = key;
    values[base + rank] = index;
  }
}

kernel void prepareSort(const device uint* count [[buffer(0)]],
                        device SortDispatch* dispatch [[buffer(1)]],
                        device DrawArguments* draw [[buffer(2)]]) {
  uint n = count[0];
  uint blocks = (n + kSortBlock - 1) / kSortBlock;
  dispatch->threadgroupsX = max(blocks, 1u);
  dispatch->threadgroupsY = 1;
  dispatch->threadgroupsZ = 1;
  dispatch->blocks = max(blocks, 1u);
  draw->vertexCount = 4;
  draw->instanceCount = n;
  draw->vertexStart = 0;
  draw->baseInstance = 0;
}

// The lanes of this simdgroup whose element has the same digit as this lane's. Every
// lane of the simdgroup must call it; a lane past the end passes valid = false.
static uint matchDigit(uint digit, bool valid) {
  uint peers = uint(simd_vote::vote_t(simd_ballot(valid)));
  for (uint b = 0; b < kSortDigitBits; ++b) {
    bool bit = (digit >> b) & 1u;
    uint vote = uint(simd_vote::vote_t(simd_ballot(bit)));
    peers &= bit ? vote : ~vote;
  }
  return peers;
}

// The block's elements are owned simdgroup by simdgroup, each simdgroup holding a
// contiguous run that it reads one coalesced row of 32 at a time. Ranking simdgroup
// major, then row, then lane therefore follows memory order, which keeps the sort
// stable without a counter per thread.
static uint simdgroupElement(uint block, uint sg, uint row, uint lane) {
  return block * kSortBlock + sg * kSortPerSimdgroup + row * 32 + lane;
}

// Counts the digits of each block: histogram[digit * blocks + block].
kernel void radixHistogram(uint tid [[thread_index_in_threadgroup]],
                           uint block [[threadgroup_position_in_grid]],
                           uint lane [[thread_index_in_simdgroup]],
                           uint sg [[simdgroup_index_in_threadgroup]],
                           const device uint* keys [[buffer(0)]],
                           const device uint* count [[buffer(1)]],
                           const device SortDispatch* dispatch [[buffer(2)]],
                           constant uint& shift [[buffer(3)]],
                           device uint* histogram [[buffer(4)]]) {
  threadgroup atomic_uint bins[kSortBins];
  atomic_store_explicit(&bins[tid], 0u, memory_order_relaxed);
  threadgroup_barrier(mem_flags::mem_threadgroup);
  uint n = count[0];
  for (uint row = 0; row < kSortPerThread; ++row) {
    uint e = simdgroupElement(block, sg, row, lane);
    bool valid = e < n;
    uint d = valid ? (keys[e] >> shift) & (kSortBins - 1) : 0u;
    uint peers = matchDigit(d, valid);
    if (valid && ctz(peers) == lane) {
      atomic_fetch_add_explicit(&bins[d], popcount(peers), memory_order_relaxed);
    }
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);
  histogram[tid * dispatch->blocks + block] = atomic_load_explicit(&bins[tid], memory_order_relaxed);
}

// One threadgroup per digit: exclusive scan of that digit's row of block counts, in
// place, and the row total into totals[digit].
kernel void radixScan(uint tid [[thread_index_in_threadgroup]],
                      uint digit [[threadgroup_position_in_grid]],
                      uint lane [[thread_index_in_simdgroup]],
                      uint sg [[simdgroup_index_in_threadgroup]],
                      const device SortDispatch* dispatch [[buffer(0)]],
                      device uint* histogram [[buffer(1)]],
                      device uint* totals [[buffer(2)]]) {
  threadgroup uint sgTotals[kSortSimdgroups];
  threadgroup uint sgOffsets[kSortSimdgroups];
  uint blocks = dispatch->blocks;
  device uint* row = histogram + digit * blocks;
  uint carry = 0;
  for (uint start = 0; start < blocks; start += kSortThreads) {
    uint i = start + tid;
    uint v = i < blocks ? row[i] : 0u;
    uint p = simd_prefix_exclusive_sum(v);
    uint s = simd_sum(v);
    if (lane == 0) sgTotals[sg] = s;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0) {
      uint run = 0;
      for (uint g = 0; g < kSortSimdgroups; ++g) {
        sgOffsets[g] = run;
        run += sgTotals[g];
      }
      sgTotals[0] = run;  // the chunk total, read after the barrier
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (i < blocks) row[i] = carry + sgOffsets[sg] + p;
    carry += sgTotals[0];
    threadgroup_barrier(mem_flags::mem_threadgroup);
  }
  if (tid == 0) totals[digit] = carry;
}

// Moves every pair to its place for this digit, keeping the order of equal digits.
kernel void radixScatter(uint tid [[thread_index_in_threadgroup]],
                         uint block [[threadgroup_position_in_grid]],
                         uint lane [[thread_index_in_simdgroup]],
                         uint sg [[simdgroup_index_in_threadgroup]],
                         const device uint* keysIn [[buffer(0)]],
                         const device uint* valuesIn [[buffer(1)]],
                         device uint* keysOut [[buffer(2)]],
                         device uint* valuesOut [[buffer(3)]],
                         const device uint* count [[buffer(4)]],
                         const device SortDispatch* dispatch [[buffer(5)]],
                         constant uint& shift [[buffer(6)]],
                         const device uint* histogram [[buffer(7)]],
                         const device uint* totals [[buffer(8)]]) {
  // How many of each digit the simdgroup has ranked so far; after the rows, its offset
  // within the block for that digit.
  threadgroup uint sgCounts[kSortSimdgroups][kSortBins];
  threadgroup uint sgTotals[kSortSimdgroups];
  threadgroup uint digitBase[kSortBins];
  threadgroup uint blockBase[kSortBins];
  uint n = count[0];
  uint blocks = dispatch->blocks;

  for (uint g = 0; g < kSortSimdgroups; ++g) sgCounts[g][tid] = 0;
  blockBase[tid] = histogram[tid * blocks + block];
  {  // digitBase[d] = sum of the totals of the digits below d
    uint v = totals[tid];
    uint p = simd_prefix_exclusive_sum(v);
    uint s = simd_sum(v);
    if (lane == 0) sgTotals[sg] = s;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint run = 0;
    for (uint g = 0; g < sg; ++g) run += sgTotals[g];
    digitBase[tid] = run + p;
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);

  uint keys[kSortPerThread];
  uint ranks[kSortPerThread];
  for (uint row = 0; row < kSortPerThread; ++row) {
    uint e = simdgroupElement(block, sg, row, lane);
    bool valid = e < n;
    keys[row] = valid ? keysIn[e] : 0u;
    uint d = (keys[row] >> shift) & (kSortBins - 1);
    uint peers = matchDigit(d, valid);
    uint leader = ctz(peers);
    uint base = 0;
    if (valid && leader == lane) {
      base = sgCounts[sg][d];
      sgCounts[sg][d] = base + popcount(peers);
    }
    base = simd_shuffle(base, valid ? leader : lane);
    ranks[row] = base + popcount(peers & ((1u << lane) - 1u));
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);
  {  // exclusive prefix over the simdgroups of digit tid, in place
    uint run = 0;
    for (uint g = 0; g < kSortSimdgroups; ++g) {
      uint v = sgCounts[g][tid];
      sgCounts[g][tid] = run;
      run += v;
    }
  }
  threadgroup_barrier(mem_flags::mem_threadgroup);

  for (uint row = 0; row < kSortPerThread; ++row) {
    uint e = simdgroupElement(block, sg, row, lane);
    if (e >= n) continue;
    uint d = (keys[row] >> shift) & (kSortBins - 1);
    uint dst = digitBase[d] + blockBase[d] + sgCounts[sg][d] + ranks[row];
    keysOut[dst] = keys[row];
    valuesOut[dst] = valuesIn[e];
  }
}
