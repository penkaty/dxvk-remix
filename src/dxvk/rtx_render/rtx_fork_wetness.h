#pragma once

// rtx_fork_wetness.h - fork-owned. Options of the wetness model (rtx.wet.*): quality and distances,
// the rain map, the wet surface, puddles, ripples and raindrops.
//
// Ported from xoxor4d's dxvk-remix game/gta4 branch (rtx_fork_wetness.h, bde0b307; zlib/MIT), credit to xoxor4d.
// RBR-RTX changes: the per draw channel (render state 210 / packed bits) is replaced by the instance category
// rtx.wetTextures plus the frame options below (blendWidth, minNormal, ...); the model is generalised to the
// scene's up axis (rtx.zUp: RBR is Y-up, his GTA IV is Z-up); everything is off while globalWetness is 0 (default).
// The shader side lives in shaders/rtx/concept/surface_material/rtx_fork_wetness.slangh,
// the rain map in rtx_fork_rain_map.cpp. Units are world units (RBR: metres).

#include "rtx_option.h"

struct RaytraceArgs;

namespace dxvk {

  class RtxContext;

  class WetnessOptions {
  public:
    // Frame wetness: the one switch. 0 = the whole feature costs one uniform branch per hit.
    RTX_OPTION_ARGS("rtx.wet", float, globalWetness, 0.0f,
                    "Wetness of the frame, 0 dry to 1 wet, for the surfaces listed in rtx.wetTextures. The game (our proxy) sets it per "
                    "frame through SetConfigVariable. 0 turns the whole feature off.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    // Quality and distances
    RTX_OPTION_ARGS("rtx.wet", int, puddleQuality, 2,
                    "0: no puddles and no rain shadow. 1: puddles from noise, rain shadow by a ray per pixel. "
                    "2: puddles pool in the depressions of a traced height field around the camera (rain map), rain shadow "
                    "from the same map; one texture fetch per pixel instead of a ray.",
                    args.minValue = 0, args.maxValue = 2);
    RTX_OPTION_ARGS("rtx.wet", float, mapExtent, 128.0f,
                    "Side length in world units of the rain map window around the camera.",
                    args.minValue = 8.0f, args.maxValue = 1024.0f);
    RTX_OPTION_ARGS("rtx.wet", int, mapResolution, 512,
                    "Texels per side of the rain map. 512 at 128 units gives 0.25 unit texels, enough to resolve a curb.",
                    args.minValue = 64, args.maxValue = 2048);
    RTX_OPTION_ARGS("rtx.wet", float, mapTraceAbove, 100.0f,
                    "Rain map rays start this far above the camera (along the scene up axis).",
                    args.minValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, mapTraceBelow, 60.0f,
                    "Rain map rays end this far below the camera.",
                    args.minValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, occlusionRayLength, 80.0f,
                    "Quality 1 and beyond the map: length in world units of the upward ray that checks for a roof above a "
                    "wet surface (rain shadow). Anything higher counts as open sky.",
                    args.minValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, occlusionMinHeight, 2.5f,
                    "Objects closer than this above the ground (world units) cast no rain shadow: cars, benches, railings, "
                    "people. Balconies, bridges and roofs above it do.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.wet", float, occlusionFeather, 1.5f,
                    "Quality 2: radius in world units of the tent blur applied to the rain cover in the map, so cover edges are "
                    "a smooth gradient. Capped at 12 map texels; the blur runs once per frame in the map pass.",
                    args.minValue = 0.0f, args.maxValue = 10.0f);
    RTX_OPTION_ARGS("rtx.wet", float, occlusionDistance, 120.0f,
                    "Quality 1 and beyond the map: camera distance up to which rain shadow rays are traced; the result fades "
                    "to exposed over the second half of the range.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.wet", float, detailDistance, 300.0f,
                    "Camera distance in world units up to which puddles and wetness variation are evaluated.",
                    args.minValue = 0.0f);
    RTX_OPTION_ARGS("rtx.wet", float, raindropDistance, 35.0f,
                    "Camera distance in world units up to which raindrop impacts are evaluated.",
                    args.minValue = 0.0f);

    // Which surfaces are wet, and where (replaces xoxor4d's per draw packed value)
    RTX_OPTION_ARGS("rtx.wet", float, blendWidth, 0.1f,
                    "Width of the transition at minNormal: how softly a surface turns from wet (flat) to dry (steep).",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, minNormal, 0.5f,
                    "Component of the surface normal along the up axis above which a surface can be wet. Vertical walls (below 0.1) "
                    "never get wet. Puddles need ground that is nearly flat (0.95).",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, fullyWetRoughnessScale, 0.0f,
                    "Roughness multiplier of a fully wet surface (before the roughness floor): 0 = as glossy as the floor allows, "
                    "1 = no change of roughness.",
                    args.minValue = 0.0f, args.maxValue = 2.0f);
    RTX_OPTION_ARGS("rtx.wet", bool, patches, true,
                    "Wet and less wet patches with streaks (wetness variation) while the ground is not fully wet.");
    RTX_OPTION_ARGS("rtx.wet", bool, puddles, true,
                    "Standing water on flat, static ground (needs puddleQuality > 0).");
    RTX_OPTION_ARGS("rtx.wet", bool, rainShadow, true,
                    "Surfaces under a roof, bridge or canopy get no puddles and no raindrops.");
    RTX_OPTION_ARGS("rtx.wet", bool, rainShadowDither, false,
                    "Quality 1: dithered rain shadow sample position (needs DLSS-RR to hide the grain).");

    // Wet surface
    RTX_OPTION_ARGS("rtx.wet", float, minRoughness, 0.05f,
                    "Roughness floor of wet surfaces outside puddles. Keeps wet ground glossy instead of mirror-like.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, albedoDarkening, 0.5f,
                    "How much a fully wet, porous (rough, dark) surface darkens. Smooth materials such as paint darken little.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, saturationBoost, 0.18f,
                    "Extra color saturation of fully wet surfaces.",
                    args.minValue = 0.0f, args.maxValue = 2.0f);

    // Puddles (world units)
    RTX_OPTION_ARGS("rtx.wet", float, puddleCoverage, 0.64f,
                    "Share of flat, featureless ground under water when fully wet. The water level rises with the wetness, "
                    "so puddles grow from the low spots outward and dry from the edges. Gutters and dips flood first.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, microAmplitude, 0.03f,
                    "Height range in world units of the small scale unevenness that puddles form in on flat ground.",
                    args.minValue = 0.001f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, noiseScale, 1.0f,
                    "Size in world units of one cell of that unevenness.",
                    args.minValue = 0.1f, args.maxValue = 100.0f);
    RTX_OPTION_ARGS("rtx.wet", float, depressionWeight, 0.3f,
                    "Quality 2: how much the depressions of the rain map (gutters, dips) lower the ground for the water "
                    "level. 1 is their measured depth.",
                    args.minValue = 0.0f, args.maxValue = 5.0f);
    RTX_OPTION_ARGS("rtx.wet", float, slopeLimit, 0.053f,
                    "Quality 2: ground grade above which water runs off and no puddle forms (0.05 = 5 percent).",
                    args.minValue = 0.005f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, slopeFeather, 2.4f,
                    "Quality 2: width of the slope fade as a multiple of slopeLimit. Puddles fade out between slopeLimit and "
                    "slopeLimit * (1 + slopeFeather).",
                    args.minValue = 0.1f, args.maxValue = 4.0f);
    RTX_OPTION_ARGS("rtx.wet", float, edgeWidth, 0.025f,
                    "Water depth in world units over which the puddle edge fades in.",
                    args.minValue = 0.0001f, args.maxValue = 0.1f);
    RTX_OPTION_ARGS("rtx.wet", float, shoreWidth, 0.02f,
                    "Height in world units above the water line that is damp ground (the darker shore band).",
                    args.minValue = 0.0f, args.maxValue = 0.5f);
    RTX_OPTION_ARGS("rtx.wet", float, puddleOcclusion, 0.5f,
                    "How much the rain shadow suppresses puddles. 1 removes them under any cover, 0 lets them form under trees "
                    "and awnings as if the rain reached them.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, puddleRoughness, 0.02f,
                    "Roughness of standing water.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, puddleDarkening, 0.25f,
                    "Albedo multiplier of the ground under a puddle, reached at 2 cm of depth.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, shoreDarkening, 0.8f,
                    "Albedo multiplier of the shore band.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    // Ripples and rain
    RTX_OPTION_ARGS("rtx.wet", float, rippleStrength, 0.08f,
                    "Normal perturbation of the wind ripples on standing water. 0 disables them.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, rippleWavelength, 12.0f,
                    "Wavelength of the shortest wind ripple in centimeters; five longer waves follow, each 1.37 times longer. "
                    "Waves shorter than a few pixels at the hit distance fade out.",
                    args.minValue = 1.0f, args.maxValue = 200.0f);
    RTX_OPTION_ARGS("rtx.wet", float, rippleVariation, 0.48f,
                    "How much the ripples come and go in gusts (0 uniform, 1 patches of still water between gusts).",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, rippleSpeed, 2.5f,
                    "Ripple animation speed.",
                    args.minValue = 0.0f, args.maxValue = 10.0f);
    RTX_OPTION_ARGS("rtx.wet", bool, raindrops, false,
                    "Raindrop impact rings on wet ground, strongest on puddles. Turn on while it rains (the proxy sets it with rainIntensity).");
    RTX_OPTION_ARGS("rtx.wet", float, rainIntensity, 1.0f,
                    "Rain strength 0..1: density and visibility of the raindrop rings (only with rtx.wet.raindrops).",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.wet", float, raindropScale, 3.0f,
                    "Size of the raindrop rings (the grid cell size is inverse to it).",
                    args.minValue = 0.1f, args.maxValue = 10.0f);
    RTX_OPTION_ARGS("rtx.wet", float, raindropGroundScale, 0.5f,
                    "Strength of raindrop impacts on wet ground outside puddles, relative to inside puddles.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    static void fillShaderParams(RtxContext& ctx, RaytraceArgs& constants);
  };

} // namespace dxvk
