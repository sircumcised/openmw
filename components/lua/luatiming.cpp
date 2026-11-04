#include "luatiming.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <utility>

namespace LuaUtil
{
    namespace
    {
        struct Entry
        {
            long long mCalls = 0;
            double mTotal = 0;
            double mMax = 0;
        };

        std::map<std::pair<int, std::string>, Entry, std::less<>> sEntries;
        unsigned sFrames = 0;
        int sDepth = 0;
        std::string sLabel = "callback";
    }

    void LuaTiming::add(int scriptIndex, std::string_view label, double seconds)
    {
        auto key = std::make_pair(scriptIndex, std::string(label));
        Entry& e = sEntries[key];
        ++e.mCalls;
        e.mTotal += seconds;
        e.mMax = std::max(e.mMax, seconds);
    }

    void LuaTiming::addFrame()
    {
        ++sFrames;
    }

    std::vector<std::string> LuaTiming::formatAndReset(
        const std::function<std::string(int)>& scriptName, std::size_t maxEntries)
    {
        std::vector<std::pair<const std::pair<int, std::string>*, const Entry*>> sorted;
        sorted.reserve(sEntries.size());
        for (const auto& [key, entry] : sEntries)
            sorted.emplace_back(&key, &entry);
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second->mTotal > b.second->mTotal; });

        std::vector<std::string> lines;
        lines.push_back(std::format("NMLUA|frames={}", sFrames));
        const double frames = sFrames > 0 ? sFrames : 1;
        for (std::size_t i = 0; i < sorted.size() && i < maxEntries; ++i)
        {
            const auto& [key, e] = sorted[i];
            lines.push_back(std::format("NMLUA|{}|{}|calls={}|ms_total={:.3f}|ms_per_frame={:.3f}|ms_max={:.3f}",
                scriptName(key->first), key->second, e->mCalls, e->mTotal * 1000, e->mTotal * 1000 / frames,
                e->mMax * 1000));
        }
        reset();
        return lines;
    }

    void LuaTiming::reset()
    {
        sEntries.clear();
        sFrames = 0;
    }

    ScopedLuaTiming::ScopedLuaTiming(int scriptIndex)
    {
        if (!LuaTiming::sEnabled || scriptIndex < 0)
            return;
        mCounted = true;
        if (sDepth++ > 0)
            return; // nested call: the outer measure already covers it
        mScriptIndex = scriptIndex;
        mStart = std::chrono::steady_clock::now();
    }

    ScopedLuaTiming::~ScopedLuaTiming()
    {
        if (!mCounted)
            return;
        --sDepth;
        if (mScriptIndex >= 0)
            LuaTiming::add(mScriptIndex, sLabel,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - mStart).count());
    }

    ScopedLuaTimingLabel::ScopedLuaTimingLabel(std::string_view label)
        : mPrevious(std::exchange(sLabel, std::string(label)))
    {
    }

    ScopedLuaTimingLabel::~ScopedLuaTimingLabel()
    {
        sLabel = std::move(mPrevious);
    }
}
