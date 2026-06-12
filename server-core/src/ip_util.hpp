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

#ifndef IP_UTIL_HPP
#define IP_UTIL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Pure, dependency-free helpers for picking which local IPv4 address should be
// offered as the default "Host". Kept separate from network_manager so the
// selection logic can be unit-tested without any networking/audio dependencies.
// Both the command line server (implicit `-b` bind) and the MFC GUI default
// host go through select_default_address().
namespace ip_util {

// Parse a dotted-quad IPv4 string (e.g. "192.168.1.10") into a 32-bit value in
// host byte order. Returns std::nullopt for anything that is not a well-formed
// IPv4 address (wrong octet count, out-of-range octet, trailing garbage, ...).
std::optional<std::uint32_t> parse_ipv4(const std::string& address);

// Whether `address` is in one of the RFC 1918 private IPv4 ranges:
//   10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
// Non-IPv4 / malformed input is treated as not private.
bool is_private_address(const std::string& address);

// Pick the default address from an ordered list (as produced by
// network_manager::get_address_list()). Prefers the first address that lives in
// a private LAN range; if none qualifies, falls back to the first address.
// Returns "" for an empty list. The input order is preserved (the list itself
// is the display order and must not be reordered).
std::string select_default_address(const std::vector<std::string>& address_list);

} // namespace ip_util

#endif // !IP_UTIL_HPP
