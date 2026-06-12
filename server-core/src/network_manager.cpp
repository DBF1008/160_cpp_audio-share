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

#include "network_manager.hpp"
#include "formatter.hpp"
#include "audio_manager.hpp"

#include <list>
#include <ranges>
#include <coroutine>
#include <algorithm>
#include <optional>
#include <vector>
#include <utility>

#ifdef _WINDOWS
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Iphlpapi.lib")
#endif // _WINDOWS

#ifdef linux
#include <sys/types.h>
#include <ifaddrs.h>
#endif

#include <spdlog/spdlog.h>
#include <fmt/ranges.h>

namespace ip = asio::ip;
using namespace std::chrono_literals;

network_manager::network_manager(std::shared_ptr<audio_manager>& audio_manager)
    : _audio_manager(audio_manager)
{
}

std::vector<std::string> network_manager::get_address_list()
{
    std::vector<std::string> address_list;

#ifdef _WINDOWS
    ULONG family = AF_INET;
    ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES;

    ULONG size = 0;
    GetAdaptersAddresses(family, flags, nullptr, nullptr, &size);
    auto pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(size);

    auto ret = GetAdaptersAddresses(family, flags, nullptr, pAddresses, &size);
    if (ret == ERROR_SUCCESS) {
        for (auto pCurrentAddress = pAddresses; pCurrentAddress; pCurrentAddress = pCurrentAddress->Next) {
            if (pCurrentAddress->OperStatus != IfOperStatusUp || pCurrentAddress->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            for (auto pUnicast = pCurrentAddress->FirstUnicastAddress; pUnicast; pUnicast = pUnicast->Next) {
                auto sockaddr = (sockaddr_in*)pUnicast->Address.lpSockaddr;
                char buf[50];
                if (inet_ntop(AF_INET, &sockaddr->sin_addr, buf, sizeof(buf))) {
                    address_list.emplace_back(buf);
                }
            }
        }
    }

    free(pAddresses);
#endif

#ifdef linux
    struct ifaddrs* ifaddrs;
    if (getifaddrs(&ifaddrs) == -1) {
        return address_list;
    }

    for (auto ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            continue;
        }
        auto sockaddr = (sockaddr_in*)ifa->ifa_addr;
        char buf[50];
        if (inet_ntop(AF_INET, &sockaddr->sin_addr, buf, sizeof(buf))) {
            address_list.emplace_back(buf);
        }
    }

    freeifaddrs(ifaddrs);
#endif

    return address_list;
}

std::string network_manager::get_default_address()
{
    return select_default_address(get_address_list());
}

std::string network_manager::select_default_address(const std::vector<std::string>& address_list)
{
    if (address_list.empty()) {
        return {};
    }

    auto is_private_address = [](const std::string& address) {
        constexpr uint32_t private_addr_list[] = {
            0x0a000000,
            0xac100000,
            0xc0a80000,
        };

        uint32_t addr;
        inet_pton(AF_INET, address.c_str(), &addr);
        addr = ntohl(addr);
        for (auto&& private_addr : private_addr_list) {
            if ((addr & private_addr) == private_addr) {
                return true;
            }
        }

        return false;
    };

    for (auto&& address : address_list) {
        if (is_private_address(address)) {
            return address;
        }
    }
    return address_list.front();
}

void network_manager::start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config)
{
    _ioc = std::make_shared<asio::io_context>();
    {
        ip::tcp::endpoint endpoint { ip::make_address(host), port };

        ip::tcp::acceptor acceptor(*_ioc, endpoint.protocol());
        acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        _audio_manager->start_loopback_recording(shared_from_this(), capture_config);
        asio::co_spawn(*_ioc, accept_tcp_loop(std::move(acceptor)), asio::detached);

        // start tcp success
        spdlog::info("tcp listen success on {}", endpoint);
    }

    {
        ip::udp::endpoint endpoint { ip::make_address(host), port };
        _udp_server = std::make_unique<udp_socket>(*_ioc, endpoint.protocol());
        _udp_server->bind(endpoint);
        asio::co_spawn(*_ioc, accept_udp_loop(), asio::detached);

        // start udp success
        spdlog::info("udp listen success on {}", endpoint);
    }

    _net_thread = std::thread([self = shared_from_this()] {
        self->_ioc->run();
    });

    spdlog::info("server started");
}

