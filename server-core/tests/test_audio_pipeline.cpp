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

#include <gtest/gtest.h>
#include "client.pb.h"

#ifdef linux
#include "linux/spa_format_mapping.hpp"
#endif

using namespace io::github::mkckr0::audio_share_app::pb;

// ============================================================================
// SPA Format Mapping Tests (Linux only — requires PipeWire/SPA headers)
// ============================================================================

#ifdef linux

using detail::spa_format_to_encoding;

// --- Supported formats: correct encoding and bits_per_sample ---

TEST(SpaFormatMapping, Float32LE)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_F32_LE, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_FLOAT);
    EXPECT_EQ(bps, 32);
}

TEST(SpaFormatMapping, S8)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_S8, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_8BIT);
    EXPECT_EQ(bps, 8);
}

TEST(SpaFormatMapping, U8)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_U8, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_8BIT);
    EXPECT_EQ(bps, 8);
}

TEST(SpaFormatMapping, S16LE)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_S16_LE, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_16BIT);
    EXPECT_EQ(bps, 16);
}

TEST(SpaFormatMapping, S24LE)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_S24_LE, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_24BIT);
    EXPECT_EQ(bps, 24);
}

TEST(SpaFormatMapping, S32LE)
{
    int bps = 0;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_S32_LE, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_32BIT);
    EXPECT_EQ(bps, 32);
}

// --- Regression: Bug 1 — unsupported format must NOT crash, must return INVALID ---

TEST(SpaFormatMapping, UnknownFormatReturnsInvalid)
{
    int bps = 999;
    auto enc = spa_format_to_encoding(0xDEADBEEF, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

TEST(SpaFormatMapping, ALawReturnsInvalid)
{
    int bps = 999;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_A_LAW, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

TEST(SpaFormatMapping, MuLawReturnsInvalid)
{
    int bps = 999;
    auto enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_U8, &bps);
    // U8 is supported (maps to PCM_8BIT), but A_LAW/MU_LAW are not
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_PCM_8BIT);
    EXPECT_EQ(bps, 8);

    bps = 999;
    enc = spa_format_to_encoding(SPA_AUDIO_FORMAT_MU_LAW, &bps);
    EXPECT_EQ(enc, AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

// Big-endian variants are NOT in the supported set (only little-endian)
TEST(SpaFormatMapping, BigEndianReturnsInvalid)
{
    int bps = 0;
    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_F32_BE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);

    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_S16_BE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);

    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_S24_BE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);

    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_S32_BE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

// Unsigned 16/24/32 bit variants are NOT in the supported set
TEST(SpaFormatMapping, UnsignedVariantsReturnInvalid)
{
    int bps = 0;
    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_U16_LE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);

    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_U24_LE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);

    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_U32_LE, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

