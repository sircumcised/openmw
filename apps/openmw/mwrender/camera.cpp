#include "camera.hpp"

#include <osg/Camera>

#include <components/misc/mathutil.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

#include "../mwmechanics/movement.hpp"

#include "../mwphysics/raycasting.hpp"

#include "npcanimation.hpp"

namespace
{

    class UpdateRenderCameraCallback : public SceneUtil::NodeCallback<UpdateRenderCameraCallback, osg::Camera*>
    {
    public:
        UpdateRenderCameraCallback(MWRender::Camera* cam)
            : mCamera(cam)
        {
        }

        void operator()(osg::Camera* cam, osg::NodeVisitor* nv)
        {
            // traverse first to update animations, in case the camera is attached to an animated node
            traverse(cam, nv);

            mCamera->updateCamera(cam);
        }

    private:
        MWRender::Camera* mCamera;
    };

}

namespace MWRender
{

    Camera::Camera(osg::Camera* camera)
        : mHeightScale(1.f)
        , mCollisionType(
              (MWPhysics::CollisionType::CollisionType_Default & ~MWPhysics::CollisionType::CollisionType_Actor)
              | MWPhysics::CollisionType_CameraOnly)
        , mCamera(camera)
        , mAnimation(nullptr)
        , mFirstPersonView(true)
        , mMode(Mode::FirstPerson)
        , mVanityAllowed(true)
        , mDeferredRotationAllowed(true)
        , mProcessViewChange(false)
        , mHeight(124.f)
        , mPitch(0.f)
        , mYaw(0.f)
        , mRoll(0.f)
        , mCameraDistance(0.f)
        , mPreferredCameraDistance(0.f)
        , mFocalPointCurrentOffset(osg::Vec2d())
        , mFocalPointTargetOffset(osg::Vec2d())
        , mFocalPointTransitionSpeedCoef(1.f)
        , mSkipFocalPointTransition(true)
        , mPreviousTransitionInfluence(0.f)
        , mShowCrosshair(false)
        , mDeferredRotation(osg::Vec3f())
        , mDeferredRotationDisabled(false)
    {
        mUpdateCallback = new UpdateRenderCameraCallback(this);
        mCamera->addUpdateCallback(mUpdateCallback);
    }

    Camera::~Camera()
    {
        mCamera->removeUpdateCallback(mUpdateCallback);
    }

    osg::Vec3d Camera::calculateTrackedPosition() const
    {
        if (!mTrackingNode)
            return osg::Vec3d();
        osg::NodePathList nodepaths = mTrackingNode->getParentalNodePaths();
        if (nodepaths.empty())
            return osg::Vec3d();
        osg::Matrix worldMat = osg::computeLocalToWorld(nodepaths[0]);
        osg::Vec3d res = worldMat.getTrans();
        if (mMode != Mode::FirstPerson)
            res.z() += mHeight * mHeightScale;
        return res;
    }

    osg::Vec3d Camera::getFocalPointOffset() const
    {
        osg::Vec3d offset;
        offset.x() = mFocalPointCurrentOffset.x() * cos(mYaw);
        offset.y() = mFocalPointCurrentOffset.x() * sin(mYaw);
        offset.z() = mFocalPointCurrentOffset.y();
        return offset;
    }

    namespace
    {
        constexpr float sHeadReturnTime = 0.3f; // seconds from the head's view back to yaw/pitch
    }

    void Camera::setStaticHeadTracking(const std::optional<osg::Vec3f>& offset, bool followRotation)
    {
        followRotation = followRotation && offset.has_value();
        if (followRotation && !mFollowHeadRotation)
            mHeadDelta.reset();
        else if (!followRotation && mFollowHeadRotation && mHeadDelta)
            mHeadReturn = sHeadReturnTime;
        mFollowHeadRotation = followRotation;
        mStaticHeadOffset = offset;
    }

    std::optional<osg::Quat> Camera::calculateHeadRotation() const
    {
        if (!mAnimation)
            return std::nullopt;
        const osg::Node* head = mAnimation->getNode("Bip01 Head");
        if (!head)
            return std::nullopt;
        osg::NodePathList nodepaths = head->getParentalNodePaths();
        if (nodepaths.empty())
            return std::nullopt;
        return osg::computeLocalToWorld(nodepaths[0]).getRotate();
    }

