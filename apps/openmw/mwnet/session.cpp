#include "session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <enet/enet.h>

namespace MWNet
{
    namespace
    {
        // Sent with every connection request. A host only accepts clients that send the same value.
        constexpr enet_uint32 sProtocolId = 0x4e4d4301; // "NMC" + version of this transport

        // Events are dropped and the session is closed if the caller lets more than this pile up.
        constexpr std::size_t sMaxQueuedEventBytes = 64 * 1024 * 1024;
        constexpr std::size_t sEventOverheadBytes = 64;

        // How long disconnect() waits for the remote side to acknowledge.
        constexpr auto sShutdownTimeout = std::chrono::milliseconds(500);

        constexpr auto sInfoUpdateInterval = std::chrono::milliseconds(100);
        constexpr auto sLivenessCheckInterval = std::chrono::milliseconds(50);

        constexpr std::uint32_t sMinTimeoutMs = 1000;
        constexpr std::uint32_t sMaxTimeoutMs = 120000;

        // Largest UDP payload. The default of ENet (1392) is too large for some VPN and overlay networks,
        // which then have to fragment every full datagram.
        constexpr enet_uint32 sMtu = 1200;

        // Handle at most this many events before looking at the command queue again.
        constexpr int sMaxEventsPerService = 256;

        // Low byte of the value ENet delivers with a disconnect. The rest is the user defined code.
        // ENet itself reports 0 when a peer times out.
        enum WireReason : enet_uint32
        {
            Wire_None = 0,
            Wire_Closed = 1,
            Wire_Kicked = 2,
            Wire_Protocol = 3,
        };

        enet_uint32 makeWireCode(WireReason reason, std::uint32_t userCode)
        {
            return static_cast<enet_uint32>(reason) | (static_cast<enet_uint32>(userCode) << 8);
        }

        void initializeEnet()
        {
            static std::once_flag flag;
            std::call_once(flag, [] {
                if (enet_initialize() != 0)
                    throw std::runtime_error("failed to initialize the network library");
                std::atexit(enet_deinitialize);
            });
        }

        std::string formatAddress(const ENetAddress& address)
        {
            char ip[64] = {};
            if (enet_address_get_host_ip(&address, ip, sizeof(ip) - 1) != 0)
                return "unknown";
            return std::string(ip) + ":" + std::to_string(address.port);
        }

        PeerId getId(const ENetPeer* peer)
        {
            return static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(peer->data));
        }

        void setId(ENetPeer* peer, PeerId id)
        {
            peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        }

        void checkConfig(const Config& config)
        {
            if (config.mTimeoutMs < sMinTimeoutMs || config.mTimeoutMs > sMaxTimeoutMs)
                throw std::runtime_error("timeout must be between " + std::to_string(sMinTimeoutMs / 1000) + " and "
                    + std::to_string(sMaxTimeoutMs / 1000) + " seconds");
        }

        // Called by ENet when a packet is delivered or given up. Keeps Session::mPendingBytes up to date.
        void ENET_CALLBACK onPacketFreed(ENetPacket* packet)
        {
            static_cast<std::atomic<std::uint64_t>*>(packet->userData)->fetch_sub(packet->dataLength);
        }

