#pragma once

#include "muncher_score.h"
#include "super_board.h"
#include "super_config.h"

#include <array>
#include <cstddef>

enum class SuperMunchKind {
    Invalid,
    Correct,
    Wrong,
    Transformation,
};

struct SuperMunchResolution {
    SuperMunchKind kind{SuperMunchKind::Invalid};
    int points{};
    bool bonusMuncher{};
    bool transformationReady{};
    bool transformed{};
    bool boardComplete{};
    bool maximumScore{};
    bool gameOver{};
};

// Headless owner for the recovered Super board, score, life, and
// transformation lifecycle. Actor scheduling and presentation remain in the
// runtime host, but all cell mutations go through this owner.
class SuperGameCore {
public:
    SuperGameCore(const SuperContent& content, OriginalRandom& random)
        : generator_(content, random), random_(random) {}

    [[nodiscard]] bool configure(const SuperBoardSettings& settings,
                                 int gameIndex, bool demo = false,
                                 bool answerVisionEnabled = true);
    [[nodiscard]] bool configure(const SuperConfig& config, int gameIndex,
                                 bool demo = false,
                                 int playerChosenDifficulty = 20);
    [[nodiscard]] bool start();
    [[nodiscard]] bool startAtLevel(int level);
    [[nodiscard]] bool advanceBoard();

    [[nodiscard]] bool beginMunchCell(std::size_t cellIndex);
    [[nodiscard]] SuperMunchResolution resolveMunchCell(
        std::size_t cellIndex);
    [[nodiscard]] SuperMunchResolution munchCell(std::size_t cellIndex);
    [[nodiscard]] bool loseMuncher();
    [[nodiscard]] bool clearCellWithoutScore(std::size_t cellIndex);
    [[nodiscard]] bool restoreCellWithoutRegeneration(std::size_t cellIndex);
    [[nodiscard]] bool restoreCell(std::size_t cellIndex,
                                   const SuperBoardCell& cell, bool eaten);
    [[nodiscard]] bool regenerateCell(std::size_t cellIndex);
    [[nodiscard]] bool completeBoardIfEmpty();

    // Image 0x089E5 selects an interior empty cell, then an interior wrong
    // cell, then any interior cell, always excluding the player's cell.
    [[nodiscard]] bool ensureTransformationCell(std::size_t playerCell);
    [[nodiscard]] bool toggleAnswerVision();
    // One callback consumes one meter unit, or two while Answer Vision is on.
    // The form ends only after the recovered subtraction becomes negative.
    [[nodiscard]] bool drainTransformationStep();
    [[nodiscard]] bool defeatTroggle();

    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool boardComplete() const { return boardComplete_; }
    [[nodiscard]] bool maximumScoreReached() const {
        return maximumScoreReached_;
    }
    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] int pressureTier() const { return pressureTier_; }
    [[nodiscard]] int correctRemaining() const { return correctRemaining_; }
    [[nodiscard]] int transformationMeter() const {
        return transformationMeter_;
    }
    [[nodiscard]] int transformationCell() const {
        return transformationCell_;
    }
    [[nodiscard]] bool superForm() const { return superForm_; }
    [[nodiscard]] bool answerVision() const { return answerVision_; }
    [[nodiscard]] bool actionFrozen() const { return answerVision_; }
    [[nodiscard]] bool eaten(std::size_t cellIndex) const;
    [[nodiscard]] const SuperBoard& board() const { return board_; }
    [[nodiscard]] const MuncherScore& scoreState() const { return scoreState_; }
    [[nodiscard]] static int pointsForLevel(int level, int difficulty);

private:
    [[nodiscard]] bool generateBoard();
    void recountCorrectRemaining();
    void clearTransformationState();

    SuperBoardGenerator generator_;
    OriginalRandom& random_;
    MuncherScore scoreState_;
    SuperBoard board_;
    std::array<bool, SuperBoardCellCount> eaten_{};
    int stagedMunchCell_{-1};
    SuperBoardCell stagedMunchValue_{};
    bool stagedTransformation_{};
    int level_{1};
    int pressureTier_{};
    int correctRemaining_{};
    int transformationMeter_{};
    int transformationCell_{-1};
    bool answerVisionOption_{true};
    bool superForm_{};
    bool answerVision_{};
    bool demo_{};
    bool configured_{};
    bool active_{};
    bool boardComplete_{};
    bool maximumScoreReached_{};

    friend struct SuperGameTestAccess;
};
