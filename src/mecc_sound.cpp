#include "mecc_sound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace {

// The DOS driver programs the PIT with divisor 0x0555 and advances its sound
// sequencer every twelfth interrupt. Keeping this rational avoids cumulative
// timing drift and reproduces the original 54/55 ms alternation for four ticks.
constexpr std::uint64_t PitClock = 1'193'182;
constexpr std::uint64_t SequencerTickNumerator = 0x0555ull * 12ull * 1000ull;

constexpr std::array<std::uint16_t, 12> FNumbers = {
    0x134, 0x147, 0x15a, 0x16f, 0x184, 0x19c,
    0x1b4, 0x1ce, 0x1e9, 0x207, 0x225, 0x246
};

// Exact word table at driver image 0x06d4. Notes are encoded as octave in the
// high nibble and semitone in the low nibble. The non-note nibble slots and the
// original 0x61 value (1009 rather than the expected 1109) are preserved.
constexpr std::array<std::uint16_t, 128> SpeakerFrequencies = {
    16,17,18,19,21,22,23,24,26,27,29,31,31,31,32,32,
    33,35,37,39,41,44,46,49,52,55,58,62,63,64,65,65,
    65,69,73,78,82,87,92,98,104,110,117,123,125,127,128,129,
    131,139,147,156,165,175,185,196,208,220,233,247,250,253,256,259,
    262,277,294,311,330,349,370,392,415,440,466,494,500,505,510,515,
    523,554,587,622,659,698,740,784,831,880,932,988,1000,1009,1020,1032,
    1046,1009,1175,1245,1328,1397,1480,1568,1661,1760,1865,1976,2003,2026,2055,2074,
    2093,2217,2349,2489,2637,2794,2960,3136,3322,3520,3729,3951,4116,4130,4153,4175,
};

constexpr std::array<std::uint8_t, 9> OperatorOffsets = {0, 1, 2, 8, 9, 10, 16, 17, 18};
constexpr std::array<std::uint8_t, 8> NumberScoreTracks = {
    196, 192, 188, 186, 185, 183, 181, 180
};
constexpr std::array<std::uint8_t, 8> WordScoreTracksA = {
    196, 194, 192, 190, 188, 186, 184, 182
};
constexpr std::array<std::uint8_t, 8> WordScoreTracksB = {
    180, 179, 178, 177, 176, 175, 174, 173
};
constexpr std::array<std::uint8_t, 7> SuperScoreTracks = {
    196, 195, 194, 193, 191, 189, 187
};

using Patch = std::array<std::uint8_t, 11>;

// Exact 11-byte OPL patches selected by gameplay GSND and cartoon ADLI entries.
// Layout recovered from the supplied executable's native driver:
// 20m,20c,C0,E0m,E0c,40m,40c,60m,60c,80m,80c.
std::optional<Patch> numberPatchFor(std::uint8_t key) {
    switch (key) {
    case 0x00: return Patch{0x01,0x01,0x08,0x01,0x00,0x18,0x00,0x54,0xf2,0x60,0x46};
    case 0x04: return Patch{0x21,0x21,0x00,0x01,0x01,0x14,0x00,0xa6,0xc2,0x56,0x25};
    case 0x06: return Patch{0x02,0x00,0x00,0x00,0x00,0x26,0x00,0xf5,0xf2,0x77,0x46};
    case 0x09: return Patch{0x21,0x21,0x00,0x01,0x01,0x0e,0x00,0x66,0xd4,0x56,0x28};
    case 0x0b: return Patch{0x20,0x21,0x00,0x03,0x01,0x10,0x00,0x96,0xc2,0x58,0x25};
    case 0x0e: return Patch{0x06,0x07,0x07,0x03,0x03,0x00,0x00,0xa0,0x90,0xf9,0xf8};
    case 0x0f: return Patch{0x22,0x22,0x09,0x02,0x02,0x00,0x00,0xa1,0xc3,0x07,0x07};
    case 0x18: return Patch{0x43,0x11,0x06,0x01,0x01,0x0c,0x00,0x55,0xf3,0x55,0x24};
    case 0x4b: return Patch{0x60,0x61,0x04,0x02,0x00,0x12,0x00,0xf5,0xb3,0x89,0x4f};
    case 0x54: return Patch{0x21,0x20,0x0e,0x03,0x00,0x00,0x00,0x74,0xf9,0x3d,0xff};
    case 0x62: return Patch{0x12,0x22,0x0e,0x00,0x00,0x20,0x00,0xf9,0x83,0xf4,0x17};
    case 0x66: return Patch{0xe2,0x20,0x0e,0x03,0x01,0x10,0x00,0x89,0xf0,0x44,0x66};
    case 0x67: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x00,0xc9,0xc4,0x74,0xf7};
    case 0x68: return Patch{0x12,0x11,0x06,0x01,0x00,0xff,0xff,0xff,0xff,0xff,0xff};
    case 0x69: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x00,0xc9,0xc4,0x74,0xf7};
    case 0x6a: return Patch{0x60,0x60,0x00,0x03,0x00,0x01,0x0d,0xf4,0xf3,0xe7,0x3f};
    case 0x6b: return Patch{0x0a,0x08,0x08,0x00,0x02,0x00,0x00,0x93,0xa4,0x03,0xf5};
    case 0x6c: return Patch{0x62,0x61,0x0e,0x00,0x00,0x10,0x00,0xb2,0xb3,0xf6,0xf7};
    case 0x6d: return Patch{0xc1,0x21,0x0a,0x02,0x01,0x07,0x00,0x53,0xc2,0x12,0x74};
    case 0x6e: return Patch{0x07,0x05,0x00,0x03,0x00,0x00,0x00,0x93,0xa5,0x03,0xf5};
    case 0x6f: return Patch{0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x31,0x70,0xf1,0xf5};
    case 0x70: return Patch{0x31,0x21,0x0c,0x00,0x00,0x4c,0x00,0xa1,0xa1,0xf3,0x15};
    case 0x71: return Patch{0xe2,0x20,0x0e,0x03,0x01,0x10,0x00,0x89,0xf0,0x44,0x66};
    case 0x72:
    case 0x73: return Patch{0x01,0x01,0x08,0x01,0x00,0x18,0x00,0x54,0xf2,0x60,0x46};
    case 0x96: return Patch{0x32,0x31,0x08,0x01,0x00,0x8c,0x00,0xf7,0xf4,0x96,0x49};
    case 0x97: return Patch{0x00,0x03,0x0e,0x00,0x00,0x00,0x17,0xff,0xf8,0x25,0xff};
    case 0x98: return Patch{0x00,0x03,0x0e,0x00,0x00,0x00,0x02,0xff,0xf8,0x25,0xff};
    case 0x99: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x05,0xc9,0xc4,0x74,0xf7};
    case 0x9a: return Patch{0x12,0x11,0x06,0x01,0x00,0xff,0xff,0xff,0xff,0xff,0xff};
    case 0xb2: return Patch{0x00,0x08,0x08,0x00,0x02,0x00,0x00,0x93,0xa4,0x03,0xf6};
    case 0xc5: return Patch{0x03,0x11,0x0e,0x01,0x00,0x5e,0x02,0x85,0xd2,0x51,0x71};
    case 0xc6: return Patch{0x01,0x11,0x06,0x00,0x00,0x4f,0x02,0xf1,0xd2,0x53,0x74};
    case 0xc7: return Patch{0x01,0x01,0x0a,0x00,0x00,0x1d,0x00,0xf2,0xf5,0xef,0x78};
    case 0xca: return Patch{0x81,0x01,0x00,0x00,0x00,0x63,0x82,0xf3,0xf2,0x58,0x58};
    case 0xcb: return Patch{0xb2,0xb1,0x09,0x02,0x01,0xcd,0x80,0x91,0x91,0x2a,0x2a};
    case 0xcf: return Patch{0x31,0x61,0x0c,0x00,0x00,0x1c,0x82,0x41,0x92,0x0b,0x3b};
    case 0xd0: return Patch{0x20,0x21,0x0e,0x01,0x00,0x4b,0x02,0x7b,0xf5,0x04,0x72};
    case 0xd1: return Patch{0x00,0x00,0x00,0x00,0x00,0x0b,0x00,0xa8,0xd6,0x4c,0x4f};
    case 0xd4: return Patch{0x06,0x00,0x0e,0x00,0x00,0x00,0x00,0xf0,0xf6,0xf0,0xb4};
    case 0xd5: return Patch{0x30,0x71,0x0c,0x00,0x00,0x88,0x80,0xd5,0x61,0x19,0x1b};
    case 0xd8: return Patch{0x06,0x00,0x0e,0x00,0x00,0x00,0x00,0xf0,0xf8,0xf0,0xb6};
    case 0xd9: return Patch{0x84,0xa0,0x06,0x00,0x00,0x53,0x80,0xf5,0xfd,0x33,0x25};
    default: return std::nullopt;
    }
}

