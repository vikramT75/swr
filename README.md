# SWR : Software Renderer

A from-scratch, high-performance CPU rasterizer written in modern C++17. It uses SDL2 exclusively for window management and pixel upload to the screen. No GPU APIs (OpenGL/Vulkan/DirectX) or third-party math libraries used.

This project demonstrates rendering mathematics, physically based rendering (PBR) and image-based lighting (IBL).

---

## Features

### Rendering

- **Physically Based Rendering (PBR)** : Full Cook-Torrance BRDF with GGX Normal Distribution, Smith Geometry masking, and Fresnel-Schlick approximations.
- **Image-Based Lighting (IBL)** : Monte Carlo hemispherical integration bakes physical irradiance maps from HDR equirectangular skyboxes at startup, giving accurate ambient diffuse from real-world lighting environments.
- **Convex Reflection Orientation** : Optional `flipReflectionY` material flag in `PBRShader` and JSON scenes to conditionally invert reflection ray Y-direction for upright HDR specular reflection on convex meshes.
- **Double-Sided Normal Handling** : Shader normal orientation ensures surfaces with `CullMode::None` (such as thin transparent glass panels) orient normals toward the viewer.
- **Shadow Mapping (PCF filtered)** : Percentage-Closer Filtering over a shadow depth buffer for soft, anti-aliased shadows.
- **Alpha Blending & Transparency** : Opaque and transparent geometry are binned automatically. Transparent triangles are sorted back-to-front (Painter's Algorithm) each frame before blending.
- **Linear HDR Pipeline** : All shading runs in linear color space using floating-point `Vec3` radiance buffers. Floating-point `.hdr` textures are loaded directly via `stbi_loadf`.

### Post-Processing

- **ACES Filmic Tonemapping** : Per-channel ACES curve maps HDR radiance to LDR display output with cinematic contrast.
- **Bloom** : Luminosity-threshold extraction (highlights `> 1.0`) fed into a bloom pass.
- **Adjustable Saturation & Exposure** : Real-time color grading via keyboard controls.
- **Gamma 2.2** : sRGB approximation applied as the final step before pixel upload.

### Performance

- **Tiled Multi-Threading** : The framebuffer is partitioned into screen-space tiles. A custom lock-free thread pool dispatches tiles to CPU cores in parallel, eliminating framebuffer race conditions entirely.
- **Zero-Overhead Abstractions** : The rasterizer is templated on `<IsShadowPass, WriteDepth>`, removing all runtime branching from the inner loop at compile time.
- **Compiler SIMD Auto-Vectorization** : Build flags `-O3 -march=native -ffast-math` (GCC/Clang) and `/O2 /arch:AVX2 /fp:fast` (MSVC) are applied automatically, letting the compiler use AVX2 on modern CPUs.

### Architecture

- **Data-Driven Scenes** : Scenes, entities, materials, transforms, and lighting are defined entirely in JSON (`nlohmann/json`). No recompilation needed to change a scene.
- **Near-Plane Clipping** : Sutherland-Hodgman clipping prevents artifacts from geometry crossing the camera near plane.
- **Perspective-Correct Interpolation** : UVs, normals, and all varyings are interpolated correctly in clip space, not screen space.
- **Debug Views** : Toggle normals, UVs, shadow maps, and tangents at runtime.

---

## Render Pipeline

```
JSON Scene
    │
    ▼
SceneLoader ──► AssetManager (Meshes, Textures, IBL bake)
    │
    ▼
Scene (Camera, Lights, ShadowMap, Entities)
    │
    ▼
Renderer::render(Scene&)
    │
    ├─ 1. Shadow Pass      — depth-only rasterization into ShadowMap
    ├─ 2. Geometry Binning — sort triangles → Opaque[] / Transparent[]
    ├─ 3. Opaque Pass      — tiled multi-threaded PBR + depth write
    ├─ 4. Transparent Pass — back-to-front alpha blending
    └─ 5. Post-Process     — Bloom → ACES Tonemap → Gamma 2.2 → SDL2 upload
```

---

## Ownership Model

| Owner          | Owns                                                        |
| -------------- | ----------------------------------------------------------- |
| `AssetManager` | Raw GPU-like resources: `Mesh`, HDR/LDR `Texture` objects   |
| `SceneLoader`  | Render state: `Material`, `PBRShader` instances             |
| `Scene`        | Scene graph: `Entity[]`, `LightList`, `Camera`, `ShadowMap` |
| `Renderer`     | Consumes a `Scene&` & owns no persistent state              |

---

## Mathematical Conventions

| Property          | Convention                               |
| ----------------- | ---------------------------------------- |
| Storage           | Row-major `m[row][col]`                  |
| Vector multiply   | Column-vector `v' = M * v`               |
| Coordinate system | Right-handed, camera looks down `-Z`     |
| NDC range         | `x, y, z ∈ [-1, +1]` (OpenGL convention) |
| Screen origin     | Top-left                                 |

---

## File Map

| Module                       | File(s)                            |
| ---------------------------- | ---------------------------------- |
| Window + pixel upload        | `platform/sdl_window.h`            |
| JSON scene loader            | `loaders/scene_loader.h`           |
| OBJ mesh loader              | `loaders/obj_loader.h`             |
| HDR framebuffer              | `core/framebuffer.h`               |
| Depth buffer                 | `core/depthbuffer.h`               |
| Shadow map                   | `core/shadow_map.h`                |
| Multi-threaded renderer      | `core/renderer.h`                  |
| IBL irradiance bake          | `core/asset_manager.h`             |
| Mat4 + fast math             | `math/mat4.h`, `math/math_utils.h` |
| Scene graph                  | `scene/scene.h`, `scene/entity.h`  |
| Camera & free-look           | `scene/camera.h`                   |
| Sutherland-Hodgman clipping  | `pipeline/clipping.h`              |
| Edge-function rasterizer     | `rasterizer/rasterizer.h`          |
| Cook-Torrance PBR            | `shading/pbr.h`                    |
| Texture sampling (HDR + LDR) | `shading/texture.h`                |

---

## Controls

| Input           | Action                                             |
| --------------- | -------------------------------------------------- |
| `W` `A` `S` `D` | Move camera (forward / left / back / right)        |
| `E` `Q`         | Move up / down                                     |
| `Shift`         | Sprint (4× speed)                                  |
| `F`             | Toggle flashlight (point light attached to camera) |
| `P` `L`         | Saturation up / down                               |
| `1` - `5`       | Debug views: None, Normals, UVs, Shadows, Tangents |
| `Mouse`         | Free look                                          |

---

## Scenes & Assets

### Scenes (`assets/scenes/`)

- `scene.json` : Default startup scene featuring a five-pedestal material showcase (chrome, copper, jade, terracotta, gold, and tinted glass).
- `scene0.json` : Minimalist composition featuring a central brass sphere, concrete monoliths, and intersecting tinted glass shields.
- `studio.json` : Material showcase configured with studio lighting (`studio.hdr`).
- `sunset.json` : Material showcase configured with sunset environment lighting (`sunset.hdr`).
- `backup.json` : Fallback scene loaded if `scene.json` is not found or invalid.

### Meshes (`assets/models/`)

- `cylinder.obj` : Procedural cylinder with counter-clockwise (CCW) outward-facing winding for correct back-face culling on pedestals.
- `sphere.obj` : High-resolution UV sphere with smooth vertex normals.
- `torus.obj` : Torus ring with parameterized major/minor radii.
- `cube.obj`, `ground.obj` : Standard geometric primitives for test setups and ground planes.

### Environment Maps (`assets/textures/`)

- `studio.hdr` : High dynamic range studio lighting map for IBL diffuse/specular irradiance.
- `sunset.hdr` : High dynamic range outdoor sunset map for directional and ambient IBL.

---

## Building

### Prerequisites

- CMake ≥ 3.16
- C++17 compiler: GCC, Clang, or MSVC
- SDL2 development libraries

### Linux / macOS

```bash
sudo apt install libsdl2-dev   # Debian/Ubuntu
# brew install sdl2            # macOS

make
```

### Windows (WSL)

```bash
sudo apt install libsdl2-dev
make
```

### Windows (MSVC / Visual Studio)

```bash
cmake -S . -B build -DSDL2_DIR="C:/SDL2/cmake"
cmake --build build --config Release
```

> **Note:** The `CMakeLists.txt` automatically applies aggressive optimization flags for each compiler

---

## Project Layout

```
swr/
├── src/
│   ├── core/          # Renderer loop, IBL baking, framebuffer, depth, shadow
│   ├── loaders/       # JSON scene parser, Wavefront OBJ parser
│   ├── math/          # Vec2/3/4, Mat4, fast math utilities
│   ├── pipeline/      # Near-plane clipping, projection transforms
│   ├── platform/      # SDL2 window, input polling
│   ├── rasterizer/    # Barycentric coords, edge functions, scanline fill
│   ├── scene/         # Camera, lights, transforms, entities, materials
│   ├── shading/       # Texture sampling, Cook-Torrance BRDF, shaders
│   ├── third_party/   # stb_image (header-only), nlohmann/json (header-only)
│   └── main.cpp       # Entry point, main loop
├── assets/
│   ├── models/        # Wavefront .obj meshes
│   ├── scenes/        # JSON scene descriptors
│   └── textures/      # HDR equirectangular maps + LDR PBR texture sets
├── screenshots/       # Render output images
├── docs/              # Additional documentation
├── CMakeLists.txt
└── Makefile
```

---

## Third-Party Dependencies

| Library                                                        | Use                           | Vendored             |
| -------------------------------------------------------------- | ----------------------------- | -------------------- |
| [SDL2](https://www.libsdl.org/)                                | Window creation, pixel upload | No (system install)  |
| [stb_image / stb_image_write](https://github.com/nothings/stb) | LDR and HDR texture loading   | Yes (`third_party/`) |
| [nlohmann/json](https://github.com/nlohmann/json)              | JSON scene deserialization    | Yes (`third_party/`) |

No math, rendering, or shading library is used : all of that is hand-written.

---

## Acknowledgements

- [Tsoding](https://www.youtube.com/@Tsoding) / [ncot_tech](https://www.youtube.com/@ncot_tech) : Project Inspiration
- [LearnOpenGL](https://learnopengl.com/) : PBR theory and IBL references
- [ACES Filmic Tonemapping](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/) : Krzysztof Narkowicz
- [Physically Based Rendering: From Theory to Implementation](https://pbr-book.org/) : Pharr, Jakob, Humphreys
