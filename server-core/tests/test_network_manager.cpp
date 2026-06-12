/*
   Regression tests for network_manager session and UDP endpoint management.

   Verifies fixes for:
   - Broadcasting to peers with unregistered UDP endpoints (wasted packets / no sound)
   - Stale same-IP sessions surviving reconnection (stream drift)
   - Crashes when remote_endpoint() is called on broken sockets
   - UDP endpoint registration and data delivery correctness
   - End-to-end reconnection scenario
*/

#define AUDIO_SHARE_TEST_BUILD

#include "network_manager.hpp"

#include <asio.hpp>
#include <spdlog/spdlog.h>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

// ============================================================================
// audio_manager stub — provides just enough to construct network_manager
// without pulling in platform audio capture (PipeWire / WASAPI)
// ============================================================================

audio_manager::audio_manager()
    : _format(std::make_unique<AudioFormat>())
{
}

audio_manager::~audio_manager() = default;

void audio_manager::start_loopback_recording(
    std::shared_ptr<network_manager>, const capture_config&)
{
}

void audio_manager::stop() {}

std::string audio_manager::get_format_binary() { return {}; }

audio_manager::endpoint_list_t audio_manager::get_endpoint_list() { return {}; }

std::string audio_manager::get_default_endpoint() { return {}; }

void audio_manager::do_loopback_recording(
    std::shared_ptr<network_manager>, const capture_config&)
{
}

// ============================================================================
// Test access helper — uses the friend declaration in network_manager
// ============================================================================

struct network_manager_test_access {
    using peer_info_t = network_manager::peer_info_t;
    using playing_peer_list_t = network_manager::playing_peer_list_t;
    using tcp_socket = network_manager::tcp_socket;

    static playing_peer_list_t& peer_list(network_manager& nm)
    {
        return nm._playing_peer_list;
    }

    static void set_udp_server(network_manager& nm,
        std::unique_ptr<network_manager::udp_socket> sock)
    {
        nm._udp_server = std::move(sock);
    }

    static asio::ip::udp::endpoint get_udp_peer(const peer_info_t& info)
    {
        return info.udp_peer;
    }
};

// ============================================================================
// Helpers
// ============================================================================

static std::shared_ptr<network_manager> make_test_nm()
{
    auto am = std::shared_ptr<audio_manager>(new audio_manager());
    auto nm = std::shared_ptr<network_manager>(new network_manager(am));
    nm->_ioc = std::make_shared<asio::io_context>();

    auto udp = std::make_unique<network_manager_test_access::udp_socket>(*nm->_ioc);
    udp->open(asio::ip::udp::v4());
    udp->bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    network_manager_test_access::set_udp_server(*nm, std::move(udp));

    return nm;
}

static std::shared_ptr<network_manager_test_access::tcp_socket>
make_tcp_peer(asio::io_context& ioc)
{
    asio::ip::tcp::acceptor acc(ioc,
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
    auto client = std::make_shared<network_manager_test_access::tcp_socket>(ioc);
    client->connect(acc.local_endpoint());
    auto server_sock = acc.accept();
    acc.close();
    auto result = std::make_shared<network_manager_test_access::tcp_socket>(
        std::move(server_sock));
    return result;
}

// ============================================================================
// Test 1: broadcast_audio_data skips peers whose UDP endpoint is not registered
//
// Bug: Before the fix, broadcast sent to ALL peers including those with
// default-constructed udp::endpoint (0.0.0.0:0), wasting packets during the
// TCP→UDP registration window. This caused "no sound on first play".
// ============================================================================

static void test_broadcast_skips_unregistered_udp_peer()
{
    auto nm = make_test_nm();
    auto& ioc = *nm->_ioc;

    // Two UDP receivers
    asio::ip::udp::socket recv1(ioc);
    recv1.open(asio::ip::udp::v4());
    recv1.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));

    asio::ip::udp::socket recv2(ioc);
    recv2.open(asio::ip::udp::v4());
    recv2.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));

    // Peer A: UDP registered (valid endpoint)
    auto peer_a = make_tcp_peer(ioc);
    int id_a = network_manager_test_access::peer_list(*nm).size() + 1;
    auto info_a = std::make_shared<network_manager_test_access::peer_info_t>();
    info_a->id = 1;
    info_a->udp_peer = recv1.local_endpoint();
    info_a->last_tick = std::chrono::steady_clock::now();
    network_manager_test_access::peer_list(*nm)[peer_a] = info_a;

    // Peer B: UDP NOT registered (default endpoint = 0.0.0.0:0)
    auto peer_b = make_tcp_peer(ioc);
    auto info_b = std::make_shared<network_manager_test_access::peer_info_t>();
    info_b->id = 2;
    // info_b->udp_peer is default-constructed (unspecified address, port 0)
    info_b->last_tick = std::chrono::steady_clock::now();
    network_manager_test_access::peer_list(*nm)[peer_b] = info_b;

    // Initiate receives before broadcast
    std::vector<uint8_t> buf1(4096);
    bool recv1_done = false;
    std::error_code recv1_ec;
    size_t recv1_bytes = 0;
    recv1.async_receive(asio::buffer(buf1),
        [&](const asio::error_code& ec, size_t n) {
            recv1_ec = ec;
            recv1_bytes = n;
            recv1_done = true;
        });

    std::vector<uint8_t> buf2(4096);
    bool recv2_done = false;
    recv2.async_receive(asio::buffer(buf2),
        [&](const asio::error_code&, size_t) {
            recv2_done = true;
        });

    // Broadcast — Peer B should be skipped
    const char data[] = "test_audio_payload_data_for_broadcast";
    nm->broadcast_audio_data(data, sizeof(data) - 1, 4);

    ioc.run();

    assert(recv1_done && !recv1_ec && recv1_bytes > 0);
    assert(!recv2_done); // Peer B has no registered UDP endpoint, nothing sent
    std::cout << "  PASS: broadcast_skips_unregistered_udp_peer\n";
}

