#include "../src/super_content.h"
#include "../src/super_board.h"
#include "../src/super_config.h"
#include "../src/super_game.h"
#include "../src/resource_ids.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

void hashByte(std::uint64_t& hash, const std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
}

void hashString(std::uint64_t& hash, const std::string& value) {
    for (const unsigned char byte : value) hashByte(hash, byte);
    hashByte(hash, 0);
}

void hashSigned16(std::uint64_t& hash, const int value) {
    const auto encoded = static_cast<std::uint16_t>(value);
    hashByte(hash, static_cast<std::uint8_t>(encoded));
    hashByte(hash, static_cast<std::uint8_t>(encoded >> 8));
}

std::uint64_t hashReferences(
    const std::vector<SuperRuleReference>& references) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const SuperRuleReference& reference : references) {
        hashByte(hash, static_cast<std::uint8_t>(reference.topic));
        hashByte(hash, static_cast<std::uint8_t>(reference.rule));
    }
    return hash;
}

std::uint64_t hashBoard(const SuperBoard& board) {
    std::uint64_t hash = 1469598103934665603ull;
    hashByte(hash, static_cast<std::uint8_t>(board.selection.topic));
    hashByte(hash, static_cast<std::uint8_t>(board.selection.rule));
    hashByte(hash, static_cast<std::uint8_t>(board.negated));
    hashString(hash, board.displayedRule);
    hashByte(hash, static_cast<std::uint8_t>(board.correctCount));
    for (const SuperBoardCell& cell : board.cells) {
        hashString(hash, cell.text);
        hashByte(hash, static_cast<std::uint8_t>(cell.correct));
        hashSigned16(hash, cell.signedSourceIndex);
    }
    return hash;
}

SuperBoardSettings allContent(const int difficulty, const bool negations) {
    SuperBoardSettings settings;
    settings.selectedTopics.fill(true);
    for (auto& rules : settings.selectedRules) rules.fill(true);
    settings.difficulty = difficulty;
    settings.negations = negations;
    return settings;
}

bool verifyBoard(const SuperContent& content, const std::uint32_t seed,
                 const SuperBoardSettings& settings, const int game,
                 const bool demo, const int expectedDifficulty,
                 const std::size_t expectedEligible,
                 const std::uint64_t expectedNaturalHash,
                 const std::uint64_t expectedSelectedTableHash,
                 const SuperRuleReference expectedSelection,
                 const bool expectedNegated, const int expectedCorrect,
                 const std::uint64_t expectedCalls,
                 const std::uint32_t expectedState,
                 const std::uint64_t expectedBoardHash) {
    OriginalRandom random(seed);
    SuperBoardGenerator generator(content, random);
    if (!generator.configure(settings, game, demo) ||
        generator.difficulty() != expectedDifficulty ||
        generator.eligibleRules().size() != expectedEligible ||
        hashReferences(generator.eligibleRules()) != expectedNaturalHash) {
        return false;
    }
    const SuperBoard board = generator.nextBoard();
    if (!board.valid || board.selection != expectedSelection ||
        board.negated != expectedNegated ||
        board.correctCount != expectedCorrect ||
        board.correctPoolCount != static_cast<int>(SuperPoolSize) ||
        board.wrongPoolCount != static_cast<int>(SuperPoolSize) ||
        hashReferences(generator.eligibleRules()) != expectedSelectedTableHash ||
        random.calls != expectedCalls || random.state != expectedState ||
        hashBoard(board) != expectedBoardHash) {
        return false;
    }
    for (const SuperBoardCell& cell : board.cells) {
        if (cell.text.empty() || cell.correct != (cell.signedSourceIndex > 0) ||
            cell.signedSourceIndex == 0 ||
            cell.signedSourceIndex < -static_cast<int>(SuperPoolSize) ||
            cell.signedSourceIndex > static_cast<int>(SuperPoolSize)) return false;
    }
    return true;
}

