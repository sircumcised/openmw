#include "dismember.hpp"

#include <array>

namespace MWRender
{
    namespace
    {
        // ESM::PRT_* values (components/esm3/loadarmo.hpp); npcanimation.cpp checks them with static_assert
        enum Prt : int
        {
            Head = 0,
            Hair = 1,
            Neck = 2,
            Cuirass = 3,
            RHand = 6,
            LHand = 7,
            RWrist = 8,
            LWrist = 9,
            Shield = 10,
            RForearm = 11,
            LForearm = 12,
            RUpperarm = 13,
            LUpperarm = 14,
            RFoot = 15,
            LFoot = 16,
            RAnkle = 17,
            LAnkle = 18,
            RKnee = 19,
            LKnee = 20,
            RLeg = 21,
            LLeg = 22,
            RPauldron = 23,
            LPauldron = 24,
            Weapon = 25,
        };

        constexpr std::uint32_t bits(std::initializer_list<int> prts)
        {
            std::uint32_t mask = 0;
            for (int p : prts)
                mask |= 1u << p;
            return mask;
        }

        struct Def
        {
            std::string_view mName;
            std::string_view mCap;
            std::vector<std::string_view> mBones;
            std::uint32_t mParts;
        };

        const std::array<Def, static_cast<int>(Piece::Count)>& defs()
        {
            static const std::array<Def, static_cast<int>(Piece::Count)> table = { {
                { "head", "neck", { "Bip01 Head" }, bits({ Head, Hair }) },
                { "rhand", "wrist", { "Bip01 R Hand" }, bits({ RHand }) },
                { "lhand", "wrist", { "Bip01 L Hand" }, bits({ LHand }) },
                { "rforearm", "elbow", { "Bip01 R Forearm", "Bip01 R Hand" }, bits({ RHand, RWrist, RForearm }) },
                { "lforearm", "elbow", { "Bip01 L Forearm", "Bip01 L Hand" }, bits({ LHand, LWrist, LForearm }) },
                { "rarm", "shoulder", { "Bip01 R UpperArm", "Bip01 R Forearm", "Bip01 R Hand" },
                    bits({ RHand, RWrist, RForearm, RUpperarm }) },
                { "larm", "shoulder", { "Bip01 L UpperArm", "Bip01 L Forearm", "Bip01 L Hand" },
                    bits({ LHand, LWrist, LForearm, LUpperarm }) },
                { "rshin", "knee", { "Bip01 R Calf", "Bip01 R Foot" }, bits({ RFoot, RAnkle, RKnee }) },
                { "lshin", "knee", { "Bip01 L Calf", "Bip01 L Foot" }, bits({ LFoot, LAnkle, LKnee }) },
                { "rleg", "hip", { "Bip01 R Thigh", "Bip01 R Calf", "Bip01 R Foot" },
                    bits({ RFoot, RAnkle, RKnee, RLeg }) },
                { "lleg", "hip", { "Bip01 L Thigh", "Bip01 L Calf", "Bip01 L Foot" },
                    bits({ LFoot, LAnkle, LKnee, LLeg }) },
                { "torso", "waist",
                    { "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2", "Bip01 Neck", "Bip01 Head", "Bip01 L UpperArm",
                        "Bip01 L Forearm", "Bip01 L Hand", "Bip01 R UpperArm", "Bip01 R Forearm", "Bip01 R Hand" },
                    bits({ Head, Hair, Neck, Cuirass, RHand, LHand, RWrist, LWrist, Shield, RForearm, LForearm,
                        RUpperarm, LUpperarm, RPauldron, LPauldron, Weapon }) },
            } };
            return table;
        }

        const Def& def(Piece piece)
        {
            return defs()[static_cast<int>(piece)];
        }
    }

    std::optional<Piece> parsePiece(std::string_view name)
    {
        for (int i = 0; i < static_cast<int>(Piece::Count); ++i)
            if (defs()[i].mName == name)
                return static_cast<Piece>(i);
        return std::nullopt;
    }

    std::string_view pieceName(Piece piece)
    {
        return def(piece).mName;
    }

    const std::vector<std::string_view>& pieceBones(Piece piece)
    {
        return def(piece).mBones;
    }

    std::string_view pieceCap(Piece piece)
    {
        return def(piece).mCap;
    }

    std::uint32_t pieceParts(Piece piece)
    {
        return def(piece).mParts;
    }

    bool canSever(Piece piece, const std::vector<Piece>& severed)
    {
        // the piece's cut bone already left with this or a bigger piece
        const std::string_view root = def(piece).mBones.front();
        for (Piece gone : severed)
            for (std::string_view bone : def(gone).mBones)
                if (bone == root)
                    return false;
        return true;
    }

    std::uint32_t hiddenParts(const std::vector<Piece>& severed)
    {
        std::uint32_t mask = 0;
        for (Piece gone : severed)
            mask |= def(gone).mParts;
        return mask;
    }

    std::uint32_t clonedParts(Piece piece, const std::vector<Piece>& severed)
    {
        return def(piece).mParts & ~hiddenParts(severed);
    }
}