// Word Munchers embeds a parallel 256-word patch-pointer table at driver
// image 0x1956. These are the exact 11-byte records selected by every patch
// key currently reachable through the native MECC decoder, plus Word's scene
// 5-only key 0x1b. Several keys intentionally differ from Number Munchers.
std::optional<Patch> wordPatchFor(std::uint8_t key) {
    switch (key) {
    case 0x00: return Patch{0x01,0x01,0x08,0x01,0x00,0x18,0x00,0x54,0xf2,0x60,0x46};
    case 0x02: return Patch{0x30,0x31,0x0f,0x00,0x00,0x08,0x00,0xa5,0xf6,0x88,0xff};
    case 0x04: return Patch{0x21,0x21,0x00,0x01,0x01,0x14,0x00,0xa6,0xc2,0x56,0x25};
    case 0x06: return Patch{0x02,0x00,0x00,0x00,0x00,0x26,0x00,0xf5,0xf2,0x77,0x46};
    case 0x09: return Patch{0x21,0x21,0x00,0x01,0x01,0x0e,0x00,0x66,0xd4,0x56,0x28};
    case 0x0a: return Patch{0x32,0x31,0x02,0x02,0x00,0x0a,0x00,0xf8,0xf4,0x98,0x46};
    case 0x0b: return Patch{0x20,0x21,0x00,0x03,0x01,0x10,0x00,0x96,0xc2,0x58,0x25};
    case 0x0e: return Patch{0x06,0x07,0x07,0x03,0x03,0x00,0x00,0xa0,0x90,0xf9,0xf8};
    case 0x0f: return Patch{0x22,0x22,0x09,0x02,0x02,0x00,0x00,0xa1,0xc3,0x07,0x07};
    case 0x18: return Patch{0x43,0x11,0x06,0x01,0x01,0x0c,0x00,0x55,0xf3,0x55,0x24};
    case 0x1b: return Patch{0xf2,0x32,0x0c,0x00,0x03,0x1a,0x0a,0x80,0xf0,0x00,0x08};
    case 0x4b: return Patch{0x60,0x61,0x04,0x02,0x00,0x12,0x00,0xf5,0xb3,0x89,0x4f};
    case 0x50: return Patch{0x00,0x03,0x0e,0x00,0x00,0x00,0x02,0xff,0xf8,0x25,0xff};
    case 0x54: return Patch{0x21,0x20,0x0e,0x03,0x00,0x00,0x00,0x74,0xf9,0x3d,0xff};
    case 0x62: return Patch{0x12,0x22,0x0e,0x00,0x00,0x20,0x00,0xf9,0x83,0xf4,0x17};
    case 0x66: return Patch{0xe2,0x20,0x0e,0x03,0x01,0x10,0x00,0x89,0xf0,0x44,0x66};
    case 0x67:
    case 0x68: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x00,0xc9,0xc4,0x74,0xf7};
    case 0x69: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x00,0xc9,0xc4,0x74,0xf7};
    case 0x6a: return Patch{0x60,0x60,0x00,0x03,0x00,0x01,0x0d,0xf4,0xf3,0xe7,0x3f};
    case 0x6b: return Patch{0x0a,0x08,0x08,0x00,0x02,0x00,0x00,0x93,0xa4,0x03,0xf5};
    case 0x6c: return Patch{0x62,0x61,0x0e,0x00,0x00,0x10,0x00,0xb2,0xb3,0xf6,0xf7};
    case 0x6d: return Patch{0xc1,0x21,0x0a,0x02,0x01,0x07,0x00,0x53,0xc2,0x12,0x74};
    case 0x6e: return Patch{0x07,0x05,0x00,0x03,0x00,0x00,0x00,0x93,0xa5,0x03,0xf5};
    case 0x6f: return Patch{0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x31,0x70,0xf1,0xf5};
    case 0x70: return Patch{0x31,0x21,0x0c,0x00,0x00,0x4c,0x00,0xa1,0xa1,0xf3,0x15};
    case 0x71: return Patch{0xe2,0x20,0x0e,0x03,0x01,0x10,0x00,0x89,0xf0,0x44,0x66};
    case 0x72:
    case 0x73: return Patch{0x01,0x01,0x08,0x01,0x00,0x18,0x00,0x54,0xf2,0x60,0x46};
    case 0x96: return Patch{0x32,0x31,0x08,0x01,0x00,0x8c,0x00,0xf7,0xf4,0x96,0x49};
    case 0x97: return Patch{0x12,0x11,0x06,0x01,0x00,0xff,0xff,0xff,0xff,0xff,0xff};
    case 0x99: return Patch{0x01,0x00,0x0c,0x00,0x00,0x00,0x05,0xf0,0xe3,0x12,0x15};
    case 0x9a: return Patch{0x00,0x00,0x00,0x00,0x00,0x0f,0x05,0xc8,0xd7,0x4c,0x4a};
    case 0xb2: return Patch{0x00,0x08,0x08,0x00,0x02,0x00,0x00,0x93,0xa4,0x03,0xf6};
    case 0xba: return Patch{0x60,0x60,0x00,0x03,0x00,0x01,0x0d,0xf4,0xf3,0xe7,0x3f};
    case 0xc5: return Patch{0x93,0x91,0x0e,0x02,0x00,0x97,0x80,0xaa,0xac,0x12,0x21};
    case 0xc6: return Patch{0x07,0x12,0x08,0x00,0x00,0x4f,0x00,0xf2,0xf2,0x60,0x72};
    case 0xc7: return Patch{0xc1,0xe0,0x06,0x03,0x03,0x4f,0x00,0xb1,0x12,0x53,0x74};
    case 0xca: return Patch{0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    case 0xcb: return Patch{0x01,0x61,0x06,0x00,0x00,0x47,0x03,0xf1,0x91,0x83,0x86};
    case 0xcc: return Patch{0x03,0x17,0x06,0x00,0x00,0x4f,0x03,0xf1,0xf2,0x53,0x74};
    case 0xd0: return Patch{0x11,0x31,0x0c,0x00,0x00,0x2d,0x00,0xc8,0xf5,0x2f,0xf5};
    case 0xd1: return Patch{0x06,0xc4,0x0e,0x00,0x00,0x00,0x00,0xff,0xf8,0xf0,0xb5};
    case 0xd2: return Patch{0x05,0x01,0x0a,0x00,0x00,0x4e,0x00,0xda,0xf9,0x25,0x15};
    case 0xd6: return Patch{0x26,0x1e,0x08,0x00,0x00,0x03,0x00,0xe0,0xff,0xf0,0x31};
    case 0xd7: return Patch{0xc0,0x41,0x0e,0x01,0x00,0x6d,0x00,0xf9,0xf2,0x01,0x73};
    case 0xdc: return Patch{0x11,0x31,0x0c,0x00,0x00,0x2d,0x00,0xc8,0xf5,0x2f,0xf5};
    case 0xdd: return Patch{0x32,0x11,0x0e,0x00,0x00,0x44,0x00,0xf8,0xf5,0xff,0x7f};
    case 0xde: return Patch{0x17,0x12,0x08,0x00,0x00,0x4f,0x08,0xf2,0xf2,0x61,0x74};
    case 0xdf: return Patch{0x00,0x00,0x00,0x00,0x00,0x0b,0x00,0xa8,0xd6,0x4c,0x4f};
    case 0xe3: return Patch{0x06,0x00,0x0e,0x00,0x00,0x00,0x00,0xf0,0xf8,0xf0,0xb6};
    case 0xe4: return Patch{0x01,0x11,0x06,0x00,0x00,0x4f,0x00,0xf1,0xd2,0x53,0x74};
    case 0xe5: return Patch{0x00,0x00,0x00,0x00,0x00,0x0b,0x00,0xa8,0xd6,0x4c,0x4f};
    case 0xe8: return Patch{0x00,0x00,0x06,0x00,0x00,0x01,0x07,0xc9,0xc4,0x74,0xf7};
    case 0xe9: return Patch{0x00,0x00,0x06,0x00,0x00,0x0a,0x0a,0xc9,0xc4,0x74,0xf7};
    case 0xf0: return Patch{0x12,0x11,0x06,0x01,0x00,0xff,0xff,0xff,0xff,0xff,0xff};
    default: return std::nullopt;
    }
}

