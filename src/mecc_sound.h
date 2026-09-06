#pragma once

#include "assets.h"
#include "dro_audio.h"

#include <cstdint>
#include <span>
#include <vector>

enum class MeccSoundProfile {
    NumberMunchers,
    WordMunchers,
    SuperMunchers,
};

struct MeccSound {
    bool valid{};
    std::uint32_t durationMilliseconds{};
    std::vector<OplWrite> writes;
    struct SpeakerWrite {
        std::uint32_t milliseconds{};
        std::uint16_t frequencyHz{}; // zero releases the PC-speaker gate
    };
    std::vector<SpeakerWrite> speakerWrites;
};

// Decode one sound from the original MECC GSND resource. The resource keeps a
// 256-entry little-endian stream table followed by the original bytecode.
[[nodiscard]] MeccSound decodeMeccGSound(
    BlobView gsnd,
    std::uint8_t soundIndex,
    MeccSoundProfile profile = MeccSoundProfile::NumberMunchers);

[[nodiscard]] std::vector<std::uint8_t> renderMeccGSoundToWave(
    BlobView gsnd,
    std::uint8_t soundIndex,
    std::uint32_t releaseTailMilliseconds = 300,
    MeccSoundProfile profile = MeccSoundProfile::NumberMunchers);

// Render the exact PC-speaker note path used by the same MECC bytecode. The
// sequence form is used for original compound cues such as GSND 8 -> 4.
[[nodiscard]] std::vector<std::uint8_t> renderMeccSpeakerSoundToWave(
    BlobView sound,
    std::uint8_t soundIndex,
    std::uint32_t releaseTailMilliseconds = 40,
    MeccSoundProfile profile = MeccSoundProfile::NumberMunchers,
    std::uint32_t startDelayMilliseconds = 0);

[[nodiscard]] std::vector<std::uint8_t> renderMeccSpeakerSequenceToWave(
    BlobView sound,
    std::span<const std::uint8_t> soundIndices,
    std::uint32_t releaseTailMilliseconds = 40,
    MeccSoundProfile profile = MeccSoundProfile::NumberMunchers);

// Render a direct PIT channel-2 tone. The games use this path for joystick
// repeat-speed feedback independently of the selectable gameplay sound driver.
[[nodiscard]] std::vector<std::uint8_t> renderPcSpeakerToneToWave(
    std::uint16_t frequencyHz,
    std::uint32_t durationMilliseconds);
