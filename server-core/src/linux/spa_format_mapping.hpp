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

#ifndef SPA_FORMAT_MAPPING_HPP
#define SPA_FORMAT_MAPPING_HPP

#ifdef linux

#include <cstdint>
#include <spa/param/audio/format-utils.h>
#include "client.pb.h"

namespace detail {

using AudioFormat_Encoding = io::github::mkckr0::audio_share_app::pb::AudioFormat_Encoding;

/// Maps a SPA audio format enum value to a protobuf AudioFormat_Encoding value.
/// Also outputs the bits per sample via *bits_per_sample (0 for unsupported formats).
/// Returns ENCODING_INVALID for any format not in the supported set
/// {F32_LE, S8, S16_LE, S24_LE, S32_LE}.
AudioFormat_Encoding spa_format_to_encoding(uint32_t spa_format, int* bits_per_sample);

} // namespace detail

#endif // linux
#endif // SPA_FORMAT_MAPPING_HPP
