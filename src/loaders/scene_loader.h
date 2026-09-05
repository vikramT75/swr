#pragma once
#include "../core/asset_manager.h"
#include "../math/math_utils.h"
#include "../scene/scene.h"
#include "../shading/pbr.h"
#include "../third_party/nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// SceneLoader — reads a JSON scene file and populates a Scene.
//
// Ownership:
//   - PBRShader instances → owned by SceneLoader (m_shaders)
//   - Material instances  → owned by SceneLoader (m_materials)
//   - Entity instances    → owned by Scene (scene.entities)
//   - Meshes / Textures   → owned by AssetManager
//
// The SceneLoader must outlive the Scene it populates.
// ---------------------------------------------------------------------------
class SceneLoader
{
  public:
    bool load(const std::string &path, Scene &scene, AssetManager &assets)
    {
        // --- Read & parse JSON ---
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "SceneLoader: failed to open " << path << "\n";
            return false;
        }

        json root;
        try
        {
            root = json::parse(file);
        }
        catch (const json::parse_error &e)
        {
            std::cerr << "SceneLoader: JSON parse error: " << e.what() << "\n";
            return false;
        }

        // --- 1. Camera ---
        if (root.contains("camera"))
        {
            auto &cam = root["camera"];
            if (cam.contains("position"))
            {
                auto &p = cam["position"];
                scene.camera.position = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
            }
            if (cam.contains("yaw"))
                scene.camera.yaw = MathUtils::toRadians(cam["yaw"].get<float>());
            if (cam.contains("pitch"))
                scene.camera.pitch = MathUtils::toRadians(cam["pitch"].get<float>());
            if (cam.contains("fov"))
                scene.camera.fovY = MathUtils::toRadians(cam["fov"].get<float>());
            if (cam.contains("zNear"))
                scene.camera.zNear = cam["zNear"].get<float>();
            if (cam.contains("zFar"))
                scene.camera.zFar = cam["zFar"].get<float>();
        }

        // --- 2. Skybox / IBL ---
        Texture *sky = nullptr;
        Texture *irr = nullptr;
        if (root.contains("skybox"))
        {
            std::string skyPath = root["skybox"].get<std::string>();
            scene.environmentMap = assets.getTexture(skyPath);
            sky = scene.environmentMap;
            irr = assets.getIrradianceMap(skyPath);
        }

        // --- 3. Shadow Map ---
        if (root.contains("shadow"))
        {
            auto &sh = root["shadow"];
            scene.shadowMap.width  = sh.value("width",  1024);
            scene.shadowMap.height = sh.value("height", 1024);
            scene.shadowMap.bias   = sh.value("bias",   0.01f);

            Vec3 dir = {-1.f, -1.f, -1.f};
            Vec3 center = {0.f, 0.f, 0.f};
            float extent = 12.f, zNear = 0.1f, zFar = 40.f;

            if (sh.contains("direction"))
            {
                auto &d = sh["direction"];
                dir = {d[0].get<float>(), d[1].get<float>(), d[2].get<float>()};
            }
            if (sh.contains("center"))
            {
                auto &c = sh["center"];
                center = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
            }
            extent = sh.value("extent", extent);
            zNear  = sh.value("zNear",  zNear);
            zFar   = sh.value("zFar",   zFar);

            scene.shadowMap.setup(dir.normalized(), center, extent, zNear, zFar);
        }

        // --- 4. Lights ---
        if (root.contains("lights"))
        {
            for (auto &jl : root["lights"])
            {
                Light light;
                std::string type = jl.value("type", "directional");
                if (type == "spot")        light.type = LightType::Spot;
                else if (type == "point")  light.type = LightType::Point;
                else                       light.type = LightType::Directional;

                if (jl.contains("direction"))
                {
                    auto &d = jl["direction"];
                    light.direction = Vec3{d[0].get<float>(), d[1].get<float>(), d[2].get<float>()}.normalized();
                }
                if (jl.contains("position"))
                {
                    auto &p = jl["position"];
                    light.position = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
                }
                if (jl.contains("color"))
                {
                    auto &c = jl["color"];
                    light.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
                }

                light.intensity    = jl.value("intensity",    1.0f);
                light.attConstant  = jl.value("attConstant",  1.0f);
                light.attLinear    = jl.value("attLinear",    0.09f);
                light.attQuadratic = jl.value("attQuadratic", 0.032f);
                light.castsShadow  = jl.value("castsShadow",  false);

                if (jl.contains("cutOff"))
                    light.cutOff = std::cos(MathUtils::toRadians(jl.value("cutOff", 12.5f)));
                if (jl.contains("outerCutOff"))
                    light.outerCutOff = std::cos(MathUtils::toRadians(jl.value("outerCutOff", 17.5f)));

                scene.lights.add(light);
            }
        }

