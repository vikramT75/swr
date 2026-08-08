#pragma once
#include "vertex.h"
#include <vector>

// Sutherland-Hodgman clip against a single plane defined by:
//   dot(v, plane) + plane.w >= 0  → inside
//
// We clip in clip space against the 6 frustum planes.
// Only the near plane (w + z >= 0) is strictly required to prevent
// divide-by-zero in perspective divide, but we clip all 6 for correctness.

// Returns the interpolation factor t where the edge (a→b) crosses the plane.
// Plane equation: dot(plane_normal, v) = 0 in homogeneous clip space.
inline float intersectPlane(const Vec4 &a, const Vec4 &b, float na, float nb) // signed distances
{
    return na / (na - nb);
}

// Interpolate a ClipVertex between two endpoints at factor t.
inline ClipVertex lerpClip(const ClipVertex &a, const ClipVertex &b, float t)
{
    ClipVertex out;

    out.clip.x = a.clip.x + (b.clip.x - a.clip.x) * t;
    out.clip.y = a.clip.y + (b.clip.y - a.clip.y) * t;
    out.clip.z = a.clip.z + (b.clip.z - a.clip.z) * t;
    out.clip.w = a.clip.w + (b.clip.w - a.clip.w) * t;

    //  World positions
    out.worldPos.x = a.worldPos.x + (b.worldPos.x - a.worldPos.x) * t;
    out.worldPos.y = a.worldPos.y + (b.worldPos.y - a.worldPos.y) * t;
    out.worldPos.z = a.worldPos.z + (b.worldPos.z - a.worldPos.z) * t;

    out.normal.x = a.normal.x + (b.normal.x - a.normal.x) * t;
    out.normal.y = a.normal.y + (b.normal.y - a.normal.y) * t;
    out.normal.z = a.normal.z + (b.normal.z - a.normal.z) * t;

    out.uv.x = a.uv.x + (b.uv.x - a.uv.x) * t;
    out.uv.y = a.uv.y + (b.uv.y - a.uv.y) * t;

    out.tangent.x = a.tangent.x + (b.tangent.x - a.tangent.x) * t;
    out.tangent.y = a.tangent.y + (b.tangent.y - a.tangent.y) * t;
    out.tangent.z = a.tangent.z + (b.tangent.z - a.tangent.z) * t;

    return out;
}

// Uses a fixed array to avoid heap allocations.
struct ClipPolygon
{
    ClipVertex verts[12];
    int count = 0;
    void push_back(const ClipVertex &v)
    {
        verts[count++] = v;
    }
};

// Clip a polygon against one plane.
inline ClipPolygon clipAgainstPlane(const ClipPolygon &poly, int plane)
{
    if (poly.count == 0)
        return {};

    auto signedDist = [&](const ClipVertex &v) -> float
    {
        switch (plane)
        {
        case 0:
            return v.clip.w + v.clip.x;
        case 1:
            return v.clip.w - v.clip.x;
        case 2:
            return v.clip.w + v.clip.y;
        case 3:
            return v.clip.w - v.clip.y;
        case 4:
            return v.clip.w + v.clip.z;
        case 5:
            return v.clip.w - v.clip.z;
        }
        return 0.f;
    };

    ClipPolygon out;
    for (int i = 0; i < poly.count; ++i)
    {
        const ClipVertex &cur = poly.verts[i];
        const ClipVertex &next = poly.verts[(i + 1) % poly.count];

        float dc = signedDist(cur);
        float dn = signedDist(next);

        bool curInside = dc >= 0.f;
        bool nextInside = dn >= 0.f;

        if (curInside)
            out.push_back(cur);

        if (curInside != nextInside)
        {
            float t = dc / (dc - dn);
            out.push_back(lerpClip(cur, next, t));
        }
    }

    return out;
}

// Clip a triangle (3 ClipVertices) against all 6 frustum planes.
inline ClipPolygon clipTriangle(const ClipVertex tri[3])
{
    ClipPolygon poly;
    poly.push_back(tri[0]);
    poly.push_back(tri[1]);
    poly.push_back(tri[2]);

    for (int plane = 0; plane < 6; ++plane)
    {
        poly = clipAgainstPlane(poly, plane);
        if (poly.count == 0)
            return {};
    }

    return poly;
}