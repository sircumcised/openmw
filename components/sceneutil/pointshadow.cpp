#include "pointshadow.hpp"

#include <algorithm>

#include <osg/ClipControl>
#include <osg/Depth>
#include <osg/Image>
#include <osg/PolygonOffset>
#include <osg/Texture2D>
#include <osgUtil/CullVisitor>

#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/settings/categories/shadows.hpp>

#include "glextensions.hpp"
#include "lightmanager.hpp"
#include "nodecallback.hpp"

namespace SceneUtil
{
    namespace
    {
        constexpr int sAllFaces = (1 << 6) - 1;

        class PointShadowCullCallback
            : public SceneUtil::NodeCallback<PointShadowCullCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            explicit PointShadowCullCallback(PointShadowManager* manager)
                : mManager(manager)
            {
            }

            void operator()(osg::Node* node, osgUtil::CullVisitor* cv)
            {
                // Shadow cameras and reflections traverse the same scene; only the scene camera drives the slots.
                if (cv->getCurrentCamera()->getName() != Constants::SceneCamera)
                {
                    traverse(node, cv);
                    return;
                }
                cv->pushStateSet(mManager->getUniformStateSet(cv->getTraversalNumber()));
                traverse(node, cv);
                cv->popStateSet();
                mManager->update(cv);
            }

        private:
            PointShadowManager* mManager;
        };

