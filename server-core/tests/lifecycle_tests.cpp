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

// Regression tests for the server start/stop lifecycle: partial-start rollback
// and ordered, idempotent teardown. These exercise the real network_manager
// and audio_manager against a deterministic fake audio backend (see
// fake_audio_manager_impl.cpp), so they run on any platform.
//
// Several of these would crash (std::terminate / std::system_error) against the
// pre-fix code:
//   - T2/T3 joined a non-joinable std::thread,
//   - T5/T6 left is_running() lying and (T6) leaked the recording thread, so a
//     later start move-assigned onto a joinable std::thread -> std::terminate,
//   - T8 destroyed an audio_manager with a still-joinable thread -> terminate.

#include "audio_manager.hpp"
#include "network_manager.hpp"
#include "fake_audio_control.hpp"

#include "pre_asio.hpp"
#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* expr, const char* file, int line)
{
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("    [FAIL] %s  (%s:%d)\n", expr, file, line);
    }
}
#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

constexpr const char* kHost = "127.0.0.1";

std::pair<std::shared_ptr<audio_manager>, std::shared_ptr<network_manager>> make_managers()
{
    auto am = std::make_shared<audio_manager>();
    auto nm = std::make_shared<network_manager>(am);
    return { am, nm };
}

audio_manager::capture_config healthy_config()
{
    audio_manager::capture_config c;
    c.endpoint_id = fake_audio::kHealthyEndpoint;
    c.encoding = audio_manager::encoding_t::encoding_default;
    return c;
}

audio_manager::capture_config failing_config()
{
    audio_manager::capture_config c;
    c.endpoint_id = fake_audio::kFailImmediately;
    c.encoding = audio_manager::encoding_t::encoding_default;
    return c;
}

// Ask the OS for a currently-free TCP port by binding to port 0.
std::uint16_t find_free_port()
{
    asio::io_context ioc;
    asio::ip::tcp::acceptor acc(ioc, asio::ip::tcp::endpoint(asio::ip::make_address(kHost), 0));
    auto port = acc.local_endpoint().port();
    acc.close();
    return port;
}

bool wait_for_active(int expected, std::chrono::milliseconds timeout = 2000ms)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fake_audio::active_count() == expected) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return fake_audio::active_count() == expected;
}

