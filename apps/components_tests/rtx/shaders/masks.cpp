#include <gtest/gtest.h>

#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx::Shaders
{
    namespace
    {
        /// **A shadow-only instance is met by every ray but the eye's.** NerevarMoon's Bodyview
        /// hides the player's head from the eye and still wants its shadow on the ground, so the
        /// head is placed on `MASK_SHADOW_ONLY`: the camera's sprite bit, which no instance ever
        /// carried. The eye's ray (`worldMask`) drops it, the shadow rays, the bounces and the
        /// water's rays (`solidMask`) keep it, and a camera that draws no sprites casts no
        /// shadow of one either.
        TEST(RtxShadersMasksTest, aShadowOnlyInstanceIsMetByEveryRayButTheEyes)
        {
            EXPECT_EQ(MASK_SHADOW_ONLY, MASK_PARTICLE);
            EXPECT_EQ(classBit(InstanceClass::ShadowOnly), MASK_SHADOW_ONLY);

            const uint eye = MASK_EVERY_CLASS;
            EXPECT_EQ(worldMask(eye) & MASK_SHADOW_ONLY, 0u);
            EXPECT_EQ(worldMask(eye) & MASK_ACTOR, MASK_ACTOR);
            EXPECT_EQ(worldMask(eye) & MASK_FIRST_PERSON, 0u);

            EXPECT_EQ(solidMask(eye) & MASK_SHADOW_ONLY, MASK_SHADOW_ONLY);
            EXPECT_EQ(solidMask(eye) & (MASK_FIRST_PERSON | MASK_WATER), 0u);
            EXPECT_EQ(solidMask(eye & ~MASK_PARTICLE) & MASK_SHADOW_ONLY, 0u);

            EXPECT_EQ(shelterMask(eye), MASK_STATIC);
        }
    }
}
