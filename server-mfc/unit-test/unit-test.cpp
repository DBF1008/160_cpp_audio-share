#include "pch.h"
#include "CppUnitTest.h"
#include "../audio-share-server/util.hpp"
#include "../audio-share-server/AppMsg.h"

#include <atomic>
#include <Windows.h>

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

    TEST_CLASS(test_update_check_safety)
    {
    public:
        TEST_METHOD(UpdateResultData_carries_new_version_info) {
            UpdateResultData data{};
            data.code = UpdateResultCode::NewVersion;
            data.bPromptUser = true;
            wcscpy_s(data.szTitle, L"New Version Available");
            wcscpy_s(data.szInfo, L"v1.0.0");
            wcscpy_s(data.szUrl, L"https://github.com/example/releases/tag/v1.0.0");
            Assert::AreEqual(static_cast<int>(UpdateResultCode::NewVersion), static_cast<int>(data.code));
            Assert::IsTrue(data.bPromptUser);
            Assert::AreEqual(L"v1.0.0", data.szInfo);
            Assert::AreEqual(L"https://github.com/example/releases/tag/v1.0.0", data.szUrl);
        }

        TEST_METHOD(UpdateResultData_carries_no_update_info) {
            UpdateResultData data{};
            data.code = UpdateResultCode::NoUpdate;
            data.bPromptUser = true;
            wcscpy_s(data.szTitle, L"No updates available");
            Assert::AreEqual(static_cast<int>(UpdateResultCode::NoUpdate), static_cast<int>(data.code));
            Assert::IsTrue(data.bPromptUser);
        }

        TEST_METHOD(UpdateResultData_carries_error_info) {
            UpdateResultData data{};
            data.code = UpdateResultCode::NetworkError;
            data.bPromptUser = false;
            wcscpy_s(data.szTitle, L"Connection timed out");
            Assert::AreEqual(static_cast<int>(UpdateResultCode::NetworkError), static_cast<int>(data.code));
            Assert::IsFalse(data.bPromptUser);
        }

        TEST_METHOD(UpdateResultData_heap_alloc_and_delete) {
            auto* pData = new UpdateResultData{};
            pData->code = UpdateResultCode::NewVersion;
            pData->bPromptUser = true;
            wcscpy_s(pData->szTitle, L"Test");
            Assert::IsNotNull(pData);
            Assert::AreEqual(static_cast<int>(UpdateResultCode::NewVersion), static_cast<int>(pData->code));
            delete pData; // Verify no crash on cleanup
        }

        TEST_METHOD(ShutdownFlag_atomic_toggle) {
            std::atomic<bool> flag{false};
            Assert::IsFalse(flag.load(std::memory_order_acquire));
            flag.store(true, std::memory_order_release);
            Assert::IsTrue(flag.load(std::memory_order_acquire));
            flag.store(false, std::memory_order_release);
            Assert::IsFalse(flag.load(std::memory_order_acquire));
        }

        TEST_METHOD(UpdateInProgress_atomic_guard) {
            std::atomic<bool> inProgress{false};
            // First CAS should succeed — no check running
            bool expected = false;
            bool swapped = inProgress.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
            Assert::IsTrue(swapped);
            Assert::IsTrue(inProgress.load());
            // Second CAS should fail — check already running
            expected = false;
            swapped = inProgress.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
            Assert::IsFalse(swapped);
            // Release the guard
            inProgress.store(false, std::memory_order_release);
            Assert::IsFalse(inProgress.load());
        }

        TEST_METHOD(PostMessageW_to_null_hwnd_returns_false) {
            // PostMessageW to a null/invalid HWND must not crash and returns FALSE
            BOOL result = ::PostMessageW(nullptr, WM_APP_UPDATE_RESULT, 0, (LPARAM)nullptr);
            Assert::IsFalse(result);
        }

        TEST_METHOD(PostMessageW_to_invalid_hwnd_returns_false) {
            // PostMessageW to a destroyed/invalid HWND must not crash
            HWND hInvalid = reinterpret_cast<HWND>(0xDEAD);
            BOOL result = ::PostMessageW(hInvalid, WM_APP_UPDATE_RESULT, 0, (LPARAM)nullptr);
            Assert::IsFalse(result);
        }

        TEST_METHOD(ShutdownEvent_create_and_signal) {
            HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Assert::IsNotNull(hEvent);
            // Initially non-signaled
            DWORD waitResult = WaitForSingleObject(hEvent, 0);
            Assert::AreEqual(static_cast<DWORD>(WAIT_TIMEOUT), waitResult);
            // Signal it
            SetEvent(hEvent);
            waitResult = WaitForSingleObject(hEvent, 0);
            Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), waitResult);
            CloseHandle(hEvent);
        }
    };
}
