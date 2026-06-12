#include "pch.h"
#include "CppUnitTest.h"
#include "../audio-share-server/util.hpp"

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

    TEST_CLASS(test_evaluate_update)
    {
    public:
        // A newer published release than the running version -> notify.
        TEST_METHOD(evaluate_update_newer) {
            auto r = util::evaluate_update(
                R"({"tag_name":"v1.2.0","html_url":"https://example.com/releases/v1.2.0"})",
                "v1.0.0");
            Assert::IsTrue(r.update_available);
            Assert::IsTrue(std::string("v1.2.0") == r.tag_name);
            Assert::IsTrue(std::string("https://example.com/releases/v1.2.0") == r.html_url);
        }

        // Same version -> no update.
        TEST_METHOD(evaluate_update_same) {
            auto r = util::evaluate_update(
                R"({"tag_name":"v1.0.0","html_url":"https://example.com/releases/v1.0.0"})",
                "v1.0.0");
            Assert::IsFalse(r.update_available);
        }

        // Published release older than the running version -> no update.
        TEST_METHOD(evaluate_update_older) {
            auto r = util::evaluate_update(
                R"({"tag_name":"v0.9.0","html_url":"https://example.com/releases/v0.9.0"})",
                "v1.0.0");
            Assert::IsFalse(r.update_available);
        }

        // Malformed response -> throws (worker treats this as an error).
        TEST_METHOD(evaluate_update_malformed_json) {
            try {
                util::evaluate_update("this is not json", "v1.0.0");
            }
            catch (const std::exception&) {
                return;
            }
            Assert::Fail();
        }

        // Missing "tag_name" field -> throws.
        TEST_METHOD(evaluate_update_missing_tag_name) {
            try {
                util::evaluate_update(R"({"html_url":"https://example.com"})", "v1.0.0");
            }
            catch (const std::exception&) {
                return;
            }
            Assert::Fail();
        }
    };
}
