#include "super_board.h"

#include <algorithm>

bool SuperBoardGenerator::addTopic(const int topic) {
    if (topic < 0 || topic >= static_cast<int>(SuperTopicCount) ||
        !settings_.selectedTopics[static_cast<std::size_t>(topic)]) return true;

    const SuperCategorySet* set = content_.set(static_cast<std::size_t>(topic));
    if (!set || set->rules.size() > SuperMaximumRulesPerTopic) return false;
    for (std::size_t rule = 0; rule < set->rules.size(); ++rule) {
        if (settings_.selectedRules[static_cast<std::size_t>(topic)][rule] &&
            set->rules[rule].minimumDifficulty() <= difficulty_) {
            eligibleRules_.push_back(
                {topic, static_cast<int>(rule)});
        }
    }
    return true;
}

bool SuperBoardGenerator::configure(const SuperBoardSettings& settings,
                                    const int gameIndex, const bool demo) {
    configured_ = false;
    settings_ = settings;
    eligibleRules_.clear();
    currentCorrectPool_.clear();
    currentWrongPool_.clear();
    ruleCursor_ = 0xf0;
    demo_ = demo;

    if (!content_.valid() || gameIndex < 0 || gameIndex > 6 ||
        (settings.difficulty != 10 && settings.difficulty != 20 &&
         settings.difficulty != 30)) return false;

    // SM 0x10BA8 consumes this draw before it scans the eligible-rule table.
    difficulty_ = demo ? (random_.range(2) == 0 ? 10 : 20)
                       : settings.difficulty;

    if (demo || gameIndex >= static_cast<int>(SuperTopicCount)) {
        for (int topic = 0; topic < static_cast<int>(SuperTopicCount); ++topic) {
            if (!addTopic(topic)) return false;
        }
    } else if (!addTopic(gameIndex)) {
        return false;
    }

    configured_ = !eligibleRules_.empty();
    return configured_;
}

void SuperBoardGenerator::shuffleEligibleRules() {
    const int count = static_cast<int>(eligibleRules_.size());
    // SM 0x1105D takes exactly one full-range draw for every table entry. A
    // self draw is merely skipped; it is not retried as it is in Word Munchers.
    for (int index = 0; index < count; ++index) {
        const int other = random_.range(count);
        if (other != index) {
            std::swap(eligibleRules_[static_cast<std::size_t>(index)],
                      eligibleRules_[static_cast<std::size_t>(other)]);
        }
    }
}

bool SuperBoardGenerator::buildPool(const SuperCategorySet& set, const int rule,
                                    const SuperMembership membership,
                                    std::vector<std::string>& destination) {
    destination.clear();
    if (rule < 0 || rule >= static_cast<int>(set.rules.size()) ||
        set.words.empty()) return false;

    // The original UI prevents a content selection with no usable member.
    // Retain that corruption defense without consuming a speculative PRNG draw.
    const auto usable = [&](const SuperWord& word) {
        return word.difficultyRank <= difficulty_ &&
               word.membership[static_cast<std::size_t>(rule)] == membership;
    };
    if (std::none_of(set.words.begin(), set.words.end(), usable)) return false;

    destination.reserve(SuperPoolSize);
    // SM 0x10E64 performs rejection sampling over the complete word list until
    // exactly 60 fixed records have been copied. Duplicates are intentional.
    while (destination.size() < SuperPoolSize) {
        const int source = random_.range(static_cast<int>(set.words.size()));
        const SuperWord& word = set.words[static_cast<std::size_t>(source)];
        if (usable(word)) destination.push_back(word.text);
    }
    return true;
}

SuperBoard SuperBoardGenerator::nextBoard() {
    SuperBoard board;
    if (!configured_ || eligibleRules_.empty()) return board;

    if (demo_) {
        ruleCursor_ = random_.range(static_cast<int>(eligibleRules_.size()));
    } else {
        ++ruleCursor_;
        if (ruleCursor_ >= static_cast<int>(eligibleRules_.size())) {
            ruleCursor_ = 0;
            shuffleEligibleRules();
        }
    }

    const SuperRuleReference selection =
        eligibleRules_[static_cast<std::size_t>(ruleCursor_)];
    const SuperCategorySet* set =
        content_.set(static_cast<std::size_t>(selection.topic));
    if (!set || selection.rule < 0 ||
        selection.rule >= static_cast<int>(set->rules.size())) return board;
    const SuperRule& rule = set->rules[static_cast<std::size_t>(selection.rule)];

    // The random negation call is conditional on both the global option and
    // the rule's source flag. Pool generation always samples Match first and
    // NonMatch second; negation swaps their destinations only afterwards.
    board.negated = settings_.negations && rule.negatable() &&
                    random_.range(2) != 0;
    std::vector<std::string> matchPool;
    std::vector<std::string> nonMatchPool;
    if (!buildPool(*set, selection.rule, SuperMembership::Match, matchPool) ||
        !buildPool(*set, selection.rule, SuperMembership::NonMatch,
                   nonMatchPool)) return board;
    if (board.negated) {
        currentCorrectPool_ = std::move(nonMatchPool);
        currentWrongPool_ = std::move(matchPool);
        board.displayedRule = "not ";
    } else {
        currentCorrectPool_ = std::move(matchPool);
        currentWrongPool_ = std::move(nonMatchPool);
    }
    board.displayedRule += rule.text;

    do {
        board.correctCount = 0;
        for (SuperBoardCell& cell : board.cells) {
            cell = nextCell();
            if (cell.correct) ++board.correctCount;
        }
    } while (board.correctCount < 2);

    board.valid = true;
    board.selection = selection;
    board.selectionIndex = ruleCursor_;
    board.correctPoolCount = static_cast<int>(currentCorrectPool_.size());
    board.wrongPoolCount = static_cast<int>(currentWrongPool_.size());
    return board;
}

SuperBoardCell SuperBoardGenerator::nextCell() {
    SuperBoardCell cell;
    if (currentCorrectPool_.empty() || currentWrongPool_.empty()) return cell;
    if (random_.range(2) != 0) {
        const int source = random_.range(static_cast<int>(currentCorrectPool_.size()));
        cell.text = currentCorrectPool_[static_cast<std::size_t>(source)];
        cell.correct = true;
        cell.signedSourceIndex = source + 1;
    } else {
        const int source = random_.range(static_cast<int>(currentWrongPool_.size()));
        cell.text = currentWrongPool_[static_cast<std::size_t>(source)];
        cell.correct = false;
        cell.signedSourceIndex = -(source + 1);
    }
    return cell;
}
