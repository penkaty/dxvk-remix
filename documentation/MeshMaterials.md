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
