#ifndef OPENMW_MWPHYSICS_JOLTWORLD_H
#define OPENMW_MWPHYSICS_JOLTWORLD_H

// NerevarMoon: Jolt Physics world beside Bullet. Bullet keeps actors, rays, projectiles and the navmesh; Jolt only
// simulates dynamic bodies against a copy of the loaded terrain and static objects. Positions are game units here,
// metres inside.

#include <memory>
#include <optional>
#include <vector>

#include <osg/Quat>
#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

namespace Resource
{
    struct BulletShape;
}

namespace MWPhysics
{
    class JoltWorld
    {
    public:
        struct Moved
        {
            MWWorld::Ptr mPtr;
            osg::Vec3f mPosition;
            osg::Quat mRotation;
            bool mSettled; // asleep in Jolt: no longer simulated, back to a normal object
        };

        JoltWorld();
        ~JoltWorld();

        void addHeightField(const float* heights, int x, int y, int size, int verts);
        void removeHeightField(int x, int y);

        void addStatic(const MWWorld::Ptr& ptr, const Resource::BulletShape& source, const osg::Vec3f& position,
            const osg::Quat& rotation, float scale);
        void setStaticPosition(const MWWorld::Ptr& ptr, const osg::Vec3f& position);
        void setStaticRotation(const MWWorld::Ptr& ptr, const osg::Quat& rotation);
        void setStaticScale(const MWWorld::Ptr& ptr, float scale);

        /// Box of half extents `halfExtents` centred at `centre` (object space, game units), started at `velocity`
        /// (game units/s). Replaces the object's static copy. False when Jolt has no room for another body.
        bool addDynamic(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents, const osg::Vec3f& centre,
            const osg::Vec3f& position, const osg::Quat& rotation, const osg::Vec3f& velocity);
        bool isDynamic(const MWWorld::Ptr& ptr) const;

        /// Static copy and dynamic body of the object, when it has them.
        void remove(const MWWorld::Ptr& ptr);
        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated);

        /// Fixed 1/60 s steps, at most 4 per call. Returns the dynamic bodies that moved, with their new transforms.
        std::vector<Moved> step(float dt);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
