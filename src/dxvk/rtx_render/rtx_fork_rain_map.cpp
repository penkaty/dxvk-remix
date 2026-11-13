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

#include "rtx_fork_game_state.h"
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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
        TEXTURE2D(RAIN_MAP_BINDING_ROAD_MASK)
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
      bool traceThisFrame = true;

      // The window the map was traced for and the options it depends on.
      float originU = 0.0f;
      float originV = 0.0f;
      float traceTop = 0.0f;
      float traceBottom = 0.0f;
      Vector3 tracedCamera = Vector3(0.0f);
      float tracedOptions[6] = {};
      uint32_t framesSinceTrace = 0;

      // The host's road mask (game value "__wet.roadMask", documentation/RemixWetAPI.md): one byte per cell, 255 = road.
      Resources::Resource roadMask;
      std::string roadMaskValue;
      std::vector<uint8_t> roadMaskCells;
      uint32_t roadMaskResolution = 0;
      uint32_t roadMaskImageResolution = 0;
      float roadMaskOriginU = 0.0f;
      float roadMaskOriginV = 0.0f;
      float roadMaskCell = 1.0f;
      bool roadMaskDirty = false;
    };

    constexpr uint32_t kMaxRoadMaskResolution = 512;

    // Reads "__wet.roadMask" = "<originU> <originV> <cellSize> <cells per side> <hex digits, 4 cells each, row by row>".
    // An empty or malformed value means no mask.
    void readRoadMask(State& s) {
      std::string value;
      fork_game_state::GameStateStore::get().tryGet("__wet.roadMask", value);
      if (value == s.roadMaskValue) {
        return;
      }
      s.roadMaskValue = value;
      s.roadMaskResolution = 0;
      s.traced = false;

      float originU = 0.0f;
      float originV = 0.0f;
      float cell = 0.0f;
      unsigned resolution = 0;
      int consumed = 0;
      if (sscanf(value.c_str(), "%f %f %f %u %n", &originU, &originV, &cell, &resolution, &consumed) < 4 ||
          cell <= 0.0f || resolution == 0 || resolution > kMaxRoadMaskResolution) {
        return;
      }
      const size_t cells = size_t(resolution) * resolution;
      const size_t digits = (cells + 3) / 4;
      if (value.size() < size_t(consumed) + digits) {
        return;
      }
      s.roadMaskCells.assign(cells, 0);
      for (size_t i = 0; i < cells; ++i) {
        const char c = value[size_t(consumed) + i / 4];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
        s.roadMaskCells[i] = (digit >> (3 - int(i % 4))) & 1 ? 255 : 0;
      }
      s.roadMaskOriginU = originU;
      s.roadMaskOriginV = originV;
      s.roadMaskCell = cell;
      s.roadMaskResolution = resolution;
      s.roadMaskDirty = true;
    }

    // The mask image always exists for the relief pass's binding: one cell while the host sends none.
    void uploadRoadMask(RtxContext& ctx, State& s) {
      const uint32_t res = std::max(s.roadMaskResolution, 1u);
      Rc<DxvkContext> dctx = &ctx;
      if (!s.roadMask.isValid() || s.roadMaskImageResolution != res) {
        s.roadMask = Resources::createImageResource(dctx, "Fork Rain Map Road Mask", VkExtent3D { res, res, 1 }, VK_FORMAT_R8_UNORM);
        s.roadMaskImageResolution = res;
        s.roadMaskDirty = true;
      }
      if (!s.roadMaskDirty) {
        return;
      }
      s.roadMaskDirty = false;
      const uint8_t road = 255;
      const uint8_t* data = s.roadMaskResolution ? s.roadMaskCells.data() : &road;
      ctx.updateImage(s.roadMask.image, VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, VkOffset3D { 0, 0, 0 },
                      VkExtent3D { res, res, 1 }, const_cast<uint8_t*>(data), res, res * res);
    }

    // A map that has not been traced for this long is traced again, for geometry that streamed in around a still camera.
    constexpr uint32_t kMaxFramesBetweenTraces = 60;

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
    if (s.enabled) {
      readRoadMask(s);
    }

    const uint32_t res = requestedResolution();
    const float extent = std::max(WetnessOptions::mapExtent(), 8.0f);
    const float texel = extent / float(res);

    // The window lies in the plane perpendicular to the scene up axis; "up" is the trace axis
    const Vector3 camera = ctx.getSceneManager().getCamera().getPosition();
    const bool zUp = RtxOptions::zUp();
    const float planeU = camera.x;
    const float planeV = zUp ? camera.y : camera.z;
    const float camUp = zUp ? camera.z : camera.y;

    const float options[6] = { float(res), extent, WetnessOptions::mapTraceAbove(), WetnessOptions::mapTraceBelow(),
                               WetnessOptions::occlusionMinHeight(), WetnessOptions::occlusionFeather() };
    const float updateDistance = WetnessOptions::mapUpdateDistance();
    const Vector3 moved = camera - s.tracedCamera;

    s.traceThisFrame = !s.traced || updateDistance <= 0.0f || memcmp(options, s.tracedOptions, sizeof(options)) != 0 ||
      dot(moved, moved) >= updateDistance * updateDistance || ++s.framesSinceTrace >= kMaxFramesBetweenTraces;

    if (s.traceThisFrame) {
      // Snapped to the texel grid so the map does not swim with the camera
      s.originU = std::floor(planeU / texel) * texel - extent * 0.5f;
      s.originV = std::floor(planeV / texel) * texel - extent * 0.5f;
      s.traceTop = camUp + std::max(WetnessOptions::mapTraceAbove(), 1.0f);
      s.traceBottom = camUp - std::max(WetnessOptions::mapTraceBelow(), 1.0f);
      s.tracedCamera = camera;
      memcpy(s.tracedOptions, options, sizeof(options));
      s.framesSinceTrace = 0;
    }

    constants.wetPuddleQuality = uint32_t(std::clamp(WetnessOptions::puddleQuality(), 0, 2));
    constants.wetMapResolution = res;
    constants.wetMapOriginX = s.originU;
    constants.wetMapOriginY = s.originV;
    constants.wetMapExtent = extent;
    constants.wetMapTexel = texel;
    constants.wetMapTraceTop = s.traceTop;
    constants.wetMapTraceBottom = s.traceBottom;
    constants.wetRoadMaskOriginX = s.roadMaskOriginU;
    constants.wetRoadMaskOriginY = s.roadMaskOriginV;
    constants.wetRoadMaskCell = s.roadMaskCell;
    constants.wetRoadMaskResolution = s.roadMaskResolution;
  }

  void ForkRainMap::dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    State& s = state();
    if (!s.enabled) {
      s.traced = false;
      return;
    }
    if (!s.traceThisFrame) {
      return;
    }
    ensureResources(ctx);
    uploadRoadMask(ctx, s);

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
    ctx.bindResourceView(RAIN_MAP_BINDING_ROAD_MASK, s.roadMask.view, nullptr);
    ctx.getCommandList()->trackResource<DxvkAccess::Read>(s.roadMask.image);
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
