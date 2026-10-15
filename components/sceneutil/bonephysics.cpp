#include "bonephysics.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <osg/NodeVisitor>
#include <osgAnimation/Bone>
#include <yaml-cpp/yaml.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "skeleton.hpp"
#include "visitor.hpp"

namespace SceneUtil
{
    namespace
    {
        constexpr float sGravityUnits = 8.96f * 69.99125109f; // Constants::GravityConst * UnitsPerMeter
        constexpr float sMaxDt = 0.05f;
        constexpr float sTeleportDistance = 150.f;
        constexpr float sStepDt = 1.f / 120.f; // fixed solver step: the chain behaves the same at any frame rate
        constexpr int sMaxSteps = 4;

        BonePhysicsController::WorldRay& worldRay()
        {
            static BonePhysicsController::WorldRay ray;
            return ray;
        }

        osg::Vec3f readVec3(const YAML::Node& node, const osg::Vec3f& fallback)
        {
            if (!node || !node.IsSequence() || node.size() != 3)
                return fallback;
            return osg::Vec3f(node[0].as<float>(), node[1].as<float>(), node[2].as<float>());
        }

        float readClamped(const YAML::Node& node, float fallback, float min, float max)
        {
            if (!node)
                return fallback;
            return std::clamp(node.as<float>(), min, max);
        }

        osg::MatrixTransform* findChildTransform(osg::Group* parent, std::string_view name)
        {
            FindByNameVisitor visitor(name);
            parent->accept(visitor);
            if (visitor.mFoundNode == nullptr)
                return nullptr;
            osg::MatrixTransform* transform = visitor.mFoundNode->asTransform()
                ? visitor.mFoundNode->asTransform()->asMatrixTransform()
                : nullptr;
            return transform;
        }

        void setBoneMatrix(osg::MatrixTransform* node, const osg::Matrixf& matrix)
        {
            node->setMatrix(matrix);
            // skinned parts: keep the skeleton-space matrix in step (same as RotateController)
            if (osgAnimation::Bone* bone = dynamic_cast<osgAnimation::Bone*>(node))
            {
                osg::Matrixf skeletonMatrix = matrix;
                if (osgAnimation::Bone* parent = bone->getBoneParent())
                    skeletonMatrix *= parent->getMatrixInSkeletonSpace();
                bone->setMatrixInSkeletonSpace(skeletonMatrix);
            }
        }
    }

    std::optional<BonePhysicsSpec> BonePhysicsSpec::parse(const std::string& yaml, std::string_view sourceName)
    {
        YAML::Node root;
        try
        {
            root = YAML::Load(yaml);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Bone physics: can't parse '" << sourceName << "': " << e.what();
            return std::nullopt;
        }
        if (!root.IsDefined() || root.IsNull() || !root["chains"])
        {
            Log(Debug::Error) << "Bone physics: '" << sourceName << "' has no 'chains' list";
            return std::nullopt;
        }
        BonePhysicsSpec spec;
        for (const YAML::Node& it : root["chains"])
        {
            Chain chain;
            if (!it["bones"] || !it["bones"].IsSequence() || it["bones"].size() == 0)
            {
                Log(Debug::Error) << "Bone physics: a chain in '" << sourceName << "' has no 'bones'";
                continue;
            }
            for (const YAML::Node& bone : it["bones"])
                chain.mBones.push_back(bone.as<std::string>());
            if (it["tip"])
                chain.mTip = readVec3(it["tip"], osg::Vec3f());
            chain.mDamping = readClamped(it["damping"], chain.mDamping, 0.f, 1.f);
            chain.mElasticity = readClamped(it["elasticity"], chain.mElasticity, 0.f, 1.f);
            chain.mStiffness = readClamped(it["stiffness"], chain.mStiffness, 0.f, 1.f);
            chain.mGravity = readClamped(it["gravity"], chain.mGravity, -10.f, 10.f);
            chain.mInertia = readClamped(it["inertia"], chain.mInertia, 0.f, 1.f);
            chain.mRadius = readClamped(it["radius"], chain.mRadius, 0.f, 100.f);
            if (it["world"])
                chain.mWorld = it["world"].as<bool>();
            spec.mChains.push_back(std::move(chain));
        }
        if (root["colliders"])
        {
            for (const YAML::Node& it : root["colliders"])
            {
                if (!it["bone"])
                    continue;
                Collider collider;
                collider.mBone = it["bone"].as<std::string>();
                collider.mRadius = readClamped(it["radius"], collider.mRadius, 0.f, 500.f);
                collider.mOffset = readVec3(it["offset"], osg::Vec3f());
                collider.mEnd = readVec3(it["end"], collider.mOffset);
                spec.mColliders.push_back(std::move(collider));
            }
        }
        if (spec.mChains.empty())
            return std::nullopt;
        return spec;
    }

