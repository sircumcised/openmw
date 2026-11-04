#ifndef OPENMW_MWRENDER_RAGDOLLPOSE_H
#define OPENMW_MWRENDER_RAGDOLLPOSE_H

// NerevarMoon: bone transforms written by the Jolt ragdoll in place of the animation controllers.

#include <optional>

#include <osg/Matrixf>
#include <osg/NodeCallback>
#include <osg/observer_ptr>

namespace MWRender
{
    /// Local matrix of a node whose world matrix is `world`, under a parent whose world matrix is `parentWorld`.
    osg::Matrixf worldToLocal(const osg::Matrixf& world, const osg::Matrixf& parentWorld);

    /// Replaces a bone's update callbacks: the bone takes a pose relative to `base` (the actor's base node), so it
    /// follows the actor wherever it is moved, or keeps a fixed local matrix until the first pose arrives.
    class RagdollBoneCallback : public osg::NodeCallback
    {
    public:
        void setRelative(const osg::Matrixf& relative, osg::Node* base)
        {
            mRelative = relative;
            mBase = base;
            mLocal.reset();
        }
        void setLocal(const osg::Matrixf& local)
        {
            mLocal = local;
            mRelative.reset();
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

    private:
        std::optional<osg::Matrixf> mRelative;
        osg::observer_ptr<osg::Node> mBase;
        std::optional<osg::Matrixf> mLocal;
    };
}

#endif
