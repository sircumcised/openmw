# NerevarMoon engine on openmw-rtx

Branch `nm-rtx`: the tag `rtx-v0.4.5` of xorza/openmw-rtx (OpenMW 0.53, Vulkan ray tracer) plus one squash commit of the NerevarMoon engine additions from branch `nm-jolt` (`f4bec41444..88c0458a58`, OpenMW 0.51 line).

Lua API added by the fork: `types.Actor.knockDown/isKnockedDown/isKnockedOut`, `types.Actor.setTimeScale/getTimeScale`, `types.Actor.sever/isSevered/isRagdoll/setLowerBodyLocked`, event `NMHitResult`, `animation.getBoneTransform/hasBonePhysics/setHeadHidden`, `animation.addVfx` option `offset`, `camera.setStaticHeadTracking`, `world.physicsImpulse/physicsSetDynamic/isRagdoll`. Engine side: Jolt Physics ragdolls and knockdown (`[Physics] jolt`), dismemberment, bone physics chains (`<model>.physics.yaml`), Bodyview (`Mask_PlayerHidden`), `[Lua] lua timing log`.

Patches to the ray tracer: the eye's view mask and the world walk skip `Mask_PlayerHidden` (`renderingmanager.cpp`, `mwrender/rtx/worldmirror.cpp`); an occluded window keeps the game running (`components/sdlutil/sdlvideowrapper.cpp`).

CI: `.github/workflows/nerevarmoon-rtx.yml` builds the Windows package on every push to `nm-rtx` and publishes the release `nerevarmoon-rtx-build-N`.

Next rtx tag: `git rebase --onto rtx-vX.Y.Z rtx-v0.4.5 nm-rtx`, resolve, push.
