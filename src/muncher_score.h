#pragma once

struct MuncherScoreAward {
    bool bonusMuncher{};
    bool maximumReached{};
};

// Score/reserve rules shared byte-for-byte by the supplied Number and Word
// executables. The stored count is reserves; the active board Muncher is the
// fourth fresh-game life and is not included in this value.
class MuncherScore {
public:
    static constexpr int MaximumScore = 1'000'000;

    explicit MuncherScore(int score = 0, int reserves = 3) { restore(score, reserves); }

    void reset();
    void restore(int score, int reserves);
    [[nodiscard]] MuncherScoreAward awardPoints(int points);
    [[nodiscard]] MuncherScoreAward awardForLevel(int level);
    [[nodiscard]] bool loseMuncher();

    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] int reserves() const { return reserves_; }
    [[nodiscard]] int visibleReserves() const;
    [[nodiscard]] static int pointsForLevel(int level);

private:
    int score_{};
    int reserves_{3};
};
