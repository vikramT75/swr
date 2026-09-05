#pragma once
#include "../core/shadow_map.h"
#include "../math/math_utils.h"
#include "../scene/light.h"
#include "shader.h"
#include "texture.h"
#include <algorithm>
#include <cmath>

struct PBRShader : Shader
{
    const LightList *lights = nullptr;
    const ShadowMap *shadowMap = nullptr;
    Vec3 cameraPos = {0.f, 0.f, 0.f};

    Vec3 albedo = {1.f, 1.f, 1.f};
    float metallic = 0.f, roughness = 0.5f, ao = 1.f;
    float opacity = 1.0f; // 1.0 = fully opaque, 0.0 = fully transparent
    // Curved-surface fix: negate rDir.y before env-map lookup.
    // Needed for convex objects (e.g. sphere) where the silhouette normal points
    // downward in screen space, inverting the specular reflection.
    bool flipReflectionY = false;

    const Texture *albedoMap = nullptr, *roughnessMap = nullptr, *metallicMap = nullptr, *normalMap = nullptr;
    const Texture *irradianceMap = nullptr, *environmentMap = nullptr;

    void setFrameState(const Vec3 &camPos, const LightList *lts, const ShadowMap *sm) override
    {
        cameraPos = camPos;
        lights = lts;
        shadowMap = sm;
    }

