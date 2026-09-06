#include "muncher_score.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr std::array<int, 16> ScorePoints = {
    5, 5, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70,
};

} // namespace

void MuncherScore::reset() {
    score_ = 0;
    reserves_ = 3;
}

void MuncherScore::restore(const int score, const int reserves) {
    score_ = std::clamp(score, 0, MaximumScore);
    reserves_ = reserves;
}

int MuncherScore::pointsForLevel(const int level) {
    if (level <= 1) return ScorePoints.front();
    if (level <= static_cast<int>(ScorePoints.size())) {
        return ScorePoints[static_cast<std::size_t>(level - 1)];
    }
    return 75;
}

MuncherScoreAward MuncherScore::awardPoints(const int points) {
    const int previousScore = score_;
    const std::int64_t updated = static_cast<std::int64_t>(score_) + points;
    score_ = static_cast<int>(std::clamp<std::int64_t>(updated, 0, MaximumScore));
    if (updated >= MaximumScore) {
        score_ = MaximumScore;
        return {.maximumReached = true};
    }

    const bool crossedFirstBonus = previousScore < 1'000 && score_ >= 1'000;
    const bool crossedLaterBonus = previousScore >= 1'000 &&
        score_ % 10'000 < previousScore % 10'000;
    if (crossedFirstBonus || crossedLaterBonus) {
        ++reserves_;
        return {.bonusMuncher = true};
    }
    return {};
}

MuncherScoreAward MuncherScore::awardForLevel(const int level) {
    return awardPoints(pointsForLevel(level));
}

bool MuncherScore::loseMuncher() {
    --reserves_;
    return reserves_ < 0;
}

int MuncherScore::visibleReserves() const {
    return std::clamp(reserves_, 0, 3);
}