        ENetHost* createHost(const ENetAddress* address, std::size_t maxPeers)
        {
            initializeEnet();
            ENetHost* host = enet_host_create(address, maxPeers, sChannelCount, 0, 0);
            if (host == nullptr)
                return nullptr;
            // Both settings must be the same on every side, so they are always on.
            if (enet_host_compress_with_range_coder(host) != 0)
            {
                enet_host_destroy(host);
                throw std::runtime_error("failed to set up network compression");
            }
            host->checksum = enet_crc32;
            host->maximumPacketSize = sMaxMessageSize;
            host->mtu = sMtu;
            return host;
        }
    }

    std::string_view toString(State state)
    {
        switch (state)
        {
            case State::Offline:
                return "offline";
            case State::Hosting:
                return "hosting";
            case State::Connecting:
                return "connecting";
            case State::Connected:
                return "connected";
        }
        return "offline";
    }

    std::string_view toString(DisconnectReason reason)
    {
        switch (reason)
        {
            case DisconnectReason::Timeout:
                return "timeout";
            case DisconnectReason::Closed:
                return "closed";
            case DisconnectReason::Kicked:
                return "kicked";
            case DisconnectReason::Protocol:
                return "protocol";
            case DisconnectReason::ConnectFailed:
                return "connect_failed";
            case DisconnectReason::Local:
                return "local";
            case DisconnectReason::Overflow:
                return "overflow";
        }
        return "timeout";
    }

    /// Everything that touches ENet after the host was created lives here and runs in the worker thread.
    class Session::Worker
    {
    public:
        Worker(Session& session, ENetHost* host, const Config& config, bool isHost)
            : mSession(session)
            , mHost(host)
            , mConfig(config)
            , mIsHost(isHost)
        {
        }

        Worker(const Worker&) = delete;
        Worker& operator=(const Worker&) = delete;

        ~Worker() { enet_host_destroy(mHost); }

        void run()
        {
            DisconnectReason reason = DisconnectReason::Local;
            std::uint32_t code = 0;
            if (mIsHost || beginConnect())
            {
                while (true)
                {
                    if (mSession.mStopRequested.load())
                    {
                        code = mSession.mStopCode.load();
                        break;
                    }
                    if (mSession.mOverflow.load())
                    {
                        reason = DisconnectReason::Overflow;
                        mSession.log("incoming messages were not read for too long, closing the session");
                        break;
                    }
                    processCommands();
                    service(1);
                    if (!mIsHost)
                        checkConnectDeadline();
                    checkLiveness();
                    if (!mIsHost && mPeers.empty())
                        break; // the only connection of a client is gone
                    updateInfo(false);
                }
            }
            shutdown(reason, code);
        }

    private:
        using Clock = std::chrono::steady_clock;

        struct Entry
        {
            ENetPeer* mPeer = nullptr;
            // Set when this side asked for the disconnect: the reason is then ours, not what ENet reports.
            bool mClosing = false;
            DisconnectReason mCloseReason = DisconnectReason::Local;
            std::uint32_t mCloseCode = 0;
            // When the peer last proved to be alive, and the ENet acknowledge time that proof was read from.
            Clock::time_point mLastAlive = Clock::now();
            enet_uint32 mLastAcknowledge = 0;
        };

        // Result of a name lookup that runs in a helper thread.
        struct Resolution
        {
            std::atomic<int> mState{ 0 }; // 0: running, 1: found, 2: failed
            ENetAddress mAddress{};
        };

        // Turns the configured address into an ENet address. Looking up a name can block for a long time,
        // so that part runs in a helper thread: the worker stays able to stop or to give up at the deadline.
        // A lookup that is abandoned finishes on its own and touches nothing but its own result.
        bool resolve(ENetAddress& address)
        {
            if (enet_address_set_host_ip(&address, mConfig.mAddress.c_str()) == 0)
                return true; // already a numeric address
            auto resolution = std::make_shared<Resolution>();
            std::thread([resolution, name = mConfig.mAddress] {
                ENetAddress resolved{};
                const bool found = enet_address_set_host(&resolved, name.c_str()) == 0;
                resolution->mAddress = resolved;
                resolution->mState.store(found ? 1 : 2);
            }).detach();
            while (resolution->mState.load() == 0)
            {
                if (mSession.mStopRequested.load() || Clock::now() >= mConnectDeadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (resolution->mState.load() != 1)
                return false;
            address = resolution->mAddress;
            return true;
        }

        bool beginConnect()
        {
            const PeerId id = mSession.mNextPeerId.fetch_add(1);
            mConnectDeadline = Clock::now() + std::chrono::milliseconds(mConfig.mTimeoutMs);
            ENetAddress address{};
            if (!resolve(address))
            {
                mSession.log("can not resolve address '" + mConfig.mAddress + "'");
                pushDisconnected(id, DisconnectReason::ConnectFailed, 0);
                return false;
            }
            address.port = mConfig.mPort;
            ENetPeer* peer = enet_host_connect(mHost, &address, sChannelCount, sProtocolId);
            if (peer == nullptr)
            {
                mSession.log("can not start a connection to " + formatAddress(address));
                pushDisconnected(id, DisconnectReason::ConnectFailed, 0);
                return false;
            }
            setId(peer, id);
            // The attempt ends at our own deadline; keep ENet from giving up earlier on long timeouts.
            enet_peer_timeout(peer, ENET_PEER_TIMEOUT_LIMIT, mConfig.mTimeoutMs, mConfig.mTimeoutMs * 2);
            mPeers.emplace(id, Entry{ peer });
            mSession.log("connecting to " + formatAddress(address));
            return true;
        }

        void checkConnectDeadline()
        {
            if (mSession.mState.load() != State::Connecting || mPeers.empty() || Clock::now() < mConnectDeadline)
                return;
            auto it = mPeers.begin();
            mSession.log("connection attempt timed out");
            const PeerId id = it->first;
            setId(it->second.mPeer, 0);
            enet_peer_reset(it->second.mPeer);
            mPeers.erase(it);
            pushDisconnected(id, DisconnectReason::ConnectFailed, 0);
        }

        // ENet notices a dead peer only when one of its retransmissions gives up, which happens at
        // exponentially growing intervals. A healthy peer acknowledges something at least every ping
        // interval, so drop a peer as soon as it was silent for the configured time.
        void checkLiveness()
        {
            const Clock::time_point now = Clock::now();
            if (now < mNextLivenessCheck)
                return;
            mNextLivenessCheck = now + sLivenessCheckInterval;
            if (!mIsHost && mSession.mState.load() == State::Connecting)
                return;
            const auto timeout = std::chrono::milliseconds(mConfig.mTimeoutMs);
            for (auto it = mPeers.begin(); it != mPeers.end();)
            {
                Entry& entry = it->second;
                if (entry.mPeer->lastReceiveTime != entry.mLastAcknowledge)
                {
                    entry.mLastAcknowledge = entry.mPeer->lastReceiveTime;
                    entry.mLastAlive = now;
                }
                if (now - entry.mLastAlive < timeout)
                {
                    ++it;
                    continue;
                }
                const PeerId id = it->first;
                const DisconnectReason reason = entry.mClosing ? entry.mCloseReason : DisconnectReason::Timeout;
                const std::uint32_t code = entry.mClosing ? entry.mCloseCode : 0;
                setId(entry.mPeer, 0);
                enet_peer_reset(entry.mPeer);
                it = mPeers.erase(it);
                pushDisconnected(id, reason, code);
                mInfoDirty = true;
            }
        }

        void processCommands()
        {
            std::vector<Command> commands;
            {
                std::lock_guard lock(mSession.mCommandsMutex);
                commands.swap(mSession.mCommands);
            }
            for (Command& command : commands)
            {
                switch (command.mType)
                {
                    case Command::Type::Send:
                        send(command);
                        break;
                    case Command::Type::Kick:
                        if (auto it = mPeers.find(command.mPeer); it != mPeers.end())
                            close(it->first, it->second, Wire_Kicked, DisconnectReason::Local, command.mCode);
                        break;
                }
            }
        }

        void send(const Command& command)
        {
            // Without the fragment flag ENet would silently send a large unreliable message reliably.
            ENetPacket* packet = enet_packet_create(command.mData.data(), command.mData.size(),
                command.mReliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
            if (packet == nullptr)
            {
                mSession.mPendingBytes.fetch_sub(command.mData.size());
                mSession.log("out of memory, a message was dropped");
                return;
            }
            packet->userData = &mSession.mPendingBytes;
            packet->freeCallback = &onPacketFreed;
            if (command.mPeer == sAllPeers)
            {
                // Sends to every connected peer and frees the packet if there is none.
                enet_host_broadcast(mHost, command.mChannel, packet);
                return;
            }
            auto it = mPeers.find(command.mPeer);
            if (it == mPeers.end() || enet_peer_send(it->second.mPeer, command.mChannel, packet) != 0)
                enet_packet_destroy(packet);
        }

        // Starts closing a connection once its queued messages have been sent.
        // The peer is removed here if ENet drops it right away, otherwise when the disconnect event arrives.
        void close(PeerId id, Entry& entry, WireReason wireReason, DisconnectReason reason, std::uint32_t code)
        {
            if (!entry.mClosing)
            {
                entry.mClosing = true;
                entry.mCloseReason = reason;
                entry.mCloseCode = code;
                enet_peer_disconnect_later(entry.mPeer, makeWireCode(wireReason, code));
            }
            if (entry.mPeer->state == ENET_PEER_STATE_DISCONNECTED)
            {
                setId(entry.mPeer, 0);
                mPeers.erase(id);
                pushDisconnected(id, reason, code);
                mInfoDirty = true;
            }
        }

        void service(enet_uint32 timeoutMs)
        {
            ENetEvent event;
            int result = enet_host_service(mHost, &event, timeoutMs);
            for (int handled = 0; result > 0;)
            {
                handleEvent(event);
                if (++handled >= sMaxEventsPerService)
                    break;
                result = enet_host_service(mHost, &event, 0);
            }
            if (result < 0)
            {
                // A socket error (network down, for example). Do not spin, the next call may succeed.
                if (!mServiceErrorLogged)
                {
                    mServiceErrorLogged = true;
                    mSession.log("network socket error");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        void handleEvent(const ENetEvent& event)
        {
            switch (event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    onConnect(event);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    onDisconnect(event);
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    onReceive(event);
                    break;
                default:
                    break;
            }
        }

        void onConnect(const ENetEvent& event)
        {
            ENetPeer* peer = event.peer;
            PeerId id = 0;
            if (mIsHost)
            {
                if (mShuttingDown)
                {
                    // Too late: say so right away instead of letting the newcomer wait for a timeout.
                    setId(peer, 0);
                    enet_peer_disconnect_now(peer, makeWireCode(Wire_Closed, 0));
                    return;
                }
                if (event.data != sProtocolId)
                {
                    mSession.log("rejected " + formatAddress(peer->address) + ": incompatible network version");
                    setId(peer, 0);
                    enet_peer_disconnect(peer, makeWireCode(Wire_Protocol, 0));
                    return;
                }
                id = mSession.mNextPeerId.fetch_add(1);
                setId(peer, id);
                mPeers.emplace(id, Entry{ peer });
            }
            else
            {
                id = getId(peer);
                auto it = mPeers.find(id);
                if (it == mPeers.end())
                    return;
                // The entry exists since the attempt started; the peer only counts as alive from now.
                it->second.mLastAlive = Clock::now();
                mSession.mState.store(State::Connected);
            }
            enet_peer_timeout(peer, ENET_PEER_TIMEOUT_LIMIT, mConfig.mTimeoutMs, mConfig.mTimeoutMs * 2);
            // By default ENet throws away unreliable packets on purpose whenever the round trip time
            // rises, as a form of congestion control. A handful of players send far too little for that
            // to be needed, and it makes unreliable updates disappear in bursts on jittery connections.
            // No deceleration keeps the throttle fully open.
            enet_peer_throttle_configure(
                peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_ACCELERATION, 0);

            Event result;
            result.mType = Event::Type::Connected;
            result.mPeer = id;
            result.mData = formatAddress(peer->address);
            mSession.log("peer " + std::to_string(id) + " connected (" + result.mData + ")");
            mSession.pushEvent(std::move(result));
            mInfoDirty = true;
        }

        void onDisconnect(const ENetEvent& event)
        {
            const PeerId id = getId(event.peer);
            setId(event.peer, 0);
            auto it = mPeers.find(id);
            if (it == mPeers.end())
                return; // a connection that was rejected or already reported

            DisconnectReason reason = DisconnectReason::Timeout;
            std::uint32_t code = 0;
            if (it->second.mClosing)
            {
                reason = it->second.mCloseReason;
                code = it->second.mCloseCode;
            }
            else
            {
                switch (event.data & 0xff)
                {
                    case Wire_Closed:
                        reason = DisconnectReason::Closed;
                        code = event.data >> 8;
                        break;
                    case Wire_Kicked:
                        reason = DisconnectReason::Kicked;
                        code = event.data >> 8;
                        break;
                    case Wire_Protocol:
                        reason = DisconnectReason::Protocol;
                        break;
                    default:
                        if (!mIsHost && mSession.mState.load() == State::Connecting)
                            reason = DisconnectReason::ConnectFailed;
                        break;
                }
            }
            mPeers.erase(it);
            pushDisconnected(id, reason, code);
            mInfoDirty = true;
        }

        void onReceive(const ENetEvent& event)
        {
            if (const PeerId id = getId(event.peer); id != 0)
            {
                if (auto it = mPeers.find(id); it != mPeers.end())
                    it->second.mLastAlive = Clock::now();
                Event result;
                result.mType = Event::Type::Message;
                result.mPeer = id;
                result.mChannel = event.channelID;
                result.mData.assign(reinterpret_cast<const char*>(event.packet->data), event.packet->dataLength);
                mSession.pushEvent(std::move(result));
            }
            enet_packet_destroy(event.packet);
        }

        void pushDisconnected(PeerId id, DisconnectReason reason, std::uint32_t code)
        {
            Event event;
            event.mType = Event::Type::Disconnected;
            event.mPeer = id;
            event.mReason = reason;
            event.mCode = code;
            mSession.log("peer " + std::to_string(id) + " disconnected (" + std::string(toString(reason)) + ")");
            // A client has one connection: losing it ends the session.
            mSession.pushEvent(std::move(event), !mIsHost);
        }

        void updateInfo(bool force)
        {
            const Clock::time_point now = Clock::now();
            if (!force && !mInfoDirty && now < mNextInfoUpdate)
                return;
            mNextInfoUpdate = now + sInfoUpdateInterval;
            mInfoDirty = false;

            std::vector<PeerInfo> peers;
            peers.reserve(mPeers.size());
            for (const auto& [id, entry] : mPeers)
            {
                const ENetPeer* peer = entry.mPeer;
                if (peer->state != ENET_PEER_STATE_CONNECTED && peer->state != ENET_PEER_STATE_DISCONNECT_LATER)
                    continue;
                PeerInfo info;
                info.mId = id;
                info.mAddress = formatAddress(peer->address);
                info.mPing = peer->roundTripTime;
                info.mPacketLoss
                    = static_cast<float>(peer->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE);
                peers.push_back(std::move(info));
            }
            std::sort(peers.begin(), peers.end(), [](const PeerInfo& l, const PeerInfo& r) { return l.mId < r.mId; });

            std::lock_guard lock(mSession.mInfoMutex);
            mSession.mPeers = std::move(peers);
            mSession.mStats.mSentBytes += mHost->totalSentData;
            mSession.mStats.mReceivedBytes += mHost->totalReceivedData;
            mSession.mStats.mSentPackets += mHost->totalSentPackets;
            mSession.mStats.mReceivedPackets += mHost->totalReceivedPackets;
            mHost->totalSentData = 0;
            mHost->totalReceivedData = 0;
            mHost->totalSentPackets = 0;
            mHost->totalReceivedPackets = 0;
        }

        void shutdown(DisconnectReason reason, std::uint32_t code)
        {
            mShuttingDown = true;
            // Messages queued right before the shutdown still go out.
            processCommands();

            std::vector<PeerId> ids;
            ids.reserve(mPeers.size());
            for (const auto& [id, entry] : mPeers)
                ids.push_back(id);
            for (PeerId id : ids)
                if (auto it = mPeers.find(id); it != mPeers.end())
                    close(id, it->second, Wire_Closed, reason, code);

            const Clock::time_point deadline = Clock::now() + sShutdownTimeout;
            while (!mPeers.empty() && Clock::now() < deadline)
                service(5);

            for (auto& [id, entry] : mPeers)
            {
                setId(entry.mPeer, 0);
                enet_peer_reset(entry.mPeer);
                pushDisconnected(id, entry.mCloseReason, entry.mCloseCode);
            }
            mPeers.clear();
            enet_host_flush(mHost);

            updateInfo(true);
            mSession.mPort.store(0);
            mSession.mState.store(State::Offline);
        }

        Session& mSession;
        ENetHost* mHost;
        Config mConfig;
        bool mIsHost;
        std::unordered_map<PeerId, Entry> mPeers;
        Clock::time_point mConnectDeadline;
        Clock::time_point mNextInfoUpdate;
        Clock::time_point mNextLivenessCheck;
        bool mInfoDirty = true;
        bool mServiceErrorLogged = false;
        bool mShuttingDown = false;
    };

    Session::Session(Logger logger)
        : mLogger(std::move(logger))
    {
    }

    Session::~Session()
    {
        disconnect();
    }

    void Session::host(const Config& config)
    {
        checkConfig(config);
        if (config.mMaxPeers < 1 || config.mMaxPeers > sMaxPeers)
            throw std::runtime_error("maxPeers must be between 1 and " + std::to_string(sMaxPeers));
        if (getState() != State::Offline)
            throw std::runtime_error("the network session is already active");
        joinWorker();

        ENetAddress address{};
        address.host = ENET_HOST_ANY;
        address.port = config.mPort;
        ENetHost* enetHost = createHost(&address, config.mMaxPeers);
        if (enetHost == nullptr)
            throw std::runtime_error("can not open UDP port " + std::to_string(config.mPort) + " (already in use?)");
        const std::uint16_t port = enetHost->address.port;
        log("hosting on UDP port " + std::to_string(port) + " for up to " + std::to_string(config.mMaxPeers)
            + " peers");
        start(enetHost, config, true);
        mPort.store(port);
    }

    void Session::connect(const Config& config)
    {
        checkConfig(config);
        if (config.mAddress.empty())
            throw std::runtime_error("address is required");
        if (config.mPort == 0)
            throw std::runtime_error("port is required");
        if (getState() != State::Offline)
            throw std::runtime_error("the network session is already active");
        joinWorker();

        ENetHost* enetHost = createHost(nullptr, 1);
        if (enetHost == nullptr)
            throw std::runtime_error("can not open a network socket");
        start(enetHost, config, false);
    }

    void Session::start(void* enetHostPointer, const Config& config, bool isHost)
    {
        ENetHost* enetHost = static_cast<ENetHost*>(enetHostPointer);
        {
            // The previous worker is gone and took its packets with it: nothing is pending.
            std::lock_guard lock(mCommandsMutex);
            mCommands.clear();
            mPendingBytes.store(0);
        }
        mStopRequested.store(false);
        mStopCode.store(0);
        mOverflow.store(false);
        mState.store(isHost ? State::Hosting : State::Connecting);
        mEnding.store(false);
        try
        {
            mThread = std::thread([this, enetHost, config, isHost] {
                try
                {
                    Worker worker(*this, enetHost, config, isHost);
                    worker.run();
                }
                catch (const std::exception& e)
                {
                    // Out of memory, most likely. The session is lost, the game is not.
                    log(std::string("network thread stopped: ") + e.what());
                }
                mPort.store(0);
                mState.store(State::Offline);
            });
        }
        catch (...)
        {
            enet_host_destroy(enetHost);
            mState.store(State::Offline);
            throw;
        }
    }

    void Session::joinWorker()
    {
        if (mThread.joinable())
            mThread.join();
    }

    void Session::disconnect(std::uint32_t code)
    {
        if (!mThread.joinable())
            return;
        mStopCode.store(std::min(code, sMaxUserCode));
        mStopRequested.store(true);
        mThread.join();
    }

    bool Session::kick(PeerId peer, std::uint32_t code)
    {
        if (getState() == State::Offline)
            return false;
        Command command;
        command.mType = Command::Type::Kick;
        command.mPeer = peer;
        command.mCode = std::min(code, sMaxUserCode);
        std::lock_guard lock(mCommandsMutex);
        mCommands.push_back(std::move(command));
        return true;
    }

    bool Session::send(PeerId peer, std::uint8_t channel, bool reliable, std::string data)
    {
        if (channel >= sChannelCount)
            throw std::runtime_error("channel must be between 0 and " + std::to_string(sChannelCount - 1));
        // ENet's packet compressor reads out of bounds on a zero length payload, so there is no such thing.
        if (data.empty())
            throw std::runtime_error("message is empty");
        if (data.size() > sMaxMessageSize)
            throw std::runtime_error("message is too large: " + std::to_string(data.size()) + " bytes, the limit is "
                + std::to_string(sMaxMessageSize));
        const State state = getState();
        if (state != State::Hosting && state != State::Connected)
            return false;
        Command command;
        command.mType = Command::Type::Send;
        command.mPeer = peer;
        command.mChannel = channel;
        command.mReliable = reliable;
        command.mData = std::move(data);
        std::lock_guard lock(mCommandsMutex);
        mPendingBytes.fetch_add(command.mData.size());
        mCommands.push_back(std::move(command));
        return true;
    }

    std::vector<Event> Session::poll()
    {
        std::vector<Event> events;
        std::lock_guard lock(mEventsMutex);
        events.swap(mEvents);
        mQueuedEventBytes = 0;
        return events;
    }

    void Session::pushEvent(Event event, bool last)
    {
        std::lock_guard lock(mEventsMutex);
        if (event.mType == Event::Type::Message)
        {
            if (mOverflow.load())
                return;
            mQueuedEventBytes += event.mData.size() + sEventOverheadBytes;
            if (mQueuedEventBytes > sMaxQueuedEventBytes)
            {
                mOverflow.store(true);
                return;
            }
        }
        mEvents.push_back(std::move(event));
        // Under the same lock as the event: whoever reads it from poll() already sees the session as offline
        // and can start a new one, and whoever sees it offline finds the event in the next poll().
        if (last)
            mEnding.store(true);
    }

    std::vector<PeerInfo> Session::getPeers() const
    {
        std::lock_guard lock(mInfoMutex);
        return mPeers;
    }

    Stats Session::getStats() const
    {
        Stats stats;
        {
            std::lock_guard lock(mInfoMutex);
            stats = mStats;
        }
        stats.mPendingBytes = getState() == State::Offline ? 0 : mPendingBytes.load();
        std::lock_guard lock(mEventsMutex);
        stats.mQueuedEvents = mEvents.size();
        return stats;
    }

    void Session::log(std::string_view message) const
    {
        if (mLogger)
            mLogger(message);
    }
}
