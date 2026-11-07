#include "joltworld.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>


#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/resource/bulletshape.hpp>

#include "joltragdoll.hpp"
#include "joltshapes.hpp"

namespace MWPhysics
{
    namespace
    {
        constexpr JPH::ObjectLayer sLayerStatic = 0;
        constexpr JPH::ObjectLayer sLayerMoving = 1;
        constexpr float sStep = 1.f / 60.f;
        constexpr int sMaxSteps = 4;
        constexpr std::size_t sMaxRagdolls = 8;
        constexpr float sRagdollTimeout = 6.f; // seconds before a fall is frozen anyway
        constexpr float sLandingSpeed = 150.f; // units/s
        constexpr float sRollTime = 0.9f; // seconds
        constexpr float sRollDone = 0.8f; // chest facing z where the body lies on its back
        constexpr float sRollSpeed = 10.f; // rad/s about the pelvis-head axis

        void trace(const char* format, ...)
        {
            char buffer[1024];
            va_list args;
            va_start(args, format);
            std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            Log(Debug::Warning) << "Jolt: " << buffer;
        }

        void initJolt()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                JPH::RegisterDefaultAllocator();
                JPH::Trace = trace;
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            });
        }

        JPH::RVec3 toJolt(const osg::Vec3f& v)
        {
            return JPH::RVec3(v.x(), v.y(), v.z()) / sJoltUnitsPerMetre;
        }

        JPH::Quat toJolt(const osg::Quat& q)
        {
            return JPH::Quat(static_cast<float>(q.x()), static_cast<float>(q.y()), static_cast<float>(q.z()),
                static_cast<float>(q.w()))
                .Normalized();
        }

        JPH::ShapeRefC scaled(const JPH::ShapeRefC& shape, float scale)
        {
            if (std::abs(scale - 1.f) < 1e-4f || scale <= 0.f)
                return shape;
            return new JPH::ScaledShape(shape, JPH::Vec3::sReplicate(scale));
        }
    }

    struct JoltWorld::Impl
    {
        using Key = const MWWorld::LiveCellRefBase*;

        struct Static
        {
            JPH::BodyID mBody;
            JPH::ShapeRefC mShape; // unscaled
        };

        struct Dynamic
        {
            MWWorld::Ptr mPtr;
            JPH::BodyID mBody;
            std::uint64_t mPieceOrder = 0; // dismember pieces only: order of creation, 0 for other objects
            float mAge = 0.f;
        };
        std::uint64_t mPieceCounter = 0;
        std::vector<JoltWorld::Moved> mStopped; // pieces stopped by the cap, reported by the next step

        struct Ragdoll
        {
            MWWorld::Ptr mPtr;
            JPH::Ref<JPH::Ragdoll> mRagdoll;
            std::vector<std::string> mNames;
            std::vector<float> mScales;
            int mChest = 0;
            float mAge = 0.f;
            float mFallSpeed = 0.f; // downward speed of pelvis/chest at the previous step
            bool mLanded = false;
            std::uint64_t mOrder = 0;
            JoltWorld::RagdollMode mMode = JoltWorld::RagdollMode::Death;
            float mRollLeft = 0.f; // seconds of roll left, 0 when not rolling
        };

        // First member: the ones below allocate through JPH::Allocate, null until RegisterDefaultAllocator
        struct Init
        {
            Init() { initJolt(); }
        } mInit;
        JPH::BroadPhaseLayerInterfaceTable mBroadPhaseLayers{ 2, 2 };
        JPH::ObjectLayerPairFilterTable mPairFilter{ 2 };
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> mObjectVsBroadPhase;
        std::unique_ptr<JPH::TempAllocatorImpl> mTempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> mJobs;
        JPH::PhysicsSystem mSystem;

        std::map<std::pair<int, int>, JPH::BodyID> mTerrain;
        std::map<Key, Static> mStatics;
        std::map<Key, Dynamic> mDynamics;
        std::map<Key, Ragdoll> mRagdolls;
        std::vector<RagdollPose> mPoses;
        std::uint64_t mRagdollCounter = 0;
        // One converted shape per collision mesh; the shared_ptr keeps the key's BulletShape alive
        std::map<const Resource::BulletShape*, std::pair<std::shared_ptr<const Resource::BulletShape>, JPH::ShapeRefC>>
            mShapes;
        JoltShapeStats mShapeStats;
        float mTimeAccum = 0.f;
        bool mOptimizePending = false;
        int mRemovedSincePrune = 0;

        Impl()
        {
            mBroadPhaseLayers.MapObjectToBroadPhaseLayer(sLayerStatic, JPH::BroadPhaseLayer(0));
            mBroadPhaseLayers.MapObjectToBroadPhaseLayer(sLayerMoving, JPH::BroadPhaseLayer(1));
            mPairFilter.EnableCollision(sLayerMoving, sLayerStatic);
            mPairFilter.EnableCollision(sLayerMoving, sLayerMoving);
            mObjectVsBroadPhase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
                mBroadPhaseLayers, 2, mPairFilter, 2);
            mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
            mJobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2);
            mSystem.Init(65536, 0, 65536, 10240, mBroadPhaseLayers, *mObjectVsBroadPhase, mPairFilter);
            mSystem.SetGravity(JPH::Vec3(0, 0, -9.81f));
            Log(Debug::Info) << "Jolt Physics " << JPH_VERSION_MAJOR << "." << JPH_VERSION_MINOR << "."
                             << JPH_VERSION_PATCH << " started (dynamic bodies)";
        }

        JPH::BodyInterface& bodies() { return mSystem.GetBodyInterface(); }

        void destroy(JPH::BodyID id)
        {
            bodies().RemoveBody(id);
            bodies().DestroyBody(id);
        }

        JPH::ShapeRefC shapeFor(const std::shared_ptr<const Resource::BulletShape>& source)
        {
            if (auto found = mShapes.find(source.get()); found != mShapes.end())
                return found->second.second;
            JPH::ShapeRefC shape;
            if (source->mCollisionShape)
                shape = convertBulletShape(*source->mCollisionShape, mShapeStats);
            mShapes.emplace(source.get(), std::make_pair(source, shape));
            return shape;
        }

        void prune()
        {
            if (++mRemovedSincePrune < 256)
                return;
            mRemovedSincePrune = 0;
            // shortcut: a shape only the cache still holds is dropped; the mesh is converted again if it comes back
            for (auto it = mShapes.begin(); it != mShapes.end();)
            {
                if (it->second.second == nullptr || it->second.second->GetRefCount() <= 1)
                    it = mShapes.erase(it);
                else
                    ++it;
            }
        }

        void removeStatic(Key key)
        {
            auto found = mStatics.find(key);
            if (found == mStatics.end())
                return;
            destroy(found->second.mBody);
            mStatics.erase(found);
            prune();
        }

        RagdollPose poseOf(const Ragdoll& ragdoll, bool frozen)
        {
            RagdollPose pose{ ragdoll.mPtr, {}, osg::Vec3f(), frozen, std::nullopt };
            pose.mAge = ragdoll.mAge;
            pose.mRolling = ragdoll.mRollLeft > 0.f;
            float lowest = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < ragdoll.mNames.size(); ++i)
            {
                const JPH::BodyID id = ragdoll.mRagdoll->GetBodyID(static_cast<int>(i));
                const JPH::RVec3 p = bodies().GetPosition(id) * sJoltUnitsPerMetre;
                const JPH::Quat q = bodies().GetRotation(id);
                const osg::Vec3f t(
                    static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ()));
                const float s = ragdoll.mScales[i];
                pose.mWorld.emplace_back(ragdoll.mNames[i],
                    osg::Matrixf::scale(s, s, s)
                        * osg::Matrixf::rotate(osg::Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()))
                        * osg::Matrixf::translate(t));
                lowest = std::min(lowest, t.z());
                if (i == 0)
                    pose.mLowest = t;
            }
            pose.mLowest.z() = lowest;
            return pose;
        }

        // `report`: queue the final pose; false when the object is going away (its Ptr must not be used later)
        void freezeRagdoll(Key key, bool report = true)
        {
            auto found = mRagdolls.find(key);
            if (found == mRagdolls.end())
                return;
            if (report)
                mPoses.push_back(poseOf(found->second, true));
            found->second.mRagdoll->RemoveFromPhysicsSystem();
            mRagdolls.erase(found);
        }

        osg::Vec3f bodyPosition(const Ragdoll& ragdoll, std::string_view name)
        {
            for (std::size_t i = 0; i < ragdoll.mNames.size(); ++i)
                if (Misc::StringUtils::ciEqual(ragdoll.mNames[i], name))
                {
                    const JPH::RVec3 p = bodies().GetPosition(ragdoll.mRagdoll->GetBodyID(static_cast<int>(i)));
                    return osg::Vec3f(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                        static_cast<float>(p.GetZ()));
                }
            return osg::Vec3f();
        }

        // Pelvis -> head axis (unit) and the facing direction: up x right, right = left upper arm -> right upper arm
        std::pair<osg::Vec3f, osg::Vec3f> axisAndFacing(const Ragdoll& ragdoll)
        {
            osg::Vec3f up = bodyPosition(ragdoll, "Bip01 Head") - bodyPosition(ragdoll, "Bip01 Pelvis");
            osg::Vec3f right
                = bodyPosition(ragdoll, "Bip01 R UpperArm") - bodyPosition(ragdoll, "Bip01 L UpperArm");
            up.normalize();
            osg::Vec3f facing = up ^ right;
            facing.normalize();
            return { up, facing };
        }

        // One roll step: the whole body turns as one piece about the pelvis-head axis, toward chest up
        void rollStep(Ragdoll& ragdoll)
        {
            const auto [axis, facing] = axisAndFacing(ragdoll);
            if (facing.z() > sRollDone)
            {
                ragdoll.mRollLeft = 0.f;
                return;
            }
            const float sign = ((axis ^ facing).z() >= 0.f) ? 1.f : -1.f;
            // full speed from face down, slower towards the back so the body does not roll on over
            const float speed = sRollSpeed * std::clamp((sRollDone + 0.15f - facing.z()) / 0.6f, 0.3f, 1.f);
            const osg::Vec3f omega = axis * (sign * speed);
            const osg::Vec3f centre = bodyPosition(ragdoll, "Bip01 Pelvis");
            for (std::size_t i = 0; i < ragdoll.mNames.size(); ++i)
            {
                const JPH::BodyID id = ragdoll.mRagdoll->GetBodyID(static_cast<int>(i));
                const JPH::RVec3 p = bodies().GetPosition(id);
                const osg::Vec3f r = osg::Vec3f(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                                         static_cast<float>(p.GetZ()))
                    - centre;
                const osg::Vec3f v = omega ^ r;
                const float vz = bodies().GetLinearVelocity(id).GetZ();
                bodies().SetLinearAndAngularVelocity(id, JPH::Vec3(v.x(), v.y(), std::min(v.z(), 0.f) + vz),
                    JPH::Vec3(omega.x(), omega.y(), omega.z()));
            }
        }

        void removeDynamic(Key key)
        {
            auto found = mDynamics.find(key);
            if (found == mDynamics.end())
                return;
            destroy(found->second.mBody);
            mDynamics.erase(found);
        }
    };

    JoltWorld::JoltWorld()
        : mImpl(std::make_unique<Impl>())
    {
    }

    JoltWorld::~JoltWorld()
    {
        Log(Debug::Info) << "Jolt: shapes converted " << mImpl->mShapeStats.mConverted << ", unsupported "
                         << mImpl->mShapeStats.mUnsupported << ", failed " << mImpl->mShapeStats.mFailed;
        for (const auto& [key, ragdoll] : mImpl->mRagdolls)
            ragdoll.mRagdoll->RemoveFromPhysicsSystem();
        mImpl->mRagdolls.clear();
        for (const auto& [key, body] : mImpl->mDynamics)
            mImpl->destroy(body.mBody);
        for (const auto& [key, body] : mImpl->mStatics)
            mImpl->destroy(body.mBody);
        for (const auto& [cell, id] : mImpl->mTerrain)
            mImpl->destroy(id);
    }

    void JoltWorld::addHeightField(const float* heights, int x, int y, int size, int verts)
    {
        removeHeightField(x, y);
        JPH::ShapeRefC shape = makeTerrainShape(heights, x, y, size, verts);
        if (shape == nullptr)
            return;
        JPH::BodyCreationSettings settings(
            shape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), JPH::EMotionType::Static, sLayerStatic);
        const JPH::BodyID id = mImpl->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (id.IsInvalid())
        {
            Log(Debug::Warning) << "Jolt: no room for terrain " << x << "," << y;
            return;
        }
        mImpl->mTerrain[{ x, y }] = id;
        mImpl->mOptimizePending = true;
    }

    void JoltWorld::removeHeightField(int x, int y)
    {
        auto found = mImpl->mTerrain.find({ x, y });
        if (found == mImpl->mTerrain.end())
            return;
        mImpl->destroy(found->second);
        mImpl->mTerrain.erase(found);
    }

    void JoltWorld::addStatic(const MWWorld::Ptr& ptr, const std::shared_ptr<const Resource::BulletShape>& source,
        const osg::Vec3f& position, const osg::Quat& rotation, float scale)
    {
        mImpl->removeStatic(ptr.mRef);
        JPH::ShapeRefC shape = mImpl->shapeFor(source);
        if (shape == nullptr)
            return;
        JPH::BodyCreationSettings settings(
            scaled(shape, scale), toJolt(position), toJolt(rotation), JPH::EMotionType::Static, sLayerStatic);
        const JPH::BodyID id = mImpl->bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (id.IsInvalid())
            return;
        mImpl->mStatics[ptr.mRef] = Impl::Static{ id, shape };
        mImpl->mOptimizePending = true;
    }

    void JoltWorld::setStaticPosition(const MWWorld::Ptr& ptr, const osg::Vec3f& position)
    {
        if (auto found = mImpl->mStatics.find(ptr.mRef); found != mImpl->mStatics.end())
            mImpl->bodies().SetPosition(found->second.mBody, toJolt(position), JPH::EActivation::DontActivate);
    }

    void JoltWorld::setStaticRotation(const MWWorld::Ptr& ptr, const osg::Quat& rotation)
    {
        if (auto found = mImpl->mStatics.find(ptr.mRef); found != mImpl->mStatics.end())
            mImpl->bodies().SetRotation(found->second.mBody, toJolt(rotation), JPH::EActivation::DontActivate);
    }

    void JoltWorld::setStaticScale(const MWWorld::Ptr& ptr, float scale)
    {
        if (auto found = mImpl->mStatics.find(ptr.mRef); found != mImpl->mStatics.end())
            mImpl->bodies().SetShape(
                found->second.mBody, scaled(found->second.mShape, scale), false, JPH::EActivation::DontActivate);
    }

    bool JoltWorld::addDynamic(const MWWorld::Ptr& ptr, const osg::Vec3f& halfExtents, const osg::Vec3f& centre,
        const osg::Vec3f& position, const osg::Quat& rotation, const osg::Vec3f& velocity)
    {
        mImpl->removeDynamic(ptr.mRef);
        mImpl->removeStatic(ptr.mRef);

        const JPH::Vec3 half
            = JPH::Vec3::sMax(JPH::Vec3(halfExtents.x(), halfExtents.y(), halfExtents.z()) / sJoltUnitsPerMetre,
                JPH::Vec3::sReplicate(0.02f));
        JPH::ShapeRefC box = new JPH::BoxShape(half, std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f));
        JPH::ShapeRefC shape = new JPH::RotatedTranslatedShape(
            JPH::Vec3(centre.x(), centre.y(), centre.z()) / sJoltUnitsPerMetre, JPH::Quat::sIdentity(), box);

        JPH::BodyCreationSettings settings(
            shape, toJolt(position), toJolt(rotation), JPH::EMotionType::Dynamic, sLayerMoving);
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast; // small items must not tunnel through floors
        settings.mFriction = 0.6f;
        settings.mRestitution = 0.25f;
        settings.mLinearVelocity = JPH::Vec3(velocity.x(), velocity.y(), velocity.z()) / sJoltUnitsPerMetre;
        const JPH::BodyID id = mImpl->bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (id.IsInvalid())
            return false;
        mImpl->mDynamics[ptr.mRef] = Impl::Dynamic{ ptr, id };
        return true;
    }

    bool JoltWorld::addDynamicCapsule(const MWWorld::Ptr& ptr, float radius, float halfHeight,
        const osg::Vec3f& position, const osg::Quat& rotation, const osg::Vec3f& velocity)
    {
        constexpr std::size_t maxPieces = 16;
        std::size_t pieces = 0;
        auto oldest = mImpl->mDynamics.end();
        for (auto it = mImpl->mDynamics.begin(); it != mImpl->mDynamics.end(); ++it)
        {
            if (it->second.mPieceOrder == 0)
                continue;
            ++pieces;
            if (oldest == mImpl->mDynamics.end() || it->second.mPieceOrder < oldest->second.mPieceOrder)
                oldest = it;
        }
        if (pieces >= maxPieces && oldest != mImpl->mDynamics.end())
        {
            const JPH::RVec3 p = mImpl->bodies().GetPosition(oldest->second.mBody) * sJoltUnitsPerMetre;
            const JPH::Quat q = mImpl->bodies().GetRotation(oldest->second.mBody);
            mImpl->mStopped.push_back(Moved{ oldest->second.mPtr,
                osg::Vec3f(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())),
                osg::Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()), true });
            mImpl->removeDynamic(oldest->first);
        }
        mImpl->removeStatic(ptr.mRef);
        mImpl->removeDynamic(ptr.mRef);
        const float r = std::max(radius, 1.f) / sJoltUnitsPerMetre;
        const float h = std::max(halfHeight, 0.5f) / sJoltUnitsPerMetre;
        JPH::BodyCreationSettings settings(new JPH::CapsuleShape(h, r), toJolt(position), toJolt(rotation),
            JPH::EMotionType::Dynamic, sLayerMoving);
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        settings.mFriction = 0.8f;
        settings.mRestitution = 0.1f;
        settings.mLinearDamping = 0.1f;
        settings.mAngularDamping = 2.f;
        settings.mLinearVelocity = JPH::Vec3(velocity.x(), velocity.y(), velocity.z()) / sJoltUnitsPerMetre;
        settings.mAngularVelocity = JPH::Vec3(velocity.y(), -velocity.x(), 0.f) / (sJoltUnitsPerMetre * 4.f);
        const JPH::BodyID id = mImpl->bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (id.IsInvalid())
            return false;
        mImpl->mDynamics[ptr.mRef] = Impl::Dynamic{ ptr, id, ++mImpl->mPieceCounter };
        return true;
    }

    bool JoltWorld::isDynamic(const MWWorld::Ptr& ptr) const
    {
        return mImpl->mDynamics.count(ptr.mRef) > 0;
    }

    bool JoltWorld::addRagdoll(const MWWorld::Ptr& ptr, const std::vector<RagdollBone>& bones, float dt,
        const osg::Vec3f& impulse, float massKg, RagdollMode mode)
    {
        mImpl->freezeRagdoll(ptr.mRef);
        RagdollBuild build = makeRagdoll(bones, massKg, sLayerMoving);
        if (build.mSettings == nullptr)
            return false;
        if (mImpl->mRagdolls.size() >= sMaxRagdolls)
        {
            // The oldest fall of a corpse goes first: a knocked down actor frozen in mid air would get up there
            auto oldest = std::min_element(mImpl->mRagdolls.begin(), mImpl->mRagdolls.end(), [](const auto& l, const auto& r) {
                const bool lDeath = l.second.mMode == RagdollMode::Death, rDeath = r.second.mMode == RagdollMode::Death;
                return lDeath != rDeath ? lDeath : l.second.mOrder < r.second.mOrder;
            });
            mImpl->freezeRagdoll(oldest->first);
        }
        JPH::Ref<JPH::Ragdoll> ragdoll = build.mSettings->CreateRagdoll(
            static_cast<JPH::CollisionGroup::GroupID>(++mImpl->mRagdollCounter), 0, &mImpl->mSystem);
        if (ragdoll == nullptr)
            return false;
        ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

        // Start with the bones' own motion; the blow lands on the chest
        const float invDt = dt > 1e-4f ? 1.f / dt : 0.f;
        for (std::size_t i = 0; i < build.mNames.size(); ++i)
        {
            osg::Vec3f velocity;
            for (const RagdollBone& bone : bones)
                if (Misc::StringUtils::ciEqual(bone.mName, build.mNames[i]))
                    velocity = (bone.mWorld.getTrans() - bone.mWorldPrev.getTrans()) * invDt;
            if (velocity.length() > 1000.f)
                velocity *= 1000.f / velocity.length();
            if (static_cast<int>(i) == build.mChest)
                velocity += impulse;
            mImpl->bodies().SetLinearVelocity(ragdoll->GetBodyID(static_cast<int>(i)),
                JPH::Vec3(velocity.x(), velocity.y(), velocity.z()) / sJoltUnitsPerMetre);
        }

        Impl::Ragdoll& entry = mImpl->mRagdolls[ptr.mRef];
        entry.mPtr = ptr;
        entry.mRagdoll = ragdoll;
        entry.mNames = std::move(build.mNames);
        entry.mScales = std::move(build.mScales);
        entry.mChest = build.mChest;
        entry.mOrder = mImpl->mRagdollCounter;
        entry.mMode = mode;
        return true;
    }

    float JoltWorld::chestUpZ(const MWWorld::Ptr& ptr) const
    {
        auto found = mImpl->mRagdolls.find(ptr.mRef);
        if (found == mImpl->mRagdolls.end())
            return 0.f;
        return mImpl->axisAndFacing(found->second).second.z();
    }

    bool JoltWorld::rollToBack(const MWWorld::Ptr& ptr)
    {
        auto found = mImpl->mRagdolls.find(ptr.mRef);
        if (found == mImpl->mRagdolls.end() || mImpl->axisAndFacing(found->second).second.z() >= -0.3f)
            return false;
        found->second.mRollLeft = sRollTime;
        found->second.mRagdoll->Activate();
        return true;
    }

    bool JoltWorld::applyRagdollImpulse(const MWWorld::Ptr& ptr, const osg::Vec3f& impulse, std::string_view bone)
    {
        auto found = mImpl->mRagdolls.find(ptr.mRef);
        if (found == mImpl->mRagdolls.end())
            return false;
        Impl::Ragdoll& ragdoll = found->second;
        int index = ragdoll.mChest;
        for (std::size_t i = 0; i < ragdoll.mNames.size(); ++i)
            if (!bone.empty() && Misc::StringUtils::ciEqual(ragdoll.mNames[i], bone))
                index = static_cast<int>(i);
        mImpl->bodies().AddLinearVelocity(ragdoll.mRagdoll->GetBodyID(index),
            JPH::Vec3(impulse.x(), impulse.y(), impulse.z()) / sJoltUnitsPerMetre);
        ragdoll.mAge = 0.f;
        return true;
    }

    bool JoltWorld::isRagdollActive(const MWWorld::Ptr& ptr) const
    {
        return mImpl->mRagdolls.count(ptr.mRef) > 0;
    }

    void JoltWorld::freezeRagdoll(const MWWorld::Ptr& ptr)
    {
        mImpl->freezeRagdoll(ptr.mRef);
    }

    std::vector<JoltWorld::RagdollPose> JoltWorld::takeRagdollPoses()
    {
        return std::exchange(mImpl->mPoses, {});
    }

    void JoltWorld::dropRagdoll(const MWWorld::Ptr& ptr)
    {
        mImpl->freezeRagdoll(ptr.mRef, false);
        std::erase_if(mImpl->mPoses, [&](const RagdollPose& pose) { return pose.mPtr.mRef == ptr.mRef; });
    }

    void JoltWorld::remove(const MWWorld::Ptr& ptr)
    {
        dropRagdoll(ptr);
        mImpl->removeStatic(ptr.mRef);
        mImpl->removeDynamic(ptr.mRef);
    }

    void JoltWorld::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        if (old.mRef == updated.mRef)
        {
            if (auto found = mImpl->mDynamics.find(old.mRef); found != mImpl->mDynamics.end())
                found->second.mPtr = updated;
            return;
        }
        if (auto found = mImpl->mStatics.find(old.mRef); found != mImpl->mStatics.end())
        {
            Impl::Static body = found->second;
            mImpl->mStatics.erase(found);
            mImpl->mStatics[updated.mRef] = body;
        }
        if (auto found = mImpl->mDynamics.find(old.mRef); found != mImpl->mDynamics.end())
        {
            const JPH::BodyID id = found->second.mBody;
            mImpl->mDynamics.erase(found);
            mImpl->mDynamics[updated.mRef] = Impl::Dynamic{ updated, id };
        }
        if (auto found = mImpl->mRagdolls.find(old.mRef); found != mImpl->mRagdolls.end())
        {
            Impl::Ragdoll ragdoll = std::move(found->second);
            mImpl->mRagdolls.erase(found);
            ragdoll.mPtr = updated;
            mImpl->mRagdolls[updated.mRef] = std::move(ragdoll);
        }
        else if (auto same = mImpl->mRagdolls.find(updated.mRef); same != mImpl->mRagdolls.end())
            same->second.mPtr = updated;
    }

    std::vector<JoltWorld::Moved> JoltWorld::step(float dt)
    {
        std::vector<Moved> moved = std::exchange(mImpl->mStopped, {});
        if (mImpl->mDynamics.empty() && mImpl->mRagdolls.empty())
        {
            mImpl->mTimeAccum = 0.f;
            return moved;
        }
        if (mImpl->mOptimizePending)
        {
            mImpl->mSystem.OptimizeBroadPhase();
            mImpl->mOptimizePending = false;
        }

        mImpl->mTimeAccum += dt;
        int steps = 0;
        while (mImpl->mTimeAccum >= sStep && steps < sMaxSteps)
        {
            mImpl->mSystem.Update(sStep, 1, mImpl->mTempAllocator.get(), mImpl->mJobs.get());
            mImpl->mTimeAccum -= sStep;
            ++steps;
        }
        mImpl->mTimeAccum = std::min(mImpl->mTimeAccum, sStep);
        if (steps == 0)
            return moved;

        JPH::BodyInterface& bodies = mImpl->bodies();
        std::vector<Impl::Key> stopped;
        for (auto& [key, ragdoll] : mImpl->mRagdolls)
        {
            ragdoll.mAge += steps * sStep;
            if (ragdoll.mRollLeft > 0.f)
            {
                ragdoll.mRollLeft = std::max(ragdoll.mRollLeft - steps * sStep, 0.f);
                if (ragdoll.mRollLeft > 0.f)
                    mImpl->rollStep(ragdoll);
            }
            RagdollPose pose = mImpl->poseOf(ragdoll, false);
            pose.mAsleep = !ragdoll.mRagdoll->IsActive();
            // First hard landing: the downward speed of pelvis or chest collapses
            float down = 0.f;
            for (int i : { 0, ragdoll.mChest })
                down = std::max(down,
                    -bodies.GetLinearVelocity(ragdoll.mRagdoll->GetBodyID(i)).GetZ() * sJoltUnitsPerMetre);
            if (!ragdoll.mLanded && ragdoll.mFallSpeed > sLandingSpeed && down < ragdoll.mFallSpeed * 0.4f)
            {
                ragdoll.mLanded = true;
                pose.mFallSpeed = ragdoll.mFallSpeed;
            }
            ragdoll.mFallSpeed = down;
            if (ragdoll.mMode == RagdollMode::Death
                && (!ragdoll.mRagdoll->IsActive() || ragdoll.mAge >= sRagdollTimeout))
                stopped.push_back(key);
            else
                mImpl->mPoses.push_back(std::move(pose));
        }
        for (Impl::Key key : stopped)
            mImpl->freezeRagdoll(key);

        std::vector<Impl::Key> settled;
        moved.reserve(mImpl->mDynamics.size());
        for (auto& [key, body] : mImpl->mDynamics)
        {
            body.mAge += steps * sStep;
            const JPH::RVec3 p = bodies.GetPosition(body.mBody) * sJoltUnitsPerMetre;
            const JPH::Quat q = bodies.GetRotation(body.mBody);
            // a piece can roll on flat ground for a long time: it stops after the ragdoll timeout
            const bool asleep
                = !bodies.IsActive(body.mBody) || (body.mPieceOrder != 0 && body.mAge >= sRagdollTimeout);
            moved.push_back(Moved{ body.mPtr,
                osg::Vec3f(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())),
                osg::Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()), asleep });
            if (asleep)
                settled.push_back(key);
        }
        for (Impl::Key key : settled)
            mImpl->removeDynamic(key);
        return moved;
    }
}