// ============================================================================
// Test 2: add_playing_peer removes stale sessions from the same TCP IP
//
// Bug: When a client reconnects (e.g. network switch), a new TCP connection
// arrives with a new socket, but the old peer entry stays in the list until
// heartbeat timeout (5s). Audio is broadcast to the OLD UDP endpoint, causing
// stream drift.
// ============================================================================

static void test_add_peer_cleans_stale_same_ip_session()
{
    auto nm = make_test_nm();
    auto& ioc = *nm->_ioc;

    // Peer 1 connects
    auto peer1 = make_tcp_peer(ioc);
    int id1 = nm->add_playing_peer(peer1);
    assert(id1 > 0);
    assert(network_manager_test_access::peer_list(*nm).size() == 1);

    // Peer 2 connects from same IP (simulates reconnection)
    auto peer2 = make_tcp_peer(ioc);
    int id2 = nm->add_playing_peer(peer2);
    assert(id2 > 0);

    // Peer 1 should have been removed, only peer 2 remains
    auto& list = network_manager_test_access::peer_list(*nm);
    assert(list.size() == 1);
    assert(list.contains(peer2));
    assert(!list.contains(peer1));
    std::cout << "  PASS: add_peer_cleans_stale_same_ip_session\n";
}

// ============================================================================
// Test 3: close_session does not crash on a broken (already closed) socket
//
// Bug: close_session() called peer->remote_endpoint() without error_code,
// which throws on broken sockets, crashing the server.
// ============================================================================

static void test_close_session_on_broken_socket_no_crash()
{
    auto nm = make_test_nm();
    auto& ioc = *nm->_ioc;

    auto peer = make_tcp_peer(ioc);
    nm->add_playing_peer(peer);
    assert(network_manager_test_access::peer_list(*nm).size() == 1);

    // Simulate abrupt disconnect — close the raw socket
    std::error_code ec;
    peer->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    peer->close(ec);

    // close_session must NOT throw even though socket is already closed
    bool threw = false;
    try {
        nm->close_session(peer);
    } catch (...) {
        threw = true;
    }

    assert(!threw);
    assert(network_manager_test_access::peer_list(*nm).empty());
    std::cout << "  PASS: close_session_on_broken_socket_no_crash\n";
}

// ============================================================================
// Test 4: fill_udp_peer correctly registers the endpoint and data is delivered
//
// Verifies the full registration chain: add_playing_peer → fill_udp_peer →
// broadcast_audio_data → data arrives at the registered UDP address.
// ============================================================================

