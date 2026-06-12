#ifndef UTIL_HPP
#define UTIL_HPP

#include <string>
#include <vector>

namespace util
{
    bool is_newer_version(const std::string& lhs, const std::string& rhs);

    // empty substring will be removed
    std::vector<std::string> split_string(const std::string& src, char delimiter);

    struct update_check_result {
        bool update_available = false;
        std::string tag_name;   // e.g. "v1.2.3"
        std::string html_url;   // release page (only filled when update_available)
    };

    // Parse the GitHub "releases/latest" JSON body and compare its tag against
    // current_version (e.g. "v1.0.0"). Throws std::exception on malformed input
    // or a missing/ill-typed "tag_name" field (same contract as is_newer_version).
    update_check_result evaluate_update(const std::string& latest_release_json,
                                        const std::string& current_version);
}

#endif // !UTIL_HPP
