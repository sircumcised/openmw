#include "pointshadow.hpp"

#include <osg/Image>

#include <components/debug/debuglog.hpp>
#include <components/settings/categories/shadows.hpp>

#include "lightmanager.hpp"
#include "pointshadowmath.hpp"

namespace SceneUtil
{
    PointShadowManager::PointShadowManager(osg::ref_ptr<osg::Group> rootNode, LightManager& lightManager,
        unsigned int staticMask, unsigned int dynamicMask, const Settings::ShadowsCategory& settings,
        Shader::ShaderManager& shaderManager)
        : mRootNode(std::move(rootNode))
        , mLightManager(lightManager)
        , mStaticMask(staticMask)
        , mDynamicMask(dynamicMask)
        , mMaxSlots(settings.mPointShadowLights)
    {
        if (!settings.mPointLightShadows)
            return;

        try
        {
            mStaticUnit = shaderManager.reserveGlobalTextureUnits(Shader::ShaderManager::Slot::PointShadowMaps, 2);
        }
        catch (const std::exception&)
        {
            mStaticUnit = -1;
        }
        if (mStaticUnit < 0)
        {
            Log(Debug::Warning) << "Point light shadows disabled: no free texture units";
            return;
        }
        mDynamicUnit = mStaticUnit + 1;

        mStaticMaps = createDepthArray(settings.mPointShadowResolution);
        mDynamicMaps = createDepthArray(settings.mPointShadowResolution);

        osg::StateSet* stateset = mRootNode->getOrCreateStateSet();
        stateset->setTextureAttribute(mStaticUnit, mStaticMaps, osg::StateAttribute::ON);
        stateset->setTextureAttribute(mDynamicUnit, mDynamicMaps, osg::StateAttribute::ON);
        stateset->addUniform(new osg::Uniform("nmPointShadowStatic", mStaticUnit));
        stateset->addUniform(new osg::Uniform("nmPointShadowDynamic", mDynamicUnit));

        mValid = true;
    }

    PointShadowManager::~PointShadowManager()
    {
        if (!mValid)
            return;
        osg::StateSet* stateset = mRootNode->getOrCreateStateSet();
        stateset->removeTextureAttribute(mStaticUnit, osg::StateAttribute::TEXTURE);
        stateset->removeTextureAttribute(mDynamicUnit, osg::StateAttribute::TEXTURE);
        stateset->removeUniform("nmPointShadowStatic");
        stateset->removeUniform("nmPointShadowDynamic");
    }

    osg::ref_ptr<osg::Texture2DArray> PointShadowManager::createDepthArray(int resolution) const
    {
        osg::ref_ptr<osg::Texture2DArray> texture = new osg::Texture2DArray;
        texture->setTextureSize(resolution, resolution, PointShadow::sMaxSlots * 6);
        texture->setInternalFormat(GL_DEPTH_COMPONENT32F);
        texture->setSourceFormat(GL_DEPTH_COMPONENT);
        texture->setSourceType(GL_FLOAT);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
        texture->setShadowComparison(true);
        texture->setShadowCompareFunc(osg::Texture::LEQUAL);
        return texture;
    }

    void PointShadowManager::setActive(bool active)
    {
        mActive = active && mValid;
    }

    Shader::ShaderManager::DefineMap PointShadowManager::getDefines(const Settings::ShadowsCategory& settings) const
    {
        if (!mValid)
            return getDisabledDefines();
        return {
            { "pointShadows", "1" },
            { "pointShadowStaticUnit", std::to_string(mStaticUnit) },
            { "pointShadowDynamicUnit", std::to_string(mDynamicUnit) },
            { "pointShadowResolution", std::to_string(settings.mPointShadowResolution.get()) + ".0" },
            { "pointShadowBias", std::to_string(settings.mPointShadowBias.get()) },
            { "pointShadowNormalOffset", std::to_string(settings.mPointShadowNormalOffset.get()) },
            { "pointShadowDebug", settings.mPointShadowDebug ? "1" : "0" },
        };
    }

    Shader::ShaderManager::DefineMap PointShadowManager::getDisabledDefines()
    {
        return {
            { "pointShadows", "0" },
            { "pointShadowStaticUnit", "0" },
            { "pointShadowDynamicUnit", "0" },
            { "pointShadowResolution", "512.0" },
            { "pointShadowBias", "0.0" },
            { "pointShadowNormalOffset", "0.0" },
            { "pointShadowDebug", "0" },
        };
    }
}
