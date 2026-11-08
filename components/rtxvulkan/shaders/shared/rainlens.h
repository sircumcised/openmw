#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RAINLENS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RAINLENS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// NerevarMoon: rain streaks on the lens, drawn over the toned picture (`rainlens.comp`). The same
// formulas as the 0.51 line's `nm_rain.omwfx` post-process shader, so both engines show one rain.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `rainlens.comp` binds what it reads and writes in set 0, and how many there are.
    const uint RAIN_LENS_BIND_SOURCE = 0;
    const uint RAIN_LENS_BIND_TARGET = 1;
    const uint RAIN_LENS_BINDINGS = 2;

    /// Threads along each edge of the pass's workgroup, the tone pass's.
    const uint RAIN_LENS_WORKGROUP = 8;

    /// What the pass is told.
    struct RainLensConstants
    {
        /// How strong the streaks are, 0..1, `core.weather.setRainLook`'s first argument.
        float mStreak;

        /// The simulation's seconds reduced over two, which the streaks scroll by: 1.5 cycles a
        /// second repeat exactly every two seconds, and a float holding hours would step.
        float mSeconds;

        /// Camera-space motion from Lua, x right and y forward, normalized or nought.
        vec2 mMotion;

        /// The picture's extent in pixels.
        uvec2 mSize;
    };

#ifdef RTX_HOST
    static_assert(sizeof(RainLensConstants) == 24, "RainLensConstants must be scalar-packed on every side");
}
#endif

#endif
