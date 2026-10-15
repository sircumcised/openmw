#ifndef OPENMW_COMPONENTS_SCENEUTIL_BONEPHYSICS_H
#define OPENMW_COMPONENTS_SCENEUTIL_BONEPHYSICS_H

// NerevarMoon: secondary motion on bone chains (chains, ropes, straps, pendants) driven by a position based
// solver with gravity, damping, elasticity towards the animated pose and sphere/capsule colliders attached to
// skeleton bones. Configured by a YAML file next to the model: "<model>.physics.yaml".

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <osg/MatrixTransform>
#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/observer_ptr>

#include "nodecallback.hpp"

namespace VFS
{
    class Manager;
}

namespace SceneUtil
{
    struct BonePhysicsSpec
    {
        struct Chain
        {
            std::vector<std::string> mBones; // first free bone to last, each a direct child of the previous
            std::optional<osg::Vec3f> mTip; // offset of a virtual end particle in the last bone's space
            float mDamping = 0.1f; // 0..1, velocity lost per frame
            float mElasticity = 0.05f; // 0..1, pull towards the animated pose per frame
            float mStiffness = 0.f; // 0..1, how far from the animated pose the chain may bend
            float mGravity = 1.f; // multiple of the game gravity (627 units/s^2)
            float mInertia = 1.f; // 0..1, share of the mount's motion the chain inherits (1 = full)
            float mRadius = 1.f; // particle radius against colliders
            bool mWorld = true; // collide with the world (floor, walls) through the engine's ray hook
        };
        struct Collider
        {
            std::string mBone; // bone of the surrounding skeleton
            float mRadius = 10.f;
            osg::Vec3f mOffset; // in the bone's space
            osg::Vec3f mEnd; // capsule: second end in the bone's space (same as offset = sphere)
        };
        std::vector<Chain> mChains;
        std::vector<Collider> mColliders;

        static std::optional<BonePhysicsSpec> parse(const std::string& yaml, std::string_view sourceName);
    };

    /// Loads "<modelPath>.physics.yaml" if it exists and attaches the controllers to the model's chain bones.
    /// Call on a freshly loaded template, before optimization. Returns the number of chains attached.
    int attachBonePhysics(osg::Node* model, const VFS::Manager& vfs, std::string_view modelPath);

    /// Runs one chain. Attached to the first bone of the chain (root role); copies of this callback on the other
    /// bones do nothing but keep them out of the optimizer and free of keyframe controllers.
    class BonePhysicsController : public NodeCallback<BonePhysicsController, osg::MatrixTransform*>
    {
    public:
        BonePhysicsController() = default;
        BonePhysicsController(
            const BonePhysicsSpec::Chain& chain, const std::vector<BonePhysicsSpec::Collider>& colliders, bool root);
        BonePhysicsController(const BonePhysicsController& copy, const osg::CopyOp& copyop);

        META_Object(SceneUtil, BonePhysicsController)

        void operator()(osg::MatrixTransform* node, osg::NodeVisitor* nv);

        /// Ray against the world set by the engine (physics system). Returns true on a hit and fills the hit
        /// point and normal. Called from the main thread during the update traversal.
        using WorldRay = std::function<bool(const osg::Vec3f& from, const osg::Vec3f& to, osg::Vec3f& hitPos,
            osg::Vec3f& hitNormal)>;
        static void setWorldRay(WorldRay ray);

        /// Snap the chain back to the animated pose on the next update.
        void reset() { mResolved = false; }

    private:
        struct Particle
        {
            osg::Vec3f mPos;
            osg::Vec3f mPrev;
        };
        struct ColliderInstance
        {
            osg::observer_ptr<osg::Node> mNode;
            float mRadius = 0.f;
            osg::Vec3f mOffset;
            osg::Vec3f mEnd;
        };

        bool resolve(osg::MatrixTransform* node);
        void resetState(const osg::Matrixf& mountWorld);
        void collide(osg::Vec3f& pos, float radius) const;
        void collideWorld(osg::Vec3f& pos, const osg::Vec3f& prev, float radius) const;

        BonePhysicsSpec::Chain mChain;
        std::vector<BonePhysicsSpec::Collider> mColliderSpecs;
        bool mRoot = false;

        // runtime, per instance
        bool mResolved = false;
        bool mBroken = false;
        std::vector<osg::observer_ptr<osg::MatrixTransform>> mNodes;
        std::vector<osg::Matrixf> mRestLocal; // the bones' own (unsimulated) local matrices
        std::vector<osg::Vec3f> mRestOffsets; // offset of particle i+1 in bone i's space
        std::vector<float> mRestLengths;
        std::vector<Particle> mParticles; // size = bones + 1
        std::vector<ColliderInstance> mColliders;
        double mLastTime = -1.0;
        osg::Vec3f mLastMount;
    };
}

#endif