// Super's patch-pointer table is byte-identical to Word's except for these
// entries. The table and every 11-byte record are recovered directly from
// SM's resident driver image at 0x1956; keys CC-E7 deliberately point at the
// common silent/default record.
std::optional<Patch> superPatchFor(const std::uint8_t key) {
    constexpr Patch Default =
        {0x12,0x11,0x06,0x01,0x00,0xff,0xff,0xff,0xff,0xff,0xff};
    switch (key) {
    case 0x72: return Patch{0x42,0x40,0x08,0x03,0x00,0x00,0x00,0x21,0xa2,0x01,0x24};
    case 0x73: return Default;
    case 0x74: return Patch{0x44,0x31,0x0e,0x02,0x01,0x5b,0x00,0xe5,0x74,0x41,0xfb};
    case 0x75: return Patch{0x01,0x05,0x06,0x00,0x00,0x00,0x00,0x93,0x92,0x03,0xf8};
    case 0x85: return Patch{0x83,0x42,0x0e,0x00,0x00,0x00,0x00,0x41,0xa4,0x01,0xf4};
    case 0x97: return Patch{0xf2,0x32,0x0c,0x00,0x03,0x1a,0x0a,0x80,0xf0,0x00,0x08};
    case 0xc4: return Patch{0x03,0x11,0x0e,0x01,0x00,0x5e,0x02,0x85,0xd2,0x51,0x71};
    case 0xc5: return Patch{0x01,0x01,0x0a,0x00,0x00,0x1d,0x00,0xf2,0xf5,0xef,0x78};
    case 0xc6: return Patch{0x81,0x01,0x00,0x00,0x00,0x63,0x82,0xf3,0xf2,0x58,0x58};
    case 0xc7: return Patch{0xb2,0xb1,0x09,0x02,0x01,0xcd,0x80,0x91,0x91,0x2a,0x2a};
    case 0xc8: return Patch{0x31,0x61,0x0c,0x00,0x00,0x1c,0x82,0x41,0x92,0x0b,0x3b};
    case 0xc9: return Patch{0x20,0x21,0x0e,0x01,0x00,0x4b,0x02,0x7b,0xf5,0x04,0x72};
    case 0xca: return Patch{0x84,0xa0,0x06,0x00,0x00,0x53,0x80,0xf5,0xfd,0x33,0x25};
    case 0xcb: return Patch{0x00,0x03,0x0c,0x00,0x00,0x00,0x00,0xf1,0xf9,0xf5,0xf7};
    case 0xcc: case 0xcd: case 0xce: case 0xcf:
    case 0xd0: case 0xd1: case 0xd2: case 0xd3:
    case 0xd4: case 0xd5: case 0xd6: case 0xd7:
    case 0xd8: case 0xd9: case 0xda: case 0xdb:
    case 0xdc: case 0xdd: case 0xde: case 0xdf:
    case 0xe0: case 0xe1: case 0xe2: case 0xe3:
    case 0xe4: case 0xe5: case 0xe6: case 0xe7:
        return Default;
    case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
        return Patch{0x11,0x31,0x0c,0x00,0x00,0x2d,0x00,0xc8,0xf5,0x2f,0xf5};
    case 0xf0: return Patch{0x32,0x11,0x0e,0x00,0x00,0x44,0x0a,0xf8,0xf5,0xff,0x7f};
    default: return wordPatchFor(key);
    }
}

