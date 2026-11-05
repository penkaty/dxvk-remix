// src/dxvk/rtx_render/rtx_fork_wetness.cpp
//
// Fork-owned file. Options of the wetness model (rtx.wet.*) and constant fill.
// Ported from xoxor4d's dxvk-remix game/gta4 branch (rtx_fork_wetness.cpp, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX: category driven instead of per draw packed bits, scene up axis generic. No Alt+X panel (removed 2026-11-02 to stay
// close to Remix Plus): the options are set from rtx.conf, the Remix API, or the RBR-RTX proxy's F3 menu (menu/menu.cpp).

#include "rtx_fork_wetness.h"

#include "rtx_fork_hooks.h"
#include "rtx_fork_rain_map.h"
#include "rtx_imgui.h"

#include "rtx/pass/raytrace_args.h"

#include "../imgui/imgui.h"

#include <algorithm>

namespace dxvk {

  void WetnessOptions::fillShaderParams(RtxContext& ctx, RaytraceArgs& constants) {
    constants.wetOcclusionRayLength = std::max(occlusionRayLength(), 1.0f);
    constants.wetOcclusionDistance = std::max(occlusionDistance(), 0.0f);
    constants.wetDetailDistance = std::max(detailDistance(), 0.0f);
    constants.wetRaindropDistance = std::max(raindropDistance(), 0.0f);
    constants.wetMinRoughness = std::clamp(minRoughness(), 0.0f, 1.0f);
    constants.wetAlbedoDarkening = std::clamp(albedoDarkening(), 0.0f, 1.0f);
    constants.wetSaturationBoost = std::max(saturationBoost(), 0.0f);
    constants.wetPuddleCoverage = std::clamp(puddleCoverage(), 0.0f, 1.0f);
    constants.wetPuddleRoughness = std::clamp(puddleRoughness(), 0.0f, 1.0f);
    constants.wetPuddleDarkening = std::clamp(puddleDarkening(), 0.0f, 1.0f);
    constants.wetShoreDarkening = std::clamp(shoreDarkening(), 0.0f, 1.0f);
    constants.wetRippleStrength = std::max(rippleStrength(), 0.0f);
    constants.wetRippleWavelength = std::max(rippleWavelength(), 1.0f);
    constants.wetRippleSpeed = std::max(rippleSpeed(), 0.0f);
    constants.wetRaindropGroundScale = std::clamp(raindropGroundScale(), 0.0f, 1.0f);
    constants.wetOcclusionMinHeight = std::max(occlusionMinHeight(), 0.0f);
    constants.wetPuddleOcclusion = std::clamp(puddleOcclusion(), 0.0f, 1.0f);
    constants.wetNoiseScale = std::max(noiseScale(), 0.1f);
    constants.wetMicroAmplitude = std::max(microAmplitude(), 0.001f);
    constants.wetDepressionWeight = std::max(depressionWeight(), 0.0f);
    constants.wetSlopeLimit = std::max(slopeLimit(), 0.005f);
    constants.wetEdgeWidth = std::max(edgeWidth(), 0.0001f);
    constants.wetShoreWidth = std::max(shoreWidth(), 0.0f);
    constants.wetSlopeFeather = std::max(slopeFeather(), 0.1f);
    constants.wetRippleVariation = std::clamp(rippleVariation(), 0.0f, 1.0f);
    constants.wetOcclusionFeather = std::max(occlusionFeather(), 0.0f);
    constants.wetGlobalWetness = std::clamp(globalWetness(), 0.0f, 1.0f);

    constants.wetBlendWidth = std::clamp(blendWidth(), 0.0f, 1.0f);
    constants.wetMinNormal = std::clamp(minNormal(), 0.0f, 1.0f);
    constants.wetFullyWetRoughnessScale = std::max(fullyWetRoughnessScale(), 0.0f);
    constants.wetRainIntensity = std::clamp(rainIntensity(), 0.0f, 1.0f);
    constants.wetRaindropScale = std::max(raindropScale(), 0.1f);
    constants.wetRaindropDensity = std::max(raindropDensity(), 1.0f);
    constants.wetFlags = (patches() ? 1u : 0u) | (puddles() ? 2u : 0u) | (rainShadow() ? 4u : 0u) |
                         (rainShadowDither() ? 8u : 0u) | (raindrops() ? 16u : 0u);

    ForkRainMap::prepare(ctx, constants);
  }

namespace fork_hooks {

  void fillWetnessShaderParams(RtxContext& ctx, RaytraceArgs& constants) {
    WetnessOptions::fillShaderParams(ctx, constants);
  }

} // namespace fork_hooks

} // namespace dxvk
