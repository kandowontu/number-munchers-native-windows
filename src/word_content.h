#pragma once

#include "assets.h"
#include "original_random.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] const std::array<std::string_view, 20>& wordSoundLabels();

struct WordRecord {
    std::array<std::uint8_t, 6> bytes{};

    [[nodiscard]] std::string text() const;
    bool operator==(const WordRecord&) const = default;
};

struct WordSection {
    std::array<std::uint8_t, 8> difficultyCounts{};
    std::vector<WordRecord> records;
};

class WordList {
public:
    explicit WordList(BlobView blob = {});

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const std::vector<WordSection>& sections() const { return sections_; }
    [[nodiscard]] const WordSection* section(std::size_t index) const;

private:
    std::vector<WordSection> sections_;
    bool valid_{};
};

struct WordSoundTuple {
    int target{};
    int firstDistractor{};
    int secondDistractor{-1};

    bool operator==(const WordSoundTuple&) const = default;
};

struct WordBoardCell {
    WordRecord record;
    bool correct{};
    int signedSourceIndex{};

    bool operator==(const WordBoardCell&) const = default;
};

struct WordBoard {
    int targetSound{-1};
    int tupleIndex{-1};
    int correctCount{};
    int targetPoolCount{};
    std::array<WordBoardCell, 30> cells{};
};

class WordBoardGenerator {
public:
    WordBoardGenerator(const WordList& words, OriginalRandom& random)
        : words_(words), random_(random) {}

    [[nodiscard]] bool configure(const std::array<bool, 20>& selectedSounds,
                                 int difficulty);
    [[nodiscard]] WordBoard nextBoard(bool demo);
    // The original keeps the active target/distractor pools after board
    // construction. Troggle trail callbacks reuse the ordinary per-cell
    // chooser rather than rebuilding or advancing the sound tuple.
    [[nodiscard]] WordBoardCell nextCell();

    [[nodiscard]] bool configured() const { return configured_; }
    [[nodiscard]] int difficulty() const { return difficulty_; }
    [[nodiscard]] const std::vector<WordSoundTuple>& tuples() const { return tuples_; }

private:
    void addPair(int first, int second);
    void addTripleRotations(int first, int second, int third);
    void shuffleTuples();
    [[nodiscard]] bool buildPools(const WordSoundTuple& tuple,
                                  std::vector<WordRecord>& correct,
                                  std::vector<WordRecord>& wrong);

    const WordList& words_;
    OriginalRandom& random_;
    std::vector<WordSoundTuple> tuples_;
    std::vector<WordRecord> currentCorrectPool_;
    std::vector<WordRecord> currentWrongPool_;
    int difficulty_{};
    int tupleCursor_{20};
    bool configured_{};
};