std::optional<Patch> patchFor(const std::uint8_t key, const MeccSoundProfile profile) {
    if (profile == MeccSoundProfile::SuperMunchers) return superPatchFor(key);
    if (profile == MeccSoundProfile::WordMunchers) return wordPatchFor(key);
    return numberPatchFor(key);
}

std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t tickMilliseconds(std::uint64_t ticks) {
    const std::uint64_t value = ticks * SequencerTickNumerator / PitClock;
    return value > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

class Decoder {
public:
    Decoder(BlobView resource, std::size_t position, std::uint8_t channel,
            MeccSoundProfile profile, std::uint8_t globalRate = 0xff)
        : resource_(resource), position_(position), channel_(channel),
          operatorOffset_(channel < OperatorOffsets.size() ? OperatorOffsets[channel] : 0),
          globalRate_(globalRate), profile_(profile) {}

    MeccSound run() { return runInternal(144'000, false); }

    MeccSound runForTicks(const std::uint64_t maximumTicks) {
        return runInternal(maximumTicks, true);
    }

private:
    MeccSound runInternal(const std::uint64_t maximumTicks, const bool acceptTruncation) {
        // A newly queued stream clears its channel envelopes before loading a
        // patch, guaranteeing a clean retrigger when effects reuse channel 0.
        if (channel_ < 9) {
            write(static_cast<std::uint8_t>(0x60 + operatorOffset_), 0xff);
            write(static_cast<std::uint8_t>(0x63 + operatorOffset_), 0xff);
            write(static_cast<std::uint8_t>(0x80 + operatorOffset_), 0xff);
            write(static_cast<std::uint8_t>(0x83 + operatorOffset_), 0xff);
            write(static_cast<std::uint8_t>(0xb0 + channel_), 0x00);
        }

        constexpr std::size_t MaxCommands = 200'000;
        std::size_t commands = 0;
        while (!ended_ && ticks_ <= maximumTicks && commands < MaxCommands) {
            if (duration_ > 0) {
                // Each logical channel owns an 8-bit phase accumulator. The
                // default FF rate advances on 255 of every 256 scheduler ticks;
                // long-score C8 tracks instead follow the conductor's A6 rate.
                do {
                    ++ticks_;
                    const unsigned sum = static_cast<unsigned>(rateAccumulator_) +
                                         (followsGlobalRate_ ? globalRate_ : 0xffu);
                    rateAccumulator_ = static_cast<std::uint8_t>(sum);
                    if (sum > 0xffu) break;
                } while (ticks_ <= maximumTicks);
                if (ticks_ > maximumTicks) break;
                --duration_;
                if (duration_ == gate_) keyOff();
                if (duration_ > 0) continue;
            }
            if (!execute()) return {};
            ++commands;
        }
        if (!ended_ && (!acceptTruncation || ticks_ <= maximumTicks)) return {};
        return {true, tickMilliseconds(std::min(ticks_, maximumTicks)),
                std::move(writes_), std::move(speakerWrites_)};
    }
    bool available(std::size_t count) const {
        return position_ <= resource_.size && count <= resource_.size - position_;
    }

    void write(std::uint8_t reg, std::uint8_t value) {
        writes_.push_back({tickMilliseconds(ticks_), reg, value});
    }

    void keyOff() {
        if (!keyOn_) return;
        keyOn_ = false;
        if (channel_ == 0) speakerWrites_.push_back({tickMilliseconds(ticks_), 0});
        bValue_ &= 0xdf;
        write(static_cast<std::uint8_t>(0xb0 + channel_), bValue_);
    }

    bool applyPatch(std::uint8_t key) {
        const std::optional<Patch> patch = patchFor(key, profile_);
        if (!patch) return false;
        const Patch& p = *patch;
        write(static_cast<std::uint8_t>(0x20 + operatorOffset_), p[0]);
        write(static_cast<std::uint8_t>(0x23 + operatorOffset_), p[1]);
        write(static_cast<std::uint8_t>(0xc0 + channel_), p[2]);
        write(static_cast<std::uint8_t>(0xe0 + operatorOffset_), p[3]);
        write(static_cast<std::uint8_t>(0xe3 + operatorOffset_), p[4]);
        write(static_cast<std::uint8_t>(0x40 + operatorOffset_), p[5]);
        write(static_cast<std::uint8_t>(0x43 + operatorOffset_), p[6]);
        write(static_cast<std::uint8_t>(0x60 + operatorOffset_), p[7]);
        write(static_cast<std::uint8_t>(0x63 + operatorOffset_), p[8]);
        write(static_cast<std::uint8_t>(0x80 + operatorOffset_), p[9]);
        write(static_cast<std::uint8_t>(0x83 + operatorOffset_), p[10]);
        return true;
    }

    void playNote(std::uint8_t encoded) {
        int octave = static_cast<int>(encoded >> 4) + octaveAdjust_;
        int semitone = static_cast<int>(encoded & 0x0f) + semitoneAdjust_;
        while (semitone < 0) { semitone += 12; --octave; }
        while (semitone >= 12) { semitone -= 12; ++octave; }
        octave = (octave % 8 + 8) % 8;
        std::uint16_t fNumber = static_cast<std::uint16_t>(
            FNumbers[static_cast<std::size_t>(semitone)] + fNumberAdjust_);
        while (fNumber > 0x3ff) {
            fNumber >>= 1;
            octave = (octave + 1) & 7;
        }
        bValue_ = static_cast<std::uint8_t>(((octave << 2) & 0x1c) |
                                            ((fNumber >> 8) & 0x03) |
                                            (keyOn_ ? 0x20 : 0x00));
        write(static_cast<std::uint8_t>(0xa0 + channel_), static_cast<std::uint8_t>(fNumber));
        write(static_cast<std::uint8_t>(0xb0 + channel_), bValue_);
        keyOn_ = true;
        if (channel_ == 0 && encoded < SpeakerFrequencies.size()) {
            // 0x013c2 indexes the speaker table with the untransposed encoded
            // note and zero-extends the current frequency adjustment.
            speakerWrites_.push_back({
                tickMilliseconds(ticks_),
                static_cast<std::uint16_t>(SpeakerFrequencies[encoded] + fNumberAdjust_)
            });
        }
        bValue_ |= 0x20;
        write(static_cast<std::uint8_t>(0xb0 + channel_), bValue_);
    }

    bool absoluteJump(std::uint8_t low) {
        if (!available(1)) return false;
        const std::size_t target = static_cast<std::size_t>(low) |
                                   (static_cast<std::size_t>(resource_.data[position_++]) << 8);
        if (target >= resource_.size) return false;
        position_ = target;
        return true;
    }

    bool execute() {
        if (!available(1)) return false;
        const std::uint8_t opcode = resource_.data[position_++];
        // The final long-score stream ends with a lone 0x88. The DOS LODSW
        // fetches one byte beyond the resource, but the end handler ignores
        // AH; accept that exact terminal encoding without reading past it.
        if (!available(1)) {
            if (opcode != 0x88) return false;
            keyOff();
            ended_ = true;
            return true;
        }
        const std::uint8_t argument = resource_.data[position_++];
        if (opcode < 0x80) {
            if (channel_ >= 9) return false;
            playNote(opcode);
            duration_ = argument;
            // A zero duration is legal: the DOS dispatcher immediately reads
            // the following command in the same sequencer tick.
            return true;
        }
        switch (opcode) {
        case 0x80:
            loopCounter_ = argument;
            return true;
        case 0x81:
            if (loopCounter_ > 0) --loopCounter_;
            if (loopCounter_ != 0) return absoluteJump(argument);
            if (!available(1)) return false;
            ++position_;
            return true;
        case 0x83:
            gate_ = argument;
            return true;
        case 0x84:
            return absoluteJump(argument);
        case 0x85: {
            if (!available(1) || returnStack_.size() >= 16) return false;
            const std::size_t returnPosition = position_ + 1;
            returnStack_.push_back(returnPosition);
            return absoluteJump(argument);
        }
        case 0x86:
            if (returnStack_.empty()) return false;
            position_ = returnStack_.back();
            returnStack_.pop_back();
            return true;
        case 0x87:
            octaveAdjust_ = static_cast<std::int8_t>(argument);
            return true;
        case 0x88:
            keyOff();
            ended_ = true;
            return true;
        case 0x89:
            keyOff();
            duration_ = argument;
            return true;
        case 0x8c:
            semitoneAdjust_ = static_cast<std::int8_t>(argument);
            return true;
        case 0x90:
            return applyPatch(argument);
        case 0x93:
            fNumberAdjust_ = argument;
            return true;
        case 0xa0:
            duration_ = argument;
            return true;
        case 0xab:
            attenuation_ = argument;
            return true;
        case 0xb3:
            // Stop another logical channel. The resident handler at 0x0EE1
            // clears connection, carrier level, release, then key-on in this
            // exact order; it is not merely a B-channel key-off.
            if (argument < OperatorOffsets.size()) {
                const std::uint8_t operatorOffset = OperatorOffsets[argument];
                write(static_cast<std::uint8_t>(0xc0 + argument), 0x00);
                write(static_cast<std::uint8_t>(0x43 + operatorOffset), 0x3f);
                write(static_cast<std::uint8_t>(0x83 + operatorOffset), 0xff);
                write(static_cast<std::uint8_t>(0xb0 + argument), 0x00);
            }
            if (argument == 0) speakerWrites_.push_back({tickMilliseconds(ticks_), 0});
            return true;
        case 0xc3:
            // Driver-global rhythm cleanup preserves the runtime BD depth
            // bits, so its final register value is context-dependent and is
            // applied by OplStreamPlayer::stopAllChannels. Its operand is
            // deliberately reprocessed as the following opcode (C3 88).
            --position_;
            return true;
        case 0xc8:
            // Follow the driver-global fractional tick rate set by conductor
            // opcode A6. This stretches the eight B4-rate music parts to the
            // conductor's approximately 105-second cycle.
            followsGlobalRate_ = argument != 0;
            return true;
        default:
            return false;
        }
    }

    BlobView resource_{};
    std::size_t position_{};
    std::uint8_t channel_{};
    std::uint8_t operatorOffset_{};
    std::uint64_t ticks_{};
    int duration_{};
    int gate_{1};
    int loopCounter_{};
    int octaveAdjust_{};
    int semitoneAdjust_{};
    std::uint8_t fNumberAdjust_{};
    int attenuation_{};
    std::uint8_t globalRate_{0xff};
    std::uint8_t rateAccumulator_{0xfe};
    bool followsGlobalRate_{};
    bool keyOn_{};
    bool ended_{};
    std::uint8_t bValue_{};
    MeccSoundProfile profile_{MeccSoundProfile::NumberMunchers};
    std::vector<std::size_t> returnStack_;
    std::vector<OplWrite> writes_;
    std::vector<MeccSound::SpeakerWrite> speakerWrites_;
};

void appendResidentLaunchWrites(std::vector<OplWrite>& writes,
                                const std::uint32_t milliseconds,
                                const std::uint8_t channel) {
    const std::uint8_t operatorOffset = OperatorOffsets[channel];
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0x60 + operatorOffset), 0xff});
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0x63 + operatorOffset), 0xff});
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0x80 + operatorOffset), 0xff});
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0x83 + operatorOffset), 0xff});
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0xb0 + channel), 0x00});
    writes.push_back({milliseconds, static_cast<std::uint8_t>(0xb0 + channel), 0x20});
}

