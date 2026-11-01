// The fork_hooks functions of the SceneManager::submitExternalDraw path (see rtx_fork_hooks.h).

#include "rtx_fork_hooks.h"

#include "rtx_asset_replacer.h"   // AssetReplacer, AssetReplacement
#include "rtx_options.h"          // RtxOptions::*, fast_unordered_set, InstanceCategories
#include "rtx_scene_manager.h"    // SceneManager
#include "rtx_scene_interface.h"  // scene::Options::assetKeys

#include <mutex>
#include <unordered_map>

namespace dxvk {
namespace fork_hooks {

  // Returns the USD mesh or light replacement bucket keyed on the API mesh handle hash, as the D3D9 draw path does.
  // The caller then calls drawReplacements and returns.
  // Returns null when there is no replacement.
  std::shared_ptr<const ReplacementBucket> externalDrawMeshReplacement(
      AssetReplacer& replacer, XXH64_hash_t meshHash) {
    // rtx.scene.assetKeys: a host key (then its aliases) replaces the handle as the lookup key.
    if (!replacer.assetResolver().empty() && scene::Options::assetKeys()) {
      if (const AssetResolver::AssetKey* key = replacer.assetResolver().meshKey(meshHash)) {
        return AssetResolver::resolve(*key, [&](uint64_t k) { return replacer.getReplacementsForMesh(k); });
      }
    }
    return replacer.getReplacementsForMesh(meshHash);
  }

  XXH64_hash_t externalDrawGameTextureHash(const MaterialData& material) {
    if (material.getType() != MaterialDataType::Opaque) {
      return kEmptyHash;
    }
    return gameTextureHash(material.getOpaqueMaterialData().getAlbedoOpacityTexture());
  }

  XXH64_hash_t gameTextureHash(const TextureRef& albedo) {
    const DxvkImageView* view = albedo.isValid() ? albedo.getImageView() : nullptr;
    if (view == nullptr) {
      return kEmptyHash;
    }
    const XXH64_hash_t imageHash = view->image()->getHash();
    return imageHash != albedo.getImageHash() ? imageHash : kEmptyHash;
  }

  // A USD replacement fills only the inputs it sets; the rest comes from the draw's own material, as the D3D9 path's
  // determineMaterialData merges the legacy material in. Without it a replacement that brings only normal/roughness/
  // height (the albedo stays the game's texture) renders the API draw with the default albedo: flat grey ground.
  // Merged copies are cached per (replacement, own material); a hot reload hands out new replacements, so the cache is
  // dropped when it grows past what one stage needs.
  std::shared_ptr<MaterialData> mergedReplacement(
      const std::shared_ptr<MaterialData>& replacement, const MaterialData& own) {
    if (replacement->getType() != MaterialDataType::Opaque || own.getType() != MaterialDataType::Opaque) {
      return replacement;
    }
    static std::mutex s_mutex;
    static std::unordered_map<XXH64_hash_t, std::pair<std::weak_ptr<MaterialData>, std::shared_ptr<MaterialData>>> s_merged;
    const XXH64_hash_t ownHash = own.getHash();
    const XXH64_hash_t key = XXH64(&ownHash, sizeof(ownHash), replacement->getHash());
    std::lock_guard<std::mutex> lock(s_mutex);
    auto it = s_merged.find(key);
    if (it != s_merged.end() && it->second.first.lock() == replacement) {
      return it->second.second;
    }
    if (s_merged.size() > 8192) {
      s_merged.clear();
    }
    auto merged = std::make_shared<MaterialData>(*replacement);
    merged->getOpaqueMaterialData().merge(own.getOpaqueMaterialData());
    s_merged[key] = { replacement, merged };
    return merged;
  }

  // Points the caller's material at its USD replacement (getReplacementMaterial) if there is one.
  // The material's own hash (or its host asset key) is looked up first, then its game texture hash.
  std::shared_ptr<MaterialData> externalDrawMaterialReplacement(
      AssetReplacer& replacer, const MaterialData*& material) {
    // Replacement materials are handed out by shared_ptr, so a hot reload cannot free one under a live draw.
    // The owner goes back to the caller, so `material` stays valid for the rest of the submesh iteration.
    std::shared_ptr<MaterialData> pReplacementMaterial;
    const AssetResolver::AssetKey* key = nullptr;
    if (!replacer.assetResolver().empty() && scene::Options::assetKeys()) {
      key = replacer.assetResolver().materialKey(material->getHash());
    }
    if (key != nullptr) {
      pReplacementMaterial = AssetResolver::resolve(*key, [&](uint64_t k) { return replacer.getReplacementMaterial(k); });
    } else {
      pReplacementMaterial = replacer.getReplacementMaterial(material->getHash());
    }
    if (pReplacementMaterial == nullptr && replacer.hasAnyMods()) {
      const XXH64_hash_t gameTextureHash = externalDrawGameTextureHash(*material);
      if (gameTextureHash != kEmptyHash) {
        pReplacementMaterial = replacer.getReplacementMaterial(gameTextureHash);
      }
    }
    if (pReplacementMaterial != nullptr) {
      pReplacementMaterial = mergedReplacement(pReplacementMaterial, *material);
      material = pReplacementMaterial.get();
    }
    return pReplacementMaterial;
  }

