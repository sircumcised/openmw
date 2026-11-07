#ifndef GAME_MWRENDER_CAMERA_H
#define GAME_MWRENDER_CAMERA_H

#include <optional>
#include <string>

#include <osg/Matrix>
#include <osg/Vec3>
#include <osg/Vec3d>
#include <osg/ref_ptr>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class Camera;
    class Callback;
    class Node;
}

namespace MWRender
{
    class NpcAnimation;

    /// \brief Camera control
    class Camera
    {
    public:
        enum class Mode : int
        {
            Static = 0,
            FirstPerson = 1,
            ThirdPerson = 2,
            Vanity = 3,
            Preview = 4
        };

        Camera(osg::Camera* camera);
        ~Camera();

        /// Attach camera to object
        void attachTo(const MWWorld::Ptr& ptr);
        MWWorld::Ptr getTrackingPtr() const { return mTrackingPtr; }

        void setFocalPointTransitionSpeed(float v) { mFocalPointTransitionSpeedCoef = v; }
        float getFocalPointTransitionSpeed() const { return mFocalPointTransitionSpeedCoef; }
        void setFocalPointTargetOffset(const osg::Vec2d& v);
        osg::Vec2d getFocalPointTargetOffset() const { return mFocalPointTargetOffset; }
        void instantTransition();
        void showCrosshair(bool v) { mShowCrosshair = v; }

        /// Update the view matrix of \a cam
        void updateCamera(osg::Camera* cam);

        /// Reset to defaults
        void reset() { setMode(Mode::FirstPerson); }

        void rotateCameraToTrackingPtr();

        float getPitch() const { return mPitch; }
        float getYaw() const { return mYaw; }
        float getRoll() const { return mRoll; }

        void setPitch(float angle, bool force = false);
        void setYaw(float angle, bool force = false);
        void setRoll(float angle) { mRoll = angle; }

        float getExtraPitch() const { return mExtraPitch; }
        float getExtraYaw() const { return mExtraYaw; }
        float getExtraRoll() const { return mExtraRoll; }
        void setExtraPitch(float angle) { mExtraPitch = angle; }
        void setExtraYaw(float angle) { mExtraYaw = angle; }
        void setExtraRoll(float angle) { mExtraRoll = angle; }

        osg::Quat getOrient() const;

        /// @param Force view mode switch, even if currently not allowed by the animation.
        void toggleViewMode(bool force = false);
        bool toggleVanityMode(bool enable);

        void applyDeferredPreviewRotationToPlayer(float dt);
        void disableDeferredPreviewRotation() { mDeferredRotationDisabled = true; }

        /// \brief Lowers the camera for sneak.
        void setSneakOffset(float offset);

        void processViewChange();

        void update(float duration, bool paused = false);

        float getCameraDistance() const { return mCameraDistance; }
        void setPreferredCameraDistance(float v) { mPreferredCameraDistance = v; }

        void setAnimation(NpcAnimation* anim);

        osg::Vec3d getTrackedPosition() const { return mTrackedPosition; }
        const osg::Vec3d& getPosition() const { return mPosition; }
        void setStaticPosition(const osg::Vec3d& pos);
        // NerevarMoon (Bodyview): in Static mode, place the camera at the tracked actor's head bone at render time
        // plus an offset (x right, y forward along the camera yaw, z up); nullopt turns it off. `followRotation`: the
        // view also turns with the head bone (ragdoll), from the view it had when this started; back to yaw/pitch
        // over 0.3 s when it ends
        // `stabilization` 0..1: share of the head's sway relative to the body that is filtered out (0.1 s low pass)
        void setStaticHeadTracking(
            const std::optional<osg::Vec3f>& offset, bool followRotation = false, float stabilization = 0.f);
        bool hasStaticHeadTracking() const { return mStaticHeadOffset.has_value(); }

