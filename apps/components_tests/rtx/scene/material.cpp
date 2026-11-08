#include <gtest/gtest.h>

#include <components/rtx/scene/material.hpp>
#include <components/rtx/shaders/look.h>

namespace Rtx
{
    namespace
    {
        /// **A blended sheet keeps its glow at one, an opaque surface keeps the tracer's eight.**
        /// NerevarMoon: Morrowind's sparks, smoke, blood and dust are `NiAlphaProperty` blends whose
        /// `NiMaterialProperty` emissive of 0.5 to 1 was a mild self-light under the rasterizer;
        /// `Shaders::EMISSIVE_INTENSITY` turns each into a lamp under the tracer, and an additive one
        /// into a burst lamp of its own. The share undoes the gain for blends alone: a glowing window
        /// or a lava flow is opaque and keeps the eight, and a cutout (foliage) is opaque to the eye.
        TEST(RtxMaterialTest, aBlendedSheetKeepsItsGlowAtOneAndAnOpaqueSurfaceKeepsEight)
        {
            EXPECT_FLOAT_EQ(sheetGlowShare(AlphaMode::Blend) * Shaders::EMISSIVE_INTENSITY, 1.0f);
            EXPECT_FLOAT_EQ(sheetGlowShare(AlphaMode::Opaque), 1.0f);
            EXPECT_FLOAT_EQ(sheetGlowShare(AlphaMode::Cutout), 1.0f);
        }
    }
}
