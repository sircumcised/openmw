#ifndef OPENMW_COMPONENTS_SCENEUTIL_BONEPHYSICSMATH_H
#define OPENMW_COMPONENTS_SCENEUTIL_BONEPHYSICSMATH_H

// NerevarMoon: capsule collision of the bone physics solver (bonephysics.cpp), kept free of the scene graph
// so it can be tested on its own.

#include <osg/Vec3f>

namespace SceneUtil::BonePhysicsMath
{
    struct Capsule
    {
        osg::Vec3f mA;
        osg::Vec3f mB;
        float mRadius = 0.f;
    };

    inline Capsule lerp(const Capsule& from, const Capsule& to, float t)
    {
        return Capsule{ from.mA + (to.mA - from.mA) * t, from.mB + (to.mB - from.mB) * t, to.mRadius };
    }

    inline osg::Vec3f closestOnAxis(const osg::Vec3f& pos, const Capsule& capsule, float& t)
    {
        const osg::Vec3f ab = capsule.mB - capsule.mA;
        const float ab2 = ab.length2();
        t = ab2 > 1e-6f ? ((pos - capsule.mA) * ab) / ab2 : 0.f;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        return capsule.mA + ab * t;
    }

    /// Keeps a particle of the given radius out of the capsule. `from` is where the particle was at the start of
    /// the step and `before` the capsule at that time: a particle that was outside and now lies on the other side
    /// of the axis (the capsule swept through it, or it moved further than the capsule is thick) goes back to the
    /// side it came from instead of leaving through the far side.
    inline void collideCapsule(
        osg::Vec3f& pos, const osg::Vec3f& from, const Capsule& before, const Capsule& now, float radius)
    {
        const float minDist = now.mRadius + radius;
        float t1 = 0.f;
        const osg::Vec3f closest = closestOnAxis(pos, now, t1);
        osg::Vec3f away = pos - closest;

        float t0 = 0.f;
        const osg::Vec3f side = from - closestOnAxis(from, before, t0);
        const bool alongAxis = t0 > 0.f && t0 < 1.f && t1 > 0.f && t1 < 1.f; // not going round a cap
        const float wasOutside = (before.mRadius + radius) * 0.9f;
        if (alongAxis && side.length2() >= wasOutside * wasOutside && side * away < 0.f)
        {
            osg::Vec3f axis = now.mB - now.mA;
            axis.normalize();
            osg::Vec3f dir = side - axis * (side * axis);
            if (dir.normalize() > 1e-4f)
            {
                pos = closest + dir * minDist;
                return;
            }
        }

        if (away.length2() >= minDist * minDist)
            return;
        const float dist = away.length();
        if (dist < 1e-4f)
            away = osg::Vec3f(0.f, 0.f, 1.f);
        else
            away /= dist;
        pos = closest + away * minDist;
    }
}

#endif