        // Shadow cameras have no children: a second parent on the light manager would multiply every
        // getParentalNodePaths() under the scene. They cull the scene through this callback instead.
        class CasterCullCallback
            : public SceneUtil::NodeCallback<CasterCullCallback, osg::Node*, osgUtil::CullVisitor*>
        {
        public:
            CasterCullCallback(osg::Node* scene, osg::StateSet* shadowsBin)
                : mScene(scene)
                , mShadowsBin(shadowsBin)
            {
            }

            void operator()(osg::Node*, osgUtil::CullVisitor* cv)
            {
                if (mShadowsBin)
                    cv->pushStateSet(mShadowsBin);
                mScene->accept(*cv);
                if (mShadowsBin)
                    cv->popStateSet();
            }

        private:
            osg::Node* mScene;
            osg::StateSet* mShadowsBin;
        };
    }

    PointShadowManager::PointShadowManager(osg::ref_ptr<osg::Group> rootNode, LightManager& lightManager,
        unsigned int staticMask, unsigned int dynamicMask, osg::ref_ptr<osg::StateSet> shadowsBinStateSet,
        const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager)
        : mRootNode(std::move(rootNode))
        , mLightManager(lightManager)
        , mStaticMask(staticMask)
        , mDynamicMask(dynamicMask)
        , mMaxSlots(settings.mPointShadowLights)
        , mNear(settings.mPointShadowNear)
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

        const int resolution = settings.mPointShadowResolution;
        mStaticMaps = createDepthAtlas(resolution);
        mDynamicMaps = createDepthAtlas(resolution);

        osg::StateSet* stateset = mRootNode->getOrCreateStateSet();
        stateset->setTextureAttribute(mStaticUnit, mStaticMaps, osg::StateAttribute::ON);
        stateset->setTextureAttribute(mDynamicUnit, mDynamicMaps, osg::StateAttribute::ON);
        stateset->addUniform(new osg::Uniform("nmPointShadowStatic", mStaticUnit));
        stateset->addUniform(new osg::Uniform("nmPointShadowDynamic", mDynamicUnit));
        // Cameras that do not push the per-frame uniforms (reflections, the map) see no shadowed light.
        osg::ref_ptr<osg::StateSet> defaults = createUniformStateSet();
        for (const auto& uniform : defaults->getUniformList())
            stateset->addUniform(uniform.second.first.get());

        mCastingStateSet = createCastingStateSet(settings, shaderManager);
        mShadowsBinStateSet = std::move(shadowsBinStateSet);
        mCasterCallback = new CasterCullCallback(&mLightManager, mShadowsBinStateSet.get());

        for (int slot = 0; slot < PointShadow::sMaxSlots; ++slot)
            for (int face = 0; face < 6; ++face)
            {
                mStaticCameras[slot][face] = createCamera(mStaticMaps, slot, face, mStaticMask, resolution);
                mDynamicCameras[slot][face] = createCamera(mDynamicMaps, slot, face, mDynamicMask, resolution);
            }

        for (auto& uniforms : mUniformStateSets)
            uniforms = createUniformStateSet();

        mCullCallback = new PointShadowCullCallback(this);
        mLightManager.addCullCallback(mCullCallback);

        mValid = true;
    }

    PointShadowManager::~PointShadowManager()
    {
        if (!mValid)
            return;
        mLightManager.removeCullCallback(mCullCallback);
        osg::StateSet* stateset = mRootNode->getOrCreateStateSet();
        stateset->removeTextureAttribute(mStaticUnit, osg::StateAttribute::TEXTURE);
        stateset->removeTextureAttribute(mDynamicUnit, osg::StateAttribute::TEXTURE);
        stateset->removeUniform("nmPointShadowStatic");
        stateset->removeUniform("nmPointShadowDynamic");
        for (const auto& uniform : mUniformStateSets[0]->getUniformList())
            stateset->removeUniform(uniform.first);
    }

    osg::ref_ptr<osg::Texture2D> PointShadowManager::createDepthAtlas(int resolution) const
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setTextureSize(resolution * 6, resolution * PointShadow::sMaxSlots);
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

    osg::ref_ptr<osg::StateSet> PointShadowManager::createCastingStateSet(
        const Settings::ShadowsCategory& settings, Shader::ShaderManager& shaderManager) const
    {
        // Same casting program and state as MWShadowTechnique; the shadows bin swaps in the alpha-tested variants.
        osg::ref_ptr<osg::Program> program = new osg::Program;
        program->addShader(shaderManager.getShader("shadowcasting.vert"));
        program->addShader(shaderManager.getShader("shadowcasting.frag",
            { { "alphaFunc", std::to_string(GL_ALWAYS) }, { "alphaToCoverage", "0" }, { "adjustCoverage", "1" },
                { "useGPUShader4", SceneUtil::getGLExtensions().isGpuShader4Supported ? "1" : "0" } }));

        osg::ref_ptr<osg::Image> white = new osg::Image;
        white->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        std::fill_n(white->data(), 4, 0xff);
        osg::ref_ptr<osg::Texture2D> fallback = new osg::Texture2D(white);

        osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
        stateset->setAttributeAndModes(program, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        stateset->setTextureAttribute(0, fallback, osg::StateAttribute::ON);
        stateset->addUniform(new osg::Uniform("useDiffuseMapForShadowAlpha", true));
        stateset->addUniform(new osg::Uniform("alphaTestShadows", false));
        stateset->setMode(GL_CULL_FACE, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);
        stateset->setAttribute(new osg::PolygonOffset(settings.mPolygonOffsetFactor, settings.mPolygonOffsetUnits),
            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        stateset->setMode(GL_POLYGON_OFFSET_FILL, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        osg::ref_ptr<osg::Depth> depth = new osg::Depth;
        depth->setWriteMask(true);
        stateset->setAttribute(depth, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        stateset->setAttribute(new osg::ClipControl(osg::ClipControl::LOWER_LEFT, osg::ClipControl::NEGATIVE_ONE_TO_ONE),
            osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
        return stateset;
    }

    osg::ref_ptr<osg::Camera> PointShadowManager::createCamera(
        osg::Texture2D* target, int slot, int face, unsigned int cullMask, int resolution) const
    {
        osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        camera->setName("PointShadowCamera");
        camera->setRenderOrder(osg::Camera::PRE_RENDER);
        camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
        camera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
        camera->setComputeNearFarMode(osg::Camera::DO_NOT_COMPUTE_NEAR_FAR);
        camera->setCullingMode(camera->getCullingMode() & ~osg::CullSettings::SMALL_FEATURE_CULLING);
        camera->setCullMask(cullMask);
        // The clear is scissored to the viewport, so each face only resets its own tile.
        camera->setViewport(face * resolution, slot * resolution, resolution, resolution);
        camera->setClearMask(GL_DEPTH_BUFFER_BIT);
        camera->setClearDepth(1.0);
        camera->setImplicitBufferAttachmentMask(0, 0);
        camera->setDrawBuffer(GL_NONE);
        camera->setReadBuffer(GL_NONE);
        camera->attach(osg::Camera::DEPTH_BUFFER, target);
        camera->setStateSet(mCastingStateSet);
        camera->setCullingActive(false);
        camera->setCullCallback(mCasterCallback);
        return camera;
    }

    osg::ref_ptr<osg::StateSet> PointShadowManager::createUniformStateSet() const
    {
        osg::ref_ptr<osg::StateSet> stateset = new osg::StateSet;
        stateset->addUniform(new osg::Uniform(osg::Uniform::INT, "nmShadowLight", PointShadow::sMaxSlots));
        stateset->addUniform(new osg::Uniform(osg::Uniform::FLOAT, "nmShadowFade", PointShadow::sMaxSlots));
        stateset->addUniform(new osg::Uniform(osg::Uniform::INT, "nmDynFaces", PointShadow::sMaxSlots));
        stateset->addUniform(new osg::Uniform(osg::Uniform::FLOAT_VEC3, "nmShadowLightPos", PointShadow::sMaxSlots));
        stateset->addUniform(new osg::Uniform(osg::Uniform::FLOAT, "nmShadowFar", PointShadow::sMaxSlots));
        stateset->addUniform(new osg::Uniform("nmViewToWorld", osg::Matrix3()));
        stateset->addUniform(new osg::Uniform("nmCameraPos", osg::Vec3f()));
        clearUniforms(*stateset);
        return stateset;
    }

    void PointShadowManager::clearUniforms(osg::StateSet& uniforms)
    {
        for (unsigned int i = 0; i < PointShadow::sMaxSlots; ++i)
        {
            uniforms.getUniform("nmShadowLight")->setElement(i, -1);
            uniforms.getUniform("nmShadowFade")->setElement(i, 0.f);
            uniforms.getUniform("nmDynFaces")->setElement(i, 0);
            uniforms.getUniform("nmShadowLightPos")->setElement(i, osg::Vec3f());
            uniforms.getUniform("nmShadowFar")->setElement(i, 96.f);
        }
    }

    void PointShadowManager::setActive(bool active)
    {
        mActive = active && mValid;
        if (!mActive)
        {
            for (auto& slot : mSlots)
                PointShadow::deactivate(slot);
            mLastPositions.clear();
            mOrigins.clear();
        }
    }

    void PointShadowManager::cullCamera(osg::Camera& camera, int face, const osg::Vec3f& lightPos, float far,
        unsigned int mask, osgUtil::CullVisitor* cv) const
    {
        camera.setViewMatrix(PointShadow::faceView(face, lightPos));
        camera.setProjectionMatrix(PointShadow::faceProjection(mNear, far));
        // The cull mask decides what casts; set it on the visitor too, whatever the camera inherits.
        const osg::Node::NodeMask savedMask = cv->getTraversalMask();
        cv->setTraversalMask(mask);
        camera.accept(*cv);
        cv->setTraversalMask(savedMask);
    }

    void PointShadowManager::update(osgUtil::CullVisitor* cv)
    {
        const unsigned int frame = cv->getTraversalNumber();
        osg::StateSet& uniforms = *getUniformStateSet(frame);
        if (!mActive)
        {
            clearUniforms(uniforms);
            return;
        }

        const double time = cv->getFrameStamp() ? cv->getFrameStamp()->getSimulationTime() : 0.0;
        const float dt = mLastTime < 0 ? 0.f : std::clamp(static_cast<float>(time - mLastTime), 0.f, 0.1f);
        mLastTime = time;

        const osg::Matrixd viewToWorld = cv->getCurrentCamera()->getInverseViewMatrix();
        const osg::Vec3f cameraPos = viewToWorld.getTrans();

        std::unordered_map<int, LightInfo> lights;
        std::vector<PointShadow::Candidate> candidates;
        for (const LightManager::LightSourceTransform& transform : mLightManager.getLights())
        {
            LightSource* source = transform.mLightSource;
            const float radius = source->getRadius();
            if (radius <= 0.f)
                continue;
            const int id = source->getId();
            auto origin = mOrigins.find(id);
            if (origin == mOrigins.end())
            {
                const osg::NodePathList paths = source->getParentalNodePaths();
                const osg::NodePath path = paths.empty() ? osg::NodePath() : paths[0];
                // carriers and light owners are told apart by their explicit category masks
                origin = mOrigins
                             .emplace(id,
                                 LightOrigin{ PointShadow::isCarried(path, mDynamicMask & ~mStaticMask),
                                     PointShadow::lightOwner(path, mStaticMask & ~mDynamicMask) })
                             .first;
            }
            // torches in hand: out of scope, and the dynamic cube would draw the carrier around the light
            if (origin->second.mCarried)
                continue;
            const osg::Vec3f position = transform.mWorldMatrix.getTrans();
            const auto last = mLastPositions.find(id);
            const bool moved = last != mLastPositions.end() && (last->second - position).length2() > 0.25f;
            const osg::Vec4f& diffuse = source->getLight(frame)->getDiffuse();
            const float intensity = 0.2126f * diffuse.r() + 0.7152f * diffuse.g() + 0.0722f * diffuse.b();
            lights[id] = LightInfo{ position, radius, moved };
            candidates.push_back({ id, PointShadow::score(radius, intensity, (position - cameraPos).length()), moved });
        }
        mLastPositions.clear();
        for (const auto& [id, info] : lights)
            mLastPositions[id] = info.mPosition;
        std::erase_if(mOrigins, [&](const auto& entry) {
            return std::none_of(mLightManager.getLights().begin(), mLightManager.getLights().end(),
                [&](const LightManager::LightSourceTransform& t) { return t.mLightSource->getId() == entry.first; });
        });

        PointShadow::Slots current;
        for (int i = 0; i < PointShadow::sMaxSlots; ++i)
            current[i] = mSlots[i].mLightId;
        const PointShadow::Slots target = PointShadow::selectSlots(candidates, current, mMaxSlots);

        PointShadow::Slots shown;
        for (int i = 0; i < PointShadow::sMaxSlots; ++i)
        {
            PointShadow::SlotState& slot = mSlots[i];
            const auto previous = lights.find(slot.mLightId);
            const bool present = previous != lights.end() && !previous->second.mMoved;
            const int staticFace = PointShadow::advanceSlot(slot, target[i], present, dt);

            shown[i] = -1;
            uniforms.getUniform("nmDynFaces")->setElement(i, 0);
            const auto info = lights.find(slot.mLightId);
            if (info == lights.end())
            {
                uniforms.getUniform("nmShadowFade")->setElement(i, 0.f);
                continue;
            }
            const osg::Vec3f& lightPos = info->second.mPosition;
            const float far = PointShadow::farPlane(info->second.mRadius, mNear);
            if (staticFace >= 0)
            {
                // the lantern or sconce around the light would otherwise block it
                osg::ref_ptr<osg::Node> owner;
                mOrigins[slot.mLightId].mOwner.lock(owner);
                const osg::Node::NodeMask ownerMask = owner ? owner->getNodeMask() : 0;
                if (owner)
                    owner->setNodeMask(0);
                cullCamera(*mStaticCameras[i][staticFace], staticFace, lightPos, far, mStaticMask, cv);
                if (owner)
                    owner->setNodeMask(ownerMask);
            }
            // shortcut: all 6 actor faces every frame; cull per face against actor bounds if the F3 cull time shows it.
            for (int face = 0; face < 6; ++face)
                cullCamera(*mDynamicCameras[i][face], face, lightPos, far, mDynamicMask, cv);

            shown[i] = slot.mLightId;
            uniforms.getUniform("nmDynFaces")->setElement(i, sAllFaces);
            uniforms.getUniform("nmShadowFade")->setElement(i, slot.mFade);
            uniforms.getUniform("nmShadowLightPos")->setElement(i, lightPos);
            uniforms.getUniform("nmShadowFar")->setElement(i, far);
        }

        const std::array<int, PointShadow::sMaxSlots> bufferIndices
            = PointShadow::toBufferIndices(shown, mLightManager.getLightIndexMap(frame));
        for (int i = 0; i < PointShadow::sMaxSlots; ++i)
            uniforms.getUniform("nmShadowLight")->setElement(i, bufferIndices[i]);

        uniforms.getUniform("nmViewToWorld")
            ->set(osg::Matrix3(viewToWorld(0, 0), viewToWorld(0, 1), viewToWorld(0, 2), viewToWorld(1, 0),
                viewToWorld(1, 1), viewToWorld(1, 2), viewToWorld(2, 0), viewToWorld(2, 1), viewToWorld(2, 2)));
        uniforms.getUniform("nmCameraPos")->set(cameraPos);
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
            { "pointShadowNear", std::to_string(settings.mPointShadowNear.get()) },
            { "pointShadowStrength", std::to_string(settings.mPointShadowStrength.get()) },
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
            { "pointShadowBias", "0.01" },
            { "pointShadowNormalOffset", "0.0" },
            { "pointShadowNear", "48.0" },
            { "pointShadowStrength", "0.75" },
            { "pointShadowDebug", "0" },
        };
    }
}
