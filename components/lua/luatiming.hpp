#ifndef COMPONENTS_LUA_LUATIMING_H
#define COMPONENTS_LUA_LUATIMING_H

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace LuaUtil
{
    // NerevarMoon: lua timing log. Real time spent per script and handler, written to openmw.log by LuaManager.
    // No mutex: Lua runs on one thread at a time (the Lua worker and synchronizedUpdate never overlap).
    class LuaTiming
    {
    public:
        static inline bool sEnabled = false;

        static void add(int scriptIndex, std::string_view label, double seconds);
        static void addFrame();
        // First line "NMLUA|frames=<f>", then up to maxEntries entries sorted by total time; clears the registry.
        static std::vector<std::string> formatAndReset(
            const std::function<std::string(int)>& scriptName, std::size_t maxEntries = 40);
        static void reset();
    };

    // Measures from construction to destruction when enabled; only the outermost measure counts. Index < 0 is ignored.
    class ScopedLuaTiming
    {
    public:
        explicit ScopedLuaTiming(int scriptIndex);
        ~ScopedLuaTiming();
        ScopedLuaTiming(const ScopedLuaTiming&) = delete;
        ScopedLuaTiming& operator=(const ScopedLuaTiming&) = delete;

    private:
        int mScriptIndex = -1;
        bool mCounted = false;
        std::chrono::steady_clock::time_point mStart;
    };

    // Sets the label used by ScopedLuaTiming (default "callback") and restores the previous one.
    // Does nothing when the log is off at construction. The string_view overload expects a string that outlives
    // the scope (engine handler names are literals); the std::string overload keeps its own copy.
    class ScopedLuaTimingLabel
    {
    public:
        explicit ScopedLuaTimingLabel(std::string_view label);
        explicit ScopedLuaTimingLabel(std::string&& label);
        explicit ScopedLuaTimingLabel(const char* label)
            : ScopedLuaTimingLabel(std::string_view(label))
        {
        }
        ~ScopedLuaTimingLabel();
        ScopedLuaTimingLabel(const ScopedLuaTimingLabel&) = delete;
        ScopedLuaTimingLabel& operator=(const ScopedLuaTimingLabel&) = delete;

    private:
        bool mActive = false;
        std::string mOwned;
        std::string_view mPrevious;
    };
}

#endif
