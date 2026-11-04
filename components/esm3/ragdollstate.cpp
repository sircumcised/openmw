#include "ragdollstate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    void RagdollState::load(ESMReader& esm)
    {
        mActor = esm.getFormId(true, "ACTR");
        esm.getHNT("PELV", mPelvisPosition.mValues);
        mBones.clear();
        while (esm.isNextSub("BONE"))
        {
            std::string name = esm.getHString();
            Quaternion rotation;
            esm.getHNT("ROTA", rotation.mValues);
            mBones.emplace_back(std::move(name), rotation);
        }
    }

    void RagdollState::save(ESMWriter& esm) const
    {
        esm.writeFormId(mActor, true, "ACTR");
        esm.writeHNT("PELV", mPelvisPosition);
        for (const auto& [name, rotation] : mBones)
        {
            esm.writeHNString("BONE", name);
            esm.writeHNT("ROTA", rotation);
        }
    }
}
