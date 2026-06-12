#pragma once

#define WM_APP_NOTIFYICON (WM_APP + 1)
#define WM_APP_UPDATE_RESULT (WM_APP + 2)

#define TIMER_ID_CHECK_UPDATE 1

enum class UpdateResultCode : int {
    NewVersion,      // A newer version was found
    NoUpdate,        // No update available
    NetworkError,    // Network or parse error
};

struct UpdateResultData {
    UpdateResultCode code;
    bool bPromptUser;             // Whether to show a prompt (true for manual checks)
    wchar_t szTitle[256];         // Notification title or error message
    wchar_t szInfo[512];          // Tag name or error detail
    wchar_t szUrl[1024];          // Download URL (for NewVersion)
};