#include "word_content.h"

#include <algorithm>

namespace {

constexpr std::array<std::string_view, 20> WordSoundLabels = {
    "/a/ as in cake", "/a/ as in hat", "/e/ as in tree", "/e/ as in bell",
    "/i/ as in kite", "/i/ as in fish", "/o/ as in boat", "/o/ as in fox",
    "/u/ as in mule", "/u/ as in duck", "/oo/ as in moon", "/oo/ as in book",
    "/ou/ as in mouse", "/au/ as in haunt", "/oi/ as in oil", "/ar/ as in car",
    "/air/ as in chair", "/eer/ as in deer", "/ir/ as in bird", "/or/ as in corn",
};

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

} // namespace

const std::array<std::string_view, 20>& wordSoundLabels() {
    return WordSoundLabels;
}

std::string WordRecord::text() const {
    const auto end = std::find(bytes.begin(), bytes.end(), std::uint8_t{});
    return {reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::size_t>(end - bytes.begin())};
}

WordList::WordList(const BlobView blob) {
    if (!blob || blob.size < 8) return;

    const std::uint32_t tableBytes = read32(blob.data);
    if (tableBytes < 8 || tableBytes % 4 != 0 || tableBytes > blob.size) return;
    const std::size_t offsetCount = tableBytes / 4;
    if (offsetCount < 2 || read32(blob.data) != tableBytes) return;

    std::vector<std::uint32_t> offsets;
    offsets.reserve(offsetCount);
    for (std::size_t index = 0; index < offsetCount; ++index) {
        offsets.push_back(read32(blob.data + index * 4));
    }
    if (offsets.front() != tableBytes || offsets.back() != blob.size) return;
    for (std::size_t index = 1; index < offsets.size(); ++index) {
        if (offsets[index - 1] >= offsets[index]) return;
    }

    std::vector<WordSection> parsed;
    parsed.reserve(offsetCount - 1);
    for (std::size_t sectionIndex = 0; sectionIndex + 1 < offsets.size(); ++sectionIndex) {
        const std::size_t start = offsets[sectionIndex];
        const std::size_t end = offsets[sectionIndex + 1];
        if (end - start < 8 || (end - start - 8) % 6 != 0) return;

        WordSection section;
        std::copy_n(blob.data + start, section.difficultyCounts.size(),
                    section.difficultyCounts.begin());
        if (!std::is_sorted(section.difficultyCounts.begin(), section.difficultyCounts.end())) return;

        const std::size_t recordCount = (end - start - 8) / 6;
        if (recordCount == 0 || section.difficultyCounts.back() + 1u != recordCount) return;
        section.records.reserve(recordCount);
        for (std::size_t recordIndex = 0; recordIndex < recordCount; ++recordIndex) {
            WordRecord record;
            std::copy_n(blob.data + start + 8 + recordIndex * 6, record.bytes.size(),
                        record.bytes.begin());
            if (std::find(record.bytes.begin(), record.bytes.end(), std::uint8_t{}) ==
                record.bytes.end()) return;
            section.records.push_back(record);
        }
        parsed.push_back(std::move(section));
    }

    if (parsed.size() != 21) return;
    sections_ = std::move(parsed);
    valid_ = true;
}

const WordSection* WordList::section(const std::size_t index) const {
    return valid_ && index < sections_.size() ? &sections_[index] : nullptr;
}

void WordBoardGenerator::addPair(const int first, const int second) {
    tuples_.push_back({first, second, -1});
    tuples_.push_back({second, first, -1});
}

void WordBoardGenerator::addTripleRotations(const int first, const int second,
                                            const int third) {
    tuples_.push_back({first, second, third});
    tuples_.push_back({second, third, first});
    tuples_.push_back({third, first, second});
}

bool WordBoardGenerator::configure(const std::array<bool, 20>& selectedSounds,
                                   const int difficulty) {
    configured_ = false;
    tuples_.clear();
    currentCorrectPool_.clear();
    currentWrongPool_.clear();
    tupleCursor_ = 20;
    if (!words_.valid() || difficulty < 0 || difficulty >= 8) return false;
    difficulty_ = difficulty;

    for (int sound = 0; sound < 12; ++sound) {
        if (!selectedSounds[static_cast<std::size_t>(sound)]) continue;
        if (sound == 8 && difficulty < 2) continue;
        tuples_.push_back({sound, sound ^ 1, -1});
    }

    std::vector<int> groupTwo;
    for (int sound = 12; sound < 15; ++sound) {
        if (selectedSounds[static_cast<std::size_t>(sound)]) groupTwo.push_back(sound);
    }
    if (groupTwo.size() == 1) return false;
    if (groupTwo.size() == 2) {
        addPair(groupTwo[0], groupTwo[1]);
    } else if (groupTwo.size() == 3) {
        addTripleRotations(groupTwo[0], groupTwo[1], groupTwo[2]);
    }

    std::vector<int> groupThree;
    for (int sound = 15; sound < 20; ++sound) {
        if (selectedSounds[static_cast<std::size_t>(sound)]) groupThree.push_back(sound);
    }
    if (groupThree.size() == 1) return false;
    if (groupThree.size() == 2) {
        addPair(groupThree[0], groupThree[1]);
    } else if (groupThree.size() == 3) {
        addTripleRotations(groupThree[0], groupThree[1], groupThree[2]);
    } else if (groupThree.size() > 3) {
        for (std::size_t targetIndex = 0; targetIndex < groupThree.size(); ++targetIndex) {
            int firstIndex{};
            do {
                firstIndex = random_.range(static_cast<int>(groupThree.size()));
            } while (firstIndex == static_cast<int>(targetIndex));
            int secondIndex{};
            do {
                secondIndex = random_.range(static_cast<int>(groupThree.size()));
            } while (secondIndex == static_cast<int>(targetIndex) || secondIndex == firstIndex);
            tuples_.push_back({groupThree[targetIndex], groupThree[firstIndex],
                               groupThree[secondIndex]});
        }
    }

    // The original full-range forced-non-self shuffle cannot terminate with a
    // one-entry tuple table. The Set Content validator prevents that state.
    if (tuples_.size() < 2) {
        tuples_.clear();
        return false;
    }
    configured_ = true;
    return true;
}