bool verifyConfiguration() {
    const BlobView blob = loadEmbeddedResource(IDR_SM_CFG);
    const SuperConfig config(blob);
    if (!config.usable() || config.version() != 1 ||
        config.applicationName() != "Super Munchers" ||
        config.quickSet() != 1 || config.gameplayDifficulty() != 4 ||
        config.topicMask() != 0xff || config.negationsEnabled() ||
        !config.answerVisionEnabled() || config.joystickFlags() != 0 ||
        config.joystickEnabled() || !config.hint().empty() ||
        !config.password().empty() || config.savedState() != 1) return false;

    const SuperJoystickCalibration& joystick = config.joystickCalibration();
    if (joystick.verticalLow != 0x10 || joystick.verticalHigh != 0x20 ||
        joystick.horizontalHigh != 0x30 || joystick.horizontalLow != 0x40) {
        return false;
    }
    for (std::size_t topic = 0; topic < SuperConfig::TopicCount; ++topic) {
        if (!config.topicSelected(topic)) return false;
        for (std::size_t rule = 0; rule < SuperMaximumRulesPerTopic; ++rule) {
            if (!config.ruleSelected(topic, rule)) return false;
        }
    }
    if (config.topicSelected(6) || config.ruleSelected(6, 0) ||
        config.ruleSelected(0, SuperMaximumRulesPerTopic)) return false;

    const SuperBoardSettings settings = config.boardSettings();
    if (settings.difficulty != 20 || settings.negations ||
        !std::all_of(settings.selectedTopics.begin(),
                     settings.selectedTopics.end(), [](const bool value) {
                         return value;
                     })) return false;

    for (std::size_t hall = 0; hall < config.halls().size(); ++hall) {
        const SuperHallTable& table = config.halls()[hall];
        if (table.capacity != 10 || table.nameLimit != 25 ||
            table.count != (hall == 1 ? 1 : 0)) return false;
        const auto active = table.activeEntries();
        if (hall == 1) {
            if (active.size() != 1 || active[0].name() != "Ken Carlo" ||
                active[0].score != 510) return false;
        } else if (!active.empty()) {
            return false;
        }
    }
    if (!std::equal(config.serialized().begin(), config.serialized().end(),
                    blob.data)) return false;

    const auto reject = [&config](const std::size_t offset,
                                  const std::uint8_t value,
                                  const bool structurallyValid,
                                  const bool contentValid,
                                  const bool hallsValid) {
        auto bytes = config.serialized();
        bytes[offset] = value;
        const SuperConfig changed({bytes.data(), bytes.size()});
        return changed.valid() == structurallyValid &&
               changed.contentValid() == contentValid &&
               changed.hallsValid() == hallsValid;
    };
    if (!reject(0, 'X', false, false, false) ||
        !reject(4, 2, false, false, false) ||
        !reject(0x26, 5, true, false, true) ||
        !reject(0x28, 6, true, false, true) ||
        !reject(0x2a, 0, true, false, true) ||
        !reject(0x49, 2, true, false, true) ||
        !reject(0x4a, 2, true, false, true) ||
        !reject(0x96, 11, true, true, false)) return false;

    auto invalidHint = config.serialized();
    invalidHint[0x4d] = 59;
    auto invalidPassword = config.serialized();
    invalidPassword[0x88] = 11;
    auto unterminatedName = config.serialized();
    std::fill_n(unterminatedName.begin() + 0x1ce, 26,
                static_cast<std::uint8_t>('A'));
    auto playerChooses = config.serialized();
    playerChooses[0x26] = 4;
    playerChooses[0x27] = 0;
    const SuperConfig chooseConfig({playerChooses.data(), playerChooses.size()});
    return !SuperConfig({invalidHint.data(), invalidHint.size()}).valid() &&
           !SuperConfig({invalidPassword.data(), invalidPassword.size()}).valid() &&
           SuperConfig({unterminatedName.data(), unterminatedName.size()}).valid() &&
           !SuperConfig({unterminatedName.data(), unterminatedName.size()}).hallsValid() &&
           chooseConfig.usable() && chooseConfig.boardSettings(30).difficulty == 30 &&
           chooseConfig.boardSettings(17).difficulty == 20 &&
           !SuperConfig({blob.data, blob.size - 1}).valid();
}

