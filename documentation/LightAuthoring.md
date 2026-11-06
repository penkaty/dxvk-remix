# Light authoring (Lamps tab)

The Lamps tab in the Remix menu (Game Setup) attaches lights to game meshes and saves them as a mod. It is meant for
lamps the game only fakes with an emissive texture or a glow sprite: street lamps, floodlights, stadium lights.

## How it works

- **Picking.** "Add a lamp: pick a mesh" arms a pick. The next left click in the scene reads the object picking buffer.
  For a D3D9 draw the scene manager keeps, per picking value, the draw's mesh replacement hash (the geometry asset hash
  rule, the same hash `/RootNode/meshes/mesh_<HASH>` is matched by), its object to world transform and its object-space
  bounds (`SceneManager::findPickedDraw`). API instances have no mesh hash, so they cannot carry a lamp.
- **Placement.** A new lamp sits just outside the mesh's bounds on the side it shines to, so the lamp housing does not
  block it, and is aimed down in the world (`rtx.zUp` decides which axis is up). Position and direction are stored in the
  mesh's object space, so the light moves with the mesh.
- **Saving.** Every lamp is a `SphereLight` child of `/RootNode/meshes/mesh_<HASH>` in
  `rtx-remix/mods/authored_lights/mod.usda`. The mesh prim has no geometry of its own, so the game's mesh is kept
  (`preserveGameObject`). The file is written to a temporary file and renamed over the old one. The mod's file watcher
  then reloads only the changed meshes (`rtx.usd.reloadOnChanged`).
- **New mod.** Mods are found when the game starts. If the folder did not exist then, lamps are saved but load at the
  next start. Shipping an empty `authored_lights/mod.usda` avoids that.
- **Upstream compatibility.** The file is a normal Remix mod: mesh-anchored lights with the standard `inputs:*` light
  attributes and `xformOp:transform`. The Remix Toolkit and upstream runtimes load it. They ignore the `remix:animation:*`
  and `remix:authoring:*` attributes, and the lights are then static.
- **Off-screen lamps.** A mesh-anchored light exists while its mesh is drawn. With `rtx.antiCulling.object.enable` the
  mesh, and with it the light, stays in the scene when the game stops drawing it outside the view.

## Animation

`LightData` reads `remix:animation:mode` (int), `rate` (cycles per second), `duty` (part of a cycle), `phase` (part of a
cycle) and `colorB` (colour) from the light prim. `SceneManager::processReplacementLights` calls
`LightData::applyAnimation` with the global time each frame the mesh is drawn:

| Mode | Value | Behaviour |
|------|-------|-----------|
| Flash | 1 | Colour A for `duty` of each cycle, then colour B. Black B means off. |
| Strobe | 2 | The same as Flash. The panel sets a fast rate and a short duty. |
| Pulse | 3 | A cosine blend from colour A to colour B and back once per cycle. |
| Hue cycle | 4 | Colour A's hue turns once per cycle. |
| Camera flashes | 5 | About half of the cycles hold one 60 ms flash at a random moment, different per light position. Colour B between flashes. |

An animation is applied only while its mesh is drawn, so it pauses while an anti-culled mesh is off screen.

## Presets

Presets set colour, intensity, radius, cone angle and softness (`LightAuthoring::presets`). Intensity is radiance per
unit area of the sphere, so a larger radius at the same intensity gives more light. The values are starting points
for tuning in the scene.

## Mesh materials

The same panel binds a material from `library.usda` to a picked mesh. See [MeshMaterials.md](MeshMaterials.md).