    int attachBonePhysics(osg::Node* model, const VFS::Manager& vfs, std::string_view modelPath)
    {
        const VFS::Path::Normalized configPath(std::string(modelPath) + ".physics.yaml");
        if (!vfs.exists(configPath))
            return 0;
        std::string rawYaml(std::istreambuf_iterator<char>(*vfs.get(configPath)), {});
        std::optional<BonePhysicsSpec> spec = BonePhysicsSpec::parse(rawYaml, configPath.value());
        if (!spec)
            return 0;
        osg::Group* group = model->asGroup();
        if (group == nullptr)
            return 0;
        int attached = 0;
        for (const BonePhysicsSpec::Chain& chain : spec->mChains)
        {
            bool ok = true;
            std::vector<osg::MatrixTransform*> nodes;
            for (const std::string& name : chain.mBones)
            {
                osg::MatrixTransform* node = findChildTransform(group, name);
                if (node == nullptr)
                {
                    Log(Debug::Warning) << "Bone physics: bone '" << name << "' not found in '" << modelPath << "'";
                    ok = false;
                    break;
                }
                nodes.push_back(node);
            }
            if (!ok)
                continue;
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                // the solver owns these nodes: no keyframe controllers, no flattening by the optimizer
                nodes[i]->setUpdateCallback(nullptr);
                nodes[i]->setDataVariance(osg::Object::DYNAMIC);
                nodes[i]->addUpdateCallback(new BonePhysicsController(chain, spec->mColliders, i == 0));
            }
            ++attached;
        }
        if (attached > 0)
            Log(Debug::Info) << "Bone physics: " << attached << " chain(s) attached to '" << modelPath << "'";
        return attached;
    }

    BonePhysicsController::BonePhysicsController(
        const BonePhysicsSpec::Chain& chain, const std::vector<BonePhysicsSpec::Collider>& colliders, bool root)
        : mChain(chain)
        , mColliderSpecs(colliders)
        , mRoot(root)
    {
    }

    BonePhysicsController::BonePhysicsController(const BonePhysicsController& copy, const osg::CopyOp& copyop)
        : NodeCallback<BonePhysicsController, osg::MatrixTransform*>(copy, copyop)
        , mChain(copy.mChain)
        , mColliderSpecs(copy.mColliderSpecs)
        , mRoot(copy.mRoot)
    {
        // runtime state is per instance: resolved again on the first update of the copy
    }

    bool BonePhysicsController::resolve(osg::MatrixTransform* node)
    {
        mNodes.clear();
        mRestLocal.clear();
        mRestRot.clear();
        mRestOffsets.clear();
        mRestLengths.clear();
        mColliders.clear();
        mNodes.push_back(node);
        for (std::size_t i = 1; i < mChain.mBones.size(); ++i)
        {
            osg::MatrixTransform* child = findChildTransform(mNodes.back().get(), mChain.mBones[i]);
            if (child == nullptr || child->getNumParents() == 0 || child->getParent(0) != mNodes.back().get())
            {
                Log(Debug::Warning) << "Bone physics: '" << mChain.mBones[i] << "' is not a direct child of '"
                                    << mChain.mBones[i - 1] << "', chain disabled";
                return false;
            }
            mNodes.push_back(child);
        }
        const std::size_t n = mNodes.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            mRestLocal.push_back(mNodes[i]->getMatrix());
            mRestRot.push_back(mRestLocal.back().getRotate());
        }
        for (std::size_t i = 0; i + 1 < n; ++i)
            mRestOffsets.push_back(mRestLocal[i + 1].getTrans());
        osg::Vec3f tip;
        if (mChain.mTip)
            tip = *mChain.mTip;
        else if (!mRestOffsets.empty())
            tip = mRestOffsets.back();
        else
            tip = osg::Vec3f(0.f, 1.f, 0.f);
        if (tip.length2() < 1e-6f)
            tip = osg::Vec3f(0.f, 1.f, 0.f);
        mRestOffsets.push_back(tip);
        for (const osg::Vec3f& offset : mRestOffsets)
            mRestLengths.push_back(std::max(offset.length(), 1e-3f));
        mParticles.assign(n + 1, Particle{});

        // colliders live on the surrounding skeleton, if any
        if (!mColliderSpecs.empty())
        {
            osg::NodePathList paths = node->getParentalNodePaths();
            Skeleton* skeleton = nullptr;
            if (!paths.empty())
            {
                for (auto it = paths[0].rbegin(); it != paths[0].rend(); ++it)
                {
                    skeleton = dynamic_cast<Skeleton*>(*it);
                    if (skeleton != nullptr)
                        break;
                }
            }
            if (skeleton != nullptr)
            {
                for (const BonePhysicsSpec::Collider& spec : mColliderSpecs)
                {
                    FindByNameVisitor visitor(spec.mBone);
                    skeleton->accept(visitor);
                    if (visitor.mFoundNode == nullptr)
                        continue;
                    ColliderInstance instance;
                    instance.mNode = visitor.mFoundNode;
                    instance.mRadius = spec.mRadius;
                    instance.mOffset = spec.mOffset;
                    instance.mEnd = spec.mEnd;
                    mColliders.push_back(std::move(instance));
                }
            }
        }
        return true;
    }

    void BonePhysicsController::resetState(const osg::Matrixf& mountWorld)
    {
        osg::Matrixf world = mountWorld;
        for (std::size_t i = 0; i < mNodes.size(); ++i)
        {
            world = mRestLocal[i] * world;
            mParticles[i].mPos = world.getTrans();
            mParticles[i].mPrev = mParticles[i].mPos;
            if (osg::ref_ptr<osg::MatrixTransform> boneNode; mNodes[i].lock(boneNode))
                setBoneMatrix(boneNode.get(), mRestLocal[i]);
        }
        const osg::Vec3f tip = mParticles[mNodes.size() - 1].mPos + world.getRotate() * mRestOffsets.back();
        mParticles.back().mPos = tip;
        mParticles.back().mPrev = tip;
    }

    void BonePhysicsController::updateColliders()
    {
        mColliderWorld.clear();
        for (const ColliderInstance& collider : mColliders)
        {
            osg::ref_ptr<osg::Node> node;
            if (!collider.mNode.lock(node))
                continue;
            osg::NodePathList paths = node->getParentalNodePaths();
            if (paths.empty())
                continue;
            const osg::Matrixf world = osg::computeLocalToWorld(paths[0]);
            mColliderWorld.push_back({ collider.mOffset * world, collider.mEnd * world, collider.mRadius });
        }
    }

    void BonePhysicsController::collide(osg::Vec3f& pos, float radius) const
    {
        for (const ColliderWorld& collider : mColliderWorld)
        {
            // closest point on the segment a-b
            const osg::Vec3f ab = collider.mB - collider.mA;
            float t = 0.f;
            const float ab2 = ab.length2();
            if (ab2 > 1e-6f)
                t = std::clamp(((pos - collider.mA) * ab) / ab2, 0.f, 1.f);
            const osg::Vec3f closest = collider.mA + ab * t;
            osg::Vec3f away = pos - closest;
            const float minDist = collider.mRadius + radius;
            if (away.length2() >= minDist * minDist)
                continue;
            const float dist = away.length();
            if (dist < 1e-4f)
                away = osg::Vec3f(0.f, 0.f, 1.f);
            else
                away /= dist;
            pos = closest + away * minDist;
        }
    }

    void BonePhysicsController::setWorldRay(WorldRay ray)
    {
        worldRay() = std::move(ray);
    }

    void BonePhysicsController::collideWorld(osg::Vec3f& pos, const osg::Vec3f& prev, float radius) const
    {
        const WorldRay& ray = worldRay();
        if (!ray)
            return;
        osg::Vec3f hitPos, hitNormal;
        // the particle's path this frame: through a floor or a wall
        if ((pos - prev).length2() > 1e-6f && ray(prev, pos, hitPos, hitNormal))
        {
            pos = hitPos + hitNormal * radius;
            return;
        }
        // resting: the floor just under it
        const osg::Vec3f probe(0.f, 0.f, radius + 0.5f);
        if (ray(pos + probe, pos - probe, hitPos, hitNormal))
        {
            if (hitNormal.z() > 0.3f)
                pos.z() = std::max(pos.z(), hitPos.z() + radius);
            else
                pos = hitPos + hitNormal * radius;
        }
    }

    void BonePhysicsController::integrate(
        const osg::Quat& mountRot, const osg::Vec3f& gravityStep, const osg::Vec3f& dragStep)
    {
        osg::Quat parentRot = mountRot;
        const float keep = 1.f - mChain.mDamping;
        for (std::size_t i = 0; i < mNodes.size(); ++i)
        {
            const osg::Quat animRot = mRestRot[i] * parentRot;
            const osg::Vec3f joint = mParticles[i].mPos;
            const osg::Vec3f rest = joint + animRot * mRestOffsets[i];
            Particle& next = mParticles[i + 1];

            const osg::Vec3f velocity = (next.mPos - next.mPrev) * keep;
            next.mPrev = next.mPos;
            next.mPos += velocity + gravityStep + dragStep;
            if (mChain.mElasticity > 0.f)
                next.mPos += (rest - next.mPos) * mChain.mElasticity;
            if (mChain.mStiffness > 0.f)
            {
                const osg::Vec3f bend = next.mPos - rest;
                const float maxBend = mRestLengths[i] * (1.f - mChain.mStiffness);
                const float bendLen = bend.length();
                if (bendLen > maxBend && bendLen > 1e-5f)
                    next.mPos = rest + bend * (maxBend / bendLen);
            }
            collide(next.mPos, mChain.mRadius);

            osg::Vec3f dir = next.mPos - joint;
            osg::Vec3f restDir = animRot * mRestOffsets[i];
            restDir.normalize();
            if (dir.length2() < 1e-8f)
                dir = restDir;
            else
                dir.normalize();
            next.mPos = joint + dir * mRestLengths[i];

            osg::Quat delta;
            delta.makeRotate(restDir, dir);
            parentRot = animRot * delta;
        }
    }

    void BonePhysicsController::operator()(osg::MatrixTransform* node, osg::NodeVisitor* nv)
    {
        if (!mRoot || mBroken || nv == nullptr || nv->getFrameStamp() == nullptr)
        {
            traverse(node, nv);
            return;
        }
        osg::NodePathList paths = node->getParentalNodePaths();
        if (paths.empty())
        {
            traverse(node, nv);
            return;
        }
        osg::NodePath mountPath = paths[0];
        mountPath.pop_back();
        const osg::Matrixf mount = osg::computeLocalToWorld(mountPath);
        const double now = nv->getFrameStamp()->getSimulationTime();

        if (!mResolved)
        {
            if (!resolve(node))
            {
                mBroken = true;
                traverse(node, nv);
                return;
            }
            resetState(mount);
            mResolved = true;
            mLastTime = now;
            mLastMount = mount.getTrans();
            traverse(node, nv);
            return;
        }

        const osg::Vec3f mountPos = mount.getTrans();
        const osg::Vec3f mountDelta = mountPos - mLastMount;
        if (mountDelta.length2() > sTeleportDistance * sTeleportDistance)
        {
            resetState(mount);
            mLastMount = mountPos;
            mLastTime = now;
            traverse(node, nv);
            return;
        }
        mLastMount = mountPos;
        const float dt = static_cast<float>(std::clamp(now - mLastTime, 0.0, static_cast<double>(sMaxDt)));
        mLastTime = now;
        if (dt <= 0.f)
        {
            traverse(node, nv);
            return;
        }

        // fixed 120 Hz steps, at most 4 per frame: a hitch slows the chain down instead of launching it
        mAccum += dt;
        int steps = static_cast<int>(mAccum / sStepDt);
        if (steps <= 0)
        {
            traverse(node, nv);
            return;
        }
        steps = std::min(steps, sMaxSteps);
        mAccum = std::min(mAccum - steps * sStepDt, sStepDt);

        const osg::Quat mountRot = mount.getRotate();
        const osg::Vec3f gravityStep(0.f, 0.f, -sGravityUnits * mChain.mGravity * sStepDt * sStepDt);
        const osg::Vec3f dragStep = mountDelta * (mChain.mInertia / static_cast<float>(steps));
        updateColliders();

        // the first joint follows the animation
        {
            const osg::Matrixf world = mRestLocal[0] * mount;
            mParticles[0].mPrev = mParticles[0].mPos;
            mParticles[0].mPos = world.getTrans();
        }
        mFrameStart.resize(mParticles.size());
        for (std::size_t i = 0; i < mParticles.size(); ++i)
            mFrameStart[i] = mParticles[i].mPos;

        for (int step = 0; step < steps; ++step)
            integrate(mountRot, gravityStep, dragStep);

        // once per frame: the world (floors, walls), then the final link directions and the node matrices
        const bool world = mChain.mWorld && worldRay();
        osg::Quat parentRot = mountRot;
        for (std::size_t i = 0; i < mNodes.size(); ++i)
        {
            osg::ref_ptr<osg::MatrixTransform> boneNode;
            if (!mNodes[i].lock(boneNode))
            {
                mBroken = true;
                break;
            }
            const osg::Quat animRot = mRestRot[i] * parentRot;
            const osg::Vec3f joint = mParticles[i].mPos;
            Particle& next = mParticles[i + 1];
            if (world)
                collideWorld(next.mPos, mFrameStart[i + 1], mChain.mRadius);

            osg::Vec3f restDir = animRot * mRestOffsets[i];
            restDir.normalize();
            osg::Vec3f dir = next.mPos - joint;
            if (dir.length2() < 1e-8f)
                dir = restDir;
            else
                dir.normalize();
            next.mPos = joint + dir * mRestLengths[i];

            osg::Quat delta;
            delta.makeRotate(restDir, dir);
            const osg::Quat newRot = animRot * delta;
            osg::Matrixf local = mRestLocal[i];
            local.setRotate(newRot * parentRot.inverse());
            setBoneMatrix(boneNode.get(), local);
            parentRot = newRot;
        }

        traverse(node, nv);
    }
}
