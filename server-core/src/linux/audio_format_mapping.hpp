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

#ifndef AUDIO_FORMAT_MAPPING_HPP
#define AUDIO_FORMAT_MAPPING_HPP

#ifdef linux

// Pure, daemon-free helpers for the Linux/PipeWire capture path. They are extracted from the
// stream callbacks in audio_manager_impl.cpp so the format-negotiation and empty-buffer logic
// can be unit-tested without a running PipeWire daemon. Keep them free of side effects: an
// unsupported format must map to ENCODING_INVALID so the caller can stop gracefully -- it must
// never terminate the process.

#include <cstdint>

#include <spa/param/audio/raw.h>

#include "audio_manager.hpp"
#include "client.pb.h"

namespace detail {

// Map a user-requested capture encoding to the SPA format we ask PipeWire to negotiate.
// Note: 8-bit PCM on the Android side (ENCODING_PCM_8BIT) is unsigned, so encoding_s8 -> U8.
constexpr enum spa_audio_format spa_format_from_encoding(audio_manager::encoding_t encoding)
{
    switch (encoding) {
    case audio_manager::encoding_t::encoding_default:
    case audio_manager::encoding_t::encoding_f32:
        return SPA_AUDIO_FORMAT_F32_LE;
    case audio_manager::encoding_t::encoding_s8:
        return SPA_AUDIO_FORMAT_U8;
    case audio_manager::encoding_t::encoding_s16:
        return SPA_AUDIO_FORMAT_S16_LE;
    case audio_manager::encoding_t::encoding_s24:
        return SPA_AUDIO_FORMAT_S24_LE;
    case audio_manager::encoding_t::encoding_s32:
        return SPA_AUDIO_FORMAT_S32_LE;
    default:
        return SPA_AUDIO_FORMAT_UNKNOWN;
    }
}

// Map a negotiated SPA format to the Android-side AudioFormat encoding. Returns
// ENCODING_INVALID for any format the client cannot consume; callers must handle that by
// logging and stopping the stream, NEVER by exiting the process. U8 (what we request for
// 8-bit) maps to ENCODING_PCM_8BIT -- this is the case that previously fell through to the
// crashing default branch.
constexpr io::github::mkckr0::audio_share_app::pb::AudioFormat_Encoding
encoding_from_spa_format(std::uint32_t spa_format)
{
    using namespace io::github::mkckr0::audio_share_app::pb;
    switch (spa_format) {
    case SPA_AUDIO_FORMAT_F32_LE:
        return AudioFormat_Encoding_ENCODING_PCM_FLOAT;
    case SPA_AUDIO_FORMAT_U8:
    case SPA_AUDIO_FORMAT_S8:
        return AudioFormat_Encoding_ENCODING_PCM_8BIT;
    case SPA_AUDIO_FORMAT_S16_LE:
        return AudioFormat_Encoding_ENCODING_PCM_16BIT;
    case SPA_AUDIO_FORMAT_S24_LE:
        return AudioFormat_Encoding_ENCODING_PCM_24BIT;
    case SPA_AUDIO_FORMAT_S32_LE:
        return AudioFormat_Encoding_ENCODING_PCM_32BIT;
    default:
        return AudioFormat_Encoding_ENCODING_INVALID;
    }
}

// Bits per sample for a negotiated SPA format (0 when unknown). Used to compute block_align.
constexpr int bits_per_sample_from_spa_format(std::uint32_t spa_format)
{
    switch (spa_format) {
    case SPA_AUDIO_FORMAT_S8:
    case SPA_AUDIO_FORMAT_U8:
        return 8;
    case SPA_AUDIO_FORMAT_S16_LE:
    case SPA_AUDIO_FORMAT_S16_BE:
    case SPA_AUDIO_FORMAT_U16_LE:
    case SPA_AUDIO_FORMAT_U16_BE:
        return 16;
    case SPA_AUDIO_FORMAT_S24_LE:
    case SPA_AUDIO_FORMAT_S24_BE:
    case SPA_AUDIO_FORMAT_U24_LE:
    case SPA_AUDIO_FORMAT_U24_BE:
        return 24;
    case SPA_AUDIO_FORMAT_S32_LE:
    case SPA_AUDIO_FORMAT_S32_BE:
    case SPA_AUDIO_FORMAT_U32_LE:
    case SPA_AUDIO_FORMAT_U32_BE:
    case SPA_AUDIO_FORMAT_F32_LE:
    case SPA_AUDIO_FORMAT_F32_BE:
    case SPA_AUDIO_FORMAT_F32P:
        return 32;
    default:
        return 0;
    }
}

// An empty/abnormal capture buffer (null data or zero size) must be skipped for broadcast,
// but the caller must still requeue it -- otherwise the buffer pool drains and the stream
// stalls, silencing capture permanently.
constexpr bool should_broadcast_buffer(const void* data, std::uint32_t size)
{
    return data != nullptr && size > 0;
}

} // namespace detail

#endif // linux
#endif // !AUDIO_FORMAT_MAPPING_HPP
