#include "ragdollpose.hpp"

#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/Transform>

#include <algorithm>

namespace MWRender
{
    osg::Matrixf worldToLocal(const osg::Matrixf& world, const osg::Matrixf& parentWorld)
    {
        return world * osg::Matrixf::inverse(parentWorld);
    }

    void RagdollBoneCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        auto* transform = static_cast<osg::MatrixTransform*>(node);
        if (mLocal)
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
