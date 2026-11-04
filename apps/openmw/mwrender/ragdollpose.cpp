#include "ragdollpose.hpp"

#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/Transform>

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
        else if (mWorld)
        {
            // The visitor's path ends at this node; the ancestors are already updated this frame
            osg::NodePath path = nv->getNodePath();
            if (!path.empty() && path.back() == node)
                path.pop_back();
            transform->setMatrix(worldToLocal(*mWorld, osg::computeLocalToWorld(path)));
        }
        traverse(node, nv);
    }
}