    void Camera::updateCamera(osg::Camera* cam)
    {
        osg::Quat orient = getOrient();
        if (mMode == Mode::Static && mStaticHeadOffset && mFollowHeadRotation)
        {
            // NerevarMoon: ragdoll view; the head's turn from the moment this started turns the view
            if (std::optional<osg::Quat> head = calculateHeadRotation())
            {
                if (!mHeadDelta)
                    mHeadDelta = orient * head->inverse();
                orient = *mHeadDelta * *head;
                mLastHeadView = orient;
            }
        }
        else if (mHeadReturn > 0.f)
        {
            osg::Quat back;
            back.slerp(1.f - mHeadReturn / sHeadReturnTime, mLastHeadView, orient);
            orient = back;
        }
        osg::Vec3d forward = orient * osg::Vec3d(0, 1, 0);
        osg::Vec3d up = orient * osg::Vec3d(0, 0, 1);

        osg::Vec3d pos = mPosition;
        if (mMode == Mode::Static && mStaticHeadOffset)
        {
            // NerevarMoon: same reason as the first person hack below; a Lua-set position is one frame behind the
            // body and its animation, and the body then shakes in front of the camera by speed * frame time
            if (std::optional<osg::Vec3d> head = calculateStaticHeadPosition())
                pos = *head;
        }
        else if (mMode == Mode::FirstPerson)
        {
            // It is a hack. Camera position depends on neck animation.
            // Animations are updated in OSG cull traversal and in order to avoid 1 frame delay we
            // recalculate the position here. Note that it becomes different from mPosition that
            // is used in other parts of the code.
            // TODO: detach camera from OSG animation and get rid of this hack.
            osg::Vec3d recalculatedTrackedPosition = calculateTrackedPosition();
            pos = calculateFirstPersonPosition(recalculatedTrackedPosition);
        }
        cam->setViewMatrixAsLookAt(pos, pos + forward, up);
        mViewMatrix = cam->getViewMatrix();
        mProjectionMatrix = cam->getProjectionMatrix();
    }

    void Camera::update(float duration, bool paused)
    {
        mLockPitch = mLockYaw = false;
        if (mQueuedMode && mAnimation->upperBodyReady())
            setMode(*mQueuedMode);
        if (mProcessViewChange)
            processViewChange();

        // only show the crosshair in game mode
        MWBase::WindowManager* wm = MWBase::Environment::get().getWindowManager();
        wm->showCrosshair(!wm->isGuiMode() && mShowCrosshair);

        if (!paused)
        {
            updateFocalPointOffset(duration);
            mHeadReturn = std::max(mHeadReturn - duration, 0.f);
        }
        updatePosition();
    }

    osg::Vec3d Camera::calculateFirstPersonPosition(const osg::Vec3d& trackedPosition) const
    {
        osg::Vec3d res = trackedPosition;
        osg::Vec2f horizontalOffset
            = Misc::rotateVec2f(osg::Vec2f(mFirstPersonOffset.x(), mFirstPersonOffset.y()), mYaw);
        res.x() += horizontalOffset.x();
        res.y() += horizontalOffset.y();
        res.z() += mFirstPersonOffset.z();
        return res;
    }

    std::optional<osg::Vec3d> Camera::calculateStaticHeadPosition() const
    {
        if (!mAnimation || !mStaticHeadOffset)
            return std::nullopt;
        const osg::Node* head = mAnimation->getNode("Bip01 Head");
        if (!head)
            return std::nullopt;
        osg::NodePathList nodepaths = head->getParentalNodePaths();
        if (nodepaths.empty())
            return std::nullopt;
        osg::Vec3d res = osg::computeLocalToWorld(nodepaths[0]).getTrans();
        const osg::Vec3f& off = *mStaticHeadOffset;
        osg::Vec2f horizontalOffset = Misc::rotateVec2f(osg::Vec2f(off.x(), off.y()), mYaw);
        res.x() += horizontalOffset.x();
        res.y() += horizontalOffset.y();
        res.z() += off.z();
        return res;
    }

