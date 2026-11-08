#include "joltragdoll.hpp"

#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include <components/misc/strings/algorithm.hpp>

#include "joltshapes.hpp"

namespace MWPhysics
{
    namespace
    {
        enum class Joint
        {
            Root,
            Spine,
            Neck,
            Shoulder,
            Elbow,
            Wrist,
            Hip,
            Knee,
            Ankle
        };

        struct PartDef
        {
            const char* mName; // "Chest" picks Bip01 Spine2, else Bip01 Spine1
            int mParent; // index in sParts
            bool mRequired;
            float mRadius; // metres, for a 0.86 m pelvis-to-head actor
            Joint mJoint;
        };

        // Parents come before children (Jolt skeleton order)
        constexpr PartDef sParts[] = {
            { "Bip01 Pelvis", -1, true, 0.12f, Joint::Root }, // 0
            { "Bip01 Spine", 0, true, 0.11f, Joint::Spine }, // 1
            { "Chest", 1, true, 0.12f, Joint::Spine }, // 2
            { "Bip01 Head", 2, true, 0.10f, Joint::Neck }, // 3
            { "Bip01 L UpperArm", 2, true, 0.055f, Joint::Shoulder }, // 4
            { "Bip01 L Forearm", 4, true, 0.045f, Joint::Elbow }, // 5
            { "Bip01 L Hand", 5, false, 0.04f, Joint::Wrist }, // 6
            { "Bip01 R UpperArm", 2, true, 0.055f, Joint::Shoulder }, // 7
            { "Bip01 R Forearm", 7, true, 0.045f, Joint::Elbow }, // 8
            { "Bip01 R Hand", 8, false, 0.04f, Joint::Wrist }, // 9
            { "Bip01 L Thigh", 0, true, 0.08f, Joint::Hip }, // 10
            { "Bip01 L Calf", 10, true, 0.06f, Joint::Knee }, // 11
            { "Bip01 L Foot", 11, false, 0.045f, Joint::Ankle }, // 12
            { "Bip01 R Thigh", 0, true, 0.08f, Joint::Hip }, // 13
            { "Bip01 R Calf", 13, true, 0.06f, Joint::Knee }, // 14
            { "Bip01 R Foot", 14, false, 0.045f, Joint::Ankle }, // 15
        };
        constexpr int sPartCount = static_cast<int>(std::size(sParts));
        constexpr float sDeg = JPH::JPH_PI / 180.f;

        const RagdollBone* findBone(const std::vector<RagdollBone>& bones, std::string_view name)
        {
            for (const RagdollBone& bone : bones)
                if (Misc::StringUtils::ciEqual(bone.mName, name))
                    return &bone;
            return nullptr;
        }

        JPH::Vec3 toMetres(const osg::Vec3f& v)
        {
            return JPH::Vec3(v.x(), v.y(), v.z()) / sJoltUnitsPerMetre;
        }

        JPH::Vec3 safeNormal(JPH::Vec3 v, JPH::Vec3 fallback)
        {
            const float len = v.Length();
            return len > 1e-5f ? v / len : fallback;
        }

        JPH::Vec3 perpendicular(JPH::Vec3 axis, JPH::Vec3 hint)
        {
            JPH::Vec3 p = hint - axis * axis.Dot(hint);
            if (p.Length() < 1e-4f)
                p = axis.GetNormalizedPerpendicular();
            return p.Normalized();
        }
    }

