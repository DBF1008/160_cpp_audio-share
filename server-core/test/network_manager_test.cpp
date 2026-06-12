/*
   Copyright 2022-2024 mkckr0 <https://github.com/mkckr0>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

// Regression tests for the play-session / UDP target-registration link.
//
// They pin the fix for two defects in network_manager:
//   1. During the TCP->UDP handshake window the server used to broadcast audio
//      to peers whose UDP endpoint was not registered yet (a default 0.0.0.0:0
//      endpoint), so first playback was silent.
//   2. On a network switch / fast reconnect the old session lingered in the
//      peer list and audio kept drifting to the stale endpoint, while the
//      session still looked healthy (heartbeat is TCP-only).
//
// The fix only streams to peers with a registered UDP endpoint, requires the
// UDP source address to match the TCP control connection, and evicts any
// same-host stale session when a new endpoint registers.
//
// The tests drive network_manager's real internals through the `nm_test` friend
// and never construct an audio_manager, so they touch no audio backend.

#include "network_manager.hpp"

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ip = asio::ip;

namespace {
int g_failures = 0;
int g_checks = 0;
} // namespace

#define CHECK(cond)                                                                          \
    do {                                                                                      \
        ++g_checks;                                                                           \
        if (!(cond)) {                                                                        \
            std::cerr << "[FAIL] " << __func__ << ": " #cond " (line " << __LINE__ << ")\n";  \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

struct nm_test {
    using tcp_socket = network_manager::tcp_socket;
    using peer_info_t = network_manager::peer_info_t;
    using udp_endpoint = ip::udp::endpoint;

    // network_manager only stores the audio_manager and never touches it on the
    // paths under test, so a null one keeps the tests free of PipeWire / audio.
    static std::shared_ptr<network_manager> make_nm()
    {
        std::shared_ptr<audio_manager> no_audio;
        return std::make_shared<network_manager>(no_audio);
    }

    static udp_endpoint udp(const char* addr, std::uint16_t port)
    {
        return udp_endpoint(ip::make_address(addr), port);
    }

    static void insert_peer(const std::shared_ptr<network_manager>& nm,
        const std::shared_ptr<tcp_socket>& key, int id, std::optional<udp_endpoint> ep)
    {
        auto info = std::make_shared<peer_info_t>();
        info->id = id;
        info->udp_peer = ep;
        nm->_playing_peer_list[key] = info;
    }

    // A connected loopback TCP connection. The accepted (server) side is used as
    // a playing-peer key so it reports a real remote_endpoint() of 127.0.0.1,
    // which fill_udp_peer's source-address check depends on. The client and
    // acceptor are kept alive to hold the connection open.
    struct conn {
        std::shared_ptr<tcp_socket> server;
        std::shared_ptr<ip::tcp::socket> client;
        std::shared_ptr<ip::tcp::acceptor> acceptor;
    };

    static conn make_loopback_conn(asio::io_context& ioc)
    {
        auto acceptor = std::make_shared<ip::tcp::acceptor>(
            ioc, ip::tcp::endpoint(ip::make_address("127.0.0.1"), 0));
        auto client = std::make_shared<ip::tcp::socket>(ioc);
        std::error_code ec;
        // Synchronous on loopback: connect completes via the kernel listen
        // backlog, then accept dequeues it immediately. No io_context run needed.
        client->connect(acceptor->local_endpoint(), ec);
        auto server = std::make_shared<tcp_socket>(ioc.get_executor());
        acceptor->accept(*server, ec);
        return conn { server, client, acceptor };
    }

    // Bug 1: a peer without a registered UDP endpoint must never be a send
    // target. The old broadcast loop sent to every peer, including the default
    // 0.0.0.0:0 endpoint of peers still in the handshake window.
    static void test_window_excludes_unregistered_peers()
    {
        asio::io_context ioc; // declared first so it outlives every socket below
        auto nm = make_nm();

        insert_peer(nm, std::make_shared<tcp_socket>(ioc.get_executor()), 1, udp("127.0.0.1", 5000));
        insert_peer(nm, std::make_shared<tcp_socket>(ioc.get_executor()), 2, std::nullopt);
        insert_peer(nm, std::make_shared<tcp_socket>(ioc.get_executor()), 3, udp_endpoint()); // default 0.0.0.0:0

        const auto targets = nm->collect_udp_targets();
        CHECK(targets.size() == 1);
        if (targets.size() == 1) {
            CHECK(targets.front() == udp("127.0.0.1", 5000));
        }
    }

    // Bug 2 (decision): only same-host registered sessions, other than the
    // keeper, are stale; unregistered or different-host peers are kept.
    static void test_select_stale_ids()
    {
        const std::vector<std::pair<int, std::optional<udp_endpoint>>> peers = {
            { 1, udp("127.0.0.1", 5000) }, // same host, registered -> stale
            { 2, std::nullopt },           // unregistered -> keep
            { 3, udp("127.0.0.1", 6000) }, // the keeper itself -> keep
            { 4, udp("10.0.0.9", 7000) },  // different host -> keep
        };
        const auto stale = network_manager::select_stale_ids(peers, /*keep_id=*/3, ip::make_address("127.0.0.1"));
        CHECK(stale.size() == 1);
        if (stale.size() == 1) {
            CHECK(stale.front() == 1);
        }
    }

    // Bug 2 (wiring): fill_udp_peer rejects a UDP source that doesn't match the
    // TCP control connection's address, then accepts a matching one.
    static void test_fill_udp_peer_rejects_mismatched_source()
    {
        asio::io_context ioc; // declared first so it outlives every socket below
        auto nm = make_nm();
        auto a = make_loopback_conn(ioc);
        CHECK(a.server->is_open());

        const int id = nm->add_playing_peer(a.server);
        CHECK(id > 0);

        nm->fill_udp_peer(id, udp("10.1.2.3", 9999)); // wrong host -> rejected
        CHECK(nm->collect_udp_targets().empty());

        nm->fill_udp_peer(id, udp("127.0.0.1", 5000)); // matches -> accepted
        const auto targets = nm->collect_udp_targets();
        CHECK(targets.size() == 1);
        if (targets.size() == 1) {
            CHECK(targets.front() == udp("127.0.0.1", 5000));
        }
    }

    // Bug 2 (end-to-end): a fresh registration from the same host evicts the
    // earlier session and closes it, so audio flows only to the new endpoint.
    static void test_reconnect_evicts_stale_session()
    {
        asio::io_context ioc; // declared first so it outlives every socket below
        auto nm = make_nm();
        auto a = make_loopback_conn(ioc);
        auto b = make_loopback_conn(ioc);
        CHECK(a.server->is_open() && b.server->is_open());

        const int id_a = nm->add_playing_peer(a.server);
        const int id_b = nm->add_playing_peer(b.server);
        CHECK(id_a > 0 && id_b > 0 && id_a != id_b);

        nm->fill_udp_peer(id_a, udp("127.0.0.1", 5000));
        CHECK(nm->collect_udp_targets().size() == 1);

        nm->fill_udp_peer(id_b, udp("127.0.0.1", 6000)); // same host supersedes A
        const auto targets = nm->collect_udp_targets();
        CHECK(targets.size() == 1);
        if (targets.size() == 1) {
            CHECK(targets.front() == udp("127.0.0.1", 6000));
        }
        CHECK(!nm->_playing_peer_list.contains(a.server)); // A removed from the list
        CHECK(!a.server->is_open());                       // A's session closed
    }

    static int run()
    {
        spdlog::set_level(spdlog::level::off); // keep test output to the checks only

        test_window_excludes_unregistered_peers();
        test_select_stale_ids();
        test_fill_udp_peer_rejects_mismatched_source();
        test_reconnect_evicts_stale_session();

        if (g_failures == 0) {
            std::cout << "network_manager_test: all " << g_checks << " checks passed\n";
            return 0;
        }
        std::cerr << "network_manager_test: " << g_failures << " of " << g_checks << " checks FAILED\n";
        return 1;
    }
};

int main()
{
    return nm_test::run();
}