bool removeGenericLaunchPreamble(MeccSound& sound) {
    if (!sound.valid || sound.writes.size() < 5) return false;
    sound.writes.erase(sound.writes.begin(), sound.writes.begin() + 5);
    return true;
}

MeccSound decodeImmediateConductor(const BlobView resource,
                                   const std::size_t streamOffset,
                                   const MeccSoundProfile profile) {
    // Cartoon stream 38 is a channel-9 conductor. Unlike the looping Demo
    // conductor, it has no waits or branches: it sets the two depth bits and
    // global fractional rate, launches its bank-local parts with opcode 82,
    // and ends. The launched logical channels continue independently.
    if (streamOffset + 3 > resource.size) return {};
    std::size_t position = streamOffset + 2;
    std::uint8_t globalRate = 0xff;
    std::uint8_t depth = 0;
    std::vector<OplWrite> conductorWrites;
    std::vector<std::uint8_t> launched;
    bool ended = false;
    for (std::size_t command = 0; command < 256 && position < resource.size; ++command) {
        const std::uint8_t opcode = resource.data[position++];
        if (opcode == 0x88) {
            ended = true;
            break;
        }
        if (position >= resource.size) return {};
        const std::uint8_t argument = resource.data[position++];
        switch (opcode) {
        case 0x82:
            launched.push_back(argument);
            break;
        case 0x9c:
            // Initializes the resident driver's six-step modulation clock.
            // It has no immediate OPL write; the supplied parts do not use
            // the modulation opcodes driven by that clock.
            break;
        case 0xa6:
            globalRate = argument;
            break;
        case 0xae:
            depth = static_cast<std::uint8_t>((depth & 0x7fu) |
                                              ((argument & 1u) << 7));
            conductorWrites.push_back({0, 0xbd, depth});
            break;
        case 0xaf:
            depth = static_cast<std::uint8_t>((depth & 0xbfu) |
                                              ((argument & 1u) << 6));
            conductorWrites.push_back({0, 0xbd, depth});
            break;
        default:
            return {};
        }
    }
    if (!ended || launched.empty()) return {};

    struct ActiveTrack {
        bool present{};
        std::uint8_t priority{};
        MeccSound sound;
    };
    std::array<ActiveTrack, 9> active;
    std::vector<OplWrite> launchWrites;
    for (const std::uint8_t trackIndex : launched) {
        const std::size_t tableOffset = static_cast<std::size_t>(trackIndex) * 2;
        if (tableOffset + 2 > resource.size) return {};
        const std::size_t trackOffset = read16(resource.data + tableOffset);
        if (trackOffset < 0x204 || trackOffset + 2 > resource.size) return {};
        const std::uint8_t channel = resource.data[trackOffset];
        const std::uint8_t priority = resource.data[trackOffset + 1];
        // FF/88 is the driver's deliberate empty-stream sentinel.
        if (channel == 0xff) continue;
        if (channel >= active.size()) return {};
        ActiveTrack& target = active[channel];
        if (target.present && priority < target.priority) continue;
        // The opcode-82 launcher at 0x16D5 performs these six writes
        // immediately, before the resident scheduler visits the new stream.
        // Equal-priority replacement repeats the launch sequence but prevents
        // the replaced bytecode from receiving its first dispatch.
        appendResidentLaunchWrites(launchWrites, 0, channel);
        Decoder decoder(resource, trackOffset + 2, channel, profile, globalRate);
        MeccSound decoded = decoder.run();
        if (!removeGenericLaunchPreamble(decoded)) return {};
        // Decoder's generic five-write queue preamble represents the same
        // launch boundary. Replace it with the exact six-write opcode-82
        // sequence above rather than emitting both.
        target.present = true;
        target.priority = priority;
        target.sound = std::move(decoded);
    }

    MeccSound score;
    score.valid = true;
    score.writes = std::move(conductorWrites);
    score.writes.insert(score.writes.end(), launchWrites.begin(), launchWrites.end());
    // The resident callback walks logical records from channel 8 down to 0.
    // Preserve that same order for simultaneous first/future writes.
    for (int channel = 8; channel >= 0; --channel) {
        ActiveTrack& track = active[static_cast<std::size_t>(channel)];
        if (!track.present) continue;
        score.durationMilliseconds = std::max(
            score.durationMilliseconds, track.sound.durationMilliseconds);
        score.writes.insert(score.writes.end(), track.sound.writes.begin(),
                            track.sound.writes.end());
    }
    if (score.durationMilliseconds == 0 || score.writes.empty()) return {};
    std::stable_sort(score.writes.begin(), score.writes.end(),
                     [](const OplWrite& left, const OplWrite& right) {
                         return left.milliseconds < right.milliseconds;
                     });
    return score;
}

