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
  keyframe controllers; the solver runs in the update traversal of the first bone, once per instance,
  in fixed 120 Hz steps (at most 4 per frame; damping and elasticity are per step), colliders and world
  rays evaluated once per frame. `tipMass` (mass of the end particle in link masses, mass-weighted
  constraints over `iterations` passes) makes a heavy ball lead the chain; `tipRadius` sizes it against
  colliders; a collider's `bone` may name a node of the model itself (an ancestor of the chain, e.g. the
  haft) as well as a skeleton bone.
  Format: `chains: [{bones: [...], tip: [x,y,z], damping, elasticity, stiffness, gravity, inertia,
  radius, world, maxSpeed, tipMass, iterations}]`, `colliders: [{bone, radius, offset: [x,y,z], end:
  [x,y,z]}]`. Link lengths are solved by mass (joint fixed, links 1, tip `tipMass`) over `iterations`
  passes, so a heavy ball pulls the chain straight; the kinematic joint is interpolated across the
  steps of a frame and `inertia` moves position and previous position together (no added velocity). Pilot: the Iron Flail chain
  (`meshes/w/nm/w_nm_iron_flail.nif.physics.yaml` in the Engine Test mod). World collision through a ray
  hook the physics system installs (`BonePhysicsController::setWorldRay`: floors, walls, doors; `world:
  false` per chain turns it off). `animation.getBoneTransform` and the new `animation.hasBonePhysics` also
  find nodes of attached weapons and worn parts (`Animation::findNodeAnywhere`, cached). Limit left:
  colliders on skeleton bones visited later in the same frame lag one frame.

## Point light shadows
Indoors, the strongest point lights (torches, candles, braziers) cast real-time shadows from statics,
objects, actors and the player. Off by default; `[Shadows]` settings:
- `point light shadows = false`: on/off.
- `point shadow lights = 4`: lights with a shadow at once (1 to 4), picked each frame by
  radius x intensity / distance, a newcomer needs 20% more than the light it replaces.
- `point shadow resolution = 512`: size of each cube face.
- `point shadow bias = 0.01` (fraction of the distance), `point shadow normal offset = 1.0`: against shadow acne.
- `point shadow debug = false`: tints each shadowed light red, green, blue, yellow.

Limits: interiors only; moving lights (a torch carried by the player or an NPC) cast none; an item
picked up or dropped updates its shadow only when the light changes slot or the cell changes.
Casters within 16 units (~22 cm) of the light never cast (`PointShadow::sNearPlane`): a Dwemer lamp sits
on its pipe inside a bracket, and with a 1-unit near plane that geometry filled the four side faces, so
light left only through the open top and bottom (build 12 bug, fixed in build 13).
Shaders: `lib/light/pointshadows.glsl`, hooked in the point light loop of `lib/light/lighting.glsl`
(per-pixel lighting only). A replaced `lighting.glsl` (shader packs) needs the same hook: include
`pointshadows.glsl`, multiply the light's diffuse and specular by `nmPointShadow(lightIndex, viewPos,
viewNormal)`, leave its ambient alone.

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
- `components/sceneutil/pointshadow.*`, `pointshadowmath.*`, `shadow.*`, `lightmanager.hpp` (`getLights`),
  `shader/shadermanager.*` (`Slot::PointShadowMaps`), `settings/categories/shadows.hpp`,
  `mwrender/renderingmanager.cpp`: point light shadows (cameras, slot choice, uniforms, settings).
