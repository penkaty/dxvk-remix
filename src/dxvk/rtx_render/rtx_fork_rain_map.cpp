// src/dxvk/rtx_render/rtx_fork_rain_map.cpp
//
// Fork-owned file. Rain map: a top-down height field of the static geometry around the
// camera. Two compute passes per frame: one downward ray per texel (rain_map_trace), then
// the relief of the result (rain_map_relief): depression and slope per texel. The wetness
// model reads it for the rain shadow and for where water pools.
//
// Ported from xoxor4d's dxvk-remix game/gta4 branch (rtx_fork_rain_map.cpp, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX: generalised to the scene up axis (rtx.zUp); the map window is (x, y) for Z-up and (x, z) for Y-up.

#include "rtx_fork_rain_map.h"

#include "rtx_fork_hooks.h"
#include "rtx_fork_wetness.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_scene_manager.h"
#include "rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/fork_rain_map/rain_map_binding_indices.h"
#include "rtx/pass/raytrace_args.h"

#include <rtx_shaders/rain_map_trace.h>
#include <rtx_shaders/rain_map_relief.h>

#include <algorithm>
#include <cmath>

namespace dxvk {

  namespace {

    class RainMapTraceShader : public ManagedShader {
      SHADER_SOURCE(RainMapTraceShader, VK_SHADER_STAGE_COMPUTE_BIT, rain_map_trace)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        RW_TEXTURE2D(RAIN_MAP_BINDING_HEIGHT_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(RainMapTraceShader);

    class RainMapReliefShader : public ManagedShader {
      SHADER_SOURCE(RainMapReliefShader, VK_SHADER_STAGE_COMPUTE_BIT, rain_map_relief)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        TEXTURE2D(RAIN_MAP_BINDING_HEIGHT_INPUT)
        RW_TEXTURE2D(RAIN_MAP_BINDING_MAP_OUTPUT)
      END_PARAMETER()
    };
    PREWARM_SHADER_PIPELINE(RainMapReliefShader);

    // Heap allocated and never freed on purpose: the images hold the device, and static destructors run after it is gone.
    struct State {
      Resources::Resource height;
      Resources::Resource map;
      Resources::Resource placeholder;
      uint32_t resolution = 0;
      bool enabled = false;
      bool traced = false;
    };

    State& state() {
      static State* s_state = new State();
      return *s_state;
    }

    uint32_t requestedResolution() {
      const uint32_t res = std::clamp(uint32_t(WetnessOptions::mapResolution()), 64u, 2048u);
      return (res + 7u) & ~7u;
    }

    void ensureResources(RtxContext& ctx) {
      State& s = state();
      const uint32_t res = requestedResolution();
      if (s.map.isValid() && s.resolution == res) {
        return;
      }
      Rc<DxvkContext> dctx = &ctx;
      const VkExtent3D extent = { res, res, 1 };
      s.height = Resources::createImageResource(dctx, "Fork Rain Map Height", extent, VK_FORMAT_R32G32_SFLOAT);
      s.map = Resources::createImageResource(dctx, "Fork Rain Map", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
      s.resolution = res;
      s.traced = false;
    }

  } // anonymous namespace

  void ForkRainMap::prepare(RtxContext& ctx, RaytraceArgs& constants) {
    State& s = state();
    // Only while something can be wet: no wetness, no map, no pass
    s.enabled = WetnessOptions::puddleQuality() >= 2 && WetnessOptions::globalWetness() > 0.0f;

    const uint32_t res = requestedResolution();
    const float extent = std::max(WetnessOptions::mapExtent(), 8.0f);
    const float texel = extent / float(res);

    // The window lies in the plane perpendicular to the scene up axis; "up" is the trace axis
    const Vector3 camera = ctx.getSceneManager().getCamera().getPosition();
    const bool zUp = RtxOptions::zUp();
    const float planeU = camera.x;
    const float planeV = zUp ? camera.y : camera.z;
    const float camUp = zUp ? camera.z : camera.y;

    // Snapped to the texel grid so the map does not swim with the camera
    const float originU = std::floor(planeU / texel) * texel - extent * 0.5f;
    const float originV = std::floor(planeV / texel) * texel - extent * 0.5f;

    constants.wetPuddleQuality = uint32_t(std::clamp(WetnessOptions::puddleQuality(), 0, 2));
    constants.wetMapResolution = res;
    constants.wetMapOriginX = originU;
    constants.wetMapOriginY = originV;
    constants.wetMapExtent = extent;
    constants.wetMapTexel = texel;
    constants.wetMapTraceTop = camUp + std::max(WetnessOptions::mapTraceAbove(), 1.0f);
    constants.wetMapTraceBottom = camUp - std::max(WetnessOptions::mapTraceBelow(), 1.0f);
  }

  void ForkRainMap::dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    State& s = state();
    if (!s.enabled) {
      return;
    }
    ensureResources(ctx);

    ScopedGpuProfileZone(&ctx, "Fork Rain Map");

    const uint32_t groups = (s.resolution + 7u) / 8u;

    ctx.bindCommonRayTracingResources(rtOutput);
    // The map is written below; the common set must not read it in the same dispatch
    ctx.bindResourceView(BINDING_FORK_RAIN_MAP, view(ctx), nullptr);

    ctx.bindResourceView(RAIN_MAP_BINDING_HEIGHT_OUTPUT, s.height.view, nullptr);
    ctx.getCommandList()->trackResource<DxvkAccess::Write>(s.height.image);
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RainMapTraceShader::getShader());
    ctx.dispatch(groups, groups, 1);

    ctx.bindResourceView(RAIN_MAP_BINDING_HEIGHT_INPUT, s.height.view, nullptr);
    ctx.bindResourceView(RAIN_MAP_BINDING_MAP_OUTPUT, s.map.view, nullptr);
    ctx.getCommandList()->trackResource<DxvkAccess::Read>(s.height.image);
    ctx.getCommandList()->trackResource<DxvkAccess::Write>(s.map.image);
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, RainMapReliefShader::getShader());
    ctx.dispatch(groups, groups, 1);

    s.traced = true;
  }

  Rc<DxvkImageView> ForkRainMap::view(RtxContext& ctx) {
    State& s = state();
    if (s.enabled && s.traced && s.map.isValid()) {
      return s.map.view;
    }
    if (!s.placeholder.isValid()) {
      Rc<DxvkContext> dctx = &ctx;
      s.placeholder = Resources::createImageResource(dctx, "Fork Rain Map Placeholder", VkExtent3D { 1, 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
    return s.placeholder.view;
  }

namespace fork_hooks {

  void dispatchRainMap(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ForkRainMap::dispatch(ctx, rtOutput);
  }

  Rc<DxvkImageView> rainMapView(RtxContext& ctx) {
    return ForkRainMap::view(ctx);
  }

} // namespace fork_hooks

} // namespace dxvk
