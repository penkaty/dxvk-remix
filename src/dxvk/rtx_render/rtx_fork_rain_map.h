#pragma once

// rtx_fork_rain_map.h - fork-owned. Top-down height field of the static geometry
// around the camera (cover height, depression, slope), traced every frame and read
// by the wetness model (rtx_fork_wetness.slangh) through the common
// binding BINDING_FORK_RAIN_MAP. Shaders in shaders/rtx/pass/fork_rain_map/.
//
// Ported from xoxor4d's dxvk-remix game/gta4 branch (rtx_fork_rain_map.h, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX: the map window lies in the plane perpendicular to the scene up axis (rtx.zUp: XZ for Y-up RBR).

#include "rtx_resources.h"

struct RaytraceArgs;

namespace dxvk {

  class RtxContext;

  class ForkRainMap {
  public:
    // Picks the map window from the camera and fills the map constants. Every frame, before the constants are uploaded.
    static void prepare(RtxContext& ctx, RaytraceArgs& constants);

    // Traces and filters the map. After the acceleration structures are built, before the G-buffer pass.
    static void dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);

    // The view bound in the common ray tracing set: the map, or a 1x1 placeholder while the map is off.
    static Rc<DxvkImageView> view(RtxContext& ctx);
  };

} // namespace dxvk
