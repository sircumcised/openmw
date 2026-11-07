#include <components/sceneutil/bonephysicsmath.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace SceneUtil::BonePhysicsMath;

    // a flail haft: axis along +Y from 0 to 31, radius 1.7; links of radius 0.8 -> 2.5 units from the axis
    Capsule haftAt(float x)
    {
        return Capsule{ osg::Vec3f(x, 0.f, 0.f), osg::Vec3f(x, 31.f, 0.f), 1.7f };
    }

    TEST(SceneUtilBonePhysicsMathTest, haftSweepingThroughAStillLinkKeepsTheLinkOnItsSide)
    {
        osg::Vec3f pos(3.f, 10.f, 0.f);
        collideCapsule(pos, osg::Vec3f(3.f, 10.f, 0.f), haftAt(0.f), haftAt(4.f), 0.8f);
        EXPECT_NEAR(pos.x(), 6.5f, 1e-4f);
        EXPECT_NEAR(pos.y(), 10.f, 1e-4f);
    }

    TEST(SceneUtilBonePhysicsMathTest, linkFasterThanTheHaftIsThickComesBackToItsSide)
    {
        osg::Vec3f pos(-3.5f, 10.f, 0.f);
        collideCapsule(pos, osg::Vec3f(3.f, 10.f, 0.f), haftAt(0.f), haftAt(0.f), 0.8f);
        EXPECT_NEAR(pos.x(), 2.5f, 1e-4f);
    }

    TEST(SceneUtilBonePhysicsMathTest, linkPushedIntoTheHaftLeavesOnTheSameSide)
    {
        osg::Vec3f pos(1.f, 10.f, 0.f);
        collideCapsule(pos, osg::Vec3f(3.f, 10.f, 0.f), haftAt(0.f), haftAt(0.f), 0.8f);
        EXPECT_NEAR(pos.x(), 2.5f, 1e-4f);
    }

    TEST(SceneUtilBonePhysicsMathTest, linkGoingOverTheCapIsLeftAlone)
    {
        osg::Vec3f pos(-3.f, 33.f, 0.f);
        collideCapsule(pos, osg::Vec3f(3.f, 33.f, 0.f), haftAt(0.f), haftAt(0.f), 0.8f);
        EXPECT_NEAR(pos.x(), -3.f, 1e-4f);
        EXPECT_NEAR(pos.y(), 33.f, 1e-4f);
    }

    TEST(SceneUtilBonePhysicsMathTest, linkAwayFromTheHaftIsLeftAlone)
    {
        osg::Vec3f pos(8.f, 10.f, 0.f);
        collideCapsule(pos, osg::Vec3f(9.f, 10.f, 0.f), haftAt(0.f), haftAt(0.f), 0.8f);
        EXPECT_NEAR(pos.x(), 8.f, 1e-4f);
    }

    TEST(SceneUtilBonePhysicsMathTest, lerpMovesBothEnds)
    {
        const Capsule c = lerp(haftAt(0.f), haftAt(4.f), 0.25f);
        EXPECT_NEAR(c.mA.x(), 1.f, 1e-4f);
        EXPECT_NEAR(c.mB.x(), 1.f, 1e-4f);
        EXPECT_NEAR(c.mB.y(), 31.f, 1e-4f);
    }
}
