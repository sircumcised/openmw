# NerevarMoon engine (OpenMW 0.51.0 + additions)

Built for Windows by the workflow "NerevarMoon Engine" (release `nerevarmoon-build-N`). Everything here is
additive: a stock OpenMW mod keeps working, a mod that uses these additions needs this build.

## Lua API additions
- `animation.addVfx(self, model, {offset = util.transform})`: the model placed in the bone's space;
  marker `animation.vfxOffset`.
- `animation.hasBonePhysics(actor, bone)`: is the node driven by the bone physics solver
  (`<model>.physics.yaml` chains, e.g. flail chains).
- `animation.getBoneTransform(actor, bone) -> util.transform | nil`: world transform of a bone of the
  actor's current model. It is the pose drawn this frame, so a camera placed from it lands one frame later.
  The matrix includes the actor's scale (a bone-local offset scales with race height). Read only; nil if
  the bone or the model is missing.
- `animation.setHeadHidden(self, hidden)`: player only (error otherwise). Hides the head, hair and helmet
  parts from the main camera; they keep casting shadows and show in water reflections. Kept across
  equipment changes and rebuilds, cleared when the player switches to first person view. A new player
  model (game load, new game, chargen race/sex/head/hair change) starts with the head shown, so Lua calls
  it again. Applied at the start of the next frame. Marker `animation.bodyview`.

## Engine changes for Bodyview (all marked `NerevarMoon` in the code)
- `mwrender/vismask.hpp`: `Mask_PlayerHidden = (1 << 21)`.
- `mwrender/renderingmanager.cpp`: the main camera cull mask leaves out `Mask_PlayerHidden`; the shadow
  casting mask takes it with `Mask_Player`; `ShadowManager::setShadowOnlyMask(Mask_PlayerHidden)`.
- `components/sceneutil/shadow.*`, `mwshadowtechnique.*`: `setShadowOnlyMask`, bits the shadow pass casts
  even when the viewing camera culls them (the shadow cull starts from the viewing camera's mask).
- `mwrender/water.cpp`: reflections at detail 4+ draw `Mask_PlayerHidden` with `Mask_Player`.
- `mwrender/npcanimation.*`: `setHeadHidden`, re-applied in `addOrReplaceIndividualPart` and `rebuild`.
- `mwrender/animation.*`: `getBoneWorldTransform`.
- `mwlua/animationbindings.cpp`, `files/lua_api/openmw/animation.lua`: the bindings.

## Jolt Physics (branch nm-jolt, sub-project 1 of the ragdoll work)

- Jolt Physics v5.6.0 (MIT) fetched in `extern/CMakeLists.txt`, static, SSE4.2 only.
- `apps/openmw/mwphysics/joltworld.*` and `joltshapes.*`: a Jolt world beside Bullet. Bullet keeps actors, rays,
  projectiles and the navmesh. Jolt gets a copy of the loaded terrain and of the static World/Door objects
  (converted from their Bullet shapes) and simulates dynamic bodies only.
- `World::doPhysics` steps Jolt at 1/60 s after the actors and moves the simulated objects with `moveObject` and
  `rotateObject`; a body that falls asleep becomes a normal object again.
- `[Physics] jolt = true` (settings-default.cfg); false creates nothing.
- Lua (global): `world.physicsAvailable`, `world.physicsSetDynamic(object, velocity|nil)`.