bool verifyGameCore(const SuperContent& content) {
    if (SuperGameCore::pointsForLevel(1, 10) != 5 ||
        SuperGameCore::pointsForLevel(1, 20) != 10 ||
        SuperGameCore::pointsForLevel(1, 30) != 15 ||
        SuperGameCore::pointsForLevel(16, 10) != 70 ||
        SuperGameCore::pointsForLevel(16, 20) != 75 ||
        SuperGameCore::pointsForLevel(16, 30) != 80 ||
        SuperGameCore::pointsForLevel(17, 10) != 75 ||
        SuperGameCore::pointsForLevel(17, 30) != 75) return false;

    const SuperConfig config(loadEmbeddedResource(IDR_SM_CFG));
    OriginalRandom random(0xf585u);
    SuperGameCore core(content, random);
    if (!core.configure(config, 0) || !core.start() || !core.active() ||
        core.level() != 1 || core.pressureTier() != 0 ||
        core.transformationMeter() != 0 || core.transformationCell() != -1 ||
        core.superForm() || core.answerVision() ||
        core.board().selection != SuperRuleReference{0, 20} ||
        core.board().correctCount != 20 || random.calls != 2878 ||
        random.state != 0xdf61b247u) return false;

    int resolved = 0;
    for (std::size_t cell = 0; cell < core.board().cells.size(); ++cell) {
        if (!core.board().cells[cell].correct) continue;
        const SuperMunchResolution result = core.munchCell(cell);
        ++resolved;
        if (result.kind != SuperMunchKind::Correct || result.points != 10 ||
            result.bonusMuncher || result.maximumScore || result.gameOver ||
            result.boardComplete != (resolved == 20) ||
            core.transformationMeter() != resolved || !core.eaten(cell) ||
            core.munchCell(cell).kind != SuperMunchKind::Invalid) return false;
    }
    if (resolved != 20 || !core.boardComplete() ||
        core.correctRemaining() != 0 || core.scoreState().score() != 200 ||
        core.scoreState().reserves() != 3 || core.ensureTransformationCell(0)) {
        return false;
    }

    if (!core.advanceBoard() || core.level() != 2 || core.pressureTier() != 1 ||
        core.boardComplete() || core.transformationMeter() != 20 ||
        core.board().selection != SuperRuleReference{0, 24} ||
        core.board().displayedRule != "in the dog family" ||
        core.board().correctCount != 10 || random.calls != 7879 ||
        random.state != 0x3cc6a76cu) return false;
    if (!core.ensureTransformationCell(0) || core.transformationCell() != 7 ||
        core.board().cells[7].signedSourceIndex != 0 || random.calls != 7880 ||
        random.state != 0x641b915du || core.ensureTransformationCell(0)) {
        return false;
    }

    const SuperMunchResolution transformed = core.munchCell(7);
    if (transformed.kind != SuperMunchKind::Transformation ||
        !transformed.transformed || !core.superForm() ||
        core.transformationCell() != -1 || core.transformationMeter() != 20 ||
        !core.defeatTroggle() || core.scoreState().score() != 250 ||
        core.scoreState().reserves() != 3) return false;
    if (!core.toggleAnswerVision() || !core.answerVision() ||
        !core.actionFrozen() || !core.drainTransformationStep() ||
        core.transformationMeter() != 18 || !core.toggleAnswerVision() ||
        core.answerVision() || !core.drainTransformationStep() ||
        core.transformationMeter() != 17) return false;
    for (int step = 0; step < 17; ++step) {
        if (!core.drainTransformationStep()) return false;
    }
    if (!core.superForm() || core.transformationMeter() != 0 ||
        !core.drainTransformationStep() || core.superForm() ||
        core.transformationMeter() != 0 || core.toggleAnswerVision() ||
        core.defeatTroggle()) return false;

    const auto wrong = std::find_if(
        core.board().cells.begin(), core.board().cells.end(),
        [](const SuperBoardCell& cell) { return cell.signedSourceIndex < 0; });
    if (wrong == core.board().cells.end()) return false;
    const std::size_t wrongIndex = static_cast<std::size_t>(
        wrong - core.board().cells.begin());
    const SuperMunchResolution failed = core.munchCell(wrongIndex);
    if (failed.kind != SuperMunchKind::Wrong || failed.gameOver ||
        core.scoreState().reserves() != 2 || core.transformationMeter() != 0 ||
        !core.active()) return false;

    OriginalRandom demoRandom(0x8e74u);
    SuperGameCore demoCore(content, demoRandom);
    return demoCore.configure(config, 0, true) && demoCore.start() &&
           demoCore.transformationMeter() == 16 && demoCore.active();
}

} // namespace