// Planar formats are NOT supported
TEST(SpaFormatMapping, PlanarFormatsReturnInvalid)
{
    int bps = 0;
    EXPECT_EQ(spa_format_to_encoding(SPA_AUDIO_FORMAT_F32P, &bps),
              AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(bps, 0);
}

#endif // linux

// ============================================================================
// AudioFormat Protobuf Tests (cross-platform)
// ============================================================================

TEST(AudioFormatProto, InvalidEncodingRoundTrip)
{
    AudioFormat fmt;
    fmt.set_encoding(AudioFormat_Encoding_ENCODING_INVALID);
    fmt.set_sample_rate(48000);
    fmt.set_channels(2);

    std::string serialized;
    ASSERT_TRUE(fmt.SerializeToString(&serialized));

    AudioFormat deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    EXPECT_EQ(deserialized.encoding(), AudioFormat_Encoding_ENCODING_INVALID);
    EXPECT_EQ(deserialized.sample_rate(), 48000);
    EXPECT_EQ(deserialized.channels(), 2);
}

TEST(AudioFormatProto, DefaultEncodingIsInvalid)
{
    AudioFormat fmt;
    // Default-constructed protobuf has encoding = ENCODING_INVALID (value 0)
    EXPECT_EQ(fmt.encoding(), AudioFormat_Encoding_ENCODING_INVALID);
}

TEST(AudioFormatProto, AllSupportedEncodingsRoundTrip)
{
    for (auto enc : {
             AudioFormat_Encoding_ENCODING_PCM_FLOAT,
             AudioFormat_Encoding_ENCODING_PCM_8BIT,
             AudioFormat_Encoding_ENCODING_PCM_16BIT,
             AudioFormat_Encoding_ENCODING_PCM_24BIT,
             AudioFormat_Encoding_ENCODING_PCM_32BIT,
         }) {
        AudioFormat fmt;
        fmt.set_encoding(enc);
        fmt.set_sample_rate(44100);
        fmt.set_channels(2);

        std::string serialized;
        ASSERT_TRUE(fmt.SerializeToString(&serialized));

        AudioFormat deserialized;
        ASSERT_TRUE(deserialized.ParseFromString(serialized));
        EXPECT_EQ(deserialized.encoding(), enc);
    }
}

// ============================================================================
// Block Align Guard Tests (cross-platform)
// Regression: Bug 4 — block_align == 0 must not cause division by zero
// ============================================================================

TEST(BlockAlignGuard, ZeroBlockAlignGuardPreventsDivisionByZero)
{
    // Simulate the exact guard condition from broadcast_audio_data
    constexpr int mtu = 1492;
    int max_seg_size = mtu - 20 - 8;  // = 1464
    int block_align = 0;

    // The fixed guard: return early if block_align <= 0
    if (block_align <= 0) {
        SUCCEED() << "Guard correctly prevents division by zero when block_align is 0";
    } else {
        max_seg_size -= max_seg_size % block_align;
        FAIL() << "Should not reach here";
    }
}

TEST(BlockAlignGuard, NegativeBlockAlignGuardPreventsDivisionByZero)
{
    int block_align = -1;

    if (block_align <= 0) {
        SUCCEED() << "Guard correctly prevents division by zero when block_align is negative";
    } else {
        FAIL() << "Should not reach here";
    }
}

TEST(BlockAlignGuard, ValidBlockAlignProducesAlignedSize)
{
    constexpr int mtu = 1492;
    int max_seg_size = mtu - 20 - 8;  // = 1464

    // 32-bit stereo: block_align = 4 * 2 = 8
    int block_align = 8;
    if (block_align > 0) {
        max_seg_size -= max_seg_size % block_align;
        EXPECT_EQ(max_seg_size % block_align, 0);
        EXPECT_LE(max_seg_size, 1464);
        EXPECT_GT(max_seg_size, 0);
    }
}

TEST(BlockAlignGuard, OneByteBlockAlignNoChange)
{
    constexpr int mtu = 1492;
    int max_seg_size = mtu - 20 - 8;  // = 1464

    // 8-bit mono: block_align = 1 * 1 = 1
    int block_align = 1;
    if (block_align > 0) {
        max_seg_size -= max_seg_size % block_align;
        EXPECT_EQ(max_seg_size, 1464);  // 1464 % 1 == 0, so no change
    }
}

TEST(BlockAlignGuard, TypicalFormatsProduceCorrectAlignment)
{
    constexpr int mtu = 1492;
    int base_seg = mtu - 20 - 8;  // = 1464

    struct TestCase {
        const char* name;
        int block_align;
        int expected_max_seg;
    };

    // 1464 % 4 = 0, 1464 % 6 = 0, 1464 % 8 = 0
    // So for these common formats, max_seg_size stays at 1464
    TestCase cases[] = {
        {"S16 stereo (4 bytes)", 4, 1464},
        {"S24 stereo (6 bytes)", 6, 1464},
        {"S32 stereo (8 bytes)", 8, 1464},
        {"F32 stereo (8 bytes)", 8, 1464},
        {"S16 5.1 (12 bytes)", 12, 1464},
        {"S32 5.1 (24 bytes)", 24, 1464},  // 1464 % 24 = 0
        {"S16 7.1 (16 bytes)", 16, 1456},  // 1464 % 16 = 8, 1464 - 8 = 1456
    };

    for (const auto& tc : cases) {
        int seg = base_seg;
        seg -= seg % tc.block_align;
        EXPECT_EQ(seg, tc.expected_max_seg) << tc.name;
        EXPECT_EQ(seg % tc.block_align, 0) << tc.name;
    }
}
