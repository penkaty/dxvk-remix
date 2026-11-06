#include "rtx_light_authoring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../../util/log/log.h"
#include "../../util/util_filesys.h"
#include "../../util/util_string.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include "../../lssusd/usd_include_end.h"

namespace dxvk {

  namespace {
    constexpr const char* kMeshPrefix = "mesh_";
    constexpr const char* kTexturePrefix = "tex_";
    constexpr const char* kLampPrefix = "authoredLight_";

    std::string hex16(XXH64_hash_t h) {
      char buf[17];
      std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(h));
      return buf;
    }

    std::string num(float v) {
      std::ostringstream o;
      o.precision(7);
      o << v;
      return o.str();
    }

    std::string vec(const Vector3& v) {
      return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ")";
    }

    // Rows x, y, z and translation. The light emits along -z (USD convention), so z = -direction.
    std::string lampMatrix(const AuthoredLamp& lamp) {
      const Vector3 z = normalize(lamp.direction) * -1.f;
    const Vector3 emitAt = lamp.position + normalize(lamp.direction) * lamp.beamOffset;
      const Vector3 helper = std::fabs(z.y) < 0.99f ? Vector3(0.f, 1.f, 0.f) : Vector3(1.f, 0.f, 0.f);
      const Vector3 x = normalize(cross(helper, z));
      const Vector3 y = cross(z, x);
      auto row = [](const Vector3& v, float w) {
        return "(" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + ", " + num(w) + ")";
      };
      return "( " + row(x, 0.f) + ", " + row(y, 0.f) + ", " + row(z, 0.f) + ", " + row(emitAt, 1.f) + " )";
    }

    template <class T>
    void readAttr(const pxr::UsdPrim& prim, const char* name, T& out) {
      const pxr::UsdAttribute a = prim.GetAttribute(pxr::TfToken(name));
      if (a) {
        a.Get(&out);
      }
    }

