#ifndef OPENMW_MWPHYSICS_JOLTWORLD_H
#define OPENMW_MWPHYSICS_JOLTWORLD_H

// NerevarMoon: Jolt Physics world beside Bullet. Bullet keeps actors, rays, projectiles and the navmesh; Jolt only
// simulates dynamic bodies against a copy of the loaded terrain and static objects. Positions are game units here,
// metres inside.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

namespace Resource
{
    struct BulletShape;
}

namespace MWPhysics
{
    /// One skeleton bone handed to the ragdoll: world transforms now and one frame earlier (game units).
    struct RagdollBone
    {
        std::string mName;
        std::string mParent; // nearest ancestor that is itself a ragdoll bone; empty for the pelvis
        osg::Matrixf mWorld;
        osg::Matrixf mWorldPrev;
    };

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

        /// Ragdoll pose after a step: world transform per ragdoll bone (game units).
        struct RagdollPose
        {
            MWWorld::Ptr mPtr;
            std::vector<std::pair<std::string, osg::Matrixf>> mWorld;
            osg::Vec3f mLowest; // pelvis x/y, lowest bone z
            bool mFrozen; // stopped or timed out: the bodies are gone, this pose is final
            std::optional<float> mFallSpeed; // set once: speed (units/s) of the first hard landing of pelvis/chest
        };

        /// Bodies built from `bones` (see joltragdoll.cpp for the required ones), started at the bones' own
        /// motion plus `impulse` (units/s) on the chest. False, with nothing created, when a required bone is missing.
        bool addRagdoll(const MWWorld::Ptr& ptr, const std::vector<RagdollBone>& bones, float dt,
            const osg::Vec3f& impulse, float massKg);
        /// Adds `impulse` (units/s) to the named bone's body (chest when empty or unknown). False when not falling.
        bool applyRagdollImpulse(const MWWorld::Ptr& ptr, const osg::Vec3f& impulse, std::string_view bone);
        bool isRagdollActive(const MWWorld::Ptr& ptr) const;
        /// Stops the fall now; the final pose comes out of takeRagdollPoses().
        void freezeRagdoll(const MWWorld::Ptr& ptr);
        /// Removes the ragdoll without a final pose and forgets its pending poses.
        void dropRagdoll(const MWWorld::Ptr& ptr);
        /// Poses produced since the last call (each step, plus freezes).
        std::vector<RagdollPose> takeRagdollPoses();

        /// Fixed 1/60 s steps, at most 4 per call. Returns the dynamic bodies that moved, with their new transforms.
        std::vector<Moved> step(float dt);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
