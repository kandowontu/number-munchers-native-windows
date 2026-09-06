#include "word_game.h"

#include <algorithm>
#include <utility>

bool WordHall::load(const WordConfig& config) {
    entries_.clear();
    if (!config.valid() || !config.hallValid()) return false;
    for (const WordHallEntry& source : config.activeHallEntries()) {
        entries_.push_back({source.name(), source.score});
    }
    return std::is_sorted(entries_.begin(), entries_.end(),
                          [](const WordHallScoreEntry& left,
                             const WordHallScoreEntry& right) {
                              return left.score > right.score;
                          });
}

int WordHall::insertionRank(const std::uint32_t score) const {
    const std::uint32_t candidate = std::min<std::uint32_t>(
        score, static_cast<std::uint32_t>(MuncherScore::MaximumScore));
    if (candidate < MinimumScore) return -1;
    const auto lower = std::find_if(entries_.begin(), entries_.end(),
                                    [candidate](const WordHallScoreEntry& entry) {
                                        return entry.score < candidate;
                                    });
    const std::size_t rank = static_cast<std::size_t>(lower - entries_.begin());
    if (rank < entries_.size() || entries_.size() < Capacity) {
        return static_cast<int>(rank);
    }
    return -1;
}

bool WordHall::insert(const std::uint32_t score, std::string name) {
    const std::uint32_t candidate = std::min<std::uint32_t>(
        score, static_cast<std::uint32_t>(MuncherScore::MaximumScore));
    const int rank = insertionRank(candidate);
    if (rank < 0) return false;
    if (name.empty()) name = "The Unknown Muncher";
    if (name.size() > NameLimit) name.resize(NameLimit);
    entries_.insert(entries_.begin() + rank, {std::move(name), candidate});
    if (entries_.size() > Capacity) entries_.resize(Capacity);
    return true;
}

bool WordHall::replace(std::vector<WordHallScoreEntry> entries) {
    if (entries.size() > Capacity ||
        !std::is_sorted(entries.begin(), entries.end(),
                        [](const WordHallScoreEntry& left,
                           const WordHallScoreEntry& right) {
                            return left.score > right.score;
                        })) {
        return false;
    }
    for (WordHallScoreEntry& entry : entries) {
        if (entry.name.size() > NameLimit) return false;
        entry.score = std::min<std::uint32_t>(
            entry.score, static_cast<std::uint32_t>(MuncherScore::MaximumScore));
    }
    entries_ = std::move(entries);
    return true;
}

bool WordGameCore::configure(const std::array<bool, 20>& selectedSounds,
                             const int wordDifficulty) {
    configured_ = generator_.configure(selectedSounds, wordDifficulty);
    active_ = false;
    boardComplete_ = false;
    maximumScoreReached_ = false;
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    return configured_;
}

bool WordGameCore::configure(const WordConfig& config) {
    if (!config.usable()) {
        configured_ = false;
        active_ = false;
        stagedMunchCell_ = -1;
        stagedMunchValue_ = {};
        return false;
    }
    std::array<bool, 20> selected{};
    for (std::size_t index = 0; index < selected.size(); ++index) {
        selected[index] = config.selectedSounds()[index] != 0;
    }
    return configure(selected, config.difficulty());
}

bool WordGameCore::start(const bool demo) {
    return startAtLevel(1, demo);
}

bool WordGameCore::startAtLevel(const int level, const bool demo) {
    if (!configured_) return false;
    scoreState_.reset();
    level_ = std::max(1, level);
    pressureTier_ = std::min(11, level_ - 1);
    boardComplete_ = false;
    maximumScoreReached_ = false;
    cartoonOrder_ = {0, 1, 2, 3, 4};
    cartoonOrderIndex_ = 0;
    pendingCartoonScene_ = -1;
    demo_ = demo;
    active_ = true;
    return generateBoard(demo_);
}

bool WordGameCore::generateBoard(const bool demo) {
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    board_ = generator_.nextBoard(demo);
    if (board_.targetSound < 0 || board_.correctCount < 2) {
        active_ = false;
        return false;
    }
    eaten_.fill(false);
    correctRemaining_ = board_.correctCount;
    boardComplete_ = false;
    return true;
}

bool WordGameCore::advanceBoard() {
    if (!active_ || !boardComplete_ || maximumScoreReached_ || cartoonPending()) return false;
    ++level_;
    pressureTier_ = std::min(11, pressureTier_ + 1);
    return generateBoard(demo_);
}

bool WordGameCore::finishCartoon() {
    if (!cartoonPending()) return false;
    ++cartoonOrderIndex_;
    if (cartoonOrderIndex_ >= static_cast<int>(cartoonOrder_.size())) {
        cartoonOrderIndex_ = 0;
    }
    pendingCartoonScene_ = -1;
    return true;
}

