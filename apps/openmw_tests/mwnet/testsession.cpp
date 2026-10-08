#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "apps/openmw/mwnet/session.hpp"

namespace MWNet
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // A session and the events it produced so far.
        struct Endpoint
        {
            Session mSession;
            std::vector<Event> mEvents;

            void pump()
            {
                for (Event& event : mSession.poll())
                    mEvents.push_back(std::move(event));
            }

            std::vector<Event> take(Event::Type type)
            {
                std::vector<Event> result;
                std::vector<Event> rest;
                for (Event& event : mEvents)
                    (event.mType == type ? result : rest).push_back(std::move(event));
                mEvents = std::move(rest);
                return result;
            }

            std::size_t count(Event::Type type) const
            {
                std::size_t result = 0;
                for (const Event& event : mEvents)
                    result += event.mType == type ? 1 : 0;
                return result;
            }
        };

        bool waitFor(std::vector<Endpoint*> endpoints, const std::function<bool()>& done,
            std::chrono::milliseconds timeout = std::chrono::seconds(10))
        {
            const Clock::time_point deadline = Clock::now() + timeout;
            while (true)
            {
                for (Endpoint* endpoint : endpoints)
                    endpoint->pump();
                if (done())
                    return true;
                if (Clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        Config makeHostConfig(std::uint32_t timeoutMs = 10000)
        {
            Config config;
            config.mPort = 0; // any free port
            config.mMaxPeers = 3;
            config.mTimeoutMs = timeoutMs;
            return config;
        }

        Config makeClientConfig(std::uint16_t port, std::uint32_t timeoutMs = 10000)
        {
            Config config;
            config.mAddress = "127.0.0.1";
            config.mPort = port;
            config.mTimeoutMs = timeoutMs;
            return config;
        }

        // Connects a client; returns the id of the client on the host and of the host on the client.
        std::pair<PeerId, PeerId> link(Endpoint& host, Endpoint& client)
        {
            client.mSession.connect(makeClientConfig(host.mSession.getPort()));
            EXPECT_TRUE(waitFor({ &host, &client },
                [&] { return host.count(Event::Type::Connected) >= 1 && client.count(Event::Type::Connected) >= 1; }));
            const std::vector<Event> onHost = host.take(Event::Type::Connected);
            const std::vector<Event> onClient = client.take(Event::Type::Connected);
            if (onHost.empty() || onClient.empty())
                return { 0, 0 };
            return { onHost.back().mPeer, onClient.back().mPeer };
        }

        TEST(MWNetSessionTest, shouldBeOfflineUntilStarted)
        {
            Session session;
            EXPECT_EQ(session.getState(), State::Offline);
            EXPECT_EQ(session.getPort(), 0);
            EXPECT_FALSE(session.send(sAllPeers, 0, true, "data"));
            EXPECT_FALSE(session.kick(1));
            EXPECT_TRUE(session.poll().empty());
            EXPECT_TRUE(session.getPeers().empty());
            session.disconnect();
        }

        TEST(MWNetSessionTest, shouldRejectInvalidArguments)
        {
            Endpoint host;
            Config noPeers = makeHostConfig();
            noPeers.mMaxPeers = 0;
            EXPECT_THROW(host.mSession.host(noPeers), std::runtime_error);
            EXPECT_THROW(host.mSession.host(makeHostConfig(1)), std::runtime_error);
            Config noAddress = makeClientConfig(1234);
            noAddress.mAddress.clear();
            EXPECT_THROW(host.mSession.connect(noAddress), std::runtime_error);
            EXPECT_THROW(host.mSession.connect(makeClientConfig(0)), std::runtime_error);
            EXPECT_EQ(host.mSession.getState(), State::Offline);

            host.mSession.host(makeHostConfig());
            EXPECT_THROW(host.mSession.host(makeHostConfig()), std::runtime_error);
            EXPECT_THROW(host.mSession.send(sAllPeers, static_cast<std::uint8_t>(sChannelCount), true, "data"),
                std::runtime_error);
            EXPECT_THROW(host.mSession.send(sAllPeers, 0, true, std::string()), std::runtime_error);
            EXPECT_THROW(
                host.mSession.send(sAllPeers, 0, true, std::string(sMaxMessageSize + 1, 'x')), std::runtime_error);

            Endpoint other;
            Config samePort = makeHostConfig();
            samePort.mPort = host.mSession.getPort();
            EXPECT_THROW(other.mSession.host(samePort), std::runtime_error);
        }

        TEST(MWNetSessionTest, shouldExchangeMessagesInBothDirections)
        {
            Endpoint host;
            Endpoint client;
            host.mSession.host(makeHostConfig());
            EXPECT_EQ(host.mSession.getState(), State::Hosting);
            ASSERT_NE(host.mSession.getPort(), 0);
            const auto [clientOnHost, hostOnClient] = link(host, client);
            ASSERT_NE(clientOnHost, 0u);
            ASSERT_NE(hostOnClient, 0u);
            EXPECT_EQ(client.mSession.getState(), State::Connected);

            const std::string binary("a\0b\0\xff", 5);
            const std::string large(1024 * 1024, 'L');
            EXPECT_TRUE(client.mSession.send(hostOnClient, 0, true, "to host"));
            EXPECT_TRUE(client.mSession.send(sAllPeers, 1, true, binary));
            EXPECT_TRUE(host.mSession.send(clientOnHost, 2, true, large));
            for (int i = 0; i < 100; ++i)
                EXPECT_TRUE(host.mSession.send(clientOnHost, 3, true, std::to_string(i)));
            ASSERT_TRUE(waitFor(
                { &host, &client },
                [&] { return host.count(Event::Type::Message) == 2 && client.count(Event::Type::Message) == 101; },
                std::chrono::seconds(30)));

            const std::vector<Event> onHost = host.take(Event::Type::Message);
            EXPECT_EQ(onHost[0].mPeer, clientOnHost);
            EXPECT_EQ(onHost[0].mChannel, 0);
            EXPECT_EQ(onHost[0].mData, "to host");
            EXPECT_EQ(onHost[1].mChannel, 1);
            EXPECT_EQ(onHost[1].mData, binary);

            const std::vector<Event> onClient = client.take(Event::Type::Message);
            int next = 0;
            for (const Event& event : onClient)
            {
                EXPECT_EQ(event.mPeer, hostOnClient);
                if (event.mChannel == 2)
                    EXPECT_EQ(event.mData, large);
                else
                    EXPECT_EQ(event.mData, std::to_string(next++)); // order is kept within a channel
            }
            EXPECT_EQ(next, 100);
        }

        TEST(MWNetSessionTest, shouldReportWhyAConnectionEnded)
        {
            Endpoint host;
            Endpoint leaving;
            Endpoint kicked;
            host.mSession.host(makeHostConfig());
            const auto [leavingOnHost, hostOnLeaving] = link(host, leaving);
            const auto [kickedOnHost, hostOnKicked] = link(host, kicked);
            ASSERT_NE(leavingOnHost, kickedOnHost);

            leaving.mSession.disconnect(42);
            EXPECT_EQ(leaving.mSession.getState(), State::Offline);
            ASSERT_TRUE(waitFor({ &host, &leaving }, [&] { return host.count(Event::Type::Disconnected) == 1; }));
            std::vector<Event> onHost = host.take(Event::Type::Disconnected);
            EXPECT_EQ(onHost[0].mPeer, leavingOnHost);
            EXPECT_EQ(onHost[0].mReason, DisconnectReason::Closed);
            EXPECT_EQ(onHost[0].mCode, 42u);
            const std::vector<Event> onLeaving = leaving.take(Event::Type::Disconnected);
            ASSERT_EQ(onLeaving.size(), 1u);
            EXPECT_EQ(onLeaving[0].mPeer, hostOnLeaving);
            EXPECT_EQ(onLeaving[0].mReason, DisconnectReason::Local);

            // A message queued before the kick still arrives, before the disconnect.
            EXPECT_TRUE(host.mSession.send(kickedOnHost, 0, true, "last words"));
            EXPECT_TRUE(host.mSession.kick(kickedOnHost, 7));
            ASSERT_TRUE(waitFor({ &host, &kicked }, [&] {
                return host.count(Event::Type::Disconnected) == 1 && kicked.count(Event::Type::Disconnected) == 1;
            }));
            ASSERT_EQ(kicked.mEvents.size(), 2u);
            EXPECT_EQ(kicked.mEvents[0].mData, "last words");
            EXPECT_EQ(kicked.mEvents[1].mPeer, hostOnKicked);
            EXPECT_EQ(kicked.mEvents[1].mReason, DisconnectReason::Kicked);
            EXPECT_EQ(kicked.mEvents[1].mCode, 7u);
            onHost = host.take(Event::Type::Disconnected);
            EXPECT_EQ(onHost[0].mPeer, kickedOnHost);
            EXPECT_EQ(onHost[0].mReason, DisconnectReason::Local);
            EXPECT_TRUE(waitFor({ &kicked }, [&] { return kicked.mSession.getState() == State::Offline; }));

            // The host going away is told to whoever is still there.
            kicked.mEvents.clear();
            const auto [againOnHost, hostOnAgain] = link(host, kicked);
            EXPECT_GT(againOnHost, kickedOnHost); // ids are not reused
            host.mSession.disconnect();
            EXPECT_EQ(host.mSession.getState(), State::Offline);
            ASSERT_TRUE(waitFor({ &kicked }, [&] { return kicked.count(Event::Type::Disconnected) == 1; }));
            EXPECT_EQ(kicked.mEvents[0].mPeer, hostOnAgain);
            EXPECT_EQ(kicked.mEvents[0].mReason, DisconnectReason::Closed);
        }

        TEST(MWNetSessionTest, shouldBeOfflineAsSoonAsTheDisconnectIsVisible)
        {
            // A client that reads its Disconnected event can start a new connection right away.
            Endpoint host;
            Endpoint client;
            host.mSession.host(makeHostConfig());
            PeerId clientOnHost = link(host, client).first;
            for (int i = 0; i < 20; ++i)
            {
                ASSERT_TRUE(host.mSession.kick(clientOnHost));
                ASSERT_TRUE(waitFor({ &client }, [&] { return client.count(Event::Type::Disconnected) == 1; }));
                EXPECT_EQ(client.mSession.getState(), State::Offline);
                client.mEvents.clear();
                ASSERT_TRUE(waitFor({ &host }, [&] { return host.count(Event::Type::Disconnected) == 1; }));
                host.mEvents.clear();
                ASSERT_NO_THROW(clientOnHost = link(host, client).first);
                ASSERT_NE(clientOnHost, 0u);
            }
        }

        TEST(MWNetSessionTest, shouldFailToConnectWhenNobodyListens)
        {
            Endpoint client;
            client.mSession.connect(makeClientConfig(1, 1000));
            ASSERT_TRUE(waitFor({ &client }, [&] { return client.count(Event::Type::Disconnected) == 1; }));
            EXPECT_EQ(client.mEvents[0].mReason, DisconnectReason::ConnectFailed);
            EXPECT_EQ(client.count(Event::Type::Connected), 0u);
            EXPECT_TRUE(waitFor({ &client }, [&] { return client.mSession.getState() == State::Offline; }));
        }

        TEST(MWNetSessionTest, shouldStayConnectedWhileTheCallerIsBusy)
        {
            // The owner of the client does not poll for three seconds (a loading screen) with a two second
            // timeout: the worker thread keeps the connection alive and no message is lost.
            Endpoint host;
            Endpoint client;
            host.mSession.host(makeHostConfig(2000));
            client.mSession.connect(makeClientConfig(host.mSession.getPort(), 2000));
            ASSERT_TRUE(waitFor({ &host, &client },
                [&] { return host.count(Event::Type::Connected) == 1 && client.count(Event::Type::Connected) == 1; }));
            const PeerId clientOnHost = host.take(Event::Type::Connected)[0].mPeer;
            client.take(Event::Type::Connected);

            int sent = 0;
            const Clock::time_point end = Clock::now() + std::chrono::seconds(3);
            while (Clock::now() < end)
            {
                EXPECT_TRUE(host.mSession.send(clientOnHost, 0, true, std::to_string(sent++)));
                host.pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            EXPECT_EQ(host.count(Event::Type::Disconnected), 0u);
            ASSERT_TRUE(waitFor({ &host, &client },
                [&] { return client.count(Event::Type::Message) == static_cast<std::size_t>(sent); }));
            EXPECT_EQ(client.count(Event::Type::Disconnected), 0u);
            const std::vector<Event> messages = client.take(Event::Type::Message);
            for (int i = 0; i < sent; ++i)
                EXPECT_EQ(messages[static_cast<std::size_t>(i)].mData, std::to_string(i));
        }
    }
}
