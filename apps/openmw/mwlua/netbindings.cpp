#include "netbindings.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

#include <sol/sol.hpp>

#include <components/debug/debuglog.hpp>
#include <components/lua/luastate.hpp>
#include <components/lua/serialization.hpp>

#include "../mwnet/session.hpp"

#include "context.hpp"

namespace MWLua
{
    namespace
    {
        // Increased when the behaviour of the package changes in a way scripts have to know about.
        constexpr int sApiVersion = 1;

        constexpr double sDefaultTimeoutSeconds = 10;
        constexpr std::size_t sDefaultMaxPeers = 3;

        using Result = std::tuple<bool, sol::optional<std::string>>;

        std::optional<double> getNumber(const sol::table& options, std::string_view key)
        {
            const sol::object value = options[key];
            if (value == sol::nil)
                return std::nullopt;
            if (value.get_type() != sol::type::number)
                throw std::runtime_error("'" + std::string(key) + "' must be a number");
            return value.as<double>();
        }

        std::uint32_t getInteger(
            const sol::table& options, std::string_view key, std::optional<std::uint32_t> defaultValue, double max)
        {
            const std::optional<double> value = getNumber(options, key);
            if (!value)
            {
                if (!defaultValue)
                    throw std::runtime_error("'" + std::string(key) + "' is required");
                return *defaultValue;
            }
            if (!(*value >= 0 && *value <= max) || *value != std::floor(*value))
                throw std::runtime_error("'" + std::string(key) + "' must be an integer between 0 and "
                    + std::to_string(static_cast<std::uint64_t>(max)));
            return static_cast<std::uint32_t>(*value);
        }

        std::uint32_t getTimeoutMs(const sol::table& options)
        {
            const double seconds = getNumber(options, "timeout").value_or(sDefaultTimeoutSeconds);
            if (!(seconds >= 1 && seconds <= 120))
                throw std::runtime_error("'timeout' must be between 1 and 120 seconds");
            return static_cast<std::uint32_t>(seconds * 1000);
        }

        std::uint32_t toInteger(double value, std::string_view what, double max)
        {
            if (!(value >= 0 && value <= max) || value != std::floor(value))
                throw std::runtime_error(std::string(what) + " must be an integer between 0 and "
                    + std::to_string(static_cast<std::uint64_t>(max)));
            return static_cast<std::uint32_t>(value);
        }

        sol::table makeEvent(lua_State* lua, const MWNet::Event& event)
        {
            sol::table result(lua, sol::create);
            result["peer"] = event.mPeer;
            switch (event.mType)
            {
                case MWNet::Event::Type::Connected:
                    result["type"] = "connected";
                    result["address"] = event.mData;
                    break;
                case MWNet::Event::Type::Disconnected:
                    result["type"] = "disconnected";
                    result["reason"] = MWNet::toString(event.mReason);
                    result["code"] = event.mCode;
                    break;
                case MWNet::Event::Type::Message:
                    result["type"] = "message";
                    result["channel"] = event.mChannel;
                    result["data"] = LuaUtil::deserialize(lua, event.mData);
                    break;
            }
            return result;
        }

