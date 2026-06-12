#pragma once

#include <string>

#define WM_APP_NOTIFYICON (WM_APP + 1)
// Posted by the update-check worker thread to the main dialog so that all UI work
// (balloon notification / message box) happens on the UI thread. lParam owns a
// heap-allocated UpdateCheckMessage; the handler takes ownership and deletes it.
#define WM_APP_CHECK_UPDATE_RESULT (WM_APP + 2)

#define TIMER_ID_CHECK_UPDATE 1

// Result of one update check, marshaled from the worker thread to the UI thread.
struct UpdateCheckMessage {
    bool ok = false;               // true: check completed; false: network/parse error
    bool prompt = false;           // manual check -> show "no update" / error message box
    bool update_available = false; // valid when ok
    std::wstring update_link;      // release html_url (when update_available)
    std::wstring tag_name;         // release tag      (when update_available)
    std::wstring error_text;       // error message    (when !ok && prompt)
};
