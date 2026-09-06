#pragma once

#include "assets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class SuperMembership : std::uint8_t {
    Unavailable = 0,
    Match = 1,
    NonMatch = 2,
};

struct SuperRule {
    std::string text;
    // 0: rule may be negated; 1: content quick-set grade threshold;
    // 2: minimum gameplay difficulty (10/20/30); 3: source workspace residue.
    // The fourth byte is retained losslessly even though the board controller
    // does not consume it.
    std::array<std::uint8_t, 4> metadata{};

    [[nodiscard]] bool negatable() const { return metadata[0] != 0; }
    [[nodiscard]] std::uint8_t quickSetThreshold() const { return metadata[1]; }
    [[nodiscard]] std::uint8_t minimumDifficulty() const { return metadata[2]; }
};

struct SuperWord {
    std::string text;
    std::uint8_t difficultyRank{}; // source values are exactly 10, 20, or 30
    std::vector<SuperMembership> membership;
};

struct SuperCategorySet {
    std::string title;
    std::vector<SuperRule> rules;
    std::vector<SuperWord> words;
    std::uint8_t wordListTrailer{};
};

class SuperContent {
public:
    explicit SuperContent(const ResArchive& archive);

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const std::array<SuperCategorySet, 6>& sets() const { return sets_; }
    [[nodiscard]] const SuperCategorySet* set(std::size_t index) const;

private:
    std::array<SuperCategorySet, 6> sets_{};
    bool valid_{};
};