    void Camera::updatePosition()
    {
        mTrackedPosition = calculateTrackedPosition();
        if (mMode == Mode::Static)
        {
            // sound listener and the rest of the frame read mPosition: give them the tracked head too
            if (std::optional<osg::Vec3d> head = calculateStaticHeadPosition())
                mPosition = *head;
            return;
        }
        if (mMode == Mode::FirstPerson)
        {
            mPosition = calculateFirstPersonPosition(mTrackedPosition);
            mCameraDistance = 0;
            return;
        }

        constexpr float cameraObstacleLimit = 5.0f;
        constexpr float focalObstacleLimit = 10.f;

        const auto* rayCasting = MWBase::Environment::get().getWorld()->getRayCasting();

        // Adjust focal point to prevent clipping.
        osg::Vec3d focalOffset = getFocalPointOffset();
        osg::Vec3d focal = mTrackedPosition + focalOffset;
        focalOffset.z() += 10.f; // Needed to avoid camera clipping through the ceiling because
                                 // character's head can be a bit higher than the collision area.
        double offsetLen = focalOffset.length();
        if (offsetLen > 0)
        {
            MWPhysics::RayCastingResult result
                = rayCasting->castSphere(focal - focalOffset, focal, focalObstacleLimit, mCollisionType);
            if (result.mHit)
            {
                double adjustmentCoef
                    = -(result.mHitPos + result.mHitNormal * focalObstacleLimit - focal).length() / offsetLen;
                focal += focalOffset * std::max(-1.0, adjustmentCoef);
            }
        }

        // Adjust camera distance.
        mCameraDistance = mPreferredCameraDistance;
        osg::Quat orient
            = osg::Quat(mPitch + mExtraPitch, osg::Vec3d(1, 0, 0)) * osg::Quat(mYaw + mExtraYaw, osg::Vec3d(0, 0, 1));
        osg::Vec3d offset = orient * osg::Vec3d(0.f, -mCameraDistance, 0.f);
        MWPhysics::RayCastingResult result
            = rayCasting->castSphere(focal, focal + offset, cameraObstacleLimit, mCollisionType);
        if (result.mHit)
        {
            mCameraDistance = (result.mHitPos + result.mHitNormal * cameraObstacleLimit - focal).length();
            offset = orient * osg::Vec3d(0.f, -mCameraDistance, 0.f);
        }

        mPosition = focal + offset;
    }

    osg::Quat Camera::getOrient() const
    {
        return osg::Quat(mRoll + mExtraRoll, osg::Vec3d(0, 1, 0)) * osg::Quat(mPitch + mExtraPitch, osg::Vec3d(1, 0, 0))
            * osg::Quat(mYaw + mExtraYaw, osg::Vec3d(0, 0, 1));
    }

    void Camera::setMode(Mode newMode, bool force)
    {
        if (mMode == newMode)
        {
            mQueuedMode = std::nullopt;
            return;
        }
        Mode oldMode = mMode;
        if (!force && (newMode == Mode::FirstPerson || oldMode == Mode::FirstPerson) && mAnimation
            && !mAnimation->upperBodyReady())
        {
            // Changing the view will stop all playing animations, so if we are playing
            // anything important, queue the view change for later
            mQueuedMode = newMode;
            return;
        }
        mMode = newMode;
        mQueuedMode = std::nullopt;
        if (newMode == Mode::FirstPerson)
            mFirstPersonView = true;
        else if (newMode == Mode::ThirdPerson)
            mFirstPersonView = false;
        calculateDeferredRotation();
        if (oldMode == Mode::FirstPerson || newMode == Mode::FirstPerson)
        {
            instantTransition();
            mProcessViewChange = true;
        }
    }

    void Camera::setFocalPointTargetOffset(const osg::Vec2d& v)
    {
        mFocalPointTargetOffset = v;
        mPreviousTransitionSpeed = mFocalPointTransitionSpeed;
        mPreviousTransitionInfluence = 1.0f;
    }

    void Camera::updateFocalPointOffset(float duration)
    {
        if (duration <= 0)
            return;

        if (mSkipFocalPointTransition)
        {
            mSkipFocalPointTransition = false;
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
            mFocalPointCurrentOffset = mFocalPointTargetOffset;
        }

        osg::Vec2d oldOffset = mFocalPointCurrentOffset;

        if (mPreviousTransitionInfluence > 0)
        {
            mFocalPointCurrentOffset -= mPreviousExtraOffset;
            mPreviousExtraOffset
                = mPreviousExtraOffset / mPreviousTransitionInfluence + mPreviousTransitionSpeed * duration;
            mPreviousTransitionInfluence
                = std::max(0.f, mPreviousTransitionInfluence - duration * mFocalPointTransitionSpeedCoef);
            mPreviousExtraOffset *= mPreviousTransitionInfluence;
            mFocalPointCurrentOffset += mPreviousExtraOffset;
        }

        osg::Vec2d delta = mFocalPointTargetOffset - mFocalPointCurrentOffset;
        if (delta.length2() > 0)
        {
            double coef = duration * (1.0 + 5.0 / delta.length()) * mFocalPointTransitionSpeedCoef
                * (1.0f - mPreviousTransitionInfluence);
            mFocalPointCurrentOffset += delta * std::min(coef, 1.0);
        }
        else
        {
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
        }

        mFocalPointTransitionSpeed = (mFocalPointCurrentOffset - oldOffset) / duration;
    }

    void Camera::toggleViewMode(bool force)
    {
        setMode(mFirstPersonView ? Mode::ThirdPerson : Mode::FirstPerson, force);
    }

    bool Camera::toggleVanityMode(bool enable)
    {
        if (!enable)
            setMode(mFirstPersonView ? Mode::FirstPerson : Mode::ThirdPerson, false);
        else if (mVanityAllowed)
            setMode(Mode::Vanity, false);
        return (mMode == Mode::Vanity) == enable;
    }

