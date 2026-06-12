#include "pch.h"
#include "CppUnitTest.h"
#include "../audio-share-server/util.hpp"
#include "../audio-share-server/config_selection.hpp"

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

    // Regression tests for the server configuration page (CServerTabPanel).
    //
    // These pin the contract of the endpoint/encoding selection-restore logic
    // that previously cross-contaminated each other and crashed on first open:
    //   * a saved value that is still available is re-selected,
    //   * a missing value (device unplugged, list changed, first run) falls back
    //     to the default item instead of throwing or rewriting another setting,
    //   * resolving the encoding can never disturb the endpoint and vice versa.
    //
    // The encoding values mirror audio_manager::encoding_t; they are duplicated as
    // plain ints so the test stays free of the MFC / audio-stack headers.
    TEST_CLASS(test_config_selection)
    {
        static constexpr int enc_default = 0;
        static constexpr int enc_invalid = 1;
        static constexpr int enc_f32 = 2;
        static constexpr int enc_s8 = 3;
        static constexpr int enc_s16 = 4;
        static constexpr int enc_s24 = 5;
        static constexpr int enc_s32 = 6;

    public:
        // ---- audio endpoint selection ----

        TEST_METHOD(endpoint_saved_present_is_selected) {
            std::vector<std::wstring> keys = { L"default", L"id-a", L"id-b" };
            Assert::AreEqual(2, config_selection::resolve_selected_index<std::wstring>(keys, L"id-b"));
            Assert::IsTrue(config_selection::contains<std::wstring>(keys, L"id-b"));
        }

        TEST_METHOD(endpoint_saved_default_selects_first) {
            std::vector<std::wstring> keys = { L"default", L"id-a", L"id-b" };
            Assert::AreEqual(0, config_selection::resolve_selected_index<std::wstring>(keys, L"default"));
            Assert::IsTrue(config_selection::contains<std::wstring>(keys, L"default"));
        }

        // Device unplugged / list changed: must fall back to the default item.
        TEST_METHOD(endpoint_missing_device_falls_back_to_default) {
            std::vector<std::wstring> keys = { L"default", L"id-a" };
            Assert::AreEqual(config_selection::default_index,
                config_selection::resolve_selected_index<std::wstring>(keys, L"id-unplugged"));
            Assert::IsFalse(config_selection::contains<std::wstring>(keys, L"id-unplugged"));
        }

        TEST_METHOD(endpoint_only_default_available) {
            std::vector<std::wstring> keys = { L"default" };
            Assert::AreEqual(0, config_selection::resolve_selected_index<std::wstring>(keys, L"any-id"));
            Assert::AreEqual(0, config_selection::resolve_selected_index<std::wstring>(keys, L"default"));
        }

        TEST_METHOD(endpoint_empty_list_returns_default_index) {
            std::vector<std::wstring> keys;
            Assert::AreEqual(config_selection::default_index,
                config_selection::resolve_selected_index<std::wstring>(keys, L"any-id"));
        }

        // ---- encoding selection ----

        TEST_METHOD(encoding_saved_present_is_selected) {
            std::vector<int> keys = { enc_default, enc_f32, enc_s8, enc_s16, enc_s24, enc_s32 };
            Assert::AreEqual(3, config_selection::resolve_selected_index<int>(keys, enc_s16));
        }

        TEST_METHOD(encoding_default_saved_selects_first) {
            std::vector<int> keys = { enc_default, enc_f32, enc_s8, enc_s16, enc_s24, enc_s32 };
            Assert::AreEqual(0, config_selection::resolve_selected_index<int>(keys, enc_default));
            Assert::IsTrue(config_selection::contains<int>(keys, enc_default));
        }

        // Unknown / no-longer-offered encoding must fall back to the default item.
        TEST_METHOD(encoding_unknown_value_falls_back_to_default) {
            std::vector<int> keys = { enc_default, enc_f32, enc_s8, enc_s16, enc_s24, enc_s32 };
            Assert::AreEqual(config_selection::default_index,
                config_selection::resolve_selected_index<int>(keys, enc_invalid));
            Assert::AreEqual(config_selection::default_index,
                config_selection::resolve_selected_index<int>(keys, 999));
            Assert::IsFalse(config_selection::contains<int>(keys, enc_invalid));
        }

        // ---- regression: endpoint and encoding are independent ----
        // The original defect resolved the encoding fallback by writing into the
        // *endpoint* setting, corrupting the saved device. Resolution is now a
        // pure read, so resolving one selection can never affect the other.
        TEST_METHOD(encoding_fallback_does_not_disturb_endpoint_selection) {
            std::vector<std::wstring> endpoint_keys = { L"default", L"id-a", L"id-b" };
            const std::wstring saved_endpoint = L"id-b";

            int endpoint_before = config_selection::resolve_selected_index<std::wstring>(endpoint_keys, saved_endpoint);

            // An encoding whose saved value is missing -> falls back to default.
            std::vector<int> encoding_keys = { enc_default, enc_f32, enc_s16 };
            int encoding_sel = config_selection::resolve_selected_index<int>(encoding_keys, enc_s32);
            Assert::AreEqual(config_selection::default_index, encoding_sel);

            int endpoint_after = config_selection::resolve_selected_index<std::wstring>(endpoint_keys, saved_endpoint);

            Assert::AreEqual(2, endpoint_before);
            Assert::AreEqual(endpoint_before, endpoint_after);  // endpoint selection unchanged
            Assert::IsTrue(saved_endpoint == L"id-b");           // saved endpoint not rewritten
            Assert::IsTrue(endpoint_keys.size() == size_t(3));   // endpoint list not mutated
        }
    };
}
