#include "ragdollstate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    void RagdollState::load(ESMReader& esm)
    {
        mActor = esm.getFormId(true, "ACTR");
        mBones.clear();
        while (esm.isNextSub("BONE"))
        {
            Bone& bone = mBones.emplace_back();
            bone.mName = esm.getHString();
            esm.getHNT("ROTA", bone.mRotation.mValues);
            esm.getHNT("POSI", bone.mPosition.mValues);
        }
    }

    void RagdollState::save(ESMWriter& esm) const
    {
        esm.writeFormId(mActor, true, "ACTR");
        for (const Bone& bone : mBones)
        {
            esm.writeHNString("BONE", bone.mName);
            esm.writeHNT("ROTA", bone.mRotation);
            esm.writeHNT("POSI", bone.mPosition);
        }
    }
}
