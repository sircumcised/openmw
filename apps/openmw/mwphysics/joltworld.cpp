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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>

#include <components/debug/debuglog.hpp>
#include <components/resource/bulletshape.hpp>

#include "joltshapes.hpp"

namespace MWPhysics
{
    namespace
    {
        constexpr JPH::ObjectLayer sLayerStatic = 0;
        constexpr JPH::ObjectLayer sLayerMoving = 1;
        constexpr float sStep = 1.f / 60.f;
        constexpr int sMaxSteps = 4;

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
        // One converted shape per collision mesh; the ref_ptr keeps the key's BulletShape alive
        std::map<const Resource::BulletShape*, std::pair<osg::ref_ptr<const Resource::BulletShape>, JPH::ShapeRefC>>
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

        JPH::ShapeRefC shapeFor(const Resource::BulletShape& source)
        {
            if (auto found = mShapes.find(&source); found != mShapes.end())
                return found->second.second;
            JPH::ShapeRefC shape;
            if (source.mCollisionShape)
                shape = convertBulletShape(*source.mCollisionShape, mShapeStats);
            mShapes.emplace(&source, std::make_pair(osg::ref_ptr<const Resource::BulletShape>(&source), shape));
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

    void JoltWorld::addStatic(const MWWorld::Ptr& ptr, const Resource::BulletShape& source,
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

    bool JoltWorld::isDynamic(const MWWorld::Ptr& ptr) const
    {
        return mImpl->mDynamics.count(ptr.mRef) > 0;
    }

    void JoltWorld::remove(const MWWorld::Ptr& ptr)
    {
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
    }

    std::vector<JoltWorld::Moved> JoltWorld::step(float dt)
    {
        std::vector<Moved> moved;
        if (mImpl->mDynamics.empty())
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
        std::vector<Impl::Key> settled;
        moved.reserve(mImpl->mDynamics.size());
        for (const auto& [key, body] : mImpl->mDynamics)
        {
            const JPH::RVec3 p = bodies.GetPosition(body.mBody) * sJoltUnitsPerMetre;
            const JPH::Quat q = bodies.GetRotation(body.mBody);
            const bool asleep = !bodies.IsActive(body.mBody);
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
