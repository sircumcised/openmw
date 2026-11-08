#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/classmasks.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/scene.h>

namespace MWRender
{
    namespace
    {
        /// **What a view's mask says to the trace.** The local map's inclusion mask is the ground,
        /// the buildings and the still water: its rays meet the statics and the water and nothing
        /// else, and no lamp lights it, because it leaves out `Mask_Lighting`. The game's own eye,
        /// which leaves nothing out, meets every class, draws the sprites, and is lit by every lamp.
        /// Under `tws` it meets everything but the statics.
        TEST(MWRenderClassMasksTest, aViewsMaskSaysWhichClassesItMeetsAndWhetherLampsLightIt)
        {
            const ViewDescription map
                = describeView(Mask_Scene | Mask_SimpleWater | Mask_Terrain | Mask_Object | Mask_Static);
            EXPECT_EQ(map.mRayMask, Rtx::classBit(Rtx::InstanceClass::Static) | Rtx::Shaders::MASK_WATER);
            EXPECT_FALSE(map.mLamps);

            const ViewDescription eye = describeView(~0u);
            EXPECT_EQ(eye.mRayMask,
                Rtx::classBit(Rtx::InstanceClass::Static) | Rtx::classBit(Rtx::InstanceClass::Actor)
                    | Rtx::classBit(Rtx::InstanceClass::Effect) | Rtx::classBit(Rtx::InstanceClass::FirstPerson)
                    | Rtx::Shaders::MASK_WATER | Rtx::Shaders::MASK_PARTICLE);
            EXPECT_TRUE(eye.mLamps);

            // `tws`: the statics' class goes, and the player keeps the actors' class on.
            const ViewDescription hidden = describeView(~sToggleWorldMask);
            EXPECT_EQ(hidden.mRayMask,
                Rtx::classBit(Rtx::InstanceClass::Actor) | Rtx::classBit(Rtx::InstanceClass::Effect)
                    | Rtx::classBit(Rtx::InstanceClass::FirstPerson) | Rtx::Shaders::MASK_WATER
                    | Rtx::Shaders::MASK_PARTICLE);
            EXPECT_TRUE(hidden.mLamps);
        }

        /// **The hidden head is a class of its own, and no view's ray mask asks for it.**
        /// NerevarMoon's Bodyview hides the player's head under `Mask_PlayerHidden`, which the eye's
        /// cull mask leaves out; the tracer walks it anyway and stands it shadow-only
        /// (`Rtx::InstanceClass::ShadowOnly`). Its bit is the sprites' camera bit, so `describeView`
        /// never sets it for the class: a view that keeps the hidden parts asks no sprites for them.
        TEST(MWRenderClassMasksTest, theHiddenHeadIsShadowOnlyAndNoViewAsksForItsBit)
        {
            const auto shadowOnly = std::find_if(sClassMasks.begin(), sClassMasks.end(),
                [](const ClassMask& held) { return held.mClass == Rtx::InstanceClass::ShadowOnly; });
            ASSERT_NE(shadowOnly, sClassMasks.end());
            EXPECT_EQ(shadowOnly->mNodes, Mask_PlayerHidden);

            EXPECT_EQ(describeView(Mask_Scene | Mask_PlayerHidden).mRayMask, 0u);
            EXPECT_EQ(describeView(~0u).mRayMask, describeView(~static_cast<unsigned int>(Mask_PlayerHidden)).mRayMask);
        }
    }
}