    Vec4 shade(const FragmentInput &frag) const override
    {
        // --- 1. Debug Visualization ---
        DebugMode activeMode = getActiveMode();
        if (activeMode != DebugMode::None)
        {
            Vec3 dCol = {0.f, 0.f, 0.f};
            if (activeMode == DebugMode::Normals)
            {
                Vec3 n = frag.normal.normalized();
                dCol = {n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f, n.z * 0.5f + 0.5f};
            }
            else if (activeMode == DebugMode::UVs)
            {
                dCol = {frag.uv.x, frag.uv.y, 0.f};
            }
            else if (activeMode == DebugMode::Tangents)
            {
                Vec3 t = frag.tangent.normalized();
                dCol = {t.x * 0.5f + 0.5f, t.y * 0.5f + 0.5f, t.z * 0.5f + 0.5f};
            }
            else if (activeMode == DebugMode::WorldPos)
            {
                dCol = {std::abs(std::sin(frag.position.x)), std::abs(std::sin(frag.position.y)),
                        std::abs(std::sin(frag.position.z))};
            }
            else if (activeMode == DebugMode::Shadows && shadowMap)
            {
                float s = shadowMap->shadowFactor(frag.position);
                dCol = {s, s, s};
            }
            return Vec4(dCol, 1.0f); // Debug views are always opaque
        }

        // --- 2. Surface Basis & Vectors ---
        Vec3 N = frag.normal.normalized();
        if (normalMap && normalMap->loaded)
        {
            Vec3 T = frag.tangent.normalized();
            T = (T - N * N.dot(T)).normalized();
            Vec3 B = N.cross(T);
            uint32_t t = normalMap->sampleBilinear(frag.uv.x, frag.uv.y);
            Vec3 texN = {((t >> 16) & 0xFF) / 255.f * 2.f - 1.f, ((t >> 8) & 0xFF) / 255.f * 2.f - 1.f,
                         (t & 0xFF) / 255.f * 2.f - 1.f};
            N = (T * texN.x + B * texN.y + N * texN.z).normalized();
        }

        Vec3 V = (cameraPos - frag.position).normalized();
        // For double-sided surfaces (CullMode::None), the interpolated normal may point
        // away from the camera on back faces. Always orient N toward the viewer.
        if (N.dot(V) < 0.0f)
            N = N * -1.0f;
        Vec3 R = (N * (2.0f * N.dot(V)) - V).normalized();

        // --- 3. Material Sampling ---
        Vec3 baseCol = {std::pow(albedo.x, 2.2f), std::pow(albedo.y, 2.2f), std::pow(albedo.z, 2.2f)};

        if (albedoMap && albedoMap->loaded)
        {
            uint32_t t = albedoMap->sampleBilinear(frag.uv.x, frag.uv.y);
            Vec3 srgb = {((t >> 16) & 0xFF) / 255.f, ((t >> 8) & 0xFF) / 255.f, (t & 0xFF) / 255.f};
            baseCol = {std::pow(srgb.x, 2.2f), std::pow(srgb.y, 2.2f), std::pow(srgb.z, 2.2f)};
        }

        float rough = roughness;
        if (roughnessMap && roughnessMap->loaded)
            rough = ((roughnessMap->sampleBilinear(frag.uv.x, frag.uv.y) >> 16) & 0xFF) / 255.f;

        float metal = metallic;
        if (metallicMap && metallicMap->loaded)
            metal = ((metallicMap->sampleBilinear(frag.uv.x, frag.uv.y) >> 16) & 0xFF) / 255.f;

        Vec3 F0 = Vec3(0.04f, 0.04f, 0.04f) + (baseCol - Vec3(0.04f, 0.04f, 0.04f)) * metal;

        // --- 4. Direct Lighting ---
        Vec3 Lo = {0.f, 0.f, 0.f};
        for (const auto &light : lights->lights)
        {
            float shadow = (light.castsShadow && shadowMap) ? shadowMap->shadowFactor(frag.position) : 1.0f;
            Vec3 L;
            float att = 1.0f;
            if (light.type == LightType::Directional)
                L = -light.direction.normalized();
            else // Point or Spot
            {
                Vec3 toL = light.position - frag.position;
                float d = toL.length();
                L = toL * (1.f / d);
                att = 1.0f / (light.attConstant + light.attLinear * d + light.attQuadratic * d * d);
                
                if (light.type == LightType::Spot)
                {
                    float theta = L.dot(-light.direction.normalized());
                    float epsilon = light.cutOff - light.outerCutOff;
                    float intensity = MathUtils::clamp((theta - light.outerCutOff) / epsilon, 0.0f, 1.0f);
                    att *= intensity;
                }
            }
            Vec3 H = (V + L).normalized();
            float nL = std::max(N.dot(L), 0.0f), nV = std::max(N.dot(V), 0.0f);
            if (nL > 0.0f)
            {
                float nH = std::max(N.dot(H), 0.0f), hV = std::max(H.dot(V), 0.0f);
                // Disney roughness remapping: α = rough², so α² = rough⁴
                float a2 = std::pow(rough * rough, 2);
                float D = a2 / (MathUtils::PI * std::pow(nH * nH * (a2 - 1.f) + 1.f, 2));
                float k = std::pow(rough + 1.f, 2) / 8.f;
                float G = (nV / (nV * (1.f - k) + k)) * (nL / (nL * (1.f - k) + k));
                Vec3 F = F0 + (Vec3(1.f, 1.f, 1.f) - F0) * std::pow(1.f - hV, 5.f);
                Vec3 spec = F * (D * G) / (4.f * nV * nL + 0.0001f);
                Vec3 kD = (Vec3(1.f, 1.f, 1.f) - F) * (1.f - metal);
                Lo += (kD * baseCol / MathUtils::PI + spec) * (light.color * light.intensity * att) * nL * shadow;
            }
        }

        // --- 5. Indirect IBL ---
        float dotNV = std::max(N.dot(V), 0.0f);
        Vec3 F_env =
            F0 + (Vec3(std::max(1.0f - rough, F0.x), std::max(1.0f - rough, F0.y), std::max(1.0f - rough, F0.z)) - F0) *
                     std::pow(1.0f - dotNV, 5.0f);

        Vec3 irradiance = {0.03f, 0.03f, 0.04f};
        if (irradianceMap && irradianceMap->loaded)
            irradiance = irradianceMap->sampleSpherical(N);

        Vec3 kD = (Vec3(1.0f, 1.0f, 1.0f) - F_env) * (1.0f - metal);
        Vec3 diffuseIBL = kD * baseCol * irradiance;

        Vec3 specularIBL = {0.f, 0.f, 0.f};
        if (environmentMap && environmentMap->loaded)
        {
            // Roughness jitter — perturbs reflection ray toward normal for rough surfaces
            Vec3 rDir = (R + N * (rough * rough)).normalized();
            if (flipReflectionY)
                rDir.y = -rDir.y;
            specularIBL = environmentMap->sampleSpherical(rDir);
        }

        Vec3 ambient = (diffuseIBL + (specularIBL * (F_env * (1.0f - rough)))) * ao;
        return Vec4(ambient + Lo, opacity); // Raw HDR Vec4 — renderer tonemaps at the end
    }
};