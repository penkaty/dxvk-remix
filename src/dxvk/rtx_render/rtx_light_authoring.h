#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../util/util_matrix.h"
#include "../../util/util_vector.h"
#include "../../util/xxHash/xxhash.h"

namespace dxvk {

  // A light attached to a game mesh, authored in the Lamps panel and saved as a mesh replacement light.
  // See documentation/LightAuthoring.md.
  struct AuthoredLamp {
    XXH64_hash_t meshHash = 0;
    XXH64_hash_t textureHash = 0;
    std::string name;
    // Position and emission direction in the mesh's object space.
    Vector3 position { 0.f };
    Vector3 direction { 0.f, -1.f, 0.f };
    Vector3 color { 1.f };
    float intensity = 10000.f;
    float radius = 0.2f;
    // Half angle in degrees. 180 is an unshaped point light.
    float coneAngle = 60.f;
    float coneSoftness = 0.2f;
    float volumetric = 1.f;
    // Matches LightData::AnimationMode.
    uint32_t animationMode = 0;
    float animationRate = 1.f;
    float animationDuty = 0.5f;
    float animationPhase = 0.f;
    Vector3 animationColorB { 0.f };
    // Every mesh drawn with textureHash gets this lamp, placed relative to that mesh's bounds centre.
    bool byTexture = false;
    // Distance the light sits along its beam from the lamp's position, to clear the lamp's own housing.
    float beamOffset = 0.f;
    // The mesh's object to world transform and bounds centre when it was picked in this session. Not saved.
    bool placementKnown = false;
    Matrix4 objectToWorld {};
    bool centerKnown = false;
    Vector3 pickedCenter { 0.f };
  };

  // A library material bound to one game mesh, replacing its material on that mesh only.
  // See documentation/MeshMaterials.md.
  struct MeshMaterial {
    XXH64_hash_t meshHash = 0;
    XXH64_hash_t textureHash = 0;
    std::string material;
    // Every mesh drawn with textureHash gets the material, not only meshHash.
    bool byTexture = false;
  };

  struct LampPreset {
    const char* name;
    Vector3 color;
    float intensity;
    float radius;
    float coneAngle;
    float coneSoftness;
  };

  class LightAuthoring {
  public:
    static const std::vector<LampPreset>& presets();
    static void applyPreset(const LampPreset& preset, AuthoredLamp& lamp);

    // The mod folder name under the mods directory. The mod must exist when the game starts for its lights to load.
    static constexpr const char* kModFolder = "authored_lights";

    std::string modFilePath() const;
    bool modFileExisted() const { return m_modFileExisted; }

    // Reads the mod file once.
    void ensureLoaded();

    std::vector<AuthoredLamp>& lamps() { return m_lamps; }

    // The materials of library.usda next to the mod file, sublayered into it when it exists.
    static constexpr const char* kLibraryFile = "library.usda";
    const std::vector<std::string>& library() const { return m_library; }
    std::vector<MeshMaterial>& meshMaterials() { return m_meshMaterials; }
    // Binds a library material to a mesh, or to every mesh with the texture. An empty name removes the binding.
    void setMeshMaterial(XXH64_hash_t meshHash, XXH64_hash_t textureHash, bool byTexture, const std::string& material);
    // Moves a lamp between its own mesh and every mesh with its texture, keeping it in place on the picked mesh.
    static void setByTexture(AuthoredLamp& lamp, bool byTexture);

    // Returns a lamp for a picked mesh: below the mesh's bounds, aimed down in the world.
    AuthoredLamp makeLamp(XXH64_hash_t meshHash, XXH64_hash_t textureHash, const Matrix4& objectToWorld,
                          const Vector3& boundsMin, const Vector3& boundsMax, bool zUp, const LampPreset& preset);

    // Writes every lamp to the mod file. Remix reloads the changed meshes when the file changes.
    bool save();

    const std::string& status() const { return m_status; }

  private:
    std::string uniqueName() const;

    bool m_loaded = false;
    bool m_modFileExisted = false;
    std::vector<AuthoredLamp> m_lamps;
    std::vector<MeshMaterial> m_meshMaterials;
    std::vector<std::string> m_library;
    std::string m_status;
  };

} // namespace dxvk