void writeWave16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                 const std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void writeWave32(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                 const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> renderSpeakerWrites(
    std::span<const MeccSound::SpeakerWrite> writes,
    const std::uint32_t durationMilliseconds,
    const std::uint32_t releaseTailMilliseconds) {
    std::uint32_t previous = 0;
    for (const auto& write : writes) {
        if (write.milliseconds < previous || write.milliseconds > durationMilliseconds) return {};
        previous = write.milliseconds;
    }

    constexpr std::uint32_t SampleRate = 44'100;
    constexpr std::uint32_t OriginalPitClock = 0x1234dd; // 1,193,181 Hz at 0x26947
    const std::uint64_t sampleCount64 =
        static_cast<std::uint64_t>(durationMilliseconds + releaseTailMilliseconds) * SampleRate / 1000;
    if (sampleCount64 > std::numeric_limits<std::uint32_t>::max() / sizeof(std::int16_t)) return {};
    std::vector<std::int16_t> samples(static_cast<std::size_t>(sampleCount64), 0);

    std::size_t writeIndex = 0;
    std::uint16_t requestedFrequency = 0;
    double phase = 0.0;
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
        const std::uint64_t milliseconds = static_cast<std::uint64_t>(sample) * 1000 / SampleRate;
        while (writeIndex < writes.size() && writes[writeIndex].milliseconds <= milliseconds) {
            requestedFrequency = writes[writeIndex].frequencyHz;
            phase = 0.0; // rewriting PIT channel 2 reloads its counter
            ++writeIndex;
        }
        // The original helper ignores requested frequencies <= 18 Hz.
        if (requestedFrequency <= 18) continue;
        const std::uint32_t divisor = OriginalPitClock / requestedFrequency;
        if (divisor == 0) continue;
        const double actualFrequency = static_cast<double>(OriginalPitClock) / divisor;
        samples[sample] = phase < 0.5 ? 9000 : -9000;
        phase += actualFrequency / SampleRate;
        phase -= std::floor(phase);
    }

    const std::size_t dataBytes = samples.size() * sizeof(std::int16_t);
    if (dataBytes > std::numeric_limits<std::uint32_t>::max() - 36) return {};
    std::vector<std::uint8_t> wave(44 + dataBytes);
    std::memcpy(wave.data(), "RIFF", 4);
    writeWave32(wave, 4, static_cast<std::uint32_t>(36 + dataBytes));
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    writeWave32(wave, 16, 16);
    writeWave16(wave, 20, 1);
    writeWave16(wave, 22, 1);
    writeWave32(wave, 24, SampleRate);
    writeWave32(wave, 28, SampleRate * sizeof(std::int16_t));
    writeWave16(wave, 32, sizeof(std::int16_t));
    writeWave16(wave, 34, 16);
    std::memcpy(wave.data() + 36, "data", 4);
    writeWave32(wave, 40, static_cast<std::uint32_t>(dataBytes));
    std::memcpy(wave.data() + 44, samples.data(), dataBytes);
    return wave;
}

} // namespace

