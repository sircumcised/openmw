-- Camera Juice: first person camera effects for OpenMW.
--
-- Effects: walking head bob, strafe tilt, turn lean, jump and landing impact (damped spring),
-- sprint FOV, damage shake (trauma model) and idle breathing sway.
--
-- The built-in camera script (scripts/omw/camera/camera.lua) resets the extra
-- pitch/yaw/roll and the first person offset in onUpdate. Built-in head bobbing
-- then adds to them in onFrame. This script also adds to them in onFrame, so it
-- stacks with head bobbing and with other mods that follow the same pattern.
--
-- Lua sign conventions (see apps/openmw/mwlua/camerabindings.cpp):
--   extra pitch > 0 looks down, extra yaw > 0 turns right,
--   extra roll > 0 lowers the left side of the view, roll < 0 lowers the right side.

local camera = require('openmw.camera')
local core = require('openmw.core')
local self = require('openmw.self')
local util = require('openmw.util')
local async = require('openmw.async')
local storage = require('openmw.storage')
local I = require('openmw.interfaces')

local Actor = require('openmw.types').Actor

local settings = storage.playerSection('SettingsCameraJuice')

local cfg = {}

local function updateSettings()
    cfg.enabled = settings:get('enabled')
    cfg.strafeTilt = math.rad(settings:get('strafeTilt'))
    cfg.turnLean = math.rad(settings:get('turnLean'))
    cfg.landingImpact = settings:get('landingImpact')
    cfg.sprintFov = math.rad(settings:get('sprintFov'))
    cfg.damageShake = settings:get('damageShake')
    cfg.idleSway = settings:get('idleSway')
    cfg.headBob = settings:get('headBob')
end

updateSettings()
settings:subscribe(async:callback(updateSettings))

-- Tuning constants. Distances are game units (about 1.4 cm each), speeds are units per second.
local TILT_RATE = 8              -- 1/s, how fast strafe tilt and turn lean follow their targets
local TURN_RATE_FOR_FULL_LEAN = 4 -- rad/s of camera yaw that gives the full turn lean
local MAX_TURN_RATE = 20         -- rad/s, larger yaw jumps (teleports, mode switches) are ignored

local SPRING_STIFFNESS = 120     -- 1/s^2, landing spring
local SPRING_DAMPING = 11        -- 1/s, about 0.5 of critical damping, so the head dips and bounces once
local LANDING_MIN_FALL_SPEED = 200 -- falls slower than this produce no dip
local LANDING_IMPULSE_PER_SPEED = 0.7
local LANDING_MAX_IMPULSE = 250
local JUMP_IMPULSE = -45         -- the head lags behind the body when a jump starts
local JUMP_MIN_UP_SPEED = 150
local DIP_PITCH_PER_UNIT = 0.006 -- rad of downward look per unit of dip

local FOV_RATE = 5               -- 1/s
local SPRINT_MIN_SPEED = 150     -- below this speed the FOV stays at base
local SPRINT_SPEED_RANGE = 250   -- speed above SPRINT_MIN_SPEED that gives the full FOV boost

local TRAUMA_DECAY = 1.5         -- 1/s, linear decay
local TRAUMA_PER_HIT = 0.2       -- trauma added by any hit
local TRAUMA_PER_HEALTH = 3      -- trauma added per fraction of max health lost
local SHAKE_MAX_ANGLE = math.rad(3)

local IDLE_SPEED = 10            -- below this speed the player counts as standing still
local IDLE_FADE_RATE = 1.5       -- 1/s
local BREATH_PERIOD = 4.5        -- seconds per breath
local BREATH_PITCH = 0.004       -- rad
local BREATH_HEIGHT = 0.6        -- units

local BOB_STEP_LENGTH = 90       -- units walked per step, same default as the built-in head bobbing
local BOB_HEIGHT = 2.5           -- units of vertical bob per step at walking speed
local BOB_SIDE = 1.2             -- units of side sway per double step at walking speed
local BOB_ROLL = math.rad(0.35)  -- roll that follows the side sway
local BOB_PITCH = math.rad(0.2)  -- small nod on each step
local BOB_WALK_SPEED = 150       -- speed that gives amplitude 1
local BOB_MAX_SCALE = 1.6        -- amplitude cap while running
local BOB_FADE_RATE = 6          -- 1/s, fade in and out on start, stop, jump and landing

