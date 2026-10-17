// NerevarMoon: indoor point light shadows.
#ifndef OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOW_H
#define OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOW_H

#include <array>
#include <unordered_map>

#include <osg/Camera>
#include <osg/Group>
#include <osg/observer_ptr>
#include <osg/Texture2D>
#include <osg/ref_ptr>

#include <components/shader/shadermanager.hpp>

#include "pointshadowmath.hpp"

namespace osgUtil
{
    class CullVisitor;
}

namespace Settings
{
    struct ShadowsCategory;
}

namespace SceneUtil
{
    class LightManager;

    /// Indoor shadows cast by the strongest point lights: one depth cube per slot, statics cached per light, actors
    /// re-rendered each frame. Each map is a 2D atlas, 6 faces wide and 4 slots high, so the shaders need no texture
    /// array extension.
    class PointShadowManager
    {
    public:
        /// staticMask / dynamicMask: camera cull masks for the cached and the per-frame cube, including the scene
        /// root's own mask. shadowsBinStateSet: MWShadowTechnique's bin that swaps in the alpha-tested casting programs.
        PointShadowManager(osg::ref_ptr<osg::Group> rootNode, LightManager& lightManager, unsigned int staticMask,
            unsigned int dynamicMask, osg::ref_ptr<osg::StateSet> shadowsBinStateSet,
            const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager);
        ~PointShadowManager();

        bool isValid() const { return mValid; }

        void setActive(bool active);
        bool isActive() const { return mActive; }

        int getStaticUnit() const { return mStaticUnit; }
        int getDynamicUnit() const { return mDynamicUnit; }

        /// Defines for shaders; all present with pointShadows = 0 when the manager is not valid.
        Shader::ShaderManager::DefineMap getDefines(const Settings::ShadowsCategory& settings) const;
        static Shader::ShaderManager::DefineMap getDisabledDefines();

        /// Uniforms the scene camera pushes around the light manager's subgraph; double-buffered for the draw thread.
        osg::StateSet* getUniformStateSet(unsigned int frameNumber) const { return mUniformStateSets[frameNumber % 2]; }

        /// After the scene camera culled the scene (so this frame's LightBuffer indices exist): picks the slots,
        /// culls the shadow cameras and fills the uniforms.
        void update(osgUtil::CullVisitor* cv);

    private:
        struct LightInfo
        {
            osg::Vec3f mPosition;
            float mRadius;
            bool mMoved;
        };

        /// Where a light hangs in the scene, looked up once per light.
        struct LightOrigin
        {
            bool mCarried;
            osg::observer_ptr<osg::Node> mOwner;
        };

        osg::ref_ptr<osg::Texture2D> createDepthAtlas(int resolution) const;
        osg::ref_ptr<osg::StateSet> createCastingStateSet(
            const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager) const;
        osg::ref_ptr<osg::Camera> createCamera(
            osg::Texture2D* target, int slot, int face, unsigned int cullMask, int resolution) const;
        osg::ref_ptr<osg::StateSet> createUniformStateSet() const;
        void cullCamera(osg::Camera& camera, int face, const osg::Vec3f& lightPos, float far, unsigned int mask,
            osgUtil::CullVisitor* cv) const;
        static void clearUniforms(osg::StateSet& uniforms);

        osg::ref_ptr<osg::Group> mRootNode;
        LightManager& mLightManager;
        unsigned int mStaticMask;
        unsigned int mDynamicMask;
        bool mValid = false;
        bool mActive = false;
        int mStaticUnit = -1;
        int mDynamicUnit = -1;
        int mMaxSlots = 0;
        osg::ref_ptr<osg::Texture2D> mStaticMaps;
        osg::ref_ptr<osg::Texture2D> mDynamicMaps;
        osg::ref_ptr<osg::StateSet> mCastingStateSet;
        osg::ref_ptr<osg::StateSet> mShadowsBinStateSet;
        osg::ref_ptr<osg::Callback> mCasterCallback;
        osg::ref_ptr<osg::Callback> mCullCallback;
        std::array<std::array<osg::ref_ptr<osg::Camera>, 6>, PointShadow::sMaxSlots> mStaticCameras;
        std::array<std::array<osg::ref_ptr<osg::Camera>, 6>, PointShadow::sMaxSlots> mDynamicCameras;
        std::array<osg::ref_ptr<osg::StateSet>, 2> mUniformStateSets;
        std::array<PointShadow::SlotState, PointShadow::sMaxSlots> mSlots;
        std::unordered_map<int, osg::Vec3f> mLastPositions;
        std::unordered_map<int, LightOrigin> mOrigins;
        double mLastTime = -1.0;
    };
}

#endif
