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

// Test-only fake audio backend.
//
// This header is included by audio_manager.hpp ONLY when AUDIO_SHARE_FAKE_AUDIO
// is defined, which the production Windows/Linux builds never do. It exists so
// the platform-independent start/stop/rollback lifecycle in network_manager and
// audio_manager can be compiled and exercised on machines without a real audio
// backend (CI, developer workstations). The behaviour of the fake backend is
// driven by capture_config::endpoint_id; see fake_audio_manager_impl.cpp.

#ifndef FAKE_AUDIO_MANAGER_IMPL_HPP
#define FAKE_AUDIO_MANAGER_IMPL_HPP

#if defined(AUDIO_SHARE_FAKE_AUDIO)

namespace detail {

class audio_manager_impl {
public:
    audio_manager_impl();
    ~audio_manager_impl();
};

} // namespace detail

#endif // AUDIO_SHARE_FAKE_AUDIO
#endif // !FAKE_AUDIO_MANAGER_IMPL_HPP
