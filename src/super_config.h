#pragma once

#include "assets.h"
#include "super_board.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SuperHallEntry {
    std::array<std::uint8_t, 26> nameBytes{};
    std::uint32_t score{};

    [[nodiscard]] std::string name() const;
};

struct SuperHallTable {
    static constexpr std::size_t Capacity = 10;

    std::uint16_t count{};
    std::uint16_t capacity{};
    std::uint16_t nameLimit{};
    std::array<SuperHallEntry, Capacity> entries{};

    [[nodiscard]] std::vector<SuperHallEntry> activeEntries() const;
};

struct SuperJoystickCalibration {
    std::uint16_t verticalLow{};
    std::uint16_t verticalHigh{};
    std::uint16_t horizontalHigh{};
    std::uint16_t horizontalLow{};
};

// Lossless, bounds-checked owner of the original 2,292-byte SM.CFG image.
// Seven independent 306-byte Hall tables follow the common options header:
// one for each category and one for Challenge.
class SuperConfig {
public:
    static constexpr std::size_t SerializedSize = 0x8f4;
    static constexpr std::size_t TopicCount = 6;
    static constexpr std::size_t RuleBytesPerTopic = 5;
    static constexpr std::size_t HallCount = 7;

    explicit SuperConfig(BlobView blob = {});

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool contentValid() const { return contentValid_; }
    [[nodiscard]] bool hallsValid() const { return hallsValid_; }
    [[nodiscard]] bool usable() const {
        return valid_ && contentValid_ && hallsValid_;
    }

    [[nodiscard]] std::uint16_t version() const { return version_; }
    [[nodiscard]] const std::string& applicationName() const {
        return applicationName_;
    }
    [[nodiscard]] std::uint16_t quickSet() const { return quickSet_; }
    [[nodiscard]] std::uint16_t gameplayDifficulty() const {
        return gameplayDifficulty_;
    }
    [[nodiscard]] std::uint8_t topicMask() const { return topicMask_; }
    [[nodiscard]] bool topicSelected(std::size_t topic) const;
    [[nodiscard]] bool ruleSelected(std::size_t topic,
                                    std::size_t rule) const;
    [[nodiscard]] bool negationsEnabled() const { return negations_ != 0; }
    [[nodiscard]] bool answerVisionEnabled() const {
        return answerVision_ != 0;
    }
    [[nodiscard]] std::uint8_t joystickFlags() const { return joystickFlags_; }
    [[nodiscard]] bool joystickEnabled() const {
        return (joystickFlags_ & 1u) != 0;
    }
    [[nodiscard]] const std::string& hint() const { return hint_; }
    [[nodiscard]] const std::string& password() const { return password_; }
    [[nodiscard]] const SuperJoystickCalibration& joystickCalibration() const {
        return joystickCalibration_;
    }
    [[nodiscard]] std::uint16_t savedState() const { return savedState_; }
    [[nodiscard]] const std::array<SuperHallTable, HallCount>& halls() const {
        return halls_;
    }

    // Player Chooses (persisted source value 4) requires a caller-supplied
    // choice of 10, 20, or 30. Source values 1..3 map directly.
    [[nodiscard]] SuperBoardSettings boardSettings(
        int playerChosenDifficulty = 20) const;

    [[nodiscard]] const std::array<std::uint8_t, SerializedSize>& serialized()
        const { return bytes_; }

private:
    std::array<std::uint8_t, SerializedSize> bytes_{};
    std::uint16_t version_{};
    std::string applicationName_;
    std::uint16_t quickSet_{};
    std::uint16_t gameplayDifficulty_{};
    std::uint8_t topicMask_{};
    std::array<std::array<std::uint8_t, RuleBytesPerTopic>, TopicCount>
        ruleBits_{};
    std::uint8_t negations_{};
    std::uint8_t answerVision_{};
    std::uint8_t joystickFlags_{};
    std::string hint_;
    std::string password_;
    SuperJoystickCalibration joystickCalibration_{};
    std::uint16_t savedState_{};
    std::array<SuperHallTable, HallCount> halls_{};
    bool valid_{};
    bool contentValid_{};
    bool hallsValid_{};
};