  // Applies the texture-based instance categories to an API draw. The albedo texture hash from the material's
  // opaque data goes to outTextureHash (object picking uses it too) and is looked up in every category set.
  //
  // An albedo imported from the game's D3D9 device (remixapi_ImportD3D9Texture) is registered under a hash the host
  // chose, but its image is the game's own and still carries the D3D9 image hash that the D3D9 draw path, rtx.conf
  // texture lists and the texture picker know it by. That hash is reported and matched. The API hash is matched as
  // well, so a list naming it keeps working.
  void externalDrawTextureCategories(
      const MaterialData* material,
      DrawCallState& drawCall,
      XXH64_hash_t& outTextureHash) {
    // The albedo texture hash is what the D3D9 path's setupCategoriesForTexture() keys on.
    XXH64_hash_t apiTextureHash = kEmptyHash;
    if (material->getType() == MaterialDataType::Opaque) {
      const TextureRef& albedo = material->getOpaqueMaterialData().getAlbedoOpacityTexture();
      if (albedo.isValid()) {
        apiTextureHash = albedo.getImageHash();
        outTextureHash = apiTextureHash;
        if (const DxvkImageView* view = albedo.getImageView()) {
          const XXH64_hash_t imageHash = view->image()->getHash();
          if (imageHash != kEmptyHash) {
            outTextureHash = imageHash;
          }
        }
      }
    }

    if (outTextureHash != 0 && outTextureHash != kEmptyHash) {
      // The category lists read here are tagged as drawcall-translation inputs, so a change to any
      // of them (texture tagging in the Remix menu) sends preserved / retained API draws through a
      // full translation next frame, which re-runs this.
      RTX_OPTION_INVALIDATION_SCOPE(RtxOptionFlags::InvalidatesDrawcallTranslation);
      const bool matchApiHash = apiTextureHash != kEmptyHash && apiTextureHash != outTextureHash;
      auto listed = [&](const fast_unordered_set& hashSet) {
        return hashSet.find(outTextureHash) != hashSet.end() ||
          (matchApiHash && hashSet.find(apiTextureHash) != hashSet.end());
      };
      auto applyCategory = [&](const fast_unordered_set& hashSet, InstanceCategories cat) {
        if (listed(hashSet)) {
          drawCall.setCategory(cat, true);
        }
      };

      applyCategory(RtxOptions::skyBoxTextures(), InstanceCategories::Sky);
      // A D3D9 draw of an ignored texture is never handed to the ray tracer; an API instance has
      // no such early out, so it is kept out of the scene instead.
      if (listed(RtxOptions::ignoreTextures())) {
        drawCall.setCategory(InstanceCategories::Ignore, true);
        drawCall.setCategory(InstanceCategories::Hidden, true);
      }
      applyCategory(RtxOptions::worldSpaceUiTextures(), InstanceCategories::WorldUI);
      applyCategory(RtxOptions::worldSpaceUiBackgroundTextures(), InstanceCategories::WorldMatte);
      applyCategory(RtxOptions::particleTextures(), InstanceCategories::Particle);
      applyCategory(RtxOptions::beamTextures(), InstanceCategories::Beam);
      applyCategory(RtxOptions::cardNormalsTextures(), InstanceCategories::CardNormals);
      applyCategory(RtxOptions::wetTextures(), InstanceCategories::Wet);
      applyCategory(RtxOptions::decalTextures(), InstanceCategories::DecalStatic);
      applyCategory(RtxOptions::terrainTextures(), InstanceCategories::Terrain);
      applyCategory(RtxOptions::animatedWaterTextures(), InstanceCategories::AnimatedWater);
      applyCategory(RtxOptions::ignoreLights(), InstanceCategories::IgnoreLights);
      applyCategory(RtxOptions::antiCullingTextures(), InstanceCategories::IgnoreAntiCulling);
      applyCategory(RtxOptions::motionBlurMaskOutTextures(), InstanceCategories::IgnoreMotionBlur);
      applyCategory(RtxOptions::hideInstanceTextures(), InstanceCategories::Hidden);
    }
  }

} // namespace fork_hooks
} // namespace dxvk
