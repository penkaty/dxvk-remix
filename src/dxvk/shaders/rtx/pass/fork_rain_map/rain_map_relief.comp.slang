// Ported from xoxor4d's dxvk-remix game/gta4 branch (rain_map_relief.comp.slang, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX: unchanged apart from this header (heights are along the scene up axis, texel (u, v) is the plane perpendicular to it).
// Fork-owned. Rain map, step 2: relief of the traced height field. Per texel the depression (how much lower it
// is than the ground around it, which is where water pools), the slope (where water runs off) and the rain cover,
// blurred over the feather radius with a tent filter so cover edges come out as a smooth gradient instead of a
// texel-hard line. Neighbours more than a meter away in height are not ground next to this texel (walls,
// bridges) and are left out of the relief.

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/fork_rain_map/rain_map_bindings.slangh"
#include "rtx/pass/raytrace_args.h"

#include "rtx/utility/math.slangh"

static const float kMissBelow = 0.5f;      // heights this far below the trace bottom are misses
static const float kNeighbourRange = 1.0f; // meters; beyond that a neighbour is not the same ground

float2 loadTexel(int2 texel)
{
  const int last = int(cb.wetMapResolution) - 1;
  return RainMapHeightInput[clamp(texel, int2(0, 0), int2(last, last))];
}

// Ground height of a texel; false where the trace found nothing
bool loadHeight(int2 texel, out float height)
{
  height = loadTexel(texel).x;
  return height > cb.wetMapTraceBottom - kMissBelow;
}

// 1 when the texel's cover stands at least the rain shadow min height above its own ground, -1 where the trace
// found nothing (left out of the blur), 0 otherwise
float loadCovered(int2 texel)
{
  const float2 value = loadTexel(texel);
  if (value.x <= cb.wetMapTraceBottom - kMissBelow)
  {
    return -1.0f;
  }
  return value.y - value.x >= cb.wetOcclusionMinHeight ? 1.0f : 0.0f;
}

// Road mask at a position in the map plane, bilinear over its cells.
float loadRoadAt(vec2 position)
{
  const vec2 t = (position - vec2(cb.wetRoadMaskOriginX, cb.wetRoadMaskOriginY)) / cb.wetRoadMaskCell - 0.5f;
  const vec2 base = floor(t);
  const vec2 f = t - base;
  const int last = int(cb.wetRoadMaskResolution) - 1;
  float road = 0.0f;
  [unroll]
  for (int i = 0; i < 4; ++i)
  {
    const int2 offset = int2(i & 1, i >> 1);
    const int2 coord = clamp(int2(base) + offset, int2(0, 0), int2(last, last));
    road += RainMapRoadMask.Load(int3(coord, 0)) * (offset.x == 0 ? 1.0f - f.x : f.x) * (offset.y == 0 ? 1.0f - f.y : f.y);
  }
  return saturate(road);
}

// Share of road around a map texel from the host's road mask: a tent over 2 m, so the mask's edge is a gradient
// a few meters wide and puddles thin out across it. 1 without a mask.
float loadRoad(uint2 texel)
{
  if (cb.wetRoadMaskResolution == 0)
  {
    return 1.0f;
  }
  const vec2 position = vec2(cb.wetMapOriginX, cb.wetMapOriginY) + (vec2(texel) + 0.5f) * cb.wetMapTexel;
  float road = 0.0f;
  float weightSum = 0.0f;
  for (int dy = -2; dy <= 2; ++dy)
  {
    for (int dx = -2; dx <= 2; ++dx)
    {
      const float weight = float((3 - abs(dx)) * (3 - abs(dy)));
      road += loadRoadAt(position + vec2(float(dx), float(dy))) * weight;
      weightSum += weight;
    }
  }
  return road / weightSum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.wetMapResolution))
  {
    return;
  }

  const float2 own = loadTexel(int2(threadId));
  const float height = own.x;
  if (height <= cb.wetMapTraceBottom - kMissBelow)
  {
    RainMapOutput[threadId] = float4(cb.wetMapTraceBottom - 1.0f, cb.wetMapTraceBottom - 1.0f, 0.0f, 0.0f);
    return;
  }

  // Two rings of eight neighbours at 1.5 m and 3 m
  const int r1 = max(1, int(round(1.5f / cb.wetMapTexel)));
  const int r2 = max(r1 + 1, int(round(3.0f / cb.wetMapTexel)));
  const int d1 = max(1, int(round(float(r1) * 0.7071f)));
  const int d2 = max(d1 + 1, int(round(float(r2) * 0.7071f)));
  const int2 offsets[16] = {
    int2(r1, 0), int2(-r1, 0), int2(0, r1), int2(0, -r1), int2(d1, d1), int2(-d1, d1), int2(d1, -d1), int2(-d1, -d1),
    int2(r2, 0), int2(-r2, 0), int2(0, r2), int2(0, -r2), int2(d2, d2), int2(-d2, d2), int2(d2, -d2), int2(-d2, -d2)
  };

  float sum = 0.0f;
  float count = 0.0f;
  // Slope: least squares plane through the inner ring (the ring is symmetric, so the cross terms cancel)
  float sxx = 0.0f, syy = 0.0f, sxh = 0.0f, syh = 0.0f;
  [unroll]
  for (int i = 0; i < 16; ++i)
  {
    float neighbour;
    if (loadHeight(int2(threadId) + offsets[i], neighbour) && abs(neighbour - height) < kNeighbourRange)
    {
      const float dh = neighbour - height;
      sum += dh;
      count += 1.0f;
      if (i < 8)
      {
        const vec2 d = vec2(offsets[i]) * cb.wetMapTexel;
        sxx += d.x * d.x;
        syy += d.y * d.y;
        sxh += d.x * dh;
        syh += d.y * dh;
      }
    }
  }
  const float depression = count > 0.0f ? sum / count : 0.0f;
  const float gx = sxx > 0.0f ? sxh / sxx : 0.0f;
  const float gy = syy > 0.0f ? syh / syy : 0.0f;

  // Rain cover blurred with a tent over the feather radius
  const int radius = clamp(int(round(cb.wetOcclusionFeather / cb.wetMapTexel)), 0, 12);
  float coverSum = 0.0f;
  float coverWeight = 0.0f;
  for (int dy = -radius; dy <= radius; ++dy)
  {
    for (int dx = -radius; dx <= radius; ++dx)
    {
      const float covered = loadCovered(int2(threadId) + int2(dx, dy));
      if (covered >= 0.0f)
      {
        const float weight = float((radius + 1 - abs(dx)) * (radius + 1 - abs(dy)));
        coverSum += covered * weight;
        coverWeight += weight;
      }
    }
  }
  const float coverage = coverWeight > 0.0f ? coverSum / coverWeight : 0.0f;

  // w carries two values: the slope (below 4) plus 4 x the road share in 255 steps (forkWetnessRainMapFetch splits them)
  const float slope = min(length(vec2(gx, gy)), 3.99f);
  RainMapOutput[threadId] = float4(coverage, height, depression, slope + 4.0f * floor(loadRoad(threadId) * 255.0f + 0.5f));
}
