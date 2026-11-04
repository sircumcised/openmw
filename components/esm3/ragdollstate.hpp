#ifndef OPENMW_ESM3_RAGDOLLSTATE_H
#define OPENMW_ESM3_RAGDOLLSTATE_H

#include <string>
#include <utility>
#include <vector>

#include "components/esm/quaternion.hpp"
#include "components/esm/vector3.hpp"

#include "refnum.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    /// NerevarMoon: pose of a ragdoll corpse (saved game record NMRD). Stock OpenMW skips the record.
    struct RagdollState
    {
        RefNum mActor;
        Vector3 mPelvisPosition; // local position of Bip01 Pelvis
        std::vector<std::pair<std::string, Quaternion>> mBones; // local rotation per bone name

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}

#endif
