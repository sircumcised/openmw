# NerevarMoon engine (OpenMW 0.51.0 + additions)

Built for Windows by the workflow "NerevarMoon Engine" (release `nerevarmoon-build-N`). Everything here is
additive: a stock OpenMW mod keeps working, a mod that uses these additions needs this build.

## Lua API additions
- `animation.addVfx(self, model, {offset = util.transform})`: the model placed in the bone's space;
  marker `animation.vfxOffset`.
- `animation.hasBonePhysics(actor, bone)`: is the node driven by the bone physics solver
  (`<model>.physics.yaml` chains, e.g. flail chains).
- `animation.getBoneTransform(actor, bone) -> util.transform | nil`: world transform of a bone of the
  actor's current model, from the last update (one frame behind). Read only; nil if the bone or the
  model is missing.
- `animation.setHeadHidden(self, hidden)`: player only (error otherwise). Hides the head, hair and helmet
  parts from the main camera; they keep casting shadows and show in water reflections. Kept across
  equipment changes and rebuilds, cleared when the player switches to first person view. Marker
  `animation.bodyview`.

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
