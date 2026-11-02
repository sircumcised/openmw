#include <components/sceneutil/upscalemath.hpp>

#include <osg/Matrixf>
#include <osg/Vec4f>

#include <gtest/gtest.h>

namespace
{
    TEST(SceneUtilUpscaleMath, mipBiasAuto)
    {
        EXPECT_NEAR(SceneUtil::upscaleMipBias(0.67f), -1.5778f, 1e-3f);
    }

    TEST(SceneUtilUpscaleMath, mipBiasAtFullScale)
    {
        EXPECT_FLOAT_EQ(SceneUtil::upscaleMipBias(1.f), -1.f);
    }

    TEST(SceneUtilUpscaleMath, parseMipBias)
    {
        EXPECT_FALSE(SceneUtil::parseMipBias("auto").has_value());
        EXPECT_FALSE(SceneUtil::parseMipBias("abc").has_value());
        EXPECT_FLOAT_EQ(*SceneUtil::parseMipBias("-0.5"), -0.5f);
    }

    TEST(SceneUtilUpscaleMath, sceneSize)
    {
        EXPECT_EQ(SceneUtil::upscaleSceneSize({ 1920, 1080 }, 0.67f), osg::Vec2i(1286, 724));
        EXPECT_EQ(SceneUtil::upscaleSceneSize({ 1, 1 }, 0.5f), osg::Vec2i(1, 1));
    }

    TEST(SceneUtilUpscaleMath, jitterToNdc)
    {
        const osg::Vec2f j = SceneUtil::jitterToNdc({ 0.25f, -0.5f }, { 1286, 724 });
        EXPECT_NEAR(j.x(), 0.5f / 1286.f, 1e-7f);
        EXPECT_NEAR(j.y(), -1.f / 724.f, 1e-7f);
    }

    TEST(SceneUtilUpscaleMath, reprojectStaticCameraIsZero)
    {
        const osg::Matrixd proj = osg::Matrixd::perspective(60.0, 16.0 / 9.0, 1.0, 10000.0);
        const osg::Matrixf m = SceneUtil::clipToPrevClip(proj, osg::Matrixd(), osg::Matrixd(), proj);
        for (bool rz : { false, true })
        {
            const osg::Vec2f d = SceneUtil::reprojectUv({ 0.3f, 0.7f }, 0.9f, m, rz);
            EXPECT_NEAR(d.x(), 0.f, 1e-5f);
            EXPECT_NEAR(d.y(), 0.f, 1e-5f);
        }
    }

    TEST(SceneUtilUpscaleMath, reprojectCameraMovedRight)
    {
        // current view is identity; in the previous view space the same point sat at x = +10
        const osg::Matrixd proj = osg::Matrixd::perspective(90.0, 1.0, 1.0, 10000.0);
        const osg::Matrixd prevView = osg::Matrixd::translate(10.0, 0.0, 0.0);
        const osg::Vec4d clip = osg::Vec4d(0.0, 0.0, -100.0, 1.0) * proj;
        const float depth = static_cast<float>((clip.z() / clip.w()) * 0.5 + 0.5);
        const osg::Matrixf m = SceneUtil::clipToPrevClip(proj, osg::Matrixd(), prevView, proj);
        const osg::Vec2f d = SceneUtil::reprojectUv({ 0.5f, 0.5f }, depth, m, false);
        EXPECT_NEAR(d.x(), 0.05f, 1e-4f);
        EXPECT_NEAR(d.y(), 0.f, 1e-5f);
    }

    TEST(SceneUtilUpscaleMath, reprojectStillCameraFarFromOriginIsZero)
    {
        // exterior cells reach 1e5 units: the view translation must cancel in double precision
        const osg::Matrixd proj = osg::Matrixd::perspective(55.0, 16.0 / 9.0, 1.0, 7168.0);
        const osg::Matrixd view = osg::Matrixd::lookAt(
            osg::Vec3d(150000.0, -120000.0, 2000.0), osg::Vec3d(150100.0, -119900.0, 1990.0), osg::Vec3d(0, 0, 1));
        const osg::Matrixf m = SceneUtil::clipToPrevClip(proj, view, view, proj);
        for (float depth : { 0.2f, 0.9f, 0.999f })
        {
            const osg::Vec2f d = SceneUtil::reprojectUv({ 0.21f, 0.83f }, depth, m, false);
            EXPECT_NEAR(d.x(), 0.f, 1e-6f);
            EXPECT_NEAR(d.y(), 0.f, 1e-6f);
        }
    }

    TEST(SceneUtilUpscaleMath, fovFromProjection)
    {
        const osg::Matrixd proj = osg::Matrixd::perspective(55.0, 16.0 / 9.0, 1.0, 7168.0);
        EXPECT_NEAR(SceneUtil::fovYFromProjection(proj), osg::DegreesToRadians(55.0f), 1e-5f);
        const osg::Matrixd jittered = proj * osg::Matrixd::translate(0.001, -0.002, 0.0);
        EXPECT_NEAR(SceneUtil::fovYFromProjection(jittered), osg::DegreesToRadians(55.0f), 1e-5f);
    }

    TEST(SceneUtilUpscaleMath, cameraCut)
    {
        EXPECT_FALSE(SceneUtil::isCameraCut({ 0, 0, 0 }, { 300, 0, 0 }));
        EXPECT_TRUE(SceneUtil::isCameraCut({ 0, 0, 0 }, { 513, 0, 0 }));
    }
}
