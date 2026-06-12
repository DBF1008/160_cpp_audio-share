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

// Regression tests for default-host selection (ip_util). These guard the bug
// where the private-range check used the network base address as a mask
// ((addr & base) == base), which misclassified many public addresses as
// private and let a wrong adapter win the default-host selection on multi-NIC
// machines (especially boxes on 172.16.0.0/12). Dependency-free so it runs in
// CTest without any networking/audio libraries.

#include "ip_util.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL [" << __LINE__ << "]: " << #cond << '\n';       \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                       \
        const std::string a_ = (actual);                                       \
        const std::string e_ = (expected);                                     \
        if (a_ != e_) {                                                        \
            std::cerr << "FAIL [" << __LINE__ << "]: " << #actual              \
                      << " == \"" << e_ << "\" but got \"" << a_ << "\"\n";    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

void test_private_true()
{
    // 10.0.0.0/8
    CHECK(ip_util::is_private_address("10.0.0.0"));
    CHECK(ip_util::is_private_address("10.1.2.3"));
    CHECK(ip_util::is_private_address("10.255.255.255"));
    // 172.16.0.0/12 (the range the bug report calls out: 172.16 - 172.31)
    CHECK(ip_util::is_private_address("172.16.0.0"));
    CHECK(ip_util::is_private_address("172.16.0.1"));
    CHECK(ip_util::is_private_address("172.20.10.2")); // iPhone USB tethering
    CHECK(ip_util::is_private_address("172.24.1.1"));
    CHECK(ip_util::is_private_address("172.31.255.255"));
    // 192.168.0.0/16
    CHECK(ip_util::is_private_address("192.168.0.0"));
    CHECK(ip_util::is_private_address("192.168.1.10"));
    CHECK(ip_util::is_private_address("192.168.255.255"));
}

void test_private_false_boundaries()
{
    // Just outside each private range.
    CHECK(!ip_util::is_private_address("9.255.255.255"));
    CHECK(!ip_util::is_private_address("11.0.0.0"));
    CHECK(!ip_util::is_private_address("172.15.255.255")); // below 172.16/12
    CHECK(!ip_util::is_private_address("172.32.0.0"));     // above 172.16/12
    CHECK(!ip_util::is_private_address("192.167.255.255"));
    CHECK(!ip_util::is_private_address("192.169.0.0"));
}

void test_private_false_old_false_positives()
{
    // Every address below was reported as "private" by the buggy
    // (addr & base) == base check. They are all public and must be rejected.
    CHECK(!ip_util::is_private_address("11.0.0.1"));    // matched 10.0.0.0 base
    CHECK(!ip_util::is_private_address("14.1.2.3"));    // matched 10.0.0.0 base
    CHECK(!ip_util::is_private_address("138.68.1.1"));  // matched 10.0.0.0 base
    CHECK(!ip_util::is_private_address("172.48.0.1"));  // matched 172.16.0.0 base
    CHECK(!ip_util::is_private_address("173.16.0.1"));  // matched 172.16.0.0 base
    CHECK(!ip_util::is_private_address("192.169.0.1")); // matched 192.168.0.0 base
    CHECK(!ip_util::is_private_address("192.170.0.1")); // matched 192.168.0.0 base
    CHECK(!ip_util::is_private_address("200.184.1.1")); // matched 192.168.0.0 base
}

void test_private_false_public_and_special()
{
    CHECK(!ip_util::is_private_address("8.8.8.8"));
    CHECK(!ip_util::is_private_address("1.1.1.1"));
    CHECK(!ip_util::is_private_address("203.0.113.5"));
    CHECK(!ip_util::is_private_address("169.254.1.1")); // link-local / APIPA
}

void test_parse_rejects_malformed()
{
    // Malformed input must not be treated as private (parse fails -> false).
    CHECK(!ip_util::is_private_address(""));
    CHECK(!ip_util::is_private_address("garbage"));
    CHECK(!ip_util::is_private_address("10.0.0"));      // too few octets
    CHECK(!ip_util::is_private_address("10.0.0.0.0"));  // too many octets
    CHECK(!ip_util::is_private_address("256.0.0.1"));   // octet out of range
    CHECK(!ip_util::is_private_address("10.0.0."));     // trailing dot
    CHECK(!ip_util::is_private_address("10.0.0.5 "));   // trailing space
    CHECK(!ip_util::is_private_address("10.0.0.-1"));   // negative octet

    // parse_ipv4 returns host-order integers for valid input.
    CHECK(ip_util::parse_ipv4("0.0.0.0") == std::optional<std::uint32_t>(0x00000000u));
    CHECK(ip_util::parse_ipv4("192.168.1.10") == std::optional<std::uint32_t>(0xC0A8010Au));
    CHECK(ip_util::parse_ipv4("255.255.255.255") == std::optional<std::uint32_t>(0xFFFFFFFFu));
    CHECK(ip_util::parse_ipv4("nope") == std::nullopt);
}

void test_select_default_address()
{
    using vec = std::vector<std::string>;

    // Empty list -> empty result.
    CHECK_EQ(ip_util::select_default_address(vec{}), "");

    // Single private address of each common class.
    CHECK_EQ(ip_util::select_default_address(vec{ "192.168.1.10" }), "192.168.1.10");
    CHECK_EQ(ip_util::select_default_address(vec{ "10.0.0.5" }), "10.0.0.5");
    CHECK_EQ(ip_util::select_default_address(vec{ "172.20.10.2" }), "172.20.10.2");

    // First private address wins; display order is preserved.
    CHECK_EQ(ip_util::select_default_address(vec{ "10.0.0.5", "192.168.1.5" }), "10.0.0.5");

    // A leading public address must NOT pre-empt a real private LAN address.
    CHECK_EQ(ip_util::select_default_address(vec{ "1.1.1.1", "10.10.10.10" }), "10.10.10.10");

    // Core regression: the exact multi-NIC failure from the bug report. The old
    // code classified 138.68.1.1 as private and returned it; the phone then
    // could not connect. The real LAN address must be chosen instead.
    CHECK_EQ(ip_util::select_default_address(vec{ "138.68.1.1", "192.168.1.10" }), "192.168.1.10");

    // 172.16/12 machine that also exposes a 173.x adapter (old false positive):
    // must skip the public 173.x and pick the real 172.x LAN.
    CHECK_EQ(ip_util::select_default_address(vec{ "173.16.0.1", "172.20.0.5" }), "172.20.0.5");

    // No private address at all -> fall back to the first (unchanged behavior).
    CHECK_EQ(ip_util::select_default_address(vec{ "203.0.113.5", "198.51.100.7" }), "203.0.113.5");
}

} // namespace

int main()
{
    test_private_true();
    test_private_false_boundaries();
    test_private_false_old_false_positives();
    test_private_false_public_and_special();
    test_parse_rejects_malformed();
    test_select_default_address();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "All ip_util regression tests passed\n";
    return 0;
}
