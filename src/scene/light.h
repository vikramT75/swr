#pragma once
#include "../math/vec3.h"
#include <vector>

enum class LightType
{
    Directional,
    Point,
    Spot
};

struct Light
{
    LightType type = LightType::Directional;
    Vec3 direction = {0.f, -1.f, 0.f}; // Directional & Spot
    Vec3 position = {0.f, 5.f, 0.f};   // Point & Spot
    Vec3 color = {1.f, 1.f, 1.f};      // linear RGB
    float intensity = 1.f;

    // Point/Spot light attenuation:  1 / (constant + linear*d + quadratic*d^2)
    float attConstant = 1.f;
    float attLinear = 0.09f;
    float attQuadratic = 0.032f;
    
    // Spot light cone angles (stored as cosine)
    float cutOff = 0.976f;       // cos(12.5 deg)
    float outerCutOff = 0.953f;  // cos(17.5 deg)

    bool castsShadow = false;
};

// Passed to shaders — owns the light list.
struct LightList
{
    std::vector<Light> lights;

    void add(const Light &l)
    {
        lights.push_back(l);
    }
    void clear()
    {
        lights.clear();
    }
};