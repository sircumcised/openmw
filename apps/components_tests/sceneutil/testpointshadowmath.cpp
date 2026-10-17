#include <components/sceneutil/pointshadowmath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>

namespace
{
    using namespace SceneUtil::PointShadow;

    constexpr Slots sEmpty{ -1, -1, -1, -1 };

    std::set<int> asSet(const Slots& slots)
    {
        std::set<int> result;
        for (int id : slots)
            if (id != -1)
                result.insert(id);
        return result;
    }

    TEST(SlotSelect, PicksTopScores)
    {
        std::vector<Candidate> lights;
        for (int i = 1; i <= 6; ++i)
            lights.push_back({ i, static_cast<float>(i), false });
        EXPECT_EQ(asSet(selectSlots(lights, sEmpty, 4)), (std::set<int>{ 3, 4, 5, 6 }));
    }

    TEST(SlotSelect, HysteresisKeepsIncumbent)
    {
        std::vector<Candidate> lights{ { 1, 10.f, false }, { 2, 10.f, false }, { 3, 10.f, false }, { 4, 10.f, false },
            { 5, 11.9f, false } };
        const Slots current{ 1, 2, 3, 4 };
        EXPECT_EQ(selectSlots(lights, current, 4), current);
    }

    TEST(SlotSelect, HysteresisSwapsAbove20)
    {
        std::vector<Candidate> lights{ { 1, 10.f, false }, { 2, 10.f, false }, { 3, 10.f, false }, { 4, 10.f, false },
            { 5, 12.1f, false } };
        const Slots result = selectSlots(lights, Slots{ 1, 2, 3, 4 }, 4);
        const std::set<int> ids = asSet(result);
        EXPECT_EQ(ids.size(), 4u);
        EXPECT_EQ(ids.count(5), 1u);
        int kept = 0;
        for (int i = 0; i < 4; ++i)
            kept += result[i] == i + 1;
        EXPECT_EQ(kept, 3);
    }

    TEST(SlotSelect, MovedLightExcluded)
    {
        std::vector<Candidate> lights{ { 1, 1.f, false }, { 7, 100.f, true } };
        EXPECT_EQ(asSet(selectSlots(lights, Slots{ 7, -1, -1, -1 }, 4)), (std::set<int>{ 1 }));
    }

    TEST(SlotSelect, RemovedLightFreesSlotAtOnce)
    {
        std::vector<Candidate> lights{ { 1, 10.f, false }, { 3, 10.f, false }, { 4, 10.f, false } };
        const Slots result = selectSlots(lights, Slots{ 1, 2, 3, 4 }, 4);
        EXPECT_EQ(result[1], -1);
        EXPECT_EQ(asSet(result), (std::set<int>{ 1, 3, 4 }));
    }

    TEST(SlotSelect, FewerLightsThanSlots)
    {
        std::vector<Candidate> two{ { 1, 1.f, false }, { 2, 2.f, false } };
        const Slots result = selectSlots(two, sEmpty, 4);
        EXPECT_EQ(asSet(result), (std::set<int>{ 1, 2 }));
        EXPECT_EQ(std::count(result.begin(), result.end(), -1), 2);
        EXPECT_EQ(selectSlots({}, Slots{ 1, 2, 3, 4 }, 4), sEmpty);
    }

    TEST(SlotSelect, MaxSlotsLimits)
    {
        std::vector<Candidate> lights;
        for (int i = 1; i <= 6; ++i)
            lights.push_back({ i, static_cast<float>(i), false });
        const Slots result = selectSlots(lights, Slots{ 1, 2, 3, 4 }, 2);
        EXPECT_EQ(result[2], -1);
        EXPECT_EQ(result[3], -1);
        EXPECT_NE(result[0], -1);
        EXPECT_NE(result[1], -1);
    }

    TEST(PointShadowMath, BufferIndicesMissingIdIsMinusOne)
    {
        const std::unordered_map<int, int> indexMap{ { 1, 5 } };
        EXPECT_EQ(toBufferIndices(Slots{ 1, 9, -1, -1 }, indexMap), (std::array<int, sMaxSlots>{ 5, -1, -1, -1 }));
    }

    TEST(PointShadowMath, ScoreFavoursNearBrightLargeLights)
    {
        EXPECT_FLOAT_EQ(score(100.f, 0.5f, 25.f), 2.f);
        EXPECT_FLOAT_EQ(score(100.f, 0.5f, 0.f), 50.f);
    }

    TEST(PointShadowMath, FarPlaneClamped)
    {
        EXPECT_FLOAT_EQ(farPlane(0.f), 16.f);
        EXPECT_FLOAT_EQ(farPlane(100.f), 200.f);
        EXPECT_FLOAT_EQ(farPlane(1e6f), 16384.f);
    }

    TEST(PointShadowMath, FaceOfMatchesFaceView)
    {
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> dist(-1.f, 1.f);
        const osg::Vec3f lightPos(100.f, -250.f, 40.f);
        const float far = 400.f;
        for (int i = 0; i < 200; ++i)
        {
            osg::Vec3f dir(dist(rng), dist(rng), dist(rng));
            if (dir.length2() < 1e-4f)
                continue;
            dir.normalize();
            osg::Vec2f uv;
            const int face = faceOf(dir, uv);
            ASSERT_GE(face, 0);
            ASSERT_LT(face, 6);
            const osg::Vec3f point = lightPos + dir * 50.f;
            const osg::Vec4f clip = osg::Vec4f(point, 1.f) * faceView(face, lightPos) * faceProjection(far);
            ASSERT_GT(clip.w(), 0.f);
            const osg::Vec3f ndc(clip.x() / clip.w(), clip.y() / clip.w(), clip.z() / clip.w());
            EXPECT_NEAR(ndc.x() * 0.5f + 0.5f, uv.x(), 1e-4f) << "face " << face;
            EXPECT_NEAR(ndc.y() * 0.5f + 0.5f, uv.y(), 1e-4f) << "face " << face;
            EXPECT_GE(ndc.z(), -1.f);
            EXPECT_LE(ndc.z(), 1.f);
            const float axisDistance = std::max({ std::abs(dir.x()), std::abs(dir.y()), std::abs(dir.z()) }) * 50.f;
            EXPECT_NEAR(faceDepthNdc(axisDistance, far), ndc.z(), 1e-4f);
        }
    }

    TEST(PointShadowMath, FaceOfAxes)
    {
        osg::Vec2f uv;
        EXPECT_EQ(faceOf(osg::Vec3f(1, 0, 0), uv), 0);
        EXPECT_NEAR(uv.x(), 0.5f, 1e-6f);
        EXPECT_NEAR(uv.y(), 0.5f, 1e-6f);
        EXPECT_EQ(faceOf(osg::Vec3f(0, -1, 0), uv), 3);
        EXPECT_EQ(faceOf(osg::Vec3f(0, 0, 1), uv), 4);
        EXPECT_EQ(faceOf(osg::Vec3f(1, 1, 1), uv), 0);
    }
}
