#include "super_content.h"

#include <algorithm>
#include <string_view>

namespace {

constexpr std::size_t CategoryRecordSize = 68;
constexpr std::size_t CategoryTextSize = 64;

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8);
}

bool fixedString(const std::uint8_t* data, std::string& result) {
    const auto* end = std::find(data, data + CategoryTextSize, std::uint8_t{});
    if (end == data + CategoryTextSize || end == data) return false;
    // The final rule record in three source sets carries non-zero legacy
    // workspace residue near the end of this fixed field. The DOS string
    // contract stops at the first NUL; rejecting bytes after it would reject
    // the authoritative files without changing any visible rule text.
    result.assign(reinterpret_cast<const char*>(data),
                  static_cast<std::size_t>(end - data));
    return true;
}

bool parseSet(const ResArchive& archive, const std::uint32_t id,
              SuperCategorySet& result) {
    const BlobView categories = archive.find("CATG", id);
    const BlobView words = archive.find("WLST", id);
    const BlobView indexes = archive.find("INDX", id);
    if (!categories || !words || !indexes ||
        categories.size < CategoryRecordSize ||
        categories.size % CategoryRecordSize != 0) return false;

    const std::uint16_t wordCount = read16(categories.data + 64);
    const std::uint16_t ruleCount = read16(categories.data + 66);
    if (wordCount == 0 || ruleCount == 0 ||
        categories.size != static_cast<std::size_t>(ruleCount + 1) *
                               CategoryRecordSize ||
        !fixedString(categories.data, result.title)) return false;

    result.rules.reserve(ruleCount);
    for (std::size_t index = 0; index < ruleCount; ++index) {
        const auto* record = categories.data + (index + 1) * CategoryRecordSize;
        SuperRule rule;
        if (!fixedString(record, rule.text)) return false;
        std::copy_n(record + CategoryTextSize, rule.metadata.size(),
                    rule.metadata.begin());
        if (rule.metadata[0] > 1) return false;
        result.rules.push_back(std::move(rule));
    }

    std::vector<std::string> parsedWords;
    parsedWords.reserve(wordCount);
    std::size_t position = 0;
    for (std::size_t index = 0; index < wordCount; ++index) {
        if (position >= words.size) return false;
        const auto* begin = words.data + position;
        const auto* end = std::find(begin, words.data + words.size, std::uint8_t{});
        if (end == words.data + words.size || end == begin) return false;
        parsedWords.emplace_back(reinterpret_cast<const char*>(begin),
                                 static_cast<std::size_t>(end - begin));
        position = static_cast<std::size_t>(end - words.data) + 1;
    }
    // Each source WLST has one non-NUL trailer byte after its declared strings.
    if (position + 1 != words.size || words.data[position] == 0) return false;
    result.wordListTrailer = words.data[position];

    const std::size_t packedBytes = (static_cast<std::size_t>(ruleCount) + 3) / 4;
    const std::size_t stride = 1 + packedBytes;
    if (indexes.size != static_cast<std::size_t>(wordCount + 1) * stride) return false;
    const auto sentinel = indexes.span().subspan(static_cast<std::size_t>(wordCount) * stride);
    if (!std::all_of(sentinel.begin(), sentinel.end(),
                     [](const std::uint8_t value) { return value == 0; })) return false;

    result.words.reserve(wordCount);
    for (std::size_t wordIndex = 0; wordIndex < wordCount; ++wordIndex) {
        const auto* record = indexes.data + wordIndex * stride;
        if (record[0] != 10 && record[0] != 20 && record[0] != 30) return false;
        SuperWord word;
        word.text = std::move(parsedWords[wordIndex]);
        word.difficultyRank = record[0];
        word.membership.reserve(ruleCount);
        for (std::size_t ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex) {
            const int shift = 6 - static_cast<int>(ruleIndex % 4) * 2;
            const std::uint8_t value =
                static_cast<std::uint8_t>((record[1 + ruleIndex / 4] >> shift) & 3u);
            if (value == 3) return false;
            word.membership.push_back(static_cast<SuperMembership>(value));
        }
        result.words.push_back(std::move(word));
    }
    return true;
}

} // namespace

SuperContent::SuperContent(const ResArchive& archive) {
    if (!archive.valid()) return;
    std::array<SuperCategorySet, 6> parsed;
    for (std::size_t index = 0; index < parsed.size(); ++index) {
        if (!parseSet(archive, static_cast<std::uint32_t>(index + 1), parsed[index])) return;
    }
    sets_ = std::move(parsed);
    valid_ = true;
}

const SuperCategorySet* SuperContent::set(const std::size_t index) const {
    return valid_ && index < sets_.size() ? &sets_[index] : nullptr;
}