int main() {
    GameAssets assets(GraphicsMode::Vga256, GameAssetSet::SuperMunchers);
    SuperContent content(assets.gameArchive());
    if (!content.valid()) {
        std::cerr << "Super category parser rejected the embedded source tables\n";
        return 1;
    }

    constexpr std::array<std::string_view, 6> titles = {
        "Animals", "Famous Americans", "Food and Health",
        "Geography", "Music", "Odds 'n' Ends",
    };
    constexpr std::array<std::size_t, 6> wordCounts = {487, 729, 441, 702, 637, 775};
    constexpr std::array<std::size_t, 6> ruleCounts = {39, 29, 11, 40, 17, 21};
    constexpr std::array<std::uint8_t, 6> trailers = {0x36, 0x09, 0x62, 0x09, 0x69, 0x20};
    constexpr std::array<std::array<std::size_t, 3>, 6> rankCounts = {{
        {154, 181, 152}, {99, 301, 329}, {142, 138, 161},
        {252, 233, 217}, {150, 251, 236}, {267, 264, 244},
    }};
    constexpr std::array<std::array<std::size_t, 3>, 6> membershipCounts = {{
        {8123, 2465, 8405}, {14761, 1785, 4595}, {3157, 418, 1276},
        {21040, 1982, 5058}, {9148, 547, 1134}, {13731, 723, 1821},
    }};

    std::uint64_t semanticHash = 1469598103934665603ull;
    for (std::size_t setIndex = 0; setIndex < titles.size(); ++setIndex) {
        const SuperCategorySet* set = content.set(setIndex);
        if (!set || set->title != titles[setIndex] ||
            set->words.size() != wordCounts[setIndex] ||
            set->rules.size() != ruleCounts[setIndex] ||
            set->wordListTrailer != trailers[setIndex]) {
            std::cerr << "Super category header " << setIndex << " regressed\n";
            return 2;
        }
        std::array<std::size_t, 3> ranks{};
        std::array<std::size_t, 3> memberships{};
        hashString(semanticHash, set->title);
        for (const SuperRule& rule : set->rules) {
            hashString(semanticHash, rule.text);
            for (const std::uint8_t byte : rule.metadata) hashByte(semanticHash, byte);
        }
        for (const SuperWord& word : set->words) {
            if (word.membership.size() != set->rules.size()) return 3;
            ranks[static_cast<std::size_t>(word.difficultyRank / 10 - 1)]++;
            hashString(semanticHash, word.text);
            hashByte(semanticHash, word.difficultyRank);
            for (const SuperMembership membership : word.membership) {
                const auto value = static_cast<std::uint8_t>(membership);
                memberships[value]++;
                hashByte(semanticHash, value);
            }
        }
        if (ranks != rankCounts[setIndex] || memberships != membershipCounts[setIndex]) {
            std::cerr << "Super category classification counts " << setIndex << " regressed\n";
            return 4;
        }
    }
    if (semanticHash != 0xc4ff1679ddf79b10ull) {
        std::cerr << "Super semantic content hash=0x" << std::hex << semanticHash << '\n';
        return 5;
    }

    if (!verifyConfiguration()) {
        std::cerr << "Super configuration/Hall layout regressed\n";
        return 6;
    }

    if (!verifyGameCore(content)) {
        std::cerr << "Super scoring/transformation game core regressed\n";
        return 7;
    }

    // These snapshots were independently generated from the raw CATG/WLST/
    // INDX bytes and the recovered 0x10A2F..0x110BD controller instructions.
    // They gate the non-Fisher-Yates shuffle, rejection-sampled 60-word pools,
    // conditional negation draw, board retry quota, and exact Borland PRNG use.
    if (!verifyBoard(content, 0xf585u, allContent(20, false), 0, false,
                     20, 33, 0x2dc029577d1a778full,
                     0x9d4d5e961bb585f7ull, {0, 20}, false, 20,
                     2878, 0xdf61b247u, 0xd2a4725b5490f8c8ull)) {
        std::cerr << "Super ordinary-category board sequence regressed\n";
        return 8;
    }
    if (!verifyBoard(content, 0x1234u, allContent(30, true), 6, false,
                     30, 157, 0xae7ea8a9c0dd867dull,
                     0x9c979819ca306f09ull, {5, 8}, true, 13,
                     9234, 0x97d180bau, 0xaca2eb17e951246full)) {
        std::cerr << "Super Challenge/negation board sequence regressed\n";
        return 9;
    }
    if (!verifyBoard(content, 0x8e74u, allContent(30, true), 6, true,
                     10, 68, 0xc83f83b6fc090ce0ull,
                     0xc83f83b6fc090ce0ull, {2, 0}, false, 15,
                     4273, 0x5e3c7415u, 0x7b860336bcda95ffull)) {
        std::cerr << "Super Demo difficulty/rule sequence regressed\n";
        return 10;
    }

    SuperBoardSettings selective = allContent(10, false);
    selective.selectedTopics.fill(false);
    selective.selectedTopics[2] = true;
    selective.selectedTopics[5] = true;
    for (auto& rules : selective.selectedRules) {
        for (std::size_t rule = 0; rule < rules.size(); ++rule) {
            rules[rule] = rule % 2 == 0;
        }
    }
    if (!verifyBoard(content, 0xbeefu, selective, 6, false,
                     10, 14, 0xb500d6f3896790a9ull,
                     0x8197494c4d1d2d21ull, {2, 8}, false, 14,
                     2133, 0xe2f51fa8u, 0xa3899e2853454bceull)) {
        std::cerr << "Super selected-topic/rule filtering regressed\n";
        return 11;
    }

    std::cout << "Super content/board: 3,771 words, 157 rules, exact selection and pools\n";
    return 0;
}
