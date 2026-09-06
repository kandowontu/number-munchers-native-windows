#include "super_config.h"

#include <algorithm>
#include <cstring>

namespace {

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::string terminatedString(const std::uint8_t* data, const std::size_t size,
                             bool& terminated) {
    const auto* end = std::find(data, data + size, std::uint8_t{});
    terminated = end != data + size;
    return {reinterpret_cast<const char*>(data),
            static_cast<std::size_t>(end - data)};
}

std::string shortString(const std::uint8_t* data, const std::size_t capacity,
                        bool& valid) {
    const std::size_t length = data[0];
    valid = length <= capacity;
    return valid ? std::string(reinterpret_cast<const char*>(data + 1), length)
                 : std::string{};
}

} // namespace

std::string SuperHallEntry::name() const {
    const auto end =
        std::find(nameBytes.begin(), nameBytes.end(), std::uint8_t{});
    return {reinterpret_cast<const char*>(nameBytes.data()),
            static_cast<std::size_t>(end - nameBytes.begin())};
}

std::vector<SuperHallEntry> SuperHallTable::activeEntries() const {
    if (count > Capacity || capacity != Capacity || nameLimit != 25) return {};
    return {entries.begin(), entries.begin() + count};
}

SuperConfig::SuperConfig(const BlobView blob) {
    if (!blob || blob.size != SerializedSize) return;
    std::copy_n(blob.data, bytes_.size(), bytes_.begin());
    if (std::memcmp(bytes_.data(), "MECC", 4) != 0) return;

    version_ = read16(bytes_.data() + 4);
    // SM 0x0D42F accepts only source versions 0 and 1.
    if (version_ > 1) return;
    bool applicationTerminated = false;
    applicationName_ =
        terminatedString(bytes_.data() + 6, 32, applicationTerminated);
    if (!applicationTerminated) return;

    // 0x0CFED copies +28 to the retained Quick Set seed, while 0x0D068
    // copies +26 to the difficulty word that 0x10019 installs as the live
    // selector. The source selectors are 1-based for difficulty.
    gameplayDifficulty_ = read16(bytes_.data() + 0x26);
    quickSet_ = read16(bytes_.data() + 0x28);
    topicMask_ = bytes_[0x2a];
    for (std::size_t topic = 0; topic < TopicCount; ++topic) {
        std::copy_n(bytes_.data() + 0x2b + topic * RuleBytesPerTopic,
                    RuleBytesPerTopic, ruleBits_[topic].begin());
    }
    negations_ = bytes_[0x49];
    answerVision_ = bytes_[0x4a];
    joystickFlags_ = bytes_[0x4c];

    bool hintValid = false;
    hint_ = shortString(bytes_.data() + 0x4d, 58, hintValid);
    bool passwordValid = false;
    password_ = shortString(bytes_.data() + 0x88, 10, passwordValid);
    if (!hintValid || !passwordValid) return;

    joystickCalibration_.verticalLow = read16(bytes_.data() + 0x80);
    joystickCalibration_.verticalHigh = read16(bytes_.data() + 0x82);
    joystickCalibration_.horizontalHigh = read16(bytes_.data() + 0x84);
    joystickCalibration_.horizontalLow = read16(bytes_.data() + 0x86);
    savedState_ = read16(bytes_.data() + 0x94);

    bool hallHeadersValid = true;
    bool hallNamesValid = true;
    for (std::size_t hall = 0; hall < HallCount; ++hall) {
        const std::size_t hallOffset = 0x96 + hall * 0x132;
        SuperHallTable& table = halls_[hall];
        table.count = read16(bytes_.data() + hallOffset);
        table.capacity = read16(bytes_.data() + hallOffset + 2);
        table.nameLimit = read16(bytes_.data() + hallOffset + 4);
        hallHeadersValid = hallHeadersValid &&
            table.count <= SuperHallTable::Capacity &&
            table.capacity == SuperHallTable::Capacity &&
            table.nameLimit == 25;
        for (std::size_t entryIndex = 0;
             entryIndex < SuperHallTable::Capacity; ++entryIndex) {
            const std::size_t entryOffset = hallOffset + 6 + entryIndex * 30;
            SuperHallEntry& entry = table.entries[entryIndex];
            std::copy_n(bytes_.data() + entryOffset, entry.nameBytes.size(),
                        entry.nameBytes.begin());
            entry.score = read32(bytes_.data() + entryOffset + 26);
            hallNamesValid = hallNamesValid &&
                std::find(entry.nameBytes.begin(), entry.nameBytes.end(),
                          std::uint8_t{}) != entry.nameBytes.end();
        }
    }

    valid_ = true;
    const bool topicSelected = (topicMask_ & 0xfcu) != 0;
    bool selectedTopicHasRule = false;
    for (std::size_t topic = 0; topic < TopicCount; ++topic) {
        if (!this->topicSelected(topic)) continue;
        selectedTopicHasRule = selectedTopicHasRule ||
            std::any_of(ruleBits_[topic].begin(), ruleBits_[topic].end(),
                        [](const std::uint8_t value) { return value != 0; });
    }
    contentValid_ = quickSet_ <= 5 && gameplayDifficulty_ >= 1 &&
                    gameplayDifficulty_ <= 4 &&
                    negations_ <= 1 && answerVision_ <= 1 && topicSelected &&
                    selectedTopicHasRule;
    hallsValid_ = hallHeadersValid && hallNamesValid;
}

bool SuperConfig::topicSelected(const std::size_t topic) const {
    return topic < TopicCount &&
           (topicMask_ & static_cast<std::uint8_t>(0x80u >> topic)) != 0;
}

bool SuperConfig::ruleSelected(const std::size_t topic,
                               const std::size_t rule) const {
    if (topic >= TopicCount || rule >= SuperMaximumRulesPerTopic) return false;
    return (ruleBits_[topic][rule / 8] &
            static_cast<std::uint8_t>(0x80u >> (rule % 8))) != 0;
}

SuperBoardSettings SuperConfig::boardSettings(
    const int playerChosenDifficulty) const {
    SuperBoardSettings settings;
    for (std::size_t topic = 0; topic < TopicCount; ++topic) {
        settings.selectedTopics[topic] = topicSelected(topic);
        for (std::size_t rule = 0; rule < SuperMaximumRulesPerTopic; ++rule) {
            settings.selectedRules[topic][rule] = ruleSelected(topic, rule);
        }
    }
    static constexpr std::array<int, 3> Difficulties = {10, 20, 30};
    if (gameplayDifficulty_ >= 1 && gameplayDifficulty_ <= Difficulties.size()) {
        settings.difficulty = Difficulties[gameplayDifficulty_ - 1];
    } else if (gameplayDifficulty_ == 4 &&
               (playerChosenDifficulty == 10 || playerChosenDifficulty == 20 ||
                playerChosenDifficulty == 30)) {
        settings.difficulty = playerChosenDifficulty;
    } else {
        settings.difficulty = 20;
    }
    settings.negations = negationsEnabled();
    return settings;
}
