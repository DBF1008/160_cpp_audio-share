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

// Control/observation surface for the test-only fake audio backend
// (fake_audio_manager_impl.cpp), shared with the lifecycle tests.

#ifndef FAKE_AUDIO_CONTROL_HPP
#define FAKE_AUDIO_CONTROL_HPP

namespace fake_audio {

// Number of fake recordings currently inside their (simulated) capture loop.
// A clean stop/rollback must drive this back to its pre-start value.
int active_count();

// capture_config::endpoint_id sentinel: makes the fake recording return
// immediately without entering the capture loop, simulating an audio-init
// failure that exits the recording thread cleanly (as the real backends'
// early-return error paths do).
inline constexpr const char* kFailImmediately = "__audio_fail__";

// An endpoint id the fake backend treats as a healthy device.
inline constexpr const char* kHealthyEndpoint = "fake-endpoint-1";

} // namespace fake_audio

#endif // !FAKE_AUDIO_CONTROL_HPP
