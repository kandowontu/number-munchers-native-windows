#include "super_game.h"

#include <algorithm>
#include <array>
#include <vector>

namespace {

constexpr std::array<int, 16> BasePoints = {
    5, 5, 5, 10, 15, 20, 25, 30,
    35, 40, 45, 50, 55, 60, 65, 70,
};

} // namespace

int SuperGameCore::pointsForLevel(const int level, const int difficulty) {
    if (level > static_cast<int>(BasePoints.size())) return 75;
    const int base = BasePoints[static_cast<std::size_t>(std::max(1, level) - 1)];
    if (difficulty >= 30) return base + 10;
    if (difficulty >= 20) return base + 5;
    return base;
}

bool SuperGameCore::configure(const SuperBoardSettings& settings,
                              const int gameIndex, const bool demo,
                              const bool answerVisionEnabled) {
    configured_ = generator_.configure(settings, gameIndex, demo);
    answerVisionOption_ = answerVisionEnabled;
    demo_ = demo;
    active_ = false;
    boardComplete_ = false;
    maximumScoreReached_ = false;
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    stagedTransformation_ = false;
    clearTransformationState();
    return configured_;
}

bool SuperGameCore::configure(const SuperConfig& config, const int gameIndex,
                              const bool demo,
                              const int playerChosenDifficulty) {
    if (!config.usable()) {
        configured_ = false;
        active_ = false;
        return false;
    }
    return configure(config.boardSettings(playerChosenDifficulty), gameIndex,
                     demo, config.answerVisionEnabled());
}

bool SuperGameCore::start() {
    return startAtLevel(1);
}

bool SuperGameCore::startAtLevel(const int level) {
    if (!configured_) return false;
    scoreState_.reset();
    level_ = std::max(1, level);
    pressureTier_ = std::min(11, level_ - 1);
    boardComplete_ = false;
    maximumScoreReached_ = false;
    active_ = true;
    clearTransformationState();
    // SM 0x08D05 starts Demo at 16/20 so its transformation appears quickly.
    transformationMeter_ = demo_ ? 16 : 0;
    return generateBoard();
}

bool SuperGameCore::generateBoard() {
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    stagedTransformation_ = false;
    transformationCell_ = -1;
    answerVision_ = false;
    superForm_ = false;
    board_ = generator_.nextBoard();
    if (!board_.valid || board_.correctCount < 2) {
        active_ = false;
        return false;
    }
    eaten_.fill(false);
    correctRemaining_ = board_.correctCount;
    boardComplete_ = false;
    return true;
}

bool SuperGameCore::advanceBoard() {
    if (!active_ || !boardComplete_ || maximumScoreReached_) return false;
    ++level_;
    pressureTier_ = std::min(11, pressureTier_ + 1);
    return generateBoard();
}

bool SuperGameCore::beginMunchCell(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || maximumScoreReached_ ||
        cellIndex >= board_.cells.size() || eaten_[cellIndex] ||
        stagedMunchCell_ >= 0) return false;

    stagedTransformation_ =
        transformationCell_ == static_cast<int>(cellIndex);
    if (!stagedTransformation_ &&
        board_.cells[cellIndex].signedSourceIndex == 0) return false;
    stagedMunchCell_ = static_cast<int>(cellIndex);
    stagedMunchValue_ = board_.cells[cellIndex];
    eaten_[cellIndex] = true;
    return true;
}

SuperMunchResolution SuperGameCore::resolveMunchCell(
    const std::size_t cellIndex) {
    SuperMunchResolution result;
    if (cellIndex >= board_.cells.size() ||
        stagedMunchCell_ != static_cast<int>(cellIndex)) return result;

    const bool transformation = stagedTransformation_;
    const SuperBoardCell cell = stagedMunchValue_;
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    stagedTransformation_ = false;
    if (transformation) {
        transformationCell_ = -1;
        superForm_ = true;
        answerVision_ = false;
        result.kind = SuperMunchKind::Transformation;
        result.transformed = true;
        if (correctRemaining_ == 0) {
            boardComplete_ = true;
            result.boardComplete = true;
        }
        return result;
    }

    if (!cell.correct) {
        result.kind = SuperMunchKind::Wrong;
        result.gameOver = loseMuncher();
        return result;
    }

    result.kind = SuperMunchKind::Correct;
    result.points = pointsForLevel(level_, generator_.difficulty());
    --correctRemaining_;
    const MuncherScoreAward award = scoreState_.awardPoints(result.points);
    result.bonusMuncher = award.bonusMuncher;
    result.maximumScore = award.maximumReached;
    if (award.maximumReached) {
        maximumScoreReached_ = true;
        active_ = false;
        return result;
    }
    if (correctRemaining_ == 0) {
        boardComplete_ = true;
        result.boardComplete = true;
    }
    // Image 0x099BB increments after score and the board-empty probe.
    ++transformationMeter_;
    result.transformationReady = transformationMeter_ >= 20 &&
                                 transformationCell_ < 0 && !superForm_;
    return result;
}

