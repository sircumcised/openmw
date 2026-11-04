#ifndef OPENMW_ESM3_RAGDOLLSTATE_H
#define OPENMW_ESM3_RAGDOLLSTATE_H

#include <string>
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
        struct Bone
        {
            std::string mName;
            Quaternion mRotation; // relative to the actor's base node
            Vector3 mPosition;
        };

        RefNum mActor;
        std::vector<Bone> mBones;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}

#endif
