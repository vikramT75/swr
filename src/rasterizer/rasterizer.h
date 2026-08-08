#pragma once
#include "../core/depthbuffer.h"
#include "../core/framebuffer.h"
#include "../core/shadow_map.h"
#include "../pipeline/triangle.h"
#include "../shading/shader.h"
#include "barycentric.h"
#include <algorithm>
#include <cmath>

// DRY templated rasterizer.
//
// IsShadowPass = true  → depth-only pass into ShadowMap (no Framebuffer, no shading)
// IsShadowPass = false → full shading pass into HDR Framebuffer
//
// The template parameter is resolved at compile time so all dead branches
// are eliminated — zero runtime overhead from the branching.
template <bool IsShadowPass, bool WriteDepth = true>
inline void rasterizeTriangle(const Triangle &tri, Framebuffer *fb, Depthbuffer *db, ShadowMap *sm, CullMode cull,
                              const Shader *shader, int tileMinX, int tileMaxX, int tileMinY, int tileMaxY)
{
    ScreenVertex v0 = tri.v[0], v1 = tri.v[1], v2 = tri.v[2];

    // 1. Edge Function
    auto edgeFunc = [](const ScreenVertex &a, const ScreenVertex &b, float cx, float cy)
    { return (b.sx - a.sx) * (cy - a.sy) - (b.sy - a.sy) * (cx - a.sx); };

    // 2. Culling (Positive area = backface in Y-down screen space)
    float originalArea = edgeFunc(v0, v1, v2.sx, v2.sy);
    if (cull == CullMode::Back && originalArea >= 0.f)
        return;
    if (cull == CullMode::Front && originalArea <= 0.f)
        return;
    if (std::abs(originalArea) < 0.00001f)
        return;

    // 3. Force positive winding for the inner loop
    float area2 = originalArea;
    if (area2 < 0.f)
    {
        std::swap(v1, v2);
        area2 = -area2;
    }

    // 4. Bounding Box clipped to tile
    int minX = std::max(tileMinX, (int)std::floor(std::min({v0.sx, v1.sx, v2.sx})));
    int maxX = std::min(tileMaxX, (int)std::ceil(std::max({v0.sx, v1.sx, v2.sx})));
    int minY = std::max(tileMinY, (int)std::floor(std::min({v0.sy, v1.sy, v2.sy})));
    int maxY = std::min(tileMaxY, (int)std::ceil(std::max({v0.sy, v1.sy, v2.sy})));

    // 5. Incremental edge deltas:  dE/dx = a.y − b.y | dE/dy = b.x − a.x
    float stepX0 = v1.sy - v2.sy, stepY0 = v2.sx - v1.sx;
    float stepX1 = v2.sy - v0.sy, stepY1 = v0.sx - v2.sx;
    float stepX2 = v0.sy - v1.sy, stepY2 = v1.sx - v0.sx;

    float px = (float)minX + 0.5f, py = (float)minY + 0.5f;
    float w0_row = edgeFunc(v1, v2, px, py);
    float w1_row = edgeFunc(v2, v0, px, py);
    float w2_row = edgeFunc(v0, v1, px, py);
    float invArea = 1.0f / area2;

    for (int y = minY; y <= maxY; ++y)
    {
        float w0 = w0_row, w1 = w1_row, w2 = w2_row;
        for (int x = minX; x <= maxX; ++x)
        {
            if (w0 >= 0 && w1 >= 0 && w2 >= 0)
            {
                float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;
                float z = b0 * v0.z + b1 * v1.z + b2 * v2.z;

                if constexpr (IsShadowPass)
                {
                    // Depth-only: write into shadow map.
                    // NOTE: shadow pass is serial — ShadowMap::testAndSet is NOT thread-safe.
                    if (sm)
                        sm->testAndSet(x, y, z);
                }
                else
                {
                    // Choose depth test based on template parameter
                    bool depthPass = WriteDepth ? db->test(x, y, z) : db->testReadOnly(x, y, z);

                    if (depthPass)
                    {
                        // Perspective-correct interpolation via 1/w
                        float w = 1.f / (b0 * v0.invW + b1 * v1.invW + b2 * v2.invW);

                        FragmentInput frag;
                        frag.position = (v0.worldPos * b0 + v1.worldPos * b1 + v2.worldPos * b2) * w;
                        frag.normal = (v0.normal * b0 + v1.normal * b1 + v2.normal * b2) * w;
                        frag.uv = (v0.uv * b0 + v1.uv * b1 + v2.uv * b2) * w;
                        frag.tangent = (v0.tangent * b0 + v1.tangent * b1 + v2.tangent * b2) * w;
                        frag.depth = z;

                        // shade() returns a raw linear HDR Vec4 (rgb, alpha)
                        Vec4 shaderOut =
                            shader ? shader->shade(frag)
                                   : Vec4{((tri.color >> 16) & 0xFF) / 255.f, ((tri.color >> 8) & 0xFF) / 255.f,
                                          (tri.color & 0xFF) / 255.f, 1.0f};

                        Vec3 srcColor = {shaderOut.x, shaderOut.y, shaderOut.z};
                        float alpha = shaderOut.w;

                        if (alpha >= 0.999f)
                        {
                            // Fully opaque, overwrite pixel
                            fb->setPixel(x, y, srcColor);
                        }
                        else if (alpha > 0.001f)
                        {
                            // Alpha Blending (Painter's Over Op)
                            Vec3 dstColor = fb->getPixel(x, y);
                            Vec3 blended = srcColor * alpha + dstColor * (1.0f - alpha);
                            fb->setPixel(x, y, blended);
                        }
                    }
                }
            }
            w0 += stepX0;
            w1 += stepX1;
            w2 += stepX2;
        }
        w0_row += stepY0;
        w1_row += stepY1;
        w2_row += stepY2;
    }
}