#ifndef OPENMW_MWRENDER_RAGDOLLPOSE_H
#define OPENMW_MWRENDER_RAGDOLLPOSE_H

// NerevarMoon: bone transforms written by the Jolt ragdoll in place of the animation controllers.

#include <optional>

#include <osg/Matrixf>
#include <osg/NodeCallback>

namespace MWRender
{
    /// Local matrix of a node whose world matrix is `world`, under a parent whose world matrix is `parentWorld`.
    osg::Matrixf worldToLocal(const osg::Matrixf& world, const osg::Matrixf& parentWorld);

    /// Replaces a bone's update callbacks: the bone takes a world pose (converted against its parent's world matrix
    /// of this frame, so the actor's root may move) or a fixed local pose (loaded from a save).
    class RagdollBoneCallback : public osg::NodeCallback
    {
    public:
        void setWorld(const osg::Matrixf& world)
        {
            mWorld = world;
            mLocal.reset();
        }
        void setLocal(const osg::Matrixf& local)
        {
            mLocal = local;
            mWorld.reset();
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

    private:
        std::optional<osg::Matrixf> mWorld;
        std::optional<osg::Matrixf> mLocal;
    };
}

#endif