void network_manager::stop_server()
{
    if (_ioc) {
        _ioc->stop();
    }
    _net_thread.join();
    _audio_manager->stop();
    _playing_peer_list.clear();
    _udp_server = nullptr;
    _ioc = nullptr;
    spdlog::info("server stopped");
}

void network_manager::wait_server()
{
    _net_thread.join();
}

bool network_manager::is_running() const
{
    return _ioc != nullptr;
}

asio::awaitable<void> network_manager::read_loop(std::shared_ptr<tcp_socket> peer)
{
    while (true) {
        cmd_t cmd = cmd_t::cmd_none;
        auto [ec, _] = co_await asio::async_read(*peer, asio::buffer(&cmd, sizeof(cmd)));
        if (ec) {
            close_session(peer);
            spdlog::trace("{} {}", __func__, ec);
            break;
        }

        spdlog::trace("cmd {}", (uint32_t)cmd);

        if (cmd == cmd_t::cmd_get_format) {
            auto format = _audio_manager->get_format_binary();
            auto size = (uint32_t)format.size();
            std::array<asio::const_buffer, 3> buffers = {
                asio::buffer(&cmd, sizeof(cmd)),
                asio::buffer(&size, sizeof(size)),
                asio::buffer(format),
            };
            auto [ec, _] = co_await asio::async_write(*peer, buffers);
            if (ec) {
                close_session(peer);
                spdlog::trace("{} {}", __func__, ec);
                break;
            }
        } else if (cmd == cmd_t::cmd_start_play) {
            int id = add_playing_peer(peer);
            if (id <= 0) {
                spdlog::error("{} id error", __func__);
                close_session(peer);
                spdlog::trace("{} {}", __func__, ec);
                break;
            }
            std::array<asio::const_buffer, 2> buffers = {
                asio::buffer(&cmd, sizeof(cmd)),
                asio::buffer(&id, sizeof(id)),
            };
            auto [ec, _] = co_await asio::async_write(*peer, buffers);
            if (ec) {
                spdlog::trace("{} {}", __func__, ec);
                close_session(peer);
                break;
            }
            asio::co_spawn(*_ioc, heartbeat_loop(peer), asio::detached);
        } else if (cmd == cmd_t::cmd_heartbeat) {
            auto it = _playing_peer_list.find(peer);
            if (it != _playing_peer_list.end()) {
                it->second->last_tick = std::chrono::steady_clock::now();
            }
        } else {
            spdlog::error("{} error cmd", __func__);
            close_session(peer);
            break;
        }
    }
    spdlog::trace("stop {}", __func__);
}

asio::awaitable<void> network_manager::heartbeat_loop(std::shared_ptr<tcp_socket> peer)
{
    std::error_code ec;
    size_t _;

    steady_timer timer(*_ioc);
    while (true) {
        timer.expires_after(3s);
        std::tie(ec) = co_await timer.async_wait();
        if (ec) {
            break;
        }

        if (!peer->is_open()) {
            break;
        }

        auto it = _playing_peer_list.find(peer);
        if (it == _playing_peer_list.end()) {
            spdlog::trace("{} it == _playing_peer_list.end()", __func__);
            close_session(peer);
            break;
        }
        if (std::chrono::steady_clock::now() - it->second->last_tick > _heartbeat_timeout) {
            spdlog::info("{} timeout", it->first->remote_endpoint());
            close_session(peer);
            break;
        }

        auto cmd = cmd_t::cmd_heartbeat;
        std::tie(ec, _) = co_await asio::async_write(*peer, asio::buffer(&cmd, sizeof(cmd)));
        if (ec) {
            spdlog::trace("{} {}", __func__, ec);
            close_session(peer);
            break;
        }
    }
    spdlog::trace("stop {}", __func__);
}

