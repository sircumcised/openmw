#ifndef OPENMW_MWPHYSICS_JOLTRAGDOLL_H
#define OPENMW_MWPHYSICS_JOLTRAGDOLL_H

// NerevarMoon: Jolt ragdoll description built from an actor's live Bip01 bones. Internal to joltworld.cpp.

#include <string>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Ragdoll/Ragdoll.h>

#include "joltworld.hpp"

namespace MWPhysics
{
    struct RagdollBuild
    {
        JPH::Ref<JPH::RagdollSettings> mSettings;
        std::vector<std::string> mNames; // bone name per body index
        std::vector<float> mScales; // uniform scale of each bone's world matrix
        int mChest = -1; // body index of the chest
        int mPelvis = 0;
    };

    /// Null mSettings when a required bone is missing. Body i sits exactly at bone mNames[i]'s world transform.
    RagdollBuild makeRagdoll(const std::vector<RagdollBone>& bones, float massKg, JPH::ObjectLayer layer);
}

#endif