-- Frame-rate independent exponential approach of `current` to `target`.
local function approach(current, target, rate, dt)
    return target + (current - target) * math.exp(-rate * dt)
end

local function wrapAngle(a)
    return (a + math.pi) % (2 * math.pi) - math.pi
end

-- Smooth pseudo-random noise in [-1, 1]: three sines with unrelated frequencies.
local function noise(t, seed)
    return (math.sin(t * 13.7 + seed) + math.sin(t * 23.1 + seed * 2.3) * 0.6 + math.sin(t * 37.9 + seed * 4.1) * 0.3)
        / 1.9
end

-- Effect state
local tilt = 0
local turnRate = 0
local prevYaw = nil

local dip, dipVelocity = 0, 0
local prevZ = nil
local wasOnGround = true
local airTime = 0
local jumpKicked = false
local peakFallSpeed = 0

local fovOffset = 0
local fovApplied = false

local trauma = 0
local shakeTime = 0
local prevHealth = nil

local idleWeight = 0
local breathTime = 0

local bobDistance = 0
local bobWeight = 0

local function resetTracking()
    prevYaw = nil
    prevZ = nil
    prevHealth = nil
    peakFallSpeed = 0
    airTime = 0
end

local function resetEffects()
    tilt, turnRate = 0, 0
    dip, dipVelocity = 0, 0
    trauma = 0
    idleWeight = 0
    bobWeight = 0
    resetTracking()
end

local function restoreFov()
    if fovApplied then
        camera.setFieldOfView(camera.getBaseFieldOfView())
        fovApplied = false
    end
    fovOffset = 0
end

local function updateTilt(dt, speed)
    local side = self.controls.sideMovement or 0
    local target = -side * cfg.strafeTilt * util.clamp(speed / 100, 0, 1)

    local yaw = camera.getYaw()
    local rawRate = 0
    if prevYaw then
        rawRate = wrapAngle(yaw - prevYaw) / dt
        if math.abs(rawRate) > MAX_TURN_RATE then rawRate = 0 end
    end
    prevYaw = yaw
    turnRate = approach(turnRate, rawRate, TILT_RATE * 1.5, dt)
    target = target - cfg.turnLean * util.clamp(turnRate / TURN_RATE_FOR_FULL_LEAN, -1, 1)

    tilt = approach(tilt, target, TILT_RATE, dt)
    return tilt
end

local function updateImpact(dt)
    local z = self.position.z
    local vz = prevZ and (z - prevZ) / dt or 0
    prevZ = z

    local onGround = Actor.isOnGround(self)
    local swimming = Actor.isSwimming(self)
    if swimming then
        peakFallSpeed = 0
        airTime = 0
    elseif not onGround then
        airTime = airTime + dt
        peakFallSpeed = math.max(peakFallSpeed, -vz)
        if wasOnGround then jumpKicked = false end
        if not jumpKicked and airTime < 0.25 and vz > JUMP_MIN_UP_SPEED then
            dipVelocity = dipVelocity + JUMP_IMPULSE * cfg.landingImpact
            jumpKicked = true
        end
    elseif not wasOnGround then
        local excess = peakFallSpeed - LANDING_MIN_FALL_SPEED
        if excess > 0 then
            local impulse = math.min(excess * LANDING_IMPULSE_PER_SPEED, LANDING_MAX_IMPULSE)
            dipVelocity = dipVelocity - impulse * cfg.landingImpact
        end
        peakFallSpeed = 0
        airTime = 0
    end
    wasOnGround = onGround or swimming

    -- Semi-implicit Euler in fixed substeps keeps the spring stable at low frame rates.
    local steps = math.max(1, math.ceil(dt / 0.01))
    local h = dt / steps
    for _ = 1, steps do
        dipVelocity = dipVelocity + (-SPRING_STIFFNESS * dip - SPRING_DAMPING * dipVelocity) * h
        dip = dip + dipVelocity * h
    end
    return dip
end

local function updateFov(dt, speed)
    local moving = (self.controls.movement or 0) > 0
    local target = 0
    if moving then
        target = cfg.sprintFov * util.clamp((speed - SPRINT_MIN_SPEED) / SPRINT_SPEED_RANGE, 0, 1)
    end
    fovOffset = approach(fovOffset, target, FOV_RATE, dt)
    if math.abs(fovOffset) > 1e-4 then
        camera.setFieldOfView(camera.getBaseFieldOfView() + fovOffset)
        fovApplied = true
    else
        restoreFov()
    end
