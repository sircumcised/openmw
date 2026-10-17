#ifndef OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOW_H
#define OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOW_H

#include <osg/Group>
#include <osg/Texture2DArray>
#include <osg/ref_ptr>

#include <components/shader/shadermanager.hpp>

namespace Settings
{
    struct ShadowsCategory;
}

namespace SceneUtil
{
    class LightManager;

    /// Indoor shadows cast by the strongest point lights: one depth cube (6 layers of a Texture2DArray) per slot,
    /// statics cached per light, actors re-rendered each frame.
    class PointShadowManager
    {
    public:
        PointShadowManager(osg::ref_ptr<osg::Group> rootNode, LightManager& lightManager, unsigned int staticMask,
            unsigned int dynamicMask, const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager);
        ~PointShadowManager();

        bool isValid() const { return mValid; }

        void setActive(bool active);
        bool isActive() const { return mActive; }

        int getStaticUnit() const { return mStaticUnit; }
        int getDynamicUnit() const { return mDynamicUnit; }

        /// Defines for shaders; all present with pointShadows = 0 when the manager is not valid.
        Shader::ShaderManager::DefineMap getDefines(const Settings::ShadowsCategory& settings) const;
        static Shader::ShaderManager::DefineMap getDisabledDefines();

    private:
        osg::ref_ptr<osg::Texture2DArray> createDepthArray(int resolution) const;

        osg::ref_ptr<osg::Group> mRootNode;
        LightManager& mLightManager;
        unsigned int mStaticMask;
        unsigned int mDynamicMask;
        bool mValid = false;
        bool mActive = false;
        int mStaticUnit = -1;
        int mDynamicUnit = -1;
        int mMaxSlots = 0;
        osg::ref_ptr<osg::Texture2DArray> mStaticMaps;
        osg::ref_ptr<osg::Texture2DArray> mDynamicMaps;
    };
}

#endif
