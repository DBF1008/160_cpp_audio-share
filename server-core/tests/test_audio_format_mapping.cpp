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

// Regression tests for the Linux/PipeWire capture format-negotiation and empty-buffer logic.
//
// These cover two defects that previously broke the Linux server:
//   1. An empty/abnormal capture buffer was skipped without being requeued, draining the
//      buffer pool and stalling the stream (the phone went silent right after connecting).
//   2. A negotiated format the client could not consume hit a default branch that called
//      exit(EXIT_FAILURE), killing the whole service. The request path also mapped s8 -> U8
//      while the parse path only knew S8, so even a valid --encoding s8 crashed.
//
// The logic now lives in pure constexpr helpers, so most checks are static_assert: a
// successful compile already proves the mapping table. main() re-runs them at runtime for
// ctest and exits non-zero on any failure.

#ifdef linux

#include <cstdint>
#include <cstdio>

#include <spa/param/audio/raw.h>

#include "linux/audio_format_mapping.hpp"

using namespace io::github::mkckr0::audio_share_app::pb;
using enc = audio_manager::encoding_t;

namespace {
constexpr char g_dummy = 0; // static storage -> &g_dummy is a constant expression
}

// --- defect 2: every user-selectable encoding negotiates a format the client accepts ------
// (encoding_s8 -> U8 -> ENCODING_PCM_8BIT is the round-trip that used to fall into exit())
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_default)) != AudioFormat_Encoding_ENCODING_INVALID);
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_f32)) == AudioFormat_Encoding_ENCODING_PCM_FLOAT);
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_s8)) == AudioFormat_Encoding_ENCODING_PCM_8BIT);
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_s16)) == AudioFormat_Encoding_ENCODING_PCM_16BIT);
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_s24)) == AudioFormat_Encoding_ENCODING_PCM_24BIT);
static_assert(encoding_from_spa_format(spa_format_from_encoding(enc::encoding_s32)) == AudioFormat_Encoding_ENCODING_PCM_32BIT);

// the exact mismatch that crashed: request builds U8, parse must accept it as 8-bit
static_assert(spa_format_from_encoding(enc::encoding_s8) == SPA_AUDIO_FORMAT_U8);
static_assert(encoding_from_spa_format(SPA_AUDIO_FORMAT_U8) == AudioFormat_Encoding_ENCODING_PCM_8BIT);

// --- defect 2: unsupported formats degrade to INVALID so the caller can stop (never exit) --
static_assert(encoding_from_spa_format(SPA_AUDIO_FORMAT_F32P) == AudioFormat_Encoding_ENCODING_INVALID);
static_assert(encoding_from_spa_format(SPA_AUDIO_FORMAT_S16_BE) == AudioFormat_Encoding_ENCODING_INVALID);
static_assert(encoding_from_spa_format(SPA_AUDIO_FORMAT_UNKNOWN) == AudioFormat_Encoding_ENCODING_INVALID);
static_assert(spa_format_from_encoding(enc::encoding_invalid) == SPA_AUDIO_FORMAT_UNKNOWN);

// --- bits per sample drives block_align ---------------------------------------------------
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_U8) == 8);
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_S16_LE) == 16);
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_S24_LE) == 24);
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_S32_LE) == 32);
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_F32_LE) == 32);
static_assert(bits_per_sample_from_spa_format(SPA_AUDIO_FORMAT_UNKNOWN) == 0);

// --- defect 1: empty-buffer policy (skip broadcast, but the caller still requeues) --------
static_assert(should_broadcast_buffer(&g_dummy, 1));
static_assert(!should_broadcast_buffer(&g_dummy, 0));
static_assert(!should_broadcast_buffer(nullptr, 1024));
static_assert(!should_broadcast_buffer(nullptr, 0));

int main()
{
    int failures = 0;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) {
            std::printf("FAIL: %s\n", msg);
            ++failures;
        }
    };

    // Runtime re-checks. The static_asserts above already enforce these at compile time;
    // running them gives ctest a pass/fail signal and exercises the real (non-constexpr)
    // buffer pointers seen by the capture callback.
    check(encoding_from_spa_format(SPA_AUDIO_FORMAT_U8) == AudioFormat_Encoding_ENCODING_PCM_8BIT,
        "U8 maps to ENCODING_PCM_8BIT (s8 request no longer crashes)");
    check(encoding_from_spa_format(SPA_AUDIO_FORMAT_F32P) == AudioFormat_Encoding_ENCODING_INVALID,
        "unsupported format maps to INVALID instead of exiting");

    char real_buf[16] = {};
    check(should_broadcast_buffer(real_buf, sizeof(real_buf)), "non-empty buffer is broadcast");
    check(!should_broadcast_buffer(real_buf, 0), "zero-size buffer is skipped (but requeued)");
    check(!should_broadcast_buffer(nullptr, sizeof(real_buf)), "null buffer is skipped (but requeued)");

    if (failures == 0) {
        std::printf("all audio-format-mapping checks passed\n");
        return 0;
    }
    std::printf("%d audio-format-mapping check(s) failed\n", failures);
    return 1;
}

#else // !linux

int main() { return 0; }

#endif // linux
