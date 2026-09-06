#pragma once

#include "original_random.h"
#include "super_content.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

constexpr std::size_t SuperTopicCount = 6;
constexpr std::size_t SuperMaximumRulesPerTopic = 40;
constexpr std::size_t SuperBoardCellCount = 30;
constexpr std::size_t SuperPoolSize = 60;

struct SuperBoardSettings {
    std::array<bool, SuperTopicCount> selectedTopics{};
    std::array<std::array<bool, SuperMaximumRulesPerTopic>,
               SuperTopicCount> selectedRules{};
    int difficulty{20};
    bool negations{};
};

struct SuperRuleReference {
    int topic{-1};
    int rule{-1};

    bool operator==(const SuperRuleReference&) const = default;
};

struct SuperBoardCell {
    std::string text;
    bool correct{};
    // The DOS controller stores a one-based index into the active 60-record
    // pool: positive for an answer and negative for a distractor.
    int signedSourceIndex{};

    bool operator==(const SuperBoardCell&) const = default;
};

struct SuperBoard {
    bool valid{};
    SuperRuleReference selection{};
    int selectionIndex{-1};
    bool negated{};
    std::string displayedRule;
    int correctCount{};
    int correctPoolCount{};
    int wrongPoolCount{};
    std::array<SuperBoardCell, SuperBoardCellCount> cells{};
};

class SuperBoardGenerator {
public:
    SuperBoardGenerator(const SuperContent& content, OriginalRandom& random)
        : content_(content), random_(random) {}

    // gameIndex 0..5 restricts play to one category. Index 6 is Challenge and
    // combines every enabled category. Demo likewise combines the categories
    // but replaces the configured difficulty with a random 10/20 choice.
    [[nodiscard]] bool configure(const SuperBoardSettings& settings,
                                 int gameIndex, bool demo);
    [[nodiscard]] SuperBoard nextBoard();
    // Transformation trails replace cells from the same two live pools and
    // therefore must not advance the rule table.
    [[nodiscard]] SuperBoardCell nextCell();

    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] bool demo() const { return demo_; }
    [[nodiscard]] int difficulty() const { return difficulty_; }
    [[nodiscard]] const std::vector<SuperRuleReference>& eligibleRules() const {
        return eligibleRules_;
    }

private:
    [[nodiscard]] bool addTopic(int topic);
    void shuffleEligibleRules();
    [[nodiscard]] bool buildPool(const SuperCategorySet& set, int rule,
                                 SuperMembership membership,
                                 std::vector<std::string>& destination);

    const SuperContent& content_;
    OriginalRandom& random_;
    SuperBoardSettings settings_{};
    std::vector<SuperRuleReference> eligibleRules_;
    std::vector<std::string> currentCorrectPool_;
    std::vector<std::string> currentWrongPool_;
    int difficulty_{};
    int ruleCursor_{0xf0};
    bool demo_{};
    bool configured_{};
};