asio::awaitable<void> network_manager::accept_tcp_loop(tcp_acceptor acceptor)
{
    while (true) {
        auto peer = std::make_shared<tcp_socket>(acceptor.get_executor());
        auto [ec] = co_await acceptor.async_accept(*peer);
        if (ec) {
            spdlog::error("{} {}", __func__, ec);
            co_return;
        }

        spdlog::info("accept {}", peer->remote_endpoint());

        // No-Delay
        peer->set_option(ip::tcp::no_delay(true), ec);
        if (ec) {
            spdlog::info("{} {}", __func__, ec);
        }

        asio::co_spawn(acceptor.get_executor(), read_loop(peer), asio::detached);
    }
}

asio::awaitable<void> network_manager::accept_udp_loop()
{
    while (true) {
        int id = 0;
        ip::udp::endpoint udp_peer;
        auto [ec, _] = co_await _udp_server->async_receive_from(asio::buffer(&id, sizeof(id)), udp_peer);
        if (ec) {
            spdlog::info("{} {}", __func__, ec);
            co_return;
        }

        fill_udp_peer(id, udp_peer);
    }
}

auto network_manager::close_session(std::shared_ptr<tcp_socket>& peer) -> playing_peer_list_t::iterator
{
    // May be called more than once for the same socket (e.g. evict_stale_peers
    // closes it, then its own read_loop unwinds and closes it again), so every
    // step must tolerate an already-closed socket and never throw.
    std::error_code ec;
    auto remote = peer->remote_endpoint(ec);
    if (!ec) {
        spdlog::info("close {}", remote);
    }
    auto it = remove_playing_peer(peer);
    peer->shutdown(ip::tcp::socket::shutdown_both, ec);
    peer->close(ec);
    return it;
}

int network_manager::add_playing_peer(std::shared_ptr<tcp_socket>& peer)
{
    if (_playing_peer_list.contains(peer)) {
        spdlog::error("{} repeat add tcp://{}", __func__, peer->remote_endpoint());
        return 0;
    }

    auto info = _playing_peer_list[peer] = std::make_shared<peer_info_t>();
    static int g_id = 0;
    info->id = ++g_id;
    info->last_tick = std::chrono::steady_clock::now();

    spdlog::trace("{} add id:{} tcp://{}", __func__, info->id, peer->remote_endpoint());
    return info->id;
}

auto network_manager::remove_playing_peer(std::shared_ptr<tcp_socket>& peer) -> playing_peer_list_t::iterator
{
    auto it = _playing_peer_list.find(peer);
    if (it == _playing_peer_list.end()) {
        // Expected during eviction / disconnect races (close_session may run
        // twice for one socket), so this is not an error.
        spdlog::debug("{} tcp peer already removed", __func__);
        return it;
    }

    it = _playing_peer_list.erase(it);
    spdlog::trace("{} remove tcp peer", __func__);
    return it;
}

void network_manager::fill_udp_peer(int id, asio::ip::udp::endpoint udp_peer)
{
    auto it = std::find_if(_playing_peer_list.begin(), _playing_peer_list.end(), [id](const playing_peer_list_t::value_type& e) {
        return e.second->id == id;
    });

    if (it == _playing_peer_list.cend()) {
        spdlog::error("{} no tcp peer id:{} udp://{}", __func__, id, udp_peer);
        return;
    }

    auto tcp_peer = it->first;  // keep the key alive across eviction
    auto info = it->second;

    // The UDP registration must originate from the same host as the TCP control
    // connection. After a network switch / fast reconnect a stale datagram from
    // an old session can race in; honoring it would point the stream at a wrong
    // or dead endpoint, so reject any source-address mismatch.
    std::error_code ec;
    auto tcp_remote = tcp_peer->remote_endpoint(ec);
    if (ec) {
        spdlog::warn("{} tcp peer already closed id:{} udp://{}", __func__, id, udp_peer);
        return;
    }
    if (udp_peer.address() != tcp_remote.address()) {
        spdlog::warn("{} reject udp registration with mismatched source id:{} tcp://{} udp://{}", __func__, id, tcp_remote, udp_peer);
        return;
    }

    // A fresh registration from this host supersedes any earlier playing session
    // from the same host (the old TCP session is left half-open on a network
    // switch / fast reconnect). Drop the stale ones first so audio flows only to
    // the current endpoint instead of drifting to the old one.
    evict_stale_peers(tcp_peer, udp_peer.address());

    info->udp_peer = udp_peer;
    spdlog::info("{} fill udp peer id:{} tcp://{} udp://{}", __func__, id, tcp_remote, udp_peer);
}

