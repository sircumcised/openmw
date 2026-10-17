// NerevarMoon: indoor point light shadows.
#ifndef OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOWMATH_H
#define OPENMW_COMPONENTS_SCENEUTIL_POINTSHADOWMATH_H

#include <array>
#include <unordered_map>
#include <vector>

#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Vec2f>
#include <osg/Vec3f>

namespace SceneUtil::PointShadow
{
    constexpr int sMaxSlots = 4;

    /// LightSource ids per shadow slot, -1 = empty
    using Slots = std::array<int, sMaxSlots>;

    struct Candidate
    {
        int mId;
        float mScore;
        bool mMoved;
    };

    float score(float radius, float intensity, float distance);

    /// Keeps incumbents unless an outsider beats the weakest by more than 20%; at most one swap per call.
    Slots selectSlots(const std::vector<Candidate>& lights, const Slots& current, int maxSlots);

    /// Maps slot ids to LightBuffer indices of this frame, -1 when the light has no index.
    std::array<int, sMaxSlots> toBufferIndices(const Slots& slots, const std::unordered_map<int, int>& indexMap);

    float farPlane(float lightRadius);

    /// Cube face (0..5 = +X, -X, +Y, -Y, +Z, -Z) seen along dir, and the [0,1] texture coordinates on it.
    int faceOf(const osg::Vec3f& dir, osg::Vec2f& uv);

    /// NDC depth that faceProjection(far) gives a point at axisDistance along the face's axis.
    float faceDepthNdc(float axisDistance, float far);

    /// Window depth [0,1] to compare against the cube for a receiver at axisDistance; bias is a fraction of the
    /// distance, so the margin keeps the same size in world units at any range.
    float shadowCompareDepth(float axisDistance, float far, float bias);

    /// A light under a node with a carrierMask bit (an actor or the player) moves with its carrier.
    bool isCarried(const osg::NodePath& path, unsigned int carrierMask);

    /// Deepest node with an ownerMask bit on the light's path: the lantern or sconce mesh holding the light.
    osg::Node* lightOwner(const osg::NodePath& path, unsigned int ownerMask);

    osg::Matrixf faceView(int face, const osg::Vec3f& lightPos);

    osg::Matrixf faceProjection(float far);

    struct SlotState
    {
        int mLightId = -1;
        int mStaticFacesDone = 0;
        float mFade = 0.f;
        bool mLeaving = false;
    };

    /// Moves a slot one frame towards targetId; returns the static face (0..5) to render this frame, or -1.
    /// currentPresent: the slot's current light still exists in the scene (a vanished light swaps without fading).
    int advanceSlot(SlotState& s, int targetId, bool currentPresent, float dt);

    void deactivate(SlotState& s);
}

#endif