    void readVec(const pxr::UsdPrim& prim, const char* name, Vector3& out) {
      pxr::GfVec3f v;
      const pxr::UsdAttribute a = prim.GetAttribute(pxr::TfToken(name));
      if (a && a.Get(&v)) {
        out = Vector3(v[0], v[1], v[2]);
      }
    }
  }

  const std::vector<LampPreset>& LightAuthoring::presets() {
    // Sodium lamps are orange, LED and metal halide lamps white. Radiance is per unit area of the sphere, so a bigger
    // lamp at the same radiance gives more light.
    static const std::vector<LampPreset> kPresets = {
      { "Streetlamp",               Vector3(1.0f, 0.62f, 0.25f), 8000.f,   0.15f, 70.f, 0.30f },
      { "Strong streetlamp",        Vector3(1.0f, 0.62f, 0.25f), 20000.f,  0.20f, 70.f, 0.30f },
      { "Modern streetlamp",        Vector3(1.0f, 0.95f, 0.88f), 10000.f,  0.20f, 60.f, 0.25f },
      { "Modern strong streetlamp", Vector3(0.95f, 0.97f, 1.0f), 25000.f,  0.25f, 60.f, 0.25f },
      { "Floodlight",               Vector3(1.0f, 0.96f, 0.90f), 40000.f,  0.30f, 35.f, 0.15f },
      { "Stadium light",            Vector3(0.95f, 0.97f, 1.0f), 150000.f, 0.60f, 40.f, 0.15f },
      { "Coloured spot",            Vector3(1.0f, 0.2f, 0.1f),   20000.f,  0.15f, 25.f, 0.10f },
      { "Point light",              Vector3(1.0f, 0.9f, 0.8f),   5000.f,   0.10f, 180.f, 0.0f },
    };
    return kPresets;
  }

  void LightAuthoring::applyPreset(const LampPreset& preset, AuthoredLamp& lamp) {
    lamp.color = preset.color;
    lamp.intensity = preset.intensity;
    lamp.radius = preset.radius;
    lamp.coneAngle = preset.coneAngle;
    lamp.coneSoftness = preset.coneSoftness;
  }

  std::string LightAuthoring::modFilePath() const {
    const std::filesystem::path dir = util::RtxFileSys::path(util::RtxFileSys::Mods) / kModFolder;
    return (dir / "mod.usda").string();
  }

  void LightAuthoring::ensureLoaded() {
    if (m_loaded) {
      return;
    }
    m_loaded = true;
    const std::string path = modFilePath();

    const std::string libraryPath = (std::filesystem::path(path).parent_path() / kLibraryFile).string();
    if (std::filesystem::exists(libraryPath)) {
      if (pxr::UsdStageRefPtr library = pxr::UsdStage::Open(libraryPath, pxr::UsdStage::LoadAll)) {
        if (const pxr::UsdPrim looks = library->GetPrimAtPath(pxr::SdfPath("/RootNode/Looks"))) {
          for (const pxr::UsdPrim& material : looks.GetChildren()) {
            if (material.GetTypeName() == pxr::TfToken("Material")) {
              m_library.push_back(material.GetName().GetString());
            }
          }
        }
      }
    }

    m_modFileExisted = std::filesystem::exists(path);
    if (!m_modFileExisted) {
      m_status = "No lamps yet. The first save creates " + path + "; its lights load at the next game start.";
      return;
    }
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(path, pxr::UsdStage::LoadAll);
    if (!stage) {
      m_status = "Could not open " + path;
      Logger::err("[LightAuthoring] " + m_status);
      return;
    }
    for (const bool byTexture : { false, true }) {
    const pxr::UsdPrim meshes = stage->GetPrimAtPath(pxr::SdfPath(byTexture ? "/RootNode/textureMeshes" : "/RootNode/meshes"));
    if (!meshes) {
      continue;
    }
    const char* const prefix = byTexture ? kTexturePrefix : kMeshPrefix;
    for (const pxr::UsdPrim& mesh : meshes.GetChildren()) {
      const std::string meshName = mesh.GetName().GetString();
      if (meshName.rfind(prefix, 0) != 0) {
        continue;
      }
      // A mesh_ prim is keyed by its mesh, a tex_ prim by its texture. Each records the other as authoring data.
      const XXH64_hash_t key = std::strtoull(meshName.c_str() + std::strlen(prefix), nullptr, 16);
      std::string other;
      readAttr(mesh, byTexture ? "remix:authoring:meshHash" : "remix:authoring:textureHash", other);
      const XXH64_hash_t otherHash = std::strtoull(other.c_str(), nullptr, 16);
      const XXH64_hash_t meshHash = byTexture ? otherHash : key;
      const XXH64_hash_t textureHash = byTexture ? key : otherHash;
      pxr::SdfPathVector targets;
      if (const pxr::UsdRelationship binding = mesh.GetRelationship(pxr::TfToken("material:binding")); binding && binding.GetTargets(&targets) && !targets.empty()) {
        MeshMaterial assigned;
        assigned.meshHash = meshHash;
        assigned.textureHash = textureHash;
        assigned.material = targets.front().GetName();
        assigned.byTexture = byTexture;
        m_meshMaterials.push_back(assigned);
      }
      for (const pxr::UsdPrim& prim : mesh.GetChildren()) {
        if (!prim.IsA<pxr::UsdLuxSphereLight>() || prim.GetName().GetString().rfind(kLampPrefix, 0) != 0) {
          continue;
        }
        AuthoredLamp lamp;
        lamp.meshHash = meshHash;
        lamp.textureHash = textureHash;
        lamp.byTexture = byTexture;
        lamp.name = prim.GetName().GetString();
        if (!byTexture) {
          std::string texture;
          readAttr(prim, "remix:authoring:textureHash", texture);
          if (!texture.empty()) {
            lamp.textureHash = std::strtoull(texture.c_str(), nullptr, 16);
          }
        }
        readAttr(prim, "remix:authoring:beamOffset", lamp.beamOffset);
        readVec(prim, "inputs:color", lamp.color);
        readAttr(prim, "inputs:intensity", lamp.intensity);
        readAttr(prim, "inputs:radius", lamp.radius);
        readAttr(prim, "inputs:shaping:cone:angle", lamp.coneAngle);
        readAttr(prim, "inputs:shaping:cone:softness", lamp.coneSoftness);
        readAttr(prim, "inputs:volumetric_radiance_scale", lamp.volumetric);
        int mode = 0;
        readAttr(prim, "remix:animation:mode", mode);
        lamp.animationMode = static_cast<uint32_t>(std::max(mode, 0));
        readAttr(prim, "remix:animation:rate", lamp.animationRate);
        readAttr(prim, "remix:animation:duty", lamp.animationDuty);
        readAttr(prim, "remix:animation:phase", lamp.animationPhase);
        readVec(prim, "remix:animation:colorB", lamp.animationColorB);
        pxr::GfMatrix4d m(1.0);
        readAttr(prim, "xformOp:transform", m);
        lamp.position = Vector3(static_cast<float>(m[3][0]), static_cast<float>(m[3][1]), static_cast<float>(m[3][2]));
        lamp.direction = normalize(Vector3(static_cast<float>(-m[2][0]), static_cast<float>(-m[2][1]), static_cast<float>(-m[2][2])));
        // The file holds the emitting position, which is the lamp's position moved along the beam.
        lamp.position = lamp.position - lamp.direction * lamp.beamOffset;
        m_lamps.push_back(lamp);
      }
    }
    }
    m_status = str::format("Loaded ", m_lamps.size(), " lamp(s) and ", m_meshMaterials.size(), " mesh material(s) from ", path);
    Logger::info("[LightAuthoring] " + m_status);
  }

  // Names are unique in the whole file, because a texture's prim gathers the lamps of several meshes.
  std::string LightAuthoring::uniqueName() const {
    for (uint32_t i = 0;; ++i) {
      const std::string name = kLampPrefix + std::to_string(i);
      const bool used = std::any_of(m_lamps.begin(), m_lamps.end(),
        [&](const AuthoredLamp& l) { return l.name == name; });
      if (!used) {
        return name;
      }
    }
  }

  AuthoredLamp LightAuthoring::makeLamp(XXH64_hash_t meshHash, XXH64_hash_t textureHash, const Matrix4& objectToWorld,
                                        const Vector3& boundsMin, const Vector3& boundsMax, bool zUp, const LampPreset& preset) {
    AuthoredLamp lamp;
    lamp.meshHash = meshHash;
    lamp.textureHash = textureHash;
    lamp.name = uniqueName();
    applyPreset(preset, lamp);
    lamp.placementKnown = true;
    lamp.objectToWorld = objectToWorld;

    const Vector3 worldDown = zUp ? Vector3(0.f, 0.f, -1.f) : Vector3(0.f, -1.f, 0.f);
    const Vector4 down = inverse(objectToWorld) * Vector4(worldDown.x, worldDown.y, worldDown.z, 0.f);
    lamp.direction = normalize(Vector3(down.x, down.y, down.z));

    // Just outside the mesh's bounds on the side the light shines to, so the lamp's own housing does not block it.
    const bool boundsValid = boundsMin.x <= boundsMax.x && boundsMin.y <= boundsMax.y && boundsMin.z <= boundsMax.z;
    if (boundsValid) {
      const Vector3 center = (boundsMin + boundsMax) * 0.5f;
      const Vector3 half = (boundsMax - boundsMin) * 0.5f;
      const float reach = std::fabs(half.x * lamp.direction.x) + std::fabs(half.y * lamp.direction.y) + std::fabs(half.z * lamp.direction.z);
      lamp.position = center + lamp.direction * (reach + lamp.radius + 0.02f);
      lamp.centerKnown = true;
      lamp.pickedCenter = center;
    }
    return lamp;
  }

  void LightAuthoring::setMeshMaterial(XXH64_hash_t meshHash, XXH64_hash_t textureHash, bool byTexture, const std::string& material) {
    auto it = std::find_if(m_meshMaterials.begin(), m_meshMaterials.end(), [&](const MeshMaterial& m) {
      return m.byTexture == byTexture && (byTexture ? m.textureHash == textureHash : m.meshHash == meshHash);
    });
    if (material.empty()) {
      if (it != m_meshMaterials.end()) {
        m_meshMaterials.erase(it);
      }
      return;
    }
    if (it == m_meshMaterials.end()) {
      m_meshMaterials.push_back(MeshMaterial { meshHash, textureHash, material, byTexture });
    } else {
      it->material = material;
      it->meshHash = meshHash;
    }
  }

  void LightAuthoring::setByTexture(AuthoredLamp& lamp, bool byTexture) {
    if (lamp.byTexture == byTexture || lamp.textureHash == 0) {
      return;
    }
    // A texture lamp's position is relative to each mesh's bounds centre (Remix adds the centre per draw).
    if (lamp.centerKnown) {
      lamp.position = byTexture ? lamp.position - lamp.pickedCenter : lamp.position + lamp.pickedCenter;
    }
    lamp.byTexture = byTexture;
  }

  bool LightAuthoring::save() {
    const std::string path = modFilePath();
    const bool hasLibrary = std::filesystem::exists(std::filesystem::path(path).parent_path() / kLibraryFile);

    // One prim per key: a mesh hash under meshes, a texture hash under textureMeshes.
    auto keyOf = [](bool byTexture, XXH64_hash_t meshHash, XXH64_hash_t textureHash) { return byTexture ? textureHash : meshHash; };
    std::vector<XXH64_hash_t> keys[2];
    auto addKey = [&](bool byTexture, XXH64_hash_t key) {
      std::vector<XXH64_hash_t>& list = keys[byTexture ? 1 : 0];
      if (key != 0 && std::find(list.begin(), list.end(), key) == list.end()) {
        list.push_back(key);
      }
    };
    for (const AuthoredLamp& l : m_lamps) {
      addKey(l.byTexture, keyOf(l.byTexture, l.meshHash, l.textureHash));
    }
    for (const MeshMaterial& m : m_meshMaterials) {
      addKey(m.byTexture, keyOf(m.byTexture, m.meshHash, m.textureHash));
    }

    std::ostringstream o;
    o << "#usda 1.0\n(\n    doc = \"Lights and materials attached to game meshes, written by the Remix Lamps panel. Each mesh_ and tex_ prim keeps the game's own mesh.\"\n";
    if (hasLibrary) {
      o << "    subLayers = [\n        @./" << kLibraryFile << "@\n    ]\n";
    }
    o << ")\n\n";
    o << "def Xform \"RootNode\"\n{\n";
    for (const bool byTexture : { false, true }) {
      o << "    def Scope \"" << (byTexture ? "textureMeshes" : "meshes") << "\"\n    {\n";
      for (XXH64_hash_t key : keys[byTexture ? 1 : 0]) {
        auto material = std::find_if(m_meshMaterials.begin(), m_meshMaterials.end(), [&](const MeshMaterial& m) {
          return m.byTexture == byTexture && keyOf(byTexture, m.meshHash, m.textureHash) == key;
        });
        const bool bound = material != m_meshMaterials.end() && hasLibrary;
        o << "        def Xform \"" << (byTexture ? kTexturePrefix : kMeshPrefix) << hex16(key) << "\""
          << (bound ? " (\n            prepend apiSchemas = [\"MaterialBindingAPI\"]\n        )" : "") << "\n        {\n";
        if (byTexture) {
          o << "            bool remix:anchor:boundsCenter = 1\n";
        }
        if (bound) {
          o << "            rel material:binding = </RootNode/Looks/" << material->material << ">\n";
        }
        // The hash the prim is not keyed by, kept for highlighting and for switching the anchor.
        XXH64_hash_t other = 0;
        for (const AuthoredLamp& l : m_lamps) {
          if (l.byTexture == byTexture && keyOf(byTexture, l.meshHash, l.textureHash) == key) {
            other = byTexture ? l.meshHash : l.textureHash;
          }
        }
        if (bound) {
          other = byTexture ? material->meshHash : material->textureHash;
        }
        if (other != 0) {
          o << "            string " << (byTexture ? "remix:authoring:meshHash" : "remix:authoring:textureHash") << " = \"" << hex16(other) << "\"\n";
        }
        for (const AuthoredLamp& l : m_lamps) {
          if (l.byTexture != byTexture || keyOf(byTexture, l.meshHash, l.textureHash) != key) {
            continue;
          }
          const bool shaped = l.coneAngle < 179.9f;
          o << "            def SphereLight \"" << l.name << "\"" << (shaped ? " (\n                prepend apiSchemas = [\"ShapingAPI\"]\n            )" : "") << "\n            {\n";
          o << "                color3f inputs:color = " << vec(l.color) << "\n";
          o << "                float inputs:intensity = " << num(l.intensity) << "\n";
          o << "                float inputs:radius = " << num(l.radius) << "\n";
          if (shaped) {
            o << "                float inputs:shaping:cone:angle = " << num(l.coneAngle) << "\n";
            o << "                float inputs:shaping:cone:softness = " << num(l.coneSoftness) << "\n";
          }
          o << "                float inputs:volumetric_radiance_scale = " << num(l.volumetric) << "\n";
          if (l.animationMode != 0) {
            o << "                int remix:animation:mode = " << l.animationMode << "\n";
            o << "                float remix:animation:rate = " << num(l.animationRate) << "\n";
            o << "                float remix:animation:duty = " << num(l.animationDuty) << "\n";
            o << "                float remix:animation:phase = " << num(l.animationPhase) << "\n";
            o << "                color3f remix:animation:colorB = " << vec(l.animationColorB) << "\n";
          }
          if (l.beamOffset != 0.f) {
            o << "                float remix:authoring:beamOffset = " << num(l.beamOffset) << "\n";
          }
          if (!byTexture && l.textureHash != 0) {
            o << "                string remix:authoring:textureHash = \"" << hex16(l.textureHash) << "\"\n";
          }
          o << "                matrix4d xformOp:transform = " << lampMatrix(l) << "\n";
          o << "                uniform token[] xformOpOrder = [\"xformOp:transform\"]\n";
          o << "            }\n";
        }
        o << "        }\n";
      }
      o << "    }\n";
    }
    o << "}\n";
    const size_t primCount = keys[0].size() + keys[1].size();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    // Written to a temporary file and moved over the old one, so the file watcher never reads half a file.
    const std::string tmp = path + ".tmp";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) {
        m_status = "Could not write " + tmp;
        Logger::err("[LightAuthoring] " + m_status);
        return false;
      }
      f << o.str();
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      m_status = "Could not replace " + path + ": " + ec.message();
      Logger::err("[LightAuthoring] " + m_status);
      return false;
    }
    m_status = str::format("Saved ", m_lamps.size(), " lamp(s) and ", m_meshMaterials.size(), " mesh material(s) on ", primCount, " mesh(es) or texture(s) to ", path,
                           m_modFileExisted ? "" : ". The mod is new: its lights load at the next game start.");
    Logger::info("[LightAuthoring] " + m_status);
    return true;
  }

} // namespace dxvk
