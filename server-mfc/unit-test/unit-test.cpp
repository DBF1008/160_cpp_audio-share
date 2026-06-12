#include "pch.h"
#include "CppUnitTest.h"
#include "../audio-share-server/util.hpp"
#include "../../server-core/src/ip_util.hpp"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace unittest
{
	TEST_CLASS(test_split_string)
	{
	public:
		
        TEST_METHOD(split_string_0) {
            Assert::IsTrue(std::vector<std::string>{"1", "2", "3"} == util::split_string("1.2.3", '.'));
        }

        TEST_METHOD(split_string_1) {
            Assert::IsTrue(std::vector<std::string>{"2", "3"} == util::split_string(".2.3", '.'));
        }

        TEST_METHOD(split_string_2) {
            Assert::IsTrue(std::vector<std::string>{} == util::split_string("", '.'));
        }

        TEST_METHOD(split_string_3) {
            Assert::IsTrue(std::vector<std::string>{} == util::split_string(".", '.'));
        }
	};

    TEST_CLASS(test_is_newer_version)
    {
    public:
        TEST_METHOD(is_newer_version_arg0) {
            try
            {
                util::is_newer_version("3.2.2", "v0.0.1");
            }
            catch (const std::exception&)
            {
                return;
            }

            Assert::Fail();
        }

        TEST_METHOD(is_newer_version_arg1) {
            try
            {
                util::is_newer_version("v3.2.", "v0.0.");
            }
            catch (const std::exception&)
            {
                return;
            }

            Assert::Fail();
        }

        TEST_METHOD(is_newer_version_version0) {
            Assert::IsTrue(util::is_newer_version("v0.0.17", "v0.0.9"));
        }

        TEST_METHOD(is_newer_version_version1) {
            Assert::IsTrue(util::is_newer_version("v0.1.0", "v0.0.17"));
        }

        TEST_METHOD(is_newer_version_version11) {
            Assert::IsFalse(util::is_newer_version("v0.0.17", "v0.1.0"));
        }

        TEST_METHOD(is_newer_version_version12) {
            Assert::IsFalse(util::is_newer_version("v0.1.0", "v0.1.0"));
        }

        TEST_METHOD(is_newer_version_version13) {
            Assert::IsTrue(util::is_newer_version("v0.2.0", "v0.1.0"));
        }

        TEST_METHOD(is_newer_version_version2) {
            Assert::IsTrue(util::is_newer_version("v0.17.0", "v0.9.17"));
        }

        TEST_METHOD(is_newer_version_version3) {
            Assert::IsTrue(util::is_newer_version("v12.17.0", "v1.0.0"));
        }

        TEST_METHOD(is_newer_version_version4) {
            Assert::IsFalse(util::is_newer_version("v12.17.0", "v12.17.0"));
        }
    };

    // Regression tests for the GUI default host selection. The MFC server tab
    // populates its Host combo with network_manager::get_address_list() and
    // pre-selects network_manager::get_default_address(), which delegates to
    // ip_util::select_default_address(). The old private-range check let public
    // addresses win on multi-NIC machines (especially boxes on 172.16/12), so
    // the phone could not connect with the pre-filled default.
    TEST_CLASS(test_select_default_address)
    {
    public:
        TEST_METHOD(is_private_common_ranges) {
            Assert::IsTrue(ip_util::is_private_address("10.1.2.3"));
            Assert::IsTrue(ip_util::is_private_address("172.16.0.1"));
            Assert::IsTrue(ip_util::is_private_address("172.20.10.2"));
            Assert::IsTrue(ip_util::is_private_address("172.31.255.255"));
            Assert::IsTrue(ip_util::is_private_address("192.168.1.10"));
        }

        TEST_METHOD(is_private_rejects_old_false_positives) {
            // All public, but wrongly treated as private by the old check.
            Assert::IsFalse(ip_util::is_private_address("11.0.0.1"));
            Assert::IsFalse(ip_util::is_private_address("138.68.1.1"));
            Assert::IsFalse(ip_util::is_private_address("172.15.255.255"));
            Assert::IsFalse(ip_util::is_private_address("172.32.0.1"));
            Assert::IsFalse(ip_util::is_private_address("172.48.0.1"));
            Assert::IsFalse(ip_util::is_private_address("173.16.0.1"));
            Assert::IsFalse(ip_util::is_private_address("192.169.0.1"));
            Assert::IsFalse(ip_util::is_private_address("200.184.1.1"));
            Assert::IsFalse(ip_util::is_private_address("8.8.8.8"));
        }

        TEST_METHOD(select_prefers_private_over_leading_public) {
            std::vector<std::string> list{ "138.68.1.1", "192.168.1.10" };
            Assert::IsTrue(std::string("192.168.1.10") == ip_util::select_default_address(list));
        }

        TEST_METHOD(select_picks_real_lan_on_172_machine) {
            std::vector<std::string> list{ "173.16.0.1", "172.20.0.5" };
            Assert::IsTrue(std::string("172.20.0.5") == ip_util::select_default_address(list));
        }

        TEST_METHOD(select_first_private_preserves_order) {
            std::vector<std::string> list{ "10.0.0.5", "192.168.1.5" };
            Assert::IsTrue(std::string("10.0.0.5") == ip_util::select_default_address(list));
        }

        TEST_METHOD(select_falls_back_to_first_when_no_private) {
            std::vector<std::string> list{ "203.0.113.5", "198.51.100.7" };
            Assert::IsTrue(std::string("203.0.113.5") == ip_util::select_default_address(list));
        }

        TEST_METHOD(select_empty_list_returns_empty) {
            std::vector<std::string> list{};
            Assert::IsTrue(ip_util::select_default_address(list).empty());
        }
    };
}
