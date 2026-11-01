# RBR-RTX wetness lite: fork touchpoints

Branch `feat/20261101-wetness` (on `feat/20261101-kim-cardnormals`). Wetness, puddles, rain map and raindrops after
xoxor4d's dxvk-remix `game/gta4` branch (`rtx_fork_wetness`, `rtx_fork_rain_map`, bde0b307; zlib/MIT). Credit to xoxor4d;
every ported file carries a header comment. The per draw channel (render state 210) is NOT ported: surfaces are selected
by the instance category `rtx.wetTextures` (like CardNormals), the rest are frame options `rtx.wet.*`.

## New files
| File | What |
| :-- | :-- |
| `src/dxvk/rtx_render/rtx_fork_wetness.{h,cpp}` | `rtx.wet.*` options, `fillShaderParams` (RaytraceArgs), ImGui body |
| `src/dxvk/rtx_render/rtx_fork_rain_map.{h,cpp}` | rain map resources, window (plane perpendicular to the up axis), two compute dispatches, placeholder view |
| `src/dxvk/shaders/rtx/concept/surface_material/rtx_fork_wetness.slangh` | the shader model (`forkApplyWetness`) |
| `src/dxvk/shaders/rtx/pass/fork_rain_map/rain_map_{trace,relief}.comp.slang`, `rain_map_bindings.slangh`, `rain_map_binding_indices.h` | rain map passes |

## Touched files (one to a few lines each)
| File | Change |
| :-- | :-- |
| `rtx_render/rtx_types.h` | `InstanceCategories::Wet` (count 26 -> 27) |
| `rtx_render/rtx_types.cpp` | `setIf(Wet, rtx.wetTextures)` |
| `rtx_render/rtx_fork_submit.cpp` | `applyCategory(wetTextures, Wet)` for API instances |
| `rtx_render/rtx_options.h` | option `rtx.wetTextures` |
| `rtx_render/rtx_instance_manager.cpp` | `surface.useWet` from the category |
| `rtx_render/rtx_materials.h` | `useWet` field, GPU surface flags0 bit 3 |
| `shaders/rtx/concept/surface/surface.h` | `useWet` property (data0b.z bit 3) |
| `rtx_render/rtx_remix_api.cpp` | `static_assert(Count == 27)` |
| `lssusd/remix_category_names.h` | `remix_category:wet` |
| `imgui/dxvk_imgui.cpp` | texture list entry "Wet Surface Textures"; header "Wetness" under Game Setup -> Step 2 |
| `rtx_render/rtx_fork_hooks.h` | `fillWetnessShaderParams`, `dispatchRainMap`, `rainMapView`, `showWetnessSettings` (+ `struct RaytraceArgs;`) |
| `rtx_render/rtx_context.cpp` | `fillWetnessShaderParams` in `updateRaytraceArgsConstantBuffer`; `dispatchRainMap` before `dispatchPathTracing`; `BINDING_FORK_RAIN_MAP` in `bindCommonRayTracingResources` |
| `rtx_render/rtx_debug_view.cpp`, `shaders/rtx/utility/debug_view_indices.h` | debug views 890 (rain map), 891 (puddles) |
| `shaders/rtx/pass/raytrace_args.h` | 41 `wet*` fields at the end of `RaytraceArgs` |
| `shaders/rtx/pass/common_binding_indices.h`, `common_bindings.slangh` | binding 220 `ForkRainMap` in the common set |
| `shaders/rtx/concept/surface_material/opaque_surface_material_interaction.slangh` | include + one guarded call before "Transform to desired ... values" |
| `dxvk/meson.build` | 4 source rows |

## Differences from xoxor4d's
- No per draw RS210 / `packedWetness` channel, no `d3dData0/1` in the material or its hash. Wet = category membership.
- His per draw bits became frame options: `blendWidth`, `minNormal`, `fullyWetRoughnessScale`, `patches`, `puddles`, `rainShadow`,
  `rainShadowDither`, `raindrops`, `rainIntensity`, `raindropScale` (his `halfDensity` is a continuous `rainIntensity`).
- Up axis generic via `rtx.zUp` (cb.isZUp): up vector, plane (xy or xz), rain map window, trace direction, ripple and raindrop
  normal offsets, "vertical walls" test. No separate `rtx.wet.upAxis`. RBR needs `rtx.zUp = False` (the default).
- Object space raindrops of vehicles and peds (his flag 3, `forkWetnessSurfaceRaindrops`) are not ported (need a per draw channel).
- Rain map sampled by hand-made bilinear `Load` (no sampler bound; his used the sky view sampler); invalid texels left out of the blend.
- Dry end: base-layer roughness extra reduction scaled by wetness (continuous to dry); `wetness = globalWetness` (decoupled from the roughness scalar).
- Rain map pass only runs with `puddleQuality >= 2` AND `globalWetness > 0`; resources are heap-allocated and never freed (static destructors vs device).
- Not added as a shader variant axis: the code is in the shared opaque interaction (all variants), gated by `surface.useWet && globalWetness > 0` (uniform branch).