        bool isVanityOrPreviewModeEnabled() const { return mMode == Mode::Vanity || mMode == Mode::Preview; }
        Mode getMode() const { return mMode; }
        std::optional<Mode> getQueuedMode() const { return mQueuedMode; }
        void setMode(Mode mode, bool force = true);

        void allowCharacterDeferredRotation(bool v) { mDeferredRotationAllowed = v; }
        void calculateDeferredRotation();
        void setFirstPersonOffset(const osg::Vec3f& v) { mFirstPersonOffset = v; }
        osg::Vec3f getFirstPersonOffset() const { return mFirstPersonOffset; }

        int getCollisionType() const { return mCollisionType; }
        void setCollisionType(int collisionType) { mCollisionType = collisionType; }

        const osg::Matrixf& getViewMatrix() const { return mViewMatrix; }
        const osg::Matrixf& getProjectionMatrix() const { return mProjectionMatrix; }

    private:
        MWWorld::Ptr mTrackingPtr;
        osg::ref_ptr<const osg::Node> mTrackingNode;
        osg::Vec3d mTrackedPosition;
        float mHeightScale;
        int mCollisionType;

        osg::ref_ptr<osg::Camera> mCamera;

        NpcAnimation* mAnimation;

        // Always 'true' if mMode == `FirstPerson`. Also it is 'true' in `Vanity` or `Preview` modes if
        // the camera should return to `FirstPerson` view after it.
        bool mFirstPersonView;

        Mode mMode;
        std::optional<Mode> mQueuedMode;
        bool mVanityAllowed;
        bool mDeferredRotationAllowed;

        bool mProcessViewChange;

        float mHeight;
        float mPitch, mYaw, mRoll;
        float mExtraPitch = 0, mExtraYaw = 0, mExtraRoll = 0;
        bool mLockPitch = false, mLockYaw = false;
        osg::Vec3d mPosition;
        osg::Matrixf mViewMatrix;
        osg::Matrixf mProjectionMatrix;

        float mCameraDistance, mPreferredCameraDistance;

        osg::Vec3f mFirstPersonOffset{ 0, 0, 0 };
        std::optional<osg::Vec3f> mStaticHeadOffset;
        std::optional<osg::Vec3d> calculateStaticHeadPosition() const;
        bool mFollowHeadRotation = false;
        std::optional<osg::Quat> mHeadDelta; // view = mHeadDelta * head rotation
        osg::Quat mLastHeadView;
        float mHeadReturn = 0.f; // seconds of the way back to yaw/pitch left
        std::optional<osg::Quat> calculateHeadRotation() const;
        float mHeadStabilization = 0.f;
        std::optional<osg::Vec3d> mHeadOffsetFiltered; // head - tracked actor position, low passed
        std::optional<osg::Vec3d> calculateHeadBonePosition() const;
        std::optional<osg::Vec3d> trackedActorPosition() const;

        osg::Vec2d mFocalPointCurrentOffset;
        osg::Vec2d mFocalPointTargetOffset;
        float mFocalPointTransitionSpeedCoef;
        bool mSkipFocalPointTransition;

        // This fields are used to make focal point transition smooth if previous transition was not finished.
        float mPreviousTransitionInfluence;
        osg::Vec2d mFocalPointTransitionSpeed;
        osg::Vec2d mPreviousTransitionSpeed;
        osg::Vec2d mPreviousExtraOffset;

        bool mShowCrosshair;

        osg::Vec3d calculateTrackedPosition() const;
        osg::Vec3d calculateFirstPersonPosition(const osg::Vec3d& trackedPosition) const;
        osg::Vec3d getFocalPointOffset() const;
        void updateFocalPointOffset(float duration);
        void updatePosition();

        osg::ref_ptr<osg::Callback> mUpdateCallback;

        // Used to rotate player to the direction of view after exiting preview or vanity mode.
        osg::Vec3f mDeferredRotation;
        bool mDeferredRotationDisabled;
    };
}

#endif
