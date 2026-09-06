#pragma once

#include "muncher_score.h"
#include "word_config.h"
#include "word_content.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct WordHallScoreEntry {
    std::string name;
    std::uint32_t score{};

    bool operator==(const WordHallScoreEntry&) const = default;
};

class WordHall {
public:
    static constexpr std::uint32_t MinimumScore = 50;
    static constexpr std::size_t Capacity = 10;
    static constexpr std::size_t NameLimit = 25;

    [[nodiscard]] bool load(const WordConfig& config);
    [[nodiscard]] int insertionRank(std::uint32_t score) const;
    [[nodiscard]] bool qualifies(std::uint32_t score) const {
        return insertionRank(score) >= 0;
    }
    [[nodiscard]] bool insert(std::uint32_t score, std::string name);
    [[nodiscard]] bool replace(std::vector<WordHallScoreEntry> entries);
    [[nodiscard]] const std::vector<WordHallScoreEntry>& entries() const { return entries_; }

private:
    std::vector<WordHallScoreEntry> entries_;
};

enum class WordMunchKind {
    Invalid,
    Correct,
    Wrong,
};

struct WordMunchResolution {
    WordMunchKind kind{WordMunchKind::Invalid};
    int points{};
    bool bonusMuncher{};
    bool boardComplete{};
    bool maximumScore{};
    bool gameOver{};
    int cartoonScene{-1};
};

// Headless owner for the recovered Word board/scoring lifecycle. Presentation,
// movement, enemy jobs, and scene hosting stay outside this class so their DOS
// timing can be integrated independently rather than approximated here.
class WordGameCore {
public:
    WordGameCore(const WordList& words, OriginalRandom& random)
        : generator_(words, random), random_(random) {}

    [[nodiscard]] bool configure(const std::array<bool, 20>& selectedSounds,
                                 int wordDifficulty);
    [[nodiscard]] bool configure(const WordConfig& config);
    [[nodiscard]] bool start(bool demo = false);
    // Non-original developer entry used by the requested level-select overlay.
    // Ordinary play always calls start() and therefore begins at level 1.
    [[nodiscard]] bool startAtLevel(int level, bool demo = false);
    [[nodiscard]] bool advanceBoard();
    [[nodiscard]] bool finishCartoon();
    [[nodiscard]] bool beginMunchCell(std::size_t cellIndex);
    [[nodiscard]] WordMunchResolution resolveMunchCell(std::size_t cellIndex);
    [[nodiscard]] WordMunchResolution munchCell(std::size_t cellIndex);
    [[nodiscard]] bool loseMuncher();
    [[nodiscard]] bool clearCellWithoutScore(std::size_t cellIndex);
    [[nodiscard]] bool regenerateCell(std::size_t cellIndex);
    [[nodiscard]] bool restoreCell(std::size_t cellIndex, const WordBoardCell& cell,
                                   bool eaten);
    [[nodiscard]] bool completeBoardIfEmpty();

    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool boardComplete() const { return boardComplete_; }
    [[nodiscard]] bool maximumScoreReached() const { return maximumScoreReached_; }
    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] int pressureTier() const { return pressureTier_; }
    [[nodiscard]] int correctRemaining() const { return correctRemaining_; }
    [[nodiscard]] bool cartoonPending() const { return pendingCartoonScene_ >= 0; }
    [[nodiscard]] int pendingCartoonScene() const { return pendingCartoonScene_; }
    [[nodiscard]] int cartoonOrderIndex() const { return cartoonOrderIndex_; }
    [[nodiscard]] const std::array<int, 5>& cartoonOrder() const { return cartoonOrder_; }
    [[nodiscard]] const WordBoard& board() const { return board_; }
    [[nodiscard]] bool eaten(std::size_t cellIndex) const;
    [[nodiscard]] const MuncherScore& scoreState() const { return scoreState_; }

private:
    [[nodiscard]] bool generateBoard(bool demo);
    void prepareCartoonForCompletedLevel();
    void recountCorrectRemaining();

    WordBoardGenerator generator_;
    OriginalRandom& random_;
    MuncherScore scoreState_;
    WordBoard board_;
    std::array<bool, 30> eaten_{};
    int stagedMunchCell_{-1};
    WordBoardCell stagedMunchValue_{};
    int level_{1};
    int pressureTier_{};
    int correctRemaining_{};
    std::array<int, 5> cartoonOrder_{0, 1, 2, 3, 4};
    int cartoonOrderIndex_{};
    int pendingCartoonScene_{-1};
    bool configured_{};
    bool active_{};
    bool boardComplete_{};
    bool maximumScoreReached_{};
    bool demo_{};
};
