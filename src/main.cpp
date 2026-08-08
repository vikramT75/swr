#include "core/asset_manager.h"
#include "core/renderer.h"
#include "loaders/scene_loader.h"
#include "math/math_utils.h"
#include "platform/sdl_window.h"
#include "scene/scene.h"
#include "shading/pbr.h"
#include <iostream>
#include <string>
#include <vector>

// Required definitions for static members in shader.h
DebugMode Shader::globalDebugMode = DebugMode::None;
float ShaderUtils::exposure = 1.0f;
float ShaderUtils::saturation = 1.0f;

int main(int, char *[])
{
    const int WIDTH = 800;
    const int HEIGHT = 600;

    SDLWindow window("swr — software renderer", WIDTH, HEIGHT);
    Renderer renderer(WIDTH, HEIGHT);
    AssetManager assets;
    Scene scene;

    // --- Load scene from JSON ---
    std::cout << "Loading assets/scenes/scene.json...\n";

    SceneLoader loader;
    if (!loader.load("assets/scenes/scene.json", scene, assets))
    {
        std::cout << "scene.json not found or invalid. Falling back to backup.json...\n";
        if (!loader.load("assets/scenes/backup.json", scene, assets))
        {
            std::cerr << "Fatal: Could not load scene.json OR backup.json. Exiting.\n";
            return 1;
        }
    }
    scene.camera.aspect = (float)WIDTH / (float)HEIGHT;

    // --- Grab named entities for animations ---
    Entity *center = loader.findEntity("center");
    std::vector<Entity *> orbiters;
    for (int i = 0; i < 3; ++i)
    {
        Entity *o = loader.findEntity("orbiter_" + std::to_string(i));
        if (o)
            orbiters.push_back(o);
    }

    // --- Main Loop ---
    float angle = 0.f;
    uint32_t lastTicks = SDL_GetTicks();

    while (window.pollEvents())
    {
        uint32_t currentTicks = SDL_GetTicks();
        float dt = (currentTicks - lastTicks) / 1000.f;
        lastTicks = currentTicks;
        angle += 1.5f * dt;

        const uint8_t *keys = SDL_GetKeyboardState(nullptr);

        // Debug mode toggles
        if (keys[SDL_SCANCODE_1])
            Shader::globalDebugMode = DebugMode::None;
        if (keys[SDL_SCANCODE_2])
            Shader::globalDebugMode = DebugMode::Normals;
        if (keys[SDL_SCANCODE_3])
            Shader::globalDebugMode = DebugMode::UVs;
        if (keys[SDL_SCANCODE_4])
            Shader::globalDebugMode = DebugMode::Shadows;
        if (keys[SDL_SCANCODE_5])
            Shader::globalDebugMode = DebugMode::Tangents;

        // Saturation control (edge-triggered, not hold-to-fire)
        static bool p_was_pressed = false, l_was_pressed = false;
        bool p_is_pressed = keys[SDL_SCANCODE_P];
        bool l_is_pressed = keys[SDL_SCANCODE_L];
        if (p_is_pressed && !p_was_pressed)
        {
            ShaderUtils::saturation += 0.1f;
            std::cout << "Saturation: " << ShaderUtils::saturation << "\n";
        }
        if (l_is_pressed && !l_was_pressed)
        {
            ShaderUtils::saturation -= 0.1f;
            std::cout << "Saturation: " << ShaderUtils::saturation << "\n";
        }
        p_was_pressed = p_is_pressed;
        l_was_pressed = l_is_pressed;

        // Flashlight toggle logic
        static bool f_was_pressed = false;
        static bool flashlight_on = false;
        bool f_is_pressed = keys[SDL_SCANCODE_F];

        if (f_is_pressed && !f_was_pressed)
        {
            flashlight_on = !flashlight_on;
        }
        f_was_pressed = f_is_pressed;

        // Flashlight tracks camera (light index 2 = point light in JSON)
        if (scene.lights.lights.size() > 2)
        {
            scene.lights.lights[2].position = scene.camera.position;
            scene.lights.lights[2].intensity = flashlight_on ? 5.0f : 0.0f;
        }

        // --- Animations ---
        if (center)
        {
            center->transform.rotation.y = angle;
            center->transform.rotation.x = angle * 0.5f;
        }

        for (int i = 0; i < (int)orbiters.size(); ++i)
        {
            float off = angle + (i * MathUtils::TWO_PI / 3.f);
            orbiters[i]->transform.position = {std::cos(off) * 2.5f, std::sin(angle * 2.f + i) * 0.5f,
                                               std::sin(off) * 2.5f};
            orbiters[i]->transform.rotation.x += 1.0f * dt;
        }

        // --- View & Projection ---
        const InputState &in = window.input();
        scene.camera.update(dt, in.w, in.s, in.a, in.d, in.e, in.q, in.shift, in.mouseDX, in.mouseDY);

        // --- Render Execution ---
        renderer.render(scene);
        renderer.applyBloom();
        window.present(renderer.pixelData());
    }

    return 0;
}