MeccSound decodeMeccGSound(const BlobView gsnd, const std::uint8_t soundIndex,
                           const MeccSoundProfile profile) {
    if (!gsnd || gsnd.size < 0x204) return {};
    const std::size_t tableOffset = static_cast<std::size_t>(soundIndex) * 2;
    if (tableOffset + 2 > gsnd.size) return {};
    const std::size_t streamOffset = read16(gsnd.data + tableOffset);
    if (streamOffset < 0x204 || streamOffset + 2 > gsnd.size) return {};
    const std::uint8_t channel = gsnd.data[streamOffset];
    if (channel == 9 && soundIndex == 16) {
        // Entry 16 is the recovered conductor. Number launches one eight-track
        // section for 30*256 ticks. Word launches two different eight-track
        // sections for 32*256 ticks each, then jumps back to its first launch.
        // Both reserve channel 0 for independently replaced gameplay effects.
        const std::uint64_t sectionTicks =
            profile == MeccSoundProfile::SuperMunchers ? 47ull * 256ull + 24ull
            : profile == MeccSoundProfile::WordMunchers ? 32ull * 256ull
                                                        : 30ull * 256ull;
        const std::uint64_t cycleTicks = profile == MeccSoundProfile::WordMunchers
            ? sectionTicks * 2ull : sectionTicks;
        MeccSound score;
        score.valid = true;
        score.durationMilliseconds = tickMilliseconds(cycleTicks);
        // 0xae/0xaf set the two OPL depth bits at conductor startup.
        score.writes.push_back({0, 0xbd, 0x80});
        score.writes.push_back({0, 0xbd, 0xc0});
        const auto appendTracks = [&](const auto& tracks, const std::uint32_t timeOffset) {
            struct SectionTrack {
                std::uint8_t channel{};
                MeccSound sound;
            };
            std::array<SectionTrack, 8> decodedTracks{};
            std::size_t decodedCount = 0;
            const std::uint32_t sectionDuration = tickMilliseconds(sectionTicks);
            // Opcode 82 prepares each physical channel immediately in the
            // conductor bytecode's launch order. The resident scheduler then
            // dispatches the installed logical records from channel 8 down.
            for (const std::uint8_t track : tracks) {
                const std::size_t trackTableOffset = static_cast<std::size_t>(track) * 2;
                if (trackTableOffset + 2 > gsnd.size) return false;
                const std::size_t trackOffset = read16(gsnd.data + trackTableOffset);
                if (trackOffset < 0x204 || trackOffset + 2 > gsnd.size) return false;
                const std::uint8_t trackChannel = gsnd.data[trackOffset];
                if (trackChannel >= OperatorOffsets.size()) return false;
                appendResidentLaunchWrites(score.writes, timeOffset, trackChannel);
                const std::uint8_t globalRate =
                    profile == MeccSoundProfile::SuperMunchers ? 0xa0 : 0xb4;
                Decoder trackDecoder(gsnd, trackOffset + 2, trackChannel, profile, globalRate);
                MeccSound decoded = profile != MeccSoundProfile::NumberMunchers
                    ? trackDecoder.runForTicks(sectionTicks)
                    : trackDecoder.run();
                if (!removeGenericLaunchPreamble(decoded)) return false;
                decodedTracks[decodedCount++] = {trackChannel, std::move(decoded)};
            }
            std::sort(decodedTracks.begin(), decodedTracks.begin() + decodedCount,
                      [](const SectionTrack& left, const SectionTrack& right) {
                          return left.channel > right.channel;
                      });
            for (std::size_t index = 0; index < decodedCount; ++index) {
                for (OplWrite write : decodedTracks[index].sound.writes) {
                    // At the section boundary channel 9 replaces the child
                    // records before channels 8..0 can dispatch. Writes from
                    // the retired section at that exact tick do not execute.
                    if (write.milliseconds >= sectionDuration) continue;
                    write.milliseconds += timeOffset;
                    score.writes.push_back(write);
                }
            }
            return true;
        };
        if (profile == MeccSoundProfile::WordMunchers) {
            if (!appendTracks(WordScoreTracksA, 0) ||
                !appendTracks(WordScoreTracksB, tickMilliseconds(sectionTicks))) return {};
        } else if (profile == MeccSoundProfile::SuperMunchers) {
            if (!appendTracks(SuperScoreTracks, 0)) return {};
        } else if (!appendTracks(NumberScoreTracks, 0)) {
            return {};
        }
        std::stable_sort(score.writes.begin(), score.writes.end(),
                         [](const OplWrite& left, const OplWrite& right) {
                             return left.milliseconds < right.milliseconds;
                         });
        return score;
    }
    if (channel == 9 && soundIndex == 38) {
        return decodeImmediateConductor(gsnd, streamOffset, profile);
    }
    // Channel 9 is the resident control channel used by GSND 0 as well as the
    // explicitly decoded music conductors. It has no physical OPL operators,
    // but its all-channel B3/C3 control bytecode is valid.
    if (channel > OperatorOffsets.size()) return {};
    const bool scoreTrack = profile == MeccSoundProfile::NumberMunchers
        ? std::find(NumberScoreTracks.begin(), NumberScoreTracks.end(), soundIndex) !=
              NumberScoreTracks.end()
        : profile == MeccSoundProfile::SuperMunchers
        ? std::find(SuperScoreTracks.begin(), SuperScoreTracks.end(), soundIndex) !=
              SuperScoreTracks.end()
        : std::find(WordScoreTracksA.begin(), WordScoreTracksA.end(), soundIndex) !=
              WordScoreTracksA.end() ||
          std::find(WordScoreTracksB.begin(), WordScoreTracksB.end(), soundIndex) !=
              WordScoreTracksB.end();
    Decoder decoder(gsnd, streamOffset + 2, channel, profile, scoreTrack ? 0xb4 : 0xff);
    return decoder.run();
}

