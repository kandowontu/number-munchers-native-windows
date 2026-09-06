#include "dro_audio.h"

#include "ymfm_opl.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace {

constexpr std::uint32_t AdLibClock = 3'579'545;

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void write16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

struct DroHeader {
    std::uint32_t pairCount{};
    std::uint32_t durationMilliseconds{};
    std::uint8_t hardwareType{};
    std::uint8_t shortDelayCode{};
    std::uint8_t longDelayCode{};
    std::span<const std::uint8_t> codeMap;
    std::span<const std::uint8_t> pairs;
};

bool parseHeader(std::span<const std::uint8_t> bytes, DroHeader& result) {
    constexpr std::array<std::uint8_t, 8> signature{'D', 'B', 'R', 'A', 'W', 'O', 'P', 'L'};
    if (bytes.size() < 26 || !std::equal(signature.begin(), signature.end(), bytes.begin()) ||
        read32(bytes.data() + 8) != 2) {
        return false;
    }

    result.pairCount = read32(bytes.data() + 12);
    result.durationMilliseconds = read32(bytes.data() + 16);
    result.hardwareType = bytes[20];
    const std::uint8_t format = bytes[21];
    const std::uint8_t compression = bytes[22];
    result.shortDelayCode = bytes[23];
    result.longDelayCode = bytes[24];
    const std::size_t codeMapLength = bytes[25];
    const std::size_t dataOffset = 26 + codeMapLength;
    const std::uint64_t pairBytes = static_cast<std::uint64_t>(result.pairCount) * 2;
    if (format != 0 || compression != 0 || result.hardwareType != 0 ||
        dataOffset > bytes.size() || pairBytes > bytes.size() - dataOffset) {
        return false;
    }
    result.codeMap = bytes.subspan(26, codeMapLength);
    result.pairs = bytes.subspan(dataOffset, static_cast<std::size_t>(pairBytes));
    return true;
}

bool decodeRegisterCode(const DroHeader& header, std::uint8_t code, std::uint8_t& reg) {
    // Version 2 uses the high bit for the second OPL3 register bank. These
    // captures declare OPL2 hardware, so only the first bank is valid.
    if ((code & 0x80u) != 0) return false;
    if (code >= header.codeMap.size()) return false;
    reg = header.codeMap[code];
    return true;
}

class Ym3812Interface final : public ymfm::ymfm_interface {};

} // namespace

DroInfo inspectDro(std::span<const std::uint8_t> bytes) {
    DroHeader header;
    if (!parseHeader(bytes, header)) return {};

    std::uint64_t elapsed = 0;
    std::uint32_t writes = 0;
    for (std::size_t position = 0; position < header.pairs.size(); position += 2) {
        const std::uint8_t code = header.pairs[position];
        const std::uint8_t value = header.pairs[position + 1];
        if (code == header.shortDelayCode) {
            elapsed += static_cast<std::uint32_t>(value) + 1;
        } else if (code == header.longDelayCode) {
            elapsed += (static_cast<std::uint32_t>(value) + 1) * 256;
        } else {
            std::uint8_t reg = 0;
            if (!decodeRegisterCode(header, code, reg)) return {};
            (void)reg;
            ++writes;
        }
    }
    if (elapsed != header.durationMilliseconds) return {};

    return {true, header.durationMilliseconds, header.pairCount, writes, header.hardwareType};
}

std::vector<std::uint8_t> renderDroToWave(std::span<const std::uint8_t> bytes,
                                          std::uint32_t releaseTailMilliseconds) {
    const OplSound sound = decodeDroSound(bytes);
    if (!sound.valid) return {};
    return renderOplWritesToWave(sound.writes, sound.durationMilliseconds,
                                 releaseTailMilliseconds);
}

OplSound decodeDroSound(const std::span<const std::uint8_t> bytes) {
    DroHeader header;
    if (!parseHeader(bytes, header)) return {};

    OplSound sound;
    sound.writes.reserve(header.pairs.size() / 2);
    std::uint64_t elapsedMilliseconds = 0;
    for (std::size_t position = 0; position < header.pairs.size(); position += 2) {
        const std::uint8_t code = header.pairs[position];
        const std::uint8_t value = header.pairs[position + 1];
        if (code == header.shortDelayCode) {
            elapsedMilliseconds += static_cast<std::uint32_t>(value) + 1;
        } else if (code == header.longDelayCode) {
            elapsedMilliseconds += (static_cast<std::uint32_t>(value) + 1) * 256;
        } else {
            std::uint8_t reg = 0;
            if (!decodeRegisterCode(header, code, reg) ||
                elapsedMilliseconds > std::numeric_limits<std::uint32_t>::max()) return {};
            sound.writes.push_back(
                {static_cast<std::uint32_t>(elapsedMilliseconds), reg, value});
        }
    }
    if (elapsedMilliseconds != header.durationMilliseconds) return {};
    sound.valid = true;
    sound.durationMilliseconds = header.durationMilliseconds;
    return sound;
}

std::vector<std::uint8_t> renderOplWritesToWave(std::span<const OplWrite> writes,
                                               std::uint32_t durationMilliseconds,
                                               std::uint32_t releaseTailMilliseconds) {
    std::uint32_t previousTimestamp = 0;
    for (const OplWrite& write : writes) {
        if (write.milliseconds < previousTimestamp || write.milliseconds > durationMilliseconds) return {};
        previousTimestamp = write.milliseconds;
    }

    Ym3812Interface interface;
    ymfm::ym3812 chip(interface);
    chip.reset();
    const std::uint32_t sampleRate = chip.sample_rate(AdLibClock);
    if (sampleRate == 0) return {};

    const std::uint64_t expectedSamples =
        static_cast<std::uint64_t>(durationMilliseconds + releaseTailMilliseconds) *
        sampleRate / 1000 + 1;
    if (expectedSamples > std::numeric_limits<std::uint32_t>::max() / sizeof(std::int16_t)) return {};
    std::vector<std::int16_t> samples;
    samples.reserve(static_cast<std::size_t>(expectedSamples));

    auto generateUntil = [&](std::uint64_t targetMilliseconds) {
        const std::uint64_t targetSample = targetMilliseconds * sampleRate / 1000;
        while (samples.size() < targetSample) {
            ymfm::ym3812::output_data output;
            chip.generate(&output);
            output.clamp16();
            samples.push_back(static_cast<std::int16_t>(output.data[0]));
        }
    };

    for (const OplWrite& write : writes) {
        generateUntil(write.milliseconds);
        chip.write_address(write.reg);
        chip.write_data(write.value);
    }
    generateUntil(static_cast<std::uint64_t>(durationMilliseconds) + releaseTailMilliseconds);

    const std::size_t dataBytes = samples.size() * sizeof(std::int16_t);
    if (dataBytes > std::numeric_limits<std::uint32_t>::max() - 36) return {};
    std::vector<std::uint8_t> wave(44 + dataBytes);
    std::memcpy(wave.data(), "RIFF", 4);
    write32(wave, 4, static_cast<std::uint32_t>(36 + dataBytes));
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    write32(wave, 16, 16);
    write16(wave, 20, 1);
    write16(wave, 22, 1);
    write32(wave, 24, sampleRate);
    write32(wave, 28, sampleRate * sizeof(std::int16_t));
    write16(wave, 32, sizeof(std::int16_t));
    write16(wave, 34, 16);
    std::memcpy(wave.data() + 36, "data", 4);
    write32(wave, 40, static_cast<std::uint32_t>(dataBytes));
    std::memcpy(wave.data() + 44, samples.data(), dataBytes);
    return wave;
}
