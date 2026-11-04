#include "ragdollpose.hpp"

#include <osg/FrameStamp>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/Transform>

#include <algorithm>
#include <vector>

namespace MWRender
{
    osg::Matrixf worldToLocal(const osg::Matrixf& world, const osg::Matrixf& parentWorld)
    {
        return world * osg::Matrixf::inverse(parentWorld);
    }

    osg::Matrixf blendLocal(const osg::Matrixf& from, const osg::Matrixf& to, float w)
    {
        w = std::clamp(w, 0.f, 1.f);
        osg::Quat rotation;
        rotation.slerp(w, from.getRotate(), to.getRotate());
        const osg::Vec3f translation = from.getTrans() * (1.f - w) + to.getTrans() * w;
        const osg::Vec3f scale = to.getScale();
        return osg::Matrixf::scale(scale) * osg::Matrixf::rotate(rotation) * osg::Matrixf::translate(translation);
    }

    float findGetupStart(const std::function<float(float)>& height, float start, float stop)
    {
        const float fallback = start + (stop - start) * 0.6f;
        const int count = static_cast<int>((stop - start) * 30.f) + 1;
        if (count < 3)
            return fallback;
        std::vector<float> z(count);
        for (int i = 0; i < count; ++i)
            z[i] = height(start + i / 30.f);
        const auto [low, high] = std::minmax_element(z.begin(), z.end());
        if (*high - *low < 1.f)
            return fallback;
        const float threshold = *low + (*high - *low) * 0.4f;
        for (int i = count - 2; i > 0; --i)
            if (z[i] < threshold && z[i] <= z[i - 1] && z[i] <= z[i + 1])
                return start + i / 30.f;
        return fallback;
    }

    void RagdollBoneCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        auto* transform = static_cast<osg::MatrixTransform*>(node);
        if (mBlend && mRelative)
        {
            // The animation callbacks before this one already wrote the animated local matrix
            const osg::FrameStamp* stamp = nv->getFrameStamp();
            const double now = stamp ? stamp->getSimulationTime() : 0.0;
            if (!mBlendStart)
                mBlendStart = now;
            const float w
                = (stamp && *mBlend > 0.f) ? static_cast<float>((now - *mBlendStart) / *mBlend) : 1.f;
            osg::NodePath path = nv->getNodePath();
            if (!path.empty() && path.back() == node)
                path.pop_back();
            osg::ref_ptr<osg::Node> base;
            mBase.lock(base);
            auto at = base ? std::find(path.begin(), path.end(), base.get()) : path.end();
            if (w >= 0.999f || at == path.end()) // 0.999: simulation time sums lose the last digit
                mBlendDone = true;
            else
            {
                const osg::Matrixf baseWorld = osg::computeLocalToWorld(osg::NodePath(path.begin(), at + 1));
                const osg::Matrixf from = worldToLocal(*mRelative * baseWorld, osg::computeLocalToWorld(path));
                transform->setMatrix(blendLocal(from, transform->getMatrix(), w));
            }
        }
        else if (mLocal)
            transform->setMatrix(*mLocal);
        else if (mRelative)
        {
            // The visitor's path ends at this node; the ancestors are already updated this frame
            osg::NodePath path = nv->getNodePath();
            if (!path.empty() && path.back() == node)
                path.pop_back();
            osg::ref_ptr<osg::Node> base;
            mBase.lock(base);
            auto at = base ? std::find(path.begin(), path.end(), base.get()) : path.end();
            if (at != path.end())
            {
                const osg::Matrixf baseWorld = osg::computeLocalToWorld(osg::NodePath(path.begin(), at + 1));
                transform->setMatrix(worldToLocal(*mRelative * baseWorld, osg::computeLocalToWorld(path)));
            }
        }
        traverse(node, nv);
    }
}
