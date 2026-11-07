#include "gibstate.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"

namespace ESM
{
    void GibState::load(ESMReader& esm)
    {
        mActor = esm.getFormId(true, "ACTR");
        mPiece = esm.getHNString("PIEC");
        int crushed = 0;
        esm.getHNOT(crushed, "CRSH");
        mCrushed = crushed != 0;
        mObject = RefNum();
        if (esm.peekNextSub("OBJE"))
            mObject = esm.getFormId(true, "OBJE");
    }

    void GibState::save(ESMWriter& esm) const
    {
        esm.writeFormId(mActor, true, "ACTR");
        esm.writeHNString("PIEC", mPiece);
        if (mCrushed)
            esm.writeHNT("CRSH", 1);
        if (mObject.isSet())
            esm.writeFormId(mObject, true, "OBJE");
    }
}
