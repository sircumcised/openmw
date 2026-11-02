# Camera Juice

OpenMW Lua mod that adds first person camera effects.
It ports the effects from the StayAtHomeDev tutorial "Camera Juice & Effects, Godot FPS Series #2"
(https://www.youtube.com/watch?v=53Awc2twnhA) to the OpenMW camera API.

## Effects

| Effect | Trigger | Camera change |
|---|---|---|
| Strafe tilt | moving sideways | roll toward the movement side, 1.5 degrees |
| Turn lean | fast mouse or stick turn | roll toward the turn side, up to 1 degree at 4 rad/s |
| Jump and landing impact | jump start, landing after a fall over 200 units/s | head dips on a damped spring and looks down, one bounce |
| Sprint FOV | moving forward faster than 150 units/s | field of view grows up to 6 degrees at 400 units/s |
| Damage shake | health loss | trauma based shake, bigger hits shake harder, decays in under 1 s |
| Idle breathing | standing still for about 1 s | slow 4.5 s up and down motion |

All effects run only in first person.
They add to the built-in head bobbing, so both work together.
Every effect has a strength setting in Options, Scripts, Camera Juice.

## Install

Add the folder and the scripts file to `openmw.cfg`:

```
data="/path/to/mods/CameraJuice"
content=CameraJuice.omwscripts
```

The OpenMW launcher can do the same: add the folder in Data Directories, then tick `CameraJuice.omwscripts`.

## Compatibility

- Sprint FOV calls `camera.setFieldOfView`. Set Sprint FOV Boost to 0 when another mod controls the field of view.
- The mod needs an OpenMW build with Lua menu scripts (0.49 or newer).

## Files

- `CameraJuice.omwscripts`: script manifest.
- `scripts/CameraJuice/settings.lua`: settings page (menu script).
- `scripts/CameraJuice/player.lua`: the effects (player script).
- `l10n/CameraJuice/en.yaml`, `it.yaml`: English and Italian strings.
