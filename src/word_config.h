#pragma once

#include "assets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct WordHallEntry {
    std::array<std::uint8_t, 26> nameBytes{};
    std::uint32_t score{};

    [[nodiscard]] std::string name() const;
};

struct WordJoystickCalibration {
    std::uint16_t verticalLow{};
    std::uint16_t verticalHigh{};
    std::uint16_t horizontalHigh{};
    std::uint16_t horizontalLow{};
};

// Lossless, bounds-checked view of the original 636-byte WM.CFG structure.
// Unknown and inactive bytes are retained so parsing and serializing an
// unedited source file is byte-exact.
class WordConfig {
public:
    static constexpr std::size_t SerializedSize = 0x27c;
    static constexpr std::size_t SoundCount = 20;
    static constexpr std::size_t HallCapacity = 10;

    explicit WordConfig(BlobView blob = {});

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool contentValid() const { return contentValid_; }
    [[nodiscard]] bool hallValid() const { return hallValid_; }
    [[nodiscard]] bool usable() const { return valid_ && contentValid_ && hallValid_; }

    [[nodiscard]] std::uint16_t version() const { return version_; }
    [[nodiscard]] const std::string& applicationName() const { return applicationName_; }
    [[nodiscard]] std::uint16_t difficulty() const { return difficulty_; }
    [[nodiscard]] const std::array<std::uint8_t, SoundCount>& selectedSounds() const {
        return selectedSounds_;
    }
    [[nodiscard]] std::uint8_t joystickFlags() const { return joystickFlags_; }
    [[nodiscard]] bool joystickEnabled() const { return (joystickFlags_ & 1u) != 0; }
    [[nodiscard]] const std::string& password() const { return password_; }
    [[nodiscard]] const std::string& hint() const { return hint_; }
    [[nodiscard]] const WordJoystickCalibration& joystickCalibration() const {
        return joystickCalibration_;
    }
    [[nodiscard]] std::uint16_t savedState() const { return savedState_; }
    [[nodiscard]] std::uint16_t hallCount() const { return hallCount_; }
    [[nodiscard]] std::uint16_t hallCapacity() const { return hallCapacity_; }
    [[nodiscard]] std::uint16_t hallNameLimit() const { return hallNameLimit_; }
    [[nodiscard]] const std::array<WordHallEntry, HallCapacity>& hallEntries() const {
        return hallEntries_;
    }
    [[nodiscard]] std::vector<WordHallEntry> activeHallEntries() const;

    [[nodiscard]] const std::array<std::uint8_t, SerializedSize>& serialized() const {
        return bytes_;
    }

private:
    std::array<std::uint8_t, SerializedSize> bytes_{};
    std::uint16_t version_{};
    std::string applicationName_;
    std::uint16_t difficulty_{};
    std::array<std::uint8_t, SoundCount> selectedSounds_{};
    std::uint8_t joystickFlags_{};
    std::string password_;
    std::string hint_;
    WordJoystickCalibration joystickCalibration_{};
    std::uint16_t savedState_{};
    std::uint16_t hallCount_{};
    std::uint16_t hallCapacity_{};
    std::uint16_t hallNameLimit_{};
    std::array<WordHallEntry, HallCapacity> hallEntries_{};
    bool valid_{};
    bool contentValid_{};
    bool hallValid_{};
};