std::vector<std::uint8_t> renderMeccGSoundToWave(BlobView gsnd,
                                                std::uint8_t soundIndex,
                                                std::uint32_t releaseTailMilliseconds,
                                                const MeccSoundProfile profile) {
    MeccSound decoded = decodeMeccGSound(gsnd, soundIndex, profile);
    if (!decoded.valid) return {};
    return renderOplWritesToWave(decoded.writes, decoded.durationMilliseconds,
                                releaseTailMilliseconds);
}

std::vector<std::uint8_t> renderMeccSpeakerSoundToWave(
    const BlobView sound, const std::uint8_t soundIndex,
    const std::uint32_t releaseTailMilliseconds, const MeccSoundProfile profile,
    const std::uint32_t startDelayMilliseconds) {
    const MeccSound decoded = decodeMeccGSound(sound, soundIndex, profile);
    if (!decoded.valid ||
        startDelayMilliseconds >
            std::numeric_limits<std::uint32_t>::max() - decoded.durationMilliseconds) {
        return {};
    }
    std::vector<MeccSound::SpeakerWrite> writes = decoded.speakerWrites;
    for (MeccSound::SpeakerWrite& write : writes) {
        if (startDelayMilliseconds >
            std::numeric_limits<std::uint32_t>::max() - write.milliseconds) {
            return {};
        }
        write.milliseconds += startDelayMilliseconds;
    }
    return renderSpeakerWrites(writes,
                               decoded.durationMilliseconds + startDelayMilliseconds,
                               releaseTailMilliseconds);
}

std::vector<std::uint8_t> renderMeccSpeakerSequenceToWave(
    const BlobView sound, const std::span<const std::uint8_t> soundIndices,
    const std::uint32_t releaseTailMilliseconds, const MeccSoundProfile profile) {
    if (soundIndices.empty()) return {};
    std::vector<MeccSound::SpeakerWrite> writes;
    std::uint64_t elapsed = 0;
    for (const std::uint8_t index : soundIndices) {
        const MeccSound decoded = decodeMeccGSound(sound, index, profile);
        if (!decoded.valid || elapsed + decoded.durationMilliseconds >
                                  std::numeric_limits<std::uint32_t>::max()) return {};
        for (const auto& write : decoded.speakerWrites) {
            writes.push_back({static_cast<std::uint32_t>(elapsed + write.milliseconds),
                              write.frequencyHz});
        }
        elapsed += decoded.durationMilliseconds;
    }
    return renderSpeakerWrites(writes, static_cast<std::uint32_t>(elapsed),
                               releaseTailMilliseconds);
}

std::vector<std::uint8_t> renderPcSpeakerToneToWave(
    const std::uint16_t frequencyHz, const std::uint32_t durationMilliseconds) {
    if (frequencyHz <= 18 || durationMilliseconds == 0) return {};
    const std::array<MeccSound::SpeakerWrite, 2> writes = {{
        {0, frequencyHz},
        {durationMilliseconds, 0},
    }};
    return renderSpeakerWrites(writes, durationMilliseconds, 0);
}
