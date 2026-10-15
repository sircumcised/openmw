# NerevarMoon engine (OpenMW 0.51.0 + additions)

Branch `nerevarmoon`, based on the tag `openmw-0.51.0`. Built for Windows by the workflow
"NerevarMoon Engine" (release `nerevarmoon-build-N`). Everything here is additive: a stock
OpenMW mod keeps working, a mod that uses these additions needs this build.

## Lua API additions
- `types.Actor.addImpulse(actor, vector3)` (global scripts, or local scripts on self): a one-shot
  velocity change in game units per second, applied at the next physics step. Up = launched like a
  jump (landing sound and fall damage work as usual); in the air = added momentum; a horizontal push
  on the ground = a slide damped by friction (`MWPhysics::sSlideFriction`, gone in about 0.2 s).
- `animation.getBoneTransform(object, bone)`: world transform (`util.transform`) of a bone, from the
  last update (one frame behind). Any object with an animation; nil if the bone is missing.
- `animation.setBoneOverride(self, bone, {rotation=, offset=} | nil)`: rotation/offset applied on top
  of the animation in the actor's local space, kept across animation changes; nil clears it.
  Bones without an animation controller are ignored (warning in the log).
- `animation.clearBoneOverrides(self)`.
- Combat core (build 4): `types.Actor.setSpeedMultiplier/getSpeedMultiplier` (applied in
  `getMaxSpeed`), `setEngineBlock/getEngineBlock` (skips `blockMeleeAttack`), `playHitReaction(actor,
  'hit'|'block'|'knockdown')` (sets the CreatureStats flags the CharacterController already plays),
  `isAttackPreparing`, `cancelAttack` (feint: aborts an attack in wind-up). `addImpulse` now takes
  `{asJump=false}`; an upward impulse from the ground otherwise costs fatigue and trains Acrobatics.

- Bone physics (build 5): a file `<model>.physics.yaml` next to a model (any data directory, VFS path)
  puts the listed bone chains under a position-based solver (gravity, damping, elasticity towards the
  rest pose, stiffness, capsule colliders on the wielder's skeleton bones). The chain nodes lose their
  keyframe controllers; the solver runs in the update traversal of the first bone, once per instance.
  Format: `chains: [{bones: [...], tip: [x,y,z], damping, elasticity, stiffness, gravity, inertia,
  radius}]`, `colliders: [{bone, radius, offset: [x,y,z], end: [x,y,z]}]`. Pilot: the Iron Flail chain
  (`meshes/w/nm/w_nm_iron_flail.nif.physics.yaml` in the Engine Test mod). Limits: no world collision
  (a flail on the ground hangs through the floor), colliders on bones visited later in the same frame
  lag one frame, `animation.getBoneTransform` does not see weapon nodes (the actor's node map is built
  from the skeleton only).

## Engine changes (all marked `NerevarMoon` in the code)
- `mwphysics/actor.hpp`: `addImpulse`/`takeImpulse` (main thread), slide velocity.
- `mwphysics/physicssystem.hpp`: `ActorFrameData::mImpulse`, `mSlide`; `PhysicsSystem::addActorImpulse`.
- `mwphysics/mtphysics.cpp`: impulse and slide copied into the frame data, slide written back.
- `mwphysics/movementsolver.cpp`: impulse applied (jump / air / slide), slide friction.
- `mwbase/world.hpp`, `mwworld/worldimp.*`: `World::addActorImpulse`.
- `mwlua/types/actor.cpp`, `files/lua_api/openmw/types.lua`: the binding and its documentation.
- `mwrender/animation.hpp/.cpp`: `getBoneWorldTransform`, `setBoneOverride`, `clearBoneOverrides`;
  one `RotateController` per overridden bone, recreated in `addControllers()` after every
  `resetActiveGroups()`.
- `mwlua/animationbindings.cpp`, `files/lua_api/openmw/animation.lua`: the bindings (writes deferred
  with `addAction`, reads straight from the node matrices).
- `mwmechanics/creaturestats.hpp`: `mSpeedMultiplier`, `mEngineBlock` (runtime only, not in saves).
- `mwclass/npc.cpp`, `mwclass/creature.cpp`: `getMaxSpeed` times the multiplier.
- `mwmechanics/combat.cpp`: `blockMeleeAttack` returns false when the engine block is disabled.
- `mwmechanics/character.*`, `actors.*`, `mechanicsmanagerimp.*`, `mwbase/mechanicsmanager.hpp`: `cancelAttack`.
- `mwphysics/physicssystem.*`: `applyJumpCost` shared by `handleJump` and `addActorImpulse(asJump)`.
- `mtphysics.cpp` (build 3): the impulse stays pending on frames without a physics step.
- `components/sceneutil/bonephysics.*` (build 5): spec parser, `attachBonePhysics`, `BonePhysicsController`;
  hooked in `Resource::SceneManager::getTemplate` right after the model is loaded, before the optimizer.