        sol::table createPackage(lua_State* lua)
        {
            auto session = std::make_shared<MWNet::Session>(
                [](std::string_view message) { Log(Debug::Info) << "[net] " << message; });

            sol::table api(lua, sol::create);
            api["API_VERSION"] = sApiVersion;
            api["ALL"] = MWNet::sAllPeers;
            api["CHANNELS"] = MWNet::sChannelCount;
            api["MAX_PEERS"] = MWNet::sMaxPeers;
            api["MAX_MESSAGE_SIZE"] = MWNet::sMaxMessageSize;

            api["host"] = [session](const sol::table& options) -> Result {
                MWNet::Config config;
                config.mPort = static_cast<std::uint16_t>(getInteger(options, "port", std::nullopt, 65535));
                config.mMaxPeers = getInteger(options, "maxPeers", static_cast<std::uint32_t>(sDefaultMaxPeers),
                    static_cast<double>(MWNet::sMaxPeers));
                if (config.mMaxPeers == 0)
                    throw std::runtime_error("'maxPeers' must be at least 1");
                config.mTimeoutMs = getTimeoutMs(options);
                try
                {
                    session->host(config);
                }
                catch (const std::exception& e)
                {
                    return { false, std::string(e.what()) };
                }
                return { true, sol::nullopt };
            };

            api["connect"] = [session](const sol::table& options) -> Result {
                MWNet::Config config;
                const sol::object address = options["address"];
                if (address.get_type() != sol::type::string || address.as<std::string_view>().empty())
                    throw std::runtime_error("'address' must be a host name or an IP address");
                config.mAddress = address.as<std::string>();
                config.mPort = static_cast<std::uint16_t>(getInteger(options, "port", std::nullopt, 65535));
                if (config.mPort == 0)
                    throw std::runtime_error("'port' must be between 1 and 65535");
                config.mTimeoutMs = getTimeoutMs(options);
                try
                {
                    session->connect(config);
                }
                catch (const std::exception& e)
                {
                    return { false, std::string(e.what()) };
                }
                return { true, sol::nullopt };
            };

            api["disconnect"] = [session](sol::optional<double> code) {
                session->disconnect(toInteger(code.value_or(0), "code", MWNet::sMaxUserCode));
            };

            api["kick"] = [session](double peer, sol::optional<double> code) -> bool {
                const MWNet::PeerId id = toInteger(peer, "peer", 4294967295.0);
                if (id == MWNet::sAllPeers)
                    throw std::runtime_error("peer must be a peer id");
                return session->kick(id, toInteger(code.value_or(0), "code", MWNet::sMaxUserCode));
            };

            api["send"] = [session](double peer, const sol::object& data, sol::optional<sol::table> options) -> bool {
                const MWNet::PeerId id = toInteger(peer, "peer", 4294967295.0);
                if (data == sol::nil)
                    throw std::runtime_error("data must not be nil");
                std::uint8_t channel = 0;
                bool reliable = true;
                if (options)
                {
                    channel = static_cast<std::uint8_t>(
                        getInteger(*options, "channel", 0, static_cast<double>(MWNet::sChannelCount - 1)));
                    const sol::object value = (*options)["reliable"];
                    if (value != sol::nil)
                    {
                        if (value.get_type() != sol::type::boolean)
                            throw std::runtime_error("'reliable' must be a boolean");
                        reliable = value.as<bool>();
                    }
                }
                // Game objects are deliberately not supported: their ids only mean something on this machine.
                return session->send(id, channel, reliable, LuaUtil::serialize(data));
            };

            api["poll"] = [session](sol::this_state state) -> sol::table {
                sol::table result(state, sol::create);
                int index = 0;
                for (const MWNet::Event& event : session->poll())
                {
                    // A message that can not be read must not cost us the other ones.
                    const int top = lua_gettop(state);
                    try
                    {
                        const sol::table luaEvent = makeEvent(state, event);
                        result[++index] = luaEvent;
                    }
                    catch (const std::exception& e)
                    {
                        lua_settop(state, top);
                        Log(Debug::Warning)
                            << "[net] dropped an unreadable message from peer " << event.mPeer << ": " << e.what();
                    }
                    catch (...)
                    {
                        // An error raised by Lua itself while building the value (out of memory, for example).
                        lua_settop(state, top);
                        Log(Debug::Warning) << "[net] dropped an unreadable message from peer " << event.mPeer;
                    }
                }
                return result;
            };

            api["getState"] = [session]() -> std::string_view { return MWNet::toString(session->getState()); };

            api["getPort"] = [session]() -> int { return session->getPort(); };

            api["getPeers"] = [session](sol::this_state state) -> sol::table {
                sol::table result(state, sol::create);
                int index = 0;
                for (const MWNet::PeerInfo& info : session->getPeers())
                {
                    sol::table peer(state, sol::create);
                    peer["id"] = info.mId;
                    peer["address"] = info.mAddress;
                    peer["ping"] = info.mPing;
                    peer["packetLoss"] = info.mPacketLoss;
                    result[++index] = peer;
                }
                return result;
            };

            api["getStats"] = [session](sol::this_state state) -> sol::table {
                const MWNet::Stats stats = session->getStats();
                sol::table result(state, sol::create);
                result["sentBytes"] = static_cast<double>(stats.mSentBytes);
                result["receivedBytes"] = static_cast<double>(stats.mReceivedBytes);
                result["sentPackets"] = static_cast<double>(stats.mSentPackets);
                result["receivedPackets"] = static_cast<double>(stats.mReceivedPackets);
                result["pendingBytes"] = static_cast<double>(stats.mPendingBytes);
                result["queuedEvents"] = static_cast<double>(stats.mQueuedEvents);
                return result;
            };

            Log(Debug::Info) << "Lua package openmw.net is available (API version " << sApiVersion << ")";
            return LuaUtil::makeReadOnly(api);
        }
    }

    sol::table initNetPackage(const Context& context)
    {
        const sol::object package
            = context.cachePackage("openmw_net", [&context]() { return createPackage(context.sol().lua_state()); });
        return package.as<sol::table>();
    }
}
