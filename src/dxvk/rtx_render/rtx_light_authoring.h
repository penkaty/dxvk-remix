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
    // The mesh's object to world transform when it was picked in this session. Not saved.
    bool placementKnown = false;
    Matrix4 objectToWorld {};
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

    // Returns a lamp for a picked mesh: below the mesh's bounds, aimed down in the world.
    AuthoredLamp makeLamp(XXH64_hash_t meshHash, XXH64_hash_t textureHash, const Matrix4& objectToWorld,
                          const Vector3& boundsMin, const Vector3& boundsMax, bool zUp, const LampPreset& preset);

    // Writes every lamp to the mod file. Remix reloads the changed meshes when the file changes.
    bool save();

    const std::string& status() const { return m_status; }

  private:
    std::string uniqueName(XXH64_hash_t meshHash) const;

    bool m_loaded = false;
    bool m_modFileExisted = false;
    std::vector<AuthoredLamp> m_lamps;
    std::string m_status;
  };

} // namespace dxvk
