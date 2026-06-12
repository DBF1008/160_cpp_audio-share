#ifndef CONFIG_SELECTION_HPP
#define CONFIG_SELECTION_HPP

#include <vector>

// Helpers for restoring a persisted combo-box selection on the server
// configuration page (audio endpoint / encoding).
//
// They are deliberately pure and free of any MFC / registry / audio-stack
// dependency so that:
//   * the audio endpoint and the encoding selection are resolved completely
//     independently of each other (resolving one can never mutate the other),
//   * a missing saved value degrades gracefully to the default item instead of
//     crashing or rewriting an unrelated setting, and
//   * the logic stays unit-testable without instantiating the GUI.
namespace config_selection {

    // Index of the default item. Both combo boxes put their "default" entry
    // first, so falling back means selecting index 0.
    constexpr int default_index = 0;

    // True when `saved_key` is one of the currently available `option_keys`.
    template <class Key>
    bool contains(const std::vector<Key>& option_keys, const Key& saved_key)
    {
        for (const auto& key : option_keys) {
            if (key == saved_key) {
                return true;
            }
        }
        return false;
    }

    // Resolve which combo-box item to select for a persisted setting.
    //
    // `option_keys` is the ordered list of item keys currently shown in the combo
    // box (endpoint ids, or encoding enum values). `saved_key` is the value
    // previously persisted by the user. Returns the index of the matching item,
    // or `default_index` when the saved value is no longer available (device
    // unplugged, list changed, first run, ...).
    //
    // Pure: it never touches persisted state, so it can never corrupt an
    // unrelated setting. Callers must guarantee a default item exists at
    // `default_index`.
    template <class Key>
    int resolve_selected_index(const std::vector<Key>& option_keys, const Key& saved_key)
    {
        for (int index = 0; index < static_cast<int>(option_keys.size()); ++index) {
            if (option_keys[index] == saved_key) {
                return index;
            }
        }
        return default_index;
    }
}

#endif // !CONFIG_SELECTION_HPP
