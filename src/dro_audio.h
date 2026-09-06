#pragma once

#include <cstdint>
#include <span>
#include <vector>

struct DroInfo {
    bool valid{};
    std::uint32_t durationMilliseconds{};
    std::uint32_t pairCount{};
    std::uint32_t registerWriteCount{};
    std::uint8_t hardwareType{};
};

struct OplWrite {
    std::uint32_t milliseconds{};
    std::uint8_t reg{};
    std::uint8_t value{};
};

struct OplSound {
    bool valid{};
    std::uint32_t durationMilliseconds{};
    std::vector<OplWrite> writes;
};

[[nodiscard]] DroInfo inspectDro(std::span<const std::uint8_t> bytes);

[[nodiscard]] OplSound decodeDroSound(std::span<const std::uint8_t> bytes);

// Render a DOSBox Raw OPL 2.0 capture through a YM3812 core and wrap the
// resulting mono signed-16-bit samples in an in-memory RIFF/WAVE file.
[[nodiscard]] std::vector<std::uint8_t> renderDroToWave(
    std::span<const std::uint8_t> bytes,
    std::uint32_t releaseTailMilliseconds = 300);

// Render timestamped YM3812 register writes. This is shared by the lossless
// DOSBox trace player and the native MECC GSND bytecode interpreter.
[[nodiscard]] std::vector<std::uint8_t> renderOplWritesToWave(
    std::span<const OplWrite> writes,
    std::uint32_t durationMilliseconds,
    std::uint32_t releaseTailMilliseconds = 300);
