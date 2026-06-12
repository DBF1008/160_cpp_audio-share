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

#include "ip_util.hpp"

namespace ip_util {

std::optional<std::uint32_t> parse_ipv4(const std::string& address)
{
    std::uint32_t result = 0;
    const std::size_t n = address.size();
    std::size_t i = 0;

    for (int octet = 0; octet < 4; ++octet) {
        // Each octet must start with at least one digit.
        if (i >= n || address[i] < '0' || address[i] > '9') {
            return std::nullopt;
        }

        int value = 0;
        int digits = 0;
        while (i < n && address[i] >= '0' && address[i] <= '9') {
            value = value * 10 + (address[i] - '0');
            ++digits;
            if (digits > 3 || value > 255) {
                return std::nullopt;
            }
            ++i;
        }

        result = (result << 8) | static_cast<std::uint32_t>(value);

        if (octet < 3) {
            // Octets 0..2 must be followed by a '.' separator.
            if (i >= n || address[i] != '.') {
                return std::nullopt;
            }
            ++i;
        }
    }

    // Reject any trailing characters (e.g. "1.2.3.4.5" or "1.2.3.4 ").
    if (i != n) {
        return std::nullopt;
    }

    return result;
}

bool is_private_address(const std::string& address)
{
    // RFC 1918 private IPv4 ranges expressed as (network base, network mask).
    // An address is private when (addr & mask) == base. The previous
    // implementation compared (addr & base) == base, treating the base address
    // itself as a mask, which both missed parts of 172.16.0.0/12 conceptually
    // and (more importantly) misclassified many public addresses as private.
    struct range_t {
        std::uint32_t base;
        std::uint32_t mask;
    };
    constexpr range_t private_ranges[] = {
        { 0x0A000000u, 0xFF000000u }, // 10.0.0.0/8
        { 0xAC100000u, 0xFFF00000u }, // 172.16.0.0/12 (172.16.0.0 - 172.31.255.255)
        { 0xC0A80000u, 0xFFFF0000u }, // 192.168.0.0/16
    };

    auto addr = parse_ipv4(address);
    if (!addr) {
        return false;
    }

    for (auto&& range : private_ranges) {
        if ((*addr & range.mask) == range.base) {
            return true;
        }
    }

    return false;
}

std::string select_default_address(const std::vector<std::string>& address_list)
{
    if (address_list.empty()) {
        return {};
    }

    // Prefer the first address that is in a private LAN range. A phone on the
    // same LAN can reach this; public / link-local addresses generally can't.
    for (auto&& address : address_list) {
        if (is_private_address(address)) {
            return address;
        }
    }

    // No private address found: keep the previous fallback of the first address.
    return address_list.front();
}

} // namespace ip_util