void WordGameCore::prepareCartoonForCompletedLevel() {
    // The board-empty branch at Word image 0x0ad76 sends Demo directly to
    // the unattended terminal at 0x0947f.  Only the ordinary main-screen
    // action at 0x120f9 reaches the cartoon selector at 0x0fd74, so an
    // unattended completion must not spend its five shuffle draws.
    if (demo_) return;

    // Word image 0x0fd74 resets on completed level 1. While the cursor is
    // zero, every completed board performs five independent full-range swaps
    // before the divisible-by-three test. The cursor advances only at scene
    // teardown (0x0ffad), including after level 18's special scene 5.
    if (level_ == 1) {
        cartoonOrder_ = {0, 1, 2, 3, 4};
        cartoonOrderIndex_ = 0;
    }
    if (cartoonOrderIndex_ == 0) {
        for (int index = 0; index < static_cast<int>(cartoonOrder_.size()); ++index) {
            const int other = random_.range(static_cast<int>(cartoonOrder_.size()));
            std::swap(cartoonOrder_[static_cast<std::size_t>(index)],
                      cartoonOrder_[static_cast<std::size_t>(other)]);
        }
    }
    if (!demo_ && level_ % 3 == 0) {
        pendingCartoonScene_ = level_ == 18
            ? 5
            : cartoonOrder_[static_cast<std::size_t>(cartoonOrderIndex_)];
    }
}

bool WordGameCore::beginMunchCell(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || maximumScoreReached_ ||
        cellIndex >= board_.cells.size() || eaten_[cellIndex] ||
        board_.cells[cellIndex].signedSourceIndex == 0 || stagedMunchCell_ >= 0) {
        return false;
    }

    // Word image 0x0964f saves the signed board record at DS:5dd2 and image
    // 0x0965b clears the live word before actor record 12 is installed. The
    // global correct-answer count is intentionally left unchanged until the
    // seventh state-5 callback resolves this staged record.
    stagedMunchCell_ = static_cast<int>(cellIndex);
    stagedMunchValue_ = board_.cells[cellIndex];
    eaten_[cellIndex] = true;
    return true;
}

WordMunchResolution WordGameCore::resolveMunchCell(const std::size_t cellIndex) {
    WordMunchResolution result;
    if (cellIndex >= board_.cells.size() || stagedMunchCell_ != static_cast<int>(cellIndex)) {
        return result;
    }

    const WordBoardCell cell = stagedMunchValue_;
    stagedMunchCell_ = -1;
    stagedMunchValue_ = {};
    if (!cell.correct) {
        result.kind = WordMunchKind::Wrong;
        result.gameOver = loseMuncher();
        return result;
    }

    result.kind = WordMunchKind::Correct;
    result.points = MuncherScore::pointsForLevel(level_);
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
        prepareCartoonForCompletedLevel();
        result.cartoonScene = pendingCartoonScene_;
    }
    return result;
}

WordMunchResolution WordGameCore::munchCell(const std::size_t cellIndex) {
    if (!beginMunchCell(cellIndex)) return {};
    return resolveMunchCell(cellIndex);
}

bool WordGameCore::loseMuncher() {
    if (!active_) return true;
    const bool gameOver = scoreState_.loseMuncher();
    if (gameOver) active_ = false;
    return gameOver;
}

void WordGameCore::recountCorrectRemaining() {
    correctRemaining_ = 0;
    for (std::size_t index = 0; index < board_.cells.size(); ++index) {
        if (!eaten_[index] && board_.cells[index].correct) ++correctRemaining_;
    }
    // Full native recounts stand in for several original incremental trail
    // updates. Preserve the saved positive record until its terminal callback;
    // the live board slot has already been blanked and may independently be
    // regenerated before then.
    if (stagedMunchCell_ >= 0 && stagedMunchValue_.correct) ++correctRemaining_;
}

bool WordGameCore::clearCellWithoutScore(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size()) return false;
    eaten_[cellIndex] = true;
    recountCorrectRemaining();
    return true;
}

bool WordGameCore::regenerateCell(const std::size_t cellIndex) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size()) return false;
    board_.cells[cellIndex] = generator_.nextCell();
    if (board_.cells[cellIndex].signedSourceIndex == 0) return false;
    eaten_[cellIndex] = false;
    recountCorrectRemaining();
    return true;
}

bool WordGameCore::restoreCell(const std::size_t cellIndex, const WordBoardCell& cell,
                               const bool eaten) {
    if (!active_ || boardComplete_ || cellIndex >= eaten_.size()) return false;
    board_.cells[cellIndex] = cell;
    eaten_[cellIndex] = eaten;
    recountCorrectRemaining();
    return true;
}

bool WordGameCore::completeBoardIfEmpty() {
    if (!active_ || boardComplete_ || correctRemaining_ != 0) return false;
    boardComplete_ = true;
    prepareCartoonForCompletedLevel();
    return true;
}

bool WordGameCore::eaten(const std::size_t cellIndex) const {
    return cellIndex < eaten_.size() && eaten_[cellIndex];
}