bool start_throws(const std::shared_ptr<network_manager>& nm, std::uint16_t port,
    const audio_manager::capture_config& cfg)
{
    try {
        nm->start_server(kHost, port, cfg);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

// ---------------------------------------------------------------------------

void t_normal_start_stop()
{
    std::printf("[T1] normal start/stop brings every resource up and back down\n");
    auto [am, nm] = make_managers();
    auto port = find_free_port();

    bool threw = false;
    try {
        nm->start_server(kHost, port, healthy_config());
    } catch (const std::exception& e) {
        threw = true;
        std::printf("    unexpected exception: %s\n", e.what());
    }
    CHECK(!threw);
    CHECK(nm->is_running());
    CHECK(wait_for_active(1));

    nm->stop_server();
    CHECK(!nm->is_running());
    CHECK(fake_audio::active_count() == 0);
}

void t_stop_without_start()
{
    std::printf("[T2] stop_server() before any start is a safe no-op\n");
    auto [am, nm] = make_managers();
    bool threw = false;
    try {
        nm->stop_server();
    } catch (...) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(!nm->is_running());
}

void t_double_stop()
{
    std::printf("[T3] stop_server() is idempotent\n");
    auto [am, nm] = make_managers();
    nm->start_server(kHost, find_free_port(), healthy_config());
    nm->stop_server();
    bool threw = false;
    try {
        nm->stop_server();
    } catch (...) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(!nm->is_running());
    CHECK(fake_audio::active_count() == 0);
}

void t_restart_same_port()
{
    std::printf("[T4] restart on the same port after a clean stop works\n");
    auto [am, nm] = make_managers();
    auto port = find_free_port();
    nm->start_server(kHost, port, healthy_config());
    nm->stop_server();

    bool threw = start_throws(nm, port, healthy_config());
    CHECK(!threw);
    CHECK(nm->is_running());
    nm->stop_server();
    CHECK(fake_audio::active_count() == 0);
}

void t_tcp_bind_conflict_rollback()
{
    std::printf("[T5] TCP bind conflict rolls back; manager stays reusable\n");
    auto [amA, nmA] = make_managers();
    auto port = find_free_port();
    nmA->start_server(kHost, port, healthy_config());
    CHECK(nmA->is_running());
    CHECK(wait_for_active(1));

    auto [amB, nmB] = make_managers();
    CHECK(start_throws(nmB, port, healthy_config())); // bind must fail
    CHECK(!nmB->is_running());                         // and roll back
    CHECK(wait_for_active(1));                         // only A's recording remains

    // B must remain usable afterwards on a free port.
    CHECK(!start_throws(nmB, find_free_port(), healthy_config()));
    CHECK(nmB->is_running());

    nmB->stop_server();
    nmA->stop_server();
    CHECK(fake_audio::active_count() == 0);
}

void t_udp_bind_conflict_rollback()
{
    std::printf("[T6] UDP bind conflict after the recording thread started rolls back fully\n");
    const int baseline = fake_audio::active_count();
    auto port = find_free_port();

    // Occupy the UDP port (exclusively) so the server's TCP bind succeeds but
    // its UDP bind fails -- i.e. a failure *after* start_loopback_recording.
    asio::io_context ioc;
    asio::ip::udp::socket hog(ioc, asio::ip::udp::endpoint(asio::ip::make_address(kHost), port));

    auto [am, nm] = make_managers();
    CHECK(start_throws(nm, port, healthy_config())); // UDP bind must fail
    CHECK(!nm->is_running());                         // rolled back
    CHECK(wait_for_active(baseline));                 // the started recording thread was stopped+joined

    // Free the UDP port. If rollback truly released the TCP acceptor, a fresh
    // start on the same port now succeeds (proves no port / thread leak).
    hog.close();
    bool threw = false;
    try {
        nm->start_server(kHost, port, healthy_config());
    } catch (const std::exception& e) {
        threw = true;
        std::printf("    unexpected exception: %s\n", e.what());
    }
    CHECK(!threw);
    CHECK(nm->is_running());

    nm->stop_server();
    CHECK(fake_audio::active_count() == baseline);
}

void t_invalid_encoding_guard()
{
    std::printf("[T7] invalid encoding fails fast without acquiring resources\n");
    const int baseline = fake_audio::active_count();
    auto [am, nm] = make_managers();
    auto port = find_free_port();

    audio_manager::capture_config c = healthy_config();
    c.encoding = audio_manager::encoding_t::encoding_invalid;
    CHECK(start_throws(nm, port, c));
    CHECK(!nm->is_running());
    CHECK(fake_audio::active_count() == baseline);

    // The port must be untouched: a normal start on the same port works.
    CHECK(!start_throws(nm, port, healthy_config()));
    nm->stop_server();
    CHECK(fake_audio::active_count() == baseline);
}

void t_audio_manager_dtor_safety()
{
    std::printf("[T8] destroying a manager whose recording thread already exited is safe\n");
    const int baseline = fake_audio::active_count();
    auto [am, nm] = make_managers();

    // Start a recording that returns almost immediately (simulated init
    // failure). The record thread finishes on its own; nobody calls stop().
    am->start_loopback_recording(nm, failing_config());
    std::this_thread::sleep_for(50ms); // let the thread run and release its self-capture
    CHECK(fake_audio::active_count() == baseline);

    // Drop all references. The audio_manager's record thread is joinable (it
    // finished but was never joined). The old defaulted destructor would
    // std::terminate() here; the fixed one must join cleanly.
    bool ok = true;
    try {
        nm.reset();
        am.reset();
    } catch (...) {
        ok = false;
    }
    CHECK(ok);
    CHECK(fake_audio::active_count() == baseline);
}

} // namespace

int main()
{
    std::printf("server-core lifecycle regression tests\n\n");

    t_normal_start_stop();
    t_stop_without_start();
    t_double_stop();
    t_restart_same_port();
    t_tcp_bind_conflict_rollback();
    t_udp_bind_conflict_rollback();
    t_invalid_encoding_guard();
    t_audio_manager_dtor_safety();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
    }
    return g_failures == 0 ? 0 : 1;
}
