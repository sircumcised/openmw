#ifndef OPENMW_MWRENDER_DISMEMBER_H
#define OPENMW_MWRENDER_DISMEMBER_H

// NerevarMoon: dismemberment tables. A piece is a set of Bip01 bones (root first) and the body part slots
// (ESM::PRT_* indices as bits) that hang from them.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace MWRender
{
    enum class Piece
    {
        Head,
        RHand,
        LHand,
        RForearm,
        LForearm,
        RArm,
        LArm,
        RShin,
        LShin,
        RLeg,
        LLeg,
        Torso,
        Count
    };

    std::optional<Piece> parsePiece(std::string_view name);
    std::string_view pieceName(Piece piece);
    /// Bip01 bones of the piece, the cut bone first.
    const std::vector<std::string_view>& pieceBones(Piece piece);
    /// The far bone that, with the cut bone, sets the piece's axis; empty for a one-bone piece (head, hand).
    std::string_view pieceEndBone(Piece piece);
    /// `outer` holds `inner` (an arm holds its hand, the torso the head): inner's cut bone is one of outer's.
    bool pieceContains(Piece outer, Piece inner);
    /// Stump cap: neck, wrist, elbow, shoulder, knee, hip or waist.
    std::string_view pieceCap(Piece piece);
    /// Body part slots of the piece, bit i = ESM::PRT_* value i.
    std::uint32_t pieceParts(Piece piece);

    /// False when the piece, or a piece that contains it, is already severed.
    bool canSever(Piece piece, const std::vector<Piece>& severed);
    /// Slots hidden on the actor once these pieces are gone.
    std::uint32_t hiddenParts(const std::vector<Piece>& severed);
    /// Slots the clone of a new piece shows: its own, minus those already gone with earlier cuts.
    std::uint32_t clonedParts(Piece piece, const std::vector<Piece>& severed);
}

#endif
