// src/dxvk/rtx_render/rtx_fork_wetness.cpp
//
// Fork-owned file. Options of the wetness model (rtx.wet.*), constant fill and UI.
// Ported from xoxor4d's dxvk-remix game/gta4 branch (rtx_fork_wetness.cpp, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX: category driven instead of per draw packed bits, scene up axis generic; the UI is a header under
// Game Setup -> Step 2: Parameter Tuning (Alt+X menu).

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
    constants.wetFlags = (patches() ? 1u : 0u) | (puddles() ? 2u : 0u) | (rainShadow() ? 4u : 0u) |
                         (rainShadowDither() ? 8u : 0u) | (raindrops() ? 16u : 0u);

    ForkRainMap::prepare(ctx, constants);
  }

  void WetnessOptions::showImguiSettings() {
    ImGui::TextWrapped("Wetness for the surfaces in rtx.wetTextures: roughness scaling, puddles, rain shadow and raindrops. "
                       "Puddles, rain shadow and raindrops are evaluated on primary hits only; bounces take the base layer alone. "
                       "Everything is off while Global Wetness is 0.");

    RemixGui::SliderFloat("Global Wetness", &globalWetnessObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::Checkbox("Raindrops", &raindropsObject());
    RemixGui::SliderFloat("Rain Intensity", &rainIntensityObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::DragFloat("Raindrop Scale", &raindropScaleObject(), 0.05f, 0.1f, 10.0f, "%.2f");

    RemixGui::Separator();
    ImGui::TextUnformatted("Surfaces");
    RemixGui::SliderFloat("Min Up Normal", &minNormalObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Blend Width", &blendWidthObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Fully Wet Roughness Scale", &fullyWetRoughnessScaleObject(), 0.0f, 2.0f, "%.2f");
    RemixGui::Checkbox("Wet Patches", &patchesObject());
    RemixGui::Checkbox("Puddles", &puddlesObject());
    RemixGui::Checkbox("Rain Shadow", &rainShadowObject());
    RemixGui::Checkbox("Rain Shadow Dither (quality 1)", &rainShadowDitherObject());

    RemixGui::Separator();
    ImGui::TextUnformatted("Quality and Distances");
    RemixGui::DragInt("Puddle Quality (0 off, 1 noise, 2 rain map)", &puddleQualityObject(), 0.05f, 0, 2);
    RemixGui::DragFloat("Rain Map Extent", &mapExtentObject(), 1.0f, 8.0f, 1024.0f, "%.0f");
    RemixGui::DragInt("Rain Map Resolution", &mapResolutionObject(), 4.0f, 64, 2048);
    RemixGui::DragFloat("Rain Map Trace Above Camera", &mapTraceAboveObject(), 1.0f, 1.0f, 1000.0f, "%.0f");
    RemixGui::DragFloat("Rain Map Trace Below Camera", &mapTraceBelowObject(), 1.0f, 1.0f, 1000.0f, "%.0f");
    RemixGui::DragFloat("Rain Shadow Min Height", &occlusionMinHeightObject(), 0.1f, 0.0f, 50.0f, "%.1f");
    RemixGui::DragFloat("Rain Shadow Feather", &occlusionFeatherObject(), 0.05f, 0.0f, 10.0f, "%.2f");
    RemixGui::DragFloat("Rain Shadow Ray Length (quality 1)", &occlusionRayLengthObject(), 1.0f, 1.0f, 1000.0f, "%.0f");
    RemixGui::DragFloat("Rain Shadow Ray Distance (quality 1)", &occlusionDistanceObject(), 1.0f, 0.0f, 2000.0f, "%.0f");
    RemixGui::DragFloat("Puddle Distance", &detailDistanceObject(), 1.0f, 0.0f, 5000.0f, "%.0f");
    RemixGui::DragFloat("Raindrop Distance", &raindropDistanceObject(), 0.5f, 0.0f, 500.0f, "%.1f");

    RemixGui::Separator();
    ImGui::TextUnformatted("Wet Surface");
    RemixGui::SliderFloat("Roughness Floor", &minRoughnessObject(), 0.0f, 1.0f, "%.3f");
    RemixGui::SliderFloat("Albedo Darkening", &albedoDarkeningObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Saturation Boost", &saturationBoostObject(), 0.0f, 2.0f, "%.2f");

    RemixGui::Separator();
    ImGui::TextUnformatted("Puddles");
    RemixGui::SliderFloat("Coverage", &puddleCoverageObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::DragFloat("Ground Unevenness", &microAmplitudeObject(), 0.001f, 0.001f, 1.0f, "%.3f");
    RemixGui::DragFloat("Unevenness Cell Size", &noiseScaleObject(), 0.01f, 0.1f, 100.0f, "%.2f");
    RemixGui::SliderFloat("Depression Weight", &depressionWeightObject(), 0.0f, 5.0f, "%.2f");
    RemixGui::SliderFloat("Slope Limit", &slopeLimitObject(), 0.005f, 1.0f, "%.3f");
    RemixGui::SliderFloat("Slope Feather", &slopeFeatherObject(), 0.1f, 4.0f, "%.2f");
    RemixGui::DragFloat("Edge Depth", &edgeWidthObject(), 0.0005f, 0.0001f, 0.1f, "%.4f");
    RemixGui::DragFloat("Shore Height", &shoreWidthObject(), 0.001f, 0.0f, 0.5f, "%.3f");
    RemixGui::SliderFloat("Rain Shadow Weight", &puddleOcclusionObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Water Roughness", &puddleRoughnessObject(), 0.0f, 1.0f, "%.3f");
    RemixGui::SliderFloat("Water Darkening", &puddleDarkeningObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Shore Darkening", &shoreDarkeningObject(), 0.0f, 1.0f, "%.2f");

    RemixGui::Separator();
    ImGui::TextUnformatted("Ripples and Rain");
    RemixGui::SliderFloat("Ripple Strength", &rippleStrengthObject(), 0.0f, 1.0f, "%.3f");
    RemixGui::DragFloat("Ripple Wavelength (cm)", &rippleWavelengthObject(), 0.1f, 1.0f, 200.0f, "%.1f");
    RemixGui::SliderFloat("Ripple Speed", &rippleSpeedObject(), 0.0f, 10.0f, "%.2f");
    RemixGui::SliderFloat("Ripple Gusts", &rippleVariationObject(), 0.0f, 1.0f, "%.2f");
    RemixGui::SliderFloat("Raindrops on Wet Ground", &raindropGroundScaleObject(), 0.0f, 1.0f, "%.2f");
  }

namespace fork_hooks {

  void fillWetnessShaderParams(RtxContext& ctx, RaytraceArgs& constants) {
    WetnessOptions::fillShaderParams(ctx, constants);
  }

  void showWetnessSettings() {
    WetnessOptions::showImguiSettings();
  }

} // namespace fork_hooks

} // namespace dxvk