end

local function updateTrauma(dt)
    local health = Actor.stats.dynamic.health(self)
    local current = health.current
    if prevHealth and current < prevHealth - 0.5 then
        local maxHealth = math.max(1, health.base + health.modifier)
        trauma = math.min(1, trauma + TRAUMA_PER_HIT + TRAUMA_PER_HEALTH * (prevHealth - current) / maxHealth)
    end
    prevHealth = current
    trauma = math.max(0, trauma - TRAUMA_DECAY * dt)
    shakeTime = shakeTime + dt
    -- Squaring makes small trauma subtle and big hits violent.
    return trauma * trauma * SHAKE_MAX_ANGLE * cfg.damageShake
end

local function updateIdle(dt, speed)
    local target = speed < IDLE_SPEED and 1 or 0
    idleWeight = approach(idleWeight, target, IDLE_FADE_RATE, dt)
    breathTime = (breathTime + dt) % BREATH_PERIOD
    return math.sin(breathTime / BREATH_PERIOD * 2 * math.pi) * idleWeight * cfg.idleSway
end

-- Walking head bob: the head drops on every step and sways sideways once per two steps.
-- The phase follows the distance walked, so the bob stays in step with the feet at any speed.
-- It turns itself off while the built-in OpenMW head bobbing is enabled, so the two never stack.
local function updateHeadBob(dt, speed)
    local builtinBob = I.Camera and I.Camera.isHeadBobbingEnabled and I.Camera.isHeadBobbingEnabled()
    local active = cfg.headBob > 0 and not builtinBob
    local target = 0
    if active and Actor.isOnGround(self) and not Actor.isSwimming(self) then
        target = util.clamp(speed / BOB_WALK_SPEED, 0, BOB_MAX_SCALE)
    end
    bobWeight = approach(bobWeight, target, BOB_FADE_RATE, dt)
    bobDistance = (bobDistance + speed * dt) % (BOB_STEP_LENGTH * 2)
    if bobWeight < 1e-3 then return 0, 0, 0, 0 end

    local phase = bobDistance / (BOB_STEP_LENGTH * 2) * 2 * math.pi -- one turn per two steps
    local step = math.cos(phase * 2)                                 -- 1 at mid stride, -1 at foot strike
    local side = math.sin(phase)
    local w = bobWeight * cfg.headBob
    local height = step * BOB_HEIGHT * 0.5 * w
    local sideOffset = side * BOB_SIDE * w
    local roll = -side * BOB_ROLL * w
    local pitch = -step * BOB_PITCH * 0.5 * w
    return height, sideOffset, roll, pitch
end

local function onFrame(dt)
    -- Same guard as the built-in camera script: with dt == 0 onUpdate did not reset the
    -- extra angles this frame, so adding to them would accumulate.
    if dt <= 0 or core.isWorldPaused() or I.UI.getMode() then
        resetTracking()
        return
    end
    if not cfg.enabled or camera.getMode() ~= camera.MODE.FirstPerson then
        resetEffects()
        restoreFov()
        return
    end

    local speed = Actor.getCurrentSpeed(self)

    local roll = updateTilt(dt, speed)
    local dipOffset = updateImpact(dt)
    updateFov(dt, speed)
    local shake = updateTrauma(dt)
    local breath = updateIdle(dt, speed)
    local bobHeight, bobSide, bobRoll, bobPitch = updateHeadBob(dt, speed)
    roll = roll + bobRoll

    local pitch = -dipOffset * DIP_PITCH_PER_UNIT + breath * BREATH_PITCH + bobPitch
    local yaw = 0
    if shake > 0 then
        pitch = pitch + shake * noise(shakeTime, 1.0)
        yaw = yaw + shake * noise(shakeTime, 7.0)
        roll = roll + shake * 0.5 * noise(shakeTime, 13.0)
    end

    camera.setExtraPitch(camera.getExtraPitch() + pitch)
    camera.setExtraYaw(camera.getExtraYaw() + yaw)
    camera.setExtraRoll(camera.getExtraRoll() + roll)
    camera.setFirstPersonOffset(camera.getFirstPersonOffset()
        + util.vector3(bobSide, 0, dipOffset + breath * BREATH_HEIGHT + bobHeight))
end

return {
    engineHandlers = {
        onFrame = onFrame,
        onTeleported = resetEffects,
        onActive = resetEffects,
        onInactive = restoreFov,
    },
}
