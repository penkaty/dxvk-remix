# Mesh materials

A material bound to a mesh replacement that keeps the original draw replaces the material of that draw. It does not affect other meshes that share the texture.

## Why

Remix replaces a game draw's material by its texture hash. Games share one texture across very different meshes, such as a ground texture on a road and on a wall, so a texture-hash material cannot fit both. A mesh-hash binding gives each mesh its own material.

## USD

```usda
def Xform "RootNode"
{
    def Scope "Looks"
    {
        def Material "rock" { ... }
    }
    def Scope "meshes"
    {
        def Xform "mesh_0123456789ABCDEF" (
            prepend apiSchemas = ["MaterialBindingAPI"]
        )
        {
            rel material:binding = </RootNode/Looks/rock>
        }
    }
}
```

- The binding may sit on the `mesh_<hash>` prim or on its `mesh` child.
- The mesh prim must keep the original draw (`preserveOriginalDrawCall`, or no reference that replaces it). Lights and other children work as usual.
- The game's colour texture and sampler are merged into the bound material, the same way a texture-hash replacement material gets them. The material's own albedo texture, if it has one, wins.
- A mesh binding is stronger than a texture-hash replacement material for the same draw.

## How it works

`UsdMod::Impl::processReplacement` resolves the binding of the preserved original and stores it on that `AssetReplacement` (`materialData`, `materialPathHash`). `SceneManager::drawReplacements` copies it, merges the draw's legacy material into the copy, and uses the copy for the original draw only. Other replacements in the bucket keep their own materials.

## Authoring

The Lamps panel (Developer menu > Game Setup > Lamps, see [LightAuthoring.md](LightAuthoring.md)) lists the materials of `library.usda` in the lamp mod's folder. Pick a mesh, choose a material, and apply it. The panel writes the binding into the lamp mod and adds `library.usda` as a sublayer.

## Texture anchors

`/RootNode/textureMeshes/tex_<texture hash>` holds a replacement for every draw of that texture that has no mesh replacement of its own. It takes the same children as a `mesh_` prim: lights, a `material:binding`, and so on. It is stored in the mesh table under `AssetReplacer::textureAnchorKey(hash)`, and the scene manager looks it up after the mesh hash and the legacy hashes miss.

With `bool remix:anchor:boundsCenter = 1` on the `tex_` prim, its lights are placed relative to each draw's bounding box centre. Games that store static meshes in world space need this, or every copy of a lamp would sit at the same point.

A change anywhere under `/RootNode/textureMeshes` reprocesses all meshes of the mod on hot reload, because these prims are not keyed by their names.

The Lamps panel writes texture-anchored lamps and materials when "Every mesh with the picked texture" is chosen. Its beam offset moves a light along its beam (`remix:authoring:beamOffset` records it, and the transform holds the moved position).
