#ifndef OPENMW_ESM3_GIBSTATE_H
#define OPENMW_ESM3_GIBSTATE_H

#include <string>

#include "refnum.hpp"

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    /// NerevarMoon: one severed piece (saved game record NMGB): the actor it came from, which piece, and the
    /// piece's object while it lies in the world (unset once picked up). Stock OpenMW skips the record.
    struct GibState
    {
        RefNum mActor;
        std::string mPiece;
        bool mCrushed = false;
        RefNum mObject;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}

#endif
