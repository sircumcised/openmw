#ifndef OPENMW_MWPHYSICS_JOLTSHAPES_H
#define OPENMW_MWPHYSICS_JOLTSHAPES_H

// NerevarMoon: Bullet collision shapes and terrain converted to Jolt shapes (metres).

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

class btCollisionShape;

namespace MWPhysics
{
    // Morrowind units per metre: Jolt's tolerances and gravity assume metres.
    constexpr float sJoltUnitsPerMetre = 70.f;

    struct JoltShapeStats
    {
        int mConverted = 0;
        int mUnsupported = 0;
        int mFailed = 0;
    };

    // Unscaled Bullet shape (a BulletShape's source mCollisionShape) to a Jolt shape in metres; null when nothing
    // in it can be converted.
    JPH::ShapeRefC convertBulletShape(const btCollisionShape& shape, JoltShapeStats& stats);

    // A cell's terrain as a triangle mesh in world metres, split like Bullet's diamond subdivision.
    JPH::ShapeRefC makeTerrainShape(const float* heights, int x, int y, int size, int verts);
}

#endif