    RagdollBuild makeRagdoll(const std::vector<RagdollBone>& bones, float massKg, JPH::ObjectLayer layer)
    {
        RagdollBuild build;

        // Which parts exist; required ones missing means no ragdoll
        std::optional<const RagdollBone*> found[sPartCount];
        for (int i = 0; i < sPartCount; ++i)
        {
            const RagdollBone* bone = nullptr;
            if (std::string_view(sParts[i].mName) == "Chest")
            {
                bone = findBone(bones, "Bip01 Spine2");
                if (bone == nullptr)
                    bone = findBone(bones, "Bip01 Spine1");
            }
            else
                bone = findBone(bones, sParts[i].mName);
            if (bone == nullptr && sParts[i].mRequired)
                return build;
            if (bone != nullptr)
                found[i] = bone;
        }

        // World positions (metres) and rotations, scale kept aside
        JPH::Vec3 pos[sPartCount];
        JPH::Quat rot[sPartCount];
        float scale[sPartCount];
        for (int i = 0; i < sPartCount; ++i)
        {
            if (!found[i])
                continue;
            osg::Vec3f t, s;
            osg::Quat r, so;
            (*found[i])->mWorld.decompose(t, r, s, so);
            pos[i] = toMetres(t);
            rot[i] = JPH::Quat(static_cast<float>(r.x()), static_cast<float>(r.y()), static_cast<float>(r.z()),
                static_cast<float>(r.w()))
                         .Normalized();
            scale[i] = s.x();
        }

        const float size = (pos[3] - pos[0]).Length() / 0.86f;
        if (!(size > 0.1f))
            return build; // collapsed skeleton: capsules would have no size
        const JPH::Vec3 up = safeNormal(pos[3] - pos[0], JPH::Vec3::sAxisZ());
        const JPH::Vec3 right = perpendicular(up, safeNormal(pos[13] - pos[10], JPH::Vec3::sAxisX()));
        const JPH::Vec3 forward = up.Cross(right).Normalized();

        // Capsule ends in world space per part
        JPH::Vec3 a[sPartCount], b[sPartCount];
        auto tipOr = [&](int part, int tip, JPH::Vec3 dir, float length) {
            return found[tip] ? pos[tip] : pos[part] + dir * length * size;
        };
        for (int i = 0; i < sPartCount; ++i)
        {
            if (!found[i])
                continue;
            a[i] = pos[i];
            switch (i)
            {
                case 0: // pelvis: across the hips
                    a[i] = pos[10];
                    b[i] = pos[13];
                    break;
                case 1:
                    b[i] = pos[2];
                    break;
                case 2:
                    b[i] = pos[2] + (pos[3] - pos[2]) * 0.8f;
                    break;
                case 3:
                    b[i] = pos[3] + up * 0.18f * size;
                    break;
                case 4:
                case 7:
                    b[i] = pos[i + 1];
                    break;
                case 5:
                case 8:
                    b[i] = tipOr(i, i + 1, safeNormal(pos[i] - pos[i - 1], -up), 0.25f);
                    break;
                case 6:
                case 9:
                    b[i] = pos[i] + safeNormal(pos[i] - pos[i - 1], -up) * 0.1f * size;
                    break;
                case 10:
                case 13:
                    b[i] = pos[i + 1];
                    break;
                case 11:
                case 14:
                    b[i] = tipOr(i, i + 1, safeNormal(pos[i] - pos[i - 1], -up), 0.4f);
                    break;
                case 12:
                case 15:
                    b[i] = pos[i] + forward * 0.15f * size - up * 0.04f * size;
                    break;
            }
        }

        // Volumes for the mass split
        float volume[sPartCount] = {};
        float totalVolume = 0.f;
        for (int i = 0; i < sPartCount; ++i)
        {
            if (!found[i])
                continue;
            const float r = sParts[i].mRadius * size;
            const float len = (b[i] - a[i]).Length();
            volume[i] = JPH::JPH_PI * r * r * std::max(len - 2 * r, 0.f) + 4.f / 3.f * JPH::JPH_PI * r * r * r;
            totalVolume += volume[i];
        }

        build.mSettings = new JPH::RagdollSettings;
        JPH::Ref<JPH::Skeleton> skeleton = new JPH::Skeleton;
        int bodyOf[sPartCount];
        std::fill(std::begin(bodyOf), std::end(bodyOf), -1);
        for (int i = 0; i < sPartCount; ++i)
        {
            if (!found[i])
                continue;
            const int parent = sParts[i].mParent < 0 ? -1 : bodyOf[sParts[i].mParent];
            bodyOf[i] = static_cast<int>(skeleton->AddJoint((*found[i])->mName, parent));
            build.mNames.push_back((*found[i])->mName);
            build.mScales.push_back(scale[i]);
            if (i == 2)
                build.mChest = bodyOf[i];

            // Shape in body space: body sits at the bone's world transform
            const float r = sParts[i].mRadius * size;
            const JPH::Quat inv = rot[i].Conjugated();
            const JPH::Vec3 la = inv * (a[i] - pos[i]);
            const JPH::Vec3 lb = inv * (b[i] - pos[i]);
            const float len = (lb - la).Length();
            const float halfHeight = std::max(0.5f * len - r, 0.01f);
            const JPH::Vec3 axis = safeNormal(lb - la, JPH::Vec3::sAxisY());
            JPH::ShapeRefC capsule = new JPH::CapsuleShape(halfHeight, r);
            JPH::ShapeRefC shape = new JPH::RotatedTranslatedShape(
                0.5f * (la + lb), JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis), capsule);

            JPH::RagdollSettings::Part& part = build.mSettings->mParts.emplace_back();
            part.SetShape(shape);
            part.mPosition = JPH::RVec3(pos[i]);
            part.mRotation = rot[i];
            part.mMotionType = JPH::EMotionType::Dynamic;
            part.mObjectLayer = layer;
            part.mMotionQuality = JPH::EMotionQuality::LinearCast;
            part.mFriction = 0.8f;
            part.mRestitution = 0.05f;
            part.mLinearDamping = 0.1f;
            part.mAngularDamping = 0.6f;
            part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            part.mMassPropertiesOverride.mMass = std::max(massKg * volume[i] / totalVolume, 0.5f);
            // a severed bone keeps its joint but collides with nothing: a corpse must not rest on an invisible head
            if ((*found[i])->mGhost)
                part.mIsSensor = true;

            if (parent < 0)
                continue;

            // Joint at this bone's origin, twist along the child segment
            const JPH::Vec3 twist = safeNormal(b[i] - a[i], -up);
            const JPH::RVec3 at(i == 0 ? pos[0] : pos[i]);
            const Joint kind = sParts[i].mJoint;
            if (kind == Joint::Elbow || kind == Joint::Knee)
            {
                JPH::Ref<JPH::HingeConstraintSettings> hinge = new JPH::HingeConstraintSettings;
                hinge->mPoint1 = hinge->mPoint2 = at;
                hinge->mHingeAxis1 = hinge->mHingeAxis2 = right;
                hinge->mNormalAxis1 = hinge->mNormalAxis2 = perpendicular(right, twist);
                // Positive turn about `right` swings a hanging segment forward: elbows bend forward, knees back
                if (kind == Joint::Elbow)
                {
                    hinge->mLimitsMin = -3.f * sDeg;
                    hinge->mLimitsMax = 150.f * sDeg;
                }
                else
                {
                    hinge->mLimitsMin = -150.f * sDeg;
                    hinge->mLimitsMax = 3.f * sDeg;
                }
                part.mToParent = hinge;
                continue;
            }

            float cone = 25.f, plane = 25.f, twistLimit = 20.f;
            switch (kind)
            {
                case Joint::Neck:
                    cone = plane = 30.f;
                    twistLimit = 30.f;
                    break;
                case Joint::Shoulder:
                case Joint::Hip:
                    cone = plane = 80.f;
                    twistLimit = 45.f;
                    break;
                case Joint::Wrist:
                    cone = plane = 40.f;
                    twistLimit = 20.f;
                    break;
                case Joint::Ankle:
                    cone = plane = 30.f;
                    twistLimit = 15.f;
                    break;
                default:
                    break;
            }
            JPH::Ref<JPH::SwingTwistConstraintSettings> swing = new JPH::SwingTwistConstraintSettings;
            swing->mPosition1 = swing->mPosition2 = at;
            swing->mTwistAxis1 = swing->mTwistAxis2 = twist;
            swing->mPlaneAxis1 = swing->mPlaneAxis2 = perpendicular(twist, right);
            swing->mNormalHalfConeAngle = cone * sDeg;
            swing->mPlaneHalfConeAngle = plane * sDeg;
            swing->mTwistMinAngle = -twistLimit * sDeg;
            swing->mTwistMaxAngle = twistLimit * sDeg;
            part.mToParent = swing;
        }

        skeleton->CalculateParentJointIndices();
        build.mSettings->mSkeleton = skeleton;
        build.mSettings->Stabilize();
        build.mSettings->DisableParentChildCollisions();
        build.mSettings->CalculateBodyIndexToConstraintIndex();
        build.mSettings->CalculateConstraintIndexToBodyIdxPair();
        return build;
    }
}
