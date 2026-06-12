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

#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <memory>
#include <vector>
#include <string>
#include <map>
#include <optional>
#include <utility>

#include "pre_asio.hpp"
#include <asio.hpp>
#include <asio/use_awaitable.hpp>

#include "audio_manager.hpp"

class network_manager : public std::enable_shared_from_this<network_manager>
{
    // Grants the regression tests access to the session / UDP-registration
    // internals exercised by the bug fix. See test/network_manager_test.cpp.
    friend struct nm_test;

    using default_token = asio::as_tuple_t<asio::use_awaitable_t<>>;
    using tcp_acceptor = default_token::as_default_on_t<asio::ip::tcp::acceptor>;
    using tcp_socket = default_token::as_default_on_t<asio::ip::tcp::socket>;
    using udp_socket = default_token::as_default_on_t<asio::ip::udp::socket>;
    using steady_timer = default_token::as_default_on_t<asio::steady_timer>;

    struct peer_info_t {
        int id = 0;
        // Empty until the client registers its UDP endpoint over UDP. While empty
        // the peer is in the TCP->UDP handshake window and must not receive audio.
        std::optional<asio::ip::udp::endpoint> udp_peer;
        std::chrono::steady_clock::time_point last_tick;
    };

    using playing_peer_list_t = std::map<std::shared_ptr<tcp_socket>, std::shared_ptr<peer_info_t>>;

    enum class cmd_t : uint32_t {
        cmd_none = 0,
        cmd_get_format = 1,
        cmd_start_play = 2,
        cmd_heartbeat = 3,
    };

public:

    explicit network_manager(std::shared_ptr<audio_manager>& audio_manager);

    static std::vector<std::string> get_address_list();
    static std::string get_default_address();
private:
    static std::string select_default_address(const std::vector<std::string>& address_list);

public:
    void start_server(const std::string& host, uint16_t port, const audio_manager::capture_config& capture_config);
    void stop_server();
    void wait_server();
    bool is_running() const;

private:
    asio::awaitable<void> accept_tcp_loop(tcp_acceptor acceptor);
    asio::awaitable<void> read_loop(std::shared_ptr<tcp_socket> peer);
    asio::awaitable<void> heartbeat_loop(std::shared_ptr<tcp_socket> peer);
    asio::awaitable<void> accept_udp_loop();
    
    playing_peer_list_t::iterator close_session(std::shared_ptr<tcp_socket>& peer);
    int add_playing_peer(std::shared_ptr<tcp_socket>& peer);
    playing_peer_list_t::iterator remove_playing_peer(std::shared_ptr<tcp_socket>& peer);
    void fill_udp_peer(int id, asio::ip::udp::endpoint udp_peer);

    // Whether a peer has a usable, registered UDP endpoint (i.e. it has finished
    // the TCP->UDP handshake and is not pointing at an unspecified address).
    static bool is_udp_registered(const peer_info_t& info);
    // Snapshot of the endpoints of every peer that is a valid send target.
    std::vector<asio::ip::udp::endpoint> collect_udp_targets() const;
    // Close every playing session, other than `keep`, that is registered to the
    // same host address. Used to drop the stale session left behind by a network
    // switch / fast reconnect so audio only flows to the current endpoint.
    void evict_stale_peers(const std::shared_ptr<tcp_socket>& keep, const asio::ip::address& address);
    // Pure decision used by evict_stale_peers: given (id, registered endpoint)
    // pairs, return the ids that are stale for a new registration from `address`.
    static std::vector<int> select_stale_ids(
        const std::vector<std::pair<int, std::optional<asio::ip::udp::endpoint>>>& peers,
        int keep_id, const asio::ip::address& address);

public:
    void broadcast_audio_data(const char* data, size_t count, int block_align);
    
    std::shared_ptr<asio::io_context> _ioc;

private:
    std::shared_ptr<audio_manager> _audio_manager;
    std::thread _net_thread;
    std::unique_ptr<udp_socket> _udp_server;
    playing_peer_list_t _playing_peer_list;
    constexpr static auto _heartbeat_timeout = std::chrono::seconds(5);
};

#endif // !NETWORK_MANAGER_HPP