SuperMunchResolution SuperGameCore::munchCell(const std::size_t cellIndex) {
    if (!beginMunchCell(cellIndex)) return {};
    return resolveMunchCell(cellIndex);
}

void SuperGameCore::clearTransformationState() {
    transformationMeter_ = 0;
    transformationCell_ = -1;
    superForm_ = false;
    answerVision_ = false;
}

bool SuperGameCore::loseMuncher() {
    if (!active_) return true;
    // SM 0x095C7 resets the entire transformation meter before applying the
    // common reserve-Muncher loss.
    clearTransformationState();
    const bool gameOver = scoreState_.loseMuncher();
    if (gameOver) active_ = false;
    return gameOver;
}

void SuperGameCore::recountCorrectRemaining() {
    correctRemaining_ = 0;
    for (std::size_t index = 0; index < board_.cells.size(); ++index) {
        if (!eaten_[index] && board_.cells[index].correct) ++correctRemaining_;
    }
    if (stagedMunchCell_ >= 0 && !stagedTransformation_ &&
        stagedMunchValue_.correct) ++correctRemaining_;
}

bool SuperGameCore::clearCellWithoutScore(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size() ||
        transformationCell_ == static_cast<int>(cellIndex)) return false;
    eaten_[cellIndex] = true;
    recountCorrectRemaining();
    return true;
}

bool SuperGameCore::restoreCellWithoutRegeneration(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size() ||
        transformationCell_ == static_cast<int>(cellIndex) ||
        board_.cells[cellIndex].signedSourceIndex == 0) return false;
    eaten_[cellIndex] = false;
    recountCorrectRemaining();
    return true;
}

bool SuperGameCore::restoreCell(const std::size_t cellIndex,
                                const SuperBoardCell& cell,
                                const bool eaten) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size() ||
        transformationCell_ == static_cast<int>(cellIndex)) return false;
    board_.cells[cellIndex] = cell;
    eaten_[cellIndex] = eaten;
    recountCorrectRemaining();
    return true;
}

bool SuperGameCore::regenerateCell(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size() ||
        transformationCell_ == static_cast<int>(cellIndex)) return false;
    board_.cells[cellIndex] = generator_.nextCell();
    if (board_.cells[cellIndex].signedSourceIndex == 0) return false;
    eaten_[cellIndex] = false;
    recountCorrectRemaining();
    return true;
}

bool SuperGameCore::completeBoardIfEmpty() {
    if (!active_ || boardComplete_ || correctRemaining_ != 0) return false;
    boardComplete_ = true;
    return true;
}

bool SuperGameCore::ensureTransformationCell(const std::size_t playerCell) {
    if (!active_ || boardComplete_ || superForm_ || transformationCell_ >= 0 ||
        transformationMeter_ < 20 || playerCell >= board_.cells.size()) {
        return false;
    }

    std::vector<int> candidates;
    const auto collect = [&](const int pass) {
        candidates.clear();
        for (int row = 1; row < 4; ++row) {
            for (int column = 1; column < 5; ++column) {
                const int cell = row * 6 + column;
                if (cell == static_cast<int>(playerCell)) continue;
                const SuperBoardCell& value =
                    board_.cells[static_cast<std::size_t>(cell)];
                const bool empty = eaten_[static_cast<std::size_t>(cell)] ||
                                   value.signedSourceIndex == 0;
                if ((pass == 0 && empty) ||
                    (pass == 1 && !empty && value.signedSourceIndex < 0) ||
                    pass == 2) {
                    candidates.push_back(cell);
                }
            }
        }
    };
    collect(0);
    if (candidates.empty()) collect(1);
    if (candidates.empty()) collect(2);
    if (candidates.empty()) return false;

    transformationCell_ = candidates[static_cast<std::size_t>(
        random_.range(static_cast<int>(candidates.size())))];
    SuperBoardCell& replaced =
        board_.cells[static_cast<std::size_t>(transformationCell_)];
    replaced = {};
    eaten_[static_cast<std::size_t>(transformationCell_)] = false;
    recountCorrectRemaining();
    return true;
}

bool SuperGameCore::toggleAnswerVision() {
    if (!active_ || !answerVisionOption_ || !superForm_) return false;
    answerVision_ = !answerVision_;
    return true;
}

bool SuperGameCore::drainTransformationStep() {
    if (!superForm_) return false;
    transformationMeter_ -= answerVision_ ? 2 : 1;
    if (transformationMeter_ < 0) {
        clearTransformationState();
    }
    return true;
}

bool SuperGameCore::defeatTroggle() {
    if (!active_ || !superForm_) return false;
    // The transformed collision branch at 0x0AB10 adds 50 directly rather
    // than entering the correct-answer bonus-Muncher routine.
    scoreState_.restore(scoreState_.score() + 50, scoreState_.reserves());
    return true;
}

bool SuperGameCore::eaten(const std::size_t cellIndex) const {
    return cellIndex < eaten_.size() && eaten_[cellIndex];
}
