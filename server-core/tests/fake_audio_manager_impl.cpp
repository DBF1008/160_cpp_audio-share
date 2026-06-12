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
// Provides the platform-specific symbols normally supplied by
// src/{win32,linux}/audio_manager_impl.cpp, but without touching real audio
// hardware, so the platform-independent start/stop/rollback lifecycle can be
// exercised on any machine. Compiled only into the server-core test target,
// which defines AUDIO_SHARE_FAKE_AUDIO.

#include "audio_manager.hpp"
#include "fake_audio_control.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace io::github::mkckr0::audio_share_app::pb;

namespace {
std::atomic<int> g_active_recordings { 0 };
} // namespace

namespace fake_audio {
int active_count()
{
    return g_active_recordings.load(std::memory_order_relaxed);
}
} // namespace fake_audio

namespace detail {

audio_manager_impl::audio_manager_impl() = default;
audio_manager_impl::~audio_manager_impl() = default;

} // namespace detail

void audio_manager::do_loopback_recording(std::shared_ptr<network_manager> /*network_manager*/, const capture_config& config)
{
    // Simulate an audio-init failure that returns cleanly without ever entering
    // the capture loop (mirrors the real backends' early-return error paths).
    if (config.endpoint_id == fake_audio::kFailImmediately) {
        return;
    }

    // Publish a plausible capture format and run a simulated capture loop until
    // stopped, polling the stop flag the same way the real backends do.
    _format->set_encoding(AudioFormat_Encoding_ENCODING_PCM_16BIT);
    _format->set_channels(2);
    _format->set_sample_rate(48000);

    g_active_recordings.fetch_add(1, std::memory_order_relaxed);
    struct active_guard {
        ~active_guard() { g_active_recordings.fetch_sub(1, std::memory_order_relaxed); }
    } guard;

    while (!_stopped) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

audio_manager::endpoint_list_t audio_manager::get_endpoint_list()
{
    return {
        { "fake-endpoint-1", "Fake Endpoint 1" },
        { "fake-endpoint-2", "Fake Endpoint 2" },
    };
}

std::string audio_manager::get_default_endpoint()
{
    return "fake-endpoint-1";
}
