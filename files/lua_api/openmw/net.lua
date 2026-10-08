---
-- A small message transport between game instances: one host, several clients.
-- Clients are connected to the host only. Messages are plain Lua values
-- (numbers, strings, booleans, tables, vectors, transforms, colors); game objects can not be sent.
--
-- There is one session per game instance. It is not tied to a save: it stays connected
-- while a game is loaded, saved or paused, and messages received in the meantime are kept
-- until somebody calls @{#net.poll}.
-- @context global|menu
-- @module net
-- @usage local net = require('openmw.net')

---
-- Version of this package.
-- @field [parent=#net] #number API_VERSION

---
-- Use in place of a peer id in @{#net.send} to address every connected peer.
-- @field [parent=#net] #number ALL

---
-- Number of channels of a connection. Messages keep their order within a channel.
-- @field [parent=#net] #number CHANNELS

---
-- Largest value accepted for `maxPeers`.
-- @field [parent=#net] #number MAX_PEERS

---
-- Largest size of one message in bytes, after serialization.
-- @field [parent=#net] #number MAX_MESSAGE_SIZE

---
-- Start hosting. Throws an error if an option is invalid.
-- @function [parent=#net] host
-- @param #table options Table with the keys:
--
--   * `port` - UDP port to listen on, 0 to let the system choose (see @{#net.getPort}); required;
--   * `maxPeers` - number of clients that can be connected at the same time (default: 3);
--   * `timeout` - seconds of silence after which a peer is dropped, 1 to 120 (default: 10).
-- @return #boolean true if the session was started.
-- @return #boolean, #string false plus the error message otherwise (port in use, session already active).
-- @usage local ok, err = net.host{ port = 25565, maxPeers = 3 }

---
-- Start connecting to a host. Returns at once: the outcome arrives through @{#net.poll}
-- as a `connected` or a `disconnected` event. Throws an error if an option is invalid.
-- @function [parent=#net] connect
-- @param #table options Table with the keys:
--
--   * `address` - host name or IPv4 address; required;
--   * `port` - UDP port of the host; required;
--   * `timeout` - seconds of silence after which the host is considered lost, and the time given to
--     the connection attempt, 1 to 120 (default: 10).
-- @return #boolean true if the attempt was started.
-- @return #boolean, #string false plus the error message otherwise (session already active).
-- @usage net.connect{ address = '192.168.1.20', port = 25565 }

---
-- Close every connection and go offline. Messages already queued with @{#net.send} are delivered first.
-- May block for up to half a second. Does nothing when offline.
-- @function [parent=#net] disconnect
-- @param #number code (optional) A number up to 16777215 the other side receives with its `disconnected` event.

---
-- Close the connection to one peer. Messages already queued for it are delivered first.
-- @function [parent=#net] kick
-- @param #number peer Peer id.
-- @param #number code (optional) A number up to 16777215 the peer receives with its `disconnected` event.
-- @return #boolean false when offline.

---
-- Send a value to a peer. Throws an error if the value can not be serialized or is too large.
-- @function [parent=#net] send
-- @param #number peer Peer id, or @{#net.ALL}.
-- @param data The value to send. Can not be nil.
-- @param #table options (optional) Table with the keys:
--
--   * `channel` - 0 to CHANNELS-1 (default: 0);
--   * `reliable` - true: the message arrives, in order, or the connection is closed (default).
--     false: the message may be lost and is dropped if a newer one of the same channel arrived first;
--     use for small data that is replaced by the next update anyway (above about 1100 bytes a message
--     is split and arrives only if every part does).
-- @return #boolean false if there is no connection (offline or still connecting).
-- @usage net.send(net.ALL, { kind = 'chat', text = 'hello' })
-- @usage net.send(peer, { kind = 'pos', pos = position }, { channel = 1, reliable = false })

---
-- Take the events that arrived since the last call, oldest first. Call it every frame from one script.
--
-- Every event is a table with `type` and `peer` (the peer id) and:
--
--   * `connected`: `address` ("ip:port"). On a client the peer is the host.
--   * `disconnected`: `reason` and `code` (the number given to `disconnect` or `kick`, otherwise 0). Reasons:
--     `timeout` (the peer went silent), `closed` (the peer left or the host shut down), `kicked`,
--     `protocol` (incompatible game build), `connect_failed`, `local` (closed by this side),
--     `overflow` (events were not polled for so long that the session was closed).
--   * `message`: `channel` and `data`.
--
-- A client goes offline after its `disconnected` event.
-- @function [parent=#net] poll
-- @return #list<#table>

---
-- Current state: "offline", "hosting", "connecting" or "connected".
-- @function [parent=#net] getState
-- @return #string

---
-- UDP port the session is hosting on, 0 otherwise.
-- @function [parent=#net] getPort
-- @return #number

---
-- Connected peers.
-- @function [parent=#net] getPeers
-- @return #list<#table> Tables with `id`, `address` ("ip:port"), `ping` (milliseconds) and `packetLoss` (0 to 1).

---
-- Traffic counters.
-- @function [parent=#net] getStats
-- @return #table Table with `sentBytes`, `receivedBytes`, `sentPackets`, `receivedPackets` (UDP level,
-- since the game was started), `pendingBytes` (messages given to `send` that are not delivered yet;
-- watch it when sending a lot of data) and `queuedEvents` (events waiting for `poll`).

return nil