void WordBoardGenerator::shuffleTuples() {
    const int count = static_cast<int>(tuples_.size());
    for (int index = 0; index < count; ++index) {
        int other{};
        do {
            other = random_.range(count);
        } while (other == index);
        std::swap(tuples_[static_cast<std::size_t>(index)],
                  tuples_[static_cast<std::size_t>(other)]);
    }
}

bool WordBoardGenerator::buildPools(const WordSoundTuple& tuple,
                                    std::vector<WordRecord>& correct,
                                    std::vector<WordRecord>& wrong) {
    const WordSection* target = words_.section(static_cast<std::size_t>(tuple.target));
    if (!target) return false;
    const int targetCount = target->difficultyCounts[static_cast<std::size_t>(difficulty_)];
    if (targetCount < 1 ||
        static_cast<std::size_t>(targetCount + 1) > target->records.size()) return false;
    // The live seed-0x8e74 Demo trajectory proves that the count is a
    // selectable-record count after the section's record-0 exemplar.  The
    // original therefore uses records 1..count for target words, exactly as
    // it does for distractors; record 0 supplies the header exemplar only.
    correct.assign(target->records.begin() + 1,
                   target->records.begin() + 1 + targetCount);

    const std::array<int, 2> distractors{tuple.firstDistractor, tuple.secondDistractor};
    const int distractorCount = tuple.secondDistractor < 0 ? 1 : 2;
    const int samplesPerSound = 180 / distractorCount;
    wrong.clear();
    wrong.reserve(180);
    for (int distractorIndex = 0; distractorIndex < distractorCount; ++distractorIndex) {
        int sectionIndex = distractors[static_cast<std::size_t>(distractorIndex)];
        if (sectionIndex == 8 && difficulty_ < 2) sectionIndex = 20;
        const WordSection* distractor = words_.section(static_cast<std::size_t>(sectionIndex));
        if (!distractor) return false;
        const int available = distractor->difficultyCounts[static_cast<std::size_t>(difficulty_)];
        if (available < 1 || static_cast<std::size_t>(available) >= distractor->records.size()) return false;
        for (int sample = 0; sample < samplesPerSound; ++sample) {
            const int recordIndex = 1 + random_.range(available);
            wrong.push_back(distractor->records[static_cast<std::size_t>(recordIndex)]);
        }
    }
    return wrong.size() == 180;
}

WordBoard WordBoardGenerator::nextBoard(const bool demo) {
    WordBoard board;
    if (!configured_ || tuples_.empty()) return board;

    if (demo) {
        tupleCursor_ = random_.range(static_cast<int>(tuples_.size()));
    } else {
        ++tupleCursor_;
        if (tupleCursor_ >= static_cast<int>(tuples_.size())) {
            tupleCursor_ = 0;
            shuffleTuples();
        }
    }

    const WordSoundTuple& tuple = tuples_[static_cast<std::size_t>(tupleCursor_)];
    if (!buildPools(tuple, currentCorrectPool_, currentWrongPool_)) return board;

    do {
        board.correctCount = 0;
        for (WordBoardCell& cell : board.cells) {
            cell = nextCell();
            if (cell.correct) ++board.correctCount;
        }
    } while (board.correctCount < 2);

    board.targetSound = tuple.target;
    board.tupleIndex = tupleCursor_;
    board.targetPoolCount = static_cast<int>(currentCorrectPool_.size());
    return board;
}

WordBoardCell WordBoardGenerator::nextCell() {
    WordBoardCell cell;
    if (currentCorrectPool_.empty() || currentWrongPool_.empty()) return cell;
    if (random_.range(2) != 0) {
        const int source = random_.range(static_cast<int>(currentCorrectPool_.size()));
        cell.record = currentCorrectPool_[static_cast<std::size_t>(source)];
        cell.correct = true;
        cell.signedSourceIndex = source + 1;
    } else {
        const int source = random_.range(static_cast<int>(currentWrongPool_.size()));
        cell.record = currentWrongPool_[static_cast<std::size_t>(source)];
        cell.correct = false;
        cell.signedSourceIndex = -(source + 1);
    }
    return cell;
}
