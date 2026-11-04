#ifndef OPENMW_MWRENDER_RAGDOLLPOSE_H
#define OPENMW_MWRENDER_RAGDOLLPOSE_H

// NerevarMoon: bone transforms written by the Jolt ragdoll in place of the animation controllers.

#include <functional>
#include <optional>

#include <osg/Matrixf>
#include <osg/NodeCallback>
#include <osg/observer_ptr>

namespace MWRender
{
    /// Local matrix of a node whose world matrix is `world`, under a parent whose world matrix is `parentWorld`.
    osg::Matrixf worldToLocal(const osg::Matrixf& world, const osg::Matrixf& parentWorld);

    /// Translation lerp, rotation slerp from `from` to `to` (w 0..1); scale taken from `to`.
    osg::Matrixf blendLocal(const osg::Matrixf& from, const osg::Matrixf& to, float w);

    /// Time in [start, stop] where a getup starts: the last low point of `height` (sampled every 1/30 s) below 40%
    /// of its range, before the final rise; 60% of the range of times when the track has no such point.
    float findGetupStart(const std::function<float(float)>& height, float start, float stop);

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
        /// Getup: the callback runs after the bone's animation callbacks and mixes the pose `fromRelative` (relative
        /// to `base`) into the animated pose over `duration` seconds of simulation time.
        void setBlend(const osg::Matrixf& fromRelative, osg::Node* base, float duration)
        {
            mRelative = fromRelative;
            mBase = base;
            mLocal.reset();
            mBlend = duration;
            mBlendStart.reset();
            mBlendDone = false;
        }
        bool blendDone() const { return mBlendDone; }
        const std::optional<osg::Matrixf>& relative() const { return mRelative; }

        void operator()(osg::Node* node, osg::NodeVisitor* nv) override;

    private:
        std::optional<osg::Matrixf> mRelative;
        osg::observer_ptr<osg::Node> mBase;
        std::optional<osg::Matrixf> mLocal;
        std::optional<float> mBlend; // seconds; set in blend mode
        std::optional<double> mBlendStart;
        bool mBlendDone = false;
    };
}

#endif
