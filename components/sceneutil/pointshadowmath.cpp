#include "pointshadowmath.hpp"

#include <algorithm>
#include <cmath>

namespace SceneUtil::PointShadow
{
    namespace
    {
        constexpr float sSwapMargin = 1.2f;

        struct Face
        {
            osg::Vec3f mForward;
            osg::Vec3f mUp;
            osg::Vec3f mRight; // mForward ^ mUp, the view x axis of lookAt
            osg::Vec3f mViewUp; // mRight ^ mForward, the view y axis of lookAt
        };

        // Shared with files/shaders/lib/light/pointshadows.glsl: change both together.
        const std::array<Face, 6> sFaces{ {
            { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 }, { 0, 0, 1 } },
            { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 0, 1 } },
            { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 0, -1, 0 }, { 0, 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 } },
            { { 0, 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, -1, 0 } },
            { { 0, 0, -1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 1, 0 } },
        } };

        const Candidate* find(const std::vector<Candidate>& lights, int id)
        {
            for (const Candidate& light : lights)
                if (light.mId == id)
                    return &light;
            return nullptr;
        }
    }

    float score(float radius, float intensity, float distance)
    {
        return radius * intensity / std::max(distance, 1.f);
    }

    Slots selectSlots(const std::vector<Candidate>& lights, const Slots& current, int maxSlots)
    {
        std::vector<Candidate> ranked;
        for (const Candidate& light : lights)
            if (!light.mMoved && light.mScore > 0.f) // negative (darkness) lights would swap slots every frame
                ranked.push_back(light);
        std::sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) {
            return a.mScore != b.mScore ? a.mScore > b.mScore : a.mId < b.mId;
        });

        Slots result = current;
        for (int i = 0; i < sMaxSlots; ++i)
            if (i >= maxSlots || (result[i] != -1 && !find(ranked, result[i])))
                result[i] = -1;

        const auto inSlots = [&](int id) { return std::find(result.begin(), result.end(), id) != result.end(); };
        auto next = ranked.begin();
        for (int i = 0; i < std::min(maxSlots, sMaxSlots); ++i)
        {
            if (result[i] != -1)
                continue;
            while (next != ranked.end() && inSlots(next->mId))
                ++next;
            if (next == ranked.end())
                break;
            result[i] = (next++)->mId;
        }

        auto outsider = std::find_if(ranked.begin(), ranked.end(), [&](const Candidate& c) { return !inSlots(c.mId); });
        if (outsider == ranked.end())
            return result;
        int weakest = -1;
        float weakestScore = 0.f;
        for (int i = 0; i < std::min(maxSlots, sMaxSlots); ++i)
        {
            const Candidate* incumbent = find(ranked, result[i]);
            if (!incumbent)
                continue;
            const float s = incumbent->mScore;
            if (weakest == -1 || s < weakestScore)
            {
                weakest = i;
                weakestScore = s;
            }
        }
        if (weakest != -1 && outsider->mScore > sSwapMargin * weakestScore)
            result[weakest] = outsider->mId;
        return result;
    }

    std::array<int, sMaxSlots> toBufferIndices(const Slots& slots, const std::unordered_map<int, int>& indexMap)
    {
        std::array<int, sMaxSlots> result;
        for (int i = 0; i < sMaxSlots; ++i)
        {
            const auto it = indexMap.find(slots[i]);
            result[i] = slots[i] != -1 && it != indexMap.end() ? it->second : -1;
        }
        return result;
    }

    float farPlane(float lightRadius)
    {
        return std::clamp(2.f * lightRadius, 2.f * sNearPlane, 16384.f);
    }

    int faceOf(const osg::Vec3f& dir, osg::Vec2f& uv)
    {
        const float ax = std::abs(dir.x()), ay = std::abs(dir.y()), az = std::abs(dir.z());
        int face;
        if (ax >= ay && ax >= az)
            face = dir.x() >= 0 ? 0 : 1;
        else if (ay >= az)
            face = dir.y() >= 0 ? 2 : 3;
        else
            face = dir.z() >= 0 ? 4 : 5;
        const Face& f = sFaces[face];
        const float depth = dir * f.mForward;
        uv = osg::Vec2f((dir * f.mRight) / depth * 0.5f + 0.5f, (dir * f.mViewUp) / depth * 0.5f + 0.5f);
        return face;
    }

    float faceDepthNdc(float axisDistance, float far)
    {
        const float n = sNearPlane;
        return (far + n) / (far - n) - 2.f * far * n / ((far - n) * axisDistance);
    }

    float shadowCompareDepth(float axisDistance, float far, float bias)
    {
        const float biased = axisDistance * (1.f - bias);
        if (biased <= sNearPlane)
            return 0.f;
        return faceDepthNdc(biased, far) * 0.5f + 0.5f;
    }

    namespace
    {
        // Plain groups keep the default mask ~0; only nodes given an explicit mask say what they are.
        bool hasCategory(const osg::Node* node, unsigned int mask)
        {
            return node->getNodeMask() != ~0u && (node->getNodeMask() & mask);
        }
    }

    bool isCarried(const osg::NodePath& path, unsigned int carrierMask)
    {
        return std::any_of(
            path.begin(), path.end(), [&](const osg::Node* node) { return hasCategory(node, carrierMask); });
    }

    osg::Node* lightOwner(const osg::NodePath& path, unsigned int ownerMask)
    {
        for (auto it = path.rbegin(); it != path.rend(); ++it)
            if (hasCategory(*it, ownerMask))
                return *it;
        return nullptr;
    }

    osg::Matrixf faceView(int face, const osg::Vec3f& lightPos)
    {
        const Face& f = sFaces[face];
        return osg::Matrixf::lookAt(lightPos, lightPos + f.mForward, f.mUp);
    }

    osg::Matrixf faceProjection(float far)
    {
        return osg::Matrixf::perspective(90.0, 1.0, sNearPlane, far);
    }

    int advanceSlot(SlotState& s, int targetId, bool currentPresent, float dt)
    {
        constexpr float fadeTime = 0.3f;
        if (targetId != s.mLightId)
        {
            if (s.mLightId != -1 && currentPresent)
            {
                s.mLeaving = true;
                s.mFade -= dt / fadeTime;
                if (s.mFade > 1e-5f)
                    return -1;
            }
            s = SlotState{ targetId };
        }
        if (s.mLightId == -1)
            return -1;
        if (s.mStaticFacesDone < 6)
            return s.mStaticFacesDone++;
        s.mLeaving = false;
        s.mFade = std::min(1.f, s.mFade + dt / fadeTime);
        return -1;
    }

    void deactivate(SlotState& s)
    {
        s = SlotState{};
    }
}
