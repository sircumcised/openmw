#ifndef OPENMW_MWNET_SESSION_H
#define OPENMW_MWNET_SESSION_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace MWNet
{
    /// Local handle of a connection. Unique for the lifetime of a Session object, never reused. 0 is never a peer.
    using PeerId = std::uint32_t;

    /// Passed to Session::send in place of a peer to address every connected peer.
    constexpr PeerId sAllPeers = 0;

    /// Upper bound of a single message. Must be the same on both sides, so it is not configurable.
    constexpr std::size_t sMaxMessageSize = 8 * 1024 * 1024;

    /// Number of independently ordered channels of every connection. Fixed for the same reason.
    constexpr std::size_t sChannelCount = 8;

    constexpr std::size_t sMaxPeers = 32;

    /// Largest value accepted as a user defined disconnect code.
    constexpr std::uint32_t sMaxUserCode = 0xffffff;

    enum class State
    {
        Offline,
        Hosting,
        Connecting,
        Connected,
    };

    enum class DisconnectReason
    {
        Timeout, // the remote side stopped answering
        Closed, // the remote side closed the connection
        Kicked, // the host removed us
        Protocol, // the remote side runs an incompatible version of the transport
        ConnectFailed, // the connection attempt did not succeed
        Local, // closed by this side
        Overflow, // the incoming queue was not polled for too long
    };

    std::string_view toString(State state);
    std::string_view toString(DisconnectReason reason);

    struct Config
    {
        std::string mAddress; // host name or IPv4 address to connect to (client only)
        std::uint16_t mPort = 0;
        std::size_t mMaxPeers = 3; // host only
        // A peer that stays silent for this long is dropped. Also the time given to a connection attempt.
        std::uint32_t mTimeoutMs = 10000;
    };

    struct Event
    {
        enum class Type
        {
            Connected,
            Disconnected,
            Message,
        };

        Type mType = Type::Message;
        PeerId mPeer = 0;
        std::uint8_t mChannel = 0; // Message
        DisconnectReason mReason = DisconnectReason::Timeout; // Disconnected
        std::uint32_t mCode = 0; // Disconnected: user defined code given to disconnect() or kick()
        std::string mData; // Message: payload; Connected: remote address as "ip:port"
    };

    struct PeerInfo
    {
        PeerId mId = 0;
        std::string mAddress; // "ip:port"
        std::uint32_t mPing = 0; // round trip time in milliseconds
        float mPacketLoss = 0; // 0..1
    };

    struct Stats
    {
        std::uint64_t mSentBytes = 0;
        std::uint64_t mReceivedBytes = 0;
        std::uint64_t mSentPackets = 0;
        std::uint64_t mReceivedPackets = 0;
        // Bytes of messages given to send() that are still waiting to be delivered.
        std::uint64_t mPendingBytes = 0;
        // Events waiting for poll().
        std::size_t mQueuedEvents = 0;
    };

    /// \brief Small reliable-UDP session: one host, several clients, star topology.
    ///
    /// All network I/O happens in a worker thread owned by the session, so the connection stays alive
    /// while the caller is busy (loading screens, saves, pauses). The caller talks to the worker only
    /// through queues: send()/kick() push commands, poll() takes the events received so far.
    ///
    /// The public methods must be called from one thread at a time.
    class Session
    {
    public:
        using Logger = std::function<void(std::string_view)>;

        explicit Session(Logger logger = {});
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        ~Session();

        /// Starts listening for clients. Throws std::runtime_error (bad arguments, port in use, already active).
        void host(const Config& config);

        /// Starts connecting to a host. The outcome arrives through poll() as Connected or Disconnected.
        /// Throws std::runtime_error (bad arguments, already active).
        void connect(const Config& config);

        /// Closes every connection and goes offline. Pending outgoing messages are flushed first.
        /// May block for up to half a second. Does nothing when already offline.
        void disconnect(std::uint32_t code = 0);

        /// Asks the worker to close one connection. Returns false when offline.
        bool kick(PeerId peer, std::uint32_t code = 0);

        /// Queues a message. \a peer is a peer id or sAllPeers. An unreliable message that does not fit
        /// in one datagram (about 1100 bytes) is split and arrives only if every part does.
        /// Returns false if the session is offline. Unknown peers are ignored (they may have just left).
        /// Throws std::runtime_error on bad arguments (channel out of range, empty or oversized data).
        bool send(PeerId peer, std::uint8_t channel, bool reliable, std::string data);

        /// Returns the events received since the previous call, in the order they happened.
        std::vector<Event> poll();

        /// A client is offline from the moment its Disconnected event can be polled.
        State getState() const { return mEnding.load() ? State::Offline : mState.load(); }

        /// UDP port the session is bound to (0 when offline). Useful after hosting on port 0.
        std::uint16_t getPort() const { return mPort.load(); }

        std::vector<PeerInfo> getPeers() const;
        Stats getStats() const;

    private:
        struct Command
        {
            enum class Type
            {
                Send,
                Kick,
            };

            Type mType = Type::Send;
            PeerId mPeer = 0;
            std::uint8_t mChannel = 0;
            bool mReliable = true;
            std::uint32_t mCode = 0;
            std::string mData;
        };

        class Worker;

        void start(void* enetHostPointer, const Config& config, bool isHost);
        void joinWorker();
        void pushEvent(Event event, bool last = false);
        void log(std::string_view message) const;

        Logger mLogger;

        std::thread mThread;
        std::atomic<State> mState{ State::Offline };
        // Set with the last event of a session that ends on its own, while the worker is still winding down.
        std::atomic<bool> mEnding{ false };
        std::atomic<bool> mStopRequested{ false };
        std::atomic<std::uint32_t> mStopCode{ 0 };
        std::atomic<std::uint16_t> mPort{ 0 };
        std::atomic<PeerId> mNextPeerId{ 1 };
        std::atomic<std::uint64_t> mPendingBytes{ 0 };

        mutable std::mutex mCommandsMutex;
        std::vector<Command> mCommands;

        mutable std::mutex mEventsMutex;
        std::vector<Event> mEvents;
        std::size_t mQueuedEventBytes = 0;
        // Set when the queue above grew too large. The worker then closes the session.
        std::atomic<bool> mOverflow{ false };

        mutable std::mutex mInfoMutex;
        std::vector<PeerInfo> mPeers;
        Stats mStats;
    };
}

#endif // OPENMW_MWNET_SESSION_H