static void test_fill_udp_peer_registers_correct_endpoint()
{
    auto nm = make_test_nm();
    auto& ioc = *nm->_ioc;

    // Create receiver
    asio::ip::udp::socket receiver(ioc);
    receiver.open(asio::ip::udp::v4());
    receiver.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));

    // Add peer and register UDP endpoint via fill_udp_peer
    auto peer = make_tcp_peer(ioc);
    int id = nm->add_playing_peer(peer);
    assert(id > 0);

    nm->fill_udp_peer(id, receiver.local_endpoint());

    // Verify UDP peer is set
    auto& list = network_manager_test_access::peer_list(*nm);
    auto it = list.find(peer);
    assert(it != list.end());
    assert(network_manager_test_access::get_udp_peer(*it->second)
        == receiver.local_endpoint());

    // Initiate receive, then broadcast
    std::vector<uint8_t> buf(4096);
    bool recv_done = false;
    size_t recv_bytes = 0;
    std::error_code recv_ec;
    receiver.async_receive(asio::buffer(buf),
        [&](const asio::error_code& ec, size_t n) {
            recv_ec = ec;
            recv_bytes = n;
            recv_done = true;
        });

    const char data[] = "fill_udp_peer_test_payload";
    nm->broadcast_audio_data(data, sizeof(data) - 1, 4);

    ioc.run();

    assert(recv_done);
    assert(!recv_ec);
    assert(recv_bytes > 0);
    std::cout << "  PASS: fill_udp_peer_registers_correct_endpoint\n";
}

// ============================================================================
// Test 5: End-to-end reconnection scenario
//
// Simulates: peer1 connects → registers UDP → receives data → "disconnects"
// (socket broken but heartbeat not yet timed out) → peer2 reconnects from
// same IP → only peer2's new UDP endpoint receives data.
// ============================================================================

static void test_reconnect_full_scenario()
{
    auto nm = make_test_nm();
    auto& ioc = *nm->_ioc;

    // Old UDP receiver (peer1's original endpoint)
    asio::ip::udp::socket old_recv(ioc);
    old_recv.open(asio::ip::udp::v4());
    old_recv.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));

    // Step 1: Peer 1 connects and registers UDP
    auto peer1 = make_tcp_peer(ioc);
    int id1 = nm->add_playing_peer(peer1);
    nm->fill_udp_peer(id1, old_recv.local_endpoint());
    assert(network_manager_test_access::peer_list(*nm).size() == 1);

    // Verify peer1 receives data before reconnection
    {
        std::vector<uint8_t> buf(4096);
        bool got = false;
        size_t n = 0;
        old_recv.async_receive(asio::buffer(buf),
            [&](const asio::error_code&, size_t bytes) {
                got = true;
                n = bytes;
            });
        const char data[] = "pre_reconnect_data";
        nm->broadcast_audio_data(data, sizeof(data) - 1, 4);
        ioc.restart();
        ioc.run();
        assert(got && n > 0);
    }

    // Step 2: Peer 1's TCP connection breaks (no heartbeat timeout yet)
    {
        std::error_code ec;
        peer1->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        peer1->close(ec);
    }

    // Step 3: Peer 2 reconnects from same IP → triggers stale cleanup
    auto peer2 = make_tcp_peer(ioc);
    int id2 = nm->add_playing_peer(peer2);
    assert(id2 > 0);
    assert(id2 != id1); // New session gets a different ID

    // Verify peer1 was cleaned up
    auto& list = network_manager_test_access::peer_list(*nm);
    assert(list.size() == 1);
    assert(!list.contains(peer1));
    assert(list.contains(peer2));

    // Step 4: Register peer2's new UDP endpoint
    asio::ip::udp::socket new_recv(ioc);
    new_recv.open(asio::ip::udp::v4());
    new_recv.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
    nm->fill_udp_peer(id2, new_recv.local_endpoint());

    // Step 5: Broadcast and verify only new endpoint gets data
    bool old_got = false;
    bool new_got = false;
    std::vector<uint8_t> buf_old(4096);
    old_recv.async_receive(asio::buffer(buf_old),
        [&](const asio::error_code&, size_t) { old_got = true; });
    std::vector<uint8_t> buf_new(4096);
    new_recv.async_receive(asio::buffer(buf_new),
        [&](const asio::error_code&, size_t) { new_got = true; });

    const char data[] = "post_reconnect_data";
    nm->broadcast_audio_data(data, sizeof(data) - 1, 4);

    ioc.restart();
    ioc.run();

    assert(new_got);  // New endpoint receives data
    assert(!old_got); // Old endpoint does NOT receive data
    std::cout << "  PASS: reconnect_full_scenario\n";
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    spdlog::set_level(spdlog::level::off);

    std::cout << "Running network_manager regression tests...\n";
    test_broadcast_skips_unregistered_udp_peer();
    test_add_peer_cleans_stale_same_ip_session();
    test_close_session_on_broken_socket_no_crash();
    test_fill_udp_peer_registers_correct_endpoint();
    test_reconnect_full_scenario();
    std::cout << "All tests passed.\n";

    return 0;
}