    void Camera::setSneakOffset(float offset)
    {
        mAnimation->setFirstPersonOffset(osg::Vec3f(0, 0, -offset));
    }

    void Camera::setYaw(float angle, bool force)
    {
        if (!mLockYaw || force)
            mYaw = static_cast<float>(Misc::normalizeAngle(angle));
        if (force)
            mLockYaw = true;
    }

    void Camera::setPitch(float angle, bool force)
    {
        const float epsilon = 0.000001f;
        float limit = static_cast<float>(osg::PI_2) - epsilon;
        if (!mLockPitch || force)
            mPitch = std::clamp(angle, -limit, limit);
        if (force)
            mLockPitch = true;
    }

    void Camera::setStaticPosition(const osg::Vec3d& pos)
    {
        if (mMode != Mode::Static)
            throw std::runtime_error("setStaticPosition can be used only if camera is in Static mode");
        mPosition = pos;
    }

    void Camera::setAnimation(NpcAnimation* anim)
    {
        mAnimation = anim;
        mProcessViewChange = true;
    }

    void Camera::processViewChange()
    {
        if (mTrackingPtr.isEmpty())
            return;
        if (mMode == Mode::FirstPerson)
        {
            mAnimation->setViewMode(NpcAnimation::VM_FirstPerson);
            mTrackingNode = mAnimation->getNode("Camera");
            if (!mTrackingNode)
                mTrackingNode = mAnimation->getNode("Head");
            mHeightScale = 1.f;
        }
        else
        {
            mAnimation->setViewMode(NpcAnimation::VM_Normal);
            SceneUtil::PositionAttitudeTransform* transform = mTrackingPtr.getRefData().getBaseNode();
            mTrackingNode = transform;
            if (transform)
                mHeightScale = transform->getScale().z();
            else
                mHeightScale = 1.f;
        }
        mProcessViewChange = false;
    }

    void Camera::applyDeferredPreviewRotationToPlayer(float dt)
    {
        if (mMode != Mode::ThirdPerson || mTrackingPtr.isEmpty())
            return;

        osg::Vec3f rot = mDeferredRotation;
        float delta = rot.normalize();
        delta = std::min(delta, (delta + 1.f) * 3 * dt);
        rot *= delta;
        mDeferredRotation -= rot;

        if (mDeferredRotationDisabled)
        {
            mDeferredRotationDisabled = delta > 0.0001;
            rotateCameraToTrackingPtr();
            return;
        }

        auto& movement = mTrackingPtr.getClass().getMovementSettings(mTrackingPtr);
        movement.mRotation[0] += rot.x();
        movement.mRotation[1] += rot.y();
        movement.mRotation[2] += rot.z();
        if (std::abs(mDeferredRotation.z()) > 0.0001)
        {
            float s = std::sin(mDeferredRotation.z());
            float c = std::cos(mDeferredRotation.z());
            float x = movement.mPosition[0];
            float y = movement.mPosition[1];
            movement.mPosition[0] = x * c + y * s;
            movement.mPosition[1] = x * -s + y * c;
        }
    }

    void Camera::rotateCameraToTrackingPtr()
    {
        if (mTrackingPtr.isEmpty())
            return;
        // NerevarMoon: with head tracking the Static camera follows the body's rotation when it is applied. The Lua
        // setYaw/setPitch from onFrame read the rotation before this frame's mouse turn and lock it (force), so the
        // body swayed in front of the camera while turning; force overrides that stale value
        const bool headTracked = mMode == Mode::Static && mStaticHeadOffset.has_value();
        if (mMode == Mode::Static && !headTracked)
            return;
        setPitch(-mTrackingPtr.getRefData().getPosition().rot[0] - mDeferredRotation.x(), headTracked);
        setYaw(-mTrackingPtr.getRefData().getPosition().rot[2] - mDeferredRotation.z(), headTracked);
    }

    void Camera::instantTransition()
    {
        mSkipFocalPointTransition = true;
        mDeferredRotationDisabled = false;
        mDeferredRotation = osg::Vec3f();
        rotateCameraToTrackingPtr();
    }

    void Camera::calculateDeferredRotation()
    {
        if (mMode == Mode::Static)
        {
            mDeferredRotation = osg::Vec3f();
            return;
        }
        MWWorld::Ptr ptr = mTrackingPtr;
        if (mMode == Mode::Preview || mMode == Mode::Vanity || ptr.isEmpty())
            return;
        if (mFirstPersonView)
        {
            instantTransition();
            return;
        }

        mDeferredRotation.x()
            = static_cast<float>(Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[0] - mPitch));
        mDeferredRotation.z() = static_cast<float>(Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[2] - mYaw));
        if (!mDeferredRotationAllowed)
            mDeferredRotationDisabled = true;
    }

}