bool network_manager::is_udp_registered(const peer_info_t& info)
{
    if (!info.udp_peer.has_value()) {
        return false;
    }
    // Guard against a never-registered / zeroed endpoint slipping through: a real
    // client never sits on the unspecified address or port 0.
    const auto& endpoint = *info.udp_peer;
    return !endpoint.address().is_unspecified() && endpoint.port() != 0;
}

std::vector<asio::ip::udp::endpoint> network_manager::collect_udp_targets() const
{
    std::vector<asio::ip::udp::endpoint> targets;
    targets.reserve(_playing_peer_list.size());
    for (const auto& [peer, info] : _playing_peer_list) {
        if (is_udp_registered(*info)) {
            targets.push_back(*info->udp_peer);
        }
    }
    return targets;
}

std::vector<int> network_manager::select_stale_ids(
    const std::vector<std::pair<int, std::optional<asio::ip::udp::endpoint>>>& peers,
    int keep_id, const asio::ip::address& address)
{
    std::vector<int> stale;
    for (const auto& [id, udp_peer] : peers) {
        if (id == keep_id) {
            continue;
        }
        if (udp_peer && udp_peer->address() == address) {
            stale.push_back(id);
        }
    }
    return stale;
}

void network_manager::evict_stale_peers(const std::shared_ptr<tcp_socket>& keep, const asio::ip::address& address)
{
    int keep_id = 0;
    if (auto keep_it = _playing_peer_list.find(keep); keep_it != _playing_peer_list.end()) {
        keep_id = keep_it->second->id;
    }

    std::vector<std::pair<int, std::optional<asio::ip::udp::endpoint>>> view;
    view.reserve(_playing_peer_list.size());
    for (const auto& [peer, info] : _playing_peer_list) {
        view.emplace_back(info->id, info->udp_peer);
    }

    const auto stale_ids = select_stale_ids(view, keep_id, address);
    if (stale_ids.empty()) {
        return;
    }

    // Collect the sockets first; closing them mutates _playing_peer_list.
    std::vector<std::shared_ptr<tcp_socket>> stale_sockets;
    for (const auto& [peer, info] : _playing_peer_list) {
        if (std::find(stale_ids.begin(), stale_ids.end(), info->id) != stale_ids.end()) {
            stale_sockets.push_back(peer);
        }
    }

    for (auto& peer : stale_sockets) {
        spdlog::info("{} evict stale session from {} superseded by new registration", __func__, address.to_string());
        close_session(peer);
    }
}

void network_manager::broadcast_audio_data(const char* data, size_t count, int block_align)
{
    if (count <= 0) {
        return;
    }
    // spdlog::trace("broadcast_audio_data count: {}", count);

    // divide udp frame
    constexpr int mtu = 1492;
    int max_seg_size = mtu - 20 - 8;
    max_seg_size -= max_seg_size % block_align; // one single sample can't be divided

    std::list<std::shared_ptr<std::vector<uint8_t>>> seg_list;

    for (int begin_pos = 0; begin_pos < count;) {
        const int real_seg_size = std::min((int)count - begin_pos, max_seg_size);
        auto seg = std::make_shared<std::vector<uint8_t>>(real_seg_size);
        std::copy((const uint8_t*)data + begin_pos, (const uint8_t*)data + begin_pos + real_seg_size, seg->begin());
        seg_list.push_back(seg);
        begin_pos += real_seg_size;
    }

    _ioc->post([seg_list = std::move(seg_list), self = shared_from_this()] {
        // Only send to peers that have completed UDP registration. A peer still
        // in the TCP->UDP handshake window (or whose stale session was just
        // evicted) has no valid endpoint, and must not be sent audio — otherwise
        // it is fired at 0.0.0.0:0 and lost, which is why first playback was
        // silent and reconnects drifted to old endpoints.
        const auto targets = self->collect_udp_targets();
        if (targets.empty()) {
            return;
        }
        for (const auto& seg : seg_list) {
            for (const auto& target : targets) {
                self->_udp_server->async_send_to(asio::buffer(*seg), target, [seg](const asio::error_code& ec, std::size_t bytes_transferred) { });
            }
        }
    });
}