        // --- 5. Materials ---
        if (root.contains("materials"))
        {
            for (auto &[id, jm] : root["materials"].items())
            {
                auto shader = std::make_unique<PBRShader>();

                // Albedo color or default white
                if (jm.contains("albedo"))
                {
                    auto &a = jm["albedo"];
                    shader->albedo = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
                }

                shader->metallic  = jm.value("metallic",  0.0f);
                shader->roughness = jm.value("roughness", 0.5f);
                shader->ao        = jm.value("ao",        1.0f);
                shader->opacity   = jm.value("opacity",   1.0f);
                shader->flipReflectionY = jm.value("flipReflectionY", false);

                // Texture maps (optional)
                if (jm.contains("albedoMap"))
                    shader->albedoMap = assets.getTexture(jm["albedoMap"].get<std::string>());
                if (jm.contains("normalMap"))
                    shader->normalMap = assets.getTexture(jm["normalMap"].get<std::string>());
                if (jm.contains("roughnessMap"))
                    shader->roughnessMap = assets.getTexture(jm["roughnessMap"].get<std::string>());
                if (jm.contains("metallicMap"))
                    shader->metallicMap = assets.getTexture(jm["metallicMap"].get<std::string>());

                // IBL maps (shared from skybox)
                shader->irradianceMap  = irr;
                shader->environmentMap = sky;

                // Build Material struct
                auto mat = std::make_unique<Material>();
                mat->shader        = shader.get();
                mat->castsShadow   = jm.value("castsShadow", true);
                mat->isTransparent = jm.value("transparent", false);

                // CullMode
                std::string cullStr = jm.value("cullMode", "back");
                if (cullStr == "none")       mat->cullMode = CullMode::None;
                else if (cullStr == "front")  mat->cullMode = CullMode::Front;
                else                          mat->cullMode = CullMode::Back;

                // Store with ownership
                m_materialMap[id] = mat.get();
                m_shaders.push_back(std::move(shader));
                m_materials.push_back(std::move(mat));
            }
        }

        // --- 6. Entities ---
        // First pass: create all entities with transforms and mesh/material
        if (root.contains("entities"))
        {
            for (auto &je : root["entities"])
            {
                std::string name = je.value("name", "unnamed");
                Entity *e = scene.createEntity(name);

                // Mesh
                if (je.contains("mesh"))
                    e->mesh = assets.getMesh(je["mesh"].get<std::string>());

                // Material (by ID)
                if (je.contains("material"))
                {
                    std::string matId = je["material"].get<std::string>();
                    auto it = m_materialMap.find(matId);
                    if (it != m_materialMap.end())
                        e->material = it->second;
                    else
                        std::cerr << "SceneLoader: unknown material '" << matId << "' on entity '" << name << "'\n";
                }

                // Transform
                if (je.contains("position"))
                {
                    auto &p = je["position"];
                    e->transform.position = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
                }
                if (je.contains("rotation"))
                {
                    auto &r = je["rotation"];
                    // JSON stores degrees — convert to radians
                    e->transform.rotation = {MathUtils::toRadians(r[0].get<float>()),
                                             MathUtils::toRadians(r[1].get<float>()),
                                             MathUtils::toRadians(r[2].get<float>())};
                }
                if (je.contains("scale"))
                {
                    auto &s = je["scale"];
                    e->transform.scaleVec = {s[0].get<float>(), s[1].get<float>(), s[2].get<float>()};
                }

                m_entityMap[name] = e;
            }

            // Second pass: resolve parent-child relationships
            for (auto &je : root["entities"])
            {
                if (je.contains("parent"))
                {
                    std::string childName  = je.value("name", "");
                    std::string parentName = je["parent"].get<std::string>();

                    auto childIt  = m_entityMap.find(childName);
                    auto parentIt = m_entityMap.find(parentName);

                    if (childIt != m_entityMap.end() && parentIt != m_entityMap.end())
                        parentIt->second->addChild(childIt->second);
                    else
                        std::cerr << "SceneLoader: failed to resolve parent '" << parentName
                                  << "' for entity '" << childName << "'\n";
                }
            }
        }

        // --- 7. Aspect ratio (needs window dims — set by caller after load) ---
        // scene.camera.aspect must be set by the caller since it depends on window size.

        std::cout << "SceneLoader: loaded " << path << " ("
                  << scene.lights.lights.size() << " lights, "
                  << m_materials.size() << " materials, "
                  << scene.entities.size() << " entities)\n";

        return true;
    }

    // Look up an entity by name (for animation references in the main loop)
    Entity *findEntity(const std::string &name) const
    {
        auto it = m_entityMap.find(name);
        return (it != m_entityMap.end()) ? it->second : nullptr;
    }

  private:
    // Owned resources — must outlive the Scene
    std::vector<std::unique_ptr<PBRShader>> m_shaders;
    std::vector<std::unique_ptr<Material>>  m_materials;

    // Lookup tables (non-owning)
    std::unordered_map<std::string, Material *> m_materialMap;
    std::unordered_map<std::string, Entity *>   m_entityMap;
};
