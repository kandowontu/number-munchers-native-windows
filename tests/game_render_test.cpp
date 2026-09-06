#include "../src/game.h"
#include "../src/dro_audio.h"
#include "../src/mecc_sound.h"
#include "../src/render.h"
#include "../src/resource_ids.h"
#include "../src/scene_script.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

constexpr double OriginalSchedulerTicksPerSecond =
    1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);

bool exactResidentStopApplied(OplStreamPlayer& player,
                              const std::uint8_t expectedDepth) {
    (void)player.renderTestSamples(1);
    if (!player.playing() || player.looping() || player.effectPlaying() ||
        player.scoreWriteCount() != 0) return false;
    constexpr std::array<std::uint8_t, 9> OperatorOffsets = {
        0, 1, 2, 8, 9, 10, 16, 17, 18
    };
    for (std::uint8_t channel = 0; channel < OperatorOffsets.size(); ++channel) {
        const std::uint8_t op = OperatorOffsets[channel];
        if (player.registerValueForTest(static_cast<std::uint8_t>(0xc0 + channel)) != 0x00 ||
            player.registerValueForTest(static_cast<std::uint8_t>(0x43 + op)) != 0x3f ||
            player.registerValueForTest(static_cast<std::uint8_t>(0x83 + op)) != 0xff ||
            player.registerValueForTest(static_cast<std::uint8_t>(0xb0 + channel)) != 0x00) {
            return false;
        }
    }
    return player.registerValueForTest(0xbd) == expectedDepth;
}

struct GameTestAccess {
    static std::uint64_t renderedFrameHash(const Renderer& renderer) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t pixel : renderer.pixels()) {
            hash ^= pixel;
            hash *= 1099511628211ull;
        }
        return hash;
    }
    static std::uint64_t fullFrameHash(Game& game) {
        Renderer renderer;
        game.render(renderer);
        return renderedFrameHash(renderer);
    }
    static std::uint64_t pixelFrameHash(
        const std::vector<std::uint32_t>& pixels) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t pixel : pixels) {
            hash ^= pixel;
            hash *= 1099511628211ull;
        }
        return hash;
    }
    static void seed(Game& game, std::uint32_t value) { game.random_.seed(value); }
    static bool dumpFirstSmartyAttractSeed(Game& game) {
        for (std::uint32_t seed = 0; seed <= 0xffffu; ++seed) {
            game.random_.seed(seed);
            game.startAttract();
            for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                const Game::EnemySlot& enemy =
                    game.enemySlots_[static_cast<std::size_t>(slot)];
                if (enemy.type != 4) continue;
                std::cout << "smarty-seed=0x" << std::hex << seed << std::dec
                          << " level=" << game.level_
                          << " slot=" << slot
                          << " random-calls=" << game.random_.calls << '\n';
                return true;
            }
        }
        return false;
    }
    static void dumpAttractSeedState(Game& game, const std::uint32_t seed,
                                     const bool primesOnly) {
        if (primesOnly) {
            for (Game::ContentSettings& settings : game.contentSettings_) {
                settings.use = false;
            }
            game.contentSettings_[static_cast<std::size_t>(Game::Mode::Primes)].use = true;
            game.contentDraft_ = game.contentSettings_;
            game.resetContentProgression();
        }
        game.random_.seed(seed);
        game.startAttract();
        std::cout << "attract-seed=0x" << std::hex << (seed & 0xffffu)
                  << " random-state=0x" << game.random_.state << std::dec
                  << " random-calls=" << game.random_.calls
                  << " level=" << game.level_
                  << " pressure=" << game.attractDifficultyIndex_
                  << " mode=" << static_cast<int>(game.activeBoardMode_)
                  << " target=" << game.target_ << " enemy-types=";
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            if (slot) std::cout << ',';
            std::cout << game.enemySlots_[static_cast<std::size_t>(slot)].type;
        }
        std::cout << '\n';
    }
    static void applyCapturedSmartyConfig(Game& game) {
        // The seeded Smarty capture uses the preserved NM.CFG whose first
        // 0x160-byte record has SHA-256
        // 8C3D76A109029EC9AB7E72C851A857F0EDF28AC0D788475DC29DAF854628B8F4.
        // Original offsets 0x28/0x2e/0x34/0x3e/0x48/0x4a/0x4e map to
        // use, in-order, minimum, maximum, Multiples cap, Equality ops,
        // and Inequality ops respectively (see image 0x0bfd0..0x0c07a).
        constexpr std::array<bool, 6> Use = {
            false, true, true, true, true, true
        };
        constexpr std::array<bool, 6> InOrder = {
            true, false, false, false, false, false
        };
        constexpr std::array<int, 6> Minimum = {3, 3, 2, 1, 1, 0};
        constexpr std::array<int, 6> Maximum = {15, 99, 199, 50, 50, 0};
        for (std::size_t index = 0; index < game.contentSettings_.size(); ++index) {
            Game::ContentSettings& settings = game.contentSettings_[index];
            settings.use = Use[index];
            settings.minimum = Minimum[index];
            settings.maximum = Maximum[index];
            settings.randomSequence = !InOrder[index];
            settings.maxMultiplier = 50;
            settings.operations = {true, true, true, true};
        }
        game.difficulty_ = 10;
        game.contentDraft_ = game.contentSettings_;
        game.resetContentProgression();
    }
    static bool diagnoseAutonomousSecondBoardInitializer() {
        // The first complete second-board page in the seeded autonomous
        // continuation is source frame 4570. Compare the complete rendered
        // page so the search covers the mode, content, HUD, level, player,
        // and retained Hall palette rather than only the visible labels.
        constexpr std::uint32_t Seed = 0xf585u;
        constexpr std::uint64_t ExpectedFrame = 0x7f26c285575459f7ull;
        constexpr std::array<const char*, Game::BoardCellCount> ExpectedLabels = {
            "1", "2", "1", "1", "1", "2",
            "1", "1", "1", "2", "",  "2",
            "1", "2", "1", "2", "1", "2",
            "2", "2", "2", "1", "1", "2",
            "2", "2", "1", "1", "2", "2",
        };

        bool exact = false;
        int bestLabelCount = -1;
        int bestStart = -1;
        for (int start = 0; start <= 1'000; ++start) {
            Game game;
            applyCapturedSmartyConfig(game);
            game.random_.seed(Seed);
            for (int call = 0; call < start; ++call) (void)game.random_.next();
            game.attractMode_ = true;
            game.hallPaletteActive_ = true;
            game.attractBoardIndex_ = 0;
            game.startNextAttractBoard();

            int labelCount = 0;
            for (int index = 0; index < Game::BoardCellCount; ++index) {
                if (index == 10 ||
                    game.cells_[static_cast<std::size_t>(index)].label ==
                        ExpectedLabels[static_cast<std::size_t>(index)]) {
                    ++labelCount;
                }
            }
            if (labelCount > bestLabelCount) {
                bestLabelCount = labelCount;
                bestStart = start;
            }

            const std::uint64_t hash = fullFrameHash(game);
            const bool visibleBoard =
                game.activeBoardMode_ == Game::Mode::Primes &&
                game.playerRow_ == 1 && game.playerColumn_ == 4 &&
                labelCount == Game::BoardCellCount;
            if (hash == ExpectedFrame || visibleBoard) {
                std::cout << "autonomous-second-board start=" << start
                          << " end=" << game.random_.calls
                          << " state=0x" << std::hex << game.random_.state
                          << " hash=0x" << hash << std::dec
                          << " level=" << game.level_
                          << " pressure=" << game.attractDifficultyIndex_
                          << " mode=" << static_cast<int>(game.activeBoardMode_)
                          << " player=" << game.playerRow_ << ','
                          << game.playerColumn_ << " labels=" << labelCount
                          << (hash == ExpectedFrame ? " exact" : " visible")
                          << '\n';
            }
            exact = exact || hash == ExpectedFrame;
        }
        std::cout << "autonomous-second-board best-start=" << bestStart
                  << " best-labels=" << bestLabelCount << "/30 exact="
                  << exact << '\n';

        // Test the competing hypothesis that the first board was already five
        // calls behind before its terminal Worker/Demo boundary. If replacing
        // only that live stream changes the captured Space decision, the five
        // calls belong to the concurrent Worker path itself instead.
        Game shifted;
        applyCapturedSmartyConfig(shifted);
        for (auto& list : shifted.scores_) list.clear();
        shifted.random_.seed(Seed);
        shifted.startAttract();
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        Renderer shiftedRenderer;
        for (int sample = 0; sample <= 1'990; ++sample) {
            shifted.renderPresentation(shiftedRenderer);
            if (sample == 1'967) {
                shifted.random_.seed(Seed);
                for (int call = 0; call < 211; ++call) {
                    (void)shifted.random_.next();
                }
            }
            if (sample == 1'968 || sample == 1'970 || sample == 1'973 ||
                sample == 1'987 || sample == 1'990) {
                std::cout << "autonomous-shifted sample=" << sample
                          << " calls=" << shifted.random_.calls
                          << " state=0x" << std::hex << shifted.random_.state
                          << " hash=0x" << renderedFrameHash(shiftedRenderer)
                          << std::dec << " munch=" << shifted.munching_
                          << " page=" << static_cast<int>(shifted.page_)
                          << " message='" << shifted.feedbackMessage_ << "'\n";
            }
            if (sample != 1'990) shifted.update(SampleSeconds);
        }
        return true;
    }
    static bool diagnoseAutonomousFinalBoardInitializer() {
        // The last complete board in original-number-autonomous-f585.avi
        // begins at source frame 23358. Search the uninterrupted Borland
        // stream for the pre-initializer call position that reproduces its
        // complete inequality board, rather than guessing from the visible
        // rule header alone.
        constexpr std::uint32_t Seed = 0xf585u;
        constexpr std::uint64_t ExpectedFrame = 0x55761275c6a0305dull;
        constexpr std::array<const char*, Game::BoardCellCount> ExpectedLabels = {
            "16+5", "14+6", "40:2", "6+4",  "4x5",  "7+12",
            "2x12", "54:3", "1x19", "35-5", "54:3", "2+20",
            "24-6", "",     "21-0", "9+1",  "20-1", "4+6",
            "2+16", "16x1", "22-2", "0+10", "16x1", "63:3",
            "13+4", "76:4", "21-5", "10+7", "60:3", "5x2",
        };

        bool exact = false;
        int bestLabelCount = -1;
        int bestStart = -1;
        for (int start = 600; start <= 1'100; ++start) {
            Game game;
            applyCapturedSmartyConfig(game);
            game.random_.seed(Seed);
            for (int call = 0; call < start; ++call) (void)game.random_.next();
            game.attractMode_ = true;
            game.hallPaletteActive_ = true;
            game.attractBoardIndex_ = 4;
            game.startNextAttractBoard();

            int labelCount = 0;
            for (int index = 0; index < Game::BoardCellCount; ++index) {
                if (game.cells_[static_cast<std::size_t>(index)].label ==
                    ExpectedLabels[static_cast<std::size_t>(index)]) {
                    ++labelCount;
                }
            }
            if (labelCount > bestLabelCount) {
                bestLabelCount = labelCount;
                bestStart = start;
            }

            const std::uint64_t hash = fullFrameHash(game);
            const bool visibleBoard =
                game.activeBoardMode_ == Game::Mode::Inequality &&
                game.target_ == 20 && game.relation_ == 0 &&
                game.playerRow_ == 2 && game.playerColumn_ == 1 &&
                labelCount == Game::BoardCellCount;
            if (hash == ExpectedFrame || visibleBoard) {
                std::cout << "autonomous-final-board start=" << start
                          << " end=" << game.random_.calls
                          << " state=0x" << std::hex << game.random_.state
                          << " hash=0x" << hash << std::dec
                          << " level=" << game.level_
                          << " pressure=" << game.attractDifficultyIndex_
                          << " mode=" << static_cast<int>(game.activeBoardMode_)
                          << " target=" << game.target_
                          << " relation=" << game.relation_
                          << " player=" << game.playerRow_ << ','
                          << game.playerColumn_ << " labels=" << labelCount
                          << " enemy-types=";
                for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                    if (slot) std::cout << ',';
                    std::cout << game.enemySlots_[static_cast<std::size_t>(slot)].type;
                }
                std::cout << '\n';
                exact = visibleBoard && hash == ExpectedFrame;
                if (exact) break;
            }
        }
        if (!exact) {
            std::cout << "autonomous-final-board no exact match; best-start="
                      << bestStart << " labels=" << bestLabelCount << '\n';
        }
        return exact;
    }
    static bool usesCapturedAutonomousPlayerEntryWalkSequence() {
        // Complete 320x200 pages reconstructed from source frames 629-644 of
        // original-number-autonomous-f585.avi.  Frame 631 is a nonuniform
        // scanout/dirty-paint fragment, so the prior completed page remains
        // resident for that public sample; the same rule applies at the
        // terminal boundary before source frame 644.
        constexpr std::array<std::uint64_t, 16> Expected = {
            0x7a2d7ad872f10c66ull,
            0x7a2d7ad872f10c66ull,
            0x7a2d7ad872f10c66ull,
            0x7574f5a8aecac535ull,
            0x7574f5a8aecac535ull,
            0xf589476df46beaf3ull,
            0xf589476df46beaf3ull,
            0xf589476df46beaf3ull,
            0xd564005d6122a129ull,
            0x15550ca75d75d336ull,
            0x221d7845e15b298dull,
            0x221d7845e15b298dull,
            0xc1ca853372ec9f6cull,
            0xc1ca853372ec9f6cull,
            0xc1ca853372ec9f6cull,
            0x460e8bb946114456ull,
        };
        constexpr int FirstSample = 628;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        if (game.random_.state != 0x5f26e95cu || game.random_.calls != 87 ||
            game.level_ != 10 || game.attractDifficultyIndex_ != 9 ||
            game.activeBoardMode_ != Game::Mode::Factors || game.target_ != 57 ||
            game.enemySlotCount_ != 3 || game.enemySlots_[0].type != 1 ||
            game.enemySlots_[1].type != 1 || game.enemySlots_[2].type != 3) {
            return false;
        }

        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == 631 &&
                (game.random_.calls != 117 || game.random_.state != 0xe31569b6u ||
                 game.playerRow_ != 1 || game.playerColumn_ != 1 || !game.moving_ ||
                 game.attractPlayerMovingEntrySlot_ != 0 ||
                 game.attractPlayerMovingEntryHoldPixels_.empty())) {
                return false;
            }
            if (sample == 636 &&
                (game.random_.calls != 118 || game.random_.state != 0x69a056afu ||
                 game.attractPlayerMovingEntrySlot_ != 0 ||
                 game.presentationFrames_.size() != 1)) {
                return false;
            }
            if (sample == 640 &&
                (game.random_.calls != 120 || game.random_.state != 0xf7c5686du ||
                 game.playerRow_ != 1 || game.playerColumn_ != 2 || game.moving_ ||
                 game.playerTerminalFrame_ != 4 ||
                 game.attractPlayerMovingEntrySlot_ != -1 ||
                 !game.attractPlayerMovingEntryTerminalHold_ ||
                 game.attractPlayerMovingEntryHoldPixels_.empty())) {
                return false;
            }
            if (sample == LastSample &&
                (game.playerTerminalFrame_ != -1 ||
                 game.attractPlayerMovingEntryTerminalHold_ ||
                 !game.attractPlayerMovingEntryHoldPixels_.empty())) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousInitialCannibalPlayerSequence() {
        // Source frames 583-609 cover a final entrant callback, downward
        // player walk, and a simultaneous state-5 Troggle bite. Frame 585 is
        // an incomplete 13-pixel entrant repaint; frame 590 is an exact
        // adjacent-page partition. Native retains completed pages only and
        // holds each player-first callback over the later bite surface.
        constexpr std::array<std::uint64_t, 27> Expected = {
            0xfb25c1a7fbc7543eull,
            0xfb25c1a7fbc7543eull,
            0xfb25c1a7fbc7543eull,
            0xfb25c1a7fbc7543eull,
            0x7a998abe50235f59ull,
            0x7a998abe50235f59ull,
            0x7a998abe50235f59ull,
            0x70783222fdfec1c3ull,
            0x70783222fdfec1c3ull,
            0x615e6b4ec77f15adull,
            0x615e6b4ec77f15adull,
            0x615e6b4ec77f15adull,
            0x4552b802dab72705ull,
            0x4552b802dab72705ull,
            0x8e2f40385d26dd2eull,
            0x8e2f40385d26dd2eull,
            0x25a5bdad3340d1c5ull,
            0x25a5bdad3340d1c5ull,
            0x25a5bdad3340d1c5ull,
            0x25a5bdad3340d1c5ull,
            0x25a5bdad3340d1c5ull,
            0x66eb1ba2d32b17b2ull,
            0x66eb1ba2d32b17b2ull,
            0x66eb1ba2d32b17b2ull,
            0x25a5bdad3340d1c5ull,
            0x25a5bdad3340d1c5ull,
            0x66eb1ba2d32b17b2ull,
        };
        constexpr int FirstSample = 583;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == 585 &&
                (game.random_.calls != 112 || game.random_.state != 0x6c56fad5u ||
                 !game.moving_ || game.playerMovementPhase() != 0 ||
                 pixelFrameHash(game.attractPlayerPhaseZeroHoldPixels_) !=
                     0xfb25c1a7fbc7543eull)) {
                return false;
            }
            if (sample == 592 &&
                (game.random_.calls != 113 || game.random_.state != 0xf250d41au ||
                 game.playerMovementPhase() != 3 ||
                 pixelFrameHash(game.cannibalPlayerPresentationHoldPixels_) !=
                     0x615e6b4ec77f15adull)) {
                return false;
            }
            if (sample == 597 &&
                (game.playerTerminalFrame_ != 8 ||
                 pixelFrameHash(game.cannibalPlayerPresentationHoldPixels_) !=
                     0x8e2f40385d26dd2eull)) {
                return false;
            }
            if (sample == 599 &&
                (game.playerTerminalFrame_ != -1 ||
                 !game.attractInitialCannibalPlayerTerminalHold_ ||
                 pixelFrameHash(game.cannibalPlayerPresentationHoldPixels_) !=
                     0x25a5bdad3340d1c5ull)) {
                return false;
            }
            if (sample == 604 &&
                (game.attractInitialCannibalPlayerTerminalHold_ ||
                 !game.cannibalPlayerPresentationHoldPixels_.empty())) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousConcurrentLeftMovementSequence() {
        // Presentation-complete pages from source frames 1249-1274. Source
        // frames 1259 and 1273 are respectively a one-block scanout splice
        // and an interrupted actor repaint, so neither is a native page.
        constexpr std::array<std::uint64_t, 18> Expected = {
            0xa1694343049a0223ull,
            0xa1694343049a0223ull,
            0xa1694343049a0223ull,
            0x3fe24acf7534c396ull,
            0x3fe24acf7534c396ull,
            0x791dbb66005a21d4ull,
            0x791dbb66005a21d4ull,
            0xd30e58eafc120ae8ull,
            0xd30e58eafc120ae8ull,
            0xd30e58eafc120ae8ull,
            0x19e989e0f4c5bb05ull,
            0x873b9b98819d728bull,
            0x1887f0c2273fe83dull,
            0x1887f0c2273fe83dull,
            0x1887f0c2273fe83dull,
            0xe6b90b57f8b89276ull,
            0xe6b90b57f8b89276ull,
            0x5f74fabcdfb9b249ull,
        };
        constexpr int FirstSample = 1256;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == 1256 &&
                (game.random_.calls != 157 || game.random_.state != 0x67837e8eu ||
                 game.playerRow_ != 0 || game.playerColumn_ != 3 ||
                 !game.moving_ || game.playerMovementPhase() != 0 ||
                 game.attractPlayerConcurrentEnemySlot_ != 2 ||
                 game.attractPlayerPhaseZeroHoldPixels_.empty())) {
                return false;
            }
            if (sample == 1259 &&
                (game.playerMovementPhase() != 1 ||
                 game.attractPlayerConcurrentEnemySlot_ != 2 ||
                 pixelFrameHash(game.attractPlayerConcurrentEnemyHoldPixels_) !=
                     0x3fe24acf7534c396ull)) {
                return false;
            }
            if (sample == 1266 &&
                (game.playerMovementPhase() != 4 ||
                 game.presentationFrames_.size() != 1 ||
                 pixelFrameHash(game.presentationFrames_.front()) !=
                     0x873b9b98819d728bull ||
                 pixelFrameHash(game.attractPlayerConcurrentEnemyHoldPixels_) !=
                     0x873b9b98819d728bull)) {
                return false;
            }
            if (sample == 1271 &&
                (game.random_.calls != 158 || game.random_.state != 0x8ab47767u ||
                 game.playerRow_ != 0 || game.playerColumn_ != 2 || game.moving_ ||
                 game.playerTerminalFrame_ != 10 ||
                 game.attractPlayerConcurrentEnemySlot_ != -1 ||
                 !game.attractPlayerConcurrentEnemyTerminalHold_ ||
                 pixelFrameHash(game.attractPlayerConcurrentEnemyHoldPixels_) !=
                     0xe6b90b57f8b89276ull)) {
                return false;
            }
            if (sample == LastSample &&
                (game.playerTerminalFrame_ != -1 ||
                 game.attractPlayerConcurrentEnemyTerminalHold_ ||
                 !game.attractPlayerConcurrentEnemyHoldPixels_.empty())) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousDualEnemyCallbackSequence() {
        // Source frames 1442-1458: slot 0 paints before slot 2 while both
        // Troggles move. Frames 1451 and 1456 are incomplete actor/player
        // repaints and are intentionally represented by their resident pages.
        constexpr std::array<std::uint64_t, 18> Expected = {
            0x03853f37d255aae2ull,
            0x03853f37d255aae2ull,
            0x03853f37d255aae2ull,
            0xe30861039f5e2972ull,
            0xe30861039f5e2972ull,
            0x5f56fc645b9746a7ull,
            0x5f56fc645b9746a7ull,
            0x5f56fc645b9746a7ull,
            0xb26129114d145554ull,
            0xb26129114d145554ull,
            0x65b0c5c9733ef627ull,
            0x3f83b5ee0794ee40ull,
            0x3f83b5ee0794ee40ull,
            0x3f83b5ee0794ee40ull,
            0x3f83b5ee0794ee40ull,
            0x872792380b426b03ull,
            0x872792380b426b03ull,
            0x283589ad4d40b396ull,
        };
        constexpr int FirstSample = 1441;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == 1441 &&
                (game.random_.calls != 168 || game.random_.state != 0x2afa7efdu ||
                 game.enemyCallbackPresentationSlots_ !=
                     std::vector<int>({0, 2}) ||
                 pixelFrameHash(game.attractConcurrentEnemyHoldPixels_) !=
                     0x03853f37d255aae2ull)) {
                return false;
            }
            if (sample == 1451 &&
                (game.random_.calls != 169 || game.random_.state != 0xda7f6062u ||
                 game.presentationFrames_.size() != 1 ||
                 pixelFrameHash(game.presentationFrames_.front()) !=
                     0x3f83b5ee0794ee40ull ||
                 !game.attractConcurrentEnemyAwaitPlayerCallback_ ||
                 pixelFrameHash(game.attractConcurrentEnemyHoldPixels_) !=
                     0x3f83b5ee0794ee40ull)) {
                return false;
            }
            if (sample == 1453 &&
                (!game.moving_ || game.playerMovementPhase() != 0 ||
                 !game.attractConcurrentEnemyAwaitPlayerCallback_)) {
                return false;
            }
            if (sample == 1456 &&
                (game.playerMovementPhase() != 1 ||
                 game.attractConcurrentEnemyAwaitPlayerCallback_ ||
                 !game.attractConcurrentEnemyHoldPixels_.empty() ||
                 game.attractPlayerConcurrentEnemySlot_ != 2 ||
                 pixelFrameHash(game.attractPlayerConcurrentEnemyHoldPixels_) !=
                     0x872792380b426b03ull)) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousTerminalDualEnemySequence() {
        // Source frames 1613-1654 cover a downward player terminal while the
        // earlier Worker and later Helper callbacks overlap. Source frame
        // 1617 is a three-pixel incomplete repaint, while frames 1622 and
        // 1629 are torn refreshes; native retains only their surrounding
        // presentation-complete pages. After slot 0 completes, the resident
        // two-slot page releases on slot 2's next callback because no new
        // player phase zero was installed on that dispatcher tick.
        constexpr std::array<std::uint64_t, 43> Expected = {
            0x9d8874b45d7093b7ull,
            0x9d8874b45d7093b7ull,
            0xbe1857394a782d15ull,
            0xbe1857394a782d15ull,
            0xbe1857394a782d15ull,
            0xa26d9be131f50dceull,
            0xa26d9be131f50dceull,
            0xef17c074a11411ccull,
            0xef17c074a11411ccull,
            0xef17c074a11411ccull,
            0x56e4f81ba11d5fd1ull,
            0x56e4f81ba11d5fd1ull,
            0x5f24d96b9368c77aull,
            0x5f24d96b9368c77aull,
            0x5f24d96b9368c77aull,
            0x3e5e6d688e694c57ull,
            0x3e5e6d688e694c57ull,
            0x92423f13680efe76ull,
            0xdc9c02166fa15e61ull,
            0xc8be0215d3793fa8ull,
            0xc8be0215d3793fa8ull,
            0xc8be0215d3793fa8ull,
            0x2ca258460192b841ull,
            0x2ca258460192b841ull,
            0x1562fbb5227221fbull,
            0x1562fbb5227221fbull,
            0x1562fbb5227221fbull,
            0x1562fbb5227221fbull,
            0x1562fbb5227221fbull,
            0x23d79851de6bdbcdull,
            0x23d79851de6bdbcdull,
            0xd39de4c2b8a5fa5cull,
            0xd39de4c2b8a5fa5cull,
            0xd39de4c2b8a5fa5cull,
            0x5ae05b0c9d597860ull,
            0x5ae05b0c9d597860ull,
            0x934caf3bbc7f1d9cull,
            0x934caf3bbc7f1d9cull,
            0x934caf3bbc7f1d9cull,
            0x06f1260a0545f0ddull,
            0x06f1260a0545f0ddull,
            0x4159507b03868648ull,
            0x4159507b03868648ull,
        };
        constexpr int FirstSample = 1612;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == 1614 &&
                (game.random_.calls != 184 || game.random_.state != 0x066ab92du ||
                 !game.moving_ || game.playerMovementPhase() != 2 ||
                 !game.presentationFrames_.empty())) {
                return false;
            }
            if (sample == 1622 &&
                (game.random_.calls != 185 || game.random_.state != 0x1f560c52u ||
                 game.playerTerminalFrame_ != 8 ||
                 game.enemyCallbackPresentationSlots_ !=
                     std::vector<int>({0, 2}) ||
                 pixelFrameHash(game.attractConcurrentEnemyHoldPixels_) !=
                     0x56e4f81ba11d5fd1ull)) {
                return false;
            }
            if (sample == 1629 &&
                (game.random_.calls != 186 || game.random_.state != 0x5b6588fbu ||
                 game.presentationFrames_.size() != 1 ||
                 pixelFrameHash(game.presentationFrames_.front()) !=
                     0xdc9c02166fa15e61ull ||
                 !game.attractConcurrentEnemyAwaitPlayerCallback_ ||
                 pixelFrameHash(game.attractConcurrentEnemyHoldPixels_) !=
                     0xdc9c02166fa15e61ull)) {
                return false;
            }
            if (sample == 1631 &&
                (game.enemyCallbackPresentationSlots_ != std::vector<int>({2}) ||
                 game.attractConcurrentEnemyAwaitPlayerCallback_ ||
                 !game.attractConcurrentEnemyHoldPixels_.empty())) {
                return false;
            }
            if (sample == LastSample &&
                (game.random_.calls != 192 || game.random_.state != 0x21423fc5u ||
                 game.moving_ || game.playerTerminalFrame_ != -1)) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousHorizontalExitHistorySequence() {
        // Source frames 1764-1789 cover the delayed left-edge Helper exit
        // and the overlapping downward player walk. Frame 1766 is an exact
        // pixel partition of its adjacent complete pages (52 newer actor
        // pixels and 269 retained pixels) and therefore remains capture-only.
        // The complete history keeps the standing player with the delayed
        // exit, then drains phases 1-4, the terminal pose, and standing page.
        constexpr std::array<std::uint64_t, 19> Expected = {
            0x61b1688904880b59ull,
            0x61b1688904880b59ull,
            0x41e633878ac881acull,
            0x41e633878ac881acull,
            0x41e633878ac881acull,
            0x37ee17898dff6fe2ull,
            0x37ee17898dff6fe2ull,
            0x3d1f49db9243d09aull,
            0x3d1f49db9243d09aull,
            0x3d1f49db9243d09aull,
            0x3d1f49db9243d09aull,
            0x3d1f49db9243d09aull,
            0x3d1f49db9243d09aull,
            0x08e83a6d458f25e4ull,
            0x213c50d733429e9bull,
            0x22cefdb61562e114ull,
            0xb59819abf9e8e838ull,
            0xc36b0c20badb6c20ull,
            0x558fc314bc231912ull,
        };
        constexpr int FirstSample = 1771;
        constexpr int LastSample =
            FirstSample + static_cast<int>(Expected.size()) - 1;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) !=
                    Expected[static_cast<std::size_t>(sample - FirstSample)]) {
                return false;
            }
            if (sample == FirstSample &&
                (game.random_.calls != 198 || game.random_.state != 0x9b20a91fu ||
                 game.enemyPresentationLagSlot_ != 2 ||
                 !game.enemyPresentationLagFullSurface_ ||
                 game.enemyPresentationLagHistory_.size() != 4 ||
                 !game.moving_ || game.playerMovementPhase() != 0)) {
                return false;
            }
            if (sample == 1784 &&
                (game.enemyPresentationLagSlot_ != 2 ||
                 game.playerTerminalFrame_ != 2 ||
                 renderedFrameHash(renderer) != 0x08e83a6d458f25e4ull)) {
                return false;
            }
            if (sample == 1785 &&
                (game.enemyPresentationLagSlot_ != -1 ||
                 game.enemyPresentationLagFullSurface_ ||
                 game.presentationFrames_.size() != 4 ||
                 renderedFrameHash(renderer) != 0x213c50d733429e9bull)) {
                return false;
            }
            if (sample == LastSample &&
                (game.enemyPresentationLagSlot_ != -1 ||
                 !game.presentationFrames_.empty() || game.moving_ ||
                 game.playerTerminalFrame_ != -1)) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousWrongAnswerRetainedSequence() {
        // Source frames 1925-2397 cover the first autonomous wrong-answer
        // chew, its 150-tick message, the restored-board hold, and the clean
        // Hall wipe. A due downward Worker remains undispatched when the
        // terminal Demo action takes ownership: each completed DOS chew page
        // retains its preceding dwell pose, and the following exact Prime
        // board proves that only Demo calls 217/218 were spent. Source frames
        // 1973, 1985, 1990, 2391,
        // 2393, and 2394 are incomplete refreshes; the hashes below gate the
        // presentation-complete pages and native's three wipe intermediates.
        constexpr int FirstSample = 1925;
        constexpr int LastSample = 2395;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        const auto expectedHash = [](const int sample) -> std::uint64_t {
            if (sample <= 1972) return 0x0d8e93e701fba773ull;
            if (sample <= 1974) return 0x3f42b552899ec8f7ull;
            if (sample <= 1977) return 0x50bae1e526f76267ull;
            if (sample <= 1979) return 0x3f42b552899ec8f7ull;
            if (sample <= 1982) return 0x50bae1e526f76267ull;
            if (sample <= 1984) return 0x3f42b552899ec8f7ull;
            if (sample <= 1986) return 0x50bae1e526f76267ull;
            if (sample == 1987) return 0x3f42b552899ec8f7ull;
            if (sample <= 2347) return 0x24cdbed8f677f47full;
            if (sample <= 2387) return 0x3f42b552899ec8f7ull;
            if (sample == 2388) return 0x09caba8553dc8fb1ull;
            if (sample <= 2390) return 0x2cc687e766578183ull;
            if (sample == 2391) return 0x413bd71b36091f2bull;
            if (sample <= 2393) return 0xb1a8f948642db583ull;
            if (sample == 2394) return 0xa4f3bcefbfe80ba8ull;
            return 0x10cd15de01d699e3ull;
        };

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            if (sample >= FirstSample &&
                renderedFrameHash(renderer) != expectedHash(sample)) {
                return false;
            }
            if (sample == 1968 &&
                (game.random_.calls != 216 || game.random_.state != 0x9e7af98du ||
                 game.munching_ ||
                 pixelFrameHash(game.attractMunchConcurrentEnemyPixels_) !=
                     0x0d8e93e701fba773ull)) {
                return false;
            }
            if (sample == 1973 &&
                (game.random_.calls != 218 || game.random_.state != 0x67d4665bu ||
                 !game.munching_ ||
                 pixelFrameHash(game.attractMunchConcurrentEnemyPixels_) !=
                     0x3f42b552899ec8f7ull)) {
                return false;
            }
            if (sample == 1987 &&
                (game.page_ != Game::Page::Feedback || game.munching_ ||
                 game.feedbackKind_ != Game::FeedbackKind::WrongAnswer ||
                 game.feedbackMessage_ != "\"37\" is not a factor of \"57\"." ||
                 game.lastPresentationAuditOrigin_ != 1362 ||
                 pixelFrameHash(game.attractMunchConcurrentEnemyPixels_) !=
                     0x3f42b552899ec8f7ull)) {
                return false;
            }
            if (sample == 2348 &&
                (!game.attractPostFeedbackBoard_ ||
                 game.attractHallTransitionPhase_ !=
                     Game::AttractHallTransitionPhase::BoardHold ||
                 pixelFrameHash(game.attractMunchConcurrentEnemyPixels_) !=
                     0x3f42b552899ec8f7ull)) {
                return false;
            }
            if (sample == LastSample &&
                (game.page_ != Game::Page::Hall ||
                 game.feedbackKind_ != Game::FeedbackKind::None ||
                 !game.attractMunchConcurrentEnemyPixels_.empty())) {
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool usesCapturedAutonomousSecondBoardSequence() {
        // The exact F585 continuation initializes the Prime board at source
        // frame 4570, then reaches the captured Bashful bottom-exit tie,
        // two consecutive Demo walks, the wrong-answer chew/feedback, and
        // the clean Hall transition at source frame 6602. It now continues
        // through the exact bottom-up logo repaint at source frame 7686 and
        // the next logo-to-board opening at frames 8776-8778. Nonuniform
        // source scanout fragments are represented by completed native pages;
        // the timing-dependent frame-8776 painter prefix is suppressed while
        // the coherent actor-free frame 8777 and full frame 8778 are exact.
        // The permanent walk now spans the entire 365-second source capture:
        // the fourth-board terminal collision, its source-selected "Aargh"
        // phrase, the recovered Less Than 20 initializer, the callback-local
        // closed-chew record, the final wrong-answer Hall transition, and the
        // stable closing logo are all fixed checkpoints below.
        constexpr int FirstSample = 5980;
        constexpr int ThirdBoardFirstSample = 8792;
        constexpr int ThirdBoardHallSample = 9775;
        constexpr int FourthBoardLastSample = 15876;
        constexpr int LastSample = 25700;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        const auto expectedHash = [](const int sample) -> std::uint64_t {
            if (sample <= 6059) return 0xa28230c6be473034ull;
            if (sample <= 6066) return 0x38c77e351272ba10ull;
            if (sample <= 6073) return 0xcda446f862331b20ull;
            if (sample <= 6080) return 0x5aa1395c9977b4a6ull;
            if (sample <= 6087) return 0x53a97c25745eebb7ull;
            if (sample <= 6099) return 0x57bff04d583892e9ull;
            switch (sample) {
            case 6100: return 0x09d4a8cdde261157ull;
            case 6101: return 0x167bc546e282eb55ull;
            case 6102: return 0x9d96593f30ecd9daull;
            case 6103: return 0x39842ee89a77614bull;
            case 6104: return 0x29062bf86fc74ec7ull;
            case 6105: return 0x5707167ab2dba7e5ull;
            case 6106: return 0x484cd1bb2bcb93f3ull;
            case 6107: return 0xca47b5ddbeacde20ull;
            case 6108: return 0x3f84f8c9ef6975eaull;
            case 6109: return 0x85a04662aa9fad8full;
            case 6110: return 0x6f7a7d466f6fde0bull;
            case 6111: return 0xa7549c4d9b03f623ull;
            case 6112: return 0x0a9edb329d15d2aeull;
            default: break;
            }
            if (sample <= 6186) return 0xee19b6d79f6b8f4bull;
            if (sample <= 6188) return 0x9d36ff494d678f53ull;
            if (sample <= 6191) return 0xf8aab7ac6634a037ull;
            if (sample <= 6193) return 0x9d36ff494d678f53ull;
            if (sample <= 6196) return 0xf8aab7ac6634a037ull;
            if (sample <= 6198) return 0x9d36ff494d678f53ull;
            if (sample <= 6201) return 0xf8aab7ac6634a037ull;
            if (sample <= 6203) return 0x9d36ff494d678f53ull;
            if (sample <= 6564) return 0xc118cc3e4eff3d27ull;
            if (sample <= 6604) return 0x9d36ff494d678f53ull;
            if (sample == 6605) return 0x4ef23805b3be66a5ull;
            if (sample <= 6607) return 0x2cc687e766578183ull;
            if (sample == 6608) return 0x413bd71b36091f2bull;
            if (sample <= 6610) return 0xb1a8f948642db583ull;
            if (sample == 6611) return 0xb87498efff8f0b15ull;
            if (sample <= 7694) return 0x2faa0bed13235713ull;
            if (sample == 7695) return 0x6d146f120a0bcdb5ull;
            if (sample == 7696) return 0x2cc687e766578183ull;
            if (sample == 7697) return 0x3f124d04d181aa5eull;
            if (sample == 7698) return 0xf79c067ba1bc34f6ull;
            if (sample == 7699) return 0xf75309bcac42bb83ull;
            if (sample == 7700) return 0xa3e68e51e618e368ull;
            if (sample <= 8782) return 0x5af513209bd9c79dull;
            if (sample == 8783) return 0xa804ae173352194cull;
            if (sample <= 8785) return 0x2cc687e766578183ull;
            if (sample == 8786) return 0x8887db592e3c9ca1ull;
            if (sample <= 8790) return 0xb1a8f948642db583ull;
            if (sample == 8791) return 0x091ea6c28742c685ull;
            return 0xe26034f67e3540cfull;
        };

        // Every native page through the third-board collision and Hall is
        // exact in the source, except the explicitly retained completed Wipe
        // pages at the end. Source-only frames 8920, 9259, 9312, 9324, 9336,
        // and 9709 are pixel-perfect horizontal splices of their adjacent
        // actor poses. Frame 9754 is only the bottom five black rows of the
        // Hall painter. None is a logical state for the double-buffered port.
        static constexpr std::array<int, 80> ThirdBoardPageStarts = {
            8792, 8867, 8869, 8872, 8874, 8877, 8879, 8932, 8934, 8937,
            8939, 8942, 8944, 8946, 8950, 9009, 9011, 9014, 9016, 9019,
            9021, 9023, 9084, 9112, 9115, 9117, 9120, 9122, 9124, 9127,
            9130, 9168, 9170, 9173, 9175, 9177, 9180, 9271, 9274, 9276,
            9278, 9281, 9283, 9286, 9289, 9302, 9305, 9307, 9310, 9312,
            9318, 9319, 9322, 9324, 9326, 9329, 9331, 9334, 9336, 9339,
            9341, 9343, 9346, 9348, 9351, 9353, 9355, 9358, 9360, 9363,
            9365, 9367, 9728, 9769, 9770, 9771, 9772, 9773, 9774, 9775,
        };
        static constexpr std::array<std::uint64_t, 80> ThirdBoardPageHashes = {
            0xe26034f67e3540cfull, 0xf28f02ac567782c3ull,
            0x2e52bbf924863213ull, 0x930ac174263bd48full,
            0xa049a9f6e3b36223ull, 0xbeb50d43ba121a1aull,
            0x50fcf11935a2c622ull, 0x5ab8af87ece09951ull,
            0xdaa33cc8461b9669ull, 0x5ab8af87ece09951ull,
            0xdaa33cc8461b9669ull, 0x5ab8af87ece09951ull,
            0xdaa33cc8461b9669ull, 0x5ab8af87ece09951ull,
            0xdaa33cc8461b9669ull, 0x493279dbc1806b67ull,
            0x6999abaeae87d3d0ull, 0x56b92ed075262b4eull,
            0x2d9e4235fb03ae6bull, 0xa43819d839021d82ull,
            0x88052bdddf013712ull, 0x782777cc1d03fb68ull,
            0xa32ad730d8e75fbeull, 0xd860a4b8cea287ebull,
            0xcff6f6bf6ee8c4f7ull, 0xd860a4b8cea287ebull,
            0xcff6f6bf6ee8c4f7ull, 0xd860a4b8cea287ebull,
            0xcff6f6bf6ee8c4f7ull, 0xd860a4b8cea287ebull,
            0xcff6f6bf6ee8c4f7ull, 0x3a7f1f92caec3643ull,
            0xe664ab1f3b3f599bull, 0x37650caab9ddb73bull,
            0x2703eab146e7840bull, 0xe50b66e273550286ull,
            0x2305a5392aa17e8eull, 0x77ffd42578a1887dull,
            0xa3e997d27a4757adull, 0x77ffd42578a1887dull,
            0xa3e997d27a4757adull, 0x77ffd42578a1887dull,
            0xa3e997d27a4757adull, 0x77ffd42578a1887dull,
            0xa3e997d27a4757adull, 0x67de157c463fb36dull,
            0x49e0e1b5330a7ca4ull, 0xaa466216e4fd7b43ull,
            0x6becbdf50bf8b664ull, 0xab4fa68fa4c627abull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0xc90c5ab4f6e3eea2ull,
            0x5feb0484166bfe3eull, 0x24c00484fedde1a8ull,
            0xb2d53fffb0373f39ull, 0x9fd08d4c99283a5dull,
            0x2cc687e766578183ull, 0x8a6d8c6b73f1ac1bull,
            0xba04247ef217e4adull, 0xb1a8f948642db583ull,
            0x99fdbcb6bd37a9c3ull, 0x5a897b0f29bc05efull,
        };

        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(0xf585u);
        game.startAttract();
        Renderer renderer;
        std::size_t thirdBoardPage = 0;
        for (int sample = 0; sample <= LastSample; ++sample) {
            game.renderPresentation(renderer);
            const std::uint64_t hash = renderedFrameHash(renderer);
            if (sample == 4575 &&
                (hash != 0x7f26c285575459f7ull ||
                 game.random_.calls != 279 ||
                 game.random_.state != 0xceb33a9cu ||
                 game.attractBoardIndex_ != 1 || game.level_ != 2 ||
                 game.attractDifficultyIndex_ != 1 ||
                 game.activeBoardMode_ != Game::Mode::Primes ||
                 game.playerRow_ != 1 || game.playerColumn_ != 4 ||
                 game.enemySlotCount_ != 1 || game.enemySlots_[0].type != 2)) {
                return false;
            }
            if (sample >= FirstSample && sample <= ThirdBoardFirstSample &&
                hash != expectedHash(sample)) {
                return false;
            }
            if (sample >= ThirdBoardFirstSample &&
                sample <= ThirdBoardHallSample) {
                while (thirdBoardPage + 1 < ThirdBoardPageStarts.size() &&
                       sample >= ThirdBoardPageStarts[thirdBoardPage + 1]) {
                    ++thirdBoardPage;
                }
                if (hash != ThirdBoardPageHashes[thirdBoardPage]) return false;
            }
            if (sample >= 13064 && sample <= 13066 &&
                hash != 0x462dcc7071df04d0ull) {
                return false;
            }
            if (sample >= 13067 && sample <= 13068 &&
                hash != 0x3801efbdf83299e0ull) {
                return false;
            }
            if (sample == 13067 &&
                (game.lastPresentationAuditOrigin_ != 1378 ||
                 game.presentationFrames_.size() != 1 || game.moving_ ||
                 game.playerRow_ != 1 || game.playerColumn_ != 5 ||
                 game.playerTerminalFrame_ != 8 || game.random_.calls != 600 ||
                 game.random_.state != 0xafd0de0du)) {
                return false;
            }
            if (sample >= 13555 && sample <= 13561 &&
                hash != 0xc64d408bfff8d96aull) {
                return false;
            }
            if (sample >= 13562 && sample <= 13568 &&
                hash != 0x3870ecb93123843aull) {
                return false;
            }
            if (sample == 13562 &&
                (game.random_.calls != 614 ||
                 game.random_.state != 0x0c0bf4ffu || !game.munching_ ||
                 game.attractMunchEnemyTerminalHoldPixels_.empty())) {
                return false;
            }
            if (sample == 13569 &&
                (hash != 0x36617503b298b4e0ull ||
                 game.lastPresentationAuditOrigin_ != 1490 ||
                 game.random_.calls != 614 ||
                 game.random_.state != 0x0c0bf4ffu ||
                 !game.attractMunchEnemyTerminalHoldPixels_.empty())) {
                return false;
            }
            if (sample >= 13570 && sample <= 13571 &&
                hash != 0x7f0e1d3a3f171570ull) {
                return false;
            }
            if (sample >= 13572 && sample <= 13573 &&
                hash != 0x36617503b298b4e0ull) {
                return false;
            }
            if (sample >= 13574 && sample <= 13576 &&
                hash != 0x7f0e1d3a3f171570ull) {
                return false;
            }
            if (sample >= 13577 && sample <= 13579 &&
                hash != 0x36617503b298b4e0ull) {
                return false;
            }
            if (sample == 6059 &&
                (game.random_.calls != 319 ||
                 game.random_.state != 0x91b264b4u ||
                 game.enemyPresentationLagSlot_ != 0 ||
                 game.enemyPresentationLagHistory_.size() != 17)) {
                return false;
            }
            if (sample == 6060 &&
                (game.random_.calls != 322 ||
                 game.random_.state != 0x9f18e853u || !game.moving_ ||
                 game.playerMovementPhase() != 0 ||
                 game.enemyPresentationLagSlot_ != 0 ||
                 game.enemyPresentationLagTicks_ != 17)) {
                return false;
            }
            if (sample == 6100 &&
                (game.random_.calls != 325 ||
                 game.random_.state != 0x49b3e7e6u ||
                 game.enemyPresentationLagSlot_ != -1 ||
                 !game.attractPrimeExitPlayerCatchup_ ||
                 game.presentationFrames_.size() != 8 || !game.moving_ ||
                 game.playerMovementPhase() != 2)) {
                return false;
            }
            if (sample == 6113 &&
                (game.attractPrimeExitPlayerCatchup_ ||
                 !game.presentationFrames_.empty() || game.moving_ ||
                 game.playerTerminalFrame_ != -1)) {
                return false;
            }
            if (sample == 6204 &&
                (game.page_ != Game::Page::Feedback ||
                 game.feedbackKind_ != Game::FeedbackKind::WrongAnswer ||
                 game.feedbackMessage_ != "The number \"1\" is not prime." ||
                 game.playerTerminalFrame_ != 12)) {
                return false;
            }
            if (sample == 6565 &&
                (!game.attractPostFeedbackBoard_ ||
                 game.attractHallTransitionPhase_ !=
                     Game::AttractHallTransitionPhase::BoardHold)) {
                return false;
            }
            if (sample == 7700 &&
                (game.attractInterstitialTransition_ !=
                     Game::AttractInterstitialTransition::HallToSplash ||
                 game.attractInterstitialFrame_ != 5)) {
                return false;
            }
            if (sample == 8791 &&
                (game.attractInterstitialTransition_ !=
                     Game::AttractInterstitialTransition::SplashToBoard ||
                 game.attractInterstitialFrame_ != 8 ||
                 game.attractBoardIndex_ != 2 ||
                 game.activeBoardMode_ != Game::Mode::Inequality ||
                 game.target_ != 24 || game.playerRow_ != 1 ||
                 game.playerColumn_ != 1 || game.random_.calls != 480 ||
                 game.random_.state != 0xfb64e725u)) {
                return false;
            }
            if (sample == ThirdBoardHallSample &&
                (game.page_ != Game::Page::Hall ||
                 game.attractBoardIndex_ != 2 ||
                 game.activeBoardMode_ != Game::Mode::Inequality ||
                 game.target_ != 24 ||
                 game.attractInterstitialTransition_ !=
                     Game::AttractInterstitialTransition::None ||
                 game.random_.calls != 499 ||
                 game.random_.state != 0x284e0200u ||
                 game.feedbackKind_ != Game::FeedbackKind::None ||
                 !game.presentationFrames_.empty() ||
                 game.enemyPresentationLagSlot_ != -1 ||
                 game.attractPrimeExitPlayerCatchup_)) {
                return false;
            }
            if (sample == 13069 &&
                (hash != 0x0cce927e5de730d0ull ||
                 game.page_ != Game::Page::Attract ||
                 game.attractBoardIndex_ != 3 || game.level_ != 3 ||
                 game.attractDifficultyIndex_ != 2 ||
                 game.activeBoardMode_ != Game::Mode::Primes ||
                 game.target_ != 0 || game.score_ != 25 ||
                 game.playerRow_ != 1 || game.playerColumn_ != 5 ||
                 game.moving_ || game.playerTerminalFrame_ != -1 ||
                 game.random_.calls != 600 ||
                 game.random_.state != 0xafd0de0du ||
                 !game.cell(2, 1).safe ||
                 !game.presentationFrames_.empty())) {
                return false;
            }
            if (sample == 13580 &&
                (hash != 0x7f0e1d3a3f171570ull ||
                 game.page_ != Game::Page::Attract ||
                 game.attractBoardIndex_ != 3 || game.level_ != 3 ||
                 game.activeBoardMode_ != Game::Mode::Primes ||
                 game.score_ != 40 || game.playerRow_ != 4 ||
                 game.playerColumn_ != 5 || game.moving_ || game.munching_ ||
                 game.playerTerminalFrame_ != 13 ||
                 game.random_.calls != 614 ||
                 game.random_.state != 0x0c0bf4ffu ||
                 !game.presentationFrames_.empty() ||
                  !game.attractMunchEnemyTerminalHoldPixels_.empty())) {
                return false;
            }
            if (sample == 13675 &&
                (hash != 0xfdedc24e0714bba6ull ||
                 game.lastPresentationAuditOrigin_ != 1518 ||
                 game.random_.calls != 618 ||
                 game.random_.state != 0x912a2b8bu)) {
                return false;
            }
            if (sample == 13767 &&
                (hash != 0xca1134999bea4106ull ||
                 game.lastPresentationAuditOrigin_ != 1406 ||
                 game.random_.calls != 622 ||
                 game.random_.state != 0xa3788457u)) {
                return false;
            }
            if (sample == 13935 && hash != 0x1418efb77d0b9b6cull) {
                return false;
            }
            if (sample == 13936 && hash != 0xe220d03968f0ac07ull) {
                return false;
            }
            static constexpr std::array<std::uint64_t, 6>
                FourthPrimeExitDrainHashes = {
                    0x6d7b876e5070c4cbull, 0x3b522a60b73f0adaull,
                    0x5ab2e7e0b940386bull, 0x6d1c192d1b5354bbull,
                    0xf882b0d4fa7f65b5ull, 0x5ef7368b35cd8be7ull,
                };
            if (sample >= 13971 && sample <= 13976 &&
                hash != FourthPrimeExitDrainHashes[
                    static_cast<std::size_t>(sample - 13971)]) {
                return false;
            }
            if (sample == 13976 &&
                (game.random_.calls != 627 ||
                 game.random_.state != 0xb99ee580u ||
                 game.enemyPresentationLagSlot_ != -1 ||
                 !game.presentationFrames_.empty())) {
                return false;
            }
            if (sample == 15010 &&
                (hash != 0x8c44619ad76abefaull || !game.moving_ ||
                 game.playerMovementPhase() != 3 ||
                 game.random_.calls != 662 ||
                 game.random_.state != 0xba1de80fu)) {
                return false;
            }
            if (sample == 15155 &&
                (hash != 0x8b3b60329f40e669ull || !game.moving_ ||
                 game.playerMovementPhase() != 0 ||
                 game.random_.calls != 669 ||
                 game.random_.state != 0x24e4dc8eu)) {
                return false;
            }
            if (sample == 15294 &&
                (hash != 0xade525d6425c9957ull ||
                 game.attractFourthPrimeSafeHoldPixels_.empty() ||
                 game.random_.calls != 676 ||
                 game.random_.state != 0x1b5a0f11u)) {
                return false;
            }
            if (sample == 15301 &&
                (hash != 0x543e054655667b31ull ||
                 !game.attractFourthPrimeSafeHoldPixels_.empty())) {
                return false;
            }
            if (sample == 15850 &&
                (hash != 0x4d7ab4c6a9d409e3ull ||
                 game.attractFourthPrimeSafeHoldPixels_.empty() ||
                 game.random_.calls != 697 ||
                 game.random_.state != 0x15a4ca52u)) {
                return false;
            }
            if (sample == 15857 &&
                (hash != 0x8313c4c085d87b89ull ||
                 !game.attractFourthPrimeSafeHoldPixels_.empty())) {
                return false;
            }
            if (sample == 15858 && hash != 0xb6f99c19597eb591ull) {
                return false;
            }
            if (sample == 15869 &&
                (hash != 0x98bec8c9a901a596ull ||
                 game.lastPresentationAuditOrigin_ != 1608 ||
                 game.random_.calls != 698 ||
                 game.random_.state != 0x2396defbu)) {
                return false;
            }
            if (sample == FourthBoardLastSample &&
                (hash != 0x679e7fdf92b78ab7ull ||
                 game.page_ != Game::Page::Attract ||
                 game.attractBoardIndex_ != 3 ||
                 game.activeBoardMode_ != Game::Mode::Primes ||
                 game.score_ != 70 || game.playerRow_ != 3 ||
                 game.playerColumn_ != 1 || game.moving_ || game.munching_ ||
                 game.playerTerminalFrame_ != 13 ||
                 game.random_.calls != 699 ||
                 game.random_.state != 0xb56aa3f8u ||
                 !game.presentationFrames_.empty() ||
                 !game.attractFourthPrimeSafeHoldPixels_.empty())) {
                return false;
            }
            if (sample == 20853 &&
                (hash != 0xa899cd12994eecfeull ||
                 game.page_ != Game::Page::Feedback ||
                 game.feedbackKind_ != Game::FeedbackKind::EatenByTroggle ||
                 game.feedbackEnemyType_ != 0 ||
                 game.feedbackMessage_ != "Aargh" ||
                 game.attractBoardIndex_ != 4 || game.level_ != 5 ||
                 game.attractDifficultyIndex_ != 4 ||
                 game.activeBoardMode_ != Game::Mode::Primes ||
                 game.playerRow_ != 4 || game.playerColumn_ != 2 ||
                 game.random_.calls != 853 ||
                 game.random_.state != 0x78390cd6u)) {
                std::cerr << "F585 checkpoint 20853 failed\n";
                return false;
            }
            if (sample == 21215 &&
                (hash != 0x5a7c0feac12111b6ull ||
                 !game.attractPostFeedbackBoard_ ||
                 game.attractHallTransitionPhase_ !=
                     Game::AttractHallTransitionPhase::BoardHold ||
                 game.feedbackMessage_ != "Aargh" ||
                 game.random_.calls != 853 ||
                 game.random_.state != 0x78390cd6u)) {
                std::cerr << "F585 checkpoint 21215 failed\n";
                return false;
            }
            if (sample == 23441 &&
                (hash != 0x55761275c6a0305dull ||
                 game.page_ != Game::Page::Attract ||
                 game.attractBoardIndex_ != 5 || game.level_ != 6 ||
                 game.attractDifficultyIndex_ != 5 ||
                 game.activeBoardMode_ != Game::Mode::Inequality ||
                 game.target_ != 20 || game.relation_ != 0 ||
                 game.playerRow_ != 2 || game.playerColumn_ != 1 ||
                 game.enemySlotCount_ != 2 ||
                 game.enemySlots_[0].type != 1 ||
                 game.enemySlots_[1].type != 3 ||
                 game.random_.calls != 1004 ||
                 game.random_.state != 0x42d476e9u)) {
                std::cerr << "F585 checkpoint 23441 failed\n";
                return false;
            }
            if (sample == 23802 &&
                (hash != 0x89fd276642ce25e7ull ||
                 game.playerRow_ != 0 || game.playerColumn_ != 3 ||
                 game.playerTerminalFrame_ != 4 ||
                 game.random_.calls != 1015 ||
                 game.random_.state != 0x683114bcu)) {
                std::cerr << "F585 checkpoint 23802 failed\n";
                return false;
            }
            if (sample == 23805 &&
                (hash != 0x4876d4ba4445be25ull ||
                 game.lastPresentationAuditOrigin_ != 1392 ||
                 game.playerTerminalFrame_ != -1 ||
                 game.random_.calls != 1015 ||
                 game.random_.state != 0x683114bcu)) {
                std::cerr << "F585 checkpoint 23805 failed\n";
                return false;
            }
            if (sample == 23806 &&
                (hash != 0x39ca790876f9a30aull ||
                 !game.presentationFrames_.empty() ||
                 game.playerTerminalFrame_ != -1)) {
                std::cerr << "F585 checkpoint 23806 failed\n";
                return false;
            }
            if (sample == 23954 &&
                (hash != 0x596210c055fe45bdull ||
                 game.page_ != Game::Page::Feedback ||
                 game.feedbackKind_ != Game::FeedbackKind::WrongAnswer ||
                 game.feedbackMessage_ != "Oops!  \"35-5=30\"" ||
                 game.score_ != 60 || game.playerTerminalFrame_ != 12 ||
                 game.random_.calls != 1020 ||
                 game.random_.state != 0x44b60a19u)) {
                std::cerr << "F585 checkpoint 23954 failed\n";
                return false;
            }
            if (sample == 24362 &&
                (hash != 0x5a897b0f29bc05efull ||
                 game.page_ != Game::Page::Hall ||
                 game.feedbackKind_ != Game::FeedbackKind::None ||
                 game.attractBoardIndex_ != 5 || game.score_ != 60 ||
                 game.random_.calls != 1020 ||
                 game.random_.state != 0x44b60a19u)) {
                std::cerr << "F585 checkpoint 24362 failed\n";
                return false;
            }
            if (sample == LastSample &&
                (hash != 0x5af513209bd9c79dull ||
                 game.page_ != Game::Page::StartupSplash ||
                 game.attractBoardIndex_ != 5 || game.level_ != 6 ||
                 game.activeBoardMode_ != Game::Mode::Inequality ||
                 game.target_ != 20 || game.score_ != 60 ||
                 game.playerRow_ != 1 || game.playerColumn_ != 3 ||
                 game.playerTerminalFrame_ != 12 ||
                 game.random_.calls != 1020 ||
                 game.random_.state != 0x44b60a19u ||
                 !game.presentationFrames_.empty())) {
                std::cerr << "F585 checkpoint 25700 failed\n";
                return false;
            }
            if (sample != LastSample) game.update(SampleSeconds);
        }
        return true;
    }
    static bool dumpSmartyAttractSequence() {
        const char* directoryText =
            std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_SMARTY_DIR");
        if (!directoryText || !*directoryText) return false;
        const std::filesystem::path directory(directoryText);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            std::cerr << "Could not create Number Smarty dump directory: "
                      << directory << " (" << error.message() << ")\n";
            return false;
        }

        constexpr std::uint32_t LiveSeed = 0xae67u;
        constexpr int SampleCount = 1'300;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        Game game;
        applyCapturedSmartyConfig(game);
        game.random_.seed(LiveSeed);
        game.startAttract();

        std::ofstream states(directory / "number-smarty-pages.tsv",
                             std::ios::binary | std::ios::trunc);
        if (!states) return false;
        states << "page\tsample\ttime_seconds\thash\tlogical_hash"
                  "\tcontroller_page\tlevel"
                  "\trandom_calls\trandom_state\tplayer_row\tplayer_column"
                  "\tmoving\tmunching\tterminal_frame\tdeath_animating"
                  "\tdeath_ticks\twarning\tpresentation_queue"
                  "\tentry_hold\twarning_hold\tdeath_surface"
                  "\tfeedback_kind\tfeedback_message\tfeedback_slot"
                  "\tpost_feedback\tpaint_order"
                  "\tenemy_slots\tactors\n";

        Renderer renderer;
        std::uint64_t previousHash = 0;
        int pageIndex = 0;
        const auto writeFrame = [&](const std::filesystem::path& path) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << "P6\n" << Renderer::Width << ' ' << Renderer::Height
                   << "\n255\n";
            for (const std::uint32_t pixel : renderer.pixels()) {
                const char rgb[3] = {
                    static_cast<char>((pixel >> 16) & 0xffu),
                    static_cast<char>((pixel >> 8) & 0xffu),
                    static_cast<char>(pixel & 0xffu),
                };
                output.write(rgb, sizeof(rgb));
            }
            return static_cast<bool>(output);
        };

        for (int sample = 0; sample < SampleCount; ++sample) {
            game.renderPresentation(renderer);
            const std::uint64_t hash = renderedFrameHash(renderer);
            if (pageIndex == 0 || hash != previousHash) {
                std::string number = std::to_string(pageIndex);
                number.insert(number.begin(),
                              5u - std::min<std::size_t>(5u, number.size()), '0');
                if (!writeFrame(directory / ("smarty-page-" + number + ".ppm"))) {
                    return false;
                }
                states << pageIndex << '\t' << sample << '\t'
                       << sample * SampleSeconds << "\t0x" << std::hex << hash
                       << "\t0x" << fullFrameHash(game)
                       << std::dec << '\t' << static_cast<int>(game.page_) << '\t'
                       << game.level_ << '\t' << game.random_.calls << "\t0x"
                       << std::hex << game.random_.state << std::dec << '\t'
                       << game.playerRow_ << '\t' << game.playerColumn_ << '\t'
                       << game.moving_ << '\t' << game.munching_ << '\t'
                       << game.playerTerminalFrame_ << '\t'
                       << game.deathAnimating_ << '\t'
                       << game.deathSequenceTicks_ << '\t'
                       << game.enemyWarning_ << '\t'
                       << game.presentationFrames_.size() << '\t'
                       << !game.attractPlayerEntryPresentationHoldPixels_.empty() << '\t'
                       << !game.attractPlayerWarningHoldPixels_.empty() << '\t'
                       << !game.deathResidentSurfacePixels_.empty() << '\t'
                       << static_cast<int>(game.feedbackKind_) << '\t'
                       << game.feedbackMessage_ << '\t'
                       << game.feedbackEnemySlot_ << '\t'
                       << game.attractPostFeedbackBoard_ << '\t';
                for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                    if (slot) states << ':';
                    states << game.enemyPaintOrder_[static_cast<std::size_t>(slot)];
                }
                states << '\t';
                for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                    if (slot) states << ';';
                    const Game::EnemySlot& enemy =
                        game.enemySlots_[static_cast<std::size_t>(slot)];
                    states << slot << ':' << enemy.type << ':'
                           << static_cast<int>(enemy.phase) << ':'
                           << enemy.timer * OriginalSchedulerTicksPerSecond;
                }
                states << '\t';
                for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                    if (index) states << ';';
                    const Game::Enemy& enemy = game.enemies_[index];
                    states << enemy.slot << ':' << enemy.type << ':'
                           << enemy.row << ':' << enemy.column << ':'
                           << enemy.direction << ':' << enemy.moving << ':'
                           << enemy.entering << ':' << enemy.exiting << ':'
                           << enemy.cannibalizing << ':'
                           << enemy.animationTimer * OriginalSchedulerTicksPerSecond
                           << ':' << enemy.moveTimer * OriginalSchedulerTicksPerSecond;
                }
                states << '\n';
                previousHash = hash;
                ++pageIndex;
            }
            game.update(SampleSeconds);
        }
        std::cout << "number_smarty_attract_seed=0x" << std::hex << LiveSeed
                  << std::dec << " samples=" << SampleCount
                  << " changed_pages=" << pageIndex
                  << " final_calls=" << game.random_.calls
                  << " final_state=0x" << std::hex << game.random_.state
                  << std::dec << '\n';
        return true;
    }
    static bool dumpAutonomousAttractSequence() {
        const char* directoryText =
            std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_AUTONOMOUS_DIR");
        if (!directoryText || !*directoryText) return false;
        const char* ppmFirstText =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_PPM_FIRST");
        const char* ppmLastText =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_PPM_LAST");
        const int ppmFirst = ppmFirstText ? std::atoi(ppmFirstText) : 1;
        const int ppmLast = ppmLastText ? std::atoi(ppmLastText) : 0;
        const char* samplePpmFirstText =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_SAMPLE_PPM_FIRST");
        const char* samplePpmLastText =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_SAMPLE_PPM_LAST");
        const int samplePpmFirst = samplePpmFirstText
            ? std::atoi(samplePpmFirstText) : 1;
        const int samplePpmLast = samplePpmLastText
            ? std::atoi(samplePpmLastText) : 0;
        const std::filesystem::path directory(directoryText);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return false;

        constexpr std::uint32_t Seed = 0xf585u;
        const char* sampleCountText =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_SAMPLE_COUNT");
        const int sampleCount = sampleCountText
            ? std::max(1, std::atoi(sampleCountText)) : 25'700;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        Game game;
        applyCapturedSmartyConfig(game);
        for (auto& list : game.scores_) list.clear();
        game.random_.seed(Seed);
        game.startAttract();
        if (game.random_.state != 0x5f26e95cu || game.random_.calls != 87 ||
            game.level_ != 10 || game.attractDifficultyIndex_ != 9 ||
            game.activeBoardMode_ != Game::Mode::Factors || game.target_ != 57 ||
            game.enemySlotCount_ != 3 || game.enemySlots_[0].type != 1 ||
            game.enemySlots_[1].type != 1 || game.enemySlots_[2].type != 3) {
            return false;
        }

        std::ofstream states(directory / "number-autonomous-pages.tsv",
                             std::ios::binary | std::ios::trunc);
        if (!states) return false;
        const bool dumpEverySample =
            std::getenv("MUNCHERS_AUDIT_NUMBER_AUTONOMOUS_ALL_SAMPLES") != nullptr;
        std::ofstream samples;
        if (dumpEverySample) {
            samples.open(directory / "number-autonomous-samples.tsv",
                         std::ios::binary | std::ios::trunc);
            if (!samples) return false;
            samples << "sample\thash\tlogical_hash\tqueue_hashes"
                       "\tqueue_origins\tpresented_origin"
                       "\trandom_calls\trandom_state\tattract_action_ticks"
                       "\tbefore_player_hash\tafter_player_hash"
                       "\tafter_player_dirty_hash\tafter_troggles_hash"
                       "\tenemy_callback_slots\tenemy_callback_hashes"
                       "\tmoving\tplayer_phase\tterminal_frame"
                       "\tmunching\tmunch_timer_ticks\tmunch_entry_slot"
                       "\tmunch_entry_overlap\tfactors_phase"
                       "\tentry_surface_hash\tentry_hold_hash"
                       "\tmunch_concurrent_hash\tqueue\tlag_slot"
                       "\tlag_ticks\tlag_enemy_history\tlag_player_history"
                       "\tlag_pending_players\tlag_displayed_player"
                       "\tlag_player_records"
                       "\tphase_zero_hold\tentry_resident\tsafe_warning_hold"
                       "\tdual_enemy_hold_hash\tdual_enemy_await"
                       "\tplayer_concurrent_slot\tplayer_concurrent_hold_hash"
                       "\tactors\n";
        }
        states << "page\tsample\ttime_seconds\thash\tcontroller_page\tboard"
                  "\tlevel\tpressure\tmode\ttarget\tscore\trandom_calls"
                  "\trandom_state\tfeedback_kind\tfeedback_enemy_type"
                  "\tfeedback_message\tplayer_row\tplayer_column"
                  "\tmoving\tmunching\tterminal_frame\tdeath_animating"
                  "\tdeath_ticks\twarning\tpresentation_queue"
                  "\tlag_slot\tlag_ticks\tlag_history\tlag_pixels"
                  "\tphase_zero_hold\tentry_resident\tentry_hold"
                  "\tdeparting_hold\tsafe_warning_hold\twarning_hold"
                  "\tdeath_surface"
                  "\tpost_feedback\tpaint_order\tenemy_slots\tactors\n";

        Renderer renderer;
        std::uint64_t previousHash = 0;
        int pageIndex = 0;
        const auto writeFrame = [&](const std::filesystem::path& path,
                                    const Renderer& sourceRenderer) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << "P6\n" << Renderer::Width << ' ' << Renderer::Height
                   << "\n255\n";
            for (const std::uint32_t pixel : sourceRenderer.pixels()) {
                const char rgb[3] = {
                    static_cast<char>((pixel >> 16) & 0xffu),
                    static_cast<char>((pixel >> 8) & 0xffu),
                    static_cast<char>(pixel & 0xffu),
                };
                output.write(rgb, sizeof(rgb));
            }
            return static_cast<bool>(output);
        };
        for (int sample = 0; sample < sampleCount; ++sample) {
            const std::uint64_t logicalHash =
                dumpEverySample ? fullFrameHash(game) : 0;
            std::vector<std::uint64_t> queuedHashes;
            std::vector<int> queuedOrigins;
            if (dumpEverySample) {
                queuedHashes.reserve(game.presentationFrames_.size());
                for (const auto& pixels : game.presentationFrames_) {
                    queuedHashes.push_back(pixelFrameHash(pixels));
                }
                queuedOrigins.assign(game.presentationFrameAuditOrigins_.begin(),
                                     game.presentationFrameAuditOrigins_.end());
            }
            game.renderPresentation(renderer);
            const std::uint64_t hash = renderedFrameHash(renderer);
            if (sample >= samplePpmFirst && sample <= samplePpmLast) {
                std::string number = std::to_string(sample);
                number.insert(number.begin(),
                              5u - std::min<std::size_t>(5u, number.size()), '0');
                if (!writeFrame(directory / ("autonomous-sample-" + number +
                                              "-presented.ppm"), renderer)) {
                    return false;
                }
                Renderer logicalRenderer;
                game.render(logicalRenderer);
                if (!writeFrame(directory / ("autonomous-sample-" + number +
                                              "-logical.ppm"), logicalRenderer)) {
                    return false;
                }
                const auto writeAuditPixels = [&](const char* suffix,
                                                  const std::vector<std::uint32_t>& pixels) {
                    if (pixels.empty()) return true;
                    Renderer auditRenderer(game.assets_.graphicsMode());
                    auditRenderer.replacePixels(pixels);
                    return writeFrame(
                        directory / ("autonomous-sample-" + number + suffix),
                        auditRenderer);
                };
                if (!writeAuditPixels("-before-player.ppm",
                                      game.auditBeforePlayerPixels_) ||
                    !writeAuditPixels("-after-player.ppm",
                                      game.auditAfterPlayerPixels_) ||
                    !writeAuditPixels("-after-player-dirty.ppm",
                                      game.auditAfterPlayerCallbackPixels_) ||
                    !writeAuditPixels("-after-troggles.ppm",
                                      game.auditAfterTrogglesPixels_)) {
                    return false;
                }
                for (std::size_t index = 0;
                     index < game.enemyCallbackPresentationFrames_.size(); ++index) {
                    Renderer callbackRenderer(game.assets_.graphicsMode());
                    callbackRenderer.replacePixels(
                        game.enemyCallbackPresentationFrames_[index]);
                    if (!writeFrame(
                            directory / ("autonomous-sample-" + number +
                                         "-enemy-callback-" +
                                         std::to_string(index) + ".ppm"),
                            callbackRenderer)) {
                        return false;
                    }
                }
            }
            if (dumpEverySample) {
                samples << sample << "\t0x" << std::hex << hash << std::dec
                        << "\t0x" << std::hex << logicalHash << std::dec << '\t';
                for (std::size_t index = 0; index < queuedHashes.size(); ++index) {
                    if (index) samples << ',';
                    samples << "0x" << std::hex << queuedHashes[index] << std::dec;
                }
                samples << '\t';
                for (std::size_t index = 0; index < queuedOrigins.size(); ++index) {
                    if (index) samples << ',';
                    samples << queuedOrigins[index];
                }
                samples << '\t' << game.lastPresentationAuditOrigin_
                        << '\t' << game.random_.calls
                        << "\t0x" << std::hex << game.random_.state << std::dec
                        << '\t' << game.attractActionTimer_ *
                                       OriginalSchedulerTicksPerSecond
                        << "\t0x" << std::hex
                        << pixelFrameHash(game.auditBeforePlayerPixels_)
                        << "\t0x"
                        << pixelFrameHash(game.auditAfterPlayerPixels_)
                        << "\t0x"
                        << pixelFrameHash(game.auditAfterPlayerCallbackPixels_)
                        << "\t0x"
                        << pixelFrameHash(game.auditAfterTrogglesPixels_)
                        << std::dec << '\t';
                for (std::size_t index = 0;
                     index < game.enemyCallbackPresentationSlots_.size(); ++index) {
                    if (index) samples << ',';
                    samples << game.enemyCallbackPresentationSlots_[index];
                }
                samples << '\t';
                for (std::size_t index = 0;
                     index < game.enemyCallbackPresentationFrames_.size(); ++index) {
                    if (index) samples << ',';
                    samples << "0x" << std::hex
                            << pixelFrameHash(
                                   game.enemyCallbackPresentationFrames_[index])
                            << std::dec;
                }
                samples
                        << '\t' << game.moving_ << '\t'
                        << (game.moving_ ? game.playerMovementPhase() : -1) << '\t'
                        << game.playerTerminalFrame_ << '\t'
                        << game.munching_ << '\t'
                        << game.munchTimer_ * OriginalSchedulerTicksPerSecond << '\t'
                        << game.attractMunchResidentEntrySlot_ << '\t'
                        << game.attractPlayerEntryMunchOverlap_ << '\t'
                        << game.attractFactorsConcurrentPhase_ << '\t'
                        << "0x" << std::hex
                        << pixelFrameHash(
                               game.attractPlayerEntryResidentSurfacePixels_)
                        << "\t0x"
                        << pixelFrameHash(
                               game.attractPlayerEntryPresentationHoldPixels_)
                        << "\t0x"
                        << pixelFrameHash(
                               game.attractMunchConcurrentEnemyPixels_)
                        << std::dec << '\t'
                        << game.presentationFrames_.size() << '\t'
                        << game.enemyPresentationLagSlot_ << '\t'
                        << game.enemyPresentationLagTicks_ << '\t'
                        << game.enemyPresentationLagHistory_.size() << '\t'
                        << game.enemyPresentationLagPlayerHistory_.size() << '\t'
                        << game.enemyPresentationLagPendingPlayers_.size() << '\t'
                        << game.enemyPresentationLagDisplayedPlayer_.row << ':'
                        << game.enemyPresentationLagDisplayedPlayer_.column << ':'
                        << game.enemyPresentationLagDisplayedPlayer_.moving << ':'
                        << game.enemyPresentationLagDisplayedPlayer_.movementPhase
                        << ':'
                        << game.enemyPresentationLagDisplayedPlayer_.terminalFrame
                        << '\t';
                for (std::size_t index = 0;
                     index < game.enemyPresentationLagPlayerHistory_.size();
                     ++index) {
                    if (index) samples << ',';
                    const Game::PresentationPlayerState& player =
                        game.enemyPresentationLagPlayerHistory_[index];
                    samples << player.row << ':' << player.column << ':'
                            << player.moving << ':' << player.movementPhase << ':'
                            << player.terminalFrame;
                }
                samples << '\t'
                        << !game.attractPlayerPhaseZeroHoldPixels_.empty() << '\t'
                        << !game.attractPlayerEntryResidentSurfacePixels_.empty()
                        << '\t'
                        << !game.attractSafeZoneWarningHoldPixels_.empty() << '\t'
                        << "0x" << std::hex
                        << pixelFrameHash(game.attractConcurrentEnemyHoldPixels_)
                        << std::dec << '\t'
                        << game.attractConcurrentEnemyAwaitPlayerCallback_ << '\t'
                        << game.attractPlayerConcurrentEnemySlot_ << '\t'
                        << "0x" << std::hex
                        << pixelFrameHash(
                               game.attractPlayerConcurrentEnemyHoldPixels_)
                        << std::dec << '\t';
                for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                    if (index) samples << ';';
                    const Game::Enemy& enemy = game.enemies_[index];
                    samples << enemy.slot << ':' << enemy.type << ':'
                            << enemy.row << ':' << enemy.column << ':'
                            << enemy.direction << ':' << enemy.dwellFrame << ':'
                            << enemy.moving << ':' << enemy.entering << ':'
                            << enemy.animationTimer * OriginalSchedulerTicksPerSecond
                            << ':' << (enemy.moving
                                ? game.enemyMovementPhase(enemy) : -1);
                }
                samples << '\n';
            }
            if (pageIndex == 0 || hash != previousHash) {
                if (pageIndex >= ppmFirst && pageIndex <= ppmLast) {
                    std::string number = std::to_string(pageIndex);
                    number.insert(number.begin(),
                                  5u - std::min<std::size_t>(5u, number.size()),
                                  '0');
                    if (!writeFrame(
                            directory / ("autonomous-page-" + number + ".ppm"),
                            renderer)) {
                        return false;
                    }
                }
                states << pageIndex << '\t' << sample << '\t'
                       << sample * SampleSeconds << "\t0x" << std::hex << hash
                       << std::dec << '\t' << static_cast<int>(game.page_) << '\t'
                       << game.attractBoardIndex_ << '\t' << game.level_ << '\t'
                       << game.attractDifficultyIndex_ << '\t'
                       << static_cast<int>(game.activeBoardMode_) << '\t'
                       << game.target_ << '\t' << game.score_ << '\t'
                       << game.random_.calls << "\t0x" << std::hex
                       << game.random_.state << std::dec << '\t'
                       << static_cast<int>(game.feedbackKind_) << '\t'
                       << game.feedbackEnemyType_ << '\t'
                       << game.feedbackMessage_ << '\t' << game.playerRow_ << '\t'
                       << game.playerColumn_ << '\t' << game.moving_ << '\t'
                       << game.munching_ << '\t' << game.playerTerminalFrame_ << '\t'
                       << game.deathAnimating_ << '\t'
                       << game.deathSequenceTicks_ << '\t'
                       << game.enemyWarning_ << '\t'
                       << game.presentationFrames_.size() << '\t'
                       << game.enemyPresentationLagSlot_ << '\t'
                       << game.enemyPresentationLagTicks_ << '\t'
                       << game.enemyPresentationLagHistory_.size() << '\t'
                       << !game.enemyPresentationLagPixels_.empty() << '\t'
                       << !game.attractPlayerPhaseZeroHoldPixels_.empty() << '\t'
                       << !game.attractPlayerEntryResidentSurfacePixels_.empty()
                       << '\t'
                       << !game.attractPlayerEntryPresentationHoldPixels_.empty()
                       << '\t'
                       << !game.attractPlayerDepartingEnemyHoldPixels_.empty()
                       << '\t'
                       << !game.attractSafeZoneWarningHoldPixels_.empty() << '\t'
                       << !game.attractPlayerWarningHoldPixels_.empty() << '\t'
                       << !game.deathResidentSurfacePixels_.empty() << '\t'
                       << game.attractPostFeedbackBoard_ << '\t';
                for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                    if (slot) states << ':';
                    states << game.enemyPaintOrder_[static_cast<std::size_t>(slot)];
                }
                states << '\t';
                for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                    if (slot) states << ';';
                    const Game::EnemySlot& enemy =
                        game.enemySlots_[static_cast<std::size_t>(slot)];
                    states << slot << ':' << enemy.type << ':'
                           << static_cast<int>(enemy.phase) << ':'
                           << enemy.timer * OriginalSchedulerTicksPerSecond << ':'
                           << enemy.edge << ':' << enemy.row << ':' << enemy.column;
                }
                states << '\t';
                for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                    if (index) states << ';';
                    const Game::Enemy& enemy = game.enemies_[index];
                    states << enemy.slot << ':' << enemy.type << ':'
                           << enemy.row << ':' << enemy.column << ':'
                           << enemy.fromRow << ':' << enemy.fromColumn << ':'
                           << enemy.direction << ':' << enemy.dwellFrame << ':'
                           << enemy.moving << ':' << enemy.entering << ':'
                           << enemy.exiting << ':' << enemy.cannibalizing << ':'
                           << enemy.animationTimer * OriginalSchedulerTicksPerSecond
                           << ':' << enemy.moveTimer * OriginalSchedulerTicksPerSecond;
                }
                states << '\n';
                previousHash = hash;
                ++pageIndex;
            }
            game.update(SampleSeconds);
        }
        return static_cast<bool>(states);
    }
    static bool usesCapturedSmartyAttractPresentationSequence() {
        // Lossless DOSBox capture
        // analysis/number-live/gameplay/smarty-seed-ae67/
        // original-number-smarty-seed-ae67.avi, frames 2911..3405.
        // Nine nonuniform scaler frames plus uniform partial repaints 2906
        // and 2981..2982 are capture-only intermediate surfaces; every
        // completed presenter page from the Smarty entry onward is listed.
        constexpr std::array<std::uint64_t, 31> Expected = {
            0x7286548303894ab9ull,
            0xd3820577a8e7de5cull,
            0xdd36891d582f6227ull,
            0x3f4c17ef04830349ull,
            0x0432f481e40dae24ull,
            0x79dbe3d3a942acfcull,
            0x8c1c6438d6c5eaeeull,
            0x16ebf16b518c482dull,
            0xc2de38b321adc384ull,
            0x6e7e4c53c5793ca0ull,
            0xf508b68f4d69a613ull,
            0x74f260f0f3b8438dull,
            0xc9da9916b5aa5401ull,
            0x2284189bdaf0c6e3ull,
            0x425934a953eb55efull,
            0xec8b73cd4058012dull,
            0xfc76318e4b28ef3dull,
            0x4aa7a65d2ecb30fcull,
            0xae40c5aa1a33f838ull,
            0xeb240a92f520195eull,
            0x4484fbbfdc81e20eull,
            0x1d0c555a843a6701ull,
            0x4484fbbfdc81e20eull,
            0x1d0c555a843a6701ull,
            0x4484fbbfdc81e20eull,
            0x1d0c555a843a6701ull,
            0x4484fbbfdc81e20eull,
            0x1d0c555a843a6701ull,
            0x4484fbbfdc81e20eull,
            0xf5d6a69a5070204cull,
            0x1d0c555a843a6701ull,
        };
        constexpr std::uint32_t LiveSeed = 0xae67u;
        constexpr int FocusedWindowSamples = 982;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;

        Game game;
        applyCapturedSmartyConfig(game);
        game.random_.seed(LiveSeed);
        game.startAttract();
        if (game.level_ != 10 || game.attractDifficultyIndex_ != 9 ||
            game.enemySlotCount_ != 3 || game.random_.calls != 61 ||
            game.random_.state != 0x915fbdd8u || !game.enemies_.empty() ||
            game.enemySlots_[0].type != 3 || game.enemySlots_[1].type != 0 ||
            game.enemySlots_[2].type != 4) {
            return false;
        }

        Renderer renderer;
        std::vector<std::uint64_t> changedHashes;
        std::uint64_t previousHash = 0;
        bool havePreviousHash = false;
        bool sawCapturedFeedback = false;
        for (int sample = 0; sample < FocusedWindowSamples; ++sample) {
            game.renderPresentation(renderer);
            const std::uint64_t hash = renderedFrameHash(renderer);
            if (!havePreviousHash || hash != previousHash) {
                changedHashes.push_back(hash);
                previousHash = hash;
                havePreviousHash = true;
            }
            if (hash == 0xf5d6a69a5070204cull &&
                game.feedbackKind_ == Game::FeedbackKind::EatenByTroggle &&
                game.feedbackMessage_ == "Aargh" &&
                game.feedbackEnemySlot_ == 2) {
                sawCapturedFeedback = true;
            }
            game.update(SampleSeconds);
        }

        const auto sequenceStart = std::find(
            changedHashes.begin(), changedHashes.end(), Expected.front()
        );
        if (sequenceStart == changedHashes.end() ||
            static_cast<std::size_t>(changedHashes.end() - sequenceStart) !=
                Expected.size() ||
            !std::equal(Expected.begin(), Expected.end(), sequenceStart)) {
            return false;
        }
        return sawCapturedFeedback && game.random_.calls == 84 &&
               game.random_.state == 0x09c161e3u;
    }
    static bool usesOriginalPrng(Game& game) {
        constexpr std::array<std::uint16_t, 8> expected = {
            346, 130, 10982, 1090, 11656, 7117, 17595, 6415
        };
        game.random_.seed(1);
        for (const std::uint16_t value : expected) {
            if (game.random_.next() != value) return false;
        }

        // The original srand argument is 16-bit, and random(min,max) reduces
        // rand() modulo the upper-exclusive width. This inclusive wrapper is
        // called with max-1 throughout the native source.
        game.random_.seed(0x10001u);
        if (game.randomInt(0, 29) != 16) return false;
        game.random_.seed(1);
        if (game.randomInt(15, 44) != 31) return false;

        // Width-one ranges still call the DOS rand() wrapper. With seed 1,
        // the consumed value is 346 and the following raw value must be 130.
        game.random_.seed(1);
        if (game.randomInt(7, 7) != 7 || game.random_.calls != 1) return false;
        return game.random_.next() == 130 && game.random_.calls == 2;
    }

    static void prepareCapturedHall(Game& game) {
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Browse;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Multiples;
        for (auto& list : game.scores_) list.clear();
        game.scores_[0].push_back({235, 1, "sdfg"});
    }

    static void prepareCapturedEmptyFactorsHall(Game& game) {
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Browse;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Factors;
        for (auto& list : game.scores_) list.clear();
    }

    static void prepareCapturedAttractHall(Game& game) {
        prepareCapturedHall(game);
        game.hallContext_ = Game::HallContext::Attract;
        game.attractMode_ = true;
    }

    static void prepareCapturedPostGameHall(Game& game) {
        prepareCapturedHall(game);
        game.hallContext_ = Game::HallContext::PostGame;
    }

    static void prepareCapturedHighlightedHall(Game& game) {
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::PostGame;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Inequality;
        for (auto& list : game.scores_) list.clear();
        constexpr std::string_view CapturedName = "vabcdefghijklmnopqrstuvwx";
        game.scores_[static_cast<std::size_t>(Game::Mode::Inequality)].push_back(
            {85, 1, std::string(CapturedName)});
        game.hallHighlightName_ = std::string(CapturedName);
        game.hallHighlightScore_ = 85;
    }

    static void prepareCapturedReplayQuestion(Game& game) {
        game.page_ = Game::Page::ReplayQuestion;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Primes;
        game.menuSelection_ = 0;
    }

    static void prepareCapturedFullHall(Game& game) {
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Browse;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Inequality;
        for (auto& list : game.scores_) list.clear();
        auto& list = game.scores_[static_cast<std::size_t>(Game::Mode::Inequality)];
        list = {{500, 1, "Alpha"}, {450, 1, "Beta"}, {400, 1, "Gamma"},
                {350, 1, "Delta"}, {300, 1, "Epsilon"}, {250, 1, "Zeta"},
                {200, 1, "Eta"}, {150, 1, "Theta"}, {100, 1, "Iota"},
                {50, 1, "Kappa"}};
    }

    static void prepareEraseEntries(Game& game) {
        game.scorePersistenceEnabled_ = false;
        for (auto& list : game.scores_) list.clear();
        game.scores_[0].push_back({235, 1, "sdfg"});
        game.page_ = Game::Page::OptionsEraseHall;
        game.menuSelection_ = 0;
        game.keyDown(VK_RETURN);
    }

    static void prepareEraseAllConfirm(Game& game) {
        game.scorePersistenceEnabled_ = false;
        for (auto& list : game.scores_) list.clear();
        game.scores_[0].push_back({235, 1, "sdfg"});
        game.page_ = Game::Page::OptionsEraseHall;
        game.menuSelection_ = 6;
        game.keyDown(VK_RETURN);
    }
    static bool exercisesEraseEditing(Game& game) {
        game.scorePersistenceEnabled_ = false;
        for (auto& list : game.scores_) list.clear();
        game.scores_[0].push_back({235, 1, "first"});
        game.scores_[0].push_back({100, 1, "second"});
        game.scores_[1].push_back({50, 1, "factor"});
        game.page_ = Game::Page::OptionsEraseHall;
        game.menuSelection_ = 0;

        game.keyDown(VK_RETURN);
        game.keyDown(VK_SPACE);
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::OptionsEraseHall || game.scores_[0].size() != 2) return false;

        game.keyDown(VK_RETURN);
        game.keyDown(VK_SPACE);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsEraseHall || game.scores_[0].size() != 1 ||
            game.scores_[0][0].name != "second") return false;

        game.menuSelection_ = 6;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN); // Default No.
        if (game.page_ != Game::Page::OptionsEraseHall || game.scores_[0].empty() ||
            game.scores_[1].empty()) return false;

        game.keyDown(VK_RETURN);
        game.keyDown(VK_LEFT);
        game.keyDown(VK_LEFT);
        if (game.page_ != Game::Page::OptionsEraseAllConfirm || game.menuSelection_ != 0) {
            return false;
        }
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_RIGHT);
        if (game.page_ != Game::Page::OptionsEraseAllConfirm || game.menuSelection_ != 1) {
            return false;
        }
        game.keyDown('Y');
        if (game.page_ != Game::Page::OptionsEraseAllConfirm || game.menuSelection_ != 0) {
            return false;
        }
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::OptionsEraseAllConfirm || game.menuSelection_ != 0) {
            return false;
        }
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::OptionsEraseHall &&
               std::all_of(game.scores_.begin(), game.scores_.end(),
                           [](const auto& list) { return list.empty(); });
    }

    static bool usesRecoveredInitiallyEmptyEraseRoute(Game& game,
                                                       std::uint64_t& frameHash) {
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        for (auto& list : game.scores_) list.clear();
        game.eraseMode_ = static_cast<int>(Game::Mode::Primes);
        game.eraseDraft_.clear();
        game.eraseSelection_ = 0;
        game.page_ = Game::Page::OptionsEraseEntries;
        frameHash = fullFrameHash(game);

        // The initial-empty path calls the exact DS:0864 wrapper. Unsupported
        // key pairs and right release remain on the message.
        game.keyDown('4');
        if (game.page_ != Game::Page::OptionsEraseEntries) return false;
        game.character(L'4');
        game.keyDown(VK_RIGHT);
        game.pointerButton(0, 0, true);
        if (game.page_ != Game::Page::OptionsEraseEntries) return false;

        game.pointerButton(0, 0, false);
        return game.page_ == Game::Page::OptionsEraseHall &&
               game.menuSelection_ == static_cast<int>(Game::Mode::Primes);
    }

    static void prepareWrongMultiple(Game& game) {
        game.page_ = Game::Page::Playing;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 16;
        game.lives_ = 4;
        game.playerRow_ = 0;
        game.playerColumn_ = 0;
        game.enemies_.clear();
        game.munching_ = false;
        game.feedbackKind_ = Game::FeedbackKind::None;
        for (Game::Cell& cell : game.cells_) cell = {};
        game.cells_[0].label = "417";
        game.cells_[0].correct = false;
    }

    static bool usesRecoveredFeedbackQuitRoute() {
        const auto enterWrongFeedback = [](Game& game, const int lives, const int score) {
            game.settingsPersistenceEnabled_ = false;
            game.scorePersistenceEnabled_ = false;
            prepareWrongMultiple(game);
            game.attractMode_ = false;
            game.mode_ = Game::Mode::Multiples;
            game.lives_ = lives;
            game.score_ = score;
            for (auto& list : game.scores_) list.clear();
            game.keyDown(VK_SPACE);
            game.resolveMunch();
            return game.page_ == Game::Page::Feedback &&
                   game.feedbackKind_ == Game::FeedbackKind::WrongAnswer;
        };

        // The shared waiter accepts keyboard Enter as well as Space, maps a
        // left release (event type 2) to Space, and leaves right-release code
        // 0xFD unmatched.
        Game enterResume;
        if (!enterWrongFeedback(enterResume, 4, 0)) return false;
        enterResume.keyDown(VK_RETURN);
        if (enterResume.page_ != Game::Page::Playing) return false;

        Game pointerResume;
        if (!enterWrongFeedback(pointerResume, 4, 0)) return false;
        if (pointerResume.pointerPress(true)) return false;
        pointerResume.pointerButton(0, 0, true);
        if (pointerResume.page_ != Game::Page::Feedback) return false;
        if (pointerResume.pointerPress(false)) return false;
        pointerResume.pointerButton(0, 0, false);
        if (pointerResume.page_ != Game::Page::Playing) return false;

        Game cancel;
        if (!enterWrongFeedback(cancel, 4, 0)) return false;
        const std::size_t quitStopsBefore =
            cancel.attractOplPlayer_.allChannelStopCountForTest();
        cancel.keyDown(VK_ESCAPE);
        (void)cancel.attractOplPlayer_.renderTestSamples(1);
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 1 ||
            !cancel.quitFromFeedback_ || cancel.attractOplPlayer_.effectPlaying() ||
            cancel.attractOplPlayer_.allChannelStopCountForTest() !=
                quitStopsBefore + 1) return false;
        cancel.keyDown(VK_LEFT);
        cancel.keyDown(VK_LEFT);
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown(VK_RIGHT);
        cancel.keyDown(VK_RIGHT);
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 1) return false;
        cancel.keyDown('Y');
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown(VK_SPACE);
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown('N');
        cancel.keyDown(VK_UP);
        cancel.keyDown(VK_DOWN);
        if (cancel.page_ != Game::Page::QuitConfirm || cancel.menuSelection_ != 1) return false;
        cancel.keyDown(VK_ESCAPE);
        if (cancel.page_ != Game::Page::Playing || cancel.quitFromFeedback_ ||
            cancel.feedbackKind_ != Game::FeedbackKind::None ||
            !cancel.feedbackMessage_.empty() || cancel.lives_ != 3) return false;

        // Confirming Yes from a feedback waiter calls the scored terminal,
        // so an otherwise qualifying voluntary exit still asks for a name.
        Game confirm;
        if (!enterWrongFeedback(confirm, 4, 55)) return false;
        confirm.keyDown(VK_ESCAPE);
        confirm.keyDown('Y');
        if (confirm.page_ != Game::Page::QuitConfirm || confirm.menuSelection_ != 0) return false;
        confirm.keyDown(VK_RETURN);
        if (confirm.page_ != Game::Page::NameEntry || confirm.quitFromFeedback_ ||
            confirm.score_ != 55 || confirm.lives_ != 3) return false;

        // A default-No return from the final-life feedback still continues
        // through the caller's ordinary no-reserves terminal decision.
        Game finalLoss;
        if (!enterWrongFeedback(finalLoss, 1, 0)) return false;
        finalLoss.keyDown(VK_ESCAPE);
        finalLoss.keyDown(VK_RETURN);
        return finalLoss.page_ == Game::Page::Hall &&
               finalLoss.hallContext_ == Game::HallContext::PostGame &&
               !finalLoss.quitFromFeedback_;
    }

    static void prepareCorrectMultiple(Game& game) {
        game.page_ = Game::Page::Playing;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 20;
        game.level_ = 1;
        game.score_ = 0;
        game.lives_ = 4;
        game.correctRemaining_ = 2;
        game.playerRow_ = 0;
        game.playerColumn_ = 0;
        game.enemies_.clear();
        game.munching_ = false;
        game.feedbackKind_ = Game::FeedbackKind::None;
        for (Game::Cell& cell : game.cells_) cell = {};
        game.cells_[0].label = "40";
        game.cells_[0].correct = true;
    }

    static void placePlayer(Game& game, int row, int column) {
        game.playerRow_ = row;
        game.playerColumn_ = column;
    }
    static void makePlayerCellMunchable(Game& game) {
        Game::Cell& cell = game.cell(game.playerRow_, game.playerColumn_);
        cell.label = "40";
        cell.correct = true;
        cell.eaten = false;
        game.correctRemaining_ = std::max(2, game.correctRemaining_);
    }

    static void prepareMove(Game& game) {
        game.page_ = Game::Page::Playing;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.enemies_.clear();
        game.moving_ = false;
        game.munching_ = false;
        for (Game::Cell& cell : game.cells_) cell = {};
    }

    static void prepareCapturedCgaFactorsBoard(Game& game) {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {{
            "53", "46", "32", "58", "42", "58",
            "58", "58", "",   "36", "39", "52",
            "2",  "48", "26", "20", "1",  "29",
            "29", "53", "6",  "29", "29", "34",
            "47", "47", "29", "35", "2",  "2",
        }};
        game.page_ = Game::Page::Playing;
        game.attractMode_ = false;
        game.mode_ = Game::Mode::Factors;
        game.activeBoardMode_ = Game::Mode::Factors;
        game.target_ = 58;
        game.level_ = 1;
        game.score_ = 0;
        game.lives_ = 4;
        game.playerRow_ = 1;
        game.playerColumn_ = 2;
        game.playerTerminalFrame_ = -1;
        game.moving_ = false;
        game.munching_ = false;
        game.enemyWarning_ = false;
        game.enemies_.clear();
        game.enemySlotCount_ = 0;
        for (auto& slot : game.enemySlots_) slot = {};
        for (auto& job : game.safeZoneJobs_) job = {};
        game.correctRemaining_ = 0;
        for (std::size_t index = 0; index < Labels.size(); ++index) {
            Game::Cell& cell = game.cells_[index];
            cell = {};
            cell.label = std::string(Labels[index]);
            cell.eaten = cell.label.empty();
            if (!cell.eaten) {
                const int value = std::stoi(cell.label);
                cell.correct = value > 0 && 58 % value == 0;
                if (cell.correct) ++game.correctRemaining_;
            }
        }
        game.cells_[3 * Game::BoardColumns + 4].safe = true;
    }

    static bool usesRecoveredLetterMovementAliases(Game&) {
        const auto routes = [](const wchar_t key, const int row, const int column,
                               const int direction) {
            Game game;
            game.settingsPersistenceEnabled_ = false;
            prepareMove(game);
            game.character(key);
            return game.moving_ && !game.munching_ && game.moveFromRow_ == 2 &&
                   game.moveFromColumn_ == 2 && game.moveToRow_ == row &&
                   game.moveToColumn_ == column && game.moveDirection_ == direction;
        };

        for (const wchar_t key : {L'8', L'a', L'A', L'i', L'I'}) {
            if (!routes(key, 1, 2, 0)) return false;
        }
        for (const wchar_t key : {L'4', L'j', L'J'}) {
            if (!routes(key, 2, 1, 3)) return false;
        }
        for (const wchar_t key : {L'6', L'k', L'K'}) {
            if (!routes(key, 2, 3, 1)) return false;
        }
        for (const wchar_t key : {L'2', L'm', L'M', L'z', L'Z'}) {
            if (!routes(key, 3, 2, 2)) return false;
        }

        Game munch;
        munch.settingsPersistenceEnabled_ = false;
        prepareCorrectMultiple(munch);
        munch.keyDown(VK_SPACE);
        return munch.munching_ && !munch.moving_;
    }

    static bool usesRecoveredCartoonInputGate(Game& game) {
        const auto prepare = [](Game& candidate) {
            candidate.settingsPersistenceEnabled_ = false;
            candidate.attractMode_ = false;
            candidate.soundOn_ = false;
            candidate.musicOn_ = false;
            candidate.page_ = Game::Page::Playing;
            candidate.level_ = 3;
            candidate.cartoonOrder_ = {0, 1, 2, 3, 4};
            candidate.cartoonOrderIndex_ = 1;
            candidate.completeLevel();
            candidate.update(1.0);
            return candidate.page_ == Game::Page::LevelComplete &&
                   candidate.levelCompleteScene_.valid() &&
                   candidate.attractInterstitialTransition_ ==
                       Game::AttractInterstitialTransition::None;
        };

        if (!prepare(game)) return false;
        game.keyDown(VK_ESCAPE);
        game.keyDown(VK_SHIFT);
        if (game.page_ != Game::Page::LevelComplete || game.level_ != 3 ||
            game.cartoonOrderIndex_ != 1) return false;
        game.keyDown(VK_LEFT);
        if (game.page_ != Game::Page::Playing || game.level_ != 4 ||
            game.cartoonOrderIndex_ != 2) return false;

        Game printable;
        if (!prepare(printable)) return false;
        printable.keyDown('Q');
        if (printable.page_ != Game::Page::LevelComplete) return false;
        printable.character(L'q');
        if (printable.page_ != Game::Page::Playing || printable.level_ != 4 ||
            printable.moving_ || printable.munching_) return false;

        Game repeatControl;
        if (!prepare(repeatControl)) return false;
        repeatControl.joystickOn_ = true;
        repeatControl.joystickRepeatTicks_ = 4;
        repeatControl.keyDown(VK_OEM_PLUS);
        if (repeatControl.page_ != Game::Page::LevelComplete ||
            repeatControl.joystickRepeatTicks_ != 4) return false;
        repeatControl.character(L'+');
        if (repeatControl.page_ != Game::Page::Playing || repeatControl.level_ != 4 ||
            repeatControl.joystickRepeatTicks_ != 3 || repeatControl.moving_ ||
            repeatControl.munching_ || repeatControl.lastJoystickRepeatFeedbackHz_ != 850 ||
            !repeatControl.effectPlayer_.playing() ||
            repeatControl.effectPlayer_.bufferSize() != 4454) return false;

        // Alt+S returns directly before selector state 6; Alt+M/P rejoin it
        // and therefore forward the same event to cartoon teardown.
        Game soundReturn;
        if (!prepare(soundReturn)) return false;
        soundReturn.soundOn_ = true;
        soundReturn.toggleSound();
        if (soundReturn.soundOn_ || soundReturn.page_ != Game::Page::LevelComplete ||
            soundReturn.level_ != 3) return false;

        Game musicFallthrough;
        if (!prepare(musicFallthrough)) return false;
        musicFallthrough.musicOn_ = true;
        musicFallthrough.toggleMusic();
        musicFallthrough.keyDown(VK_PROCESSKEY);
        if (musicFallthrough.musicOn_ || musicFallthrough.page_ != Game::Page::Playing ||
            musicFallthrough.level_ != 4) return false;

        Game speakerFallthrough;
        if (!prepare(speakerFallthrough)) return false;
        speakerFallthrough.speakerEffects_ = false;
        speakerFallthrough.toggleSpeaker();
        speakerFallthrough.keyDown(VK_PROCESSKEY);
        if (!speakerFallthrough.speakerEffects_ ||
            speakerFallthrough.page_ != Game::Page::Playing ||
            speakerFallthrough.level_ != 4) return false;

        Game joystickButton;
        if (!prepare(joystickButton)) return false;
        joystickButton.keyDown(VK_SPACE);
        if (joystickButton.page_ != Game::Page::Playing ||
            joystickButton.level_ != 4 || joystickButton.munching_) return false;

        Game pointer;
        if (!prepare(pointer) || !pointer.pointerPress(true)) return false;
        return pointer.page_ == Game::Page::Playing && pointer.level_ == 4;
    }

    static bool usesRecoveredBusyPauseGate(Game& game) {
        prepareMove(game);
        game.beginMove(2, 3, 1);
        const double moveTimer = game.moveTimer_;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Paused || !game.moving_) return false;
        game.update(1.0);
        if (game.moveTimer_ != moveTimer || !game.moving_) return false;
        game.keyDown(VK_RETURN);
        game.update(1.0 / OriginalSchedulerTicksPerSecond + 1e-12);
        if (game.page_ != Game::Page::Playing || !game.moving_ ||
            !(game.moveTimer_ < moveTimer)) return false;

        Game chewing;
        chewing.settingsPersistenceEnabled_ = false;
        prepareCorrectMultiple(chewing);
        chewing.keyDown(VK_SPACE);
        const double munchTimer = chewing.munchTimer_;
        chewing.keyDown(VK_RETURN);
        chewing.update(1.0);
        if (chewing.page_ != Game::Page::Paused || !chewing.munching_ ||
            chewing.munchTimer_ != munchTimer) return false;

        Game recovering;
        recovering.settingsPersistenceEnabled_ = false;
        prepareMove(recovering);
        recovering.playerRecovering_ = true;
        recovering.keyDown(VK_RETURN);
        if (recovering.page_ != Game::Page::Paused || !recovering.playerRecovering_) {
            return false;
        }

        Game pointer;
        prepareMove(pointer);
        if (!pointer.pointerPress(true) || pointer.page_ != Game::Page::Paused ||
            !pointer.pointerPress(true) || pointer.page_ != Game::Page::Playing) return false;
        pointer.page_ = Game::Page::Title;
        return !pointer.pointerPress(true);
    }

    static bool usesRecoveredOrdinaryTroggleFrames(Game& game) {
        Game::Enemy enemy;
        for (int direction = 0; direction < 4; ++direction) {
            enemy.direction = direction;
            enemy.entering = true;
            enemy.moving = false;
            if (Game::ordinaryEnemyFrame(enemy) != direction * 3) return false;
            enemy.moving = true;
            if (Game::ordinaryEnemyFrame(enemy) != direction * 3) return false;
            enemy.entering = false;
            if (Game::ordinaryEnemyFrame(enemy) != direction * 3 + 2) return false;
            enemy.moving = false;
            const int expectedDwell = direction == 2 ? 15 : direction * 3 + 1;
            if (Game::ordinaryEnemyFrame(enemy) != expectedDwell) return false;
        }
        constexpr std::array<int, 21> BiteFrames = {
            12, 13, 14, 13, 12, 13, 12, 13, 14, 13, 12,
            13, 12, 13, 14, 13, 12, 13, 12, 13, 14
        };
        for (int tick = 0; tick < static_cast<int>(BiteFrames.size()); ++tick) {
            if (Game::troggleBiteFrameAtTick(tick) !=
                BiteFrames[static_cast<std::size_t>(tick)]) return false;
        }
        if (Game::troggleBiteFrameAtTick(-1) != 12 ||
            Game::troggleBiteFrameAtTick(21) != 13) return false;

        game.level_ = 1;
        const double interval = 3.0 / OriginalSchedulerTicksPerSecond;
        Game::Enemy vertical;
        vertical.direction = 0;
        vertical.moving = true;
        const double verticalDuration = game.enemyMoveAnimationDuration(0);
        for (int phase = 1; phase <= 5; ++phase) {
            vertical.animationTimer = verticalDuration - (phase - 1) * interval;
            if (game.enemyMovementPhase(vertical) != phase) return false;
        }
        Game::Enemy horizontal;
        horizontal.direction = 1;
        horizontal.moving = true;
        const double horizontalDuration = game.enemyMoveAnimationDuration(1);
        for (int phase = 1; phase <= 6; ++phase) {
            horizontal.animationTimer = horizontalDuration - (phase - 1) * interval;
            if (game.enemyMovementPhase(horizontal) != phase) return false;
        }
        vertical.entering = true;
        vertical.animationTimer = verticalDuration + interval;
        if (game.enemyMovementPhase(vertical) != 1) return false;
        vertical.animationTimer -= interval;
        if (game.enemyMovementPhase(vertical) != 2) return false;
        vertical.animationTimer = interval;
        if (game.enemyMovementPhase(vertical) != 5) return false;
        vertical.entering = false;
        vertical.exiting = true;
        vertical.animationTimer = interval;
        if (game.enemyMovementPhase(vertical) != 5) return false;

        const auto downMiddle = game.assets_.spriteFrame(1007, 7);
        const auto downDwellAlias = game.assets_.spriteFrame(1007, 15);
        return downMiddle && downDwellAlias &&
               downMiddle->sheetId == downDwellAlias->sheetId &&
               downMiddle->x == downDwellAlias->x && downMiddle->y == downDwellAlias->y &&
               downMiddle->width == downDwellAlias->width &&
               downMiddle->height == downDwellAlias->height;
    }

    static bool usesRecoveredPlayerMovement(Game& game) {
        constexpr std::array<int, 6> FrameOffsets = {2, 2, 1, 0, 1, 2};
        constexpr double TickSeconds = 1.0 / OriginalSchedulerTicksPerSecond;
        for (int direction = 0; direction < 4; ++direction) {
            const bool vertical = direction == 0 || direction == 2;
            const int stepCount = vertical ? 5 : 6;
            const double duration = Game::playerMoveAnimationDuration(direction);
            if (std::abs(duration - stepCount * TickSeconds) > 1e-12) return false;

            game.moving_ = true;
            game.moveDirection_ = direction;
            for (int phase = 0; phase < stepCount; ++phase) {
                game.moveTimer_ = duration - phase * TickSeconds;
                if (game.playerMovementPhase() != phase ||
                    Game::playerMoveFrameAtPhase(direction, phase) !=
                        direction * 3 + FrameOffsets[static_cast<std::size_t>(phase)]) {
                    return false;
                }
            }
        }

        game.page_ = Game::Page::Playing;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.enemies_.clear();
        game.enemySlotCount_ = 0;
        game.safeZoneJobCount_ = 0;
        game.moving_ = false;
        game.munching_ = false;
        game.gameplayTickAccumulator_ = 0.0;
        game.beginMove(2, 3, 1);
        for (int callback = 1; callback < 6; ++callback) {
            game.updatePlayerMovementTick(TickSeconds);
            if (!game.moving_ || game.playerMovementPhase() != callback ||
                game.playerColumn_ != 2) return false;
        }
        game.updatePlayerMovementTick(TickSeconds);
        if (game.moving_ || game.playerColumn_ != 3 || game.moveTimer_ != 0.0 ||
            game.playerTerminalFrame_ != 4) return false;

        game.beginMove(1, 3, 0);
        for (int callback = 1; callback < 5; ++callback) {
            game.updatePlayerMovementTick(TickSeconds);
            if (!game.moving_ || game.playerMovementPhase() != callback ||
                game.playerRow_ != 2) return false;
        }
        game.updatePlayerMovementTick(TickSeconds);
        return !game.moving_ && game.playerRow_ == 1 && game.moveTimer_ == 0.0 &&
               game.playerTerminalFrame_ == 2;
    }

    static bool usesRecoveredMunchDispatcher() {
        constexpr std::array<int, 8> Frames = {12, 13, 14, 13, 12, 13, 14, 13};
        if (std::abs(Game::MunchAnimationDuration -
                     Game::MunchAnimationTicks / OriginalSchedulerTicksPerSecond) > 1e-12) {
            return false;
        }
        for (int tick = 0; tick <= Game::MunchAnimationTicks; ++tick) {
            if (Game::munchFrameAtTick(tick) != Frames[static_cast<std::size_t>(tick)]) {
                return false;
            }
        }
        if (Game::munchFrameAtTick(-1) != 12 ||
            Game::munchFrameAtTick(Game::MunchAnimationTicks + 1) != 13) return false;

        Game staged;
        staged.page_ = Game::Page::Playing;
        staged.activeBoardMode_ = Game::Mode::Multiples;
        staged.target_ = 5;
        staged.level_ = 1;
        staged.score_ = 0;
        staged.lives_ = 4;
        staged.correctRemaining_ = 2;
        staged.playerRow_ = 2;
        staged.playerColumn_ = 2;
        staged.soundOn_ = false;
        staged.musicOn_ = false;
        staged.enemies_.clear();
        staged.enemySlotCount_ = 0;
        staged.safeZoneJobCount_ = 0;
        for (Game::Cell& cell : staged.cells_) cell = {};
        Game::Cell& selected = staged.cell(2, 2);

        // The original tests the live board word before cue/animation setup.
        // A blank Number cell can be reachable with eaten=false after a
        // Troggle restores a previously blank record, so both representations
        // of zero must reject the action.
        staged.munch();
        if (staged.munching_ || staged.munchCellIndex_ != -1 || staged.score_ != 0 ||
            staged.lives_ != 4) return false;

        selected.label = "10";
        selected.correct = true;
        staged.munch();
        if (!staged.munching_ || !selected.eaten || staged.correctRemaining_ != 2 ||
            staged.munchSavedCell_.label != "10" || !staged.munchSavedCell_.correct) {
            return false;
        }

        // The terminal must score the saved record, not a later live-cell
        // mutation, and must not clear that replacement a second time.
        selected.label = "7";
        selected.correct = false;
        selected.eaten = false;
        staged.resolveMunch();
        if (staged.munching_ || staged.score_ != 5 || staged.correctRemaining_ != 1 ||
            selected.eaten || selected.label != "7") {
            return false;
        }

        // The final state-5 callback posts selector-100 action 6. If a host
        // frame spans that callback, only the time after it may reach the
        // synchronous board Wipe. Compare a 2.25-tick update whose callback
        // is one tick away with direct completion plus the 1.25-tick tail.
        const auto prepareFinal = [](Game& game) {
            game.settingsPersistenceEnabled_ = false;
            game.random_.seed(0x4d43u);
            game.startGame(Game::Mode::Multiples, 1);
            game.soundOn_ = false;
            game.musicOn_ = false;
            game.enemies_.clear();
            game.enemySlotCount_ = 0;
            game.safeZoneJobCount_ = 0;
            for (Game::Cell& cell : game.cells_) cell = {};
            constexpr int finalCell = Game::BoardColumns + 1;
            game.cells_[finalCell].label = "5";
            game.cells_[finalCell].correct = true;
            game.cells_[finalCell].eaten = false;
            game.correctRemaining_ = 1;
            game.playerRow_ = finalCell / Game::BoardColumns;
            game.playerColumn_ = finalCell % Game::BoardColumns;
            game.munch();
            game.munchTimer_ = 1.0 / OriginalSchedulerTicksPerSecond;
            game.gameplayTickAccumulator_ = 0.0;
            return game.munching_;
        };
        Game direct;
        Game scheduled;
        if (!prepareFinal(direct) || !prepareFinal(scheduled)) return false;
        constexpr double postTerminalTicks = 1.25;
        direct.resolveMunch();
        direct.update(postTerminalTicks / OriginalSchedulerTicksPerSecond);
        scheduled.update((1.0 + postTerminalTicks) / OriginalSchedulerTicksPerSecond);
        if (direct.page_ != Game::Page::Playing || scheduled.page_ != Game::Page::Playing ||
            direct.level_ != 2 || scheduled.level_ != 2 ||
            direct.boardGenerationSerial_ != scheduled.boardGenerationSerial_ ||
            direct.random_.state != scheduled.random_.state ||
            direct.random_.calls != scheduled.random_.calls ||
            direct.enemySlotCount_ != scheduled.enemySlotCount_ ||
            direct.safeZoneJobCount_ != scheduled.safeZoneJobCount_ ||
            std::abs(direct.gameplayTickAccumulator_) > 1e-12 ||
            std::abs(scheduled.gameplayTickAccumulator_) > 1e-12 ||
            direct.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToBoard ||
            scheduled.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToBoard ||
            direct.attractInterstitialFrame_ != scheduled.attractInterstitialFrame_ ||
            std::abs(direct.transitionTimer_ - scheduled.transitionTimer_) > 1e-12) {
            return false;
        }
        for (int index = 0; index < direct.enemySlotCount_; ++index) {
            const Game::EnemySlot& expected = direct.enemySlots_[static_cast<std::size_t>(index)];
            const Game::EnemySlot& actual = scheduled.enemySlots_[static_cast<std::size_t>(index)];
            if (expected.type != actual.type || expected.phase != actual.phase ||
                expected.timer != actual.timer) return false;
        }
        for (int index = 0; index < direct.safeZoneJobCount_; ++index) {
            const Game::SafeZoneJob& expected =
                direct.safeZoneJobs_[static_cast<std::size_t>(index)];
            const Game::SafeZoneJob& actual =
                scheduled.safeZoneJobs_[static_cast<std::size_t>(index)];
            if (expected.active != actual.active || expected.cellIndex != actual.cellIndex ||
                expected.period != actual.period || expected.timer != actual.timer) return false;
        }

        // A coalesced presentation update must not resolve the player before
        // earlier public ticks. On tick one this Troggle reaches the occupied
        // endpoint while job 4 is still in chew state 5, so the standing-only
        // collision predicate rejects it. Tick two then resolves the saved
        // correct answer. Processing the whole host frame before recurring
        // records would incorrectly start a player bite here.
        Game protectedChew;
        protectedChew.settingsPersistenceEnabled_ = false;
        protectedChew.random_.seed(0x4348u);
        protectedChew.startGame(Game::Mode::Multiples, 1);
        protectedChew.soundOn_ = false;
        protectedChew.musicOn_ = false;
        protectedChew.safeZoneJobCount_ = 0;
        protectedChew.enemies_.clear();
        protectedChew.enemySlotCount_ = 1;
        protectedChew.enemySlots_[0] = {};
        protectedChew.enemySlots_[0].type = 0;
        protectedChew.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        const int protectedCell = protectedChew.playerRow_ * Game::BoardColumns +
                                  protectedChew.playerColumn_;
        protectedChew.cells_[static_cast<std::size_t>(protectedCell)] = {
            "5", true, false, false, 0.0};
        protectedChew.correctRemaining_ = 2;
        protectedChew.munch();
        protectedChew.munchTimer_ = 2.0 / OriginalSchedulerTicksPerSecond;
        Game::Enemy endpoint;
        endpoint.row = protectedChew.playerRow_;
        endpoint.column = protectedChew.playerColumn_;
        endpoint.fromRow = endpoint.row;
        endpoint.fromColumn = std::max(0, endpoint.column - 1);
        endpoint.type = 0;
        endpoint.slot = 0;
        endpoint.moving = true;
        endpoint.animationTimer = 1.0 / OriginalSchedulerTicksPerSecond;
        protectedChew.enemies_.push_back(endpoint);
        protectedChew.gameplayTickAccumulator_ = 0.0;
        protectedChew.update(2.25 / OriginalSchedulerTicksPerSecond);
        if (protectedChew.page_ != Game::Page::Playing || protectedChew.deathAnimating_ ||
            protectedChew.munching_ || protectedChew.score_ != 5 ||
            protectedChew.correctRemaining_ != 1 || protectedChew.enemies_.empty() ||
            protectedChew.enemies_[0].moving ||
            std::abs(protectedChew.gameplayTickAccumulator_ - 0.25) > 1e-12) {
            return false;
        }
        return true;
    }

    static bool usesRecoveredPlayerCollisionStateGate(Game& game) {
        game.page_ = Game::Page::Playing;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.lives_ = 4;
        game.soundOn_ = false;
        game.musicOn_ = false;
        game.playerRecovering_ = false;
        game.deathAnimating_ = false;
        game.enemySlotCount_ = 1;
        game.safeZoneJobCount_ = 0;
        game.enemies_.clear();
        for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        for (Game::Cell& cell : game.cells_) cell = {};

        Game::Enemy endpoint;
        endpoint.row = 2;
        endpoint.column = 2;
        endpoint.fromRow = 2;
        endpoint.fromColumn = 1;
        endpoint.type = 0;
        endpoint.slot = 0;
        endpoint.moving = true;
        endpoint.animationTimer = 0.0;
        game.enemies_.push_back(endpoint);

        game.moving_ = true;
        game.munching_ = false;
        if (game.playerCanBeCaughtAtEnemyEndpoint()) return false;
        game.updateEnemies(0.0);
        if (game.deathAnimating_) return false;

        game.enemies_[0].moving = true;
        game.enemies_[0].animationTimer = 0.0;
        game.moving_ = false;
        game.munching_ = true;
        if (game.playerCanBeCaughtAtEnemyEndpoint()) return false;
        game.updateEnemies(0.0);
        if (game.deathAnimating_) return false;

        game.enemies_[0].moving = true;
        game.enemies_[0].animationTimer = 0.0;
        game.munching_ = false;
        if (!game.playerCanBeCaughtAtEnemyEndpoint()) return false;
        game.updateEnemies(0.0);
        return game.deathAnimating_ && game.feedbackEnemyType_ == 0;
    }

    static bool usesRecoveredGameplayPointerRouting(Game& game) {
        const auto reset = [&game](int row, int column) {
            game.page_ = Game::Page::Playing;
            game.activeBoardMode_ = Game::Mode::Multiples;
            game.target_ = 5;
            game.level_ = 1;
            game.score_ = 0;
            game.lives_ = 4;
            game.correctRemaining_ = 2;
            game.playerRow_ = row;
            game.playerColumn_ = column;
            game.moving_ = false;
            game.moveTimer_ = 0.0;
            game.munching_ = false;
            game.munchTimer_ = 0.0;
            game.munchCellIndex_ = -1;
            game.pointerCellQueue_.clear();
            game.enemies_.clear();
            game.enemySlotCount_ = 0;
            game.safeZoneJobCount_ = 0;
            game.gameplayTickAccumulator_ = 0.0;
            game.feedbackKind_ = Game::FeedbackKind::None;
            game.playerRecovering_ = false;
            game.deathAnimating_ = false;
            for (Game::Cell& current : game.cells_) current = {};
        };
        const auto clickCell = [&game](int row, int column) {
            game.pointerButton(Game::BoardLeft + column * Game::BoardCellWidth + 1,
                               Game::BoardTop + row * Game::BoardCellHeight + 1, false);
        };
        const auto finishStep = [&game]() {
            game.update(Game::MoveAnimationDuration + 0.001);
        };

        // pSeqmouse queues a target; it does not teleport. 0x07fd4 chooses
        // the axis with the larger distance and resolves ties vertically.
        reset(2, 2);
        Game::Cell& distant = game.cell(0, 5);
        distant.label = "25";
        distant.correct = true;
        clickCell(0, 5);
        if (!game.moving_ || game.moveDirection_ != 1 || game.moveToRow_ != 2 ||
            game.moveToColumn_ != 3 || game.playerRow_ != 2 || game.playerColumn_ != 2) {
            return false;
        }
        finishStep();
        if (!game.moving_ || game.moveDirection_ != 0 || game.moveToRow_ != 1 ||
            game.moveToColumn_ != 3) return false; // Equal 2-by-2 delta: vertical.
        finishStep();
        if (!game.moving_ || game.moveDirection_ != 1 || game.moveToRow_ != 1 ||
            game.moveToColumn_ != 4) return false;
        finishStep();
        if (!game.moving_ || game.moveDirection_ != 0 || game.moveToRow_ != 0 ||
            game.moveToColumn_ != 4) return false;
        finishStep();
        if (!game.moving_ || game.moveDirection_ != 1 || game.moveToRow_ != 0 ||
            game.moveToColumn_ != 5) return false;
        finishStep();
        if (game.moving_ || game.munching_ || game.playerRow_ != 0 ||
            game.playerColumn_ != 5 || distant.eaten || game.score_ != 0 ||
            !game.pointerCellQueue_.empty()) return false;

        // Arrival consumes only the movement target. A new/current-cell click
        // enters the ordinary seven-tick chew path and scoring resolution.
        clickCell(0, 5);
        if (!game.munching_ || game.munchCellIndex_ != 5) return false;
        game.update(Game::MunchAnimationDuration + 0.001);
        if (!distant.eaten || game.score_ != 5 || game.munching_) return false;

        // The original nine-slot ring accepts the second click during the
        // walk. It becomes a current-cell eat as soon as arrival completes.
        reset(2, 2);
        Game::Cell& queued = game.cell(2, 4);
        queued.label = "20";
        queued.correct = true;
        clickCell(2, 4);
        clickCell(2, 4);
        if (game.pointerCellQueue_.size() != 2) return false;
        finishStep();
        if (!game.moving_ || game.moveToColumn_ != 4 ||
            game.pointerCellQueue_.size() != 1) return false;
        finishStep();
        if (game.moving_ || !game.munching_ || !game.pointerCellQueue_.empty() ||
            game.playerColumn_ != 4) return false;

        // Cell zero is encoded as byte zero in DOS and is distinguished from
        // an empty ring by the queue indices/count. Keep that edge case live.
        reset(0, 1);
        Game::Cell& zero = game.cell(0, 0);
        zero.label = "10";
        zero.correct = true;
        clickCell(0, 0);
        if (!game.moving_ || game.moveDirection_ != 3) return false;
        finishStep();
        if (game.munching_ || game.playerColumn_ != 0) return false;
        clickCell(0, 0);
        return game.munching_ && game.munchCellIndex_ == 0;
    }

    static bool usesRecoveredMixedGameplayInputRing(Game& game) {
        constexpr double TickSeconds = 1.0 / OriginalSchedulerTicksPerSecond;
        const auto reset = [&game]() {
            game.page_ = Game::Page::Playing;
            game.activeBoardMode_ = Game::Mode::Multiples;
            game.target_ = 5;
            game.level_ = 1;
            game.score_ = 0;
            game.lives_ = 4;
            game.correctRemaining_ = 2;
            game.playerRow_ = 2;
            game.playerColumn_ = 2;
            game.moving_ = false;
            game.moveTimer_ = 0.0;
            game.munching_ = false;
            game.munchTimer_ = 0.0;
            game.munchCellIndex_ = -1;
            game.pointerCellQueue_.clear();
            game.enemies_.clear();
            game.enemySlotCount_ = 0;
            game.safeZoneJobCount_ = 0;
            game.gameplayTickAccumulator_ = 0.0;
            game.feedbackKind_ = Game::FeedbackKind::None;
            game.playerRecovering_ = false;
            game.deathAnimating_ = false;
            for (Game::Cell& current : game.cells_) current = {};
        };

        // Live DOS `JJ` starts the second left step from the first movement
        // terminal. Physical arrows and printable aliases occupy one FIFO.
        reset();
        game.keyDown(VK_RIGHT);
        game.character(L'k');
        game.keyDown(VK_DOWN);
        if (!game.moving_ || game.moveToColumn_ != 3 ||
            game.pointerCellQueue_ != std::deque<int>{'K', 'M'}) return false;
        game.update(Game::playerMoveAnimationDuration(1) + 0.001);
        if (!game.moving_ || game.playerRow_ != 2 || game.playerColumn_ != 3 ||
            game.moveToRow_ != 2 || game.moveToColumn_ != 4 ||
            game.pointerCellQueue_ != std::deque<int>{'M'}) return false;
        game.update(Game::playerMoveAnimationDuration(1) + 0.001);
        if (!game.moving_ || game.playerColumn_ != 4 || game.moveToRow_ != 3 ||
            !game.pointerCellQueue_.empty()) return false;

        // Live DOS `Space+J` retains J behind the seven-tick correct chew.
        // The chew terminal restores actor state 4; the next public player
        // callback, rather than the terminal callback itself, consumes J.
        reset();
        Game::Cell& current = game.cell(2, 2);
        current.label = "5";
        current.correct = true;
        game.keyDown(VK_SPACE);
        game.character(L'j');
        if (!game.munching_ || game.pointerCellQueue_ != std::deque<int>{'J'}) return false;
        game.update(Game::MunchAnimationDuration + 0.001);
        if (game.munching_ || game.moving_ || game.score_ != 5 ||
            game.pointerCellQueue_ != std::deque<int>{'J'}) return false;
        game.update(TickSeconds + 1e-12);
        if (!game.moving_ || game.moveDirection_ != 3 || game.moveToColumn_ != 1 ||
            !game.pointerCellQueue_.empty()) return false;

        // Collision setup clears old input, but selector-state-4 input can
        // fill the ring while player actor state 6 waits for the survivor.
        reset();
        game.playerRecovering_ = true;
        for (int index = 0; index < 10; ++index) game.character(L'k');
        if (game.moving_ || game.pointerCellQueue_.size() != 9) return false;
        game.playerRecovering_ = false;
        game.update(TickSeconds + 1e-12);
        if (!game.moving_ || game.moveDirection_ != 1 || game.moveToColumn_ != 3 ||
            game.pointerCellQueue_.size() != 8) return false;

        // Enter's common-dispatcher branch clears both key and pointer bytes
        // before entering Time out; resume clears the ring again.
        reset();
        game.keyDown(VK_RIGHT);
        game.character(L'k');
        game.pointerButton(Game::BoardLeft + 5 * Game::BoardCellWidth + 1,
                           Game::BoardTop + 2 * Game::BoardCellHeight + 1, false);
        if (game.pointerCellQueue_.size() != 2) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Paused || !game.moving_ ||
            !game.pointerCellQueue_.empty()) return false;
        game.pointerCellQueue_.push_back('J');
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::Playing && game.moving_ &&
               game.pointerCellQueue_.empty();
    }

    static void placeSafeCell(Game& game, int row, int column) {
        game.cell(row, column).safe = true;
    }

    static void prepareTroggleDeath(Game& game, int lives = 4, int score = 0) {
        game.page_ = Game::Page::Playing;
        game.playerRow_ = 1;
        game.playerColumn_ = 2;
        game.lives_ = lives;
        game.score_ = score;
        game.mode_ = Game::Mode::Multiples;
        for (auto& list : game.scores_) list.clear();
        game.moving_ = false;
        game.munching_ = false;
        game.feedbackKind_ = Game::FeedbackKind::None;
        game.enemies_.clear();
        Game::Enemy enemy{1, 2, 0, 1.0};
        enemy.direction = 1;
        enemy.slot = 0;
        game.enemies_.push_back(enemy);
        game.enemySlotCount_ = 1;
        for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.safeZoneJobCount_ = 0;
        game.enemyWarning_ = false;
        game.correctRemaining_ = 10;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 5;
        for (Game::Cell& cell : game.cells_) cell = {};
        game.cell(0, 0).label = "5";
        game.cell(0, 0).correct = true;
        game.cell(0, 1).label = "10";
        game.cell(0, 1).correct = true;
        game.loseLife(0, 0);
    }

    static bool liveCollisionBiteCompositeMatches(Game& game) {
        prepareTroggleDeath(game);
        game.playerRow_ = 2;
        game.playerColumn_ = 3;
        game.enemies_[0].row = 2;
        game.enemies_[0].column = 3;
        game.enemies_[0].fromRow = 2;
        game.enemies_[0].fromColumn = 4;
        for (Game::Cell& cell : game.cells_) cell = {};

        constexpr std::uint64_t LiveOpenHash = 0x2cc4ae9134dff1eeull;
        constexpr std::uint64_t LiveClosedHash = 0xe98dda1e9e05c039ull;
        const auto collisionCellHash = [](const Renderer& renderer) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 86; y < 117; ++y) {
                for (int x = 164; x < 212; ++x) {
                    value ^= renderer.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };

        Renderer renderer;
        for (int tick = 0; tick < Game::TroggleEatAnimationTicks; ++tick) {
            game.deathSequenceTicks_ = tick;
            game.render(renderer);
            const std::uint64_t expected = (tick & 1) == 0
                ? LiveOpenHash : LiveClosedHash;
            if (collisionCellHash(renderer) != expected) return false;
        }

        // The complete fourth-Demo collision at global frames 15938-15989
        // supplies a second live species oracle. Bashful enters downward on
        // the Muncher's row-0/column-1 cell, paints stationary record 15, and
        // then alternates the same state-5 record numbers as Reggie. Gate the
        // exact occupied cell so the generic bite scheduler cannot conceal a
        // type-specific BTMP/palette or placement regression.
        Game bashful;
        prepareTroggleDeath(bashful);
        bashful.attractMode_ = true;
        bashful.page_ = Game::Page::Feedback;
        bashful.mode_ = Game::Mode::Multiples;
        bashful.activeBoardMode_ = Game::Mode::Multiples;
        bashful.target_ = 15;
        bashful.playerRow_ = 0;
        bashful.playerColumn_ = 1;
        bashful.feedbackKind_ = Game::FeedbackKind::EatenByTroggle;
        bashful.feedbackEnemyType_ = 2;
        bashful.feedbackEnemySlot_ = 0;
        bashful.feedbackMessage_.clear();
        bashful.deathAnimating_ = true;
        bashful.deathSequenceTicks_ = 0;
        bashful.attractPostFeedbackBoard_ = false;
        bashful.hallPaletteActive_ = false;
        static constexpr std::array<std::string_view, Game::BoardCellCount>
            CapturedFourthLabels = {
                "45",  "690", "165", "92",  "294", "516",
                "675", "180", "564", "490", "690", "600",
                "420", "",    "504", "90",  "555", "195",
                "180", "744", "726", "480", "43",  "405",
                "49",  "90",  "720", "165", "705", "390"};
        for (int index = 0; index < Game::BoardCellCount; ++index) {
            Game::Cell& current = bashful.cells_[static_cast<std::size_t>(index)];
            current = {};
            current.label = std::string(
                CapturedFourthLabels[static_cast<std::size_t>(index)]);
            current.eaten = index == 6 || index == 7 || index == 12 || index == 13;
        }
        bashful.enemies_.clear();

        // The fourth Demo board begins this capture window with a Worker
        // entering from the left while the Muncher stands in row 0. Runs 0,
        // 2, and 4 are complete framebuffers; run 1 is a horizontal scanout
        // splice across the 690 label between the complete phase-5 surface
        // and the following eaten-cell surface, so native must prove its two
        // source pages without presenting that partial refresh as gameplay.
        bashful.page_ = Game::Page::Attract;
        bashful.feedbackKind_ = Game::FeedbackKind::None;
        bashful.deathAnimating_ = false;
        bashful.enemyWarning_ = true;
        bashful.playerTerminalFrame_ = -1;
        Game::Enemy workerProbe;
        workerProbe.row = 4;
        workerProbe.column = 0;
        workerProbe.fromRow = 4;
        workerProbe.fromColumn = -1;
        workerProbe.direction = 1;
        workerProbe.type = 1;
        workerProbe.slot = 0;
        workerProbe.entering = true;
        workerProbe.moving = true;
        const double workerEntryDuration =
            bashful.enemyMoveAnimationDuration(workerProbe.direction);
        static constexpr std::array<std::uint64_t, 3>
            CapturedWorkerApproachHashes = {
                0x0c7d59142f70ee0dull, 0xf9017fef1b1103eeull,
                0xa0130023af56d5dfull,
            };
        std::vector<std::uint32_t> workerPhase5Surface;
        for (int phase = 4; phase <= 6; ++phase) {
            workerProbe.animationTimer =
                (8 - phase) * workerEntryDuration / 6.0;
            bashful.enemies_ = {workerProbe};
            const std::uint64_t actual = fullFrameHash(bashful);
            const std::uint64_t expected =
                CapturedWorkerApproachHashes[static_cast<std::size_t>(phase - 4)];
            if (actual != expected) {
                std::cerr << "captured fourth-board Worker approach phase " << phase
                          << " hash=0x" << std::hex << actual << " expected=0x"
                          << expected << std::dec << '\n';
                return false;
            }
            if (phase == 5) workerPhase5Surface = bashful.capturePresentationFrame();
        }

        workerProbe.animationTimer = workerEntryDuration / 2.0;
        bashful.enemies_ = {workerProbe};
        bashful.cell(0, 1).eaten = true;
        const std::vector<std::uint32_t> eatenStandingSurface =
            bashful.capturePresentationFrame();
        Renderer eatenStandingRenderer;
        eatenStandingRenderer.replacePixels(eatenStandingSurface);
        constexpr std::uint64_t CapturedEatenStandingSurfaceHash =
            0x570067be4ee60e89ull;
        if (renderedFrameHash(eatenStandingRenderer) !=
            CapturedEatenStandingSurfaceHash) {
            std::cerr << "captured fourth-board eaten standing surface changed\n";
            return false;
        }
        bashful.cell(0, 1).eaten = false;

        std::vector<std::uint32_t> scanoutSplice = workerPhase5Surface;
        for (int y = 43; y <= 45; ++y) {
            const std::size_t first = static_cast<std::size_t>(y * Renderer::Width);
            std::copy_n(eatenStandingSurface.begin() +
                            static_cast<std::ptrdiff_t>(first),
                        Renderer::Width,
                        scanoutSplice.begin() + static_cast<std::ptrdiff_t>(first));
        }
        Renderer scanoutSpliceRenderer;
        scanoutSpliceRenderer.replacePixels(std::move(scanoutSplice));
        constexpr std::uint64_t CapturedLabelScanoutSpliceHash =
            0x207c5242fcfaa0b9ull;
        if (renderedFrameHash(scanoutSpliceRenderer) !=
            CapturedLabelScanoutSpliceHash) {
            std::cerr << "captured fourth-board label scanout splice proof changed\n";
            return false;
        }

        workerProbe.entering = false;
        workerProbe.moving = false;
        workerProbe.animationTimer = 0.0;
        workerProbe.dwellFrame = 4;
        bashful.enemies_ = {workerProbe};
        if (fullFrameHash(bashful) != CapturedWorkerApproachHashes[2]) {
            std::cerr << "captured fourth-board Worker dwell hash changed\n";
            return false;
        }

        bashful.enemies_.clear();
        bashful.playerTerminalFrame_ = -1;
        bashful.enemyWarning_ = false;
        Game::Enemy bashfulBiter;
        bashfulBiter.row = 0;
        bashfulBiter.column = 1;
        bashfulBiter.fromRow = -1;
        bashfulBiter.fromColumn = 1;
        bashfulBiter.direction = 2;
        bashfulBiter.dwellFrame = 15;
        bashfulBiter.type = 2;
        bashfulBiter.slot = 1;
        bashfulBiter.savedCell = bashful.cell(0, 1);
        bashfulBiter.savedCellValid = true;
        bashful.enemies_ = {workerProbe, bashfulBiter};
        bashful.enemySlotCount_ = 2;
        bashful.enemySlots_[0].type = 1;
        bashful.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        bashful.enemySlots_[1].type = 2;
        bashful.enemySlots_[1].phase = Game::EnemySlotPhase::Active;
        bashful.safeZoneJobCount_ = 0;

        const auto bashfulCellHash = [](const Renderer& frame) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 26; y < 57; ++y) {
                for (int x = 68; x < 116; ++x) {
                    value ^= frame.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };
        static constexpr std::array<std::uint64_t, 4>
            CapturedBashfulTopEntryCellHashes = {
                0x91ef59c32990a69dull, 0x9f86dff368b34adcull,
                0x97504fcbb0cc916cull, 0x025ccb235e45f928ull,
            };
        static constexpr std::array<std::uint64_t, 4>
            CapturedBashfulApproachFullHashes = {
                0xce9382c8fa615ba4ull, 0x0674ece67f2455d9ull,
                0xb3af352e246803cbull, 0xe1601a53581b8dd7ull,
            };
        bashful.page_ = Game::Page::Attract;
        bashful.feedbackKind_ = Game::FeedbackKind::None;
        bashful.deathAnimating_ = false;
        bashful.enemies_[1].entering = true;
        bashful.enemies_[1].moving = true;
        const double bashfulEntryDuration =
            bashful.enemyMoveAnimationDuration(2);
        for (int phase = 2; phase <= 5; ++phase) {
            bashful.enemies_[1].animationTimer =
                (7 - phase) * bashfulEntryDuration / 5.0;
            // The last two captured entry callbacks retain Muncher record 12
            // beneath the clipped Bashful actor.
            bashful.playerTerminalFrame_ = phase >= 4 ? 12 : -1;
            bashful.enemyWarning_ = phase <= 3;
            bashful.render(renderer);
            const std::uint64_t actual = bashfulCellHash(renderer);
            const std::uint64_t expected =
                CapturedBashfulTopEntryCellHashes[
                    static_cast<std::size_t>(phase - 2)];
            if (actual != expected) {
                std::cerr << "captured Bashful top-entry phase " << phase
                          << " cell hash=0x" << std::hex << actual
                          << " expected=0x" << expected << std::dec << '\n';
                return false;
            }
            const std::uint64_t fullActual = renderedFrameHash(renderer);
            const std::uint64_t fullExpected =
                CapturedBashfulApproachFullHashes[
                    static_cast<std::size_t>(phase - 2)];
            if (fullActual != fullExpected) {
                std::cerr << "captured Bashful top-entry phase " << phase
                          << " full-frame hash=0x" << std::hex << fullActual
                          << " expected=0x" << fullExpected << std::dec << '\n';
                return false;
            }
        }
        bashful.playerTerminalFrame_ = -1;
        bashful.enemyWarning_ = false;
        bashful.page_ = Game::Page::Feedback;
        bashful.feedbackKind_ = Game::FeedbackKind::EatenByTroggle;
        bashful.deathAnimating_ = true;
        bashful.enemies_[1].entering = false;
        bashful.enemies_[1].moving = false;
        bashful.enemies_[1].animationTimer = 0.0;
        constexpr std::uint64_t CapturedBashfulDwellHash =
            0xb5e9d4f2896dc571ull;
        constexpr std::uint64_t CapturedBashfulOpenHash =
            0x4c92bb70c7130902ull;
        constexpr std::uint64_t CapturedBashfulClosedHash =
            0x04dddb0a1d7e4f7aull;
        constexpr int selectedBashfulSlot = 1;
        bashful.feedbackEnemySlot_ = bashful.enemySlotCount_ + 1;
        bashful.render(renderer);
        const std::uint64_t dwellHash = bashfulCellHash(renderer);
        bashful.feedbackEnemySlot_ = selectedBashfulSlot;
        if (dwellHash != CapturedBashfulDwellHash) {
            std::cerr << "captured Bashful collision dwell hash=0x" << std::hex
                      << dwellHash << " expected=0x" << CapturedBashfulDwellHash
                      << std::dec << '\n';
            return false;
        }
        for (int tick = 0; tick < Game::TroggleEatAnimationTicks; ++tick) {
            bashful.deathSequenceTicks_ = tick;
            bashful.render(renderer);
            const std::uint64_t actual = bashfulCellHash(renderer);
            const std::uint64_t expected = (tick & 1) == 0
                ? CapturedBashfulOpenHash : CapturedBashfulClosedHash;
            if (actual != expected) {
                std::cerr << "captured Bashful collision tick " << tick
                          << " hash=0x" << std::hex << actual << " expected=0x"
                          << expected << std::dec << '\n';
                return false;
            }
        }

        Game::Enemy selectedBashful = bashful.enemies_[1];
        Game::Enemy residentWorker;
        residentWorker.row = 4;
        residentWorker.column = 0;
        residentWorker.type = 1;
        residentWorker.slot = 0;
        residentWorker.direction = 3;
        residentWorker.dwellFrame = 4;
        Game::Enemy residentReggie;
        residentReggie.row = 0;
        residentReggie.column = 5;
        residentReggie.fromRow = 0;
        residentReggie.fromColumn = 6;
        residentReggie.type = 0;
        residentReggie.slot = 2;
        residentReggie.direction = 3;
        residentReggie.dwellFrame = 10;
        bashful.enemies_ = {residentWorker, selectedBashful, residentReggie};
        bashful.enemySlotCount_ = 3;
        bashful.feedbackEnemySlot_ = bashful.enemySlotCount_ + 1;
        bashful.deathAnimating_ = true;
        bashful.feedbackMessage_.clear();
        bashful.enemies_[2].entering = true;
        bashful.enemies_[2].moving = true;
        const double reggieEntryDuration =
            bashful.enemyMoveAnimationDuration(3);
        static constexpr std::array<std::uint64_t, 5>
            CapturedReggieApproachFullHashes = {
                0xf8f3a2c263541298ull, 0xf0cbec6e6211f047ull,
                0xba340f8d65e4a4d5ull, 0x72b4f8c27d6cf048ull,
                0x281513927419c02bull,
            };
        for (int phase = 2; phase <= 6; ++phase) {
            bashful.enemies_[2].animationTimer =
                (8 - phase) * reggieEntryDuration / 6.0;
            const std::uint64_t actual = fullFrameHash(bashful);
            const std::uint64_t expected =
                CapturedReggieApproachFullHashes[
                    static_cast<std::size_t>(phase - 2)];
            if (actual != expected) {
                std::cerr << "captured fourth-board Reggie approach phase "
                          << phase << " hash=0x" << std::hex << actual
                          << " expected=0x" << expected << std::dec << '\n';
                return false;
            }
        }
        constexpr std::uint64_t CapturedBashfulPreBiteHash =
            0x281513927419c02bull;
        const std::uint64_t preBiteHash = fullFrameHash(bashful);
        if (preBiteHash != CapturedBashfulPreBiteHash) {
            std::cerr << "captured Bashful pre-bite full-frame hash=0x"
                      << std::hex << preBiteHash << " expected=0x"
                      << CapturedBashfulPreBiteHash << std::dec << '\n';
            return false;
        }

        bashful.enemies_[2].entering = false;
        bashful.enemies_[2].moving = false;
        bashful.enemies_[2].animationTimer = 0.0;
        bashful.feedbackEnemySlot_ = 1;
        constexpr std::uint64_t CapturedBashfulOpenFullHash =
            0x51bfb8a5c8690ffeull;
        constexpr std::uint64_t CapturedBashfulClosedFullHash =
            0x0006ec7c3d12ca3eull;
        for (int tick = 0; tick < Game::TroggleEatAnimationTicks; ++tick) {
            bashful.deathSequenceTicks_ = tick;
            const std::uint64_t actual = fullFrameHash(bashful);
            const std::uint64_t expected = (tick & 1) == 0
                ? CapturedBashfulOpenFullHash : CapturedBashfulClosedFullHash;
            if (actual != expected) {
                std::cerr << "captured Bashful full-frame bite tick " << tick
                          << " hash=0x" << std::hex << actual << " expected=0x"
                          << expected << std::dec << '\n';
                return false;
            }
        }

        bashful.deathAnimating_ = false;
        bashful.feedbackMessage_ = "Aargh";
        constexpr std::uint64_t CapturedBashfulFeedbackHash =
            0x1a6de5ece4d3e73eull;
        const std::uint64_t feedbackHash = fullFrameHash(bashful);
        if (feedbackHash != CapturedBashfulFeedbackHash) {
            std::cerr << "captured Bashful feedback full-frame hash=0x" << std::hex
                      << feedbackHash << " expected=0x"
                      << CapturedBashfulFeedbackHash << std::dec << '\n';
            return false;
        }
        bashful.attractPostFeedbackBoard_ = true;
        bashful.attractHallWipeVariant_ = Game::AttractHallWipeVariant::Collision;
        bashful.attractHallTransitionPhase_ =
            Game::AttractHallTransitionPhase::CollisionTransient;
        constexpr std::uint64_t CapturedBashfulPostFeedbackHash =
            0x7a5db4784587a379ull;
        const std::uint64_t postFeedbackHash = fullFrameHash(bashful);
        if (postFeedbackHash != CapturedBashfulPostFeedbackHash) {
            std::cerr << "captured Bashful post-feedback full-frame hash=0x"
                      << std::hex << postFeedbackHash << " expected=0x"
                      << CapturedBashfulPostFeedbackHash << std::dec << '\n';
            return false;
        }
        return true;
    }

    static void prepareCapturedNameEntry(Game& game) {
        game.page_ = Game::Page::NameEntry;
        game.activeBoardMode_ = Game::Mode::Inequality;
        game.mode_ = Game::Mode::Inequality;
        game.target_ = 16;
        game.relation_ = 1;
        game.level_ = 1;
        game.score_ = 85;
        game.lives_ = 0;
        game.nameInput_ = "mm";
        game.enemies_.clear();
        for (Game::Cell& cell : game.cells_) cell = {};
        // These four records are the unobscured cells in the lossless
        // original 320x200 name-entry frame. The modal covers every other
        // board label, so their retained values cannot affect the reference.
        game.cell(0, 0).label = "12-0";
        game.cell(0, 1).label = "34-8";
        game.cell(0, 3).label = "2x13";
        game.cell(4, 3).label = "1+5";
    }

    static bool usesCapturedNameEntryInput(Game& game) {
        game.scorePersistenceEnabled_ = false;
        game.page_ = Game::Page::NameEntry;
        game.mode_ = Game::Mode::Inequality;
        game.score_ = 85;
        game.level_ = 1;
        game.nameInput_.clear();
        game.scores_[static_cast<std::size_t>(Game::Mode::Inequality)].clear();
        for (char character = 'a'; character <= 'z'; ++character) game.character(character);
        if (game.nameInput_ != "abcdefghijklmnopqrstuvwxy") return false;
        game.keyDown(VK_RETURN);
        const auto& list = game.scores_[static_cast<std::size_t>(Game::Mode::Inequality)];
        return game.page_ == Game::Page::Hall &&
               game.hallContext_ == Game::HallContext::PostGame && list.size() == 1 &&
               list[0].name == "abcdefghijklmnopqrstuvwxy" && list[0].score == 85 &&
               game.hallHighlightName_ == list[0].name && game.hallHighlightScore_ == 85;
    }

    static bool usesRecoveredNameEntryEditing(Game& game) {
        game.scorePersistenceEnabled_ = false;
        game.mode_ = Game::Mode::Multiples;
        game.score_ = 80;
        game.level_ = 2;
        auto& list = game.scores_[0];
        list.clear();
        game.page_ = Game::Page::NameEntry;
        game.nameInput_.clear();

        game.character(L'a');
        game.character(L'b');
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            game.keyDown(ignored);
            if (game.nameInput_ != "ab") return false;
        }
        game.keyDown(VK_LEFT);
        if (game.nameInput_ != "a") return false;
        game.character(L'\b');
        if (!game.nameInput_.empty()) return false;
        game.character(L'c');
        game.character(L'd');
        game.keyDown(VK_HOME);
        if (!game.nameInput_.empty()) return false;

        // DS:0A4C stores a zero minimum threshold. Empty Enter exits the
        // editor, and the Hall caller substitutes DS:0996.
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Hall ||
            game.hallContext_ != Game::HallContext::PostGame || list.size() != 1 ||
            list[0].name != "The Unknown Muncher" ||
            game.hallHighlightName_ != list[0].name || game.hallHighlightScore_ != 80) return false;

        // Escape also exits unconditionally; a partial name is retained.
        list.clear();
        game.page_ = Game::Page::NameEntry;
        game.nameInput_.clear();
        game.character(L'x');
        game.character(L'y');
        game.keyDown(VK_ESCAPE);
        return game.page_ == Game::Page::Hall &&
               game.hallContext_ == Game::HallContext::PostGame &&
               list.size() == 1 && list[0].name == "xy" &&
               game.hallHighlightName_ == "xy" && game.hallHighlightScore_ == 80;
    }

    static bool usesRecoveredHallAdmissionAndTieRules(Game& game) {
        game.scorePersistenceEnabled_ = false;
        game.mode_ = Game::Mode::Multiples;
        auto& list = game.scores_[0];
        list.clear();

        game.score_ = 49;
        if (game.qualifiesForHallOfFame()) return false;
        game.score_ = 50;
        if (!game.qualifiesForHallOfFame()) return false;

        list = {{100, 1, "leader"}, {80, 1, "first tie"},
                {80, 2, "second tie"}, {60, 1, "tail"}};
        game.score_ = 80;
        game.level_ = 3;
        game.nameInput_ = "new tie";
        game.submitScore();
        if (list.size() != 5 || list[1].name != "first tie" ||
            list[2].name != "second tie" || list[3].name != "new tie") return false;

        list = {{500, 1, "1"}, {450, 1, "2"}, {400, 1, "3"},
                {350, 1, "4"}, {300, 1, "5"}, {250, 1, "6"},
                {200, 1, "7"}, {150, 1, "8"}, {100, 1, "9"},
                {50, 1, "10"}};
        game.score_ = 50;
        if (game.qualifiesForHallOfFame()) return false;
        game.score_ = 51;
        return game.qualifiesForHallOfFame();
    }

    static bool usesRecoveredHallNavigation(Game& game) {
        game.attractMode_ = false;

        // 0x12b6d has an empty accelerator list and terminates on Enter, not
        // keyboard Space. Both the Play and Hall selectors use this routine.
        game.page_ = Game::Page::ModeSelect;
        game.menuSelection_ = 0;
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::ModeSelect || game.menuSelection_ != 0) return false;

        game.page_ = Game::Page::HallSelect;
        game.menuSelection_ = 0;
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::HallSelect || game.menuSelection_ != 0) return false;
        game.keyDown(VK_DOWN);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Hall ||
            game.hallContext_ != Game::HallContext::Browse ||
            game.mode_ != Game::Mode::Factors) return false;

        // Display argument 7 ignores ordinary letters and accepts the exact
        // DS:0864 set. Its caller then redraws the title menu, not the Hall
        // selector.
        game.keyDown('Q');
        if (game.page_ != Game::Page::Hall) return false;
        game.hallHighlightName_ = "transient";
        game.hallHighlightScore_ = 123;
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::Title || !game.hallHighlightName_.empty() ||
            game.hallHighlightScore_ != 0) return false;

        game.page_ = Game::Page::HallSelect;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Title) return false;

        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Browse;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Title) return false;

        // The end-game callback also paints argument 7, then continues to the
        // same-mode replay question after the accepted Hall key.
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::PostGame;
        game.hallHighlightName_ = "transient";
        game.hallHighlightScore_ = 123;
        game.keyDown('Q');
        if (game.page_ != Game::Page::Hall) return false;
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::ReplayQuestion || game.menuSelection_ != 0 ||
            !game.hallHighlightName_.empty() || game.hallHighlightScore_ != 0) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Title) return false;

        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::PostGame;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::ReplayQuestion) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Title) return false;

        game.page_ = Game::Page::ReplayQuestion;
        game.mode_ = Game::Mode::Primes;
        game.menuSelection_ = 1;
        game.keyDown('Y');
        if (game.page_ != Game::Page::ReplayQuestion || game.menuSelection_ != 0) return false;
        game.keyDown(VK_SPACE);
        game.keyDown(VK_UP);
        game.keyDown(VK_DOWN);
        if (game.page_ != Game::Page::ReplayQuestion || game.menuSelection_ != 0) return false;
        game.keyDown(VK_LEFT);
        game.keyDown(VK_LEFT);
        if (game.page_ != Game::Page::ReplayQuestion || game.menuSelection_ != 0) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_RIGHT);
        if (game.page_ != Game::Page::ReplayQuestion || game.menuSelection_ != 1) return false;
        game.keyDown(VK_LEFT);
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::Playing && game.mode_ == Game::Mode::Primes &&
               game.level_ == 1;
    }

    static bool usesRecoveredReplayPaletteCarryover() {
        Game game;
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.mode_ = Game::Mode::Primes;
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::PostGame;
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::ReplayQuestion) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Playing || !game.hallPaletteActive_) return false;
        while (game.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            game.transitionTimer_ = 0.0;
            game.update(0.0);
        }

        game.munching_ = true;
        game.munchTimer_ = Game::MunchAnimationDuration;
        Renderer renderer;
        game.render(renderer);
        const std::vector<std::uint32_t> replayHall = renderer.pixels();
        game.hallPaletteActive_ = false;
        game.render(renderer);
        const std::vector<std::uint32_t> replayInitial = renderer.pixels();
        // First-board Number Demo frame 2544 already contains 0x414100 at
        // the one occupied index-111 player pixel. The retained Hall flag is
        // a real controller state, but it does not alter this player slot.
        if (replayHall != replayInitial) return false;

        game.startGame(Game::Mode::Primes);
        if (game.hallPaletteActive_) return false;
        game.hallPaletteActive_ = true;
        game.startAttract();
        if (game.hallPaletteActive_) return false;
        game.beginAttractHall();
        if (!game.hallPaletteActive_) return false;
        game.stopAttract();
        return !game.hallPaletteActive_ && game.page_ == Game::Page::Title;
    }

    static bool usesRecoveredCgaCartoonPresenter() {
        Game cartoon(GraphicsMode::Cga4);
        cartoon.settingsPersistenceEnabled_ = false;
        cartoon.scorePersistenceEnabled_ = false;
        cartoon.random_.seed(0x4213u);
        cartoon.startGame(Game::Mode::Multiples, 3);
        Renderer sourceRenderer(GraphicsMode::Cga4);
        cartoon.render(sourceRenderer);
        const std::vector<std::uint32_t> source = sourceRenderer.pixels();
        cartoon.completeLevel();
        const int selectedScene = cartoon.pendingCartoonScene_;
        if (cartoon.page_ != Game::Page::LevelComplete ||
            cartoon.levelCompleteScene_.valid() || selectedScene < 0 ||
            cartoon.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToCartoon ||
            cartoon.attractInterstitialFrame_ != 0 ||
            cartoon.boardPresentationSourcePixels_ != source ||
            cartoon.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 5> hashes{};
        for (int frame = 0; frame < static_cast<int>(hashes.size()); ++frame) {
            hashes[static_cast<std::size_t>(frame)] = fullFrameHash(cartoon);
            cartoon.transitionTimer_ = 0.0;
            cartoon.update(0.0);
            if (frame == 0) {
                constexpr double CaptureFrameSeconds = 31250.0 / 2190197.0;
                constexpr std::array<int, 5> CoveredSamples = {43, 43, 44, 43, 44};
                const double expectedCovered =
                    (CoveredSamples[static_cast<std::size_t>(selectedScene)] - 1) *
                    CaptureFrameSeconds;
                if (std::abs(cartoon.transitionTimer_ - expectedCovered) > 1e-12 ||
                    cartoon.levelCompleteScene_.valid()) return false;
            }
            if (frame == 1 && !cartoon.levelCompleteScene_.valid()) return false;
        }
        static constexpr std::array<std::uint64_t, 5> ExpectedHashes = {
            0x4f9727e1e24262b4ull, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0xbd376e82f1418183ull,
            0xf75309bcac42bb83ull,
        };
        if (hashes != ExpectedHashes) {
            std::cerr << "number CGA cartoon presenter hashes:";
            for (const std::uint64_t hash : hashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        return cartoon.attractInterstitialTransition_ ==
                   Game::AttractInterstitialTransition::None &&
               cartoon.levelCompleteScene_.valid() &&
               cartoon.sceneTickAccumulator_ == 0.0 &&
               cartoon.acceptsCommonDispatcherInput();
    }

    static bool usesRecoveredUserBoardPresenter() {
        Game game;
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.random_.seed(0x4211u);
        game.page_ = Game::Page::Title;
        game.menuSelection_ = 0;

        Renderer renderer;
        game.render(renderer);
        const std::vector<std::uint32_t> source = renderer.pixels();
        game.startGameWithBoardPresentation(Game::Mode::Multiples, 1);
        if (game.page_ != Game::Page::Playing || game.level_ != 1 ||
            game.attractMode_ ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToBoard ||
            game.attractInterstitialFrame_ != 0 ||
            game.boardPresentationSourcePixels_ != source) return false;

        const int row = game.playerRow_;
        const int column = game.playerColumn_;
        game.keyDown(VK_RIGHT);
        game.character(L'6');
        game.pointerButton(Game::BoardLeft + 1, Game::BoardTop + 1, false);
        if (game.moving_ || game.munching_ || game.playerRow_ != row ||
            game.playerColumn_ != column || game.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 9> hashes{};
        for (int frame = 0; frame < static_cast<int>(hashes.size()); ++frame) {
            if (game.attractInterstitialTransition_ !=
                    Game::AttractInterstitialTransition::UserToBoard ||
                game.attractInterstitialFrame_ != frame) return false;
            hashes[static_cast<std::size_t>(frame)] = fullFrameHash(game);
            game.transitionTimer_ = 0.0;
            game.update(0.0);
        }
        if (game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::None ||
            !game.boardPresentationSourcePixels_.empty() ||
            !game.acceptsCommonDispatcherInput()) return false;

        constexpr std::array<std::uint64_t, 9> ExpectedHashes = {
            0xdcf1b974b9f9916full, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x8887db592e3c9ca1ull,
            0xb1a8f948642db583ull, 0xb1a8f948642db583ull,
            0xb1a8f948642db583ull, 0x28e9f37d90d3cd0bull,
            0xded8296b313be0bbull,
        };
        if (hashes != ExpectedHashes) {
            std::cerr << "number user board presenter hashes:";
            for (const std::uint64_t hash : hashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }

        Game direct;
        direct.settingsPersistenceEnabled_ = false;
        direct.scorePersistenceEnabled_ = false;
        direct.random_.seed(0x4212u);
        direct.startGame(Game::Mode::Multiples, 1);
        direct.completeLevel();
        if (direct.level_ != 2 || direct.page_ != Game::Page::Playing ||
            direct.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToBoard) return false;
        while (direct.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            direct.transitionTimer_ = 0.0;
            direct.update(0.0);
        }

        Game cartoon;
        cartoon.settingsPersistenceEnabled_ = false;
        cartoon.scorePersistenceEnabled_ = false;
        cartoon.random_.seed(0x4213u);
        cartoon.startGame(Game::Mode::Multiples, 3);
        Renderer cartoonSourceRenderer;
        cartoon.render(cartoonSourceRenderer);
        const std::vector<std::uint32_t> cartoonSource =
            cartoonSourceRenderer.pixels();
        const std::size_t loaderStopsBefore =
            cartoon.attractOplPlayer_.allChannelStopCountForTest();
        cartoon.completeLevel();
        if (cartoon.page_ != Game::Page::LevelComplete ||
            cartoon.levelCompleteScene_.valid() ||
            cartoon.pendingCartoonScene_ < 0 ||
            cartoon.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToCartoon ||
            cartoon.attractInterstitialFrame_ != 0 ||
            cartoon.boardPresentationSourcePixels_ != cartoonSource ||
            cartoon.sceneTickAccumulator_ != 0.0 ||
            cartoon.attractOplPlayer_.scoreWriteCount() != 0) return false;
        const int selectedCartoonScene = cartoon.pendingCartoonScene_;
        (void)cartoon.attractOplPlayer_.renderTestSamples(1);
        if (cartoon.attractOplPlayer_.allChannelStopCountForTest() !=
                loaderStopsBefore + 1 ||
            cartoon.attractOplPlayer_.effectPlaying()) return false;

        const bool soundBefore = cartoon.soundOn_;
        cartoon.keyDown(VK_LEFT);
        cartoon.character(L'+');
        cartoon.pointerButton(Game::BoardLeft + 1, Game::BoardTop + 1, false);
        cartoon.toggleSound();
        if (cartoon.soundOn_ != soundBefore || cartoon.level_ != 3 ||
            cartoon.cartoonOrderIndex_ != 0 ||
            cartoon.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 5> cartoonHashes{};
        for (int frame = 0; frame < static_cast<int>(cartoonHashes.size()); ++frame) {
            if (cartoon.attractInterstitialTransition_ !=
                    Game::AttractInterstitialTransition::UserToCartoon ||
                cartoon.attractInterstitialFrame_ != frame) return false;
            cartoonHashes[static_cast<std::size_t>(frame)] = fullFrameHash(cartoon);
            cartoon.transitionTimer_ = 0.0;
            cartoon.update(0.0);
            if (frame == 0) {
                constexpr double captureFrameSeconds = 31250.0 / 2190197.0;
                constexpr std::array<int, 5> coveredSamples = {43, 43, 44, 43, 44};
                const double expectedCovered = std::max(
                    0.015,
                    (coveredSamples[static_cast<std::size_t>(selectedCartoonScene)] - 1) *
                        captureFrameSeconds);
                if (std::abs(cartoon.transitionTimer_ - expectedCovered) > 1e-12 ||
                    cartoon.levelCompleteScene_.valid() ||
                    cartoon.pendingCartoonScene_ < 0) return false;
                // The original load delay is quiet: no setup callback may
                // repopulate channel 0 during its 745 generated samples.
                (void)cartoon.attractOplPlayer_.renderTestSamples(745);
                if (cartoon.attractOplPlayer_.effectPlaying()) return false;
            }
            if (frame == 1 &&
                (!cartoon.levelCompleteScene_.valid() ||
                 cartoon.pendingCartoonScene_ != -1)) return false;
            if (frame == 3 &&
                (std::abs(cartoon.transitionTimer_ - 0.010) > 1e-12 ||
                 cartoon.attractOplPlayer_.scoreWriteCount() != 0)) return false;
        }
        if (cartoon.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::None ||
            !cartoon.boardPresentationSourcePixels_.empty() ||
            !cartoon.acceptsCommonDispatcherInput() ||
            cartoon.sceneTickAccumulator_ != 0.0) return false;
        const BlobView cartoonBank = cartoon.assets_.gameArchive().find(
            "ADLI", static_cast<std::uint32_t>(cartoon.cutsceneSoundBank_));
        const MeccSound cartoonScore = decodeMeccGSound(cartoonBank, 38);
        if (!cartoonScore.valid || !cartoon.attractOplPlayer_.playing() ||
            cartoon.attractOplPlayer_.looping() ||
            cartoon.attractOplPlayer_.scoreWriteCount() != cartoonScore.writes.size()) {
            return false;
        }

        constexpr std::array<std::uint64_t, 5> ExpectedCartoonHashes = {
            0xcee45715a1dbb0a7ull, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0xbd376e82f1418183ull,
            0x2f6bfdfe6b2b8183ull,
        };
        if (cartoonHashes != ExpectedCartoonHashes) {
            std::cerr << "number user cartoon presenter hashes:";
            for (const std::uint64_t hash : cartoonHashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }

        // Scene 4 formerly retained only the executable-proven 15 ms minimum.
        // Its lossless Number capture measures 44 cyan samples, so lock that
        // bank explicitly instead of relying on the shuffled scene above.
        Game sceneFourTransition;
        sceneFourTransition.settingsPersistenceEnabled_ = false;
        sceneFourTransition.scorePersistenceEnabled_ = false;
        sceneFourTransition.random_.seed(0x4214u);
        sceneFourTransition.startGame(Game::Mode::Multiples, 3);
        sceneFourTransition.cartoonOrderIndex_ = 1;
        sceneFourTransition.cartoonOrder_[1] = 4;
        sceneFourTransition.completeLevel();
        if (sceneFourTransition.pendingCartoonScene_ != 4 ||
            sceneFourTransition.attractInterstitialFrame_ != 0) return false;
        sceneFourTransition.transitionTimer_ = 0.0;
        sceneFourTransition.update(0.0);
        constexpr double sceneFourCovered =
            (44 - 1) * (31250.0 / 2190197.0);
        if (sceneFourTransition.attractInterstitialFrame_ != 1 ||
            std::abs(sceneFourTransition.transitionTimer_ - sceneFourCovered) > 1e-12) {
            return false;
        }

        const auto prepareFinalCartoon = [](Game& candidate) {
            candidate.settingsPersistenceEnabled_ = false;
            candidate.scorePersistenceEnabled_ = false;
            candidate.random_.seed(0x4d44u);
            candidate.startGame(Game::Mode::Multiples, 3);
            candidate.soundOn_ = false;
            candidate.musicOn_ = false;
            candidate.enemies_.clear();
            candidate.enemySlotCount_ = 0;
            candidate.safeZoneJobCount_ = 0;
            for (Game::Cell& cell : candidate.cells_) cell = {};
            constexpr int finalCell = Game::BoardColumns + 1;
            candidate.cells_[finalCell].label = "5";
            candidate.cells_[finalCell].correct = true;
            candidate.correctRemaining_ = 1;
            candidate.playerRow_ = finalCell / Game::BoardColumns;
            candidate.playerColumn_ = finalCell % Game::BoardColumns;
            candidate.munch();
            candidate.munchTimer_ = 1.0 / OriginalSchedulerTicksPerSecond;
            candidate.gameplayTickAccumulator_ = 0.0;
            return candidate.munching_;
        };
        Game directCartoon;
        Game scheduledCartoon;
        if (!prepareFinalCartoon(directCartoon) ||
            !prepareFinalCartoon(scheduledCartoon)) return false;
        constexpr double postCartoonTerminalTicks = 1.25;
        directCartoon.resolveMunch();
        directCartoon.update(postCartoonTerminalTicks /
                             OriginalSchedulerTicksPerSecond);
        scheduledCartoon.update((1.0 + postCartoonTerminalTicks) /
                                OriginalSchedulerTicksPerSecond);
        if (directCartoon.page_ != Game::Page::LevelComplete ||
            scheduledCartoon.page_ != Game::Page::LevelComplete ||
            directCartoon.level_ != 3 || scheduledCartoon.level_ != 3 ||
            directCartoon.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToCartoon ||
            scheduledCartoon.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::UserToCartoon ||
            directCartoon.attractInterstitialFrame_ !=
                scheduledCartoon.attractInterstitialFrame_ ||
            std::abs(directCartoon.transitionTimer_ -
                     scheduledCartoon.transitionTimer_) > 1e-12 ||
            directCartoon.sceneTickAccumulator_ != 0.0 ||
            scheduledCartoon.sceneTickAccumulator_ != 0.0 ||
            directCartoon.random_.state != scheduledCartoon.random_.state ||
            directCartoon.random_.calls != scheduledCartoon.random_.calls) return false;

        // The high-bit resident branch gates stream 38 by Music and the
        // selected AdLib device, independently of the ordinary Sound flag.
        Game soundIndependent;
        soundIndependent.soundOn_ = true;
        soundIndependent.musicOn_ = true;
        soundIndependent.speakerEffects_ = false;
        soundIndependent.cutsceneSoundBank_ = 11;
        soundIndependent.playOriginalAdli(11, 1);
        const std::size_t preservedCueCount =
            soundIndependent.attractOplPlayer_.effectPlayCount();
        soundIndependent.soundOn_ = false;
        soundIndependent.playCartoonScore();
        Game musicDisabled;
        musicDisabled.musicOn_ = false;
        musicDisabled.speakerEffects_ = false;
        musicDisabled.cutsceneSoundBank_ = 11;
        musicDisabled.playCartoonScore();
        Game speakerSelected;
        speakerSelected.musicOn_ = true;
        speakerSelected.speakerEffects_ = true;
        speakerSelected.cutsceneSoundBank_ = 11;
        speakerSelected.playCartoonScore();
        if (!soundIndependent.attractOplPlayer_.playing() ||
            soundIndependent.attractOplPlayer_.scoreWriteCount() == 0 ||
            !soundIndependent.attractOplPlayer_.effectPlaying() ||
            soundIndependent.attractOplPlayer_.effectPlayCount() != preservedCueCount ||
            musicDisabled.attractOplPlayer_.playing() ||
            speakerSelected.attractOplPlayer_.playing()) return false;

        (void)cartoon.attractOplPlayer_.renderTestSamples(1);
        const std::uint8_t retainedDepth = static_cast<std::uint8_t>(
            cartoon.attractOplPlayer_.registerValueForTest(0xbd) & 0xc0u);
        const std::size_t teardownStopsBefore =
            cartoon.attractOplPlayer_.allChannelStopCountForTest();
        cartoon.finishLevelComplete();
        // 15 ms floors to 745 samples at the YM3812's 49,715 Hz rate. The
        // outer teardown's second GSND-0 dispatch is due on the next sample.
        (void)cartoon.attractOplPlayer_.renderTestSamples(745);
        if (cartoon.attractOplPlayer_.allChannelStopCountForTest() !=
            teardownStopsBefore + 1) return false;
        (void)cartoon.attractOplPlayer_.renderTestSamples(1);
        return cartoon.level_ == 4 && cartoon.page_ == Game::Page::Playing &&
               cartoon.attractOplPlayer_.allChannelStopCountForTest() ==
                   teardownStopsBefore + 2 &&
               exactResidentStopApplied(cartoon.attractOplPlayer_, retainedDepth) &&
               cartoon.attractInterstitialTransition_ ==
                   Game::AttractInterstitialTransition::UserToBoard;
    }

    static bool usesRecoveredMenuPointerWidgets() {
        Game title;
        title.settingsPersistenceEnabled_ = false;
        title.scorePersistenceEnabled_ = false;
        title.page_ = Game::Page::Title;
        title.menuSelection_ = 0;

        // INT 33h mask 0x001e excludes motion, so movement never hover-selects
        // a normal list item or restart the title's attract timeout. Inter-row
        // gaps and the ragged right edge also do not count as item hits.
        title.pointerMove(60, 105);
        if (title.menuSelection_ != 0) return false;
        title.update(29.9);
        title.pointerMove(60, 105);
        title.update(0.11);
        if (!title.attractMode_ || title.page_ != Game::Page::Attract) return false;
        title.stopAttract();
        title.pointerButton(60, 100, false);  // one-pixel gap after Play
        title.pointerButton(186, 105, false); // one past Hall's right edge
        if (title.page_ != Game::Page::Title || title.menuSelection_ != 0) return false;

        // First click changes the highlight; the click on the selected row
        // is the event the DOS widget converts to carriage return.
        title.pointerButton(60, 105, false);
        if (title.page_ != Game::Page::Title || title.menuSelection_ != 1) return false;
        title.pointerButton(60, 105, false);
        if (title.page_ != Game::Page::HallSelect || title.menuSelection_ != 0) return false;
        title.pointerMove(100, 84);
        if (title.menuSelection_ != 0) return false;
        title.pointerButton(100, 84, false);
        if (title.page_ != Game::Page::HallSelect || title.menuSelection_ != 1) return false;
        title.pointerButton(100, 84, false);
        if (title.page_ != Game::Page::Hall || title.mode_ != Game::Mode::Factors ||
            title.hallContext_ != Game::HallContext::Browse) return false;

        // Event type 13 (right release) is converted to carriage return. It
        // activates the current selection without pointer hit-testing.
        Game secondary;
        secondary.settingsPersistenceEnabled_ = false;
        secondary.page_ = Game::Page::Title;
        secondary.menuSelection_ = 1;
        secondary.pointerButton(0, 0, true);
        if (secondary.page_ != Game::Page::HallSelect || secondary.menuSelection_ != 0) {
            return false;
        }

        // With one enabled ordinary game plus Challenge, the selector moves
        // its two rows from y=70 to y=90. Its old top position and the first
        // row's one-past-right edge must not activate anything.
        Game selector;
        selector.settingsPersistenceEnabled_ = false;
        for (int index = 1; index < static_cast<int>(Game::Mode::Challenge); ++index) {
            selector.contentSettings_[static_cast<std::size_t>(index)].use = false;
        }
        selector.page_ = Game::Page::ModeSelect;
        selector.menuSelection_ = 0;
        selector.pointerButton(100, 74, false);
        selector.pointerButton(203, 94, false);
        if (selector.page_ != Game::Page::ModeSelect || selector.menuSelection_ != 0) return false;
        Renderer selectorRenderer;
        selector.render(selectorRenderer);
        if (selectorRenderer.pixels()[89 * Renderer::Width + 113] != Colors::White ||
            selectorRenderer.pixels()[69 * Renderer::Width + 113] == Colors::White) return false;
        selector.pointerButton(100, 104, false);
        if (selector.page_ != Game::Page::ModeSelect || selector.menuSelection_ != 1) return false;
        selector.pointerButton(100, 104, false);
        if (selector.page_ != Game::Page::Playing || selector.mode_ != Game::Mode::Challenge) return false;

        // A one-entry selector is bypassed by the original caller.
        Game soleMode;
        soleMode.settingsPersistenceEnabled_ = false;
        for (int index = 0; index < static_cast<int>(Game::Mode::Challenge); ++index) {
            soleMode.contentSettings_[static_cast<std::size_t>(index)].use = false;
        }
        soleMode.enterModeSelect();
        if (soleMode.page_ != Game::Page::Playing || soleMode.mode_ != Game::Mode::Challenge) return false;

        Game prompt;
        prompt.settingsPersistenceEnabled_ = false;
        prompt.page_ = Game::Page::ReplayQuestion;
        prompt.mode_ = Game::Mode::Primes;
        prompt.menuSelection_ = 1;
        prompt.pointerMove(120, 105);
        if (prompt.menuSelection_ != 1) return false;
        prompt.pointerButton(156, 105, false); // one past Yes's right edge
        prompt.pointerButton(120, 114, false); // one past its bottom edge
        if (prompt.page_ != Game::Page::ReplayQuestion || prompt.menuSelection_ != 1) return false;
        prompt.pointerButton(120, 105, false);
        if (prompt.page_ != Game::Page::ReplayQuestion || prompt.menuSelection_ != 0) return false;
        prompt.pointerButton(120, 105, false);
        if (prompt.page_ != Game::Page::Playing || prompt.mode_ != Game::Mode::Primes) return false;

        // The empty custom-key lists on these generic widgets do not contain
        // Space. Yes/No's list contains arrows and Y/N, but not Space either.
        Game keyboard;
        keyboard.settingsPersistenceEnabled_ = false;
        keyboard.page_ = Game::Page::Title;
        keyboard.menuSelection_ = 0;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::Title) return false;
        keyboard.page_ = Game::Page::InstructionsQuestion;
        keyboard.menuSelection_ = 1;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::InstructionsQuestion) return false;
        keyboard.page_ = Game::Page::ReplayQuestion;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::ReplayQuestion) return false;
        keyboard.page_ = Game::Page::QuitConfirm;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::QuitConfirm) return false;
        keyboard.page_ = Game::Page::Options;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::Options) return false;
        keyboard.page_ = Game::Page::OptionsDifficulty;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::OptionsDifficulty) return false;
        keyboard.page_ = Game::Page::OptionsEraseAllConfirm;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::OptionsEraseAllConfirm) return false;

        // The common fixed-list widget treats Left like Up and Right like
        // Down, with wrapping; Home/End select the first/last row. None of
        // those keys activates the selected row.
        Game navigation;
        navigation.settingsPersistenceEnabled_ = false;
        navigation.scorePersistenceEnabled_ = false;
        navigation.page_ = Game::Page::Title;
        navigation.menuSelection_ = 2;
        navigation.keyDown(VK_LEFT);
        navigation.keyDown(VK_RIGHT);
        if (navigation.page_ != Game::Page::Title || navigation.menuSelection_ != 2) return false;
        navigation.keyDown(VK_HOME);
        if (navigation.menuSelection_ != 0) return false;
        navigation.keyDown(VK_LEFT);
        if (navigation.menuSelection_ != 4) return false;
        navigation.keyDown(VK_RIGHT);
        navigation.keyDown(VK_END);
        if (navigation.page_ != Game::Page::Title || navigation.menuSelection_ != 4) return false;

        for (int index = 1; index < static_cast<int>(Game::Mode::Challenge); ++index) {
            navigation.contentSettings_[static_cast<std::size_t>(index)].use = false;
        }
        navigation.page_ = Game::Page::ModeSelect;
        navigation.menuSelection_ = 0;
        navigation.keyDown(VK_LEFT);
        if (navigation.page_ != Game::Page::ModeSelect || navigation.menuSelection_ != 1) return false;
        navigation.keyDown(VK_HOME);
        navigation.keyDown(VK_END);
        if (navigation.menuSelection_ != 1) return false;

        navigation.page_ = Game::Page::HallSelect;
        navigation.menuSelection_ = 3;
        navigation.keyDown(VK_HOME);
        navigation.keyDown(VK_END);
        if (navigation.page_ != Game::Page::HallSelect || navigation.menuSelection_ != 5) return false;

        navigation.page_ = Game::Page::Options;
        navigation.menuSelection_ = 3;
        navigation.keyDown(VK_HOME);
        navigation.keyDown(VK_LEFT);
        if (navigation.page_ != Game::Page::Options || navigation.menuSelection_ != 5) return false;

        navigation.page_ = Game::Page::OptionsDifficulty;
        navigation.menuSelection_ = 0;
        navigation.keyDown(VK_LEFT);
        if (navigation.menuSelection_ != 10) return false;
        navigation.keyDown(VK_RIGHT);
        navigation.keyDown(VK_END);
        if (navigation.page_ != Game::Page::OptionsDifficulty || navigation.menuSelection_ != 10) {
            return false;
        }

        navigation.page_ = Game::Page::OptionsEraseHall;
        navigation.menuSelection_ = 0;
        navigation.keyDown(VK_END);
        navigation.keyDown(VK_RIGHT);
        if (navigation.page_ != Game::Page::OptionsEraseHall || navigation.menuSelection_ != 0) {
            return false;
        }

        navigation.eraseDraft_.clear();
        for (int index = 0; index < 10; ++index) {
            navigation.eraseDraft_.push_back({100 - index, 1, "entry" + std::to_string(index)});
        }
        navigation.page_ = Game::Page::OptionsEraseEntries;
        navigation.eraseSelection_ = 4;
        navigation.keyDown(VK_HOME);
        navigation.keyDown(VK_LEFT);
        if (navigation.page_ != Game::Page::OptionsEraseEntries ||
            navigation.eraseSelection_ != 9 || navigation.eraseDraft_.size() != 10) return false;
        navigation.keyDown(VK_RIGHT);
        navigation.keyDown(VK_END);
        if (navigation.eraseSelection_ != 9 || navigation.eraseDraft_.size() != 10) return false;

        // Yes/No's custom list consumes Up/Down as no-ops. Left/Home choose
        // Yes and Right/End choose No, again without accepting the prompt.
        for (const Game::Page promptPage : {
                 Game::Page::InstructionsQuestion, Game::Page::ReplayQuestion,
                 Game::Page::QuitConfirm, Game::Page::OptionsEraseAllConfirm}) {
            navigation.page_ = promptPage;
            navigation.menuSelection_ = 1;
            navigation.keyDown(VK_UP);
            navigation.keyDown(VK_DOWN);
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 1) return false;
            navigation.keyDown(VK_HOME);
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 0) return false;
            navigation.keyDown(VK_END);
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 1) return false;
            navigation.keyDown(VK_LEFT);
            navigation.keyDown(VK_RIGHT);
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 1) return false;
            navigation.character(L'4');
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 0) return false;
            navigation.character(L'8');
            if (navigation.menuSelection_ != 1) return false;
            navigation.character(L'2');
            if (navigation.menuSelection_ != 0) return false;
            navigation.character(L'6');
            if (navigation.page_ != promptPage || navigation.menuSelection_ != 1) return false;
            navigation.character(L'$');
            if (navigation.menuSelection_ != 1) return false;
        }

        // The outer Hall-erasure caller is a deliberate exception: its local
        // dispatch joins Space with Enter after the generic widget returns.
        keyboard.scorePersistenceEnabled_ = false;
        keyboard.page_ = Game::Page::OptionsEraseHall;
        keyboard.menuSelection_ = 0;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::OptionsEraseEntries || keyboard.eraseMode_ != 0) return false;
        keyboard.page_ = Game::Page::OptionsEraseHall;
        keyboard.menuSelection_ = 6;
        keyboard.keyDown(VK_SPACE);
        if (keyboard.page_ != Game::Page::OptionsEraseAllConfirm ||
            keyboard.menuSelection_ != 1 || keyboard.eraseMode_ != 6) return false;

        // Flag 0x80 on the common numbered widget enables ASCII digits as
        // selection-only shortcuts. A selected row 1 prefixes rows 10/11.
        Game numbered;
        numbered.settingsPersistenceEnabled_ = false;
        numbered.scorePersistenceEnabled_ = false;
        numbered.page_ = Game::Page::Title;
        numbered.menuSelection_ = 4;
        numbered.character(L'3');
        if (numbered.page_ != Game::Page::Title || numbered.menuSelection_ != 2) return false;
        numbered.character(L'!');
        numbered.character(L'0');
        if (numbered.page_ != Game::Page::Title || numbered.menuSelection_ != 2) return false;

        Game filteredNumbered;
        filteredNumbered.settingsPersistenceEnabled_ = false;
        for (int index = 1; index < static_cast<int>(Game::Mode::Challenge); ++index) {
            filteredNumbered.contentSettings_[static_cast<std::size_t>(index)].use = false;
        }
        filteredNumbered.page_ = Game::Page::ModeSelect;
        filteredNumbered.menuSelection_ = 0;
        filteredNumbered.character(L'2');
        if (filteredNumbered.page_ != Game::Page::ModeSelect ||
            filteredNumbered.menuSelection_ != 1) return false;
        filteredNumbered.keyDown(VK_RETURN);
        if (filteredNumbered.page_ != Game::Page::Playing ||
            filteredNumbered.mode_ != Game::Mode::Challenge) return false;

        numbered.page_ = Game::Page::Options;
        numbered.menuSelection_ = 5;
        numbered.character(L'2');
        if (numbered.page_ != Game::Page::Options || numbered.menuSelection_ != 1) return false;
        numbered.page_ = Game::Page::OptionsEraseHall;
        numbered.menuSelection_ = 0;
        numbered.character(L'7');
        if (numbered.page_ != Game::Page::OptionsEraseHall || numbered.menuSelection_ != 6) {
            return false;
        }

        numbered.page_ = Game::Page::OptionsDifficulty;
        numbered.menuSelection_ = 7;
        numbered.character(L'1');
        numbered.character(L'0');
        if (numbered.menuSelection_ != 9) return false;
        numbered.character(L'1');
        numbered.character(L'1');
        if (numbered.page_ != Game::Page::OptionsDifficulty || numbered.menuSelection_ != 10) {
            return false;
        }

        numbered.eraseDraft_.clear();
        for (int index = 0; index < 10; ++index) {
            numbered.eraseDraft_.push_back({100 - index, 1, "entry" + std::to_string(index)});
        }
        numbered.page_ = Game::Page::OptionsEraseEntries;
        numbered.eraseSelection_ = 4;
        numbered.character(L'1');
        numbered.character(L'0');
        if (numbered.page_ != Game::Page::OptionsEraseEntries ||
            numbered.eraseSelection_ != 9 || numbered.eraseDraft_.size() != 10) return false;

        // Options-family fixed lists use the same select-then-activate rule.
        Game options;
        options.settingsPersistenceEnabled_ = false;
        options.page_ = Game::Page::Options;
        options.menuSelection_ = 0;
        options.pointerMove(60, 85);
        if (options.menuSelection_ != 0) return false;
        options.pointerButton(60, 85, false);
        if (options.page_ != Game::Page::Options || options.menuSelection_ != 1) return false;
        options.pointerButton(60, 85, false);
        if (options.page_ != Game::Page::OptionsContent) return false;

        options.page_ = Game::Page::OptionsDifficulty;
        options.menuSelection_ = 0;
        options.pointerMove(60, 63);
        if (options.menuSelection_ != 0) return false;
        options.pointerButton(60, 63, false);
        if (options.page_ != Game::Page::OptionsDifficulty || options.menuSelection_ != 1) return false;
        options.pointerButton(60, 63, false);
        if (options.page_ != Game::Page::Options) return false;

        options.page_ = Game::Page::OptionsEraseHall;
        options.menuSelection_ = 0;
        options.pointerButton(90, 86, false);
        if (options.page_ != Game::Page::OptionsEraseHall || options.menuSelection_ != 1) return false;

        options.eraseMode_ = 0;
        options.eraseSelection_ = 0;
        options.eraseDraft_ = {{100, 1, "first"}, {50, 1, "second"}};
        options.page_ = Game::Page::OptionsEraseEntries;
        options.pointerMove(50, 52);
        if (options.eraseSelection_ != 0) return false;
        options.pointerButton(50, 52, false);
        if (options.page_ != Game::Page::OptionsEraseEntries || options.eraseSelection_ != 1) return false;
        options.pointerButton(50, 52, false);
        if (options.page_ != Game::Page::OptionsEraseEntries || options.eraseSelection_ != 0 ||
            options.eraseDraft_.size() != 1 || options.eraseDraft_[0].name != "first") {
            return false;
        }
        options.pointerButton(0, 0, true);
        if (options.page_ != Game::Page::OptionsEraseHall || options.menuSelection_ != 0 ||
            options.scores_[0].size() != 1 || options.scores_[0][0].name != "first") return false;
        return true;
    }

    static bool usesRecoveredContentPointerAndChallengeRules() {
        Game game;
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        game.contentRow_ = 0;
        game.contentColumn_ = 0;

        // The active row is a flag-clear common widget. Home/End and the
        // keypad-style 4/8 previous, 2/6 next aliases move horizontally
        // within that row; the outer custom list reserves Up/Down for rows.
        game.contentRow_ = static_cast<int>(Game::Mode::Factors);
        game.contentColumn_ = 0;
        game.character(L'4');
        if (game.page_ != Game::Page::OptionsContent || game.contentRow_ != 1 ||
            game.contentColumn_ != 2) return false;
        game.character(L'8');
        game.character(L'2');
        game.character(L'6');
        if (game.contentColumn_ != 0) return false;
        game.character(L'5');
        if (game.contentColumn_ != 0) return false;
        game.keyDown(VK_END);
        if (game.contentColumn_ != 2) return false;
        game.keyDown(VK_RIGHT);
        if (game.contentColumn_ != 0) return false;
        game.keyDown(VK_LEFT);
        game.keyDown(VK_HOME);
        if (game.page_ != Game::Page::OptionsContent || game.contentRow_ != 1 ||
            game.contentColumn_ != 0) return false;

        game.contentRow_ = static_cast<int>(Game::Mode::Challenge);
        game.contentColumn_ = 0;
        game.keyDown(VK_END);
        game.keyDown(VK_LEFT);
        game.character(L'6');
        if (game.page_ != Game::Page::OptionsContent || game.contentRow_ != 5 ||
            game.contentColumn_ != 0) return false;

        // Modify Other's four-operation chooser is the other Number
        // flag-clear list. Navigation never toggles an operation or accepts.
        game.contentRow_ = static_cast<int>(Game::Mode::Equality);
        game.contentColumn_ = 3;
        game.openContentOther();
        const auto operationsBeforeNavigation = game.contentDraft_[3].operations;
        game.keyDown(VK_LEFT);
        if (game.page_ != Game::Page::OptionsContentOperations ||
            game.contentOperationSelection_ != 3) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_END);
        if (game.contentOperationSelection_ != 3) return false;
        game.keyDown(VK_HOME);
        game.character(L'4');
        game.character(L'8');
        game.character(L'2');
        game.character(L'6');
        if (game.page_ != Game::Page::OptionsContentOperations ||
            game.contentOperationSelection_ != 0 ||
            game.contentDraft_[3].operations != operationsBeforeNavigation) return false;
        game.character(L'&');
        if (game.contentOperationSelection_ != 0) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::OptionsContent) return false;

        game.contentRow_ = 0;
        game.contentColumn_ = 0;

        // DS:19a6 places row 0 at baseline 60 and row 1 at 76. Ordinary
        // motion does not select, and x=124/y=69 are inter-item gaps.
        game.pointerMove(125, 76);
        game.pointerButton(124, 60, false);
        game.pointerButton(125, 69, false);
        if (game.page_ != Game::Page::OptionsContent || game.contentRow_ != 0 ||
            game.contentColumn_ != 0) return false;

        // Exercise both inclusive corners and the one-past-right exclusion
        // for every valid default-preset field, using the exact padded DOS
        // strings regenerated by image 0x15961.
        std::array<std::array<std::string, 4>, 6> fieldText{};
        fieldText[0] = {" yes ", "   2 - 20  ", "  random  ", " up to 50 "};
        fieldText[1] = {" yes ", "   3 - 99  ", "  random  ", ""};
        fieldText[2] = {" yes ", "   2 - 199 ", "", ""};
        fieldText[3] = {" yes ", "   1 - 50  ", "  random  ", ""};
        fieldText[4] = {" yes ", "   1 - 50  ", "  random  ", ""};
        fieldText[5] = {" yes ", "", "", ""};
        std::string operationGlyphs(9, ' ');
        for (int operation = 0; operation < 4; ++operation) {
            operationGlyphs[static_cast<std::size_t>(1 + operation * 2)] =
                static_cast<char>(2 + operation * 2);
        }
        fieldText[3][3] = operationGlyphs;
        fieldText[4][3] = operationGlyphs;
        static constexpr std::array<int, 4> fieldLefts = {93, 125, 193, 257};
        Renderer textMeasure;
        const auto selectElsewhere = [&game](int row, int column) {
            if (row == 0 && column == 0) {
                game.contentRow_ = 0;
                game.contentColumn_ = 1;
            } else {
                game.contentRow_ = 0;
                game.contentColumn_ = 0;
            }
            game.page_ = Game::Page::OptionsContent;
        };
        for (int row = 0; row < 6; ++row) {
            const int baseline = 60 + row * 16;
            for (int column = 0; column < 4; ++column) {
                if (!game.contentFieldValid(row, column)) continue;
                const int left = fieldLefts[static_cast<std::size_t>(column)];
                const int right = left + textMeasure.textWidth(
                    game.assets_.smallFont(), fieldText[static_cast<std::size_t>(row)]
                                                       [static_cast<std::size_t>(column)]);
                selectElsewhere(row, column);
                const int oldRow = game.contentRow_;
                const int oldColumn = game.contentColumn_;
                game.pointerButton(right + 1, baseline, false);
                if (game.contentRow_ != oldRow || game.contentColumn_ != oldColumn) return false;
                game.pointerButton(left, baseline - 1, false);
                if (game.contentRow_ != row || game.contentColumn_ != column) return false;
                selectElsewhere(row, column);
                game.pointerButton(right, baseline + 8, false);
                if (game.contentRow_ != row || game.contentColumn_ != column) return false;
            }
        }

        // A first exact hit selects another field; repeating it follows the
        // recovered Space-equivalent activation branch.
        game.contentRow_ = 0;
        game.contentColumn_ = 0;
        game.pointerButton(125, 76, false);
        if (game.page_ != Game::Page::OptionsContent || game.contentRow_ != 1 ||
            game.contentColumn_ != 1) return false;
        game.pointerButton(125, 76, false);
        if (game.page_ != Game::Page::OptionsContentRange || game.contentRow_ != 1) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::OptionsContent) return false;

        // Challenge's Use field is a real row-5 item. With multiple component
        // games, its selected value toggles yes/no like the other Use fields.
        game.pointerButton(93, 140, false);
        if (game.contentRow_ != static_cast<int>(Game::Mode::Challenge) ||
            game.contentColumn_ != 0) return false;
        game.pointerButton(93, 140, false);
        if (game.contentDraft_[static_cast<std::size_t>(Game::Mode::Challenge)].use) return false;

        // The Play selector walks all six enable bytes, so disabled Challenge
        // disappears rather than being injected unconditionally.
        game.keyDown(VK_RETURN);
        const std::vector<Game::Mode> withoutChallenge = game.enabledPlayModes();
        if (game.page_ != Game::Page::Options ||
            withoutChallenge != std::vector<Game::Mode>{
                Game::Mode::Multiples, Game::Mode::Factors, Game::Mode::Primes,
                Game::Mode::Equality, Game::Mode::Inequality}) return false;

        // On commit with exactly one component, 0x14c68 clears Challenge even
        // when its draft flag was previously enabled.
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        for (int index = 0; index < static_cast<int>(Game::Mode::Challenge); ++index) {
            game.contentDraft_[static_cast<std::size_t>(index)].use =
                index == static_cast<int>(Game::Mode::Equality);
        }
        game.contentDraft_[static_cast<std::size_t>(Game::Mode::Challenge)].use = true;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Options ||
            game.contentSettings_[static_cast<std::size_t>(Game::Mode::Challenge)].use ||
            game.enabledPlayModes() != std::vector<Game::Mode>{Game::Mode::Equality}) return false;

        game.enterModeSelect();
        return game.page_ == Game::Page::Playing && game.mode_ == Game::Mode::Equality;
    }

    static void prepareEnemyWarning(Game& game) {
        game.page_ = Game::Page::Playing;
        game.level_ = 1;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.enemies_.clear();
        game.random_.seed(0x5741524eu);
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Waiting;
        game.enemySlots_[0].timer = 0.0;
        game.safeZoneJobCount_ = 0;
        game.enemyWarning_ = false;
        game.playerRecovering_ = false;
        for (Game::Cell& cell : game.cells_) cell = {};
    }

    static bool isFeedback(const Game& game) { return game.page_ == Game::Page::Feedback; }
    static bool isPlaying(const Game& game) { return game.page_ == Game::Page::Playing; }
    static bool isPaused(const Game& game) { return game.page_ == Game::Page::Paused; }
    static bool isQuitConfirm(const Game& game) { return game.page_ == Game::Page::QuitConfirm; }
    static bool isHall(const Game& game) { return game.page_ == Game::Page::Hall; }
    static bool isPostGameHall(const Game& game) {
        return game.page_ == Game::Page::Hall &&
               game.hallContext_ == Game::HallContext::PostGame;
    }
    static bool isNameEntry(const Game& game) { return game.page_ == Game::Page::NameEntry; }
    static int lives(const Game& game) { return game.lives_; }
    static bool firstCellEaten(const Game& game) { return game.cells_[0].eaten; }
    static bool playerMoving(const Game& game) { return game.moving_; }
    static std::size_t queuedPlayerInputCount(const Game& game) {
        return game.pointerCellQueue_.size();
    }
    static int playerRow(const Game& game) { return game.playerRow_; }
    static int playerColumn(const Game& game) { return game.playerColumn_; }
    static int target(const Game& game) { return game.target_; }
    static int score(const Game& game) { return game.score_; }
    static void setLives(Game& game, int lives) { game.lives_ = lives; }
    static std::string feedbackMessage(const Game& game) { return game.feedbackMessage_; }
    static int correctCellCount(const Game& game) {
        return static_cast<int>(std::count_if(game.cells_.begin(), game.cells_.end(),
                                              [](const Game::Cell& cell) { return cell.correct; }));
    }
    static int safeCellCount(const Game& game) {
        return static_cast<int>(std::count_if(game.cells_.begin(), game.cells_.end(),
                                              [](const Game::Cell& cell) { return cell.safe; }));
    }
    static bool validCapturedMultiplesRange(const Game& game) {
        if (game.target_ < 2 || game.target_ > 20) return false;
        return std::all_of(game.cells_.begin(), game.cells_.end(), [&game](const Game::Cell& cell) {
            if (cell.label.empty()) return cell.eaten;
            const int value = std::stoi(cell.label);
            return value >= 1 && value <= game.target_ * 50 && cell.correct == (value % game.target_ == 0);
        });
    }
    static int populatedCellCount(const Game& game) {
        return static_cast<int>(std::count_if(game.cells_.begin(), game.cells_.end(),
                                              [](const Game::Cell& cell) { return !cell.label.empty(); }));
    }
    static bool playerStartsOnBlank(const Game& game) {
        const Game::Cell& start = game.cell(game.playerRow_, game.playerColumn_);
        return start.eaten && start.label.empty() && !start.correct;
    }
    static bool usesVariableAnswerDensity(Game& game) {
        game.random_.seed(0x4e4d3131u);
        game.mode_ = Game::Mode::Multiples;
        std::array<bool, Game::BoardCellCount> seenCounts{};
        int distinct = 0;
        for (int board = 0; board < 24; ++board) {
            game.generateBoard();
            const int count = correctCellCount(game);
            if (count <= 0 || count >= Game::BoardCellCount - 1 || populatedCellCount(game) != 29 ||
                !playerStartsOnBlank(game) || game.playerRow_ < 1 || game.playerRow_ > 3 ||
                game.playerColumn_ < 1 || game.playerColumn_ > 4) return false;
            if (!seenCounts[count]) {
                seenCounts[count] = true;
                ++distinct;
            }
        }
        return distinct > 1;
    }
    static bool usesSuppliedContentRanges(Game& game) {
        game.random_.seed(0x52414e47u);
        struct ExpectedRange { Game::Mode mode; int minimum; int maximum; };
        constexpr std::array ranges = {
            ExpectedRange{Game::Mode::Multiples, 2, 20},
            ExpectedRange{Game::Mode::Factors, 3, 99},
            ExpectedRange{Game::Mode::Equality, 1, 50},
            ExpectedRange{Game::Mode::Inequality, 1, 50}};
        for (const auto& range : ranges) {
            game.mode_ = range.mode;
            for (int sample = 0; sample < 100; ++sample) {
                game.prepareMode();
                if (game.target_ < range.minimum || game.target_ > range.maximum) return false;
            }
        }
        return game.difficulty_ == 10;
    }
    static bool usesOriginalDifficultyPresets(Game& game) {
        struct ExpectedPreset {
            bool use;
            int minimum;
            int maximum;
            int sequence;
            int other;
        };
        constexpr std::array<std::array<ExpectedPreset, 6>, 11> expected = {{
            {{{false, 2,  5, 1,  5}, {false, 3,  5, 1, 0}, {false, 2,  25, 0, 0},
              {true,  1, 20, 1, 15}, {true,  1, 10, 1, 3}, {true, 0, 0, 0, 0}}},
            {{{true,  2,  5, 1,  5}, {true,  3, 25, 1, 0}, {false, 2,  25, 0, 0},
              {true,  1, 20, 1, 15}, {true,  1, 20, 1, 3}, {true, 0, 0, 0, 0}}},
            {{{true,  2,  9, 1,  5}, {true,  3, 25, 1, 0}, {false, 2,  50, 0, 0},
              {true,  1, 24, 1, 15}, {true,  1, 20, 1, 3}, {true, 0, 0, 0, 0}}},
            {{{true,  2,  9, 2,  9}, {true,  3, 64, 2, 0}, {false, 2,  50, 0, 0},
              {true,  1, 24, 2, 15}, {true,  1, 20, 2, 7}, {true, 0, 0, 0, 0}}},
            {{{true,  2,  9, 2,  9}, {true,  3, 64, 2, 0}, {false, 2,  50, 0, 0},
              {true,  1, 24, 2, 15}, {true,  1, 24, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 11, 2, 13}, {true,  3, 81, 2, 0}, {true,  2,  50, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 24, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 11, 2, 13}, {true,  3, 81, 2, 0}, {true,  2,  50, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 50, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 12, 2, 13}, {true,  3, 99, 2, 0}, {true,  2,  99, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 50, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 12, 2, 18}, {true,  3, 99, 2, 0}, {true,  2,  99, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 50, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 20, 2, 20}, {true,  3, 99, 2, 0}, {true,  2, 199, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 50, 2, 15}, {true, 0, 0, 0, 0}}},
            {{{true,  2, 20, 2, 50}, {true,  3, 99, 2, 0}, {true,  2, 199, 0, 0},
              {true,  1, 50, 2, 15}, {true,  1, 50, 2, 15}, {true, 0, 0, 0, 0}}},
        }};
        for (int difficulty = 0; difficulty < static_cast<int>(expected.size()); ++difficulty) {
            game.applyDifficultyPreset(difficulty);
            if (game.difficulty_ != difficulty || game.contentDraft_ != game.contentSettings_) return false;
            for (int mode = 0; mode < static_cast<int>(Game::Mode::Count); ++mode) {
                const ExpectedPreset& source = expected[static_cast<std::size_t>(difficulty)]
                                                       [static_cast<std::size_t>(mode)];
                const Game::ContentSettings& settings = game.contentSettings_[static_cast<std::size_t>(mode)];
                if (settings.use != source.use || settings.minimum != source.minimum ||
                    settings.maximum != source.maximum ||
                    settings.randomSequence != (source.sequence != 1) ||
                    game.contentSequenceNext_[static_cast<std::size_t>(mode)] != source.minimum) return false;
                if (mode == static_cast<int>(Game::Mode::Multiples) &&
                    settings.maxMultiplier != source.other) return false;
                if (mode == static_cast<int>(Game::Mode::Equality) ||
                    mode == static_cast<int>(Game::Mode::Inequality)) {
                    for (int operation = 0; operation < 4; ++operation) {
                        if (settings.operations[static_cast<std::size_t>(operation)] !=
                            ((source.other & (1 << operation)) != 0)) return false;
                    }
                }
            }
        }
        game.applyDifficultyPreset(0);
        const std::vector<Game::Mode> easyModes = game.enabledPlayModes();
        if (easyModes != std::vector<Game::Mode>{Game::Mode::Equality, Game::Mode::Inequality,
                                                 Game::Mode::Challenge} ||
            !game.contentFieldValid(static_cast<int>(Game::Mode::Challenge), 0)) return false;
        game.random_.seed(0x4348414cu);
        game.mode_ = Game::Mode::Challenge;
        for (int sample = 0; sample < 100; ++sample) {
            game.prepareMode();
            if (game.activeBoardMode_ != Game::Mode::Equality &&
                game.activeBoardMode_ != Game::Mode::Inequality) return false;
        }
        game.applyDifficultyPreset(10);
        return true;
    }
    static bool persistsOptionsAndContent(Game& game) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("number-munchers-native-settings-" + std::to_string(GetCurrentProcessId()) + ".txt");
        const std::filesystem::path cancelPath = std::filesystem::temp_directory_path() /
            ("number-munchers-password-cancel-" + std::to_string(GetCurrentProcessId()) + ".txt");
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(cancelPath, error);

        // Number's byte-isomorphic password routine leaves its accepted result
        // set when Escape cancels the hint. The caller therefore rewrites the
        // restored strings; Escape from the first field performs no write.
        Game passwordCancel;
        passwordCancel.scorePersistenceEnabled_ = false;
        passwordCancel.settingsPathOverride_ = cancelPath.wstring();
        passwordCancel.settingsPersistenceEnabled_ = true;
        passwordCancel.optionPassword_ = "Munch";
        passwordCancel.optionHint_ = "five letters";
        passwordCancel.passwordDraft_ = "Other";
        passwordCancel.hintDraft_ = "changed hint";
        passwordCancel.page_ = Game::Page::OptionsPassword;
        passwordCancel.passwordEditingHint_ = false;
        passwordCancel.keyDown(VK_ESCAPE);
        if (std::filesystem::exists(cancelPath)) return false;

        passwordCancel.page_ = Game::Page::OptionsPassword;
        passwordCancel.passwordEditingHint_ = true;
        passwordCancel.keyDown(VK_ESCAPE);
        if (!std::filesystem::exists(cancelPath) ||
            passwordCancel.optionPassword_ != "Munch" ||
            passwordCancel.optionHint_ != "five letters") {
            std::filesystem::remove(cancelPath, error);
            return false;
        }
        Game restoredCancel;
        restoredCancel.scorePersistenceEnabled_ = false;
        restoredCancel.settingsPathOverride_ = cancelPath.wstring();
        restoredCancel.settingsPersistenceEnabled_ = true;
        restoredCancel.loadSettings();
        if (restoredCancel.optionPassword_ != "Munch" ||
            restoredCancel.optionHint_ != "five letters") {
            std::filesystem::remove(cancelPath, error);
            return false;
        }
        std::filesystem::remove(cancelPath, error);

        // An existing directory cannot be opened as the native settings file.
        // This forces the recovered 0x0F074 -> 0x07A91 alert without opening a
        // game window, emulator, or output device.
        Game writeFailure;
        writeFailure.scorePersistenceEnabled_ = false;
        writeFailure.settingsPathOverride_ = std::filesystem::temp_directory_path().wstring();
        writeFailure.settingsPersistenceEnabled_ = true;
        writeFailure.optionPassword_ = "Munch";
        writeFailure.optionHint_ = "five letters";
        writeFailure.passwordDraft_ = "Other";
        writeFailure.hintDraft_ = "changed hint";
        writeFailure.page_ = Game::Page::OptionsPassword;
        writeFailure.passwordEditingHint_ = true;
        writeFailure.keyDown(VK_ESCAPE);
        if (!writeFailure.configurationWriteError_ ||
            writeFailure.configurationWriteErrorBackgroundPage_ !=
                Game::Page::OptionsPassword ||
            writeFailure.page_ != Game::Page::Options) {
            return false;
        }
        const std::uint64_t writeFailureHash = fullFrameHash(writeFailure);
        constexpr std::uint64_t ExpectedWriteFailureHash = 0xebef0f53026e2fc5ull;
        if (writeFailureHash != ExpectedWriteFailureHash) {
            std::cerr << "number configuration-write alert frame hash=0x" << std::hex
                      << writeFailureHash << std::dec << '\n';
            return false;
        }
        writeFailure.pointerButton(0, 0, false);
        writeFailure.toggleCheatMenu();
        if (!writeFailure.configurationWriteError_ || writeFailure.cheatOpen_) return false;
        writeFailure.keyDown('Q');
        if (!writeFailure.configurationWriteError_) return false;
        writeFailure.character(L'q');
        if (writeFailure.configurationWriteError_ ||
            writeFailure.page_ != Game::Page::Options) {
            return false;
        }

        Game cgaWriteFailure(GraphicsMode::Cga4);
        cgaWriteFailure.scorePersistenceEnabled_ = false;
        cgaWriteFailure.settingsPathOverride_ =
            std::filesystem::temp_directory_path().wstring();
        cgaWriteFailure.settingsPersistenceEnabled_ = true;
        cgaWriteFailure.page_ = Game::Page::Options;
        cgaWriteFailure.menuSelection_ = 4;
        cgaWriteFailure.keyDown(VK_RETURN);
        if (!cgaWriteFailure.configurationWriteError_ || !cgaWriteFailure.joystickOn_) {
            return false;
        }
        const std::uint64_t cgaWriteFailureHash = fullFrameHash(cgaWriteFailure);
        constexpr std::uint64_t ExpectedCgaWriteFailureHash = 0x79a0c1c085714f2bull;
        if (cgaWriteFailureHash != ExpectedCgaWriteFailureHash) {
            std::cerr << "number CGA configuration-write alert frame hash=0x" << std::hex
                      << cgaWriteFailureHash << std::dec << '\n';
            return false;
        }
        cgaWriteFailure.keyDown(VK_F2);
        if (cgaWriteFailure.configurationWriteError_) return false;

        Game hallWriteFailure;
        hallWriteFailure.settingsPersistenceEnabled_ = false;
        hallWriteFailure.scorePathOverride_ =
            std::filesystem::temp_directory_path().wstring();
        hallWriteFailure.scorePersistenceEnabled_ = true;
        hallWriteFailure.page_ = Game::Page::OptionsEraseEntries;
        hallWriteFailure.eraseMode_ = 0;
        hallWriteFailure.eraseDraft_ = {{235, 1, "sdfg"}};
        hallWriteFailure.keyDown(VK_RETURN);
        if (!hallWriteFailure.configurationWriteError_ ||
            hallWriteFailure.configurationWriteErrorBackgroundPage_ !=
                Game::Page::OptionsEraseEntries ||
            hallWriteFailure.page_ != Game::Page::OptionsEraseHall) {
            return false;
        }
        hallWriteFailure.keyDown(VK_ESCAPE);
        if (hallWriteFailure.configurationWriteError_ ||
            hallWriteFailure.page_ != Game::Page::OptionsEraseHall) {
            return false;
        }

        game.settingsPathOverride_ = path.wstring();
        game.settingsPersistenceEnabled_ = true;
        game.applyDifficultyPreset(3);
        game.soundOn_ = false;
        game.musicOn_ = false;
        game.joystickOn_ = true;
        game.joystickCalibrated_ = true;
        game.joystickLeftThreshold_ = 600;
        game.joystickRightThreshold_ = 1400;
        game.joystickUpThreshold_ = 1200;
        game.joystickDownThreshold_ = 2800;
        game.speakerEffects_ = true;
        game.optionPassword_ = "abcdefghij";
        game.optionHint_ = std::string(50, 'h');
        Game::ContentSettings& multiples = game.contentSettings_[0];
        multiples.minimum = 4;
        multiples.maximum = 8;
        multiples.randomSequence = true;
        multiples.maxMultiplier = 17;
        game.contentSettings_[3].operations = {true, false, true, false};
        game.contentDraft_ = game.contentSettings_;
        game.saveSettings();

        Game loaded;
        loaded.settingsPathOverride_ = path.wstring();
        loaded.settingsPersistenceEnabled_ = true;
        loaded.loadSettings();
        const bool matches = loaded.difficulty_ == 3 && !loaded.soundOn_ && !loaded.musicOn_ &&
            loaded.joystickOn_ && loaded.joystickCalibrated_ && loaded.speakerEffects_ &&
            loaded.joystickLeftThreshold_ == 600 && loaded.joystickRightThreshold_ == 1400 &&
            loaded.joystickUpThreshold_ == 1200 && loaded.joystickDownThreshold_ == 2800 &&
            loaded.optionPassword_ == "abcdefghij" && loaded.optionHint_ == std::string(50, 'h') &&
            loaded.contentSettings_ == game.contentSettings_ &&
            loaded.contentDraft_ == loaded.contentSettings_ &&
            loaded.contentSequenceNext_[0] == 4;
        std::filesystem::remove(path, error);
        game.settingsPersistenceEnabled_ = false;
        return matches;
    }
    static bool usesRecoveredJoystickCalibration(Game& game) {
        const auto fail = [](const char* stage) {
            std::cerr << "number joystick regression: " << stage << '\n';
            return false;
        };
        game.settingsPersistenceEnabled_ = false;
        game.page_ = Game::Page::Options;
        game.menuSelection_ = 5;
        game.joystickOn_ = false;
        game.joystickCalibrated_ = false;
        game.setJoystickState(false, 0, 0, 0);

        int promptDumpIndex = 0;
        const auto promptMatches = [&game, &promptDumpIndex](std::string_view prompt) {
            Renderer actual;
            game.render(actual);

            if (const char* directoryText =
                    std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_JOYSTICK_DIR");
                directoryText && *directoryText) {
                const std::filesystem::path directory(directoryText);
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (!error) {
                    std::string number = std::to_string(promptDumpIndex++);
                    number.insert(number.begin(), 2u - std::min<std::size_t>(2u, number.size()), '0');
                    std::ofstream output(
                        directory / ("number-joystick-prompt-" + number + "-native.ppm"),
                        std::ios::binary | std::ios::trunc);
                    if (output) {
                        output << "P6\n" << Renderer::Width << ' ' << Renderer::Height
                               << "\n255\n";
                        for (const std::uint32_t pixel : actual.pixels()) {
                            const char rgb[3] = {
                                static_cast<char>((pixel >> 16) & 0xffu),
                                static_cast<char>((pixel >> 8) & 0xffu),
                                static_cast<char>(pixel & 0xffu),
                            };
                            output.write(rgb, sizeof(rgb));
                        }
                    }
                }
            }

            // Compose the recovered flags-0xc3 page independently of
            // renderOptionsJoystickCalibration().
            Renderer expected;
            const GemFont& font = game.assets_.largeFont();
            expected.clear(Colors::Black);
            expected.drawCenteredText(font, 159, 5, "Calibrate Joystick", Colors::White);
            expected.horizontalLine(2, 317, 17, Colors::Cyan);
            expected.horizontalLine(0, 319, 19, Colors::Cyan);
            expected.horizontalLine(0, 319, 171, Colors::Cyan);
            expected.horizontalLine(2, 317, 173, Colors::Cyan);
            expected.drawText(font, 0, 176, "Press Space Bar to continue.", Colors::White);
            expected.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);
            constexpr int top = 81;
            constexpr int bottom = 118;
            expected.fillRect(0, top, 320, bottom - top + 1, Colors::Black);
            expected.horizontalLine(0, 319, top, Colors::Cyan);
            expected.horizontalLine(2, 317, top + 2, Colors::Cyan);
            expected.horizontalLine(2, 317, bottom - 2, Colors::Cyan);
            expected.horizontalLine(0, 319, bottom, Colors::Cyan);
            expected.verticalLine(0, top, bottom, Colors::Cyan);
            expected.verticalLine(319, top, bottom, Colors::Cyan);
            expected.verticalLine(2, top + 2, bottom - 2, Colors::Cyan);
            expected.verticalLine(317, top + 2, bottom - 2, Colors::Cyan);
            expected.drawCenteredText(font, 159, 95, prompt, Colors::White);
            return actual.pixels() == expected.pixels();
        };

        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsJoystickCalibration ||
            game.joystickCalibrationStep_ != -1 ||
            // FNV-64 of logical frame 850 in the lossless VGA reference AVI
            // (SHA-256 BB3E18E5BB6B84F9B12000C86268FC571D1E45549304F51413ED07378C60DA0D).
            fullFrameHash(game) != 0x761c0d49d5d02fa6ull ||
            !promptMatches("Attach joystick to computer.")) return false;
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != -1) return false;

        game.setJoystickState(true, 0, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != -1) return false;
        game.setJoystickState(false, 0, 0, 0);
        game.setJoystickState(true, 1000, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 0 ||
            !promptMatches("Center your joystick.")) return false;
        game.pointerButton(0, 0, true);
        if (game.joystickCalibrationStep_ != 0) return fail("calibration right release");
        game.pointerButton(0, 0, false);
        if (game.joystickCalibrationStep_ != 1 ||
            !promptMatches("Position your joystick: left")) return false;

        game.setJoystickState(true, 200, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 2 || game.joystickDraftLeftThreshold_ != 600 ||
            !promptMatches("Position your joystick: right")) return false;
        game.setJoystickState(true, 1800, 2000, 0);
        game.keyDown(VK_RETURN);
        if (game.joystickCalibrationStep_ != 3 || game.joystickDraftRightThreshold_ != 1400 ||
            !promptMatches("Position your joystick: up")) return false;
        game.setJoystickState(true, 1000, 400, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 4 || game.joystickDraftUpThreshold_ != 1200 ||
            !promptMatches("Position your joystick: down")) return false;
        game.setJoystickState(true, 1000, 3600, 0);
        game.keyDown(VK_RETURN);
        if (game.joystickCalibrationStep_ != 5 || game.joystickDraftDownThreshold_ != 2800 ||
            !promptMatches("Your joystick is ready for use.") || game.joystickOn_) return false;

        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::Options || game.menuSelection_ != 5 ||
            !game.joystickCalibrated_ || game.joystickOn_ ||
            game.joystickLeftThreshold_ != 600 || game.joystickRightThreshold_ != 1400 ||
            game.joystickUpThreshold_ != 1200 || game.joystickDownThreshold_ != 2800) return false;

        // Escape at any intermediate prompt discards the draft calibration.
        game.setJoystickState(true, 1000, 2000, 0);
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN);
        game.setJoystickState(true, 0, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickDraftLeftThreshold_ == 600) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Options || game.menuSelection_ != 5 ||
            game.joystickLeftThreshold_ != 600 || game.joystickRightThreshold_ != 1400 ||
            game.joystickUpThreshold_ != 1200 || game.joystickDownThreshold_ != 2800) return false;

        const auto prepareJoystick = [](Game& target) {
            target.settingsPersistenceEnabled_ = false;
            target.joystickOn_ = true;
            target.joystickCalibrated_ = true;
            target.joystickLeftThreshold_ = 600;
            target.joystickRightThreshold_ = 1400;
            target.joystickUpThreshold_ = 1200;
            target.joystickDownThreshold_ = 2800;
            target.joystickClockFraction_ = 0.0;
            target.joystickClockTicks_ = 100;
            target.joystickCachedClockTicks_ = 100;
            target.joystickNextRepeatTicks_ = 104;
            target.joystickPendingDirection_ = 0;
            target.joystickLastSampledDirection_ = 0;
            target.setJoystickState(true, 1000, 2000, 0);
            target.updateJoystickInput(0.0);
        };
        for (const auto& [x, y, expectedDirection] : std::array{
                 std::tuple<std::uint32_t, std::uint32_t, int>{1000, 1000, 0},
                 std::tuple<std::uint32_t, std::uint32_t, int>{1500, 2000, 1},
                 std::tuple<std::uint32_t, std::uint32_t, int>{1000, 3000, 2},
                 std::tuple<std::uint32_t, std::uint32_t, int>{500, 2000, 3}}) {
            Game directionGame;
            prepareMove(directionGame);
            prepareJoystick(directionGame);
            directionGame.setJoystickState(true, x, y, 0);
            directionGame.updateJoystickInput(0.0);
            if (directionGame.moving_) return fail("axis dispatched before deadline");
            directionGame.updateJoystickInput(4.1 / OriginalSchedulerTicksPerSecond);
            if (!directionGame.moving_ || directionGame.moveDirection_ != expectedDirection) {
                return fail("queued cardinal direction");
            }
        }

        Game diagonalGame;
        prepareMove(diagonalGame);
        prepareJoystick(diagonalGame);
        diagonalGame.setJoystickState(true, 500, 1000, 0);
        diagonalGame.updateJoystickInput(0.0);
        diagonalGame.updateJoystickInput(4.1 / OriginalSchedulerTicksPerSecond);
        if (diagonalGame.moving_) return fail("diagonal rejection");

        Game repeatGame;
        repeatGame.page_ = Game::Page::Options;
        repeatGame.menuSelection_ = 0;
        prepareJoystick(repeatGame);
        repeatGame.setJoystickState(true, 1000, 1000, 0);
        repeatGame.updateJoystickInput(0.0);
        if (repeatGame.menuSelection_ != 0 ||
            repeatGame.joystickPendingDirection_ != VK_UP) return fail("pending sample");
        repeatGame.updateJoystickInput(3.9 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != 0) return fail("first repeat fired early");
        repeatGame.updateJoystickInput(0.2 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != 5 || repeatGame.joystickPendingDirection_ != 0) {
            return fail("first pending delivery");
        }
        repeatGame.joystickClockFraction_ = 0.0;
        repeatGame.updateJoystickInput(0.0); // queue the still-held axis for the next deadline
        repeatGame.updateJoystickInput(3.9 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != 5) return fail("held repeat fired early");
        repeatGame.updateJoystickInput(0.2 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != 4) return fail("held repeat missed deadline");
        repeatGame.setJoystickState(true, 500, 1000, 0);
        repeatGame.updateJoystickInput(1.0);
        if (repeatGame.menuSelection_ != 4) return fail("diagonal pending clear");

        // The resident selector-100 dispatcher is not running inside any of
        // the synchronous menu/widget/waiter pages. Lock the complete native
        // modal set: neither repeat controls nor Alt toggles may escape into
        // those private loops.
        constexpr std::array<Game::Page, 26> modalPages = {
            Game::Page::StartupVersion, Game::Page::StartupSplash,
            Game::Page::Title, Game::Page::InstructionsQuestion,
            Game::Page::ModeSelect, Game::Page::Information,
            Game::Page::Options, Game::Page::OptionsDifficulty,
            Game::Page::OptionsContent, Game::Page::OptionsContentRange,
            Game::Page::OptionsContentOtherNumber,
            Game::Page::OptionsContentOperations, Game::Page::OptionsContentHelp,
            Game::Page::OptionsContentValidation, Game::Page::OptionsEraseHall,
            Game::Page::OptionsEraseEntries, Game::Page::OptionsEraseAllConfirm,
            Game::Page::OptionsPassword, Game::Page::OptionsPasswordPrompt,
            Game::Page::OptionsJoystickCalibration, Game::Page::HallSelect,
            Game::Page::Hall, Game::Page::QuitConfirm, Game::Page::NameEntry,
            Game::Page::ReplayQuestion, Game::Page::Attract,
        };
        for (const Game::Page page : modalPages) {
            Game modal;
            modal.settingsPersistenceEnabled_ = false;
            modal.page_ = page;
            modal.attractMode_ = false;
            modal.cheatOpen_ = false;
            modal.joystickOn_ = true;
            modal.joystickCalibrated_ = true;
            modal.joystickRepeatTicks_ = 4;
            const bool sound = modal.soundOn_;
            const bool music = modal.musicOn_;
            const bool speaker = modal.speakerEffects_;
            const std::size_t plays = modal.effectPlayer_.playCount();
            if (modal.acceptsCommonDispatcherInput()) return fail("modal dispatcher predicate");
            modal.character(L'+');
            modal.toggleSound();
            modal.toggleMusic();
            modal.toggleSpeaker();
            if (modal.joystickRepeatTicks_ != 4 ||
                modal.effectPlayer_.playCount() != plays ||
                modal.soundOn_ != sound || modal.musicOn_ != music ||
                modal.speakerEffects_ != speaker) return fail("modal shortcut leakage");
        }
        for (const Game::Page page : {Game::Page::Playing, Game::Page::Paused,
                                      Game::Page::Feedback, Game::Page::LevelComplete}) {
            Game live;
            live.page_ = page;
            if (!live.acceptsCommonDispatcherInput()) return fail("live dispatcher predicate");
            live.cheatOpen_ = true;
            if (live.acceptsCommonDispatcherInput()) return fail("cheat dispatcher isolation");
        }

        // '+' and '-' adjust the archived repeat word only while the common
        // dispatcher owns a live selector state and at least one joystick flag
        // is present. The original clamps it to 1..10.
        Game repeatControl;
        repeatControl.settingsPersistenceEnabled_ = false;
        repeatControl.page_ = Game::Page::Playing;
        repeatControl.joystickOn_ = false;
        repeatControl.joystickCalibrated_ = false;
        if (repeatControl.joystickRepeatTicks_ != 4) return false;
        for (const wchar_t key : {L'+', L'=', L'-', L'_'}) repeatControl.character(key);
        if (repeatControl.joystickRepeatTicks_ != 4) return false;
        repeatControl.joystickCalibrated_ = true;
        const std::size_t feedbackStarts = repeatControl.effectPlayer_.playCount();
        repeatControl.character(L'_');
        repeatControl.character(L'=');
        if (repeatControl.joystickRepeatTicks_ != 4 ||
            repeatControl.lastJoystickRepeatFeedbackHz_ != 800 ||
            repeatControl.effectPlayer_.playCount() != feedbackStarts + 2 ||
            repeatControl.effectPlayer_.bufferSize() != 4454) return false;
        for (int expected = 5; expected <= 10; ++expected) {
            repeatControl.character(L'-');
            if (repeatControl.joystickRepeatTicks_ != expected ||
                repeatControl.lastJoystickRepeatFeedbackHz_ != 1000 - 50 * expected ||
                repeatControl.effectPlayer_.bufferSize() != 4454) return false;
        }
        if (repeatControl.joystickRepeatTicks_ != 10) return false;
        const std::size_t upperBoundStarts = repeatControl.effectPlayer_.playCount();
        repeatControl.character(L'_');
        if (repeatControl.joystickRepeatTicks_ != 10 ||
            repeatControl.effectPlayer_.playCount() != upperBoundStarts) return false;
        for (int expected = 9; expected >= 1; --expected) {
            repeatControl.character(L'+');
            if (repeatControl.joystickRepeatTicks_ != expected ||
                repeatControl.lastJoystickRepeatFeedbackHz_ != 1000 - 50 * expected ||
                repeatControl.effectPlayer_.bufferSize() != 4454) return false;
        }
        if (repeatControl.joystickRepeatTicks_ != 1) return false;
        const std::size_t lowerBoundStarts = repeatControl.effectPlayer_.playCount();
        repeatControl.character(L'=');
        if (repeatControl.joystickRepeatTicks_ != 1 ||
            repeatControl.effectPlayer_.playCount() != lowerBoundStarts) return false;
        repeatControl.joystickCalibrated_ = false;
        repeatControl.joystickOn_ = true;
        repeatControl.character(L'-');
        if (repeatControl.joystickRepeatTicks_ != 2) return false;

        // Selector states 1..3 process repeat control first, then stop Demo
        // with that same translated event. The tone must survive teardown.
        Game demoRepeat;
        demoRepeat.settingsPersistenceEnabled_ = false;
        demoRepeat.startAttract();
        demoRepeat.joystickOn_ = true;
        demoRepeat.joystickCalibrated_ = false;
        demoRepeat.joystickRepeatTicks_ = 4;
        demoRepeat.keyDown(VK_OEM_PLUS);
        if (!demoRepeat.attractMode_ || demoRepeat.joystickRepeatTicks_ != 4) {
            return fail("Demo translated repeat deferral");
        }
        demoRepeat.character(L'+');
        if (demoRepeat.attractMode_ || demoRepeat.page_ != Game::Page::Title ||
            demoRepeat.joystickRepeatTicks_ != 3 ||
            demoRepeat.lastJoystickRepeatFeedbackHz_ != 850 ||
            !demoRepeat.effectPlayer_.playing() ||
            demoRepeat.effectPlayer_.bufferSize() != 4454) {
            return fail("Demo repeat-before-exit ordering");
        }

        repeatGame.joystickRepeatTicks_ = 10;
        repeatGame.joystickClockFraction_ = 0.0;
        repeatGame.joystickCachedClockTicks_ = repeatGame.joystickClockTicks_;
        repeatGame.joystickNextRepeatTicks_ = repeatGame.joystickClockTicks_ + 10;
        repeatGame.joystickPendingDirection_ = 0;
        repeatGame.joystickLastSampledDirection_ = 0;
        repeatGame.setJoystickState(true, 1000, 2000, 0);
        repeatGame.updateJoystickInput(0.0);
        repeatGame.setJoystickState(true, 1000, 1000, 0);
        repeatGame.updateJoystickInput(0.0);
        const int slowFirst = repeatGame.menuSelection_;
        repeatGame.updateJoystickInput(9.9 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != slowFirst) return fail("slow repeat fired early");
        repeatGame.updateJoystickInput(0.2 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ == slowFirst) return fail("slow repeat missed deadline");

        repeatGame.joystickRepeatTicks_ = 1;
        repeatGame.joystickClockFraction_ = 0.0;
        repeatGame.joystickCachedClockTicks_ = repeatGame.joystickClockTicks_;
        repeatGame.joystickNextRepeatTicks_ = repeatGame.joystickClockTicks_ + 1;
        repeatGame.joystickPendingDirection_ = 0;
        repeatGame.joystickLastSampledDirection_ = 0;
        repeatGame.setJoystickState(true, 1000, 2000, 0);
        repeatGame.updateJoystickInput(0.0);
        repeatGame.setJoystickState(true, 1000, 1000, 0);
        repeatGame.updateJoystickInput(0.0);
        const int fastFirst = repeatGame.menuSelection_;
        repeatGame.updateJoystickInput(0.9 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != fastFirst) return fail("fast repeat fired early");
        repeatGame.updateJoystickInput(0.2 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ == fastFirst) return fail("fast repeat missed deadline");

        // A late host frame may dispatch only one repeat. The recovered
        // routine reads the public clock again and schedules a complete new
        // interval from that instant instead of preserving the overrun.
        const int beforeOverrun = repeatGame.menuSelection_;
        repeatGame.updateJoystickInput(0.1);
        if (repeatGame.menuSelection_ == beforeOverrun ||
            repeatGame.joystickNextRepeatTicks_ !=
                repeatGame.joystickCachedClockTicks_ + 1) return fail("overdue reset");
        const int afterOverrun = repeatGame.menuSelection_;
        repeatGame.joystickClockFraction_ = 0.0;
        repeatGame.updateJoystickInput(0.0);
        repeatGame.updateJoystickInput(0.9 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ != afterOverrun) return fail("overdue follow-on early");
        repeatGame.updateJoystickInput(0.2 / OriginalSchedulerTicksPerSecond);
        if (repeatGame.menuSelection_ == afterOverrun) return fail("overdue follow-on late");

        // The player movement callback clears the original directional gate.
        // Its public-clock deadline can expire while the actor is busy, but it
        // must not be rolled forward until the terminal callback re-enables
        // direction dispatch.
        Game movementGate;
        prepareMove(movementGate);
        prepareJoystick(movementGate);
        movementGate.setJoystickState(true, 1500, 2000, 0);
        movementGate.joystickRepeatTicks_ = 4;
        movementGate.joystickCachedClockTicks_ = movementGate.joystickClockTicks_;
        movementGate.joystickNextRepeatTicks_ = movementGate.joystickClockTicks_ + 1;
        movementGate.joystickPendingDirection_ = VK_RIGHT;
        movementGate.joystickLastSampledDirection_ = VK_RIGHT;
        movementGate.moving_ = true;
        const std::uint64_t gatedCachedClock = movementGate.joystickCachedClockTicks_;
        const std::uint64_t gatedDeadline = movementGate.joystickNextRepeatTicks_;
        movementGate.updateJoystickInput(2.0 / OriginalSchedulerTicksPerSecond);
        if (!movementGate.moving_ ||
            movementGate.joystickCachedClockTicks_ != gatedCachedClock ||
            movementGate.joystickNextRepeatTicks_ != gatedDeadline ||
            movementGate.joystickPendingDirection_ != VK_RIGHT) return fail("movement gate hold");
        movementGate.moving_ = false;
        const int movementColumn = movementGate.playerColumn_;
        movementGate.updateJoystickInput(0.0);
        if (!movementGate.moving_ ||
            movementGate.moveToColumn_ != movementColumn + 1 ||
            movementGate.joystickNextRepeatTicks_ !=
                movementGate.joystickCachedClockTicks_ + 4 ||
            movementGate.joystickPendingDirection_ != 0) return fail("movement terminal delivery");

        Game buttonGame;
        prepareMove(buttonGame);
        prepareJoystick(buttonGame);
        buttonGame.setJoystickState(true, 1000, 2000, 0x02);
        buttonGame.update(0.001);
        if (buttonGame.page_ != Game::Page::Paused) return false;
        buttonGame.update(0.001);
        if (buttonGame.page_ != Game::Page::Paused) return false;
        buttonGame.setJoystickState(true, 1000, 2000, 0);
        buttonGame.update(0.001);
        if (buttonGame.page_ != Game::Page::Paused) return false;
        buttonGame.setJoystickState(true, 1000, 2000, 0x02);
        buttonGame.update(0.001);
        if (buttonGame.page_ != Game::Page::Playing) return false;

        Game priorityGame;
        prepareCorrectMultiple(priorityGame);
        prepareJoystick(priorityGame);
        priorityGame.setJoystickState(true, 1000, 2000, 0x03);
        priorityGame.update(0.001);
        if (!priorityGame.munching_ || priorityGame.page_ != Game::Page::Playing) return false;

        Game releaseGame;
        releaseGame.page_ = Game::Page::Options;
        releaseGame.menuSelection_ = 0;
        prepareJoystick(releaseGame);
        releaseGame.setJoystickState(true, 1000, 1000, 0x01);
        releaseGame.update(0.001);
        releaseGame.setJoystickState(true, 1000, 1000, 0);
        releaseGame.update(0.001);
        if (releaseGame.menuSelection_ != 0) return false;
        releaseGame.updateJoystickInput(4.0 / OriginalSchedulerTicksPerSecond);
        if (releaseGame.menuSelection_ != 5) return false;

        releaseGame.joystickOn_ = false;
        releaseGame.menuSelection_ = 0;
        releaseGame.setJoystickState(true, 1000, 3000, 0);
        releaseGame.update(1.0);
        if (releaseGame.menuSelection_ != 0) return false;
        releaseGame.joystickOn_ = true;
        releaseGame.setJoystickState(false, 0, 0, 0);
        releaseGame.update(1.0);
        return releaseGame.menuSelection_ == 0;
    }
    static bool enforcesRecoveredOptionsPassword(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.page_ = Game::Page::Title;
        game.menuSelection_ = 3;
        game.optionPassword_ = "munch";
        game.optionHint_ = "five letters";
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsPasswordPrompt) return false;

        for (const char character : std::string("wrong")) game.character(character);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsPasswordPrompt || !game.passwordRejected_ ||
            !game.passwordAttempt_.empty()) return false;

        // DS:0ff2 is rendered by the same captured-calibrated flags-0xc3
        // modal primitive as Set Content validation. Lock its exact recovered
        // composition independently of renderOptionsPasswordPrompt().
        Renderer actualRejected;
        game.render(actualRejected);
        Game expectedBase;
        expectedBase.settingsPersistenceEnabled_ = false;
        expectedBase.page_ = Game::Page::OptionsPasswordPrompt;
        expectedBase.optionPassword_ = "munch";
        expectedBase.optionHint_ = "five letters";
        Renderer expectedRejected;
        expectedBase.render(expectedRejected);
        constexpr int modalTop = 67;
        constexpr int modalBottom = 131;
        expectedRejected.fillRect(0, modalTop, 320, modalBottom - modalTop + 1, Colors::Black);
        expectedRejected.horizontalLine(0, 319, modalTop, Colors::Cyan);
        expectedRejected.horizontalLine(2, 317, modalTop + 2, Colors::Cyan);
        expectedRejected.horizontalLine(2, 317, modalBottom - 2, Colors::Cyan);
        expectedRejected.horizontalLine(0, 319, modalBottom, Colors::Cyan);
        expectedRejected.verticalLine(0, modalTop, modalBottom, Colors::Cyan);
        expectedRejected.verticalLine(319, modalTop, modalBottom, Colors::Cyan);
        expectedRejected.verticalLine(2, modalTop + 2, modalBottom - 2, Colors::Cyan);
        expectedRejected.verticalLine(317, modalTop + 2, modalBottom - 2, Colors::Cyan);
        expectedRejected.drawCenteredText(expectedBase.assets_.largeFont(), 159, 81,
                                          "That password is not correct.", Colors::White);
        expectedRejected.drawCenteredText(expectedBase.assets_.largeFont(), 159, 99,
                                          "Please try again.", Colors::White);
        if (actualRejected.pixels() != expectedRejected.pixels()) return false;

        // The nonzero modal flag reaches pAccept with exact DS:0864
        // {Escape, Enter, Space, NUL}. Unsupported physical key pairs and
        // arrows leave the rejection visible; right release retains 0xFD.
        game.keyDown('X');
        if (!game.passwordRejected_) return false;
        game.character('x');
        game.keyDown(VK_RIGHT);
        game.pointerButton(0, 0, true);
        if (!game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        // Left release becomes Space and is consumed by the modal.
        game.pointerButton(0, 0, false);
        if (game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        for (const char character : std::string("wrong")) game.character(character);
        game.keyDown(VK_RETURN);
        if (!game.passwordRejected_) return false;
        game.keyDown(VK_RETURN);
        if (game.passwordRejected_ || game.page_ != Game::Page::OptionsPasswordPrompt ||
            !game.passwordAttempt_.empty()) return false;
        game.character(L'\r');
        if (!game.passwordAttempt_.empty()) return false;

        game.character('z');
        game.character('y');
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            game.keyDown(ignored);
            if (game.passwordAttempt_ != "zy") return false;
        }
        game.keyDown(VK_LEFT);
        if (game.passwordAttempt_ != "z") return false;
        game.character(L'\b');
        if (!game.passwordAttempt_.empty()) return false;
        game.character('x');
        game.keyDown(VK_HOME);
        if (!game.passwordAttempt_.empty()) return false;

        // The original comparison at 0xec38 is case-insensitive, but its final
        // iteration compares the stored terminating NUL. A trailing candidate
        // byte must fail rather than being accepted as a matching prefix.
        for (const char character : std::string("MUNCHER")) game.character(character);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsPasswordPrompt || !game.passwordRejected_ ||
            !game.passwordAttempt_.empty()) return false;
        game.keyDown('Q');
        if (!game.passwordRejected_) return false;
        game.character('q');
        if (!game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        game.keyDown(VK_ESCAPE);
        if (game.passwordRejected_ || game.page_ != Game::Page::OptionsPasswordPrompt) return false;

        // DS:0F74 classifies ! through ~ as legal password characters; Space
        // is excluded, while the comparison remains case-insensitive.
        game.character(' ');
        if (!game.passwordAttempt_.empty()) return false;
        for (const char character : std::string("MUNCH")) game.character(character);
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Options) return false;

        game.menuSelection_ = 3;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsPassword ||
            game.passwordDraft_ != "munch" || game.hintDraft_ != "five letters") return false;
        for (int index = 0; index < 5; ++index) game.character(L'\b');
        for (const char character : std::string("other")) game.character(character);
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Options || game.optionPassword_ != "munch" ||
            game.optionHint_ != "five letters") return false;

        game.menuSelection_ = 3;
        game.keyDown(VK_RETURN);
        for (int index = 0; index < 5; ++index) game.character(L'\b');
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Options || !game.optionPassword_.empty() ||
            !game.optionHint_.empty()) return false;

        Game limits;
        limits.settingsPersistenceEnabled_ = false;
        limits.page_ = Game::Page::OptionsPassword;
        limits.passwordEditingHint_ = false;
        limits.character(' ');
        if (!limits.passwordDraft_.empty()) return false;
        for (int index = 0; index < 11; ++index) limits.character('p');
        if (limits.passwordDraft_.size() != 10) return false;
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            limits.keyDown(ignored);
            if (limits.passwordDraft_.size() != 10) return false;
        }
        limits.keyDown(VK_LEFT);
        if (limits.passwordDraft_.size() != 9) return false;
        limits.character('p');
        limits.keyDown(VK_RETURN);
        if (!limits.passwordEditingHint_) return false;
        limits.character(' ');
        if (limits.hintDraft_ != " ") return false;
        limits.keyDown(VK_HOME);
        if (!limits.hintDraft_.empty()) return false;
        for (int index = 0; index < 51; ++index) limits.character('h');
        return limits.hintDraft_.size() == 50;
    }
    static std::optional<int> expressionValue(const std::string& label) {
        const std::size_t operatorAt = label.find_first_of("+-x:");
        if (operatorAt == std::string::npos) return std::nullopt;
        const int left = std::stoi(label.substr(0, operatorAt));
        const int right = std::stoi(label.substr(operatorAt + 1));
        switch (label[operatorAt]) {
        case '+': return left + right;
        case '-': return left - right;
        case 'x': return left * right;
        case ':':
            if (right == 0 || left % right != 0) return std::nullopt;
            return left / right;
        default: return std::nullopt;
        }
    }
    static bool generatesFourOperationEquality(Game& game) {
        game.random_.seed(0x45515541u);
        game.mode_ = Game::Mode::Equality;
        bool sawDivision = false;
        for (int board = 0; board < 12; ++board) {
            game.generateBoard();
            for (const Game::Cell& cell : game.cells_) {
                if (cell.label.empty()) continue;
                const auto result = expressionValue(cell.label);
                if (!result || cell.correct != (*result == game.target_)) return false;
                sawDivision = sawDivision || cell.label.find(':') != std::string::npos;
            }
        }
        return sawDivision;
    }
    static bool generatesExpressionInequality(Game& game) {
        game.random_.seed(0x494e4551u);
        game.mode_ = Game::Mode::Inequality;
        for (int board = 0; board < 16; ++board) {
            game.generateBoard();
            if (game.target_ < 1 || game.target_ > 50 || populatedCellCount(game) != 29 ||
                (game.relation_ == 0 && game.target_ < 2) ||
                (game.target_ == 1 && game.relation_ != 2)) return false;
            for (const Game::Cell& cell : game.cells_) {
                if (cell.label.empty()) continue;
                const auto result = expressionValue(cell.label);
                if (!result) return false;
                const bool matches = game.relation_ == 0 ? *result < game.target_
                                   : game.relation_ == 1 ? *result != game.target_
                                                         : *result > game.target_;
                if (cell.correct != matches) return false;
            }
        }
        return true;
    }
    static bool usesRecoveredDecoyDistributions(Game& game) {
        game.random_.seed(0x4445434fu);
        game.applyDifficultyPreset(10);

        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 5;
        for (int sample = 0; sample < 200; ++sample) {
            const Game::Cell wrong = game.generatedCell(false);
            const int value = std::stoi(wrong.label);
            if (wrong.correct || value < 1 || value > 250 || value % 5 == 0) return false;
        }

        game.activeBoardMode_ = Game::Mode::Factors;
        game.target_ = 63;
        game.random_.seed(1);
        // rand()==346 selects index four. The executable's descending
        // {63,21,9,7,3,1} table therefore yields 3, not ascending-table 21.
        if (game.generatedCell(true).label != "3" || game.random_.calls != 1) return false;

        game.target_ = 12;
        for (int sample = 0; sample < 200; ++sample) {
            const Game::Cell wrong = game.generatedCell(false);
            const int value = std::stoi(wrong.label);
            if (wrong.correct || value <= 1 || value >= 12 || 12 % value == 0) return false;
        }

        constexpr std::array<int, 5> strictOffsets = {1, 2, 3, 4, 10};
        constexpr std::array<int, 6> inclusiveOffsets = {0, 1, 2, 3, 4, 10};
        const auto allowed = [](const auto& offsets, int delta) {
            return std::find(offsets.begin(), offsets.end(), delta) != offsets.end();
        };

        game.activeBoardMode_ = Game::Mode::Equality;
        game.target_ = 20;
        for (int sample = 0; sample < 200; ++sample) {
            const auto value = expressionValue(game.generatedCell(false).label);
            if (!value || !allowed(strictOffsets, std::abs(*value - game.target_))) return false;
        }

        game.activeBoardMode_ = Game::Mode::Inequality;
        for (int relation = 0; relation < 3; ++relation) {
            game.relation_ = relation;
            for (int sample = 0; sample < 100; ++sample) {
                const auto right = expressionValue(game.generatedCell(true).label);
                const auto wrong = expressionValue(game.generatedCell(false).label);
                if (!right || !wrong) return false;
                if (relation == 0 &&
                    (!allowed(strictOffsets, game.target_ - *right) ||
                     !allowed(inclusiveOffsets, *wrong - game.target_))) return false;
                if (relation == 1 &&
                    (!allowed(strictOffsets, std::abs(*right - game.target_)) ||
                     *wrong != game.target_)) return false;
                if (relation == 2 &&
                    (!allowed(strictOffsets, *right - game.target_) ||
                     !allowed(inclusiveOffsets, game.target_ - *wrong))) return false;
            }
        }
        return true;
    }
    static bool usesRecoveredPrimeExpressionAndProgressionRules(Game& game) {
        game.random_.seed(0x5052494du);
        game.applyDifficultyPreset(10);

        // DATA:2 is the exact 46-word prime table. The cursor begins one
        // entry before the lower bound, advances once per board, and caps at
        // the final prime not exceeding the configured maximum.
        game.mode_ = Game::Mode::Primes;
        if (game.primeMaximumIndex_ != -1 || game.primeMaximumIndexLimit_ != 45) return false;
        for (int expected = 0; expected <= 45; ++expected) {
            game.prepareMode();
            if (game.primeMaximumIndex_ != expected) return false;
        }
        game.prepareMode();
        if (game.primeMaximumIndex_ != 45) return false;

        const auto testPrime = [](int value) {
            if (value < 2) return false;
            for (int divisor = 2; divisor * divisor <= value; ++divisor) {
                if (value % divisor == 0) return false;
            }
            return true;
        };
        game.activeBoardMode_ = Game::Mode::Primes;
        game.primeMaximumIndex_ = 2;
        bool sawOne = false;
        bool sawFour = false;
        for (int sample = 0; sample < 100; ++sample) {
            const int value = std::stoi(game.generatedCell(false).label);
            if (value != 1 && value != 4) return false;
            sawOne = sawOne || value == 1;
            sawFour = sawFour || value == 4;
        }
        if (!sawOne || !sawFour) return false;
        game.primeMaximumIndex_ = 20;
        for (int sample = 0; sample < 300; ++sample) {
            const int correct = std::stoi(game.generatedCell(true).label);
            const int wrong = std::stoi(game.generatedCell(false).label);
            if (!testPrime(correct) || correct > 73 || testPrime(wrong) || wrong > 73) return false;
        }

        // 0x0ffd1 caps the freely selected addition/subtraction operand at 9
        // and division at both 9 and a 100-point dividend. Helper 0x10531
        // may then swap commutative addition/multiplication operands.
        game.activeBoardMode_ = Game::Mode::Equality;
        game.target_ = 20;
        auto& operations = game.contentSettings_[static_cast<std::size_t>(Game::Mode::Equality)].operations;
        for (int operation = 0; operation < 4; ++operation) {
            operations = {false, false, false, false};
            operations[static_cast<std::size_t>(operation)] = true;
            for (int sample = 0; sample < 100; ++sample) {
                const std::string label = game.generatedCell(true).label;
                const std::size_t at = label.find_first_of("+-x:");
                if (at == std::string::npos) return false;
                const int left = std::stoi(label.substr(0, at));
                const int right = std::stoi(label.substr(at + 1));
                if (operation == 0 && (label[at] != '+' || std::min(left, right) < 0 ||
                                       std::min(left, right) > 9 || left + right != 20)) return false;
                if (operation == 1 && (label[at] != '-' || right < 0 || right > 9 || left - right != 20)) return false;
                if (operation == 2 && (label[at] != 'x' || left * right != 20)) return false;
                if (operation == 3 && (label[at] != ':' || right < 1 || right > 5 || left > 100 || left / right != 20)) return false;
            }
        }

        // 0x0fa23 advances all keyed streams on every board, even though a
        // single non-Challenge mode is selected for play.
        game.applyDifficultyPreset(0);
        game.mode_ = Game::Mode::Multiples;
        game.prepareMode();
        if (game.contentSequenceNext_[0] != 3 || game.contentSequenceNext_[1] != 4 ||
            game.contentSequenceNext_[3] != 2 || game.contentSequenceNext_[4] != 2) return false;
        game.prepareMode();
        if (game.contentSequenceNext_[0] != 4 || game.contentSequenceNext_[1] != 5 ||
            game.contentSequenceNext_[3] != 3 || game.contentSequenceNext_[4] != 3) return false;

        game.applyDifficultyPreset(10);
        std::array<int, 4> previous = {-1, -1, -1, -1};
        constexpr std::array<int, 4> indices = {0, 1, 3, 4};
        for (int board = 0; board < 50; ++board) {
            game.prepareMode();
            for (std::size_t stream = 0; stream < indices.size(); ++stream) {
                const int value = game.contentPreviousRandom_[static_cast<std::size_t>(indices[stream])];
                if (value == previous[stream]) return false;
                previous[stream] = value;
            }
        }
        return true;
    }
    static std::string capturedInequalityFeedback(Game& game) {
        game.activeBoardMode_ = Game::Mode::Inequality;
        game.target_ = 34;
        game.relation_ = 1;
        Game::Cell cell;
        cell.label = "30+4";
        return game.wrongAnswerMessage(cell);
    }
    static bool usesCapturedFeedbackForms(Game& game) {
        Game::Cell cell;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 16;
        cell.label = "417";
        if (game.wrongAnswerMessage(cell) != "\"417\" is not a multiple of \"16\".") return false;
        game.activeBoardMode_ = Game::Mode::Factors;
        game.target_ = 20;
        cell.label = "3";
        if (game.wrongAnswerMessage(cell) != "\"3\" is not a factor of \"20\".") return false;
        game.activeBoardMode_ = Game::Mode::Primes;
        cell.label = "1";
        if (game.wrongAnswerMessage(cell) != "The number \"1\" is not prime.") return false;
        game.activeBoardMode_ = Game::Mode::Equality;
        cell.label = "3+4";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"3+4=7\"") return false;
        cell.label = "9-2";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"9-2=7\"") return false;
        cell.label = "3x4";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"3x4=12\"") return false;
        cell.label = "76:4";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"76:4=19\"") return false;
        game.activeBoardMode_ = Game::Mode::Inequality;
        cell.label = "76:4";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"76:4=19\"") return false;
        cell.label = "39-7";
        if (game.wrongAnswerMessage(cell) != "Oops!  \"39-7=32\"") return false;

        // Challenge never owns a sixth formatter. The board initializer stores
        // the selected component in activeBoardMode_, and the DOS dispatcher at
        // image 0x105f0 reads only that value.
        game.mode_ = Game::Mode::Challenge;
        game.activeBoardMode_ = Game::Mode::Factors;
        game.target_ = 20;
        cell.label = "3";
        return game.wrongAnswerMessage(cell) == "\"3\" is not a factor of \"20\".";
    }
    static void prepareCapturedInequalityFeedbackFrame(Game& game) {
        game.page_ = Game::Page::Feedback;
        game.activeBoardMode_ = Game::Mode::Inequality;
        game.target_ = 34;
        game.feedbackKind_ = Game::FeedbackKind::WrongAnswer;
        game.feedbackMessage_ = "Oops!  \"30+4=34\"";
        game.deathAnimating_ = false;
        game.attractPostFeedbackBoard_ = false;
    }
    static void prepareCapturedAttractCollisionFeedbackFrame(Game& game) {
        game.page_ = Game::Page::Feedback;
        game.attractMode_ = true;
        game.feedbackKind_ = Game::FeedbackKind::EatenByTroggle;
        game.feedbackEnemyType_ = 0;
        game.feedbackMessage_ = "Oops";
        game.deathAnimating_ = false;
        game.attractPostFeedbackBoard_ = false;
    }
    static bool usesRecoveredWrongAnswerFlow(Game& game) {
        struct Case {
            Game::Mode selectedMode;
            Game::Mode activeMode;
            int target;
            std::string_view label;
            std::string_view expected;
        };
        constexpr std::array<Case, 6> cases = {{
            {Game::Mode::Multiples, Game::Mode::Multiples, 16, "417",
             "\"417\" is not a multiple of \"16\"."},
            {Game::Mode::Factors, Game::Mode::Factors, 20, "3",
             "\"3\" is not a factor of \"20\"."},
            {Game::Mode::Primes, Game::Mode::Primes, 0, "1",
             "The number \"1\" is not prime."},
            {Game::Mode::Equality, Game::Mode::Equality, 7, "3+4",
             "Oops!  \"3+4=7\""},
            {Game::Mode::Inequality, Game::Mode::Inequality, 34, "30+4",
             "Oops!  \"30+4=34\""},
            {Game::Mode::Challenge, Game::Mode::Factors, 20, "3",
             "\"3\" is not a factor of \"20\"."},
        }};

        for (const Case& test : cases) {
            game.page_ = Game::Page::Playing;
            game.attractMode_ = false;
            game.mode_ = test.selectedMode;
            game.activeBoardMode_ = test.activeMode;
            game.target_ = test.target;
            game.level_ = 7;
            game.score_ = 321;
            game.lives_ = 4;
            game.correctRemaining_ = 1;
            game.playerRow_ = 0;
            game.playerColumn_ = 0;
            game.moving_ = false;
            game.munching_ = false;
            game.feedbackKind_ = Game::FeedbackKind::None;
            game.feedbackMessage_.clear();
            game.playerRecovering_ = false;
            game.pointerCellQueue_.clear();
            game.random_.seed(0x46454544u);
            for (Game::Cell& cell : game.cells_) cell = {};
            game.cells_[0].label = std::string(test.label);
            game.cells_[0].correct = false;
            game.cells_[1].label = "preserved";
            game.cells_[1].correct = true;

            game.enemies_.clear();
            game.enemySlotCount_ = 1;
            for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
            game.enemySlots_[0].type = 2;
            game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
            Game::Enemy enemy;
            enemy.row = 4;
            enemy.column = 5;
            enemy.type = 2;
            enemy.slot = 0;
            enemy.moveTimer = 1.25;
            game.enemies_.push_back(enemy);
            game.safeZoneJobCount_ = 1;
            game.safeZoneJobs_[0] = {true, 28, 4.0, 2.5};

            game.keyDown(VK_SPACE);
            if (!game.munching_) return false;
            game.update(0.251);
            constexpr double TickSeconds = 1.0 / OriginalSchedulerTicksPerSecond;
            const double safeTimerAtFeedback = 2.5 - 7.0 * TickSeconds;
            const double enemyTimerAtFeedback = 1.25 - 6.0 * TickSeconds;
            if (game.page_ != Game::Page::Feedback ||
                game.feedbackKind_ != Game::FeedbackKind::WrongAnswer ||
                game.feedbackMessage_ != test.expected || game.lives_ != 3 ||
                game.score_ != 321 || game.correctRemaining_ != 1 ||
                !game.cells_[0].eaten || game.cells_[1].eaten ||
                game.cells_[1].label != "preserved" ||
                std::abs(game.safeZoneJobs_[0].timer - safeTimerAtFeedback) > 1e-12 ||
                std::abs(game.enemies_[0].moveTimer - enemyTimerAtFeedback) > 1e-12) {
                return false;
            }

            const std::uint64_t callsAtFeedback = game.random_.calls;
            game.update(5.0);
            game.keyDown(VK_RIGHT);
            if (game.page_ != Game::Page::Feedback || game.playerRow_ != 0 ||
                game.playerColumn_ != 0 || game.lives_ != 3 ||
                game.random_.calls != callsAtFeedback || game.enemies_.size() != 1 ||
                std::abs(game.enemies_[0].moveTimer - enemyTimerAtFeedback) > 1e-12 ||
                std::abs(game.safeZoneJobs_[0].timer - safeTimerAtFeedback) > 1e-12 ||
                game.cells_[1].eaten) return false;

            game.keyDown(VK_SPACE);
            if (game.page_ != Game::Page::Playing || game.target_ != test.target ||
                game.level_ != 7 || game.score_ != 321 || game.lives_ != 3 ||
                game.feedbackKind_ != Game::FeedbackKind::None ||
                !game.feedbackMessage_.empty() || !game.cells_[0].eaten ||
                game.cells_[1].eaten || game.cells_[1].label != "preserved") return false;
        }
        return true;
    }
    static bool isMoving(const Game& game) { return game.moving_; }
    static int moveDirection(const Game& game) { return game.moveDirection_; }
    static bool isDeathAnimating(const Game& game) { return game.deathAnimating_; }
    static std::size_t enemyCount(const Game& game) { return game.enemies_.size(); }
    static bool enemyWarning(const Game& game) { return game.enemyWarning_; }
    static double enemyWarningRemaining(const Game& game) {
        for (int index = 0; index < game.enemySlotCount_; ++index) {
            const Game::EnemySlot& slot = game.enemySlots_[static_cast<std::size_t>(index)];
            if (slot.phase == Game::EnemySlotPhase::Warning) return slot.timer;
        }
        return -1.0;
    }
    static bool playerRecovering(const Game& game) { return game.playerRecovering_; }
    static void makeScoreQualify(Game& game) {
        game.score_ = 50;
        game.mode_ = Game::Mode::Primes;
        game.scores_[static_cast<std::size_t>(Game::Mode::Primes)].clear();
    }
    static bool spawnsFromAnEdge(Game& game) {
        game.random_.seed(0x54524f47u);
        game.level_ = 1;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.enemies_.clear();
        for (Game::Cell& cell : game.cells_) cell = {};
        game.spawnEnemy();
        if (game.enemies_.size() != 1) return false;
        const Game::Enemy& enemy = game.enemies_.front();
        const double expectedEntryDuration = game.enemyMoveAnimationDuration(enemy.direction) +
            3.0 / OriginalSchedulerTicksPerSecond;
        if (enemy.type != 0 || !enemy.moving ||
            std::abs(enemy.animationTimer - expectedEntryDuration) > 0.000001) return false;
        const bool top = enemy.row == 0 && enemy.fromRow == -1 && enemy.direction == 2;
        const bool right = enemy.column == Game::BoardColumns - 1 &&
                           enemy.fromColumn == Game::BoardColumns && enemy.direction == 3;
        const bool bottom = enemy.row == Game::BoardRows - 1 &&
                            enemy.fromRow == Game::BoardRows && enemy.direction == 0;
        const bool left = enemy.column == 0 && enemy.fromColumn == -1 && enemy.direction == 1;
        return top || right || bottom || left;
    }
    static bool usesSpeciesMovementRules(Game& game) {
        if (Game::steeredEnemyDirection(0, 1, 240, 3, 0) != 1 ||
            Game::steeredEnemyDirection(1, 1, 240, 3, 0) != 0 ||
            Game::steeredEnemyDirection(1, 1, 240, 3, 1) != 2 ||
            Game::steeredEnemyDirection(1, 1, 240, 3, 2) != 1 ||
            Game::steeredEnemyDirection(3, 1, 240, 3, 0) != 0 ||
            Game::steeredEnemyDirection(3, 1, 240, 3, 1) != 2 ||
            Game::steeredEnemyDirection(3, 1, 240, 3, 2) != 1 ||
            Game::steeredEnemyDirection(2, 1, 96, 3, 9) != 1 ||
            Game::steeredEnemyDirection(2, 0, 96, 2, 9) != 0 ||
            Game::steeredEnemyDirection(2, 1, 97, 3, 0) != 2 ||
            Game::steeredEnemyDirection(2, 1, 97, 3, 2) != 0 ||
            Game::steeredEnemyDirection(2, 1, 97, 3, 4) != 3 ||
            Game::steeredEnemyDirection(2, 1, 97, 3, 6) != 1 ||
            Game::steeredEnemyDirection(4, 1, 96, 3, 1) != 3 ||
            Game::steeredEnemyDirection(4, 1, 192, 3, 0) != 3 ||
            Game::steeredEnemyDirection(4, 1, 192, 3, 1) != 1 ||
            Game::steeredEnemyDirection(4, 1, 193, 3, 0) != 1) return false;

        game.random_.seed(0x41495255u);
        game.playerRow_ = 2;
        game.playerColumn_ = 0;
        game.enemies_.clear();
        for (Game::Cell& cell : game.cells_) cell = {};

        Game::Enemy reggie;
        reggie.row = 2;
        reggie.column = 2;
        reggie.type = 0;
        reggie.direction = 1;
        game.moveEnemy(reggie);
        return reggie.moving && reggie.row == 2 && reggie.column == 3 && reggie.direction == 1;
    }
    static bool retriesProtectedTurnsWithoutSyntheticCap(Game& game) {
        // Only bit 16 of the LCG result controls each left/right retry, so its
        // entire future is determined by the low 17-bit state. Exhaust every
        // such state, direction, and reachable zero/one/two-safe-direction
        // mask; the longest real-board chain is 24 draws.
        constexpr std::uint32_t Low17Mask = (1u << 17) - 1u;
        constexpr std::uint32_t Multiplier = 0x015a4e35u;
        int maximumReachableDraws = 0;
        for (int safeMask = 0; safeMask < 16; ++safeMask) {
            int safeDirectionCount = 0;
            for (int direction = 0; direction < 4; ++direction) {
                safeDirectionCount += (safeMask >> direction) & 1;
            }
            if (safeDirectionCount > 2) continue;
            for (std::uint32_t initialState = 0; initialState <= Low17Mask;
                 ++initialState) {
                for (int initialDirection = 0; initialDirection < 4;
                     ++initialDirection) {
                    std::uint32_t state = initialState;
                    int direction = initialDirection;
                    int draws = 0;
                    while ((safeMask & (1 << direction)) != 0) {
                        state = (state * Multiplier + 1u) & Low17Mask;
                        direction = (direction + (((state >> 16) & 1u) == 0 ? 3 : 1)) % 4;
                        ++draws;
                    }
                    maximumReachableDraws = std::max(maximumReachableDraws, draws);
                }
            }
        }
        if (maximumReachableDraws != 24) return false;

        // This deliberately supplies three protected neighbors to expose the
        // loop contract. Normal boards have at most two safe-zone jobs, but
        // Borland state 57606 needs 37 left/right draws to find the sole open
        // direction here; the former native 32-attempt guard stopped early.
        game.page_ = Game::Page::Title;
        for (Game::Cell& current : game.cells_) current = {};
        game.cell(2, 1).safe = true;
        game.cell(2, 3).safe = true;
        game.cell(3, 2).safe = true;
        game.random_.seed(57606u);

        Game::Enemy reggie;
        reggie.row = 2;
        reggie.column = 2;
        reggie.fromRow = 2;
        reggie.fromColumn = 2;
        reggie.type = 0;
        reggie.direction = 3;
        return game.moveEnemy(reggie) && reggie.moving && reggie.row == 1 &&
               reggie.column == 2 && reggie.direction == 0 &&
               game.random_.calls == 37;
    }
    static bool usesLivePlayerCoordinatesForEnemySteering(Game& game) {
        game.page_ = Game::Page::Playing;
        game.level_ = 1;
        game.correctRemaining_ = 10;
        game.playerRow_ = 1;
        game.playerColumn_ = 0;
        game.moving_ = false;
        game.enemies_.clear();
        for (Game::Cell& current : game.cells_) current = {};
        game.cell(0, 0).label = "5";
        game.cell(0, 0).correct = true;
        game.cell(0, 1).label = "10";
        game.cell(0, 1).correct = true;

        Game::Enemy stationaryDistance;
        stationaryDistance.row = 4;
        stationaryDistance.column = 0;
        stationaryDistance.type = 2;
        stationaryDistance.direction = 2;
        game.random_.seed(0x50495845u);
        game.moveEnemy(stationaryDistance);
        if (game.random_.calls != 0) return false; // 90 pixels: flee without a roll.

        Game::Enemy movingDistance;
        movingDistance.row = 4;
        movingDistance.column = 0;
        movingDistance.type = 2;
        movingDistance.direction = 2;
        game.moving_ = true;
        game.moveFromRow_ = 1;
        game.moveFromColumn_ = 0;
        game.moveToRow_ = 1;
        game.moveToColumn_ = 1;
        game.moveDirection_ = 1;
        game.moveTimer_ = Game::MoveAnimationDuration * 0.65; // Phase 2: x = 16.
        game.random_.seed(0x50495845u);
        game.moveEnemy(movingDistance);
        game.moving_ = false;
        return game.random_.calls == 1; // 16 + 90 = 106 pixels: random far branch.
    }
    static bool reggieLeavesBoard(Game& game) {
        game.page_ = Game::Page::Playing;
        game.level_ = 1;
        game.playerRow_ = 2;
        game.playerColumn_ = 3;
        game.correctRemaining_ = 10;
        game.enemySlotCount_ = 3;
        game.safeZoneJobCount_ = 0;
        game.enemies_.clear();
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                Game::EnemySlotPhase::Active;
        }
        for (Game::Cell& cell : game.cells_) cell = {};
        // Keep the board live while exercising edge movement. A synthetic
        // all-blank board would correctly take the newly recovered terminal
        // branch before steering the actor.
        game.cell(0, 0).label = "5";
        game.cell(0, 0).correct = true;
        Game::Enemy reggie;
        reggie.row = 2;
        reggie.column = 0;
        reggie.type = 0;
        reggie.slot = 0;
        reggie.direction = 3;
        game.moveEnemy(reggie);
        if (!reggie.moving || !reggie.exiting || reggie.column != -1) return false;
        game.enemies_.push_back(reggie);

        game.playerColumn_ = Game::BoardColumns - 1;
        Game::Enemy smarty;
        smarty.row = 2;
        smarty.column = 0;
        smarty.type = 4;
        smarty.slot = 1;
        smarty.direction = 3;
        game.moveEnemy(smarty);
        if (!smarty.moving || !smarty.exiting || smarty.column != -1) return false;
        game.enemies_.push_back(smarty);

        game.updateEnemies(game.enemyMoveAnimationDuration(3) + 0.001);
        if (!game.enemies_.empty()) return false;

        // The captured vertical exit retains one terminal actor callback
        // beyond its five visible 6-pixel steps before the slot is rearmed.
        Game::Enemy vertical;
        vertical.row = Game::BoardRows - 1;
        vertical.column = 2;
        vertical.type = 0;
        vertical.slot = 0;
        vertical.direction = 2;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.moveEnemy(vertical);
        const double visibleDuration = game.enemyMoveAnimationDuration(2);
        const double terminalInterval = 3.0 / OriginalSchedulerTicksPerSecond;
        if (!vertical.moving || !vertical.exiting || vertical.row != Game::BoardRows ||
            std::abs(vertical.animationTimer - visibleDuration - terminalInterval) > 0.000001) {
            return false;
        }
        game.enemies_.push_back(vertical);
        game.updateEnemies(visibleDuration + 0.001);
        if (game.enemies_.empty()) return false;
        game.updateEnemies(terminalInterval);
        return game.enemies_.empty();
    }
    static bool preservesEnemyDueJobOrder(Game& game) {
        game.page_ = Game::Page::Playing;
        game.level_ = 10;
        game.playerRow_ = 2;
        game.playerColumn_ = 1;
        game.correctRemaining_ = 10;
        for (Game::Cell& current : game.cells_) current = {};
        game.random_.seed(62853u);
        for (int call = 0; call < 185; ++call) (void)game.random_.next();

        game.enemySlotCount_ = 3;
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].type = 0;
            game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                Game::EnemySlotPhase::Active;
        }
        game.enemySlots_[1].phase = Game::EnemySlotPhase::Waiting;
        game.enemySlots_[1].timer = 0.0;

        Game::Enemy redEndpoint;
        redEndpoint.fromRow = 4;
        redEndpoint.fromColumn = 3;
        redEndpoint.row = 4;
        redEndpoint.column = 4;
        redEndpoint.direction = 1;
        redEndpoint.type = 0;
        redEndpoint.slot = 2;
        redEndpoint.moving = true;
        redEndpoint.animationTimer = 0.0;
        game.enemies_ = {redEndpoint};

        game.updateEnemies(0.0);
        if (game.random_.calls != 188 || game.enemies_.size() != 2 ||
            game.enemySlots_[1].phase != Game::EnemySlotPhase::Warning) {
            std::cerr << "mixed tie calls/enemies/phase=" << game.random_.calls << '/'
                      << game.enemies_.size() << '/'
                      << static_cast<int>(game.enemySlots_[1].phase);
            for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                std::cerr << " p" << slot << '='
                          << static_cast<int>(game.enemySlots_[static_cast<std::size_t>(slot)].phase);
            }
            for (const Game::Enemy& enemy : game.enemies_) {
                std::cerr << " [s" << enemy.slot << " moving=" << enemy.moving
                          << " frozen=" << enemy.overlapFrozen << " timer="
                          << enemy.animationTimer << ']';
            }
            std::cerr << '\n';
            return false;
        }
        const auto survivor = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                           [](const Game::Enemy& enemy) {
                                               return enemy.slot == 2;
                                           });
        const auto returning = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                            [](const Game::Enemy& enemy) {
                                                return enemy.slot == 1;
                                            });
        if (survivor == game.enemies_.end() || returning == game.enemies_.end() ||
            std::abs(survivor->moveTimer * OriginalSchedulerTicksPerSecond - 95.0) >= 0.0001 ||
            !returning->entering || returning->moving || returning->row != 1 ||
            returning->column != 0 || returning->fromRow != 1 ||
            returning->fromColumn != -1 || returning->direction != 1) {
            std::cerr << "mixed tie actor state";
            for (const Game::Enemy& enemy : game.enemies_) {
                std::cerr << " [s" << enemy.slot << " r" << enemy.row << "c" << enemy.column
                          << " fr" << enemy.fromRow << "c" << enemy.fromColumn << " d"
                          << enemy.direction << " moving=" << enemy.moving << " entering="
                          << enemy.entering << " timerTicks="
                          << enemy.moveTimer * OriginalSchedulerTicksPerSecond << ']';
            }
            std::cerr << '\n';
            return false;
        }

        // An endpoint collision does not abort the common record scan.  The
        // next Troggle job must still own its due edge/coordinate draws.
        game.random_.seed(0x4f524445u);
        game.page_ = Game::Page::Playing;
        game.deathAnimating_ = false;
        game.feedbackEnemySlot_ = -1;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.enemySlotCount_ = 3;
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].type = 0;
        }
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.enemySlots_[1].phase = Game::EnemySlotPhase::Active;
        game.enemySlots_[2].phase = Game::EnemySlotPhase::Waiting;
        game.enemySlots_[2].timer = 0.0;
        Game::Enemy collider;
        collider.row = game.playerRow_;
        collider.column = game.playerColumn_;
        collider.fromRow = game.playerRow_;
        collider.fromColumn = game.playerColumn_ - 1;
        collider.type = 0;
        collider.slot = 0;
        collider.moving = true;
        collider.animationTimer = 0.0;
        Game::Enemy hiddenCollider = collider;
        hiddenCollider.slot = 1;
        hiddenCollider.moving = false;
        hiddenCollider.moveTimer = 0.0;
        game.enemies_ = {collider, hiddenCollider};
        const std::uint64_t callsBeforeCollision = game.random_.calls;
        if (!game.updateEnemies(0.0) || !game.deathAnimating_ ||
            game.feedbackEnemySlot_ != 0 || game.random_.calls != callsBeforeCollision + 3 ||
            game.enemySlots_[2].phase != Game::EnemySlotPhase::Warning) {
            std::cerr << "endpoint tail state update/death/slot/calls/phase="
                      << game.deathAnimating_ << '/' << game.feedbackEnemySlot_ << '/'
                      << (game.random_.calls - callsBeforeCollision) << '/'
                      << static_cast<int>(game.enemySlots_[2].phase) << '\n';
            return false;
        }
        const auto hidden = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                         [](const Game::Enemy& enemy) {
                                             return enemy.slot == 1;
                                         });
        const auto laterWarning = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                                [](const Game::Enemy& enemy) {
                                                    return enemy.slot == 2;
                                                });
        const bool matches = hidden != game.enemies_.end() && hidden->collisionHidden &&
            hidden->overlapFrozen && !hidden->moving && hidden->moveTimer == 0.0 &&
            laterWarning != game.enemies_.end() && laterWarning->entering &&
            !laterWarning->moving;
        if (!matches) {
            std::cerr << "endpoint tail actors=" << game.enemies_.size();
            for (const Game::Enemy& enemy : game.enemies_) {
                std::cerr << " [s" << enemy.slot << " hidden=" << enemy.collisionHidden
                          << " frozen=" << enemy.overlapFrozen << " moving=" << enemy.moving
                          << " timer=" << enemy.moveTimer << ']';
            }
            std::cerr << '\n';
        }
        return matches;
    }
    static bool usesMeasuredEnemyCadence(Game& game) {
        game.random_.seed(0x43414445u);
        constexpr std::array<int, 12> expectedLimits = {
            1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3
        };
        constexpr std::array<int, 12> expectedArrivalTicks = {
            300, 300, 270, 270, 240, 210, 180, 150, 120, 90, 60, 30
        };
        constexpr std::array<int, 12> expectedMoveIntervalTicks = {
            3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1
        };
        constexpr std::array<std::array<int, 5>, 12> expectedWeights = {{
            {{100,  0,  0,  0,  0}}, {{90,  0, 10,  0,  0}},
            {{ 80,  0, 10, 10,  0}}, {{70, 10, 10, 10,  0}},
            {{ 60, 10, 10, 10, 10}}, {{50, 15, 10, 15, 10}},
            {{ 45, 15, 15, 15, 10}}, {{40, 15, 15, 15, 15}},
            {{ 35, 15, 20, 15, 15}}, {{30, 15, 20, 20, 15}},
            {{ 25, 20, 20, 20, 15}}, {{20, 20, 20, 20, 20}},
        }};
        for (int tier = 0; tier < 12; ++tier) {
            game.level_ = tier + 1;
            if (Game::difficultyIndexForLevel(game.level_) != tier ||
                Game::maximumEnemiesForLevel(game.level_) != expectedLimits[static_cast<std::size_t>(tier)] ||
                game.enemyMoveAnimationDuration(1) !=
                    6.0 * expectedMoveIntervalTicks[static_cast<std::size_t>(tier)] /
                        OriginalSchedulerTicksPerSecond ||
                game.enemyMoveAnimationDuration(0) !=
                    5.0 * expectedMoveIntervalTicks[static_cast<std::size_t>(tier)] /
                        OriginalSchedulerTicksPerSecond) return false;
            int weightTotal = 0;
            for (int type = 0; type < 5; ++type) {
                const int weight = Game::enemyWeightForLevel(game.level_, type);
                if (weight != expectedWeights[static_cast<std::size_t>(tier)][static_cast<std::size_t>(type)]) {
                    return false;
                }
                weightTotal += weight;
            }
            if (weightTotal != 100) return false;
            for (int sample = 0; sample < 100; ++sample) {
                const double spawnTicks =
                    game.enemySpawnDelay() * OriginalSchedulerTicksPerSecond;
                if (spawnTicks < expectedArrivalTicks[static_cast<std::size_t>(tier)] ||
                    spawnTicks > expectedArrivalTicks[static_cast<std::size_t>(tier)] + 89) return false;
                const int type = game.chooseEnemyType();
                if (Game::enemyWeightForLevel(game.level_, type) == 0) return false;
            }
        }
        game.random_.seed(0x4457454cu);
        for (int sample = 0; sample < 256; ++sample) {
            const double dwellTicks =
                game.enemyDwellDelay() * OriginalSchedulerTicksPerSecond;
            if (dwellTicks < 31.0 || dwellTicks > 120.0) return false;
        }
        return Game::difficultyIndexForLevel(99) == 11 &&
               Game::maximumEnemiesForLevel(99) == 3 &&
               Game::enemyWeightForLevel(99, 4) == 20;
    }
    static bool usesRecurringEnemySlots(Game& game) {
        game.random_.seed(0x534c4f54u);
        game.level_ = 5;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        for (Game::Cell& cell : game.cells_) cell = {};
        game.enemies_.clear();
        game.initializeEnemySlots();
        if (game.enemySlotCount_ != 2) return false;
        const int persistentType = game.enemySlots_[0].type;
        if (Game::enemyWeightForLevel(game.level_, persistentType) == 0) return false;

        game.enemySlots_[0].timer = 0.0;
        game.updateEnemies(0.0);
        if (game.enemySlots_[0].phase != Game::EnemySlotPhase::Warning ||
            game.enemySlots_[0].timer != Game::TroggleWarningDuration) return false;
        const double warning = game.enemySlots_[0].timer;
        game.updateEnemies(warning + 0.001);
        auto first = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                  [](const Game::Enemy& enemy) { return enemy.slot == 0; });
        if (first == game.enemies_.end() || first->type != persistentType ||
            game.enemySlots_[0].phase != Game::EnemySlotPhase::Active) return false;

        first->exiting = true;
        first->moving = true;
        first->animationTimer = 0.0;
        first->row = -1;
        game.updateEnemies(0.0);
        if (game.enemySlots_[0].phase != Game::EnemySlotPhase::Waiting ||
            game.enemySlots_[0].type != persistentType ||
            game.enemySlots_[0].timer < 240.0 / OriginalSchedulerTicksPerSecond ||
            game.enemySlots_[0].timer > 329.0 / OriginalSchedulerTicksPerSecond) return false;

        game.enemySlots_[0].timer = 0.0;
        game.updateEnemies(0.0);
        const double secondWarning = game.enemySlots_[0].timer;
        game.updateEnemies(secondWarning + 0.001);
        const auto second = std::find_if(game.enemies_.begin(), game.enemies_.end(),
                                         [](const Game::Enemy& enemy) { return enemy.slot == 0; });
        return second != game.enemies_.end() && second->type == persistentType;
    }
    static bool keepsEnemySchedulerRunningDuringPlayerAnimation(Game& game) {
        game.page_ = Game::Page::Playing;
        game.level_ = 1;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.moving_ = true;
        game.moveTimer_ = 1.0;
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Waiting;
        game.enemySlots_[0].timer = 0.05;
        game.random_.seed(1);

        // Two public job ticks elapse while the 1-second player animation is
        // still active, expiring the 50-ms slot countdown. The edge selector
        // consumes one side call and one coordinate call.
        game.update(0.07);
        return game.moving_ && game.enemySlots_[0].phase == Game::EnemySlotPhase::Warning &&
               game.random_.calls == 2;
    }
    static bool appliesSpeciesCellRules(Game& game) {
        game.random_.seed(0x43454c4cu);
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 5;
        game.level_ = 1;
        game.correctRemaining_ = 10;
        game.page_ = Game::Page::Playing;
        for (Game::Cell& cell : game.cells_) cell = {};
        for (int index = 6; index < 15; ++index) {
            game.cells_[static_cast<std::size_t>(index)].label = "5";
            game.cells_[static_cast<std::size_t>(index)].correct = true;
        }

        Game::Enemy helper;
        helper.row = 0;
        helper.column = 0;
        helper.type = 3;
        game.cell(0, 0).label = "10";
        game.cell(0, 0).correct = true;
        game.applyEnemyCellEffect(helper);
        if (!game.cell(0, 0).eaten || !game.cell(0, 0).label.empty() ||
            game.cell(0, 0).correct || game.correctRemaining_ != 9) return false;

        Game::Enemy worker;
        worker.row = 0;
        worker.column = 1;
        worker.type = 1;
        const int remainingBeforeWorker = game.correctRemaining_;
        game.applyEnemyCellEffect(worker);
        if (game.cell(0, 1).eaten || game.cell(0, 1).label.empty() ||
            game.correctRemaining_ != remainingBeforeWorker + (game.cell(0, 1).correct ? 1 : 0)) {
            return false;
        }

        Game::Enemy reggie;
        reggie.row = 0;
        reggie.column = 2;
        reggie.type = 0;
        game.cell(0, 2).safe = true;
        game.cell(0, 2).safeTimer = 4.0;
        const int remainingBeforeBlankReggie = game.correctRemaining_;
        game.applyEnemyCellEffect(reggie);
        if (!game.cell(0, 2).label.empty() || !game.cell(0, 2).eaten ||
            !game.cell(0, 2).safe || game.cell(0, 2).safeTimer != 4.0 ||
            game.correctRemaining_ != remainingBeforeBlankReggie) return false;

        Game::Enemy bashful;
        bashful.row = 0;
        bashful.column = 3;
        bashful.type = 2;
        const int remainingBeforeBlankBashful = game.correctRemaining_;
        game.applyEnemyCellEffect(bashful);
        if (!game.cell(0, 3).label.empty() || !game.cell(0, 3).eaten ||
            game.correctRemaining_ != remainingBeforeBlankBashful) return false;

        game.cell(0, 3).label = "25";
        game.cell(0, 3).correct = true;
        game.cell(0, 3).eaten = false;
        ++game.correctRemaining_;
        const int remainingBeforeBashful = game.correctRemaining_;
        game.applyEnemyCellEffect(bashful);
        const Game::Cell& bashfulReplacement = game.cell(0, 3);
        if (bashfulReplacement.eaten || bashfulReplacement.label.empty() ||
            game.correctRemaining_ != remainingBeforeBashful - 1 +
                                      (bashfulReplacement.correct ? 1 : 0)) return false;

        game.cell(0, 4).label = "20";
        game.cell(0, 4).correct = true;
        ++game.correctRemaining_;
        const int remainingBeforeReggie = game.correctRemaining_;
        reggie.column = 4;
        game.applyEnemyCellEffect(reggie);
        const Game::Cell& reggieReplacement = game.cell(0, 4);
        if (reggieReplacement.eaten || reggieReplacement.label.empty() ||
            game.correctRemaining_ != remainingBeforeReggie - 1 +
                                      (reggieReplacement.correct ? 1 : 0)) return false;

        Game::Enemy smarty;
        smarty.row = 0;
        smarty.column = 5;
        smarty.type = 4;
        game.cell(0, 5).label = "30";
        game.cell(0, 5).correct = true;
        ++game.correctRemaining_;
        game.cell(0, 5).safe = true;
        game.cell(0, 5).safeTimer = 7.0;
        const int remainingBeforeSmarty = game.correctRemaining_;
        game.applyEnemyCellEffect(smarty);
        const Game::Cell& preserved = game.cell(0, 5);
        return preserved.label == "30" && preserved.correct && !preserved.eaten && preserved.safe &&
               preserved.safeTimer == 7.0 && game.correctRemaining_ == remainingBeforeSmarty;
    }
    static bool abortsTrailCallbacksAfterBoardCompletion(Game& safeZone) {
        const auto prepare = [](Game& game, const int cellIndex) {
            game.settingsPersistenceEnabled_ = false;
            game.soundOn_ = true;
            game.musicOn_ = false;
            game.speakerEffects_ = false;
            game.attractMode_ = false;
            game.page_ = Game::Page::Playing;
            game.level_ = 1;
            game.cheatLevel_ = 1;
            game.moving_ = false;
            game.munching_ = false;
            game.enemies_.clear();
            for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
            for (Game::SafeZoneJob& job : game.safeZoneJobs_) job = {};
            game.enemySlotCount_ = 0;
            game.safeZoneJobCount_ = 0;
            game.enemyWarning_ = false;
            for (Game::Cell& cell : game.cells_) cell = {};
            Game::Cell& finalCell = game.cells_[static_cast<std::size_t>(cellIndex)];
            finalCell.label = "5";
            finalCell.correct = true;
            finalCell.eaten = false;
            game.correctRemaining_ = 1;
            game.lastGameplaySound_ = -1;
            game.previousGameplaySound_ = -1;
        };
        const auto helperAt = [](const Game& game, const int cellIndex) {
            Game::Enemy helper;
            helper.row = cellIndex / Game::BoardColumns;
            helper.column = cellIndex % Game::BoardColumns;
            helper.fromRow = helper.row;
            helper.fromColumn = helper.column;
            helper.type = 3;
            helper.slot = 0;
            helper.savedCell = game.cells_[static_cast<std::size_t>(cellIndex)];
            helper.savedCellValid = true;
            return helper;
        };

        // 0x09a3a exits the safe-zone callback immediately when 0x0a183
        // reports that a species trail removed the last positive cell. The
        // freshly generated board must not be followed by retirement sounds
        // or an erase through the now-stale old-board actor index.
        safeZone.random_.seed(0x4142u);
        safeZone.startGame(Game::Mode::Multiples, 1);
        safeZone.random_.seed(0x535au);
        OriginalRandom safeProbe = safeZone.random_;
        const int safeCell = (1 + safeProbe.range(Game::BoardRows - 2)) * Game::BoardColumns +
                             1 + safeProbe.range(Game::BoardColumns - 2);
        prepare(safeZone, safeCell);
        safeZone.enemySlotCount_ = 1;
        safeZone.enemySlots_[0].type = 3;
        safeZone.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        safeZone.enemies_.push_back(helperAt(safeZone, safeCell));
        safeZone.safeZoneJobCount_ = 1;
        safeZone.safeZoneJobs_[0].period = 10.0;
        safeZone.safeZoneJobs_[0].timer = 0.0;
        Game safeBaseline;
        safeBaseline.settingsPersistenceEnabled_ = false;
        safeBaseline.random_.seed(0x4142u);
        safeBaseline.startGame(Game::Mode::Multiples, 1);
        prepare(safeBaseline, safeCell);
        safeBaseline.random_ = safeZone.random_;
        const int baselineSafeCell =
            safeBaseline.randomInt(1, Game::BoardRows - 2) * Game::BoardColumns +
            safeBaseline.randomInt(1, Game::BoardColumns - 2);
        if (baselineSafeCell != safeCell ||
            safeBaseline.applyEnemyCellEffect(helperAt(safeBaseline, safeCell))) {
            return false;
        }
        const std::size_t safePlays = safeZone.attractOplPlayer_.effectPlayCount();
        safeZone.gameplayTickAccumulator_ = 0.0;
        safeZone.update(1.0 / OriginalSchedulerTicksPerSecond + 1e-12);
        if (safeZone.page_ != Game::Page::Playing || safeZone.level_ != 2 ||
            !safeZone.enemies_.empty() || safeZone.previousGameplaySound_ != -1 ||
            safeZone.lastGameplaySound_ != 15 ||
            safeZone.attractOplPlayer_.effectPlayCount() != safePlays + 1 ||
            std::abs(safeZone.gameplayTickAccumulator_) > 1e-12 ||
            safeZone.random_.state != safeBaseline.random_.state ||
            safeZone.random_.calls != safeBaseline.random_.calls ||
            safeZone.enemySlotCount_ != safeBaseline.enemySlotCount_ ||
            safeZone.safeZoneJobCount_ != safeBaseline.safeZoneJobCount_ ||
            safeZone.enemySlots_[0].phase != safeBaseline.enemySlots_[0].phase ||
            safeZone.enemySlots_[0].timer != safeBaseline.enemySlots_[0].timer ||
            safeZone.safeZoneJobs_[0].timer != safeBaseline.safeZoneJobs_[0].timer) {
            return false;
        }

        // The ordinary state-4 movement callback has the same terminal
        // branch. Compare it with a direct call to prove updateEnemies stops
        // before steering can consume another random value through a stale
        // Enemy reference after generateBoard clears the vector.
        Game direct;
        Game scheduled;
        direct.random_.seed(0x4d56u);
        scheduled.random_.seed(0x4d56u);
        direct.startGame(Game::Mode::Multiples, 1);
        scheduled.startGame(Game::Mode::Multiples, 1);
        constexpr int movementCell = Game::BoardColumns + 1;
        prepare(direct, movementCell);
        prepare(scheduled, movementCell);
        Game::Enemy directHelper = helperAt(direct, movementCell);
        Game::Enemy scheduledHelper = helperAt(scheduled, movementCell);
        scheduledHelper.moveTimer = 0.0;
        scheduled.enemySlotCount_ = 1;
        scheduled.enemySlots_[0].type = 3;
        scheduled.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        scheduled.enemies_.push_back(scheduledHelper);
        if (direct.applyEnemyCellEffect(directHelper)) return false;
        scheduled.gameplayTickAccumulator_ = 0.0;
        scheduled.update(1.0 / OriginalSchedulerTicksPerSecond + 1e-12);
        return direct.page_ == Game::Page::Playing && scheduled.page_ == Game::Page::Playing &&
               direct.level_ == 2 && scheduled.level_ == 2 &&
               direct.random_.calls == scheduled.random_.calls &&
               direct.random_.state == scheduled.random_.state &&
               direct.lastGameplaySound_ == 15 && scheduled.lastGameplaySound_ == 15 &&
               direct.attractOplPlayer_.effectPlayCount() == 1 &&
               scheduled.attractOplPlayer_.effectPlayCount() == 1 &&
               scheduled.enemies_.empty() &&
               std::abs(scheduled.gameplayTickAccumulator_) <= 1e-12;
    }
    static bool cyclesIndependentSafeZones(Game& game) {
        game.random_.seed(0x53414645u);
        game.level_ = 1;
        game.enemies_.clear();
        bool sawInitialPlayerSafeOverlap = false;
        for (int board = 0; board < 96; ++board) {
            game.generateBoard();
            const Game::Cell& playerCell = game.cell(game.playerRow_, game.playerColumn_);
            if (playerCell.safe) {
                sawInitialPlayerSafeOverlap = playerCell.eaten && playerCell.label.empty();
                break;
            }
        }
        if (!sawInitialPlayerSafeOverlap) return false;
        game.generateBoard();
        if (game.safeZoneJobCount_ != 2 || safeCellCount(game) != 1 ||
            Game::maximumSafeZoneJobsForLevel(1) != 2 ||
            Game::initialSafeZonesForLevel(1) != 1 ||
            Game::maximumSafeZoneJobsForLevel(8) != 1 ||
            Game::initialSafeZonesForLevel(4) != 0) return false;
        for (int index = 0; index < game.safeZoneJobCount_; ++index) {
            const Game::SafeZoneJob& job = game.safeZoneJobs_[static_cast<std::size_t>(index)];
            if (job.period < 210.0 / OriginalSchedulerTicksPerSecond ||
                job.period > 419.0 / OriginalSchedulerTicksPerSecond ||
                job.timer <= 0.0 || job.timer > job.period) return false;
            if (job.active) {
                const int row = job.cellIndex / Game::BoardColumns;
                const int column = job.cellIndex % Game::BoardColumns;
                if (row < 1 || row > 3 || column < 1 || column > 4) return false;
            }
        }

        int inactive = game.safeZoneJobs_[0].active ? 1 : 0;
        Game::SafeZoneJob& adding = game.safeZoneJobs_[static_cast<std::size_t>(inactive)];
        adding.timer = 0.0;
        game.updateSafeZones(0.0);
        if (!adding.active || safeCellCount(game) != 2) return false;

        const int removing = inactive == 0 ? 1 : 0;
        Game::SafeZoneJob& active = game.safeZoneJobs_[static_cast<std::size_t>(removing)];
        active.timer = 0.0;
        game.updateSafeZones(0.0);
        return !active.active && active.cellIndex == -1 && safeCellCount(game) == 1;
    }
    static bool usesTroggleOverlapAndSafeCrossingRules(Game& game) {
        game.random_.seed(0x4f564552u);
        game.page_ = Game::Page::Playing;
        game.level_ = 5;
        game.playerRow_ = 3;
        game.playerColumn_ = 4;
        game.correctRemaining_ = 10;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 5;
        for (Game::Cell& current : game.cells_) current = {};
        game.cell(0, 0).label = "5";
        game.cell(0, 0).correct = true;
        game.cell(0, 1).label = "10";
        game.cell(0, 1).correct = true;

        // 0x0904d tests only the safe-zone byte. Another Troggle in the
        // destination cell must not force a turn.
        Game::Enemy occupant;
        occupant.row = 2;
        occupant.column = 2;
        occupant.type = 1;
        occupant.slot = 1;
        game.enemies_ = {occupant};
        Game::Enemy mover;
        mover.row = 2;
        mover.column = 1;
        mover.type = 0;
        mover.direction = 1;
        mover.slot = 0;
        game.moveEnemy(mover);
        if (!mover.moving || mover.row != 2 || mover.column != 2) return false;

        // Edge warnings are accepted directly even if that edge cell is
        // already occupied.
        game.enemies_.clear();
        for (int column = 0; column < Game::BoardColumns; ++column) {
            Game::Enemy top;
            top.row = 0;
            top.column = column;
            game.enemies_.push_back(top);
            Game::Enemy bottom = top;
            bottom.row = Game::BoardRows - 1;
            game.enemies_.push_back(bottom);
        }
        for (int row = 1; row < Game::BoardRows - 1; ++row) {
            Game::Enemy left;
            left.row = row;
            left.column = 0;
            game.enemies_.push_back(left);
            Game::Enemy right = left;
            right.column = Game::BoardColumns - 1;
            game.enemies_.push_back(right);
        }
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Waiting;
        if (!game.beginEnemyWarning(0) ||
            game.enemySlots_[0].phase != Game::EnemySlotPhase::Warning) return false;

        // The captured first demo is a genuine Troggle-on-Troggle bite, not
        // simple visual overlap. A lower-numbered moving job freezes a
        // resident state-4 job before the resident can depart, alternates
        // bite records for 21 scheduler ticks, then rearms the victim after
        // installing a fresh dwell for the survivor.
        game.random_.seed(0x43414e4eu);
        game.enemies_.clear();
        game.enemySlotCount_ = 2;
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].type = slot;
            game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                Game::EnemySlotPhase::Active;
        }
        Game::Enemy resident;
        resident.row = 2;
        resident.column = 2;
        resident.type = 1;
        resident.slot = 1;
        resident.moveTimer = 0.0;
        resident.savedCell = game.cell(2, 2);
        resident.savedCellValid = true;
        Game::Enemy cannibal;
        cannibal.fromRow = 2;
        cannibal.fromColumn = 1;
        cannibal.row = 2;
        cannibal.column = 2;
        cannibal.type = 0;
        cannibal.slot = 0;
        cannibal.direction = 1;
        cannibal.moving = true;
        cannibal.animationTimer = 0.0;
        game.enemies_ = {resident, cannibal}; // Deliberately not scheduler order.
        game.updateEnemies(0.0);
        if (game.random_.calls != 1 || game.enemies_.size() != 2 ||
            !game.enemies_[0].overlapFrozen || !game.enemies_[0].collisionHidden ||
            !game.enemies_[1].cannibalizing) return false;
        game.updateEnemies(20.0 / OriginalSchedulerTicksPerSecond);
        if (!game.enemies_[1].cannibalizing || game.enemySlots_[1].phase !=
            Game::EnemySlotPhase::Active) return false;
        game.updateEnemies(2.0 / OriginalSchedulerTicksPerSecond);
        if (game.random_.calls != 3 || game.enemies_.size() != 1 ||
            game.enemies_[0].slot != 0 || game.enemies_[0].cannibalizing ||
            game.enemies_[0].direction != 1 || game.enemies_[0].dwellFrame != 15 ||
            game.ordinaryEnemyFrame(game.enemies_[0]) != 15 ||
            game.enemySlots_[1].phase != Game::EnemySlotPhase::Waiting ||
            game.enemySlots_[1].type != 1) return false;

        // One overlapping species owns the bite; the common collision routine
        // hides the other until its movement callback assigns a new frame.
        Game::Enemy worker;
        worker.row = 2;
        worker.column = 2;
        worker.type = 1;
        worker.direction = 1;
        worker.slot = 0;
        Game::Enemy smarty = worker;
        smarty.type = 4;
        smarty.slot = 1;
        Game::Enemy pendingEntry = worker;
        pendingEntry.type = 0;
        pendingEntry.slot = 2;
        pendingEntry.entering = true;
        pendingEntry.fromColumn = Game::BoardColumns;
        game.enemies_ = {worker, smarty, pendingEntry};
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.lives_ = 4;
        game.loseLife(4, 1);
        if (game.feedbackEnemyType_ != 4 || game.feedbackEnemySlot_ != 1 ||
            !game.enemies_[0].collisionHidden || game.enemies_[1].collisionHidden ||
            game.enemies_[2].collisionHidden || game.enemies_[2].overlapFrozen) {
            return false;
        }
        game.moveEnemy(game.enemies_[0]);
        if (game.enemies_[0].collisionHidden) return false;

        // A new safe cell catches a Troggle whose active move crosses it,
        // applies that species' trail at its destination, and rearms its slot.
        game.page_ = Game::Page::Playing;
        game.feedbackKind_ = Game::FeedbackKind::None;
        game.deathAnimating_ = false;
        game.enemies_.clear();
        for (Game::Cell& current : game.cells_) current = {};
        game.cell(0, 0).label = "5";
        game.cell(0, 0).correct = true;
        game.cell(0, 1).label = "10";
        game.cell(0, 1).correct = true;
        for (int row = 1; row <= 3; ++row) {
            for (int column = 1; column <= 4; ++column) game.cell(row, column).safe = true;
        }
        game.cell(1, 1).safe = false;
        game.cell(1, 2).label = "10";
        game.cell(1, 2).correct = true;
        game.playerRow_ = 1;
        game.playerColumn_ = 1;
        game.lives_ = 4;
        game.correctRemaining_ = 10;
        Game::Enemy helper;
        helper.fromRow = 1;
        helper.fromColumn = 1;
        helper.row = 1;
        helper.column = 2;
        helper.type = 3;
        helper.moving = true;
        helper.slot = 0;
        game.enemies_.push_back(helper);
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.safeZoneJobCount_ = 1;
        game.safeZoneJobs_[0] = {};
        game.safeZoneJobs_[0].period = 1.0;
        game.safeZoneJobs_[0].timer = 0.0;
        game.updateSafeZones(0.0);
        return game.safeZoneJobs_[0].active && game.safeZoneJobs_[0].cellIndex == 7 &&
               game.enemies_.empty() && game.cell(1, 2).eaten &&
               game.enemySlots_[0].phase == Game::EnemySlotPhase::Waiting &&
               game.page_ == Game::Page::Playing && game.lives_ == 4;
    }
    static bool usesFourMuncherLifeCycle(Game& game) {
        game.page_ = Game::Page::Playing;
        game.lives_ = 4;
        game.invincible_ = false;
        game.score_ = 0;
        for (int death = 0; death < 4; ++death) {
            game.loseLife(0);
            if (game.lives_ != 4 - death || !game.lifeLossPending_ ||
                game.page_ != Game::Page::Feedback) return false;
            game.update(Game::TroggleEatAnimationDuration + 0.001);
            if (game.lives_ != 3 - death || game.lifeLossPending_) return false;
            game.keyDown(VK_SPACE);
            if (death < 3 && game.page_ != Game::Page::Playing) return false;
        }
        return game.page_ == Game::Page::Hall &&
               game.hallContext_ == Game::HallContext::PostGame;
    }
    static bool usesRecoveredCartoonCadenceAndOrder(Game& game) {
        // The raw DOS LCG outputs for seed 0x564c reduce modulo five to
        // 2,2,1,2,1 / 3,4,0,4,1 / 4,3,1,3,3. Applying the original's
        // full-range swap loop three times must yield these exact arrays.
        game.random_.seed(0x4c45564cu);
        game.cartoonOrder_ = {0, 1, 2, 3, 4};
        game.cartoonOrderIndex_ = 0;
        game.prepareCartoonOrderForCompletedLevel(1);
        if (game.random_.calls != 5 ||
            game.cartoonOrder_ != std::array<int, 5>{2, 4, 3, 0, 1}) return false;
        game.prepareCartoonOrderForCompletedLevel(2);
        if (game.random_.calls != 10 ||
            game.cartoonOrder_ != std::array<int, 5>{3, 2, 0, 4, 1}) return false;
        game.prepareCartoonOrderForCompletedLevel(3);
        if (game.random_.calls != 15 ||
            game.cartoonOrder_ != std::array<int, 5>{1, 0, 4, 3, 2}) return false;

        // Once a cartoon has closed, completions consume no shuffle calls
        // until all five order slots have been used and the cursor wraps.
        game.advanceCartoonOrder();
        const std::uint64_t callsAfterFirstCartoon = game.random_.calls;
        game.prepareCartoonOrderForCompletedLevel(4);
        game.prepareCartoonOrderForCompletedLevel(6);
        if (game.random_.calls != callsAfterFirstCartoon || game.cartoonOrderIndex_ != 1) {
            return false;
        }
        for (int scene = 1; scene < 5; ++scene) game.advanceCartoonOrder();
        if (game.cartoonOrderIndex_ != 0) return false;
        game.prepareCartoonOrderForCompletedLevel(16);
        if (game.random_.calls != callsAfterFirstCartoon + 5) return false;

        // Integration: a shuffled scene is used after level 3, the visible
        // level remains 3 during it, and advances only when cartoon teardown
        // returns to the board initializer.
        game.random_.seed(0x4c45564cu);
        game.mode_ = Game::Mode::Multiples;
        game.page_ = Game::Page::Playing;
        game.musicOn_ = false;
        game.soundOn_ = false;
        game.cartoonOrder_ = {4, 2, 0, 3, 1};
        game.cartoonOrderIndex_ = 2;
        game.level_ = 3;
        game.score_ = 240;
        game.completeLevel();
        if (game.page_ != Game::Page::LevelComplete || game.level_ != 3 ||
            game.cutsceneImage_ != 2013 || game.score_ != 240 ||
            game.levelCompleteScene_.valid() ||
            game.pendingCartoonScene_ != 0) return false;
        const std::optional<SpriteFrame> continuationFrame = game.assets_.spriteFrame(2013, 22);
        if (!continuationFrame || continuationFrame->sheetId != 3013) return false;
        Renderer renderer;
        game.render(renderer);
        const std::vector<std::uint32_t> initialFrame = renderer.pixels();
        game.update(0.8);
        if (game.page_ != Game::Page::LevelComplete) return false;
        game.render(renderer);
        if (renderer.pixels() == initialFrame) return false;
        game.transitionTimer_ = 0.0;
        game.update(0.001);
        return game.page_ == Game::Page::Playing && game.level_ == 4 &&
               game.cartoonOrderIndex_ == 3;
    }
    static bool rendersRecoveredCartoonTimelines(Game& game) {
        static constexpr std::array<int, 5> Images = {2013, 2015, 2017, 2019, 2021};
        static constexpr std::array<int, 5> TerminalTicks = {183, 199, 223, 194, 186};
        // Executable-backed presenter baselines for every host-visible SCPT
        // state. The independent muted live audit now observes every complete
        // DOS-presented timeline exactly, including all scene-3 terminal-loop
        // repetitions; these hashes independently retain the native baseline.
        static constexpr std::array<std::uint64_t, 5> ExpectedVgaTimelineHashes = {
            0xc0d1952ade7496e9ull,
            0xcf0893ea88f18845ull,
            0xb706c9f88cdb6d97ull,
            0xabfe60bbd905cdf7ull,
            0xff411c4488c2cdeaull,
        };
        // Filled from the same complete renderer timeline in hardware CGA
        // mode. These are kept separately because the palette/background
        // register conversion intentionally changes the rendered pixels.
        static constexpr std::array<std::uint64_t, 5> ExpectedCgaTimelineHashes = {
            0xc20d4824e86ee441ull,
            0x61d15b818f0f57c9ull,
            0x573e09e024d8e68bull,
            0xca516ce0f36707ceull,
            0x897e0888c0214914ull,
        };

        std::array<std::uint64_t, 5> timelineHashes{};
        std::array<int, 5> visibleFrames{};

        const char* dumpDirectoryText = std::getenv("MUNCHERS_DUMP_NUMBER_SCENE_DIR");
        const bool dumpFrames = dumpDirectoryText && *dumpDirectoryText;
        const std::filesystem::path dumpDirectory =
            dumpFrames ? std::filesystem::path(dumpDirectoryText) : std::filesystem::path{};
        std::ofstream stateOutput;
        if (dumpFrames) {
            std::error_code error;
            std::filesystem::create_directories(dumpDirectory, error);
            if (error) {
                std::cerr << "Could not create Number scene dump directory: "
                          << dumpDirectory << " (" << error.message() << ")\n";
                return false;
            }
            stateOutput.open(dumpDirectory / "number-scene-states.tsv",
                             std::ios::binary | std::ios::trunc);
            if (!stateOutput) return false;
            stateOutput << "scene\ttick\tkind\tindex\tthread\tframe\tx\ty\tlayer"
                           "\tvisible\tended\n";
        }

        const auto dumpFrame = [&](const Renderer& renderer, const int sceneIndex,
                                   const int tick) {
            if (!dumpFrames) return true;
            std::string tickText = std::to_string(tick);
            if (tickText.size() < 5u) {
                tickText.insert(tickText.begin(), 5u - tickText.size(), '0');
            }
            const std::filesystem::path outputPath =
                dumpDirectory / ("scene-" + std::to_string(sceneIndex) +
                                 "-tick-" + tickText + ".ppm");
            std::ofstream output(outputPath, std::ios::binary);
            if (!output) {
                std::cerr << "Could not create Number scene frame: " << outputPath << '\n';
                return false;
            }
            output << "P6\n" << Renderer::Width << ' ' << Renderer::Height << "\n255\n";
            for (const std::uint32_t pixel : renderer.pixels()) {
                const char rgb[3] = {
                    static_cast<char>((pixel >> 16) & 0xffu),
                    static_cast<char>((pixel >> 8) & 0xffu),
                    static_cast<char>(pixel & 0xffu),
                };
                output.write(rgb, sizeof(rgb));
            }
            if (!output) {
                std::cerr << "Could not write Number scene frame: " << outputPath << '\n';
                return false;
            }
            return true;
        };

        {
            Game timed;
            timed.settingsPersistenceEnabled_ = false;
            timed.soundOn_ = false;
            timed.musicOn_ = false;
            timed.page_ = Game::Page::LevelComplete;
            timed.transitionTimer_ = 1000.0;
            timed.cutsceneImage_ = Images[0];
            timed.cutsceneSoundBank_ = 11;
            if (!timed.loadLevelCompleteScene(0)) return false;
            timed.advanceLevelCompleteScene();
            timed.update(0.099);
            if (std::abs(timed.sceneTickAccumulator_ - 0.99) > 1e-9) return false;
            timed.update(0.001);
            if (std::abs(timed.sceneTickAccumulator_) > 1e-9) return false;
        }

        game.settingsPersistenceEnabled_ = false;
        game.soundOn_ = false;
        game.musicOn_ = false;
        game.page_ = Game::Page::LevelComplete;

        for (int sceneIndex = 0; sceneIndex < static_cast<int>(Images.size()); ++sceneIndex) {
            game.cutsceneImage_ = Images[static_cast<std::size_t>(sceneIndex)];
            game.cutsceneSoundBank_ = 11 + sceneIndex;
            if (!game.loadLevelCompleteScene(sceneIndex)) return false;

            // completeLevel() runs the first scene callback synchronously.
            // The signal-99 terminal callback tears the page down before it
            // can be painted, so hash ticks 1 through terminal-1 exactly.
            game.advanceLevelCompleteScene();
            int tick = 1;
            std::uint64_t timeline = 1469598103934665603ull;
            while (game.levelCompleteScene_.valid() &&
                   !game.levelCompleteScene_.finished() && tick < 20'000) {
                if (stateOutput) {
                    const auto writeActors = [&](const std::vector<SceneActor>& actors,
                                                 const char* kind) {
                        for (std::size_t index = 0; index < actors.size(); ++index) {
                            const SceneActor& actor = actors[index];
                            stateOutput << sceneIndex << '\t' << tick << '\t' << kind
                                        << '\t' << index << '\t' << actor.thread << '\t'
                                        << actor.frame << '\t' << actor.x << '\t' << actor.y
                                        << '\t' << actor.layer << '\t' << actor.visible << '\t'
                                        << actor.ended << '\n';
                        }
                    };
                    writeActors(game.levelCompleteScene_.bakedActors(), "baked");
                    writeActors(game.levelCompleteScene_.actors(), "live");
                    const auto& paintEvents = game.levelCompleteScene_.paintEvents();
                    for (std::size_t index = 0; index < paintEvents.size(); ++index) {
                        const ScenePaintEvent& event = paintEvents[index];
                        const auto writePaintActor = [&](const SceneActor& actor,
                                                         const char* kind) {
                            stateOutput << sceneIndex << '\t' << tick << '\t' << kind
                                        << '\t' << index << '\t' << actor.thread << '\t'
                                        << actor.frame << '\t' << actor.x << '\t' << actor.y
                                        << '\t' << actor.layer << '\t' << actor.visible << '\t'
                                        << actor.ended << '\n';
                        };
                        writePaintActor(
                            event.previous,
                            event.dirty ? "paint-dirty-previous" : "paint-clean-previous");
                        writePaintActor(
                            event.current,
                            event.dirty ? "paint-dirty-current" : "paint-clean-current");
                    }
                }
                timeline ^= static_cast<std::uint64_t>(tick);
                timeline *= 1099511628211ull;
                timeline ^= fullFrameHash(game);
                timeline *= 1099511628211ull;
                Renderer renderer;
                game.render(renderer);
                if (!dumpFrame(renderer, sceneIndex, tick)) return false;
                ++visibleFrames[static_cast<std::size_t>(sceneIndex)];
                game.advanceLevelCompleteScene();
                ++tick;
            }
            timelineHashes[static_cast<std::size_t>(sceneIndex)] = timeline;
            if (!game.levelCompleteScene_.valid() ||
                !game.levelCompleteScene_.finished() ||
                game.levelCompleteScene_.signal() != 99 ||
                tick != TerminalTicks[static_cast<std::size_t>(sceneIndex)] ||
                visibleFrames[static_cast<std::size_t>(sceneIndex)] != tick - 1) {
                return false;
            }
        }

        const auto& expectedTimelineHashes =
            game.assets_.graphicsMode() == GraphicsMode::Cga4
                ? ExpectedCgaTimelineHashes
                : ExpectedVgaTimelineHashes;
        if (timelineHashes != expectedTimelineHashes) {
            std::cerr << "Recovered Number cartoon framebuffer timelines:" << std::hex;
            for (const std::uint64_t hash : timelineHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << std::dec << " visible=";
            for (const int count : visibleFrames) std::cerr << ' ' << count;
            std::cerr << '\n';
            return false;
        }
        return true;
    }
    static bool decodesRecoveredCartoonScores(Game& game) {
        static constexpr std::array<std::uint32_t, 5> ExpectedDurations = {
            18'368, 19'191, 26'495, 17'516, 19'562,
        };
        static constexpr std::array<std::size_t, 5> ExpectedWriteCounts = {
            319, 1'210, 1'040, 549, 779,
        };
        static constexpr std::array<std::uint64_t, 5> ExpectedHashes = {
            0xebfa1bcd24fa66f5ull, 0x07c9337c6978f332ull,
            0x15a1b65d33897586ull, 0xf740bb586aae2df8ull,
            0xfaaa5af1426219b9ull,
        };
        std::array<std::uint32_t, 5> durations{};
        std::array<std::size_t, 5> writeCounts{};
        std::array<std::uint64_t, 5> hashes{};
        for (int scene = 0; scene < 5; ++scene) {
            const BlobView bank = game.assets_.gameArchive().find(
                "ADLI", static_cast<std::uint32_t>(11 + scene));
            const MeccSound score = decodeMeccGSound(bank, 38);
            if (!score.valid || score.durationMilliseconds == 0 ||
                score.writes.size() < 2 || score.writes[0].reg != 0xbd ||
                score.writes[0].value != 0x80 || score.writes[1].reg != 0xbd ||
                score.writes[1].value != 0xc0 || !score.speakerWrites.empty()) return false;
            durations[static_cast<std::size_t>(scene)] = score.durationMilliseconds;
            writeCounts[static_cast<std::size_t>(scene)] = score.writes.size();
            std::uint64_t hash = 1469598103934665603ull;
            const auto byte = [&](const std::uint8_t value) {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            const auto word = [&](const std::uint32_t value) {
                for (int shift = 0; shift < 32; shift += 8) {
                    byte(static_cast<std::uint8_t>(value >> shift));
                }
            };
            word(score.durationMilliseconds);
            word(static_cast<std::uint32_t>(score.writes.size()));
            for (const OplWrite& write : score.writes) {
                word(write.milliseconds);
                byte(write.reg);
                byte(write.value);
            }
            hashes[static_cast<std::size_t>(scene)] = hash;
        }
        if (durations != ExpectedDurations || writeCounts != ExpectedWriteCounts ||
            hashes != ExpectedHashes) {
            std::cerr << "Number cartoon scores durations=";
            for (const auto value : durations) std::cerr << ' ' << value;
            std::cerr << " writes=";
            for (const auto value : writeCounts) std::cerr << ' ' << value;
            std::cerr << " hashes=" << std::hex;
            for (const auto value : hashes) std::cerr << " 0x" << value;
            std::cerr << std::dec << '\n';
            return false;
        }
        return true;
    }
    static bool usesReferenceScoreTableAndBonuses(Game& game) {
        constexpr std::array expected = {
            5, 5, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70
        };
        for (int level = 1; level <= static_cast<int>(expected.size()); ++level) {
            if (Game::scoreValueForLevel(level) != expected[static_cast<std::size_t>(level - 1)]) {
                return false;
            }
        }
        if (Game::scoreValueForLevel(17) != 75 || Game::scoreValueForLevel(99) != 75) return false;
        game.score_ = 995;
        game.lives_ = 2;
        if (game.addScore(5)) return false;
        if (game.score_ != 1000 || game.lives_ != 3) return false;
        game.score_ = 9995;
        if (game.addScore(5) || game.score_ != 10000 || game.lives_ != 4) return false;
        game.score_ = 19995;
        if (game.addScore(5) || game.score_ != 20000 || game.lives_ != 5) return false;
        game.score_ = 989995;
        game.lives_ = 99;
        if (game.addScore(5) || game.score_ != 990000 || game.lives_ != 100) return false;
        game.score_ = 990000;
        if (game.addScore(75) || game.score_ != 990075 || game.lives_ != 100) return false;
        game.score_ = 999995;
        if (!game.addScore(5) || game.score_ != Game::MaximumScore || game.lives_ != 100) {
            return false;
        }

        // 0x07edf invokes the ordinary scored-game terminal at one million.
        // An admitted score enters the board-preserving name editor immediately
        // and cannot also take the final-answer level/cartoon path.
        game.scorePersistenceEnabled_ = false;
        game.scores_[0].clear();
        game.mode_ = Game::Mode::Multiples;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.page_ = Game::Page::Playing;
        game.level_ = 16;
        game.score_ = 999930;
        game.lives_ = 100;
        game.correctRemaining_ = 1;
        game.playerRow_ = 0;
        game.playerColumn_ = 0;
        game.enemies_.clear();
        game.soundOn_ = false;
        game.musicOn_ = false;
        for (Game::Cell& cell : game.cells_) cell = {};
        game.cells_[0].label = "1000000";
        game.cells_[0].correct = true;
        game.munch();
        game.update(Game::MunchAnimationDuration + 0.001);
        if (game.page_ != Game::Page::NameEntry || game.score_ != Game::MaximumScore ||
            game.level_ != 16 || game.lives_ != 100 || !game.cells_[0].eaten ||
            game.correctRemaining_ != 0 || game.levelCompleteScene_.valid()) return false;

        // Stable tie ordering rejects an equal one-million candidate when all
        // ten Hall slots already contain that maximum, so the same terminal
        // proceeds directly to the post-game Hall.
        game.scores_[0].assign(10, Game::ScoreEntry{Game::MaximumScore, 16, "max"});
        game.page_ = Game::Page::Playing;
        game.score_ = 999930;
        game.correctRemaining_ = 2;
        game.cells_[0].eaten = false;
        game.munch();
        game.update(Game::MunchAnimationDuration + 0.001);
        return game.page_ == Game::Page::Hall &&
               game.hallContext_ == Game::HallContext::PostGame &&
               game.score_ == Game::MaximumScore && game.lives_ == 100;
    }
    static bool usesCapturedLaterLevelUserHud(Game& game) {
        // The reference process was allowed to initialize an ordinary user
        // Factors board from RNG state 0x1cb5, with only its level/score/reserve
        // data installed before the original Wipe and HUD painter ran.
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        applyCapturedSmartyConfig(game);
        game.random_.seed(0x1cb5u);
        // The injection changes only the visible level after the original has
        // selected its level-1 pressure tier, so generate that exact ordinary
        // board first and change the display/scoring level before painting.
        game.startGame(Game::Mode::Factors, 1);
        game.level_ = 7;
        game.score_ = 321;

        Renderer renderer;
        game.render(renderer);
        if (const char* directoryText =
                std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_LATER_HUD_DIR");
            directoryText && *directoryText) {
            const std::filesystem::path directory(directoryText);
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            std::ofstream output(directory / "number-level7-score321-native.ppm",
                                 std::ios::binary | std::ios::trunc);
            if (!error && output) {
                output << "P6\n" << Renderer::Width << ' ' << Renderer::Height
                       << "\n255\n";
                for (const std::uint32_t pixel : renderer.pixels()) {
                    const char rgb[3] = {
                        static_cast<char>((pixel >> 16) & 0xffu),
                        static_cast<char>((pixel >> 8) & 0xffu),
                        static_cast<char>(pixel & 0xffu),
                    };
                    output.write(rgb, sizeof(rgb));
                }
            }
        }

        const std::uint64_t hash = renderedFrameHash(renderer);
        if (hash != 0x8b5ff7fa8195e03full) {
            std::cerr << "captured level-7 user HUD hash: 0x" << std::hex
                      << hash << std::dec << '\n';
            return false;
        }
        return game.page_ == Game::Page::Playing && !game.attractMode_ &&
               game.level_ == 7 && game.score_ == 321 && game.lives_ == 4 &&
               game.activeBoardMode_ == Game::Mode::Factors && game.target_ == 41 &&
               game.playerRow_ == 1 && game.playerColumn_ == 1;
    }
    static bool isStartupVersion(const Game& game) { return game.page_ == Game::Page::StartupVersion; }
    static bool isStartupSplash(const Game& game) { return game.page_ == Game::Page::StartupSplash; }
    static bool isTitle(const Game& game) { return game.page_ == Game::Page::Title; }
    static bool isAttract(const Game& game) { return game.page_ == Game::Page::Attract; }
    static bool exercisesAttractInterstitialLifecycle(Game& game) {
        game.random_.seed(0x44454d4fu);
        game.startAttract();
        constexpr double interstitialSeconds =
            450.0 / OriginalSchedulerTicksPerSecond;

        // Force a wrong synthetic munch. The captured Prime demo likewise
        // ends by intentionally eating a wrong cell rather than solving it.
        Game::Cell& current = game.cell(game.playerRow_, game.playerColumn_);
        current = {};
        current.label = "1";
        game.activeBoardMode_ = Game::Mode::Primes;
        game.mode_ = Game::Mode::Primes;
        game.munch();
        game.update(0.26);
        constexpr double feedbackSeconds = 150.0 / OriginalSchedulerTicksPerSecond;
        constexpr double captureFrameSeconds = 31250.0 / 2190197.0;
        constexpr double postFeedbackBoardSeconds = 39.0 * captureFrameSeconds;
        constexpr double hallWipeSeconds = 7.0 * captureFrameSeconds;
        if (game.page_ != Game::Page::Feedback ||
            game.transitionTimer_ < feedbackSeconds - 0.001) return false;
        game.update(feedbackSeconds - 0.01);
        if (game.page_ != Game::Page::Feedback || game.attractPostFeedbackBoard_) return false;
        game.update(0.02);
        if (game.page_ != Game::Page::Feedback || !game.attractPostFeedbackBoard_ ||
            game.attractHallTransitionPhase_ !=
                Game::AttractHallTransitionPhase::BoardHold) return false;
        game.update(postFeedbackBoardSeconds - 0.01);
        if (game.page_ != Game::Page::Feedback ||
            game.attractHallTransitionPhase_ !=
                Game::AttractHallTransitionPhase::BoardHold) return false;
        game.update(0.02);
        if (game.page_ != Game::Page::Feedback ||
            game.attractHallTransitionPhase_ != Game::AttractHallTransitionPhase::Wipe ||
            game.attractHallWipeFrame_ != 0) return false;

        // The clean terminal samples the closing Wipe as it first reaches the
        // bottom edge. Verify that exact early staircase without depending on
        // the synthetic board contents used by this lifecycle test.
        Renderer baseline;
        game.attractHallTransitionPhase_ = Game::AttractHallTransitionPhase::BoardHold;
        game.render(baseline);
        Renderer wipe;
        game.attractHallTransitionPhase_ = Game::AttractHallTransitionPhase::Wipe;
        game.attractHallWipeFrame_ = 0;
        game.render(wipe);
        const auto pixel = [](const Renderer& rendered, const int x, const int y) {
            return rendered.pixels()[static_cast<std::size_t>(y) * Renderer::Width + x];
        };
        if (pixel(wipe, 0, 191) != pixel(baseline, 0, 191) ||
            pixel(wipe, 0, 192) != Colors::Cyan ||
            pixel(wipe, 11, 192) != pixel(baseline, 11, 192) ||
            pixel(wipe, 309, 195) != pixel(baseline, 309, 195) ||
            pixel(wipe, 310, 195) != Colors::Cyan ||
            pixel(wipe, 0, 199) != Colors::Cyan) return false;

        // Frames 1-5 are independent of the board underneath. Their complete
        // FNV hashes come directly from lossless frames 9006-9010.
        constexpr std::array<std::uint64_t, 5> capturedWipeHashes = {
            0x2cc687e766578183ull,
            0x2cc687e766578183ull,
            0x413bd71b36091f2bull,
            0xb1a8f948642db583ull,
            0xb1a8f948642db583ull,
        };
        for (int frame = 1; frame <= 5; ++frame) {
            game.attractHallWipeFrame_ = frame;
            if (fullFrameHash(game) !=
                capturedWipeHashes[static_cast<std::size_t>(frame - 1)]) return false;
        }

        game.attractHallTransitionPhase_ = Game::AttractHallTransitionPhase::Wipe;
        game.attractHallWipeFrame_ = 0;
        game.transitionTimer_ = captureFrameSeconds;
        game.update(hallWipeSeconds - 0.001);
        if (game.page_ != Game::Page::Feedback ||
            game.attractHallTransitionPhase_ != Game::AttractHallTransitionPhase::Wipe) {
            return false;
        }
        game.update(0.002);
        if (game.page_ != Game::Page::Hall || game.mode_ != Game::Mode::Primes ||
            std::abs(game.transitionTimer_ - interstitialSeconds) > 0.0001) return false;

        game.update(interstitialSeconds - 0.01);
        if (game.page_ != Game::Page::Hall) return false;
        game.update(0.02);
        if (game.page_ != Game::Page::Hall ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::HallToSplash ||
            game.attractInterstitialFrame_ != 0) return false;

        // Frames 7258-7262 no longer depend on the Hall underneath. Lock the
        // five complete frames while frame 7257's aperture is covered by the
        // captured-Hall hash gate below.
        constexpr std::array<std::uint64_t, 5> hallToSplashHashes = {
            0x2cc687e766578183ull,
            0x3f124d04d181aa5eull,
            0xf79c067ba1bc34f6ull,
            0xf75309bcac42bb83ull,
            0xa3e68e51e618e368ull,
        };
        for (int frame = 1; frame < 6; ++frame) {
            game.attractInterstitialFrame_ = frame;
            if (fullFrameHash(game) !=
                hallToSplashHashes[static_cast<std::size_t>(frame - 1)]) return false;
        }
        game.attractInterstitialFrame_ = 0;
        game.transitionTimer_ = captureFrameSeconds;
        for (int nextFrame = 1; nextFrame <= 6; ++nextFrame) {
            game.update(game.transitionTimer_ + 0.000001);
            if (nextFrame < 6) {
                if (game.attractInterstitialTransition_ !=
                        Game::AttractInterstitialTransition::HallToSplash ||
                    game.attractInterstitialFrame_ != nextFrame) return false;
            }
        }
        if (game.page_ != Game::Page::StartupSplash ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::None ||
            std::abs(game.transitionTimer_ - interstitialSeconds) > 0.0001) return false;

        // A key must also stop the score when the demo is on its logo page.
        game.keyDown(VK_SPACE);
        if (!game.attractMode_ || game.page_ == Game::Page::Title) return false;
        game.character(L' ');
        if (game.attractMode_ || game.page_ != Game::Page::Title || !audioStopped(game)) return false;

        game.startAttract();
        // The executable has no board-count branch in either main-screen
        // record or callback.  A third-board Hall must therefore retain the
        // same 450-tick Hall/logo pair and continue to board four.
        game.attractBoardIndex_ = 2;
        game.completeLevel();
        if (game.page_ != Game::Page::Attract ||
            game.attractHallTransitionPhase_ !=
                Game::AttractHallTransitionPhase::BoardHold) return false;
        game.update(postFeedbackBoardSeconds + hallWipeSeconds + 0.001);
        if (game.page_ != Game::Page::Hall) return false;
        game.update(game.transitionTimer_ + 0.000001);
        if (game.page_ != Game::Page::Hall ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::HallToSplash ||
            game.attractInterstitialFrame_ != 0) return false;
        for (int nextFrame = 1; nextFrame <= 6; ++nextFrame) {
            game.update(game.transitionTimer_ + 0.000001);
        }
        if (game.page_ != Game::Page::StartupSplash ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::None) return false;

        game.update(game.transitionTimer_ + 0.000001);
        if (game.page_ != Game::Page::StartupSplash ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::SplashToBoard ||
            game.attractInterstitialFrame_ != 0) return false;
        constexpr std::array<std::uint64_t, 7> splashToBoardHashes = {
            0xa804ae173352194cull,
            0x2cc687e766578183ull,
            0x2cc687e766578183ull,
            0x8887db592e3c9ca1ull,
            0xb1a8f948642db583ull,
            0xb1a8f948642db583ull,
            0xb1a8f948642db583ull,
        };
        for (int frame = 0; frame < 7; ++frame) {
            if (game.attractInterstitialFrame_ != frame ||
                fullFrameHash(game) !=
                    splashToBoardHashes[static_cast<std::size_t>(frame)]) return false;
            game.update(game.transitionTimer_ + 0.000001);
        }
        if (game.page_ != Game::Page::Attract || game.attractBoardIndex_ != 3 ||
            game.attractInterstitialFrame_ != 7) return false;
        game.update(game.transitionTimer_ + 0.000001);
        if (game.attractInterstitialFrame_ != 8) return false;
        game.update(game.transitionTimer_ + 0.000001);
        return game.page_ == Game::Page::Attract && game.attractBoardIndex_ == 3 &&
               game.attractInterstitialTransition_ ==
                   Game::AttractInterstitialTransition::None &&
               game.mode_ == game.activeBoardMode_ &&
               game.activeBoardMode_ != Game::Mode::Challenge &&
               game.correctRemaining_ >= 2 &&
               std::abs(game.attractActionTimer_ -
                        30.0 / OriginalSchedulerTicksPerSecond) < 0.0001;
    }
    static bool rendersCapturedAttractBoardPaint(Game& game) {
        prepareCapturedAttractHall(game);
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::HallToSplash;
        game.attractInterstitialFrame_ = 0;
        if (fullFrameHash(game) != 0x3bcbb335a57022d8ull) {
            return false;
        }

        game.page_ = Game::Page::Attract;
        game.attractMode_ = true;
        game.activeBoardMode_ = Game::Mode::Primes;
        game.mode_ = Game::Mode::Primes;
        game.playerRow_ = 3;
        game.playerColumn_ = 1;
        game.lives_ = 4;
        game.score_ = 0;
        game.moving_ = false;
        game.munching_ = false;
        game.deathAnimating_ = false;
        game.playerRecovering_ = false;
        game.enemyWarning_ = false;
        game.enemies_.clear();
        for (Game::Cell& current : game.cells_) current = {};
        constexpr std::array<std::string_view, Game::BoardCellCount> labels = {
            "2", "1", "2", "2", "1", "1",
            "1", "1", "1", "1", "2", "1",
            "1", "2", "1", "2", "2", "1",
            "1", "1", "2", "1", "2", "1",
            "1", "1", "2", "1", "1", "1",
        };
        for (std::size_t index = 0; index < labels.size(); ++index) {
            game.cells_[index].label = std::string(labels[index]);
        }
        game.cell(3, 1).eaten = true;

        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::SplashToBoard;
        game.attractInterstitialFrame_ = 7;
        const std::uint64_t firstPaintHash = fullFrameHash(game);
        if (firstPaintHash != 0x0dfdc39623c3439full) {
            std::cerr << "First torn paint hash 0x" << std::hex << firstPaintHash
                      << std::dec << '\n';
            return false;
        }
        game.attractInterstitialFrame_ = 8;
        const std::uint64_t secondPaintHash = fullFrameHash(game);
        if (secondPaintHash != 0x43e28d7e7b30136bull) {
            std::cerr << "Second torn paint hash 0x" << std::hex << secondPaintHash
                      << std::dec << '\n';
        }
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::None;
        const std::uint64_t stableHash = fullFrameHash(game);
        if (stableHash != 0x9c1938a9423716dfull) {
            std::cerr << "Stable captured Prime board hash 0x" << std::hex
                      << stableHash << std::dec << '\n';
        }
        return secondPaintHash == 0x43e28d7e7b30136bull &&
               stableHash == 0x9c1938a9423716dfull;
    }
    static bool exercisesRecoveredAttractController(Game& game) {
        game.random_.seed(0x085c1u);
        game.startAttract();
        game.safeZoneJobCount_ = 0;
        game.enemySlotCount_ = 0;
        game.enemies_.clear();

        // Image 0x085c1 always eats a positive (correct) current cell and
        // replaces the initial 30-tick delay with a 15-44-tick delay.
        Game::Cell& correct = game.cell(game.playerRow_, game.playerColumn_);
        correct = {};
        correct.label = "170";
        correct.correct = true;
        game.attractActionTimer_ = 0.0;
        game.updateAttractPlayer(0.0);
        if (!game.munching_ ||
            game.attractActionTimer_ < 15.0 / OriginalSchedulerTicksPerSecond ||
            game.attractActionTimer_ > 44.0 / OriginalSchedulerTicksPerSecond) return false;

        // The scheduler countdown continues during the chew actor animation.
        const double timerBeforeChew = game.attractActionTimer_;
        game.update(0.1);
        const double elapsedJobTicks =
            std::floor(0.1 * OriginalSchedulerTicksPerSecond) /
            OriginalSchedulerTicksPerSecond;
        if (!game.munching_ ||
            std::abs(game.attractActionTimer_ - (timerBeforeChew - elapsedJobTicks)) > 0.0001) {
            return false;
        }

        // On a blank, rotate clockwise from the random starting direction
        // until the sole adjacent positive cell is selected.
        game.munching_ = false;
        game.munchTimer_ = 0.0;
        game.munchCellIndex_ = -1;
        game.moving_ = false;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        for (Game::Cell& current : game.cells_) {
            current = {};
            current.eaten = true;
        }
        Game::Cell& left = game.cell(2, 1);
        left.eaten = false;
        left.label = "245";
        left.correct = true;
        game.attractActionTimer_ = 0.0;
        game.updateAttractPlayer(0.0);
        if (!game.moving_ || game.moveToRow_ != 2 || game.moveToColumn_ != 1 ||
            game.moveDirection_ != 3) return false;

        // Negative (incorrect) values use the recovered one-in-ten munch
        // branch; verify that both outcomes are reachable across seeds.
        bool sawWrongMunch = false;
        bool sawWrongMove = false;
        for (std::uint32_t seed = 0; seed < 256 && !(sawWrongMunch && sawWrongMove); ++seed) {
            game.random_.seed(seed);
            game.moving_ = false;
            game.munching_ = false;
            game.playerRow_ = 2;
            game.playerColumn_ = 2;
            for (Game::Cell& current : game.cells_) current = {};
            Game::Cell& wrong = game.cell(2, 2);
            wrong.label = "229";
            wrong.correct = false;
            wrong.eaten = false;
            game.attractActionTimer_ = 0.0;
            game.updateAttractPlayer(0.0);
            sawWrongMunch = sawWrongMunch || game.munching_;
            sawWrongMove = sawWrongMove || game.moving_;
        }
        return sawWrongMunch && sawWrongMove;
    }
    static bool replaysCapturedAttractThroughThirdBoard(Game& game) {
        static constexpr std::array<std::string_view, Game::BoardCellCount> labels = {
            "2", "1", "2", "2", "1", "1",
            "1", "1", "1", "1", "2", "1",
            "1", "2", "1", "2", "2", "1",
            "1", "",  "2", "1", "2", "1",
            "1", "1", "2", "1", "1", "1"};
        static constexpr std::array<std::string_view, Game::BoardCellCount> thirdLabels = {
            "9x3",  "30+0", "40-6", "8x5",  "78:3", "3x10",
            "2+28", "",     "34-4", "10x3", "33-3", "31-1",
            "10x3", "9+21", "8+25", "54:2", "14x2", "34-7",
            "4+30", "36-6", "28+2", "27x1", "90:3", "34-4",
            "28+2", "30x1", "39-9", "6x5",  "5+25", "30x1"};

        game.random_.seed(62853u);
        // Isolate the two supplied NM.CFG Hall records reached by this replay
        // from any per-user score file on the machine running the test.
        game.scores_[static_cast<std::size_t>(Game::Mode::Multiples)] = {
            {235, 1, "sdfg"}};
        game.scores_[static_cast<std::size_t>(Game::Mode::Equality)] = {
            {105, 1, "   .p;l,kjmhgfds"}};
        game.startAttract();
        const std::uint64_t firstBoardSetupCalls = game.random_.calls;
        std::ostringstream initialJobs;
        initialJobs << "First board level=" << game.level_ << " safeJobs="
                    << game.safeZoneJobCount_;
        for (int index = 0; index < game.safeZoneJobCount_; ++index) {
            const Game::SafeZoneJob& job = game.safeZoneJobs_[static_cast<std::size_t>(index)];
            initialJobs << " [" << index << " ticks="
                        << job.timer * OriginalSchedulerTicksPerSecond
                        << " active=" << job.active << " cell=" << job.cellIndex << ']';
        }
        constexpr double frameSeconds = 1.0 / 70.086;
        double elapsed = 0.0;
        std::uint64_t secondBoardInitializerStart = 0;
        bool firstCollisionFeedbackMatched = false;
        std::vector<std::uint64_t> firstCollisionPresentedHashes;
        bool firstCollisionCaptureCompleted = false;
        constexpr std::array<std::uint64_t, 23>
            ExpectedFirstCollisionPresentedHashes = {
                0xeaf1becc0fdbab4eull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull, 0x550d2185aa30525dull,
                0xd26de8addafe2fdeull,
                0x19cad4e7fdbfb3aaull,
            };
        std::uint64_t collisionTransientHash = 0;
        std::uint64_t collisionStableBoardHash = 0;
        std::array<std::uint64_t, 6> collisionWipeHashes{};
        std::vector<std::uint64_t> cannibalPresentedHashes;
        std::uint64_t cannibalPreBiteHash = 0;
        std::uint64_t cannibalTerminalHash = 0;
        bool cannibalActorPairMatched = false;
        bool cannibalTerminalActorMatched = false;
        bool cannibalWasActive = false;
        bool cannibalCompleted = false;
        int cannibalSurvivorSlot = -1;
        int cannibalOrdinal = 0;
        bool anyCannibalWasActive = false;
        std::uint64_t secondCannibalTerminalResidentHash = 0;
        std::uint64_t secondCannibalTerminalPresentedHash = 0;
        std::vector<std::uint64_t> secondCannibalPresentedHashes;
        bool secondCannibalActorPairMatched = false;
        bool secondCannibalTerminalActorMatched = false;
        int secondCannibalSurvivorSlot = -1;
        constexpr std::array<std::uint64_t, 8>
            expectedFirstBoardOpeningContinuousHashes = {
                0x8785f4ad483807bfull,
                0xb785ac15f2cd658full,
                0xff7bc8df6ccdb88bull,
                0xf1600cd2200eaa14ull,
                0x6662f01075e622a3ull,
                0xbee9a3d8e0784776ull,
                0x24e910f3a85fe37eull,
                0xf1c25756d2406cebull,
            };
        // Frames 2390-2544 are the actual start of the first Demo board.
        // These eight completed opening/player-move pages surround one
        // nonuniform adjacent-page scanout splice at frame 2459.
        std::vector<std::uint64_t> firstBoardOpeningContinuousHashes;
        bool firstBoardOpeningContinuousStarted = false;
        bool firstBoardOpeningContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 6>
            expectedCallbackLocalCompositeHashes = {
                0x0e8e08721d4058a5ull,
                0x8d7d65b430d3bb11ull,
                0x4828c2e029993611ull,
                0x3c602a2c866dfe39ull,
                0xe23a4b5428ad3debull,
                0xa420cc163f3eea11ull,
            };
        // These complete job-local pages fall between adjacent DOS capture
        // samples. Five are exact actor-layer partitions of their completed
        // neighbors; the final overlapping Reggie page is independently
        // locked by the later trail/top-exit audit.
        std::size_t callbackLocalCompositeIndex = 0;
        constexpr std::array<std::uint64_t, 27>
            expectedThirdCannibalCompleteSourceHashes = {
                0xcc6c4b7a76a2710full,
                0xc1f047009585a266ull,
                0xda6d6a442a74c319ull,
                0xfead76875493ba4aull,
                0xe3da789efaec1b46ull,
                0xc59da72ce12d16a0ull,
                0x63ee6b4832c22fb1ull,
                0xa6229786a14dd582ull,
                0x06cb6c4ea66608d1ull, 0x6152fafaa73d7232ull,
                0x06cb6c4ea66608d1ull, 0x6152fafaa73d7232ull,
                0x06cb6c4ea66608d1ull, 0x6152fafaa73d7232ull,
                0x06cb6c4ea66608d1ull, 0x6152fafaa73d7232ull,
                0x06cb6c4ea66608d1ull, 0x6152fafaa73d7232ull,
                0x06cb6c4ea66608d1ull,
                0xeebd145b457a21b5ull,
                0xa8b719ad18c85d7eull,
                0xbbdc43595d7184daull,
                0xad3fcf5f3575f07eull,
                0x176b4069b147951bull,
                0x6d680d9f028d2dc5ull,
                0x29ae0c41a86cb0a5ull,
                0xb63a35e2db896fdfull,
            };
        std::vector<std::uint64_t> thirdCannibalPresentedHashes;
        bool thirdCannibalCaptureStarted = false;
        bool thirdCannibalCaptureCompleted = false;
        constexpr std::array<std::uint64_t, 21>
            expectedPlayerReggieSafeBridgeHashes = {
                0x1dd62b4466906995ull,
                0x4491d303fb06fde5ull,
                0xa994bec9b912db7aull,
                0x4bc68b2d8d3dff6dull,
                0xe07a95288193372full,
                0x63325e023794cd27ull,
                0x8bf7d640704dc345ull,
                0xd8b272f47d6dfeadull,
                0x1a80add72279b16full,
                0x3b7b40f54a797986ull,
                0x76f512b461c48411ull,
                0x3edc38c1f41183d5ull,
                0x364d49a1b24d2eddull,
                0xd9690d6e3079143full,
                0x6fea9f28a7295153ull,
                0x296d98fcc27505a3ull,
                0x488ff75f480d9544ull,
                0x58cfb69f263882abull,
                0xc31b0b4841013f19ull,
                0x39ed0a3d7d8ba751ull,
                0x0c719b74227b74c3ull,
            };
        // Source frames 4162-4308 contain these twenty-one completed pages.
        // Seven intervening logical runs are measured incomplete dirty
        // paints: five break 2x physical duplication, while the uniform two
        // comprise a mixed terminal-to-idle player repaint and a partial
        // safe-zone outline. Native deliberately omits all seven.
        std::vector<std::uint64_t> playerReggieSafeBridgeHashes;
        bool playerReggieSafeBridgeStarted = false;
        bool playerReggieSafeBridgeCompleted = false;
        constexpr std::array<std::uint64_t, 49>
            expectedPreSecondCannibalBridgeHashes = {
                0x2767224657bc284full,
                0x9c2a34ed7ac568afull,
                0xc0a6ee9b27e67d09ull,
                0x90df701e79c254c5ull,
                0x0d01b843d88560dfull,
                0xcc0e0de1de8d656full,
                0xe0ee250696524029ull,
                0xcc99ba02210d20cfull,
                0xe3917a00434a9f77ull,
                0x08c27a61bed70c10ull,
                0xa8d57829a51640d7ull,
                0x54f057703239c603ull,
                0x4efebe8554e69903ull,
                0x3d4da5527806ad09ull,
                0x19522965df27480full,
                0xc9d124d301d24e04ull,
                0x0af77d261b7c3429ull,
                0xec454913a320012cull,
                0x995bf789e2207d33ull,
                0xe00329466e865a70ull,
                0xcf3fa2a1142620c1ull,
                0x4b22ec53dd3fc1b3ull,
                0x97dbcb7c05834298ull,
                0x365b10b7446ac591ull,
                0xcb439b81cde44070ull,
                0x13e1a3d2940bcb7full,
                0x29eb17d5e5431e70ull,
                0x7340fdff0fa99195ull,
                0xb47e78a1ab14b133ull,
                0x809893e4447a85caull,
                0xd0382b2402119bc8ull,
                0x5c0ddf923cbef276ull,
                0x5ca7eae85a0f7aa2ull,
                0x7074e37364ae5be3ull,
                0x45cf651612d405c2ull,
                0x891b04b3d9725d18ull,
                0xcb33014bd3990bcdull,
                0x427e52b8ee8f43b2ull,
                0x17fe3eab44623767ull,
                0x1517206cc8a288caull,
                0xe237d8caa169fc69ull,
                0x702158e1560e3cdfull,
                0xc4326a2a2cc654baull,
                0x3c47849e2be7a0bcull,
                0x655d549bbebb0701ull,
                0xf709cc6e27177085ull,
                0x25404dc95a0a250bull,
                0xc538f6953a848b99ull,
                0x10be6199d0450f51ull,
            };
        // Frames 4340-4652 contribute these forty-nine completed pages.
        // Ten intervening runs are pixel-proven dirty-paint fragments and
        // remain capture-only rather than becoming native flicker.
        std::vector<std::uint64_t> preSecondCannibalBridgeHashes;
        bool preSecondCannibalBridgeStarted = false;
        bool preSecondCannibalBridgeCompleted = false;
        constexpr std::array<std::uint64_t, 51>
            expectedPostSecondCannibalContinuousHashes = {
                0x6068127ed60caa9eull,
                0x4b06f95fd290f2d6ull,
                0x6068127ed60caa9eull,
                0x4b06f95fd290f2d6ull,
                0x6068127ed60caa9eull,
                0x4b06f95fd290f2d6ull,
                0xe3a2ecf270a39a63ull,
                0x700a9eb63b7f9ac3ull,
                0x55c104a7965e8e46ull,
                0xa3cab5651c5d758dull,
                0x954312a9b8857423ull,
                0xa84b12122a1c711bull,
                0xd341d2c43e2df1fdull,
                0x512ae94e92bacc53ull,
                0xe11ad48eb4e8a590ull,
                0x2b8d3f27ef095cd7ull,
                0x203babe12ee81521ull,
                0xea3a1ca245c9dff7ull,
                0x50c033cfcd2d6a04ull,
                0xe084f4dfc9b38c21ull,
                0xa56d41419e7e7115ull,
                0xf6907e0bc7ad5b8bull,
                0x41c53faeff24b63full,
                0x3f07b5ffd2c12c6full,
                0x4daecac897082870ull,
                0x87fd439d18c59c6full,
                0xcde78c4df6bf8cf2ull,
                0x6dcd686cc87887faull,
                0xa514da93f902a94dull,
                0x1c32203d8651b312ull,
                0xb43c4a001b66a851ull,
                0x62bbc32dd2f0b75dull,
                0x49d74dcb74aee599ull,
                0xf6907e0bc7ad5b8bull,
                0x6fd42d11fdc62815ull,
                0x97ba654ba5a6b086ull,
                0x7b5b1d7af2e81727ull,
                0xeec053901d6a935eull,
                0xfea658c2efdac9c1ull,
                0xe034ee4069d1c812ull,
                0xfc7e5a97228b2433ull,
                0xc8694767724b56bfull,
                0xd328f52c66e3676full,
                0x5bbd8b44c9fb7e38ull,
                0x7c890527a99ab6f7ull,
                0xdbc24a3454bad8ddull,
                0xd2a16f4d883be485ull,
                0x2e623256e8419d36ull,
                0xeeca540c25c21a4eull,
                0xac62fb5f0bb81428ull,
                0x7327515ab704a216ull,
            };
        // Frames 4706-5173 contain these 51 presentation-complete pages in
        // one uninterrupted sequence after the second Cannibal. Four
        // intervening one-frame surfaces are measured capture artifacts:
        // two nonuniform scanout/dirty splices and two uniform incomplete
        // 41-pixel row repaints. Native deliberately omits all four.
        std::vector<std::uint64_t> postSecondCannibalContinuousHashes;
        bool postSecondCannibalContinuousStarted = false;
        bool postSecondCannibalContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 14>
            expectedEarlyBottomEntryContinuousHashes = {
                0x4f73aa52e893ff06ull,
                0x5a20108e52669299ull,
                0x5ab9f311800d5403ull,
                0x9c56e9b502682745ull,
                0x0c5717288217e3a2ull,
                0xd68c66339f73174bull,
                0xb8270abed0e3bad1ull,
                0x396dcee5ce2eb935ull,
                0xb8270abed0e3bad1ull,
                0x9b285be3f8494708ull,
                0x4dcb586c49227eb7ull,
                0x596ca0baaab14f93ull,
                0x7d512488e348373cull,
                0xb5e9980aa54703c6ull,
            };
        // Frames 2840-2875 continuously cover the final upward-player page,
        // the selector-5 chew, the resident Reggie entry, and the overlapping
        // Bashful entry. Frames 2851 and 2870 are incomplete dirty repaints;
        // these are the fourteen presentation-complete pages around them.
        std::vector<std::uint64_t> earlyBottomEntryContinuousHashes;
        bool earlyBottomEntryContinuousStarted = false;
        bool earlyBottomEntryContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 10>
            expectedFirstCannibalApproachContinuousHashes = {
                0xcd4b6197aaff42d8ull,
                0xbb5f1d6dbff1882full,
                0xae867fe9848dd2abull,
                0x27348e311a6a7c8eull,
                0xe44c66f6f7f5c1bdull,
                0xa1cdfa908f89e64full,
                0xcae38fcdd3cc6cbdull,
                0x9d81a6334d541956ull,
                0xf4814097833d3b3bull,
                0x378e270f4598ac61ull,
            };
        // Frames 2919-2944 continuously cover the Demo player's left walk,
        // a simultaneous right-entry repaint, and the stable pre-cannibal
        // page. Frame 2940 is the sole incomplete doubled-block refresh.
        std::vector<std::uint64_t> firstCannibalApproachContinuousHashes;
        bool firstCannibalApproachContinuousStarted = false;
        bool firstCannibalApproachContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 8>
            expectedPlayerWarningBoundaryContinuousHashes = {
                0x83cf33fc2f2b040dull,
                0x8debf5981044e557ull,
                0x0dddbb1637f7e038ull,
                0xb7d877b492c6d1ecull,
                0xa060590d41749ebcull,
                0x66da4d52cc2cfecaull,
                0x7ae3dde61c3ddeb9ull,
                0x0e8bc09c389491d2ull,
            };
        // Frames 3300-3340 continuously cover the six rightward player
        // positions, its terminal callback before a later warning slot, and
        // the following standing/warning page. Run 2 contains one torn host
        // frame but also two complete uniform copies of the gated page.
        std::vector<std::uint64_t> playerWarningBoundaryContinuousHashes;
        bool playerWarningBoundaryContinuousStarted = false;
        bool playerWarningBoundaryContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 15>
            expectedFirstBoardChewEntryBoundaryHashes = {
                0x3d3db73670066f0aull,
                0x8a48d1dd7cb50364ull,
                0x1683bb2c4202d4a3ull,
                0x9142307dc36a7501ull,
                0x383516f5cf1a8d6full,
                0x8e1c5070fc3883a3ull,
                0x383516f5cf1a8d6full,
                0x6e040c01569e47cdull,
                0x9d3a5c2fa8506fd9ull,
                0xdb89baaff479c4ffull,
                0x0d4ca7ed5432e65dull,
                0xbef955a089d70533ull,
                0x065e4dc3d00563a3ull,
                0x0f5e344e7a5c6143ull,
                0x3d13c3bdf171e891ull,
            };
        // Frames 3520-3606 continuously cover the pre-chew dwell, a
        // bottom-left entry terminal callback, all seven chew intervals, the
        // warning removal, and a new right-edge entry. One host frame in the
        // 3566-3568 run is torn; uniform copies establish the complete page.
        std::vector<std::uint64_t> firstBoardChewEntryBoundaryHashes;
        bool firstBoardChewEntryBoundaryStarted = false;
        bool firstBoardChewEntryBoundaryCompleted = false;
        constexpr std::array<std::uint64_t, 11>
            expectedLaterBottomEntryContinuousHashes = {
                0x7327515ab704a216ull,
                0x4f785c11832caccaull,
                0x62ef78163a5c73fdull,
                0xbc9e4db839758068ull,
                0x08b631a66e5d0f0full,
                0x90456223812679d4ull,
                0x9c0190c31703db30ull,
                0xbcaa9e6d0c9ce28bull,
                0xe1a2c8d0cb0630ebull,
                0xd35094db8e41f03cull,
                0x364c0086fe50ea46ull,
            };
        // Frames 5173-5215 contain these eleven presentation-complete pages.
        // The uniform frame 5184 is a one-row incomplete player repaint and
        // frame 5196 contains twenty nonuniform doubled blocks; both remain
        // capture-only.
        std::vector<std::uint64_t> laterBottomEntryContinuousHashes;
        bool laterBottomEntryContinuousStarted = false;
        bool laterBottomEntryContinuousCompleted = false;
        constexpr std::array<std::uint64_t, 7>
            expectedLaterBottomExitContinuousHashes = {
                0xe572978e3378b63cull,
                0x889c0f21b7d634d5ull,
                0x8f14e90902d728c7ull,
                0xb55be53e20024918ull,
                0xe4fec8a6974cfe71ull,
                0x8abe6082e3593b9full,
                0xfe9afa1a084c2395ull,
            };
        // Frames 5687-5718 continuously cover the final dwell, five clipped
        // exit poses, and actor-removal page while the Demo player starts its
        // upward move. Frame 5711 is a capture-only scanline splice.
        std::vector<std::uint64_t> laterBottomExitContinuousHashes;
        bool laterBottomExitContinuousStarted = false;
        bool laterBottomExitContinuousCompleted = false;
        // The one-frame 0x0806cb7dc6681e51 surface between the first two
        // hashes restores only four x=308 pixels before the next actor paint
        // overwrites them. It is a uniform capture of an incomplete dirty
        // repaint, not a completed movement callback page.
        constexpr std::array<std::uint64_t, 6>
            expectedBashfulRightEntryHashes = {
                0x78ba03ad58531cf9ull,
                0x5cb927dc8aeb3669ull,
                0xb3b10c32deee6853ull,
                0x78b3a87806e074bfull,
                0xdda5874eef639e5full,
                0x3225d1e44387aa7dull,
            };
        std::vector<std::uint64_t> bashfulRightEntryHashes;
        bool bashfulRightEntryStarted = false;
        bool bashfulRightEntryCompleted = false;
        constexpr std::array<std::uint64_t, 8>
            expectedLaterBashfulLeftTrailHashes = {
                0xb63a35e2db896fdfull,
                0xbbfc9aeb9be72ddbull,
                0xd2b7ae9542c00588ull,
                0x69c5a4df86f95156ull,
                0xc0d1489ab64eca36ull,
                0xea113826b1a5efd9ull,
                0xe5aaf2ea06cd3586ull,
                0xf6fcbdff7ec65128ull,
            };
        std::vector<std::uint64_t> laterBashfulLeftTrailHashes;
        bool laterBashfulLeftTrailStarted = false;
        bool laterBashfulLeftTrailCompleted = false;
        constexpr std::array<std::uint64_t, 15>
            expectedReggieDownSafeEntryHashes = {
                0xf6fcbdff7ec65128ull,
                0xc9b6c6488a99cfdaull,
                0xd08f888bb4309eceull,
                0x034bd44a3d090056ull,
                0xeb1b9db942524bb6ull,
                0xc2e6a0dc4a981512ull,
                0x002051f0a10a9b6eull,
                0xc26825aa87eb9654ull,
                0x7f60864972d563efull,
                0xffaf14433fb4ea38ull,
                0x41bae93c9cd82772ull,
                0xb481e31d458d5008ull,
                0x9d94cc7c50eeb633ull,
                0x3506fdb321ea1e12ull,
                0x257ca66a2aac6e80ull,
            };
        // Source frames 5446-5659 contain these fifteen completed pages in
        // order. Frame 5598 is a nonuniform incomplete repaint and is absent.
        // The two boundary hashes are shared with the adjacent left-trail and
        // right-approach gates. In particular, no safe-zone-only phase-zero
        // page may appear between destination dwell and player phase 1.
        std::vector<std::uint64_t> reggieDownSafeEntryHashes;
        bool reggieDownSafeEntryStarted = false;
        bool reggieDownSafeEntryCompleted = false;
        constexpr std::array<std::uint64_t, 15>
            expectedChewTopEntryHashes = {
                0x364c0086fe50ea46ull,
                0x88a6bc3c750f5d31ull,
                0xc49574c442045281ull,
                0x88a6bc3c750f5d31ull,
                0xc49574c442045281ull,
                0x88a6bc3c750f5d31ull,
                0xc49574c442045281ull,
                0x88a6bc3c750f5d31ull,
                0xc49574c442045281ull,
                0x9109d9df3ffef78bull,
                0x00652cf1be948be5ull,
                0x4cea0c8dbfc9b06full,
                0x0d7cf12c93e39149ull,
                0xb527905804bb3ea7ull,
                0x9109d9df3ffef78bull,
            };
        // Frames 5216-5331 contain these completed pages. Exact scanout
        // splices at physical rows 340 and 236 and one nonuniform entry
        // repaint are capture-only and deliberately absent.
        std::vector<std::uint64_t> chewTopEntryHashes;
        bool chewTopEntryStarted = false;
        bool chewTopEntryCompleted = false;
        std::vector<std::uint64_t> bashfulTrailPresentedHashes;
        bool bashfulTrailCaptureStarted = false;
        bool bashfulTrailCaptureCompleted = false;
        std::vector<std::uint64_t> reggieTrailPresentedHashes;
        bool reggieTrailCaptureStarted = false;
        bool reggieTrailCaptureCompleted = false;
        std::vector<std::uint64_t> secondBashfulTrailPresentedHashes;
        bool secondBashfulTrailCaptureStarted = false;
        bool secondBashfulTrailCaptureCompleted = false;
        constexpr std::array<std::uint64_t, 14>
            expectedLaterTrailTopExitSourceHashes = {
                0x56588d1c3c07b155ull,
                0x2b8a6091bbc16843ull,
                0x9687ed7086b3ed13ull,
                0xf57ae1c0a5f12b77ull,
                0x5a1c5815fff1d413ull,
                0x123dfd255115d1a3ull,
                0xc84b45ed1fa8ba43ull,
                0xc58469c69de839c9ull,
                0x13ae5f670e4dda67ull,
                0xa0f217f26983da49ull,
                0x9a5f740b9cc0fdd5ull,
                0xf55632728d63a0a0ull,
                0x7db49d2a001bf27full,
                0x46881471ff4615dfull,
            };
        constexpr std::array<std::uint64_t, 16>
            expectedLaterTrailTopExitNativeHashes = {
                0x56588d1c3c07b155ull,
                0xe23a4b5428ad3debull,
                0x2b8a6091bbc16843ull,
                0x9687ed7086b3ed13ull,
                0xf57ae1c0a5f12b77ull,
                0x5a1c5815fff1d413ull,
                0x123dfd255115d1a3ull,
                0xc84b45ed1fa8ba43ull,
                0xa420cc163f3eea11ull,
                0xc58469c69de839c9ull,
                0x13ae5f670e4dda67ull,
                0xa0f217f26983da49ull,
                0x9a5f740b9cc0fdd5ull,
                0xf55632728d63a0a0ull,
                0x7db49d2a001bf27full,
                0x46881471ff4615dfull,
            };
        std::vector<std::uint64_t> laterTrailTopExitSourceMatches;
        std::vector<std::uint64_t> laterTrailTopExitNativeHashes;
        bool laterTrailTopExitNativeStarted = false;
        bool laterTrailTopExitNativeCompleted = false;
        constexpr std::array<std::uint64_t, 8> expectedFirstDemoChewHashes = {
            0x629739525e247327ull,
            0x8facc08aff61688full,
            0x629739525e247327ull,
            0x8facc08aff61688full,
            0x629739525e247327ull,
            0x8facc08aff61688full,
            0x629739525e247327ull,
            0x8facc08aff61688full,
        };
        std::vector<std::uint64_t> firstDemoChewHashes;
        bool firstDemoChewStarted = false;
        constexpr std::array<std::uint64_t, 9>
            expectedFirstDemoChewRightExitHashes = {
                0xd0a7034db4c37224ull,
                0x1e7bb5780b03c6a7ull,
                0x2c32e321d97f24abull,
                0xc04ce8b24d4ae276ull,
                0x6ac19dd06e550acdull,
                0x91470bdd52ee50efull,
                0xea530a5b4a61d313ull,
                0xe52d6002157a9fb5ull,
                0x693dd0c1ea6dd7c9ull,
            };
        std::vector<std::uint64_t> firstDemoChewRightExitHashes;
        bool firstDemoChewRightExitStarted = false;
        constexpr std::array<std::uint64_t, 9>
            expectedFirstDemoDepartingEnemyOverlapHashes = {
                0xb555a565272c576eull,
                0xfbdb404c2920434full,
                0xf76f906d9e9b233eull,
                0xc7bac9d6d2402c5bull,
                0xb9fa6d42d3af4de3ull,
                0x206150b8f750c989ull,
                0xcc3707259e7c2f75ull,
                0x6f5e7efaa3b03fd5ull,
                0xdd01e529ca1c4643ull,
            };
        std::vector<std::uint64_t>
            firstDemoDepartingEnemyOverlapHashes;
        bool firstDemoDepartingEnemyOverlapStarted = false;
        constexpr std::array<std::uint64_t, 24>
            expectedFirstDemoPostDepartureContinuousHashes = {
                0xdd01e529ca1c4643ull,
                0x629739525e247327ull,
                0x8facc08aff61688full,
                0x629739525e247327ull,
                0x8facc08aff61688full,
                0x629739525e247327ull,
                0x8facc08aff61688full,
                0x629739525e247327ull,
                0x8facc08aff61688full,
                0x3b336c4f9b082f55ull,
                0x49b6c343a28bbdc5ull,
                0xf9b4d332c091913bull,
                0x180ac3bf15df9bd7ull,
                0xd6a3242e658ba2f5ull,
                0x6de23c5730908925ull,
                0xa3d6fed9d8ca19ffull,
                0xd037b3e0a3204d15ull,
                0xd67042a6fc52f892ull,
                0x59ea5889fa908926ull,
                0x3ce3bab96ff221f9ull,
                0x58b1872b25bfd9ceull,
                0x479eef29297d97edull,
                0x66cc834de63cb2e2ull,
                0xd0a7034db4c37224ull,
            };
        std::vector<std::uint64_t>
            firstDemoPostDepartureContinuousHashes;
        bool firstDemoPostDepartureContinuousStarted = false;
        const bool auditFirstBoardPresentationSequence =
            std::getenv("MUNCHERS_AUDIT_FIRST_BOARD_SEQUENCE") != nullptr;
        const char* firstBoardPageDumpDirectoryText =
            std::getenv("MUNCHERS_AUDIT_FIRST_BOARD_PAGE_DIR");
        const bool dumpFirstBoardPages =
            firstBoardPageDumpDirectoryText && *firstBoardPageDumpDirectoryText;
        const std::filesystem::path firstBoardPageDumpDirectory =
            dumpFirstBoardPages
                ? std::filesystem::path(firstBoardPageDumpDirectoryText)
                : std::filesystem::path{};
        if (dumpFirstBoardPages) {
            std::error_code error;
            std::filesystem::create_directories(
                firstBoardPageDumpDirectory, error);
            if (error) return false;
        }
        std::vector<std::uint64_t> firstBoardPresentationSequence;
        std::vector<int> firstBoardPresentationStartFrames;
        int firstBoardPresentationFrame = 0;
        std::uint64_t previousFirstBoardHash = fullFrameHash(game);
        Renderer trailPresentationRenderer(game.assets_.graphicsMode());
        std::vector<std::string> replayTrace;
        std::string boardBeforeTerminal;
        while (game.attractMode_ && game.attractBoardIndex_ == 0 && elapsed < 180.0) {
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            const Game::AttractHallTransitionPhase previousHallPhase =
                game.attractHallTransitionPhase_;
            game.update(frameSeconds);
            elapsed += frameSeconds;
            game.renderPresentation(trailPresentationRenderer);
            const std::uint64_t presentedFirstBoardHash =
                renderedFrameHash(trailPresentationRenderer);
            const bool firstBoardPresentationChanged =
                firstBoardPresentationSequence.empty() ||
                firstBoardPresentationSequence.back() !=
                    presentedFirstBoardHash;
            if (auditFirstBoardPresentationSequence &&
                firstBoardPresentationChanged) {
                firstBoardPresentationSequence.push_back(
                    presentedFirstBoardHash);
                firstBoardPresentationStartFrames.push_back(
                    firstBoardPresentationFrame);
            }
            if (dumpFirstBoardPages && firstBoardPresentationChanged) {
                if (std::find(expectedCallbackLocalCompositeHashes.begin(),
                              expectedCallbackLocalCompositeHashes.end(),
                              presentedFirstBoardHash) !=
                    expectedCallbackLocalCompositeHashes.end()) {
                    std::ostringstream name;
                    name << std::hex << presentedFirstBoardHash << std::dec
                         << '-' << firstBoardPresentationFrame << ".ppm";
                    std::ofstream output(firstBoardPageDumpDirectory / name.str(),
                                         std::ios::binary);
                    if (!output) return false;
                    output << "P6\n" << Renderer::Width << ' '
                           << Renderer::Height << "\n255\n";
                    for (const std::uint32_t pixel :
                         trailPresentationRenderer.pixels()) {
                        const char rgb[3] = {
                            static_cast<char>((pixel >> 16) & 0xffu),
                            static_cast<char>((pixel >> 8) & 0xffu),
                            static_cast<char>(pixel & 0xffu),
                        };
                        output.write(rgb, sizeof(rgb));
                    }
                    if (!output) return false;
                }
            }
            const std::uint64_t currentFirstBoardHash = fullFrameHash(game);
            if (callbackLocalCompositeIndex <
                    expectedCallbackLocalCompositeHashes.size() &&
                presentedFirstBoardHash ==
                    expectedCallbackLocalCompositeHashes[
                        callbackLocalCompositeIndex]) {
                ++callbackLocalCompositeIndex;
            }
            if (!firstBoardOpeningContinuousCompleted &&
                (firstBoardOpeningContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedFirstBoardOpeningContinuousHashes.front())) {
                firstBoardOpeningContinuousStarted = true;
                if (firstBoardOpeningContinuousHashes.empty() ||
                    firstBoardOpeningContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    firstBoardOpeningContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedFirstBoardOpeningContinuousHashes.back()) {
                    firstBoardOpeningContinuousCompleted = true;
                }
            }
            if (!firstCollisionCaptureCompleted &&
                game.page_ == Game::Page::Feedback &&
                game.feedbackKind_ == Game::FeedbackKind::EatenByTroggle) {
                if (firstCollisionPresentedHashes.empty() ||
                    firstCollisionPresentedHashes.back() !=
                        presentedFirstBoardHash) {
                    firstCollisionPresentedHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (!game.deathAnimating_) {
                    firstCollisionCaptureCompleted = true;
                }
            }
            if (!bashfulRightEntryCompleted &&
                (bashfulRightEntryStarted ||
                 presentedFirstBoardHash ==
                     expectedBashfulRightEntryHashes.front())) {
                bashfulRightEntryStarted = true;
                if (bashfulRightEntryHashes.empty() ||
                    bashfulRightEntryHashes.back() != presentedFirstBoardHash) {
                    bashfulRightEntryHashes.push_back(presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedBashfulRightEntryHashes.back()) {
                    bashfulRightEntryCompleted = true;
                }
            }
            if (!playerReggieSafeBridgeCompleted &&
                (playerReggieSafeBridgeStarted ||
                 presentedFirstBoardHash ==
                     expectedPlayerReggieSafeBridgeHashes.front())) {
                playerReggieSafeBridgeStarted = true;
                if (playerReggieSafeBridgeHashes.empty() ||
                    playerReggieSafeBridgeHashes.back() !=
                        presentedFirstBoardHash) {
                    playerReggieSafeBridgeHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedPlayerReggieSafeBridgeHashes.back()) {
                    playerReggieSafeBridgeCompleted = true;
                }
            }
            if (!preSecondCannibalBridgeCompleted &&
                (preSecondCannibalBridgeStarted ||
                 presentedFirstBoardHash ==
                     expectedPreSecondCannibalBridgeHashes.front())) {
                preSecondCannibalBridgeStarted = true;
                if (preSecondCannibalBridgeHashes.empty() ||
                    preSecondCannibalBridgeHashes.back() !=
                        presentedFirstBoardHash) {
                    preSecondCannibalBridgeHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedPreSecondCannibalBridgeHashes.back()) {
                    preSecondCannibalBridgeCompleted = true;
                }
            }
            if (!postSecondCannibalContinuousCompleted &&
                (postSecondCannibalContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedPostSecondCannibalContinuousHashes.front())) {
                postSecondCannibalContinuousStarted = true;
                if (postSecondCannibalContinuousHashes.empty() ||
                    postSecondCannibalContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    postSecondCannibalContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedPostSecondCannibalContinuousHashes.back()) {
                    postSecondCannibalContinuousCompleted = true;
                }
            }
            if (!earlyBottomEntryContinuousCompleted &&
                (earlyBottomEntryContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedEarlyBottomEntryContinuousHashes.front())) {
                earlyBottomEntryContinuousStarted = true;
                if (earlyBottomEntryContinuousHashes.empty() ||
                    earlyBottomEntryContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    earlyBottomEntryContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedEarlyBottomEntryContinuousHashes.back()) {
                    earlyBottomEntryContinuousCompleted = true;
                }
            }
            if (!firstCannibalApproachContinuousCompleted &&
                (firstCannibalApproachContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedFirstCannibalApproachContinuousHashes.front())) {
                firstCannibalApproachContinuousStarted = true;
                if (firstCannibalApproachContinuousHashes.empty() ||
                    firstCannibalApproachContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    firstCannibalApproachContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedFirstCannibalApproachContinuousHashes.back()) {
                    firstCannibalApproachContinuousCompleted = true;
                }
            }
            if (!playerWarningBoundaryContinuousCompleted &&
                (playerWarningBoundaryContinuousStarted ||
                 (presentedFirstBoardHash ==
                      expectedPlayerWarningBoundaryContinuousHashes.front() &&
                  !game.munching_ && game.playerRow_ == 0 &&
                  game.playerColumn_ == 1))) {
                playerWarningBoundaryContinuousStarted = true;
                if (playerWarningBoundaryContinuousHashes.empty() ||
                    playerWarningBoundaryContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    playerWarningBoundaryContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedPlayerWarningBoundaryContinuousHashes.back()) {
                    playerWarningBoundaryContinuousCompleted = true;
                }
            }
            if (!firstBoardChewEntryBoundaryCompleted &&
                (firstBoardChewEntryBoundaryStarted ||
                 (presentedFirstBoardHash ==
                      expectedFirstBoardChewEntryBoundaryHashes.front() &&
                  !game.munching_ && game.playerRow_ == 1 &&
                  game.playerColumn_ == 3))) {
                firstBoardChewEntryBoundaryStarted = true;
                if (firstBoardChewEntryBoundaryHashes.empty() ||
                    firstBoardChewEntryBoundaryHashes.back() !=
                        presentedFirstBoardHash) {
                    firstBoardChewEntryBoundaryHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedFirstBoardChewEntryBoundaryHashes.back()) {
                    firstBoardChewEntryBoundaryCompleted = true;
                }
            }
            if (!laterBottomEntryContinuousCompleted &&
                (laterBottomEntryContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedLaterBottomEntryContinuousHashes.front())) {
                laterBottomEntryContinuousStarted = true;
                if (laterBottomEntryContinuousHashes.empty() ||
                    laterBottomEntryContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    laterBottomEntryContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedLaterBottomEntryContinuousHashes.back()) {
                    laterBottomEntryContinuousCompleted = true;
                }
            }
            if (!laterBottomExitContinuousCompleted &&
                (laterBottomExitContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedLaterBottomExitContinuousHashes.front())) {
                laterBottomExitContinuousStarted = true;
                if (laterBottomExitContinuousHashes.empty() ||
                    laterBottomExitContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    laterBottomExitContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedLaterBottomExitContinuousHashes.back()) {
                    laterBottomExitContinuousCompleted = true;
                }
            }
            if (!laterBashfulLeftTrailCompleted &&
                (laterBashfulLeftTrailStarted ||
                 presentedFirstBoardHash ==
                     expectedLaterBashfulLeftTrailHashes.front())) {
                laterBashfulLeftTrailStarted = true;
                if (laterBashfulLeftTrailHashes.empty() ||
                    laterBashfulLeftTrailHashes.back() !=
                        presentedFirstBoardHash) {
                    laterBashfulLeftTrailHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedLaterBashfulLeftTrailHashes.back()) {
                    laterBashfulLeftTrailCompleted = true;
                }
            }
            if (!reggieDownSafeEntryCompleted &&
                (reggieDownSafeEntryStarted ||
                 presentedFirstBoardHash ==
                     expectedReggieDownSafeEntryHashes.front())) {
                reggieDownSafeEntryStarted = true;
                if (reggieDownSafeEntryHashes.empty() ||
                    reggieDownSafeEntryHashes.back() !=
                        presentedFirstBoardHash) {
                    reggieDownSafeEntryHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                    expectedReggieDownSafeEntryHashes.back()) {
                    reggieDownSafeEntryCompleted = true;
                }
            }
            if (!chewTopEntryCompleted &&
                (chewTopEntryStarted ||
                 presentedFirstBoardHash ==
                     expectedChewTopEntryHashes.front())) {
                chewTopEntryStarted = true;
                if (chewTopEntryHashes.empty() ||
                    chewTopEntryHashes.back() != presentedFirstBoardHash) {
                    chewTopEntryHashes.push_back(presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash ==
                        expectedChewTopEntryHashes.back() &&
                    std::find(chewTopEntryHashes.begin(),
                              chewTopEntryHashes.end(),
                              expectedChewTopEntryHashes[
                                  expectedChewTopEntryHashes.size() - 2]) !=
                        chewTopEntryHashes.end()) {
                    chewTopEntryCompleted = true;
                }
            }
            if (!bashfulTrailCaptureCompleted &&
                (bashfulTrailCaptureStarted ||
                 presentedFirstBoardHash == 0x184234cd0b3459dfull)) {
                bashfulTrailCaptureStarted = true;
                if (bashfulTrailPresentedHashes.empty() ||
                    bashfulTrailPresentedHashes.back() != presentedFirstBoardHash) {
                    bashfulTrailPresentedHashes.push_back(presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash == 0xfb75ae4b0f01f04aull) {
                    bashfulTrailCaptureCompleted = true;
                }
            }
            if (!reggieTrailCaptureCompleted &&
                (reggieTrailCaptureStarted ||
                 presentedFirstBoardHash == 0xf492558db25be420ull)) {
                reggieTrailCaptureStarted = true;
                if (reggieTrailPresentedHashes.empty() ||
                    reggieTrailPresentedHashes.back() != presentedFirstBoardHash) {
                    reggieTrailPresentedHashes.push_back(presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash == 0x8aed25e1d63b0451ull) {
                    reggieTrailCaptureCompleted = true;
                }
            }
            if (!secondBashfulTrailCaptureCompleted &&
                (secondBashfulTrailCaptureStarted ||
                 presentedFirstBoardHash == 0xa6c7a8866bcae5f5ull)) {
                secondBashfulTrailCaptureStarted = true;
                if (secondBashfulTrailPresentedHashes.empty() ||
                    secondBashfulTrailPresentedHashes.back() !=
                        presentedFirstBoardHash) {
                    secondBashfulTrailPresentedHashes.push_back(
                        presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash == 0x8d188d13d9079609ull) {
                    secondBashfulTrailCaptureCompleted = true;
                }
            }
            // DOSBox-X exposes two incomplete refreshes during the following
            // Reggie crossing. Native deliberately presents complete callback
            // frames, so lock every complete source state as an ordered
            // subsequence instead of synthesizing those capture tears.
            if (laterTrailTopExitSourceMatches.size() <
                    expectedLaterTrailTopExitSourceHashes.size() &&
                presentedFirstBoardHash == expectedLaterTrailTopExitSourceHashes[
                    laterTrailTopExitSourceMatches.size()]) {
                laterTrailTopExitSourceMatches.push_back(presentedFirstBoardHash);
            }
            if (!laterTrailTopExitNativeCompleted &&
                (laterTrailTopExitNativeStarted ||
                 presentedFirstBoardHash == 0x56588d1c3c07b155ull)) {
                laterTrailTopExitNativeStarted = true;
                if (laterTrailTopExitNativeHashes.empty() ||
                    laterTrailTopExitNativeHashes.back() != presentedFirstBoardHash) {
                    laterTrailTopExitNativeHashes.push_back(presentedFirstBoardHash);
                }
                if (presentedFirstBoardHash == 0x46881471ff4615dfull) {
                    laterTrailTopExitNativeCompleted = true;
                }
            }
            if (firstDemoChewHashes.size() < expectedFirstDemoChewHashes.size() &&
                (firstDemoChewStarted ||
                 presentedFirstBoardHash == expectedFirstDemoChewHashes.front())) {
                firstDemoChewStarted = true;
                if (firstDemoChewHashes.empty() ||
                    firstDemoChewHashes.back() != presentedFirstBoardHash) {
                    firstDemoChewHashes.push_back(presentedFirstBoardHash);
                }
            }
            if (firstDemoChewRightExitHashes.size() <
                    expectedFirstDemoChewRightExitHashes.size() &&
                (firstDemoChewRightExitStarted ||
                 presentedFirstBoardHash ==
                     expectedFirstDemoChewRightExitHashes.front())) {
                firstDemoChewRightExitStarted = true;
                if (firstDemoChewRightExitHashes.empty() ||
                    firstDemoChewRightExitHashes.back() !=
                        presentedFirstBoardHash) {
                    firstDemoChewRightExitHashes.push_back(
                        presentedFirstBoardHash);
                }
            }
            if (firstDemoDepartingEnemyOverlapHashes.size() <
                    expectedFirstDemoDepartingEnemyOverlapHashes.size() &&
                (firstDemoDepartingEnemyOverlapStarted ||
                 presentedFirstBoardHash ==
                     expectedFirstDemoDepartingEnemyOverlapHashes.front())) {
                firstDemoDepartingEnemyOverlapStarted = true;
                if (firstDemoDepartingEnemyOverlapHashes.empty() ||
                    firstDemoDepartingEnemyOverlapHashes.back() !=
                        presentedFirstBoardHash) {
                    firstDemoDepartingEnemyOverlapHashes.push_back(
                        presentedFirstBoardHash);
                }
            }
            if (firstDemoPostDepartureContinuousHashes.size() <
                    expectedFirstDemoPostDepartureContinuousHashes.size() &&
                (firstDemoPostDepartureContinuousStarted ||
                 presentedFirstBoardHash ==
                     expectedFirstDemoPostDepartureContinuousHashes.front())) {
                firstDemoPostDepartureContinuousStarted = true;
                if (firstDemoPostDepartureContinuousHashes.empty() ||
                    firstDemoPostDepartureContinuousHashes.back() !=
                        presentedFirstBoardHash) {
                    firstDemoPostDepartureContinuousHashes.push_back(
                        presentedFirstBoardHash);
                }
            }
            const auto cannibal = std::find_if(
                game.enemies_.begin(), game.enemies_.end(), [](const Game::Enemy& enemy) {
                    return enemy.cannibalizing;
                });
            const bool anyCannibalActive = cannibal != game.enemies_.end();
            if (anyCannibalActive && !anyCannibalWasActive) {
                ++cannibalOrdinal;
                if (cannibalOrdinal == 2) {
                    secondCannibalSurvivorSlot = cannibal->slot;
                }
            }
            if (anyCannibalActive && cannibalOrdinal == 2) {
                if (secondCannibalPresentedHashes.empty() ||
                    secondCannibalPresentedHashes.back() != presentedFirstBoardHash) {
                    secondCannibalPresentedHashes.push_back(presentedFirstBoardHash);
                }
                const auto victim = std::find_if(
                    game.enemies_.begin(), game.enemies_.end(),
                    [&](const Game::Enemy& enemy) {
                        return &enemy != &*cannibal && enemy.overlapFrozen &&
                               enemy.collisionHidden && enemy.row == cannibal->row &&
                               enemy.column == cannibal->column;
                    });
                secondCannibalActorPairMatched = secondCannibalActorPairMatched ||
                    (cannibal->type == 0 && cannibal->row == 4 &&
                     cannibal->column == 5 && victim != game.enemies_.end() &&
                     victim->type == 0);
            } else if (!anyCannibalActive && anyCannibalWasActive &&
                       cannibalOrdinal == 2 && secondCannibalTerminalResidentHash == 0) {
                secondCannibalTerminalResidentHash = currentFirstBoardHash;
                secondCannibalTerminalPresentedHash = presentedFirstBoardHash;
                const auto survivor = std::find_if(
                    game.enemies_.begin(), game.enemies_.end(), [&](const Game::Enemy& enemy) {
                        return enemy.slot == secondCannibalSurvivorSlot;
                    });
                secondCannibalTerminalActorMatched =
                    survivor != game.enemies_.end() && survivor->type == 0 &&
                    survivor->row == 4 && survivor->column == 5 &&
                    survivor->direction == 1 && survivor->dwellFrame == 15 &&
                    !survivor->moving && !survivor->entering;
            }
            if (!thirdCannibalCaptureCompleted &&
                ((anyCannibalActive && cannibalOrdinal == 3) ||
                 thirdCannibalCaptureStarted)) {
                if (!thirdCannibalCaptureStarted &&
                    presentedFirstBoardHash ==
                        expectedThirdCannibalCompleteSourceHashes.front()) {
                    thirdCannibalCaptureStarted = true;
                }
                if (thirdCannibalCaptureStarted &&
                    (thirdCannibalPresentedHashes.empty() ||
                     thirdCannibalPresentedHashes.back() !=
                         presentedFirstBoardHash)) {
                    thirdCannibalPresentedHashes.push_back(
                        presentedFirstBoardHash);
                    if (presentedFirstBoardHash ==
                        expectedThirdCannibalCompleteSourceHashes.back()) {
                        thirdCannibalCaptureCompleted = true;
                    }
                }
            }
            anyCannibalWasActive = anyCannibalActive;
            if (!cannibalCompleted && cannibal != game.enemies_.end()) {
                if (cannibalSurvivorSlot < 0) cannibalSurvivorSlot = cannibal->slot;
                if (cannibalPreBiteHash == 0) cannibalPreBiteHash = previousFirstBoardHash;
                const auto victim = std::find_if(
                    game.enemies_.begin(), game.enemies_.end(),
                    [&](const Game::Enemy& enemy) {
                        return &enemy != &*cannibal && enemy.overlapFrozen &&
                               enemy.collisionHidden && enemy.row == cannibal->row &&
                               enemy.column == cannibal->column;
                    });
                cannibalActorPairMatched = cannibalActorPairMatched ||
                    (cannibal->type == 0 && victim != game.enemies_.end() &&
                     victim->type == 0);
                const std::uint64_t hash = currentFirstBoardHash;
                if (cannibalPresentedHashes.empty() ||
                    cannibalPresentedHashes.back() != hash) {
                    cannibalPresentedHashes.push_back(hash);
                }
                cannibalWasActive = true;
            } else if (!cannibalCompleted && cannibalWasActive &&
                       cannibalTerminalHash == 0) {
                cannibalTerminalHash = currentFirstBoardHash;
                const auto survivorIterator = std::find_if(
                    game.enemies_.begin(), game.enemies_.end(), [&](const Game::Enemy& enemy) {
                        return enemy.slot == cannibalSurvivorSlot;
                    });
                if (survivorIterator != game.enemies_.end()) {
                    const Game::Enemy& survivor = *survivorIterator;
                    cannibalTerminalActorMatched =
                        survivor.type == 0 && survivor.row == 2 && survivor.column == 0 &&
                        survivor.direction == 1 && survivor.dwellFrame == 15 &&
                        !survivor.moving && !survivor.entering;
                }
                cannibalWasActive = false;
                cannibalCompleted = true;
            }
            if (game.page_ == Game::Page::Feedback && !game.deathAnimating_ &&
                game.feedbackEnemyType_ == 0 && game.feedbackMessage_ == "Oops") {
                firstCollisionFeedbackMatched = true;
            }
            if (game.random_.calls != callsBeforeUpdate) {
                std::ostringstream line;
                line << elapsed << "s calls " << callsBeforeUpdate << "->" << game.random_.calls
                     << " page=" << static_cast<int>(game.page_)
                     << " player=" << game.playerRow_ << ',' << game.playerColumn_
                     << " move=" << game.moving_ << "->" << game.moveToRow_ << ','
                     << game.moveToColumn_ << " d" << game.moveDirection_
                     << " munch=" << game.munching_
                     << " feedbackEnemy=" << game.feedbackEnemyType_
                     << " death=" << game.deathAnimating_ << ':' << game.deathSequenceTicks_
                     << " message='" << game.feedbackMessage_ << '\''
                     << " enemies=" << game.enemies_.size();
                for (const Game::Enemy& enemy : game.enemies_) {
                    line << " [s" << enemy.slot << "t" << enemy.type << " r" << enemy.row << "c" << enemy.column
                         << " d" << enemy.direction << " m" << enemy.moving
                         << " e" << enemy.entering << " f" << enemy.overlapFrozen
                         << " s'" << (enemy.savedCellValid ? enemy.savedCell.label : "-") << "']";
                }
                line << " board2c0='" << game.cell(2, 0).label
                     << "' board2c1='" << game.cell(2, 1).label << '\'';
                replayTrace.push_back(line.str());
            }
            if (collisionTransientHash == 0 &&
                game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::CollisionTransient) {
                collisionTransientHash = fullFrameHash(game);
            }
            if (collisionStableBoardHash == 0 &&
                previousHallPhase ==
                    Game::AttractHallTransitionPhase::CollisionTransient &&
                game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::BoardHold) {
                collisionStableBoardHash = fullFrameHash(game);
            }
            if (game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::Wipe &&
                game.attractHallWipeFrame_ >= 0 &&
                game.attractHallWipeFrame_ <
                    static_cast<int>(collisionWipeHashes.size()) &&
                collisionWipeHashes[static_cast<std::size_t>(
                    game.attractHallWipeFrame_)] == 0) {
                collisionWipeHashes[static_cast<std::size_t>(
                    game.attractHallWipeFrame_)] = fullFrameHash(game);
            }
            if (boardBeforeTerminal.empty() && elapsed >= 35.70) {
                std::ostringstream board;
                board << "Native board at " << elapsed << "s remaining="
                      << game.correctRemaining_ << ':';
                for (const Game::Cell& cell : game.cells_) {
                    board << " ['" << cell.label << "' correct=" << cell.correct
                          << " eaten=" << cell.eaten << ']';
                }
                boardBeforeTerminal = board.str();
            }
            if (game.attractBoardIndex_ == 1) secondBoardInitializerStart = callsBeforeUpdate;
            previousFirstBoardHash = currentFirstBoardHash;
            ++firstBoardPresentationFrame;
        }
        if (auditFirstBoardPresentationSequence) {
            std::cerr << "FIRST_BOARD_PRESENTATION_SEQUENCE";
            for (std::size_t index = 0;
                 index < firstBoardPresentationSequence.size(); ++index) {
                std::cerr << " 0x" << std::hex
                          << firstBoardPresentationSequence[index]
                          << std::dec << '@'
                          << firstBoardPresentationStartFrames[index];
            }
            std::cerr << '\n';
        }
        constexpr std::array<std::uint64_t, 6> expectedCollisionWipeHashes = {
            0x4a4c45bf4704e54eull,
            0x2cc687e766578183ull,
            0x8a6d8c6b73f1ac1bull,
            0xba04247ef217e4adull,
            0xb1a8f948642db583ull,
            0x99fdbcb6bd37a9c3ull,
        };
        constexpr std::array<std::uint64_t, 21> expectedCannibalPresentedHashes = {
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull, 0xd49ca9360564ffedull,
            0xfb1aea9a6d579c2eull,
        };
        constexpr std::array<std::uint64_t, 21>
            expectedSecondCannibalPresentedHashes = {
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull, 0xda55160b92bbed1dull,
                0x7330140d95db0b7aull,
                0x3857e0c412f9e4b9ull,
                0xd045aea037581456ull,
            };
        constexpr std::array<std::uint64_t, 9> expectedBashfulTrailPresentedHashes = {
            0x184234cd0b3459dfull,
            0xa94134af316ca8b8ull,
            0xaf6848f5b4c6bbd6ull,
            0x1bb9864c54149626ull,
            0x0b897d98e748403aull,
            0xaa84126d4da449c4ull,
            0x986345441e325759ull,
            0x1a1be4728a608892ull,
            0xfb75ae4b0f01f04aull,
        };
        constexpr std::array<std::uint64_t, 13> expectedReggieTrailPresentedHashes = {
            0xf492558db25be420ull,
            0x044ce8fdb154953bull,
            0x13450ceeaf58c464ull,
            0x90c96dd35f2bdae1ull,
            0xd8e6a13f6279775cull,
            0x6867628c7e648f88ull,
            0xe92fec284bd9bfb1ull,
            0x602eaac0e4a50bb1ull,
            0x0efba26d5447243bull,
            0xb01a36cb2a818931ull,
            0x40db124c7e3ac8b1ull,
            0x505d7de14f423a31ull,
            0x8aed25e1d63b0451ull,
        };
        constexpr std::array<std::uint64_t, 8>
            expectedSecondBashfulTrailPresentedHashes = {
                0xa6c7a8866bcae5f5ull,
                0x6f7a7394886841e7ull,
                0xef3ff5ff0d3fa9d5ull,
                0xab71d999bf2856bbull,
                0x088730934a538989ull,
                0x075359e422075a8dull,
                0xb16b07bb61c205adull,
                0x8d188d13d9079609ull,
            };
        if (!game.attractMode_ || game.attractBoardIndex_ != 1 ||
            collisionTransientHash != 0xeaf1becc0fdbab4eull ||
            collisionStableBoardHash != 0xeaf1becc0fdbab4eull ||
            collisionWipeHashes != expectedCollisionWipeHashes ||
            !firstBoardOpeningContinuousCompleted ||
            firstBoardOpeningContinuousHashes.size() !=
                expectedFirstBoardOpeningContinuousHashes.size() ||
            !std::equal(firstBoardOpeningContinuousHashes.begin(),
                        firstBoardOpeningContinuousHashes.end(),
                        expectedFirstBoardOpeningContinuousHashes.begin()) ||
            callbackLocalCompositeIndex !=
                expectedCallbackLocalCompositeHashes.size() ||
            !firstCollisionCaptureCompleted ||
            firstCollisionPresentedHashes.size() !=
                ExpectedFirstCollisionPresentedHashes.size() ||
            !std::equal(firstCollisionPresentedHashes.begin(),
                        firstCollisionPresentedHashes.end(),
                        ExpectedFirstCollisionPresentedHashes.begin()) ||
            cannibalPreBiteHash != 0x378e270f4598ac61ull ||
            !cannibalActorPairMatched ||
            !cannibalTerminalActorMatched ||
            cannibalPresentedHashes.size() != expectedCannibalPresentedHashes.size() ||
            !std::equal(cannibalPresentedHashes.begin(), cannibalPresentedHashes.end(),
                        expectedCannibalPresentedHashes.begin()) ||
            cannibalTerminalHash != 0x9c2f96cd8e6918c2ull ||
            cannibalOrdinal != 3 ||
            !secondCannibalActorPairMatched ||
            !secondCannibalTerminalActorMatched ||
            secondCannibalPresentedHashes.size() !=
                expectedSecondCannibalPresentedHashes.size() ||
            !std::equal(secondCannibalPresentedHashes.begin(),
                        secondCannibalPresentedHashes.end(),
                        expectedSecondCannibalPresentedHashes.begin()) ||
            secondCannibalTerminalResidentHash != 0x6068127ed60caa9eull ||
            secondCannibalTerminalPresentedHash != 0x6068127ed60caa9eull ||
            !thirdCannibalCaptureCompleted ||
            thirdCannibalPresentedHashes.size() !=
                expectedThirdCannibalCompleteSourceHashes.size() ||
            !std::equal(thirdCannibalPresentedHashes.begin(),
                        thirdCannibalPresentedHashes.end(),
                        expectedThirdCannibalCompleteSourceHashes.begin()) ||
            !playerReggieSafeBridgeCompleted ||
            playerReggieSafeBridgeHashes.size() !=
                expectedPlayerReggieSafeBridgeHashes.size() ||
            !std::equal(playerReggieSafeBridgeHashes.begin(),
                        playerReggieSafeBridgeHashes.end(),
                        expectedPlayerReggieSafeBridgeHashes.begin()) ||
            !preSecondCannibalBridgeCompleted ||
            preSecondCannibalBridgeHashes.size() !=
                expectedPreSecondCannibalBridgeHashes.size() ||
            !std::equal(preSecondCannibalBridgeHashes.begin(),
                        preSecondCannibalBridgeHashes.end(),
                        expectedPreSecondCannibalBridgeHashes.begin()) ||
            !postSecondCannibalContinuousCompleted ||
            postSecondCannibalContinuousHashes.size() !=
                expectedPostSecondCannibalContinuousHashes.size() ||
            !std::equal(postSecondCannibalContinuousHashes.begin(),
                        postSecondCannibalContinuousHashes.end(),
                        expectedPostSecondCannibalContinuousHashes.begin()) ||
            !earlyBottomEntryContinuousCompleted ||
            earlyBottomEntryContinuousHashes.size() !=
                expectedEarlyBottomEntryContinuousHashes.size() ||
            !std::equal(earlyBottomEntryContinuousHashes.begin(),
                        earlyBottomEntryContinuousHashes.end(),
                        expectedEarlyBottomEntryContinuousHashes.begin()) ||
            !firstCannibalApproachContinuousCompleted ||
            firstCannibalApproachContinuousHashes.size() !=
                expectedFirstCannibalApproachContinuousHashes.size() ||
            !std::equal(firstCannibalApproachContinuousHashes.begin(),
                        firstCannibalApproachContinuousHashes.end(),
                        expectedFirstCannibalApproachContinuousHashes.begin()) ||
            !playerWarningBoundaryContinuousCompleted ||
            playerWarningBoundaryContinuousHashes.size() !=
                expectedPlayerWarningBoundaryContinuousHashes.size() ||
            !std::equal(playerWarningBoundaryContinuousHashes.begin(),
                        playerWarningBoundaryContinuousHashes.end(),
                        expectedPlayerWarningBoundaryContinuousHashes.begin()) ||
            !firstBoardChewEntryBoundaryCompleted ||
            firstBoardChewEntryBoundaryHashes.size() !=
                expectedFirstBoardChewEntryBoundaryHashes.size() ||
            !std::equal(firstBoardChewEntryBoundaryHashes.begin(),
                        firstBoardChewEntryBoundaryHashes.end(),
                        expectedFirstBoardChewEntryBoundaryHashes.begin()) ||
            !laterBottomEntryContinuousCompleted ||
            laterBottomEntryContinuousHashes.size() !=
                expectedLaterBottomEntryContinuousHashes.size() ||
            !std::equal(laterBottomEntryContinuousHashes.begin(),
                        laterBottomEntryContinuousHashes.end(),
                        expectedLaterBottomEntryContinuousHashes.begin()) ||
            !laterBottomExitContinuousCompleted ||
            laterBottomExitContinuousHashes.size() !=
                expectedLaterBottomExitContinuousHashes.size() ||
            !std::equal(laterBottomExitContinuousHashes.begin(),
                        laterBottomExitContinuousHashes.end(),
                        expectedLaterBottomExitContinuousHashes.begin()) ||
            !bashfulRightEntryCompleted ||
            bashfulRightEntryHashes.size() !=
                expectedBashfulRightEntryHashes.size() ||
            !std::equal(bashfulRightEntryHashes.begin(),
                        bashfulRightEntryHashes.end(),
                        expectedBashfulRightEntryHashes.begin()) ||
            !laterBashfulLeftTrailCompleted ||
            laterBashfulLeftTrailHashes.size() !=
                expectedLaterBashfulLeftTrailHashes.size() ||
            !std::equal(laterBashfulLeftTrailHashes.begin(),
                        laterBashfulLeftTrailHashes.end(),
                        expectedLaterBashfulLeftTrailHashes.begin()) ||
            !reggieDownSafeEntryCompleted ||
            reggieDownSafeEntryHashes.size() !=
                expectedReggieDownSafeEntryHashes.size() ||
            !std::equal(reggieDownSafeEntryHashes.begin(),
                        reggieDownSafeEntryHashes.end(),
                        expectedReggieDownSafeEntryHashes.begin()) ||
            !chewTopEntryCompleted ||
            chewTopEntryHashes.size() != expectedChewTopEntryHashes.size() ||
            !std::equal(chewTopEntryHashes.begin(),
                        chewTopEntryHashes.end(),
                        expectedChewTopEntryHashes.begin()) ||
            !bashfulTrailCaptureCompleted ||
            bashfulTrailPresentedHashes.size() !=
                expectedBashfulTrailPresentedHashes.size() ||
            !std::equal(bashfulTrailPresentedHashes.begin(),
                        bashfulTrailPresentedHashes.end(),
                        expectedBashfulTrailPresentedHashes.begin()) ||
            !reggieTrailCaptureCompleted ||
            reggieTrailPresentedHashes.size() !=
                expectedReggieTrailPresentedHashes.size() ||
            !std::equal(reggieTrailPresentedHashes.begin(),
                        reggieTrailPresentedHashes.end(),
                        expectedReggieTrailPresentedHashes.begin()) ||
            !secondBashfulTrailCaptureCompleted ||
            secondBashfulTrailPresentedHashes.size() !=
                expectedSecondBashfulTrailPresentedHashes.size() ||
            !std::equal(secondBashfulTrailPresentedHashes.begin(),
                        secondBashfulTrailPresentedHashes.end(),
                        expectedSecondBashfulTrailPresentedHashes.begin()) ||
            laterTrailTopExitSourceMatches.size() !=
                expectedLaterTrailTopExitSourceHashes.size() ||
            !std::equal(laterTrailTopExitSourceMatches.begin(),
                        laterTrailTopExitSourceMatches.end(),
                        expectedLaterTrailTopExitSourceHashes.begin()) ||
            !laterTrailTopExitNativeCompleted ||
            laterTrailTopExitNativeHashes.size() !=
                expectedLaterTrailTopExitNativeHashes.size() ||
            !std::equal(laterTrailTopExitNativeHashes.begin(),
                        laterTrailTopExitNativeHashes.end(),
                        expectedLaterTrailTopExitNativeHashes.begin()) ||
            firstDemoChewHashes.size() != expectedFirstDemoChewHashes.size() ||
            !std::equal(firstDemoChewHashes.begin(), firstDemoChewHashes.end(),
                        expectedFirstDemoChewHashes.begin()) ||
            firstDemoChewRightExitHashes.size() !=
                expectedFirstDemoChewRightExitHashes.size() ||
            !std::equal(firstDemoChewRightExitHashes.begin(),
                        firstDemoChewRightExitHashes.end(),
                        expectedFirstDemoChewRightExitHashes.begin()) ||
            firstDemoDepartingEnemyOverlapHashes.size() !=
                expectedFirstDemoDepartingEnemyOverlapHashes.size() ||
            !std::equal(firstDemoDepartingEnemyOverlapHashes.begin(),
                        firstDemoDepartingEnemyOverlapHashes.end(),
                        expectedFirstDemoDepartingEnemyOverlapHashes.begin()) ||
            firstDemoPostDepartureContinuousHashes.size() !=
                expectedFirstDemoPostDepartureContinuousHashes.size() ||
            !std::equal(firstDemoPostDepartureContinuousHashes.begin(),
                        firstDemoPostDepartureContinuousHashes.end(),
                        expectedFirstDemoPostDepartureContinuousHashes.begin())) {
            const auto reportSequenceMismatch = [](
                const char* name, const auto& actual, const auto& expected) {
                if (actual.size() == expected.size() &&
                    std::equal(actual.begin(), actual.end(), expected.begin())) {
                    return;
                }
                std::size_t first = 0;
                while (first < actual.size() && first < expected.size() &&
                       actual[first] == expected[first]) {
                    ++first;
                }
                std::cerr << "Replay sequence mismatch " << name
                          << ": actual=" << actual.size()
                          << " expected=" << expected.size()
                          << " first=" << first;
                if (first < actual.size()) {
                    std::cerr << " actual_hash=0x" << std::hex << actual[first]
                              << std::dec;
                }
                if (first < expected.size()) {
                    std::cerr << " expected_hash=0x" << std::hex << expected[first]
                              << std::dec;
                }
                std::cerr << '\n';
            };
            reportSequenceMismatch("collision-wipe", collisionWipeHashes,
                                   expectedCollisionWipeHashes);
            reportSequenceMismatch("opening", firstBoardOpeningContinuousHashes,
                                   expectedFirstBoardOpeningContinuousHashes);
            reportSequenceMismatch("first-collision", firstCollisionPresentedHashes,
                                   ExpectedFirstCollisionPresentedHashes);
            reportSequenceMismatch("first-cannibal", cannibalPresentedHashes,
                                   expectedCannibalPresentedHashes);
            reportSequenceMismatch("second-cannibal", secondCannibalPresentedHashes,
                                   expectedSecondCannibalPresentedHashes);
            reportSequenceMismatch("third-cannibal", thirdCannibalPresentedHashes,
                                   expectedThirdCannibalCompleteSourceHashes);
            reportSequenceMismatch("player-reggie-safe", playerReggieSafeBridgeHashes,
                                   expectedPlayerReggieSafeBridgeHashes);
            reportSequenceMismatch("pre-second-cannibal", preSecondCannibalBridgeHashes,
                                   expectedPreSecondCannibalBridgeHashes);
            reportSequenceMismatch("post-second-cannibal", postSecondCannibalContinuousHashes,
                                   expectedPostSecondCannibalContinuousHashes);
            reportSequenceMismatch("early-bottom-entry", earlyBottomEntryContinuousHashes,
                                   expectedEarlyBottomEntryContinuousHashes);
            reportSequenceMismatch("first-cannibal-approach",
                                   firstCannibalApproachContinuousHashes,
                                   expectedFirstCannibalApproachContinuousHashes);
            reportSequenceMismatch("player-warning", playerWarningBoundaryContinuousHashes,
                                   expectedPlayerWarningBoundaryContinuousHashes);
            reportSequenceMismatch("chew-entry", firstBoardChewEntryBoundaryHashes,
                                   expectedFirstBoardChewEntryBoundaryHashes);
            reportSequenceMismatch("later-bottom-entry", laterBottomEntryContinuousHashes,
                                   expectedLaterBottomEntryContinuousHashes);
            reportSequenceMismatch("later-bottom-exit", laterBottomExitContinuousHashes,
                                   expectedLaterBottomExitContinuousHashes);
            reportSequenceMismatch("bashful-right-entry", bashfulRightEntryHashes,
                                   expectedBashfulRightEntryHashes);
            reportSequenceMismatch("bashful-left-trail", laterBashfulLeftTrailHashes,
                                   expectedLaterBashfulLeftTrailHashes);
            reportSequenceMismatch("reggie-down-entry", reggieDownSafeEntryHashes,
                                   expectedReggieDownSafeEntryHashes);
            reportSequenceMismatch("chew-top-entry", chewTopEntryHashes,
                                   expectedChewTopEntryHashes);
            reportSequenceMismatch("bashful-trail", bashfulTrailPresentedHashes,
                                   expectedBashfulTrailPresentedHashes);
            reportSequenceMismatch("reggie-trail", reggieTrailPresentedHashes,
                                   expectedReggieTrailPresentedHashes);
            reportSequenceMismatch("second-bashful-trail",
                                   secondBashfulTrailPresentedHashes,
                                   expectedSecondBashfulTrailPresentedHashes);
            reportSequenceMismatch("later-top-exit-source",
                                   laterTrailTopExitSourceMatches,
                                   expectedLaterTrailTopExitSourceHashes);
            reportSequenceMismatch("later-top-exit-native",
                                   laterTrailTopExitNativeHashes,
                                   expectedLaterTrailTopExitNativeHashes);
            reportSequenceMismatch("first-demo-chew", firstDemoChewHashes,
                                   expectedFirstDemoChewHashes);
            reportSequenceMismatch("first-demo-chew-right-exit",
                                   firstDemoChewRightExitHashes,
                                   expectedFirstDemoChewRightExitHashes);
            reportSequenceMismatch("first-demo-departure",
                                   firstDemoDepartingEnemyOverlapHashes,
                                   expectedFirstDemoDepartingEnemyOverlapHashes);
            reportSequenceMismatch("first-demo-post-departure",
                                   firstDemoPostDepartureContinuousHashes,
                                   expectedFirstDemoPostDepartureContinuousHashes);
            std::cerr << "Captured replay did not reach demo board 2 after " << elapsed << " seconds\n";
            std::cerr << "Replay gates: attract=" << game.attractMode_
                      << " board=" << game.attractBoardIndex_
                      << " callback-local=" << callbackLocalCompositeIndex
                      << '/' << expectedCallbackLocalCompositeHashes.size()
                      << " collision-complete=" << firstCollisionCaptureCompleted
                      << " cannibal-complete=" << cannibalCompleted
                      << " third-cannibal-complete=" << thirdCannibalCaptureCompleted
                      << " opening=" << firstBoardOpeningContinuousCompleted
                      << " player-safe=" << playerReggieSafeBridgeCompleted
                      << " pre-second=" << preSecondCannibalBridgeCompleted
                      << " post-second=" << postSecondCannibalContinuousCompleted
                      << " early-bottom=" << earlyBottomEntryContinuousCompleted
                      << " first-approach=" << firstCannibalApproachContinuousCompleted
                      << " warning=" << playerWarningBoundaryContinuousCompleted
                      << " chew-entry=" << firstBoardChewEntryBoundaryCompleted
                      << " later-entry=" << laterBottomEntryContinuousCompleted
                      << " later-exit=" << laterBottomExitContinuousCompleted
                      << " bashful-right=" << bashfulRightEntryCompleted
                      << " bashful-left=" << laterBashfulLeftTrailCompleted
                      << " reggie-down=" << reggieDownSafeEntryCompleted
                      << " chew-top=" << chewTopEntryCompleted
                      << " bashful-trail=" << bashfulTrailCaptureCompleted
                      << " reggie-trail=" << reggieTrailCaptureCompleted
                      << " second-bashful=" << secondBashfulTrailCaptureCompleted
                      << " top-exit=" << laterTrailTopExitNativeCompleted << '\n';
            std::cerr << "Collision transition hashes: transient=0x" << std::hex
                      << collisionTransientHash << " stable=0x" << collisionStableBoardHash
                      << " wipe=";
            for (const std::uint64_t hash : collisionWipeHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstBoardOpeningContinuous=";
            for (const std::uint64_t hash :
                 firstBoardOpeningContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstPlayerBite=";
            for (const std::uint64_t hash : firstCollisionPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " cannibalPair=" << cannibalActorPairMatched
                      << " terminalActor=" << cannibalTerminalActorMatched
                      << " pre=0x" << cannibalPreBiteHash
                      << " terminal=0x" << cannibalTerminalHash << " sequence=";
            for (const std::uint64_t hash : cannibalPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterCannibalOrdinal=" << std::dec << cannibalOrdinal
                      << " secondPair=" << secondCannibalActorPairMatched
                      << " secondTerminalActor=" << secondCannibalTerminalActorMatched
                      << " secondTerminal=0x" << std::hex
                      << secondCannibalTerminalResidentHash << "/0x"
                      << secondCannibalTerminalPresentedHash << " secondSequence=";
            for (const std::uint64_t hash : secondCannibalPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " thirdCompleteSequence=";
            for (const std::uint64_t hash : thirdCannibalPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " playerReggieSafeBridge=";
            for (const std::uint64_t hash : playerReggieSafeBridgeHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " preSecondCannibalBridge=";
            for (const std::uint64_t hash : preSecondCannibalBridgeHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " postSecondCannibalContinuous=";
            for (const std::uint64_t hash :
                 postSecondCannibalContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " earlyBottomEntryContinuous=";
            for (const std::uint64_t hash : earlyBottomEntryContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstCannibalApproachContinuous=";
            for (const std::uint64_t hash :
                 firstCannibalApproachContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " playerWarningBoundaryContinuous=";
            for (const std::uint64_t hash :
                 playerWarningBoundaryContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstBoardChewEntryBoundary=";
            for (const std::uint64_t hash :
                 firstBoardChewEntryBoundaryHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterBottomEntryContinuous=";
            for (const std::uint64_t hash : laterBottomEntryContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterBottomExitContinuous=";
            for (const std::uint64_t hash : laterBottomExitContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " bashfulRightEntry=";
            for (const std::uint64_t hash : bashfulRightEntryHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterBashfulLeftTrail=";
            for (const std::uint64_t hash : laterBashfulLeftTrailHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " reggieDownSafeEntry=";
            for (const std::uint64_t hash : reggieDownSafeEntryHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " chewTopEntry=";
            for (const std::uint64_t hash : chewTopEntryHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " bashfulTrail=";
            for (const std::uint64_t hash : bashfulTrailPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " reggieTrail=";
            for (const std::uint64_t hash : reggieTrailPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " secondBashfulTrail=";
            for (const std::uint64_t hash : secondBashfulTrailPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterTrailTopExit=";
            for (const std::uint64_t hash : laterTrailTopExitSourceMatches) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " laterTrailTopExitNative=";
            for (const std::uint64_t hash : laterTrailTopExitNativeHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstDemoChew=";
            for (const std::uint64_t hash : firstDemoChewHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstDemoChewRightExit=";
            for (const std::uint64_t hash : firstDemoChewRightExitHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstDemoDepartingEnemyOverlap=";
            for (const std::uint64_t hash :
                 firstDemoDepartingEnemyOverlapHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " firstDemoPostDepartureContinuous=";
            for (const std::uint64_t hash :
                 firstDemoPostDepartureContinuousHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        if (game.activeBoardMode_ != Game::Mode::Primes || game.playerRow_ != 3 ||
            game.playerColumn_ != 1 || secondBoardInitializerStart != 271 ||
            !firstCollisionFeedbackMatched) {
            std::cerr << "Captured demo board 2 state mismatch after " << elapsed
                      << " seconds: mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_ << " level=" << game.level_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " prngCalls=" << game.random_.calls
                      << " firstSetupCalls=" << firstBoardSetupCalls
                      << " secondInitializerStart=" << secondBoardInitializerStart
                      << " firstFeedback=" << firstCollisionFeedbackMatched << '\n';
            std::cerr << initialJobs.str() << '\n';
            std::cerr << boardBeforeTerminal << '\n';
            for (const std::string& line : replayTrace) std::cerr << line << '\n';
            std::cerr << "Complete second-board initializer starts matching the capture:";
            bool foundInitializer = false;
            for (int start = 0; start <= 300; ++start) {
                game.random_.seed(62853u);
                for (int call = 0; call < start; ++call) (void)game.random_.next();
                game.attractBoardIndex_ = 0;
                game.startNextAttractBoard();
                bool matches = game.activeBoardMode_ == Game::Mode::Primes &&
                               game.playerRow_ == 3 && game.playerColumn_ == 1;
                for (int index = 0; matches && index < Game::BoardCellCount; ++index) {
                    matches = game.cells_[static_cast<std::size_t>(index)].label ==
                              labels[static_cast<std::size_t>(index)];
                }
                if (matches) {
                    std::cerr << ' ' << start;
                    foundInitializer = true;
                }
            }
            if (!foundInitializer) std::cerr << " none";
            std::cerr << '\n';
            return false;
        }
        for (int index = 0; index < Game::BoardCellCount; ++index) {
            const Game::Cell& current = game.cells_[static_cast<std::size_t>(index)];
            if (current.label != labels[static_cast<std::size_t>(index)]) {
                std::cerr << "Captured demo board 2 cell " << index << " mismatch after "
                          << elapsed << " seconds: label='" << current.label << "' expected='"
                          << labels[static_cast<std::size_t>(index)] << "' prngCalls="
                          << game.random_.calls << " firstSetupCalls=" << firstBoardSetupCalls
                          << " secondInitializerStart=" << secondBoardInitializerStart << '\n';
                std::cerr << "Actual labels:";
                for (const Game::Cell& cell : game.cells_) {
                    std::cerr << " ['" << cell.label << "' correct=" << cell.correct
                              << " eaten=" << cell.eaten << ']';
                }
                std::cerr << '\n';
                std::cerr << "Prime cell-stream starts matching captured labels:";
                bool foundStart = false;
                for (int start = 0; start <= 300; ++start) {
                    game.random_.seed(62853u);
                    for (int call = 0; call < start; ++call) (void)game.random_.next();
                    game.activeBoardMode_ = Game::Mode::Primes;
                    game.primeMaximumIndex_ = 0;
                    bool matches = true;
                    for (int cellIndex = 0; cellIndex < Game::BoardCellCount; ++cellIndex) {
                        const Game::Cell candidate = game.generatedCell(game.randomInt(0, 1) != 0);
                        if (cellIndex != 19 &&
                            candidate.label != labels[static_cast<std::size_t>(cellIndex)]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        std::cerr << ' ' << start;
                        foundStart = true;
                    }
                }
                if (!foundStart) std::cerr << " none";
                std::cerr << '\n';
                std::cerr << "Replay PRNG events:\n";
                std::cerr << boardBeforeTerminal << '\n';
                for (const std::string& line : replayTrace) std::cerr << line << '\n';
                return false;
            }
        }

        // The board index advances on captured painter frame 8351. Finish the
        // second torn frame before measuring gameplay from the first stable
        // board, where the lossless action timestamps are anchored.
        while (game.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            game.update(frameSeconds);
            elapsed += frameSeconds;
        }

        const bool auditSecondBoardPresentationSequence =
            std::getenv("MUNCHERS_AUDIT_SECOND_BOARD_SEQUENCE") != nullptr;
        const char* secondBoardPageDumpDirectoryText =
            std::getenv("MUNCHERS_AUDIT_SECOND_BOARD_PAGE_DIR");
        const bool dumpSecondBoardPages = secondBoardPageDumpDirectoryText &&
            *secondBoardPageDumpDirectoryText;
        const std::filesystem::path secondBoardPageDumpDirectory =
            dumpSecondBoardPages
                ? std::filesystem::path(secondBoardPageDumpDirectoryText)
                : std::filesystem::path{};
        if (dumpSecondBoardPages) {
            std::error_code error;
            std::filesystem::create_directories(
                secondBoardPageDumpDirectory, error);
            if (error) return false;
        }
        Renderer secondBoardPresentationRenderer(game.assets_.graphicsMode());
        std::vector<std::uint64_t> secondBoardPresentationSequence;
        std::vector<int> secondBoardPresentationStartFrames;
        int secondBoardPresentationFrame = 0;
        bool secondBoardPageDumpFailed = false;
        const auto captureSecondBoardPresentation = [&]() {
            game.renderPresentation(secondBoardPresentationRenderer);
            const std::uint64_t hash =
                renderedFrameHash(secondBoardPresentationRenderer);
            const bool changed = secondBoardPresentationSequence.empty() ||
                secondBoardPresentationSequence.back() != hash;
            if (changed) {
                secondBoardPresentationSequence.push_back(hash);
                secondBoardPresentationStartFrames.push_back(
                    secondBoardPresentationFrame);
                if (dumpSecondBoardPages) {
                    std::ostringstream name;
                    name << std::hex << hash << std::dec << '-'
                         << secondBoardPresentationSequence.size() - 1
                         << ".ppm";
                    std::ofstream output(secondBoardPageDumpDirectory / name.str(),
                                         std::ios::binary);
                    if (!output) {
                        secondBoardPageDumpFailed = true;
                    } else {
                        output << "P6\n" << Renderer::Width << ' '
                               << Renderer::Height << "\n255\n";
                        for (const std::uint32_t pixel :
                             secondBoardPresentationRenderer.pixels()) {
                            const char rgb[3] = {
                                static_cast<char>((pixel >> 16) & 0xffu),
                                static_cast<char>((pixel >> 8) & 0xffu),
                                static_cast<char>(pixel & 0xffu),
                            };
                            output.write(rgb, sizeof(rgb));
                        }
                        if (!output) secondBoardPageDumpFailed = true;
                    }
                }
            }
            ++secondBoardPresentationFrame;
        };
        // The ordinary test advances the preceding board painter without
        // presenting it. Drain those already-completed transition pages
        // before anchoring frame zero to DOS frame 8353, the first stable
        // Prime board page.
        while (!game.presentationFrames_.empty()) {
            game.renderPresentation(secondBoardPresentationRenderer);
        }
        captureSecondBoardPresentation();

        // The preserved second board is unusually short and therefore gives
        // us a dense scheduler oracle. From the blank start at r3c1, the DOS
        // controller walks up, eats the correct 2, walks left, then takes its
        // fallible one-in-ten munch branch on the incorrect 1. The ensuing
        // The exact 150-tick feedback wait and captured 46-frame Hall lead-in
        // lead to the captured Equals-30 board.
        const double secondBoardStart = elapsed;
        bool sawMoveUp = false;
        bool sawCorrectMunch = false;
        bool sawMoveLeft = false;
        bool sawWrongMunch = false;
        bool sawWrongFeedback = false;
        bool feedbackStateMatched = false;
        bool sawPostFeedbackBoard = false;
        bool sawHall = false;
        bool sawSplash = false;
        std::array<std::uint64_t, Game::MunchAnimationTicks> correctMunchHashes{};
        std::uint64_t cleanStableBoardHash = 0;
        std::array<std::uint64_t, 7> cleanWipeHashes{};
        std::uint64_t primeHallHash = 0;
        double moveUpAt = -1.0;
        double correctMunchAt = -1.0;
        double moveLeftAt = -1.0;
        double wrongMunchAt = -1.0;
        double feedbackAt = -1.0;
        std::uint64_t thirdBoardInitializerStart = 0;
        std::vector<std::string> secondBoardTrace;
        while (game.attractMode_ && game.attractBoardIndex_ == 1 && elapsed < 180.0) {
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            const Game::Page previousPage = game.page_;
            const bool previousPostBoard = game.attractPostFeedbackBoard_;
            const int previousRow = game.playerRow_;
            const int previousColumn = game.playerColumn_;
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            const Game::AttractHallTransitionPhase previousHallPhase =
                game.attractHallTransitionPhase_;

            game.update(frameSeconds);
            elapsed += frameSeconds;
            captureSecondBoardPresentation();
            const double boardSeconds = elapsed - secondBoardStart;

            if (!sawMoveUp && !wasMoving && game.moving_ && game.moveFromRow_ == 3 &&
                game.moveFromColumn_ == 1 && game.moveToRow_ == 2 &&
                game.moveToColumn_ == 1 && game.moveDirection_ == 0) {
                sawMoveUp = true;
                moveUpAt = boardSeconds;
            }
            if (!sawCorrectMunch && !wasMunching && game.munching_ &&
                game.munchCellIndex_ == 13) {
                sawCorrectMunch = true;
                correctMunchAt = boardSeconds;
            }
            if (game.munching_ && game.munchCellIndex_ == 13) {
                const double chewElapsed =
                    Game::MunchAnimationDuration - std::max(0.0, game.munchTimer_);
                const int chewTick = std::clamp(
                    static_cast<int>(std::floor(
                        chewElapsed * OriginalSchedulerTicksPerSecond + 1e-7)),
                    0, Game::MunchAnimationTicks - 1);
                std::uint64_t& hash = correctMunchHashes[static_cast<std::size_t>(chewTick)];
                if (hash == 0) hash = fullFrameHash(game);
            }
            if (!sawMoveLeft && !wasMoving && game.moving_ && game.moveFromRow_ == 2 &&
                game.moveFromColumn_ == 1 && game.moveToRow_ == 2 &&
                game.moveToColumn_ == 0 && game.moveDirection_ == 3) {
                sawMoveLeft = true;
                moveLeftAt = boardSeconds;
            }
            if (!sawWrongMunch && !wasMunching && game.munching_ &&
                game.munchCellIndex_ == 12) {
                sawWrongMunch = true;
                wrongMunchAt = boardSeconds;
            }
            if (!sawWrongFeedback && previousPage != Game::Page::Feedback &&
                game.page_ == Game::Page::Feedback) {
                sawWrongFeedback = true;
                feedbackAt = boardSeconds;
                feedbackStateMatched = game.feedbackKind_ == Game::FeedbackKind::WrongAnswer &&
                    game.feedbackMessage_ == "The number \"1\" is not prime." &&
                    game.cell(2, 1).eaten && game.cell(2, 0).eaten &&
                    game.score_ == 25 && game.playerRow_ == 2 && game.playerColumn_ == 0 &&
                    !game.deathAnimating_ && game.enemies_.empty();
            }
            if (!sawPostFeedbackBoard && !previousPostBoard &&
                game.attractPostFeedbackBoard_) {
                sawPostFeedbackBoard = true;
            }
            if (!sawHall && previousPage != Game::Page::Hall && game.page_ == Game::Page::Hall) {
                sawHall = true;
                primeHallHash = fullFrameHash(game);
            }
            if (!sawSplash && previousPage != Game::Page::StartupSplash &&
                game.page_ == Game::Page::StartupSplash) {
                sawSplash = true;
            }
            if (game.attractBoardIndex_ == 2) thirdBoardInitializerStart = callsBeforeUpdate;

            if (cleanStableBoardHash == 0 &&
                previousHallPhase == Game::AttractHallTransitionPhase::None &&
                game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::BoardHold &&
                game.attractHallWipeVariant_ ==
                    Game::AttractHallWipeVariant::Clean) {
                cleanStableBoardHash = fullFrameHash(game);
            }
            if (game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::Wipe &&
                game.attractHallWipeFrame_ >= 0 &&
                game.attractHallWipeFrame_ <
                    static_cast<int>(cleanWipeHashes.size()) &&
                cleanWipeHashes[static_cast<std::size_t>(
                    game.attractHallWipeFrame_)] == 0) {
                cleanWipeHashes[static_cast<std::size_t>(
                    game.attractHallWipeFrame_)] = fullFrameHash(game);
            }

            if (game.random_.calls != callsBeforeUpdate || game.moving_ != wasMoving ||
                game.munching_ != wasMunching || game.page_ != previousPage ||
                game.attractPostFeedbackBoard_ != previousPostBoard ||
                game.playerRow_ != previousRow || game.playerColumn_ != previousColumn) {
                std::ostringstream line;
                line << boardSeconds << "s calls " << callsBeforeUpdate << "->"
                     << game.random_.calls << " page=" << static_cast<int>(game.page_)
                     << " player=" << game.playerRow_ << ',' << game.playerColumn_
                     << " move=" << game.moving_ << " " << game.moveFromRow_ << ','
                     << game.moveFromColumn_ << "->" << game.moveToRow_ << ','
                     << game.moveToColumn_ << " d" << game.moveDirection_
                     << " munch=" << game.munching_ << " cell=" << game.munchCellIndex_
                     << " post=" << game.attractPostFeedbackBoard_
                     << " score=" << game.score_ << " enemies=" << game.enemies_.size();
                secondBoardTrace.push_back(line.str());
            }
        }

        constexpr std::array<std::uint64_t, 7> expectedCleanWipeHashes = {
            0x805dc5541d914cddull,
            0x2cc687e766578183ull,
            0x2cc687e766578183ull,
            0x413bd71b36091f2bull,
            0xb1a8f948642db583ull,
            0xb1a8f948642db583ull,
            0xb87498efff8f0b15ull,
        };
        // Lossless DOS frames 8462-8478 show the seven live chew callbacks on
        // this later-level Prime board. BTMP records 12 and 14 are pixel-
        // identical in context, while record 13 is the alternating closed
        // pose. Hash the complete 320x200 frame so the board, actors, status,
        // cell erasure, score, and chew pose are all checked together.
        constexpr std::array<std::uint64_t, Game::MunchAnimationTicks>
            expectedCorrectMunchHashes = {
                0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull,
                0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull,
                0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull,
                0x09ee3b653a7a0c5full,
            };
        const auto routeFailed = [&]() {
            return !sawMoveUp || !sawCorrectMunch || !sawMoveLeft || !sawWrongMunch ||
                   !sawWrongFeedback || !feedbackStateMatched || !sawPostFeedbackBoard ||
                   !sawHall || !sawSplash || thirdBoardInitializerStart != 334 ||
                   correctMunchHashes != expectedCorrectMunchHashes ||
                   cleanStableBoardHash != 0xe252873073689d8bull ||
                   cleanWipeHashes != expectedCleanWipeHashes ||
                   primeHallHash != 0x2faa0bed13235713ull ||
                   !game.attractMode_ || game.attractBoardIndex_ != 2 ||
                   game.feedbackMessage_ != "";
        };
        if (routeFailed()) {
            std::cerr << "Captured demo board 2 route mismatch: up=" << sawMoveUp
                      << " correct=" << sawCorrectMunch << " left=" << sawMoveLeft
                      << " wrong=" << sawWrongMunch << " feedback=" << sawWrongFeedback
                      << " feedbackState=" << feedbackStateMatched
                      << " post=" << sawPostFeedbackBoard << " hall=" << sawHall
                      << " splash=" << sawSplash << " board=" << game.attractBoardIndex_
                      << " thirdStart=" << thirdBoardInitializerStart
                      << " page=" << static_cast<int>(game.page_) << " elapsed=" << elapsed
                      << " calls=" << game.random_.calls << " stable=0x" << std::hex
                      << cleanStableBoardHash << " hall=0x" << primeHallHash << " chew=";
            for (const std::uint64_t hash : correctMunchHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << " wipe=";
            for (const std::uint64_t hash : cleanWipeHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << std::dec << '\n';
            for (const std::string& line : secondBoardTrace) std::cerr << line << '\n';
            return false;
        }

        // Frame analysis of the lossless DOS capture places these action
        // starts at approximately 0.97, 1.53, 2.75, 3.31 and 3.61 seconds
        // from the first stable board frame. Allow only the one-frame board
        // transition uncertainty plus the actor's visually identical lead-in.
        const auto nearReference = [](double actual, double expected, double tolerance) {
            return std::abs(actual - expected) <= tolerance;
        };
        if (!nearReference(moveUpAt, 0.97, 0.12) ||
            !nearReference(correctMunchAt, 1.53, 0.16) ||
            !nearReference(moveLeftAt, 2.75, 0.16) ||
            !nearReference(wrongMunchAt, 3.31, 0.16) ||
            !nearReference(feedbackAt, 3.61, 0.16)) {
            std::cerr << "Captured demo board 2 timing mismatch: up=" << moveUpAt
                      << " correct=" << correctMunchAt << " left=" << moveLeftAt
                      << " wrong=" << wrongMunchAt << " feedback=" << feedbackAt << '\n';
            for (const std::string& line : secondBoardTrace) std::cerr << line << '\n';
            return false;
        }

        if (game.activeBoardMode_ != Game::Mode::Equality || game.target_ != 30 ||
            game.playerRow_ != 1 || game.playerColumn_ != 1) {
            std::cerr << "Captured demo board 3 state mismatch: mode="
                      << static_cast<int>(game.activeBoardMode_) << " target=" << game.target_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " initializerStart=" << thirdBoardInitializerStart
                      << " calls=" << game.random_.calls << '\n';
            std::cerr << "Actual labels:";
            for (const Game::Cell& cell : game.cells_) std::cerr << " ['" << cell.label << "']";
            std::cerr << "\nComplete third-board initializer starts matching the capture:";
            bool foundInitializer = false;
            for (int start = 0; start <= 500; ++start) {
                game.random_.seed(62853u);
                for (int call = 0; call < start; ++call) (void)game.random_.next();
                game.attractBoardIndex_ = 1;
                game.startNextAttractBoard();
                bool matches = game.activeBoardMode_ == Game::Mode::Equality &&
                               game.target_ == 30 && game.playerRow_ == 1 &&
                               game.playerColumn_ == 1;
                for (int index = 0; matches && index < Game::BoardCellCount; ++index) {
                    matches = game.cells_[static_cast<std::size_t>(index)].label ==
                              thirdLabels[static_cast<std::size_t>(index)];
                }
                if (matches) {
                    std::cerr << ' ' << start;
                    foundInitializer = true;
                }
            }
            if (!foundInitializer) std::cerr << " none";
            std::cerr << '\n';
            std::cerr << "Equality cell-stream starts matching visible labels:";
            bool foundCellStream = false;
            int bestCellStreamStart = -1;
            int bestCellStreamMatches = -1;
            std::array<std::string, Game::BoardCellCount> bestCellStreamLabels{};
            for (int start = 0; start <= 1000; ++start) {
                game.random_.seed(62853u);
                for (int call = 0; call < start; ++call) (void)game.random_.next();
                game.activeBoardMode_ = Game::Mode::Equality;
                game.target_ = 30;
                bool matches = true;
                int matchingLabels = 0;
                std::array<std::string, Game::BoardCellCount> candidateLabels{};
                for (int index = 0; index < Game::BoardCellCount; ++index) {
                    const Game::Cell candidate = game.generatedCell(game.randomInt(0, 1) != 0);
                    candidateLabels[static_cast<std::size_t>(index)] = candidate.label;
                    if (index != 7) {
                        const bool labelMatches = candidate.label ==
                            thirdLabels[static_cast<std::size_t>(index)];
                        matches = matches && labelMatches;
                        matchingLabels += labelMatches ? 1 : 0;
                    }
                }
                if (matchingLabels > bestCellStreamMatches) {
                    bestCellStreamMatches = matchingLabels;
                    bestCellStreamStart = start;
                    bestCellStreamLabels = std::move(candidateLabels);
                }
                if (matches) {
                    std::cerr << ' ' << start;
                    foundCellStream = true;
                }
            }
            if (!foundCellStream) std::cerr << " none";
            std::cerr << '\n';
            std::cerr << "Best cell stream " << bestCellStreamStart << " matched "
                      << bestCellStreamMatches << "/29 labels:";
            for (const std::string& label : bestCellStreamLabels) {
                std::cerr << " ['" << label << "']";
            }
            std::cerr << '\n';
            for (const std::string& line : secondBoardTrace) std::cerr << line << '\n';
            return false;
        }
        for (int index = 0; index < Game::BoardCellCount; ++index) {
            const Game::Cell& current = game.cells_[static_cast<std::size_t>(index)];
            if (current.label != thirdLabels[static_cast<std::size_t>(index)]) {
                std::cerr << "Captured demo board 3 cell " << index << " mismatch: label='"
                          << current.label << "' expected='"
                          << thirdLabels[static_cast<std::size_t>(index)]
                          << "' initializerStart=" << thirdBoardInitializerStart
                          << " calls=" << game.random_.calls << '\n';
                std::cerr << "Actual labels:";
                for (const Game::Cell& cell : game.cells_) std::cerr << " ['" << cell.label << "']";
                std::cerr << '\n';
                for (const std::string& line : secondBoardTrace) std::cerr << line << '\n';
                return false;
            }
        }
        while (game.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            game.update(frameSeconds);
            captureSecondBoardPresentation();
        }
        if (auditSecondBoardPresentationSequence) {
            std::cerr << "SECOND_BOARD_PRESENTATION_SEQUENCE";
            for (std::size_t index = 0;
                 index < secondBoardPresentationSequence.size(); ++index) {
                std::cerr << " 0x" << std::hex
                          << secondBoardPresentationSequence[index]
                          << std::dec << '@'
                          << secondBoardPresentationStartFrames[index];
            }
            std::cerr << '\n';
        }
        constexpr std::array<std::uint64_t, 51>
            expectedSecondBoardPresentationSequence = {
                0x9c1938a9423716dfull, 0x2e493c34d76d82f9ull,
                0x179951c674b68e32ull, 0x1b4ffe5e0bc0b94full,
                0x1ced67fbfcd6a8cfull, 0x3fc0f34c0f41df81ull,
                0x179831b9e73ab1d7ull, 0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull, 0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull, 0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull, 0x09ee3b653a7a0c5full,
                0xa2e8dc753a83a4b7ull, 0x397919da5ffc6225ull,
                0x3216917cb0867d62ull, 0x34946eb865b2eaabull,
                0x93de2e44470affdbull, 0x11ee49f01025cab7ull,
                0xbbad020c85df3116ull, 0x3194b0f852fe9253ull,
                0xe252873073689d8bull, 0x8b0cd57df32a034bull,
                0xe252873073689d8bull, 0x8b0cd57df32a034bull,
                0xe252873073689d8bull, 0x8b0cd57df32a034bull,
                0xe252873073689d8bull, 0xf25fbca34cc91507ull,
                0xe252873073689d8bull, 0x805dc5541d914cddull,
                0x2cc687e766578183ull, 0x413bd71b36091f2bull,
                0xb1a8f948642db583ull, 0xb87498efff8f0b15ull,
                0x2faa0bed13235713ull, 0x6d146f120a0bcdb5ull,
                0x2cc687e766578183ull, 0x3f124d04d181aa5eull,
                0xf79c067ba1bc34f6ull, 0xf75309bcac42bb83ull,
                0xa3e68e51e618e368ull, 0x5af513209bd9c79dull,
                0xa804ae173352194cull, 0x2cc687e766578183ull,
                0x8887db592e3c9ca1ull, 0xb1a8f948642db583ull,
                0x6973f6b66f456765ull, 0xe9c651f138b929a3ull,
                0x9816424ec49822b7ull,
            };
        if (secondBoardPresentationSequence.size() !=
                expectedSecondBoardPresentationSequence.size() ||
            secondBoardPageDumpFailed ||
            !std::equal(secondBoardPresentationSequence.begin(),
                        secondBoardPresentationSequence.end(),
                        expectedSecondBoardPresentationSequence.begin())) {
            std::cerr << "Captured demo board 2 continuous presentation sequence mismatch:";
            for (const std::uint64_t hash : secondBoardPresentationSequence) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        return true;
    }
    static bool replaysCapturedThirdBoardThroughFifthCapture(Game& game) {
        if (!game.attractMode_ || game.attractBoardIndex_ != 2 ||
            game.page_ != Game::Page::Attract || game.activeBoardMode_ != Game::Mode::Equality ||
            game.target_ != 30 || game.playerRow_ != 1 || game.playerColumn_ != 1) return false;

        // Earlier boards are advanced headlessly by this long replay. In the
        // real app one presentation is consumed after every host update; keep
        // this focused Worker oracle isolated from any unobserved prior frame.
        game.discardPresentationFrames();

        static constexpr std::array<std::string_view, 11> expectedActions = {
            "M7>8", "E8", "M8>9", "E9", "M9>10",
            "E10", "M10>11", "E11", "M11>5", "E5", "F1:"};
        constexpr double frameSeconds = 1.0 / 70.086;
        double elapsed = 0.0;
        std::vector<std::string> actions;
        std::vector<double> actionTimes;
        std::vector<std::string> trace;
        std::array<std::vector<std::uint64_t>, 5> workerEntryHashes;
        std::vector<std::uint64_t> workerPresentedHashes;
        std::uint64_t workerEntryDwellHash = 0;
        std::uint64_t workerCollisionStartPresentedHash = 0;
        Renderer presentationRenderer(game.assets_.graphicsMode());
        const bool auditThirdBoardPresentationSequence =
            std::getenv("MUNCHERS_AUDIT_THIRD_BOARD_SEQUENCE") != nullptr;
        const char* thirdBoardPageDumpDirectoryText =
            std::getenv("MUNCHERS_AUDIT_THIRD_BOARD_PAGE_DIR");
        const bool dumpThirdBoardPages = thirdBoardPageDumpDirectoryText &&
            *thirdBoardPageDumpDirectoryText;
        const std::filesystem::path thirdBoardPageDumpDirectory =
            dumpThirdBoardPages
                ? std::filesystem::path(thirdBoardPageDumpDirectoryText)
                : std::filesystem::path{};
        if (dumpThirdBoardPages) {
            std::error_code error;
            std::filesystem::create_directories(
                thirdBoardPageDumpDirectory, error);
            if (error) return false;
        }
        std::vector<std::uint64_t> thirdBoardPresentationSequence;
        std::vector<int> thirdBoardPresentationStartFrames;
        int thirdBoardPresentationFrame = 0;
        bool thirdBoardPresentationComplete = false;
        bool thirdBoardPageDumpFailed = false;
        const auto recordThirdBoardPresentation =
            [&](const std::uint64_t hash) {
                const bool changed = thirdBoardPresentationSequence.empty() ||
                    thirdBoardPresentationSequence.back() != hash;
                if (changed) {
                    thirdBoardPresentationSequence.push_back(hash);
                    thirdBoardPresentationStartFrames.push_back(
                        thirdBoardPresentationFrame);
                    if (dumpThirdBoardPages) {
                        std::ostringstream name;
                        name << std::hex << hash << std::dec << '-'
                             << thirdBoardPresentationSequence.size() - 1
                             << ".ppm";
                        std::ofstream output(
                            thirdBoardPageDumpDirectory / name.str(),
                            std::ios::binary);
                        if (!output) {
                            thirdBoardPageDumpFailed = true;
                        } else {
                            output << "P6\n" << Renderer::Width << ' '
                                   << Renderer::Height << "\n255\n";
                            for (const std::uint32_t pixel :
                                 presentationRenderer.pixels()) {
                                const char rgb[3] = {
                                    static_cast<char>((pixel >> 16) & 0xffu),
                                    static_cast<char>((pixel >> 8) & 0xffu),
                                    static_cast<char>(pixel & 0xffu),
                                };
                                output.write(rgb, sizeof(rgb));
                            }
                            if (!output) thirdBoardPageDumpFailed = true;
                        }
                    }
                }
                ++thirdBoardPresentationFrame;
            };
        game.renderPresentation(presentationRenderer);
        recordThirdBoardPresentation(
            renderedFrameHash(presentationRenderer));
        while (game.page_ == Game::Page::Attract && elapsed < 15.0) {
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            const Game::Page previousPage = game.page_;
            const int previousRow = game.playerRow_;
            const int previousColumn = game.playerColumn_;
            const std::uint64_t callsBeforeUpdate = game.random_.calls;

            game.update(frameSeconds);
            elapsed += frameSeconds;
            game.renderPresentation(presentationRenderer);
            const std::uint64_t presentedHash = renderedFrameHash(presentationRenderer);
            recordThirdBoardPresentation(presentedHash);

            const auto enteringWorker = std::find_if(
                game.enemies_.begin(), game.enemies_.end(), [](const Game::Enemy& enemy) {
                    return enemy.type == 1 && enemy.row == 4 && enemy.column == 5 &&
                           enemy.fromRow == 4 && enemy.fromColumn == 6 &&
                           enemy.direction == 3;
                });
            if (enteringWorker != game.enemies_.end()) {
                if (enteringWorker->entering && enteringWorker->moving) {
                    const int phase = game.enemyMovementPhase(*enteringWorker);
                    if (phase >= 2 && phase <= 6) {
                        const std::uint64_t hash = fullFrameHash(game);
                        auto& phaseHashes = workerEntryHashes[
                            static_cast<std::size_t>(phase - 2)];
                        if (phaseHashes.empty() || phaseHashes.back() != hash) {
                            phaseHashes.push_back(hash);
                        }
                        if (phase <= 3 &&
                            (workerPresentedHashes.empty() ||
                             workerPresentedHashes.back() != presentedHash)) {
                            workerPresentedHashes.push_back(presentedHash);
                        }
                    }
                } else if (!enteringWorker->entering && !enteringWorker->moving &&
                           workerEntryDwellHash == 0) {
                    workerEntryDwellHash = fullFrameHash(game);
                }
            }

            if (!wasMoving && game.moving_) {
                const int from = game.moveFromRow_ * Game::BoardColumns + game.moveFromColumn_;
                const int to = game.moveToRow_ * Game::BoardColumns + game.moveToColumn_;
                actions.push_back("M" + std::to_string(from) + ">" + std::to_string(to));
                actionTimes.push_back(elapsed);
            }
            if (!wasMunching && game.munching_) {
                actions.push_back("E" + std::to_string(game.munchCellIndex_));
                actionTimes.push_back(elapsed);
            }
            if (previousPage != Game::Page::Feedback && game.page_ == Game::Page::Feedback) {
                actions.push_back("F" + std::to_string(game.feedbackEnemyType_) + ":" +
                                  game.feedbackMessage_);
                actionTimes.push_back(elapsed);
                workerCollisionStartPresentedHash = presentedHash;
            }

            if (game.random_.calls != callsBeforeUpdate || game.moving_ != wasMoving ||
                game.munching_ != wasMunching || game.page_ != previousPage ||
                game.playerRow_ != previousRow || game.playerColumn_ != previousColumn) {
                std::ostringstream line;
                line << elapsed << "s calls " << callsBeforeUpdate << "->" << game.random_.calls
                     << " page=" << static_cast<int>(game.page_)
                     << " player=" << game.playerRow_ << ',' << game.playerColumn_
                     << " move=" << game.moving_ << ' ' << game.moveFromRow_ << ','
                     << game.moveFromColumn_ << "->" << game.moveToRow_ << ','
                     << game.moveToColumn_ << " munch=" << game.munching_
                     << " cell=" << game.munchCellIndex_ << " feedback="
                     << game.feedbackEnemyType_ << ':' << game.feedbackMessage_
                     << " score=" << game.score_ << " enemies=" << game.enemies_.size();
                for (const Game::Enemy& enemy : game.enemies_) {
                    line << " [s" << enemy.slot << "t" << enemy.type << " r" << enemy.row
                          << 'c' << enemy.column << " d" << enemy.direction
                          << " m" << enemy.moving << " e" << enemy.entering;
                    if (enemy.moving) {
                        line << " p" << game.enemyMovementPhase(enemy);
                    }
                    line << ']';
                }
                trace.push_back(line.str());
            }
        }

        bool actionsMatch = actions.size() == expectedActions.size();
        for (std::size_t index = 0; actionsMatch && index < actions.size(); ++index) {
            actionsMatch = actions[index] == expectedActions[index];
        }
        const std::array<double, 10> referenceActionTimes = {
            1.00, 1.95, 3.01, 4.52, 5.07, 6.21, 7.72, 9.16, 9.85, 10.46};
        bool timingsMatch = actionTimes.size() == expectedActions.size();
        for (std::size_t index = 0; timingsMatch && index < referenceActionTimes.size(); ++index) {
            timingsMatch = std::abs(actionTimes[index] - referenceActionTimes[index]) <= 0.20;
        }
        const bool collisionStateMatches = game.page_ == Game::Page::Feedback &&
            game.feedbackKind_ == Game::FeedbackKind::EatenByTroggle &&
            game.feedbackEnemyType_ == 1 && game.feedbackMessage_.empty() &&
            game.playerRow_ == 0 && game.playerColumn_ == 5 && game.deathAnimating_ &&
            game.deathSequenceTicks_ == 0 && game.cell(1, 2).eaten &&
            game.cell(1, 3).eaten && game.cell(1, 4).eaten && game.cell(1, 5).eaten;
        const std::array<std::vector<std::uint64_t>, 5> expectedWorkerEntryHashes = {{
            {0xf6fe900e88a30737ull, 0xc0942cb89de34acbull},
            {0xb8d21853e1ca581eull},
            {0xf0d15d79889d1260ull},
            {0xd3cce8ef612d3b0full},
            {0x5819250ce11b6e9cull},
        }};
        const std::vector<std::uint64_t> expectedWorkerPresentedHashes = {
            0xf6fe900e88a30737ull,
            0xc0942cb89de34acbull,
            0xa5d7f189aefddfb2ull,
            0xb8d21853e1ca581eull,
        };
        const bool workerEntryMatches =
            workerEntryHashes == expectedWorkerEntryHashes &&
            workerPresentedHashes == expectedWorkerPresentedHashes &&
            workerEntryDwellHash == 0xb9828f8af6989002ull;
        if (!actionsMatch || !timingsMatch || !collisionStateMatches ||
            !workerEntryMatches) {
            std::cerr << "Captured demo board 3 route/collision mismatch after " << elapsed
                      << " seconds: actions=";
            for (std::size_t index = 0; index < actions.size(); ++index) {
                std::cerr << ' ' << actions[index] << '@' << actionTimes[index];
            }
            std::cerr << " state=" << collisionStateMatches << " calls=" << game.random_.calls
                      << " workerEntry=" << workerEntryMatches << " hashes=";
            for (const auto& phase : workerEntryHashes) {
                std::cerr << " [";
                for (const std::uint64_t hash : phase) {
                    std::cerr << " 0x" << std::hex << hash;
                }
                std::cerr << " ]";
            }
            std::cerr << " dwell=0x" << std::hex << workerEntryDwellHash
                      << " presented=";
            for (const std::uint64_t hash : workerPresentedHashes) {
                std::cerr << " 0x" << hash;
            }
            std::cerr << std::dec << '\n';
            for (const std::string& line : trace) std::cerr << line << '\n';
            return false;
        }

        const std::uint64_t callsAtCollision = game.random_.calls;
        const int selectedCollisionSlot = game.feedbackEnemySlot_;
        game.feedbackEnemySlot_ = game.enemySlotCount_ + 1;
        const std::uint64_t collisionDwellHash = fullFrameHash(game);
        game.feedbackEnemySlot_ = selectedCollisionSlot;
        if (collisionDwellHash != 0xdbf84ed13c67dedaull) {
            std::cerr << "Captured Worker pre-bite dwell hash=0x" << std::hex
                      << collisionDwellHash << ", expected 0xdbf84ed13c67deda"
                      << std::dec << '\n';
            return false;
        }
        double biteElapsed = 0.0;
        std::vector<std::string> biteTrace;
        std::vector<std::uint64_t> workerCollisionPresentedHashes;
        if (workerCollisionStartPresentedHash != 0) {
            workerCollisionPresentedHashes.push_back(
                workerCollisionStartPresentedHash);
        }
        std::array<std::uint64_t, Game::TroggleEatAnimationTicks>
            workerBiteLogicalHashes{};
        workerBiteLogicalHashes[0] = fullFrameHash(game);
        while (game.deathAnimating_ && biteElapsed < 2.0) {
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            const auto safeJobsBefore = game.safeZoneJobs_;
            game.update(frameSeconds);
            biteElapsed += frameSeconds;
            if (game.deathAnimating_ && game.deathSequenceTicks_ >= 0 &&
                game.deathSequenceTicks_ < Game::TroggleEatAnimationTicks) {
                workerBiteLogicalHashes[static_cast<std::size_t>(
                    game.deathSequenceTicks_)] = fullFrameHash(game);
            }
            game.renderPresentation(presentationRenderer);
            const std::uint64_t bitePresentedHash =
                renderedFrameHash(presentationRenderer);
            recordThirdBoardPresentation(bitePresentedHash);
            if (workerCollisionPresentedHashes.empty() ||
                workerCollisionPresentedHashes.back() != bitePresentedHash) {
                workerCollisionPresentedHashes.push_back(bitePresentedHash);
            }
            if (game.random_.calls != callsBeforeUpdate) {
                std::ostringstream line;
                line << biteElapsed << "s tick=" << game.deathSequenceTicks_ << " calls "
                     << callsBeforeUpdate << "->" << game.random_.calls << " enemies=";
                for (const Game::Enemy& enemy : game.enemies_) {
                    line << " [s" << enemy.slot << "t" << enemy.type << " r" << enemy.row
                         << 'c' << enemy.column << " d" << enemy.direction
                         << " m" << enemy.moving << " e" << enemy.entering << ']';
                }
                line << " safe=";
                for (int index = 0; index < game.safeZoneJobCount_; ++index) {
                    const Game::SafeZoneJob& before =
                        safeJobsBefore[static_cast<std::size_t>(index)];
                    const Game::SafeZoneJob& after =
                        game.safeZoneJobs_[static_cast<std::size_t>(index)];
                    line << " [j" << index << ' ' << before.active << ':' << before.cellIndex
                         << '@' << before.timer << "->" << after.active << ':'
                         << after.cellIndex << '@' << after.timer << ']';
                }
                biteTrace.push_back(line.str());
            }
        }
        const auto laterEntry = std::find_if(
            game.enemies_.begin(), game.enemies_.end(),
            [](const Game::Enemy& enemy) { return enemy.slot == 2; });
        const std::uint64_t workerFeedbackHash = fullFrameHash(game);
        if (auditThirdBoardPresentationSequence) {
            std::cerr << "THIRD_BOARD_BITE_PRNG calls=" << callsAtCollision
                      << "->" << game.random_.calls << '\n';
            for (const std::string& line : biteTrace) {
                std::cerr << "THIRD_BOARD_BITE_PRNG " << line << '\n';
            }
        }
        static constexpr std::array<std::uint64_t,
                                    Game::TroggleEatAnimationTicks>
            ExpectedWorkerBiteLogicalHashes = {
                0xf8e5c94e49765996ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0x86ddf50f242878e4ull,
                0x28b02fdb861bba66ull, 0x0a94f93fe8d7e660ull,
                0x6eead36170fa7886ull, 0xe2fdfe9fa771bcb6ull,
                0xc7902b1b3549d7f4ull, 0x8c2f87ab2e8faa40ull,
                0xf20c3488c9978b6eull, 0x8c2f87ab2e8faa40ull,
            };
        static const std::vector<std::uint64_t>
            ExpectedWorkerCleanPresentedHashes = {
                0xdbf84ed13c67dedaull, 0xf8e5c94e49765996ull,
                0x64c5c546d7b61954ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0x86ddf50f242878e4ull,
                0x28b02fdb861bba66ull, 0x86ddf50f242878e4ull,
                0x6eead36170fa7886ull, 0x0a94f93fe8d7e660ull,
                0xe2fdfe9fa771bcb6ull, 0xc7902b1b3549d7f4ull,
                0xe2fdfe9fa771bcb6ull, 0x8c2f87ab2e8faa40ull,
                0xf20c3488c9978b6eull, 0x8c2f87ab2e8faa40ull,
                0x89cd39b70d508c96ull,
            };
        if (game.deathAnimating_ || game.deathSequenceTicks_ != Game::TroggleEatAnimationTicks ||
            game.feedbackMessage_ != "Aargh" || game.random_.calls != callsAtCollision + 4 ||
            std::abs(biteElapsed - 21.0 / OriginalSchedulerTicksPerSecond) > 0.03 ||
            laterEntry == game.enemies_.end() || game.enemyMovementPhase(*laterEntry) != 4 ||
            workerFeedbackHash != 0x89cd39b70d508c96ull ||
            workerBiteLogicalHashes != ExpectedWorkerBiteLogicalHashes ||
            workerCollisionPresentedHashes != ExpectedWorkerCleanPresentedHashes) {
            std::cerr << "Captured demo board 3 bite/feedback phrase mismatch: bite="
                      << biteElapsed << " ticks=" << game.deathSequenceTicks_
                      << " message='" << game.feedbackMessage_ << "' calls="
                      << callsAtCollision << "->" << game.random_.calls
                      << " laterPhase="
                      << (laterEntry == game.enemies_.end()
                              ? -1 : game.enemyMovementPhase(*laterEntry))
                      << " feedbackHash=0x" << std::hex << workerFeedbackHash
                      << " logical="
                      << (workerBiteLogicalHashes == ExpectedWorkerBiteLogicalHashes)
                      << " presented="
                      << (workerCollisionPresentedHashes ==
                          ExpectedWorkerCleanPresentedHashes)
                      << std::dec << '\n';
            for (const std::string& line : biteTrace) std::cerr << line << '\n';
            std::cerr << "Presented hashes:";
            for (const std::uint64_t hash : workerCollisionPresentedHashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }

        static constexpr std::array<std::string_view, Game::BoardCellCount> fourthLabels = {
            "45",  "690", "165", "92",  "294", "516",
            "675", "180", "564", "490", "690", "600",
            "420", "",    "504", "90",  "555", "195",
            "180", "744", "726", "480", "43",  "405",
            "49",  "90",  "720", "165", "705", "390"};
        double lifecycleElapsed = elapsed + biteElapsed;
        double hallAt = -1.0;
        double splashAt = -1.0;
        double fourthBoardAt = -1.0;
        std::uint64_t fourthInitializerStart = 0;
        std::uint64_t equalityHallHash = 0;
        while (lifecycleElapsed < 70.0) {
            const Game::Page previousPage = game.page_;
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            game.update(frameSeconds);
            lifecycleElapsed += frameSeconds;
            if (!thirdBoardPresentationComplete) {
                if (game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::Wipe) {
                    thirdBoardPresentationComplete = true;
                } else {
                    game.renderPresentation(presentationRenderer);
                    const std::uint64_t thirdBoardHash =
                        renderedFrameHash(presentationRenderer);
                    recordThirdBoardPresentation(thirdBoardHash);
                }
            }
            if (hallAt < 0.0 && previousPage != Game::Page::Hall &&
                game.page_ == Game::Page::Hall) {
                hallAt = lifecycleElapsed;
                equalityHallHash = fullFrameHash(game);
            }
            if (splashAt < 0.0 && previousPage != Game::Page::StartupSplash &&
                game.page_ == Game::Page::StartupSplash) {
                splashAt = lifecycleElapsed;
            }
            if (game.attractBoardIndex_ == 3 && game.page_ == Game::Page::Attract) {
                fourthBoardAt = lifecycleElapsed;
                fourthInitializerStart = callsBeforeUpdate;
                break;
            }
        }
        if (auditThirdBoardPresentationSequence) {
            std::cerr << "THIRD_BOARD_PRESENTATION_SEQUENCE";
            for (std::size_t index = 0;
                 index < thirdBoardPresentationSequence.size(); ++index) {
                std::cerr << " 0x" << std::hex
                          << thirdBoardPresentationSequence[index]
                          << std::dec << '@'
                          << thirdBoardPresentationStartFrames[index];
            }
            std::cerr << '\n';
        }
        constexpr std::array<std::uint64_t, 106>
            expectedThirdBoardPresentationSequence = {
                0x9816424ec49822b7ull, 0x63e69085456d3dadull,
                0xb8813a9400a54386ull, 0x401262c9d99d0d39ull,
                0x2780a8f00516fc0aull, 0x7b4e5c6b52fdf070ull,
                0x68deb45e22334133ull, 0xd1488edbc0a94082ull,
                0x3aed40efd723c425ull, 0x378c7f08561aa1a5ull,
                0x3aed40efd723c425ull, 0x378c7f08561aa1a5ull,
                0x3aed40efd723c425ull, 0x378c7f08561aa1a5ull,
                0x3aed40efd723c425ull, 0x378c7f08561aa1a5ull,
                0xb3cf488eae27b48full, 0x3d0ca97719b81300ull,
                0x9022271b44a0784full, 0x76757ff7da7e54e4ull,
                0xb0f60bb02eab9382ull, 0x09ceadef4a501f75ull,
                0x7cc14d9ad0e66adcull, 0xa5c8e76b453d8521ull,
                0x54383fb30a8eebc5ull, 0xa5c8e76b453d8521ull,
                0x54383fb30a8eebc5ull, 0xa5c8e76b453d8521ull,
                0x54383fb30a8eebc5ull, 0xa5c8e76b453d8521ull,
                0x54383fb30a8eebc5ull, 0x82e04177d33d58abull,
                0xf7511068d8a22fc8ull, 0xbbcce5df3f2b3c0bull,
                0x5e611bd97b1a5540ull, 0x2fd3bf2faf34aa7eull,
                0x505a2768398470c5ull, 0xf11ae0ef2f71a7f4ull,
                0xd4ce5454721c4a2aull, 0x2db9df7ff4d6d82dull,
                0x30554fedda6fc869ull, 0x2db9df7ff4d6d82dull,
                0x30554fedda6fc869ull, 0x2db9df7ff4d6d82dull,
                0x30554fedda6fc869ull, 0x2db9df7ff4d6d82dull,
                0x30554fedda6fc869ull, 0x965c0e6a86133747ull,
                0x896417e2a504cb58ull, 0x82ae4446511f0c7bull,
                0xdcd6c94e6a656148ull, 0x998f0229b678e59aull,
                0x43f0f9d5e5806601ull, 0x6834f19513bdb88cull,
                0xde050eee8f69eb3full, 0xc3d5ff8a1eae144bull,
                0xde050eee8f69eb3full, 0xc3d5ff8a1eae144bull,
                0xde050eee8f69eb3full, 0xf6fe900e88a30737ull,
                0xc0942cb89de34acbull, 0xa5d7f189aefddfb2ull,
                0xb8d21853e1ca581eull, 0xf0d15d79889d1260ull,
                0xd3cce8ef612d3b0full, 0x5819250ce11b6e9cull,
                0xb9828f8af6989002ull, 0xda032615bc8a1024ull,
                0xd8aea9f77b56159full, 0xacda0ec1a28fd498ull,
                0x5be988a73bd1ec4cull, 0xc970a918b51d0027ull,
                0x81bf9f184ced24afull, 0x43574dc5f334efbeull,
                0xcb1c546a6abe426full, 0xea389a1bc2afdd27ull,
                0x3fc5cb617d85c1c7ull, 0x427d39fd421bfd57ull,
                0x0975ecb7c2bbc617ull, 0xf81ad2ca3b0192c2ull,
                0xdbf84ed13c67dedaull, 0xf8e5c94e49765996ull,
                0x64c5c546d7b61954ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0xe9a0890639ba7aa6ull,
                0x2f6a98e811428c64ull, 0x86ddf50f242878e4ull,
                0x28b02fdb861bba66ull, 0x86ddf50f242878e4ull,
                0x6eead36170fa7886ull, 0x0a94f93fe8d7e660ull,
                0xe2fdfe9fa771bcb6ull, 0xc7902b1b3549d7f4ull,
                0xe2fdfe9fa771bcb6ull, 0x8c2f87ab2e8faa40ull,
                0xf20c3488c9978b6eull, 0x8c2f87ab2e8faa40ull,
                0x89cd39b70d508c96ull, 0x25a5e4cc4a413f32ull,
            };
        if (thirdBoardPageDumpFailed ||
            thirdBoardPresentationSequence.size() !=
                expectedThirdBoardPresentationSequence.size() ||
            !std::equal(thirdBoardPresentationSequence.begin(),
                        thirdBoardPresentationSequence.end(),
                        expectedThirdBoardPresentationSequence.begin())) {
            std::cerr << "Captured demo board 3 continuous presentation sequence mismatch:";
            for (const std::uint64_t hash : thirdBoardPresentationSequence) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        bool fourthStateMatches = game.attractMode_ && game.attractBoardIndex_ == 3 &&
            game.page_ == Game::Page::Attract && game.activeBoardMode_ == Game::Mode::Multiples &&
            game.target_ == 15 && game.level_ == 10 && game.attractDifficultyIndex_ == 9 &&
            game.playerRow_ == 2 && game.playerColumn_ == 1;
        for (int index = 0; fourthStateMatches && index < Game::BoardCellCount; ++index) {
            fourthStateMatches = game.cells_[static_cast<std::size_t>(index)].label ==
                                 fourthLabels[static_cast<std::size_t>(index)];
        }
        while (game.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            game.update(frameSeconds);
            lifecycleElapsed += frameSeconds;
        }
        fourthBoardAt = lifecycleElapsed;
        if (hallAt < 0.0 || splashAt < 0.0 || fourthBoardAt < 0.0 ||
            std::abs(hallAt - 17.36) > 0.20 || std::abs(splashAt - 32.81) > 0.20 ||
            std::abs(fourthBoardAt - 48.47) > 0.20 || fourthInitializerStart != 507 ||
            equalityHallHash != 0xae0c27ac1fa0e50dull || !fourthStateMatches) {
            std::cerr << "Captured demo board 3 terminal lifecycle/board 4 mismatch: hall="
                      << hallAt << " splash=" << splashAt << " board4=" << fourthBoardAt
                      << " start=" << fourthInitializerStart << " hallHash=0x" << std::hex
                      << equalityHallHash << std::dec << " calls=" << game.random_.calls
                      << " mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_ << " level=" << game.level_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_ << '\n';
            std::cerr << "Board 4 labels:";
            for (const Game::Cell& current : game.cells_) {
                std::cerr << " ['" << current.label << "']";
            }
            std::cerr << '\n';
            return false;
        }

        constexpr std::uint64_t capturedFourthFrameHash = 0x373eb5db7ab6534bull;
        if (fullFrameHash(game) != capturedFourthFrameHash) {
            std::cerr << "Captured demo board 4 full-frame hash diverged before replay\n";
            return false;
        }

        // The supplied recording exits from the preceding Hall and starts a
        // fresh Demo batch from the title. Nothing after this initializer is
        // evidence for an uninterrupted board-3-to-board-4 route. Keep the
        // older deterministic native continuation available only as an
        // explicitly requested development probe; the permanent source gate
        // below reproduces the real external key and title-idle restart.
        if (std::getenv("MUNCHERS_AUDIT_UNCAPTURED_NATIVE_CONTINUATION") == nullptr) {
            return true;
        }

        const bool auditFourthBoardPresentationSequence =
            std::getenv("MUNCHERS_AUDIT_FOURTH_BOARD_SEQUENCE") != nullptr;
        Renderer fourthBoardPresentationRenderer(game.assets_.graphicsMode());
        std::vector<std::uint64_t> fourthBoardPresentationSequence;
        std::vector<int> fourthBoardPresentationStartFrames;
        int fourthBoardPresentationFrame = 0;
        bool fourthBoardPresentationComplete = false;
        const auto recordFourthBoardPresentation =
            [&](const std::uint64_t hash) {
                if (fourthBoardPresentationSequence.empty() ||
                    fourthBoardPresentationSequence.back() != hash) {
                    fourthBoardPresentationSequence.push_back(hash);
                    fourthBoardPresentationStartFrames.push_back(
                        fourthBoardPresentationFrame);
                }
                ++fourthBoardPresentationFrame;
            };
        if (auditFourthBoardPresentationSequence) {
            while (!game.presentationFrames_.empty()) {
                game.renderPresentation(fourthBoardPresentationRenderer);
            }
            game.renderPresentation(fourthBoardPresentationRenderer);
            recordFourthBoardPresentation(
                renderedFrameHash(fourthBoardPresentationRenderer));
        }

        static constexpr std::array<std::string_view, 34> expectedFourthActions = {
            "M13>12", "E12", "M12>6", "E6", "M6>7", "E7", "M7>1", "E1",
            "M1>2", "E2", "M2>1", "M1>7", "M7>13", "M13>14", "M14>15", "E15",
            "M15>16", "E16", "M16>10", "E10", "M10>11", "E11", "M11>17", "E17",
            "M17>23", "M23>22", "E22", "M22>28", "E28", "M28>29", "M29>28",
            "M28>29", "M29>28", "F0:"};
        static constexpr std::array<double, 34> referenceFourthActionTimes = {
            1.04, 2.51, 3.24, 4.57, 5.39, 6.71, 8.18, 8.69,
            10.20, 10.82, 12.33, 15.60, 17.06, 17.82, 19.16, 19.85,
            21.36, 22.29, 23.61, 24.88, 26.37, 27.12, 27.71, 28.66,
            29.56, 30.08, 31.40, 32.62, 34.12, 35.90, 36.80, 38.82,
            39.58, 40.51};
        double boardFourElapsed = 0.0;
        std::vector<std::string> fourthActions;
        std::vector<double> fourthActionTimes;
        bool fourthCollisionStateMatches = false;
        int fourthCollisionType = -1;
        int fourthCollisionPlayerRow = -1;
        int fourthCollisionPlayerColumn = -1;
        int fourthCollisionScore = -1;
        std::size_t fourthCollisionEnemies = 0;
        std::uint64_t fourthCollisionCalls = 0;
        std::uint64_t fourthTerminalCalls = 0;
        std::string fourthTerminalMessage;
        int fourthTerminalTicks = -1;
        bool fourthTerminalStateMatches = false;
        double fourthTerminalAt = -1.0;
        double fourthHallAt = -1.0;
        std::uint64_t fourthHallHash = 0;
        const std::uint64_t fourthBoardStartCalls = game.random_.calls;
        while (boardFourElapsed < 120.0 && game.page_ != Game::Page::Hall) {
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            const bool wasDeathAnimating = game.deathAnimating_;
            const Game::Page previousPage = game.page_;
            game.update(frameSeconds);
            boardFourElapsed += frameSeconds;
            if (auditFourthBoardPresentationSequence &&
                !fourthBoardPresentationComplete) {
                if (game.attractHallTransitionPhase_ ==
                    Game::AttractHallTransitionPhase::Wipe) {
                    fourthBoardPresentationComplete = true;
                } else {
                    game.renderPresentation(fourthBoardPresentationRenderer);
                    recordFourthBoardPresentation(
                        renderedFrameHash(fourthBoardPresentationRenderer));
                }
            }

            if (!wasMoving && game.moving_) {
                const int from = game.moveFromRow_ * Game::BoardColumns + game.moveFromColumn_;
                const int to = game.moveToRow_ * Game::BoardColumns + game.moveToColumn_;
                fourthActions.push_back("M" + std::to_string(from) + ">" +
                                        std::to_string(to));
                fourthActionTimes.push_back(boardFourElapsed);
            }
            if (!wasMunching && game.munching_) {
                fourthActions.push_back("E" + std::to_string(game.munchCellIndex_));
                fourthActionTimes.push_back(boardFourElapsed);
            }
            if (previousPage != Game::Page::Feedback && game.page_ == Game::Page::Feedback) {
                fourthActions.push_back("F" + std::to_string(game.feedbackEnemyType_) + ":" +
                                        game.feedbackMessage_);
                fourthActionTimes.push_back(boardFourElapsed);
                fourthCollisionType = game.feedbackEnemyType_;
                fourthCollisionPlayerRow = game.playerRow_;
                fourthCollisionPlayerColumn = game.playerColumn_;
                fourthCollisionScore = game.score_;
                fourthCollisionEnemies = game.enemies_.size();
                fourthCollisionCalls = game.random_.calls;
                fourthCollisionStateMatches = game.feedbackKind_ ==
                        Game::FeedbackKind::EatenByTroggle &&
                    game.feedbackEnemyType_ == 0 && game.feedbackMessage_.empty() &&
                    game.playerRow_ == 4 && game.playerColumn_ == 4 &&
                    game.deathAnimating_ && game.score_ == 480 &&
                    game.enemies_.size() == 3;
            }
            if (wasDeathAnimating && !game.deathAnimating_) {
                fourthTerminalAt = boardFourElapsed;
                fourthTerminalCalls = game.random_.calls;
                fourthTerminalMessage = game.feedbackMessage_;
                fourthTerminalTicks = game.deathSequenceTicks_;
                fourthTerminalStateMatches = game.feedbackMessage_ == "Oops" &&
                    game.deathSequenceTicks_ == Game::TroggleEatAnimationTicks &&
                    game.random_.calls == 753;
            }
            if (previousPage != Game::Page::Hall && game.page_ == Game::Page::Hall) {
                fourthHallAt = boardFourElapsed;
                fourthHallHash = fullFrameHash(game);
            }
        }

        if (auditFourthBoardPresentationSequence) {
            std::cerr << "FOURTH_BOARD_PRESENTATION_SEQUENCE";
            for (std::size_t index = 0;
                 index < fourthBoardPresentationSequence.size(); ++index) {
                std::cerr << " 0x" << std::hex
                          << fourthBoardPresentationSequence[index]
                          << std::dec << '@'
                          << fourthBoardPresentationStartFrames[index];
            }
            std::cerr << '\n';
        }

        bool fourthActionsMatch = fourthActions.size() == expectedFourthActions.size();
        for (std::size_t index = 0; fourthActionsMatch && index < fourthActions.size(); ++index) {
            fourthActionsMatch = fourthActions[index] == expectedFourthActions[index] &&
                std::abs(fourthActionTimes[index] - referenceFourthActionTimes[index]) <= 0.18;
        }
        if (!fourthActionsMatch || !fourthCollisionStateMatches ||
            !fourthTerminalStateMatches || fourthBoardStartCalls != 584 ||
            std::abs(fourthTerminalAt - 41.22) > 0.18 ||
            std::abs(fourthHallAt - 47.07) > 0.20 ||
            fourthHallHash != 0x0dc6649d785d392aull) {
            std::cerr << "Captured demo board 4 route/collision/Hall mismatch: calls="
                      << fourthBoardStartCalls << "->" << game.random_.calls
                      << " collision=" << fourthCollisionStateMatches
                      << " [type=" << fourthCollisionType << " player="
                      << fourthCollisionPlayerRow << ',' << fourthCollisionPlayerColumn
                      << " score=" << fourthCollisionScore << " enemies="
                      << fourthCollisionEnemies << " calls=" << fourthCollisionCalls << ']'
                      << " terminal=" << fourthTerminalStateMatches << '@' << fourthTerminalAt
                      << " calls=" << fourthTerminalCalls << " ticks=" << fourthTerminalTicks
                      << " message='" << fourthTerminalMessage << '\''
                      << " hall=" << fourthHallAt << " hallHash=0x" << std::hex
                      << fourthHallHash << std::dec << " actions=";
            for (std::size_t index = 0; index < fourthActions.size(); ++index) {
                std::cerr << ' ' << fourthActions[index] << '@' << fourthActionTimes[index];
            }
            std::cerr << '\n';
            return false;
        }

        double boardFourLifecycleElapsed = boardFourElapsed;
        double fourthSplashAt = -1.0;
        double fifthBoardAt = -1.0;
        std::uint64_t fifthInitializerStart = 0;
        while (boardFourLifecycleElapsed < 120.0 && game.attractBoardIndex_ == 3) {
            const Game::Page previousPage = game.page_;
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            game.update(frameSeconds);
            boardFourLifecycleElapsed += frameSeconds;
            if (fourthSplashAt < 0.0 && previousPage != Game::Page::StartupSplash &&
                game.page_ == Game::Page::StartupSplash) {
                fourthSplashAt = boardFourLifecycleElapsed;
            }
            if (game.attractBoardIndex_ == 4) {
                fifthBoardAt = boardFourLifecycleElapsed;
                fifthInitializerStart = callsBeforeUpdate;
            }
        }

        static constexpr std::array<std::string_view, Game::BoardCellCount> fifthLabels = {
            "1", "2", "1", "2", "2", "1",
            "1", "2", "1", "1", "1", "2",
            "2", "2", "2", "2", "",  "2",
            "1", "1", "1", "2", "1", "1",
            "2", "1", "2", "2", "2", "2"};
        bool fifthStateMatches = game.attractMode_ && game.attractBoardIndex_ == 4 &&
            game.page_ == Game::Page::Attract &&
            game.activeBoardMode_ == Game::Mode::Primes && game.target_ == 0 &&
            game.level_ == 7 && game.attractDifficultyIndex_ == 6 &&
            game.playerRow_ == 2 && game.playerColumn_ == 4 &&
            game.random_.calls == 814 && game.correctRemaining_ == 16;
        for (int index = 0; fifthStateMatches && index < Game::BoardCellCount; ++index) {
            fifthStateMatches = game.cells_[static_cast<std::size_t>(index)].label ==
                                fifthLabels[static_cast<std::size_t>(index)];
        }
        while (game.attractInterstitialTransition_ !=
               Game::AttractInterstitialTransition::None) {
            game.update(frameSeconds);
            boardFourLifecycleElapsed += frameSeconds;
        }
        fifthBoardAt = boardFourLifecycleElapsed;
        const std::uint64_t fifthFrameHash = fullFrameHash(game);
        if (std::abs(fourthSplashAt - 62.48) > 0.20 ||
            std::abs(fifthBoardAt - 78.13) > 0.20 || fifthInitializerStart != 753 ||
            !fifthStateMatches || fifthFrameHash != 0xc441aaa9554e82afull) {
            std::cerr << "Captured demo board 4 lifecycle/board 5 initializer mismatch: splash="
                      << fourthSplashAt << " board5=" << fifthBoardAt
                      << " start=" << fifthInitializerStart << " hash=0x" << std::hex
                      << fifthFrameHash << std::dec << " calls=" << game.random_.calls
                      << " mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_ << " level=" << game.level_
                      << " difficulty=" << game.attractDifficultyIndex_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " remaining=" << game.correctRemaining_
                      << " labels=";
            for (const Game::Cell& current : game.cells_) {
                std::cerr << " ['" << current.label << "']";
            }
            std::cerr << '\n';
            return false;
        }

        // With state-5 collision suppression restored, the continuous replay
        // reaches a Prime board here. Its autonomous route is cut short when
        // independently scheduled Troggle trail effects remove the remaining
        // positive cells and enter the attract Hall.
        static constexpr std::array<std::string_view, 11> expectedFifthActions = {
            "M16>17", "E17", "M17>11", "E11", "M11>10", "M10>4",
            "E4", "M4>3", "E3", "M3>2", "E2"};
        static constexpr std::array<double, 11> referenceFifthActionTimes = {
            1.04, 2.20, 3.27, 3.81, 5.02, 6.12, 6.76, 7.86, 8.96, 9.72, 10.47};
        double boardFiveElapsed = 0.0;
        double fifthHallAt = -1.0;
        std::vector<std::string> fifthActions;
        std::vector<double> fifthActionTimes;
        while (boardFiveElapsed < 27.0 && game.page_ != Game::Page::Hall) {
            const Game::Page previousPage = game.page_;
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            game.update(frameSeconds);
            boardFiveElapsed += frameSeconds;
            if (!wasMoving && game.moving_) {
                const int from = game.moveFromRow_ * Game::BoardColumns + game.moveFromColumn_;
                const int to = game.moveToRow_ * Game::BoardColumns + game.moveToColumn_;
                fifthActions.push_back("M" + std::to_string(from) + ">" +
                                       std::to_string(to));
                fifthActionTimes.push_back(boardFiveElapsed);
            }
            if (!wasMunching && game.munching_) {
                fifthActions.push_back("E" + std::to_string(game.munchCellIndex_));
                fifthActionTimes.push_back(boardFiveElapsed);
            }
            if (previousPage != Game::Page::Hall && game.page_ == Game::Page::Hall) {
                fifthHallAt = boardFiveElapsed;
            }
        }

        bool fifthActionsMatch = fifthActions.size() == expectedFifthActions.size();
        for (std::size_t index = 0; fifthActionsMatch && index < fifthActions.size(); ++index) {
            fifthActionsMatch = fifthActions[index] == expectedFifthActions[index] &&
                std::abs(fifthActionTimes[index] - referenceFifthActionTimes[index]) <= 0.18;
        }
        if (!fifthActionsMatch || fifthHallAt < 0.0 ||
            game.page_ != Game::Page::Hall || game.attractBoardIndex_ != 4 ||
            game.playerRow_ != 0 || game.playerColumn_ != 2 || game.score_ != 100 ||
            game.correctRemaining_ != 12 || game.random_.calls != 839) {
            std::cerr << "Captured demo board 5 route diverged at " << boardFiveElapsed
                      << "s: hall=" << fifthHallAt
                      << " page=" << static_cast<int>(game.page_)
                      << " feedback=" << game.feedbackEnemyType_ << ':' << game.feedbackMessage_
                      << " death=" << game.deathAnimating_ << ':' << game.deathSequenceTicks_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " score=" << game.score_ << " remaining=" << game.correctRemaining_
                      << " calls=" << game.random_.calls << " actions=";
            for (std::size_t index = 0; index < fifthActions.size(); ++index) {
                std::cerr << ' ' << fifthActions[index] << '@' << fifthActionTimes[index];
            }
            std::cerr << '\n';
            return false;
        }
        struct PostCaptureTransition {
            double seconds;
            Game::Page page;
            int board;
            bool attract;
            std::uint64_t calls;
            Game::Mode mode;
            int target;
            int level;
            int score;
        };
        // Every Hall contributes its measured six-frame closing Wipe, and
        // every logo contributes seven frames before the first board-paint
        // frame changes Page/board state. Boards after the supplied recording
        // are a deterministic native continuation, not live-reference claims.
        static constexpr std::array expectedPostCapture = {
            PostCaptureTransition{15.538, Game::Page::StartupSplash, 4, true, 839, Game::Mode::Primes, 0, 7, 100},
            PostCaptureTransition{31.076, Game::Page::Attract, 5, true, 916, Game::Mode::Multiples, 19, 10, 0},
            PostCaptureTransition{41.920, Game::Page::Feedback, 5, true, 946, Game::Mode::Multiples, 19, 10, 80},
            PostCaptureTransition{48.455, Game::Page::Hall, 5, true, 952, Game::Mode::Multiples, 19, 10, 80},
            PostCaptureTransition{63.993, Game::Page::StartupSplash, 5, true, 952, Game::Mode::Multiples, 19, 10, 80},
            PostCaptureTransition{79.531, Game::Page::Attract, 6, true, 1041, Game::Mode::Factors, 71, 8, 0},
            PostCaptureTransition{91.274, Game::Page::Feedback, 6, true, 1068, Game::Mode::Factors, 71, 8, 120},
            PostCaptureTransition{97.794, Game::Page::Hall, 6, true, 1074, Game::Mode::Factors, 71, 8, 120},
            PostCaptureTransition{113.332, Game::Page::StartupSplash, 6, true, 1074, Game::Mode::Factors, 71, 8, 120},
            PostCaptureTransition{128.870, Game::Page::Attract, 7, true, 1136, Game::Mode::Primes, 0, 7, 0},
            PostCaptureTransition{141.255, Game::Page::Feedback, 7, true, 1159, Game::Mode::Primes, 0, 7, 125},
            PostCaptureTransition{147.790, Game::Page::Hall, 7, true, 1167, Game::Mode::Primes, 0, 7, 125},
            PostCaptureTransition{163.328, Game::Page::StartupSplash, 7, true, 1167, Game::Mode::Primes, 0, 7, 125},
            PostCaptureTransition{178.866, Game::Page::Attract, 8, true, 1308, Game::Mode::Equality, 14, 5, 0},
            PostCaptureTransition{297.135, Game::Page::Feedback, 8, true, 1675, Game::Mode::Equality, 14, 5, 240},
            PostCaptureTransition{303.656, Game::Page::Hall, 8, true, 1677, Game::Mode::Equality, 14, 5, 240},
            PostCaptureTransition{319.194, Game::Page::StartupSplash, 8, true, 1677, Game::Mode::Equality, 14, 5, 240},
        };

        double postCaptureElapsed = 0.0;
        Game::Page previousPostPage = game.page_;
        int previousPostBoard = game.attractBoardIndex_;
        bool previousPostAttract = game.attractMode_;
        std::vector<PostCaptureTransition> postCaptureTransitions;
        while (postCaptureElapsed < 340.0 &&
               postCaptureTransitions.size() < expectedPostCapture.size()) {
            game.update(frameSeconds);
            postCaptureElapsed += frameSeconds;
            if (game.page_ != previousPostPage || game.attractBoardIndex_ != previousPostBoard ||
                game.attractMode_ != previousPostAttract) {
                postCaptureTransitions.push_back({
                    postCaptureElapsed, game.page_, game.attractBoardIndex_, game.attractMode_,
                    game.random_.calls, game.activeBoardMode_, game.target_, game.level_, game.score_});
                previousPostPage = game.page_;
                previousPostBoard = game.attractBoardIndex_;
                previousPostAttract = game.attractMode_;
            }
        }
        if (postCaptureTransitions.size() != expectedPostCapture.size()) {
            std::cerr << "Post-capture attract transition count "
                      << postCaptureTransitions.size() << " != "
                      << expectedPostCapture.size() << '\n';
            for (std::size_t index = 0; index < postCaptureTransitions.size(); ++index) {
                const PostCaptureTransition& actual = postCaptureTransitions[index];
                std::cerr << index << ": {" << actual.seconds << ", page="
                          << static_cast<int>(actual.page) << ", board=" << actual.board
                          << ", attract=" << actual.attract << ", calls=" << actual.calls
                          << ", mode=" << static_cast<int>(actual.mode)
                          << ", target=" << actual.target << ", level=" << actual.level
                          << ", score=" << actual.score << "}\n";
            }
            return false;
        }
        bool transitionsMatch = true;
        for (std::size_t index = 0; index < expectedPostCapture.size(); ++index) {
            const PostCaptureTransition& actual = postCaptureTransitions[index];
            const PostCaptureTransition& expected = expectedPostCapture[index];
            if (std::abs(actual.seconds - expected.seconds) > 0.03 ||
                actual.page != expected.page || actual.board != expected.board ||
                actual.attract != expected.attract || actual.calls != expected.calls ||
                actual.mode != expected.mode || actual.target != expected.target ||
                actual.level != expected.level || actual.score != expected.score) {
                std::cerr << "Post-capture attract transition " << index << " diverged: actual="
                          << actual.seconds << "s/page=" << static_cast<int>(actual.page)
                          << "/board=" << actual.board << "/attract=" << actual.attract
                          << "/calls=" << actual.calls << "/mode="
                          << static_cast<int>(actual.mode) << "/target=" << actual.target
                          << "/level=" << actual.level << "/score=" << actual.score
                          << " expected=" << expected.seconds << "s/page="
                          << static_cast<int>(expected.page) << "/board=" << expected.board
                          << "/attract=" << expected.attract << "/calls=" << expected.calls
                          << "/mode=" << static_cast<int>(expected.mode) << "/target="
                          << expected.target << "/level=" << expected.level << "/score="
                          << expected.score << '\n';
                transitionsMatch = false;
            }
        }
        return transitionsMatch;
    }
    static bool replaysCapturedRestartedFourthBoard(Game& game) {
        if (!game.attractMode_ || game.attractBoardIndex_ != 2 ||
            game.page_ != Game::Page::Attract ||
            game.activeBoardMode_ != Game::Mode::Equality ||
            game.target_ != 30 || game.random_.calls != 480) {
            std::cerr << "Restart replay precondition: attract=" << game.attractMode_
                      << " board=" << game.attractBoardIndex_
                      << " page=" << static_cast<int>(game.page_)
                      << " mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_
                      << " calls=" << game.random_.calls << '\n';
            return false;
        }

        constexpr double frameSeconds = 1.0 / 70.086;
        Renderer presentationRenderer(game.assets_.graphicsMode());
        double elapsed = 0.0;
        while (game.page_ != Game::Page::Hall && elapsed < 30.0) {
            game.update(frameSeconds);
            game.renderPresentation(presentationRenderer);
            elapsed += frameSeconds;
        }
        if (game.page_ != Game::Page::Hall || game.random_.calls != 507) {
            std::cerr << "Restart replay Hall: page=" << static_cast<int>(game.page_)
                      << " calls=" << game.random_.calls
                      << " elapsed=" << elapsed << '\n';
            return false;
        }

        // The lossless capture's third Hall is not an autonomous cutoff: an
        // external any-key event arrives 10.629752 seconds into the ordinary
        // 450-tick Hall. Reproduce that event, then let the title's normal
        // 30-second idle timeout start the fresh captured Demo batch.
        double hallElapsed = 0.0;
        while (hallElapsed + frameSeconds < 10.629752) {
            game.update(frameSeconds);
            game.renderPresentation(presentationRenderer);
            hallElapsed += frameSeconds;
        }
        game.keyDown(VK_LEFT);
        if (game.attractMode_ || game.page_ != Game::Page::Title) return false;

        double titleElapsed = 0.0;
        while (!game.attractMode_ && titleElapsed < 31.0) {
            game.update(frameSeconds);
            game.renderPresentation(presentationRenderer);
            titleElapsed += frameSeconds;
        }
        if (!game.attractMode_ || game.page_ != Game::Page::Attract ||
            game.attractBoardIndex_ != 0 ||
            game.activeBoardMode_ != Game::Mode::Multiples ||
            game.target_ != 15 || game.level_ != 10 ||
            game.attractDifficultyIndex_ != 9 ||
            game.playerRow_ != 2 || game.playerColumn_ != 1 ||
            game.random_.calls != 584 ||
            fullFrameHash(game) != 0x373eb5db7ab6534bull) {
            std::cerr << "Restart replay board: attract=" << game.attractMode_
                      << " page=" << static_cast<int>(game.page_)
                      << " board=" << game.attractBoardIndex_
                      << " mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_
                      << " level=" << game.level_
                      << " difficulty=" << game.attractDifficultyIndex_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " calls=" << game.random_.calls
                      << " hash=0x" << std::hex << fullFrameHash(game)
                      << std::dec << " titleElapsed=" << titleElapsed << '\n';
            return false;
        }

        game.discardPresentationFrames();
        const bool auditRestartedFourthBoard =
            std::getenv("MUNCHERS_AUDIT_RESTARTED_FOURTH_BOARD_SEQUENCE") != nullptr;
        const char* fourthBoardPageDumpDirectoryText =
            std::getenv("MUNCHERS_AUDIT_FOURTH_BOARD_PAGE_DIR");
        const bool dumpFourthBoardPages = fourthBoardPageDumpDirectoryText &&
            *fourthBoardPageDumpDirectoryText;
        const std::filesystem::path fourthBoardPageDumpDirectory =
            dumpFourthBoardPages
                ? std::filesystem::path(fourthBoardPageDumpDirectoryText)
                : std::filesystem::path{};
        if (dumpFourthBoardPages) {
            std::error_code error;
            std::filesystem::create_directories(fourthBoardPageDumpDirectory, error);
            if (error) return false;
        }
        std::vector<std::uint64_t> sequence;
        std::vector<int> starts;
        std::vector<std::string> actions;
        std::vector<double> actionTimes;
        int presentationFrame = 0;
        bool fourthBoardPageDumpFailed = false;
        const auto recordFourthBoardPresentation = [&](const std::uint64_t hash) {
            const bool changed = sequence.empty() || sequence.back() != hash;
            if (changed) {
                sequence.push_back(hash);
                starts.push_back(presentationFrame);
                if (dumpFourthBoardPages) {
                    std::ostringstream name;
                    name << std::hex << hash << std::dec << '-'
                         << sequence.size() - 1 << ".ppm";
                    std::ofstream output(fourthBoardPageDumpDirectory / name.str(),
                                         std::ios::binary);
                    if (!output) {
                        fourthBoardPageDumpFailed = true;
                    } else {
                        output << "P6\n" << Renderer::Width << ' '
                               << Renderer::Height << "\n255\n";
                        for (const std::uint32_t pixel :
                             presentationRenderer.pixels()) {
                            const char rgb[3] = {
                                static_cast<char>((pixel >> 16) & 0xffu),
                                static_cast<char>((pixel >> 8) & 0xffu),
                                static_cast<char>(pixel & 0xffu),
                            };
                            output.write(rgb, sizeof(rgb));
                        }
                        if (!output) fourthBoardPageDumpFailed = true;
                    }
                }
            }
            ++presentationFrame;
        };
        game.renderPresentation(presentationRenderer);
        recordFourthBoardPresentation(renderedFrameHash(presentationRenderer));
        double boardElapsed = 0.0;
        double collisionAt = -1.0;
        double terminalAt = -1.0;
        std::uint64_t collisionCalls = 0;
        std::uint64_t terminalCalls = 0;
        std::string terminalMessage;
        bool collisionStateMatches = false;
        while (boardElapsed < 30.0 &&
               game.attractHallTransitionPhase_ !=
                   Game::AttractHallTransitionPhase::Wipe) {
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            const bool wasDeathAnimating = game.deathAnimating_;
            const Game::Page previousPage = game.page_;
            const std::uint64_t callsBeforeUpdate = game.random_.calls;
            const int biteTickBeforeUpdate = game.deathSequenceTicks_;
            const double demoTimerBeforeUpdate = game.attractActionTimer_;
            game.update(frameSeconds);
            boardElapsed += frameSeconds;
            if (auditRestartedFourthBoard &&
                game.random_.calls != callsBeforeUpdate &&
                (previousPage == Game::Page::Feedback ||
                 game.page_ == Game::Page::Feedback)) {
                std::cerr << "RESTARTED_FOURTH_BOARD_PRNG t=" << boardElapsed
                          << " calls=" << callsBeforeUpdate << "->"
                          << game.random_.calls
                          << " bite=" << biteTickBeforeUpdate << "->"
                          << game.deathSequenceTicks_
                          << " demo=" << demoTimerBeforeUpdate << "->"
                          << game.attractActionTimer_;
                for (const Game::Enemy& enemy : game.enemies_) {
                    std::cerr << " [s" << enemy.slot << "t" << enemy.type
                              << " r" << enemy.row << 'c' << enemy.column
                              << " d" << enemy.direction
                              << " m" << enemy.moving
                              << " e" << enemy.entering
                              << " p" << game.enemyMovementPhase(enemy)
                              << " timer=" << enemy.moveTimer << '/'
                              << enemy.animationTimer << ']';
                }
                std::cerr << '\n';
            }
            if (game.attractHallTransitionPhase_ ==
                Game::AttractHallTransitionPhase::Wipe) {
                break;
            }
            game.renderPresentation(presentationRenderer);
            const std::uint64_t hash = renderedFrameHash(presentationRenderer);
            if (auditRestartedFourthBoard &&
                (sequence.empty() || sequence.back() != hash) &&
                presentationFrame >= 575 && presentationFrame <= 635) {
                std::cerr << "RESTARTED_FOURTH_BOARD_PAGE frame="
                          << presentationFrame << " hash=0x" << std::hex << hash
                          << " logical=0x" << fullFrameHash(game)
                          << std::dec << " page=" << static_cast<int>(game.page_)
                          << " playerTerminal=" << game.playerTerminalFrame_
                          << " death=" << game.deathAnimating_ << ':'
                          << game.deathSequenceTicks_
                          << " queued=" << game.presentationFrames_.size()
                          << " resident=" << !game.deathResidentSurfacePixels_.empty()
                          << " lag=" << game.enemyPresentationLagSlot_ << ':'
                          << game.enemyPresentationLagTicks_
                          << " holds="
                          << !game.attractPlayerDepartingEnemyHoldPixels_.empty()
                          << !game.attractPlayerPhaseZeroHoldPixels_.empty()
                          << !game.attractSafeZoneWarningHoldPixels_.empty()
                          << !game.attractPlayerEntryPresentationHoldPixels_.empty()
                          << !game.attractPlayerWarningHoldPixels_.empty();
                for (const Game::Enemy& enemy : game.enemies_) {
                    std::cerr << " [s" << enemy.slot << "t" << enemy.type
                              << " m" << enemy.moving << " e" << enemy.entering
                              << " p" << game.enemyMovementPhase(enemy) << ']';
                }
                std::cerr << '\n';
            }
            recordFourthBoardPresentation(hash);
            if (!wasMoving && game.moving_) {
                const int from = game.moveFromRow_ * Game::BoardColumns +
                                 game.moveFromColumn_;
                const int to = game.moveToRow_ * Game::BoardColumns +
                               game.moveToColumn_;
                actions.push_back("M" + std::to_string(from) + ">" +
                                  std::to_string(to));
                actionTimes.push_back(boardElapsed);
            }
            if (!wasMunching && game.munching_) {
                actions.push_back("E" + std::to_string(game.munchCellIndex_));
                actionTimes.push_back(boardElapsed);
            }
            if (previousPage != Game::Page::Feedback &&
                game.page_ == Game::Page::Feedback) {
                actions.push_back("F" + std::to_string(game.feedbackEnemyType_) +
                                  ":" + game.feedbackMessage_);
                actionTimes.push_back(boardElapsed);
                collisionAt = boardElapsed;
                collisionCalls = game.random_.calls;
                collisionStateMatches =
                    game.feedbackKind_ == Game::FeedbackKind::EatenByTroggle &&
                    game.feedbackEnemyType_ == 2 && game.feedbackMessage_.empty() &&
                    game.playerRow_ == 0 && game.playerColumn_ == 1 &&
                    game.deathAnimating_ && game.score_ == 120 &&
                    game.enemies_.size() == 3;
            }
            if (wasDeathAnimating && !game.deathAnimating_) {
                terminalAt = boardElapsed;
                terminalCalls = game.random_.calls;
                terminalMessage = game.feedbackMessage_;
            }
        }

        if (auditRestartedFourthBoard) {
            std::cerr << "RESTARTED_FOURTH_BOARD_PRESENTATION_SEQUENCE";
            for (std::size_t index = 0; index < sequence.size(); ++index) {
                std::cerr << " 0x" << std::hex << sequence[index]
                          << std::dec << '@' << starts[index];
            }
            std::cerr << '\n';
            std::cerr << "RESTARTED_FOURTH_BOARD_ACTIONS";
            for (std::size_t index = 0; index < actions.size(); ++index) {
                std::cerr << ' ' << actions[index] << '@' << actionTimes[index];
            }
            std::cerr << " terminal=0x" << std::hex << sequence.back()
                      << std::dec << " elapsed=" << boardElapsed << '\n';
        }
        const double wipeAt = boardElapsed;
        while (game.page_ != Game::Page::Hall && boardElapsed < 17.0) {
            game.update(frameSeconds);
            game.renderPresentation(presentationRenderer);
            boardElapsed += frameSeconds;
        }
        const double hallAt = boardElapsed;
        const std::uint64_t hallHash = fullFrameHash(game);

        static constexpr std::array<std::string_view, 8> expectedActions = {
            "M13>12", "E12", "M12>6", "E6",
            "M6>7", "E7", "M7>1", "F2:"};
        static constexpr std::array<double, 8> expectedActionTimes = {
            1.04158, 2.51120, 3.23888, 4.56582,
            5.39337, 6.70605, 8.17567, 8.76067};
        bool actionsMatch = actions.size() == expectedActions.size();
        for (std::size_t index = 0; actionsMatch && index < actions.size(); ++index) {
            actionsMatch = actions[index] == expectedActions[index] &&
                std::abs(actionTimes[index] - expectedActionTimes[index]) <= 0.08;
        }

        // These presentation-complete source pages from global capture frames
        // 15321-16390 pin every phase of the collision boundary. They form a
        // subsequence because the DOS stream also contains incomplete dirty
        // refreshes; the board-wide reconciliation requires every native page
        // to occur in source order and permits no native-only composite.
        static constexpr std::array<std::uint64_t, 15> sourceKeyPages = {
            0x373eb5db7ab6534bull, 0xce9382c8fa615ba4ull,
            0x0674ece67f2455d9ull, 0xb3af352e246803cbull,
            0xe1601a53581b8dd7ull, 0xf8f3a2c263541298ull,
            0xf0cbec6e6211f047ull, 0xba340f8d65e4a4d5ull,
            0x72b4f8c27d6cf048ull, 0x281513927419c02bull,
            0x51bfb8a5c8690ffeull, 0x0006ec7c3d12ca3eull,
            0x1a6de5ece4d3e73eull, 0x7a5db4784587a379ull,
            0x0dc6649d785d392aull};
        std::size_t keyIndex = 0;
        for (const std::uint64_t hash : sequence) {
            if (keyIndex < sourceKeyPages.size() &&
                hash == sourceKeyPages[keyIndex]) {
                ++keyIndex;
            }
        }
        if (keyIndex + 1 == sourceKeyPages.size() &&
            hallHash == sourceKeyPages.back()) {
            ++keyIndex;
        }

        const bool matched =
            !fourthBoardPageDumpFailed && game.page_ == Game::Page::Hall &&
            actionsMatch &&
            collisionStateMatches && collisionCalls == 604 &&
            std::abs(collisionAt - 8.76067) <= 0.08 &&
            terminalMessage == "Aargh" && terminalCalls == 607 &&
            std::abs(terminalAt - 9.68810) <= 0.08 &&
            std::abs(wipeAt - 15.4096) <= 0.08 &&
            std::abs(hallAt - 15.4952) <= 0.08 &&
            hallHash == 0x0dc6649d785d392aull &&
            keyIndex == sourceKeyPages.size();
        if (!matched) {
            std::cerr << "Restart replay terminal: phase="
                      << static_cast<int>(game.attractHallTransitionPhase_)
                      << " collision=" << collisionStateMatches << '@'
                      << collisionAt << "/calls=" << collisionCalls
                      << " terminal='" << terminalMessage << "'@" << terminalAt
                      << "/calls=" << terminalCalls
                      << " wipe=" << wipeAt << " hall=" << hallAt
                      << "/hash=0x" << std::hex << hallHash << std::dec
                      << " keys=" << keyIndex << '/' << sourceKeyPages.size()
                      << " actions=" << actionsMatch
                      << " page=" << static_cast<int>(game.page_)
                      << " calls=" << game.random_.calls << '\n';
        }
        const bool auditRestartedFifthBoard =
            std::getenv("MUNCHERS_AUDIT_RESTARTED_FIFTH_BOARD_SEQUENCE") != nullptr;
        if (!matched || !auditRestartedFifthBoard) return matched;

        // Continue the captured fresh Demo batch beyond the Multiples Hall.
        // This development trace establishes the next board's real initializer
        // and route before they are admitted to the permanent source gate.
        double fifthLeadIn = 0.0;
        while (fifthLeadIn < 40.0 &&
               !(game.page_ == Game::Page::Attract &&
                 game.attractBoardIndex_ == 1 &&
                 game.attractInterstitialTransition_ ==
                     Game::AttractInterstitialTransition::None)) {
            game.update(frameSeconds);
            game.renderPresentation(presentationRenderer);
            fifthLeadIn += frameSeconds;
        }
        if (game.page_ != Game::Page::Attract || game.attractBoardIndex_ != 1 ||
            game.attractInterstitialTransition_ !=
                Game::AttractInterstitialTransition::None) {
            std::cerr << "RESTARTED_FIFTH_BOARD_INITIALIZER missing lead="
                      << fifthLeadIn << " page=" << static_cast<int>(game.page_)
                      << " board=" << game.attractBoardIndex_ << '\n';
            return false;
        }

        game.discardPresentationFrames();
        std::cerr << "RESTARTED_FIFTH_BOARD_INITIALIZER lead=" << fifthLeadIn
                  << " calls=" << game.random_.calls
                  << " mode=" << static_cast<int>(game.activeBoardMode_)
                  << " target=" << game.target_ << " level=" << game.level_
                  << " difficulty=" << game.attractDifficultyIndex_
                  << " player=" << game.playerRow_ << ',' << game.playerColumn_
                  << " remaining=" << game.correctRemaining_
                  << " hash=0x" << std::hex << fullFrameHash(game) << std::dec
                  << " labels=";
        for (const Game::Cell& current : game.cells_) {
            std::cerr << " ['" << current.label << "']";
        }
        std::cerr << '\n';

        const char* fifthBoardPageDumpDirectoryText =
            std::getenv("MUNCHERS_AUDIT_FIFTH_BOARD_PAGE_DIR");
        const bool dumpFifthBoardPages = fifthBoardPageDumpDirectoryText &&
            *fifthBoardPageDumpDirectoryText;
        const std::filesystem::path fifthBoardPageDumpDirectory =
            dumpFifthBoardPages
                ? std::filesystem::path(fifthBoardPageDumpDirectoryText)
                : std::filesystem::path{};
        if (dumpFifthBoardPages) {
            std::error_code error;
            std::filesystem::create_directories(fifthBoardPageDumpDirectory, error);
            if (error) return false;
        }
        std::vector<std::uint64_t> fifthSequence;
        std::vector<int> fifthStarts;
        std::vector<std::string> fifthActions;
        std::vector<double> fifthActionTimes;
        int fifthPresentationFrame = 0;
        bool fifthBoardPageDumpFailed = false;
        const bool traceFifthBoardState =
            std::getenv("MUNCHERS_AUDIT_FIFTH_BOARD_STATE_TRACE") != nullptr;
        const bool auditFifthBoardExit =
            std::getenv("MUNCHERS_AUDIT_FIFTH_BOARD_EXIT") != nullptr;
        const auto recordFifthPage = [&](const std::uint64_t hash) {
            if (fifthSequence.empty() || fifthSequence.back() != hash) {
                fifthSequence.push_back(hash);
                fifthStarts.push_back(fifthPresentationFrame);
                if (traceFifthBoardState) {
                    std::cerr << "RESTARTED_FIFTH_BOARD_STATE index="
                              << fifthSequence.size() - 1
                              << " frame=" << fifthPresentationFrame
                              << " hash=0x" << std::hex << hash << std::dec
                              << " player=" << game.playerRow_ << ','
                              << game.playerColumn_
                              << " moving=" << game.moving_;
                    if (game.moving_) {
                        std::cerr << "/phase=" << game.playerMovementPhase()
                                  << "/from=" << game.moveFromRow_ << ','
                                  << game.moveFromColumn_ << "/to="
                                  << game.moveToRow_ << ',' << game.moveToColumn_;
                    }
                    std::cerr << " terminal=" << game.playerTerminalFrame_
                              << " queued=" << game.presentationFrames_.size()
                              << " lag=" << game.enemyPresentationLagSlot_
                              << '/' << game.enemyPresentationLagTicks_
                              << " holds="
                              << !game.attractPlayerPhaseZeroHoldPixels_.empty()
                              << !game.attractPlayerEntryPresentationHoldPixels_.empty()
                              << !game.attractPlayerDepartingEnemyHoldPixels_.empty()
                              << " enemies=";
                    for (const Game::Enemy& enemy : game.enemies_) {
                        std::cerr << '[' << enemy.slot << ':' << enemy.type
                                  << '@' << enemy.row << ',' << enemy.column
                                  << "/from=" << enemy.fromRow << ','
                                  << enemy.fromColumn << "/moving="
                                  << enemy.moving << "/entry=" << enemy.entering
                                  << "/exit=" << enemy.exiting << "/phase="
                                  << (enemy.moving
                                          ? game.enemyMovementPhase(enemy) : -1)
                                  << ']';
                    }
                    std::cerr << '\n';
                }
                if (dumpFifthBoardPages) {
                    std::ostringstream name;
                    name << std::hex << hash << std::dec << '-'
                         << fifthSequence.size() - 1 << ".ppm";
                    std::ofstream output(fifthBoardPageDumpDirectory / name.str(),
                                         std::ios::binary);
                    if (!output) {
                        fifthBoardPageDumpFailed = true;
                    } else {
                        output << "P6\n" << Renderer::Width << ' '
                               << Renderer::Height << "\n255\n";
                        for (const std::uint32_t pixel :
                             presentationRenderer.pixels()) {
                            const char rgb[3] = {
                                static_cast<char>((pixel >> 16) & 0xffu),
                                static_cast<char>((pixel >> 8) & 0xffu),
                                static_cast<char>(pixel & 0xffu),
                            };
                            output.write(rgb, sizeof(rgb));
                        }
                        if (!output) fifthBoardPageDumpFailed = true;
                    }
                }
            }
            ++fifthPresentationFrame;
        };
        game.renderPresentation(presentationRenderer);
        recordFifthPage(renderedFrameHash(presentationRenderer));
        double fifthElapsed = 0.0;
        constexpr int FifthBoardExitPresentationFrame = 20528 - 18570;
        constexpr int FifthBoardTailLastPresentationFrame = 22003 - 18570;
        while ((!auditFifthBoardExit && fifthElapsed < 30.0 &&
                game.page_ == Game::Page::Attract &&
                game.attractBoardIndex_ == 1) ||
               (auditFifthBoardExit &&
                fifthPresentationFrame <= FifthBoardTailLastPresentationFrame &&
                ((game.page_ == Game::Page::Attract &&
                  game.attractBoardIndex_ == 1) ||
                 game.page_ == Game::Page::Title))) {
            const bool wasMoving = game.moving_;
            const bool wasMunching = game.munching_;
            const Game::Page previousPage = game.page_;
            if (auditFifthBoardExit &&
                fifthPresentationFrame == FifthBoardExitPresentationFrame) {
                game.keyDown(VK_LEFT);
            }
            game.update(frameSeconds);
            fifthElapsed += frameSeconds;
            if (traceFifthBoardState &&
                ((fifthPresentationFrame >= 980 && fifthPresentationFrame <= 1010) ||
                 (fifthPresentationFrame >= 1540 && fifthPresentationFrame <= 1600))) {
                std::cerr << "RESTARTED_FIFTH_BOARD_TICK frame="
                          << fifthPresentationFrame << " player="
                          << game.playerRow_ << ',' << game.playerColumn_
                          << "/moving=" << game.moving_ << "/phase="
                          << (game.moving_ ? game.playerMovementPhase() : -1)
                          << "/terminal=" << game.playerTerminalFrame_
                          << "/queue=" << game.presentationFrames_.size()
                          << "/lag=" << game.enemyPresentationLagSlot_ << ':'
                          << game.enemyPresentationLagTicks_ << "/enemies=";
                for (const Game::Enemy& enemy : game.enemies_) {
                    std::cerr << '[' << enemy.slot << "/moving=" << enemy.moving
                              << "/phase="
                              << (enemy.moving
                                      ? game.enemyMovementPhase(enemy) : -1)
                              << "/exit=" << enemy.exiting << ']';
                }
                std::cerr << '\n';
            }
            game.renderPresentation(presentationRenderer);
            recordFifthPage(renderedFrameHash(presentationRenderer));
            if (!wasMoving && game.moving_) {
                fifthActions.push_back(
                    "M" + std::to_string(game.moveFromRow_ * Game::BoardColumns +
                                          game.moveFromColumn_) + ">" +
                    std::to_string(game.moveToRow_ * Game::BoardColumns +
                                   game.moveToColumn_));
                fifthActionTimes.push_back(fifthElapsed);
            }
            if (!wasMunching && game.munching_) {
                fifthActions.push_back("E" + std::to_string(game.munchCellIndex_));
                fifthActionTimes.push_back(fifthElapsed);
            }
            if (previousPage != Game::Page::Feedback &&
                game.page_ == Game::Page::Feedback) {
                fifthActions.push_back("F" +
                    std::to_string(game.feedbackEnemyType_) + ":" +
                    game.feedbackMessage_);
                fifthActionTimes.push_back(fifthElapsed);
            }
        }
        std::cerr << "RESTARTED_FIFTH_BOARD_PRESENTATION_SEQUENCE";
        for (std::size_t index = 0; index < fifthSequence.size(); ++index) {
            std::cerr << " 0x" << std::hex << fifthSequence[index]
                      << std::dec << '@' << fifthStarts[index];
        }
        std::cerr << '\n';
        std::cerr << "RESTARTED_FIFTH_BOARD_ACTIONS";
        for (std::size_t index = 0; index < fifthActions.size(); ++index) {
            std::cerr << ' ' << fifthActions[index] << '@'
                      << fifthActionTimes[index];
        }
        std::cerr << " elapsed=" << fifthElapsed
                  << " page=" << static_cast<int>(game.page_)
                  << " calls=" << game.random_.calls << '\n';
        if (auditFifthBoardExit) {
            game.renderPresentation(presentationRenderer);
            std::cerr << "RESTARTED_FIFTH_BOARD_EXIT frame="
                      << fifthPresentationFrame << " page="
                      << static_cast<int>(game.page_) << " attract="
                      << game.attractMode_ << " title_idle="
                      << game.titleIdleTimer_ << " calls=" << game.random_.calls
                      << " hash=0x" << std::hex
                      << renderedFrameHash(presentationRenderer) << std::dec
                      << '\n';
        }
        return !fifthBoardPageDumpFailed;
    }
    static bool exercisesRecoveredAttractAudio(Game& game) {
        if (!game.attractMode_ || !game.musicOn_ || !game.attractOplPlayer_.looping() ||
            game.attractOplPlayer_.scoreWriteCount() < 1'000 ||
            game.musicPlayer_.playing()) return false;
        const std::size_t starts = game.attractOplPlayer_.loopPlayCount();
        const std::size_t feedbackStarts = game.attractOplPlayer_.effectPlayCount();

        // The original conductor leaves OPL channel 0 free. A gameplay cue
        // must therefore enter the same continuously clocked chip without
        // replacing the channel-1..8 score or starting a second PCM owner.
        (void)game.playOriginalSound(7, false);
        if (!game.attractOplPlayer_.effectPlaying() || game.effectPlayer_.playing() ||
            !game.attractOplPlayer_.looping()) return false;

        game.toggleMusic();
        if (game.musicOn_ || !game.attractOplPlayer_.playing() ||
            game.attractOplPlayer_.looping() ||
            !game.attractOplPlayer_.effectPlaying() ||
            game.attractOplPlayer_.effectPlayCount() != feedbackStarts + 2 ||
            game.musicPlayer_.playing() || game.effectPlayer_.playing()) return false;
        game.toggleMusic();
        return game.musicOn_ && game.attractOplPlayer_.looping() &&
               game.attractOplPlayer_.effectPlaying() &&
               game.attractOplPlayer_.loopPlayCount() == starts + 1 &&
               game.attractOplPlayer_.effectPlayCount() == feedbackStarts + 3;
    }
    static bool exercisesRecoveredToggleFeedback() {
        const auto fail = [](const char* stage) {
            std::cerr << "Number toggle-feedback stage: " << stage << '\n';
            return false;
        };
        const auto prepare = [](Game& game) {
            game.settingsPersistenceEnabled_ = false;
            game.page_ = Game::Page::Playing;
            game.attractMode_ = false;
            game.soundOn_ = true;
            game.musicOn_ = false;
            game.speakerEffects_ = false;
        };
        const auto expectedOpl = [](const Game& game, const std::uint8_t stream,
                                    const std::uint32_t delay) {
            const BlobView bank = game.assets_.gameArchive().find("GSND", 12);
            MeccSound sound = decodeMeccGSound(bank, stream);
            if (!sound.valid) return std::vector<std::int16_t>{};
            for (OplWrite& write : sound.writes) write.milliseconds += delay;
            sound.durationMilliseconds += delay;
            OplStreamPlayer reference;
            if (!reference.startIdle() ||
                !reference.playEffect(std::move(sound.writes),
                                      sound.durationMilliseconds)) {
                return std::vector<std::int16_t>{};
            }
            return reference.renderTestSamples(49'715);
        };
        const auto waveHash = [](const std::vector<std::uint8_t>& wave) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const std::uint8_t byte : wave) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        };

        Game soundOff;
        prepare(soundOff);
        soundOff.toggleSound();
        const bool downSubmitted = soundOff.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> down = soundOff.attractOplPlayer_.renderTestSamples(49'715);
        const std::vector<std::int16_t> expectedDown = expectedOpl(soundOff, 1, 0);
        if (soundOff.soundOn_ || !downSubmitted ||
            soundOff.attractOplPlayer_.effectPlayCount() != 1 ||
            down != expectedDown) {
            std::cerr << "Number down flags=" << soundOff.soundOn_ << ','
                      << soundOff.attractOplPlayer_.effectPlaying() << ','
                      << soundOff.attractOplPlayer_.effectPlayCount()
                      << " samples=" << down.size() << ',' << expectedDown.size()
                      << " equal=" << (down == expectedDown) << '\n';
            return fail("Alt+S off OPL stream 1");
        }

        Game soundOn;
        prepare(soundOn);
        soundOn.soundOn_ = false;
        soundOn.toggleSound();
        const bool upSubmitted = soundOn.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> up = soundOn.attractOplPlayer_.renderTestSamples(49'715);
        if (!soundOn.soundOn_ || !upSubmitted ||
            soundOn.attractOplPlayer_.effectPlayCount() != 1 ||
            up != expectedOpl(soundOn, 2, 0) || up == down) {
            return fail("Alt+S on OPL stream 2");
        }

        Game musicOff;
        prepare(musicOff);
        musicOff.musicOn_ = true;
        musicOff.toggleMusic();
        const bool delayedSubmitted = musicOff.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> delayed =
            musicOff.attractOplPlayer_.renderTestSamples(49'715);
        const auto firstAudible = std::find_if(
            delayed.begin(), delayed.end(), [](const std::int16_t sample) {
                return sample != 0;
            });
        if (musicOff.musicOn_ || !musicOff.attractOplPlayer_.playing() ||
            musicOff.attractOplPlayer_.looping() ||
            !delayedSubmitted ||
            musicOff.attractOplPlayer_.effectPlayCount() != 1 ||
            delayed != expectedOpl(musicOff, 1, 50) ||
            firstAudible == delayed.end() ||
            std::distance(delayed.begin(), firstAudible) < 2'485) {
            std::cerr << "Number delayed first audible="
                      << std::distance(delayed.begin(), firstAudible) << '\n';
            return fail("Alt+M off delayed OPL stream 1");
        }

        Game musicOn;
        prepare(musicOn);
        musicOn.musicOn_ = false;
        musicOn.toggleMusic();
        const bool enabledSubmitted = musicOn.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> enabled =
            musicOn.attractOplPlayer_.renderTestSamples(49'715);
        if (!musicOn.musicOn_ || !enabledSubmitted ||
            musicOn.attractOplPlayer_.looping() ||
            musicOn.attractOplPlayer_.effectPlayCount() != 1 ||
            enabled != expectedOpl(musicOn, 2, 0)) {
            return fail("Alt+M on OPL stream 2");
        }

        const BlobView bank = soundOff.assets_.gameArchive().find("GSND", 12);
        const std::vector<std::uint8_t> speakerDown = renderMeccSpeakerSoundToWave(
            bank, 1, 40, MeccSoundProfile::NumberMunchers);
        const std::vector<std::uint8_t> speakerUp = renderMeccSpeakerSoundToWave(
            bank, 2, 40, MeccSoundProfile::NumberMunchers);
        const std::vector<std::uint8_t> speakerDelayed = renderMeccSpeakerSoundToWave(
            bank, 1, 40, MeccSoundProfile::NumberMunchers, 50);
        Game speakerSoundOff;
        prepare(speakerSoundOff);
        speakerSoundOff.speakerEffects_ = true;
        speakerSoundOff.toggleSound();
        if (!speakerSoundOff.effectPlayer_.playing() ||
            speakerSoundOff.effectPlayer_.bufferHash() != waveHash(speakerDown)) {
            return fail("Alt+S off speaker stream 1");
        }
        Game speakerSoundOn;
        prepare(speakerSoundOn);
        speakerSoundOn.speakerEffects_ = true;
        speakerSoundOn.soundOn_ = false;
        speakerSoundOn.toggleSound();
        if (!speakerSoundOn.effectPlayer_.playing() ||
            speakerSoundOn.effectPlayer_.bufferHash() != waveHash(speakerUp)) {
            return fail("Alt+S on speaker stream 2");
        }
        Game speakerMusicOff;
        prepare(speakerMusicOff);
        speakerMusicOff.speakerEffects_ = true;
        speakerMusicOff.musicOn_ = true;
        speakerMusicOff.toggleMusic();
        if (!speakerMusicOff.effectPlayer_.playing() ||
            speakerMusicOff.effectPlayer_.bufferHash() != waveHash(speakerDelayed) ||
            speakerDelayed.size() != speakerDown.size() + 4'410) {
            std::cerr << "Number speaker delay sizes=" << speakerDown.size()
                      << ',' << speakerDelayed.size() << '\n';
            return fail("Alt+M off delayed speaker stream 1");
        }
        return true;
    }
    static bool exercisesRecoveredGameplayAudioRoutes() {
        const auto fail = [](const char* stage) {
            std::cerr << "Number gameplay-audio stage: " << stage << '\n';
            return false;
        };
        const auto prepare = [](Game& game) {
            game.settingsPersistenceEnabled_ = false;
            game.soundOn_ = true;
            game.musicOn_ = false;
            game.speakerEffects_ = false;
        };
        const auto disableBoardJobs = [](Game& game) {
            game.enemySlotCount_ = 0;
            game.safeZoneJobCount_ = 0;
            game.enemyWarning_ = false;
        };
        const auto adlibEffectPlays = [](const Game& game) {
            return game.attractOplPlayer_.effectPlayCount();
        };
        const auto waveHash = [](const std::vector<std::uint8_t>& wave) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const std::uint8_t byte : wave) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        };
        const auto placeCell = [](Game& game, const bool correct) {
            Game::Cell& selected = game.cell(game.playerRow_, game.playerColumn_);
            selected = {};
            selected.label = correct ? "10" : "7";
            selected.correct = correct;
            selected.eaten = false;
            game.correctRemaining_ = 2;
        };

        Game start;
        prepare(start);
        start.startGame(Game::Mode::Multiples, 1);
        if (start.lastGameplaySound_ != -1 || adlibEffectPlays(start) != 0) {
            return fail("silent board start");
        }

        Game correct;
        prepare(correct);
        correct.startGame(Game::Mode::Multiples, 1);
        disableBoardJobs(correct);
        placeCell(correct, true);
        std::size_t plays = adlibEffectPlays(correct);
        correct.munch();
        if (!correct.munching_ || correct.lastGameplaySound_ != 7 ||
            adlibEffectPlays(correct) != plays + 1) return fail("correct chew cue");

        Game wrong;
        prepare(wrong);
        wrong.startGame(Game::Mode::Multiples, 1);
        disableBoardJobs(wrong);
        placeCell(wrong, false);
        plays = adlibEffectPlays(wrong);
        wrong.munch();
        if (!wrong.munching_ || wrong.lastGameplaySound_ != 8 ||
            adlibEffectPlays(wrong) != plays + 1) return fail("wrong chew cue");

        Game warning;
        prepare(warning);
        warning.startGame(Game::Mode::Multiples, 1);
        if (warning.enemySlotCount_ < 1) return fail("warning setup");
        plays = adlibEffectPlays(warning);
        if (!warning.beginEnemyWarning(0) || warning.lastGameplaySound_ != 9 ||
            adlibEffectPlays(warning) != plays + 1) return fail("warning cue");
        plays = adlibEffectPlays(warning);
        warning.spawnPendingEnemy(0);
        if (warning.enemySlots_[0].phase != Game::EnemySlotPhase::Active ||
            warning.lastGameplaySound_ != 3 ||
            adlibEffectPlays(warning) != plays + 1) return fail("edge-entry cue");

        Game information;
        prepare(information);
        information.page_ = Game::Page::InstructionsQuestion;
        information.menuSelection_ = 0;
        plays = adlibEffectPlays(information);
        information.keyDown(VK_RETURN);
        if (information.page_ != Game::Page::Information ||
            information.informationReturnPage_ != Game::Page::ModeSelect ||
            information.informationPage_ != 0 || information.lastGameplaySound_ != 13 ||
            adlibEffectPlays(information) != plays + 1) return fail("information cue 13");
        information.keyDown(VK_SPACE);
        if (information.informationPage_ != 1 || information.lastGameplaySound_ != 14 ||
            information.previousGameplaySound_ != 13 ||
            adlibEffectPlays(information) != plays + 2) return fail("information cue 14");

        Game safe;
        prepare(safe);
        safe.startGame(Game::Mode::Multiples, 1);
        disableBoardJobs(safe);
        safe.safeZoneJobCount_ = 1;
        safe.safeZoneJobs_[0].active = true;
        safe.safeZoneJobs_[0].cellIndex = 7;
        safe.safeZoneJobs_[0].period = 10.0;
        safe.safeZoneJobs_[0].timer = 0.0;
        safe.cells_[7].safe = true;
        plays = adlibEffectPlays(safe);
        safe.updateSafeZones(0.0);
        if (safe.safeZoneJobs_[0].active || safe.cells_[7].safe ||
            safe.lastGameplaySound_ != 5 ||
            adlibEffectPlays(safe) != plays + 1) return fail("safe-zone expiry cue");

        const OriginalRandom safeRandom = safe.random_;
        const int activatedCell = safe.chooseSafeZoneCell();
        safe.random_ = safeRandom;
        Game::Enemy displaced;
        displaced.row = activatedCell / Game::BoardColumns;
        displaced.column = activatedCell % Game::BoardColumns;
        displaced.fromRow = displaced.row;
        displaced.fromColumn = displaced.column;
        displaced.type = 0;
        displaced.slot = 0;
        displaced.savedCell = safe.cells_[static_cast<std::size_t>(activatedCell)];
        displaced.savedCellValid = true;
        safe.enemySlotCount_ = 1;
        safe.enemySlots_[0].type = 0;
        safe.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        safe.enemies_.push_back(displaced);
        safe.safeZoneJobs_[0].timer = 0.0;
        plays = adlibEffectPlays(safe);
        safe.updateSafeZones(0.0);
        if (!safe.safeZoneJobs_[0].active ||
            safe.safeZoneJobs_[0].cellIndex != activatedCell || !safe.enemies_.empty() ||
            safe.previousGameplaySound_ != 11 || safe.lastGameplaySound_ != 4 ||
            adlibEffectPlays(safe) != plays + 2) return fail("safe-zone activation cues");

        Game cannibal;
        prepare(cannibal);
        cannibal.startGame(Game::Mode::Multiples, 1);
        disableBoardJobs(cannibal);
        cannibal.enemySlotCount_ = 2;
        for (int slot = 0; slot < 2; ++slot) {
            cannibal.enemySlots_[static_cast<std::size_t>(slot)].type = slot;
            cannibal.enemySlots_[static_cast<std::size_t>(slot)].phase =
                Game::EnemySlotPhase::Active;
        }
        Game::Enemy survivor;
        survivor.row = 0;
        survivor.column = 0;
        survivor.type = 0;
        survivor.slot = 0;
        survivor.cannibalizing = true;
        survivor.cannibalTimer = 0.0;
        Game::Enemy victim = survivor;
        victim.type = 1;
        victim.slot = 1;
        victim.cannibalizing = false;
        victim.overlapFrozen = true;
        victim.overlapRetired = true;
        victim.collisionHidden = true;
        cannibal.enemies_ = {survivor, victim};
        plays = adlibEffectPlays(cannibal);
        cannibal.updateEnemies(0.0);
        if (cannibal.enemies_.size() != 1 || cannibal.enemies_[0].slot != 0 ||
            cannibal.lastGameplaySound_ != 11 ||
            adlibEffectPlays(cannibal) != plays + 1) return fail("cannibal retirement cue");

        const auto prepareCollision = [&](Game& game, const bool demo, const int lives) {
            prepare(game);
            game.startGame(Game::Mode::Multiples, 1);
            disableBoardJobs(game);
            game.enemySlotCount_ = 2;
            game.lives_ = lives;
            game.attractMode_ = demo;
            game.page_ = demo ? Game::Page::Attract : Game::Page::Playing;
            game.attractActionTimer_ = 100.0;
            for (int slot = 0; slot < 2; ++slot) {
                game.enemySlots_[static_cast<std::size_t>(slot)].type = 4 - slot;
                game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                    Game::EnemySlotPhase::Active;
                Game::Enemy enemy;
                enemy.row = game.playerRow_;
                enemy.column = game.playerColumn_;
                enemy.fromRow = enemy.row;
                enemy.fromColumn = enemy.column;
                enemy.type = 4 - slot;
                enemy.slot = slot;
                enemy.moveTimer = 10.0;
                game.enemies_.push_back(enemy);
            }
        };
        const auto finishCollision = [](Game& game) {
            constexpr double tick = 1.0 / OriginalSchedulerTicksPerSecond;
            for (int index = 0; index < Game::TroggleEatAnimationTicks; ++index) {
                game.update(tick + 1e-12);
            }
        };
        static constexpr std::array<std::string_view, 5> Exclamations = {
            "Yikes", "Oops", "Aargh", "Oh, Oh", "Rats",
        };

        Game collision;
        prepareCollision(collision, false, 4);
        OriginalRandom phraseProbe = collision.random_;
        (void)phraseProbe.range(90); // Survivor dwell precedes the phrase.
        const std::string expectedPhrase(Exclamations[
            static_cast<std::size_t>(phraseProbe.range(5))]);
        const std::uint64_t calls = collision.random_.calls;
        plays = adlibEffectPlays(collision);
        collision.loseLife(4, 0);
        if (!collision.deathAnimating_ || collision.lastGameplaySound_ != 12 ||
            adlibEffectPlays(collision) != plays + 1) return fail("collision start cue");
        finishCollision(collision);
        if (collision.deathAnimating_ || collision.feedbackMessage_ != expectedPhrase ||
            collision.random_.calls != calls + 3 || collision.lives_ != 3 ||
            collision.enemies_.size() != 1 || collision.enemies_[0].slot != 0 ||
            collision.enemySlots_[1].phase != Game::EnemySlotPhase::Waiting ||
            collision.previousGameplaySound_ != 10 || collision.lastGameplaySound_ != 11 ||
            adlibEffectPlays(collision) != plays + 3) {
            return fail("ordinary collision terminal order/cues");
        }

        Game demoCollision;
        prepareCollision(demoCollision, true, 4);
        plays = adlibEffectPlays(demoCollision);
        demoCollision.loseLife(4, 0);
        finishCollision(demoCollision);
        if (demoCollision.enemies_.size() != 2 ||
            !demoCollision.enemies_[1].collisionHidden ||
            demoCollision.enemySlots_[1].phase != Game::EnemySlotPhase::Active ||
            demoCollision.previousGameplaySound_ != 12 ||
            demoCollision.lastGameplaySound_ != 10 ||
            adlibEffectPlays(demoCollision) != plays + 2) {
            return fail("Demo collision terminal boundary");
        }

        Game finalCollision;
        prepareCollision(finalCollision, false, 1);
        plays = adlibEffectPlays(finalCollision);
        finalCollision.loseLife(4, 0);
        finishCollision(finalCollision);
        if (finalCollision.lives_ != 0 || finalCollision.enemies_.size() != 2 ||
            !finalCollision.enemies_[1].collisionHidden ||
            finalCollision.enemySlots_[1].phase != Game::EnemySlotPhase::Active ||
            finalCollision.previousGameplaySound_ != 12 ||
            finalCollision.lastGameplaySound_ != 10 ||
            adlibEffectPlays(finalCollision) != plays + 2) {
            return fail("final-life collision terminal boundary");
        }

        Game interstitial;
        prepare(interstitial);
        interstitial.attractMode_ = true;
        interstitial.page_ = Game::Page::Hall;
        interstitial.transitionTimer_ = 0.0;
        plays = adlibEffectPlays(interstitial);
        interstitial.beginAttractInterstitialTransition(
            Game::AttractInterstitialTransition::HallToSplash);
        interstitial.updateAttractInterstitialTransition(1.0);
        if (interstitial.page_ != Game::Page::StartupSplash ||
            interstitial.previousGameplaySound_ != 12 ||
            interstitial.lastGameplaySound_ != 13 ||
            adlibEffectPlays(interstitial) != plays + 2) {
            return fail("Hall-to-logo cue pair");
        }

        Game complete;
        prepare(complete);
        complete.startGame(Game::Mode::Multiples, 1);
        disableBoardJobs(complete);
        plays = adlibEffectPlays(complete);
        complete.completeLevel();
        if (complete.page_ != Game::Page::Playing || complete.level_ != 2 ||
            complete.previousGameplaySound_ != -1 || complete.lastGameplaySound_ != 15 ||
            adlibEffectPlays(complete) != plays + 1) return fail("direct completion cue");

        Game cartoon;
        prepare(cartoon);
        cartoon.startGame(Game::Mode::Multiples, 3);
        disableBoardJobs(cartoon);
        plays = adlibEffectPlays(cartoon);
        cartoon.completeLevel();
        if (cartoon.page_ != Game::Page::LevelComplete ||
            cartoon.lastGameplaySound_ != 15 ||
            adlibEffectPlays(cartoon) < plays + 1) return fail("cartoon completion cue");

        Game soundOff;
        prepare(soundOff);
        soundOff.soundOn_ = false;
        soundOff.startGame(Game::Mode::Multiples, 1);
        plays = adlibEffectPlays(soundOff);
        soundOff.completeLevel();
        if (soundOff.lastGameplaySound_ != -1 ||
            adlibEffectPlays(soundOff) != plays) return fail("Sound-off gate");

        Game speaker;
        prepare(speaker);
        speaker.speakerEffects_ = true;
        speaker.startGame(Game::Mode::Multiples, 1);
        const BlobView bank = speaker.assets_.gameArchive().find("GSND", 12);
        const std::vector<std::uint8_t> expectedSpeaker =
            renderMeccSpeakerSoundToWave(bank, 15);
        const std::size_t speakerPlays = speaker.effectPlayer_.playCount();
        speaker.completeLevel();
        if (speaker.lastGameplaySound_ != 15 ||
            speaker.effectPlayer_.playCount() != speakerPlays + 1 ||
            speaker.effectPlayer_.bufferHash() != waveHash(expectedSpeaker)) {
            return fail("PC-speaker completion route");
        }
        return true;
    }
    static bool exercisesSpeakerSelection(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.page_ = Game::Page::Playing;
        game.soundOn_ = true;
        game.musicOn_ = false;
        game.speakerEffects_ = false;

        // With music disabled, Alt+P only changes the current driver byte. It
        // neither stops an AdLib cue while selecting speaker nor stops a
        // speaker cue while restoring AdLib.
        (void)game.playOriginalSound(7, false);
        if (!game.attractOplPlayer_.effectPlaying()) return false;
        game.toggleSpeaker();
        if (!game.speakerEffects_ || !game.attractOplPlayer_.effectPlaying() ||
            game.effectPlayer_.playing()) return false;
        (void)game.playOriginalSound(7, false);
        if (!game.effectPlayer_.playing() || game.effectPlayer_.bufferSize() < 1000) return false;
        game.toggleSpeaker();
        if (game.speakerEffects_ || !game.effectPlayer_.playing()) return false;
        game.attractOplPlayer_.stop();
        game.effectPlayer_.stop();

        // Ordinary AdLib cues use a persistent idle chip rather than separate
        // reset-and-mix waveforms. Render one second after each of two cues so
        // the second fingerprint depends on preserved chip/LFO history.
        const std::size_t effectStarts = game.attractOplPlayer_.effectPlayCount();
        (void)game.playOriginalSound(7, false);
        if (!game.attractOplPlayer_.playing() || game.attractOplPlayer_.looping() ||
            !game.attractOplPlayer_.effectPlaying() ||
            game.attractOplPlayer_.effectPlayCount() != effectStarts + 1 ||
            game.effectPlayer_.playing()) return false;
        std::vector<std::int16_t> samples =
            game.attractOplPlayer_.renderTestSamples(49'715);
        (void)game.playOriginalSound(8, false);
        if (game.attractOplPlayer_.effectPlayCount() != effectStarts + 2) return false;
        const std::vector<std::int16_t> second =
            game.attractOplPlayer_.renderTestSamples(49'715);
        samples.insert(samples.end(), second.begin(), second.end());
        std::uint64_t hash = 1469598103934665603ull;
        bool nonzero = false;
        for (const std::int16_t sample : samples) {
            nonzero = nonzero || sample != 0;
            const std::uint16_t bits = static_cast<std::uint16_t>(sample);
            hash ^= static_cast<std::uint8_t>(bits);
            hash *= 1099511628211ull;
            hash ^= static_cast<std::uint8_t>(bits >> 8);
            hash *= 1099511628211ull;
        }
        constexpr std::uint64_t ExpectedPersistentCueHash = 0x5fa8ed9d1584b20aull;
        if (!nonzero || hash != ExpectedPersistentCueHash) {
            std::cerr << "Persistent ordinary OPL cue hash=0x" << std::hex
                      << hash << std::dec << '\n';
            return false;
        }
        game.toggleSound();
        if (game.soundOn_ || !game.attractOplPlayer_.playing() ||
            game.attractOplPlayer_.looping() ||
            !game.attractOplPlayer_.effectPlaying()) return false;

        // With music enabled the asymmetric branch is different: selecting
        // speaker dispatches entry 0 and stops the score, while restoration
        // restarts it only in Demo selector state 2 and leaves a speaker cue
        // already in flight alone.
        game.attractOplPlayer_.stop();
        game.effectPlayer_.stop();
        game.soundOn_ = true;
        game.musicOn_ = true;
        game.speakerEffects_ = false;
        game.startAttract();
        if (!game.attractOplPlayer_.looping()) return false;
        (void)game.attractOplPlayer_.renderTestSamples(1);
        const std::size_t scoreStarts = game.attractOplPlayer_.loopPlayCount();
        game.toggleSpeaker();
        (void)game.attractOplPlayer_.renderTestSamples(1);
        constexpr std::array<std::uint8_t, 9> OperatorOffsets = {
            0, 1, 2, 8, 9, 10, 16, 17, 18
        };
        bool channelsRetired = true;
        for (std::uint8_t channel = 0; channel < OperatorOffsets.size(); ++channel) {
            const std::uint8_t op = OperatorOffsets[channel];
            channelsRetired = channelsRetired &&
                game.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0xc0 + channel)) == 0x00 &&
                game.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0x43 + op)) == 0x3f &&
                game.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0x83 + op)) == 0xff &&
                game.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0xb0 + channel)) == 0x00;
        }
        if (!game.speakerEffects_ || !game.attractOplPlayer_.playing() ||
            game.attractOplPlayer_.looping() || game.attractOplPlayer_.effectPlaying() ||
            game.attractOplPlayer_.scoreWriteCount() != 0 || !channelsRetired ||
            game.attractOplPlayer_.registerValueForTest(0xbd) != 0xc0) return false;
        (void)game.playOriginalSound(7, false);
        if (!game.effectPlayer_.playing()) return false;
        game.toggleSpeaker();
        return !game.speakerEffects_ && game.attractOplPlayer_.looping() &&
               game.attractOplPlayer_.loopPlayCount() == scoreStarts + 1 &&
               game.effectPlayer_.playing();
    }
    static bool audioStopped(const Game& game) {
        return !game.attractOplPlayer_.looping() &&
               !game.attractOplPlayer_.effectPlaying() &&
               game.attractOplPlayer_.scoreWriteCount() == 0 &&
               !game.musicPlayer_.playing() && !game.effectPlayer_.playing();
    }
    static void prepareFilteredModeSelect(Game& game) {
        game.contentSettings_[static_cast<std::size_t>(Game::Mode::Multiples)].use = false;
        game.page_ = Game::Page::ModeSelect;
        game.menuSelection_ = 0;
    }
    static bool startedFactors(const Game& game) {
        return game.page_ == Game::Page::Playing && game.mode_ == Game::Mode::Factors &&
               game.activeBoardMode_ == Game::Mode::Factors;
    }
    static void prepareInformation(Game& game) {
        game.page_ = Game::Page::Information;
        game.informationReturnPage_ = Game::Page::Title;
        game.informationPage_ = 0;
    }
    static bool isInformationPage(const Game& game, int page) {
        return game.page_ == Game::Page::Information && game.informationPage_ == page;
    }
    static bool exercisesRecoveredInstructionRouting(Game& game) {
        game.keyDown(VK_RETURN); // Version -> logo.
        game.keyDown(VK_RETURN); // Logo -> title.
        if (game.page_ != Game::Page::Title || game.menuSelection_ != 0) return false;

        game.keyDown(VK_RETURN); // Play -> instruction question, default No.
        if (game.page_ != Game::Page::InstructionsQuestion || game.menuSelection_ != 1) {
            return false;
        }
        // pSeqKey recognizes Up/Down but deliberately leaves this horizontal
        // Yes/No selection unchanged; Left/Right select the corresponding side.
        game.keyDown(VK_UP);
        game.keyDown(VK_DOWN);
        if (game.menuSelection_ != 1) return false;
        game.keyDown(VK_LEFT);
        if (game.menuSelection_ != 0) return false;
        game.keyDown(VK_LEFT);
        if (game.menuSelection_ != 0) return false;
        game.keyDown(VK_RIGHT);
        if (game.menuSelection_ != 1) return false;
        game.keyDown(VK_RIGHT);
        if (game.menuSelection_ != 1) return false;

        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::InstructionsQuestion || game.menuSelection_ != 1) {
            return false;
        }
        game.keyDown(static_cast<UINT>('Y'));
        if (game.page_ != Game::Page::InstructionsQuestion || game.menuSelection_ != 0) {
            return false;
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Information || game.informationPage_ != 0 ||
            game.informationReturnPage_ != Game::Page::ModeSelect) return false;
        for (const UINT arrow : {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT}) {
            game.keyDown(arrow);
            if (game.page_ != Game::Page::Information || game.informationPage_ != 0) {
                return false;
            }
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::Information || game.informationPage_ != 1) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Title) return false;

        // Normal completion, unlike Escape, proceeds to game selection.
        game.keyDown(VK_RETURN);
        game.keyDown(static_cast<UINT>('Y'));
        if (game.page_ != Game::Page::InstructionsQuestion || game.menuSelection_ != 0) {
            return false;
        }
        game.keyDown(VK_RETURN);
        for (int page = 0; page < 6; ++page) game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::ModeSelect || game.menuSelection_ != 0) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::Title) return false;

        // N/n selects No without accepting it; Enter performs the activation.
        game.keyDown(VK_RETURN);
        game.keyDown(static_cast<UINT>('N'));
        if (game.page_ != Game::Page::InstructionsQuestion || game.menuSelection_ != 1) {
            return false;
        }
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::ModeSelect && game.menuSelection_ == 0;
    }
    static void prepareContentRange(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Multiples);
        game.contentColumn_ = 1;
        game.openContentRange();
    }
    static void prepareContentOtherNumber(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Multiples);
        game.contentColumn_ = 3;
        Game::ContentSettings& settings = game.contentDraft_[0];
        settings.minimum = 3;
        settings.maximum = 15;
        game.openContentOther();
    }
    static void prepareContentOperations(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Equality);
        game.contentColumn_ = 3;
        game.openContentOther();
    }
    static void prepareContentOperationsInequality(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Inequality);
        game.contentColumn_ = 3;
        game.openContentOther();
    }
    static void prepareContentHelp(Game& game) {
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        game.keyDown(VK_F1);
    }
    static bool isContentHelpPage(const Game& game, int page) {
        return game.page_ == Game::Page::OptionsContentHelp && game.contentHelpPage_ == page;
    }
    static void prepareNoGamesValidation(Game& game) {
        game.beginContentEdit();
        for (Game::ContentSettings& settings : game.contentDraft_) settings.use = false;
        game.contentDraft_[0].minimum = 3;
        game.contentDraft_[0].maximum = 15;
        game.contentDraft_[0].randomSequence = false;
        game.contentRow_ = static_cast<int>(Game::Mode::Challenge);
        game.contentColumn_ = 0;
        game.page_ = Game::Page::OptionsContent;
        game.showContentValidation(Game::ContentValidationKind::NoGames);
    }
    static void prepareRangeValidation(Game& game, bool order) {
        game.beginContentEdit();
        game.contentDraft_[0].minimum = 3;
        game.contentDraft_[0].maximum = 15;
        game.contentRow_ = static_cast<int>(Game::Mode::Multiples);
        game.contentColumn_ = 1;
        game.openContentRange();
        game.contentInputStage_ = order ? 1 : 0;
        game.contentNumericInput_ = order ? "3" : "0";
        game.showContentValidation(order ? Game::ContentValidationKind::RangeOrder
                                         : Game::ContentValidationKind::ValueRange);
    }
    static void prepareNoOperationsValidation(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Equality);
        game.contentColumn_ = 3;
        game.contentDraft_[3].operations = {false, false, false, false};
        game.openContentOther();
        game.contentOperationSelection_ = 3;
        game.showContentValidation(Game::ContentValidationKind::NoOperations);
    }
    static bool usesRecoveredNumericEditorTransactions(Game& game) {
        game.beginContentEdit();
        game.contentRow_ = static_cast<int>(Game::Mode::Multiples);
        game.contentColumn_ = 1;
        const Game::ContentSettings originalRange = game.contentDraft_[0];
        game.openContentRange();

        // The maximum allowable value is 20, so the original line editor caps
        // this field at two digits. Empty Enter is parsed as zero and reaches
        // the value-range modal rather than remaining silently in the field.
        game.character(L'1');
        game.character(L'2');
        game.character(L'3');
        if (game.contentNumericInput_ != "12") return false;
        game.contentNumericInput_.clear();
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentValidation ||
            game.contentValidationKind_ != Game::ContentValidationKind::ValueRange) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentRange || game.contentInputStage_ != 0 ||
            !game.contentNumericInput_.empty()) return false;

        // Both candidates remain local until the pair validates. A low>high
        // modal leaves the draft untouched and restarts at the lower field.
        game.character(L'1');
        game.character(L'0');
        game.keyDown(VK_RETURN);
        if (game.contentInputStage_ != 1 || game.contentDraft_[0].minimum != originalRange.minimum) {
            return false;
        }
        game.character(L'5');
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentValidation ||
            game.contentValidationKind_ != Game::ContentValidationKind::RangeOrder ||
            game.contentDraft_[0].minimum != originalRange.minimum ||
            game.contentDraft_[0].maximum != originalRange.maximum) return false;
        game.pointerButton(0, 0, false);
        if (game.page_ != Game::Page::OptionsContentRange || game.contentInputStage_ != 0 ||
            !game.contentNumericInput_.empty()) return false;

        game.character(L'5');
        game.keyDown(VK_RETURN);
        game.character(L'2');
        game.character(L'0');
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContent || game.contentDraft_[0].minimum != 5 ||
            game.contentDraft_[0].maximum != 20) return false;

        // Multiples Other always uses literal bounds 3..50. Escape from its
        // state-1 confirmation loops back to state 0 instead of closing it.
        game.contentDraft_[0].minimum = 20;
        game.contentDraft_[0].maximum = 20;
        game.contentDraft_[0].maxMultiplier = 7;
        game.contentRow_ = static_cast<int>(Game::Mode::Multiples);
        game.contentColumn_ = 3;
        game.openContentOther();
        game.character(L'3');
        game.keyDown(VK_RETURN);
        if (!game.contentNumberConfirm_ || game.page_ != Game::Page::OptionsContentOtherNumber) {
            return false;
        }
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::OptionsContentOtherNumber || game.contentNumberConfirm_ ||
            !game.contentNumericInput_.empty() || game.contentDraft_[0].maxMultiplier != 7) {
            return false;
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentValidation ||
            game.contentValidationKind_ != Game::ContentValidationKind::ValueRange) return false;
        game.keyDown(VK_SPACE);
        game.character(L'5');
        game.character(L'0');
        game.character(L'0');
        if (game.contentNumericInput_ != "50") return false;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::OptionsContent &&
               game.contentDraft_[0].maxMultiplier == 50;
    }
    static int contentEditorFailure(Game& game) {
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        game.keyDown(VK_SPACE); // Do not include Multiples in Challenge.
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_SPACE);
        game.character(L'3');
        game.keyDown(VK_RETURN);
        game.character(L'1');
        game.character(L'5');
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContent || game.contentColumn_ != 1) return 1;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_SPACE); // in order
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_SPACE);
        game.character(L'3');
        game.character(L'0');
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentOtherNumber ||
            !game.contentNumberConfirm_) return 2;
        const std::uint64_t confirmationHash = fullFrameHash(game);
        constexpr std::uint64_t ExpectedConfirmationHash = 0xd06b91c92d4be15aull;
        if (confirmationHash != ExpectedConfirmationHash) {
            std::cerr << "Multiples Other confirmation frame hash=0x" << std::hex
                      << confirmationHash << std::dec << '\n';
            return 11;
        }
        game.keyDown(VK_LEFT);
        game.keyDown(VK_SPACE);
        game.pointerButton(0, 0, false);
        if (game.page_ != Game::Page::OptionsContentOtherNumber ||
            !game.contentNumberConfirm_) return 2;
        const int originalMultiplier = game.contentDialogBackup_.maxMultiplier;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != Game::Page::OptionsContentOtherNumber ||
            game.contentNumberConfirm_ || !game.contentNumericInput_.empty() ||
            game.contentDraft_[0].maxMultiplier != originalMultiplier) return 2;
        game.character(L'3');
        game.character(L'0');
        game.keyDown(VK_RETURN);
        if (!game.contentNumberConfirm_) return 2;
        game.pointerButton(0, 0, true);
        if (game.page_ != Game::Page::OptionsContent) return 2;
        game.keyDown(VK_DOWN); // Equality is the next row with an Other field.
        game.keyDown(VK_SPACE);
        if (game.page_ != Game::Page::OptionsContentOperations) return 3;
        for (int operation = 1; operation < 4; ++operation) {
            game.keyDown(VK_DOWN);
            game.keyDown(VK_SPACE);
        }
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN); // Commit the complete Set Content transaction.

        const Game::ContentSettings& multiples = game.contentSettings_[0];
        const Game::ContentSettings& equality = game.contentSettings_[3];
        if (game.page_ != Game::Page::Options || multiples.use || multiples.minimum != 3 ||
            multiples.maximum != 15 || multiples.randomSequence || multiples.maxMultiplier != 30 ||
            equality.operations != std::array<bool, 4>{true, false, false, false}) return 4;

        game.mode_ = Game::Mode::Multiples;
        game.prepareMode();
        if (game.target_ != 3) return 5;
        game.prepareMode();
        if (game.target_ != 4) return 6;
        for (int board = 0; board < 4; ++board) {
            game.generateBoard();
            for (const Game::Cell& cell : game.cells_) {
                if (!cell.label.empty() && std::stoi(cell.label) > game.target_ * 30) return 7;
            }
        }

        game.mode_ = Game::Mode::Equality;
        for (int board = 0; board < 8; ++board) {
            game.generateBoard();
            if (game.activeBoardMode_ != Game::Mode::Equality) return 8;
            for (const Game::Cell& cell : game.cells_) {
                if (!cell.label.empty() && cell.label.find('+') == std::string::npos) return 9;
            }
        }

        const auto committed = game.contentSettings_;
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        game.keyDown(VK_SPACE);
        game.keyDown(VK_ESCAPE);
        return game.contentSettings_ == committed && game.page_ == Game::Page::Options ? 0 : 10;
    }
    static bool exercisesContentValidation(Game& game) {
        game.beginContentEdit();
        game.page_ = Game::Page::OptionsContent;
        for (Game::ContentSettings& settings : game.contentDraft_) settings.use = false;
        game.contentDraft_[static_cast<std::size_t>(Game::Mode::Challenge)].use = true;
        game.keyDown(VK_RETURN);
        if (game.page_ != Game::Page::OptionsContentValidation ||
            game.contentValidationKind_ != Game::ContentValidationKind::NoGames) return false;
        game.pointerButton(0, 0, true);
        if (game.page_ != Game::Page::OptionsContentValidation) return false;
        game.pointerButton(0, 0, false);
        if (game.page_ != Game::Page::OptionsContent) return false;

        game.contentDraft_[0].use = true;
        game.contentRow_ = 0;
        game.contentColumn_ = 1;
        game.keyDown(VK_SPACE);
        game.character(L'0');
        game.keyDown(VK_RETURN);
        if (game.contentValidationKind_ != Game::ContentValidationKind::ValueRange) return false;
        game.keyDown(VK_SPACE);
        game.character(L'\b');
        game.character(L'3');
        game.keyDown(VK_RETURN);
        game.character(L'2');
        game.keyDown(VK_RETURN);
        if (game.contentValidationKind_ != Game::ContentValidationKind::RangeOrder) return false;
        game.keyDown(VK_SPACE);
        game.keyDown(VK_ESCAPE);

        game.contentRow_ = static_cast<int>(Game::Mode::Equality);
        game.contentColumn_ = 3;
        game.keyDown(VK_SPACE);
        game.contentDraft_[3].operations = {false, false, false, false};
        game.keyDown(VK_RETURN);
        if (game.contentValidationKind_ != Game::ContentValidationKind::NoOperations) return false;
        game.keyDown(VK_SPACE);
        game.keyDown(VK_SPACE);
        game.keyDown(VK_RETURN);
        return game.page_ == Game::Page::OptionsContent;
    }
    static bool liveNm000LeftEntryMatches(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.page_ = Game::Page::Playing;
        game.mode_ = Game::Mode::Multiples;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 13;
        game.level_ = 1;
        game.score_ = 0;
        game.lives_ = 4;
        game.correctRemaining_ = 10;
        game.playerRow_ = 2;
        game.playerColumn_ = 3;
        game.playerTerminalFrame_ = -1;
        game.moving_ = false;
        game.munching_ = false;
        game.deathAnimating_ = false;
        game.playerRecovering_ = false;
        game.enemyWarning_ = false;
        game.attractMode_ = false;
        game.hallPaletteActive_ = false;
        game.attractPostFeedbackBoard_ = false;
        game.enemies_.clear();
        game.enemySlotCount_ = 1;
        for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.safeZoneJobCount_ = 0;
        for (Game::SafeZoneJob& job : game.safeZoneJobs_) job = {};

        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "39",  "577", "312", "222", "483", "117",
            "416", "380", "65",  "219", "172", "132",
            "547", "270", "78",  "",    "468", "286",
            "331", "82",  "364", "507", "346", "208",
            "493", "465", "432", "248", "192", "183",
        };
        for (std::size_t index = 0; index < Labels.size(); ++index) {
            Game::Cell& current = game.cells_[index];
            current = {};
            current.label = std::string(Labels[index]);
            current.eaten = current.label.empty();
            if (!current.label.empty()) {
                current.correct = std::stoi(current.label) % game.target_ == 0;
            }
        }

        const auto expectHash = [&](std::string_view state,
                                    const std::uint64_t expected) {
            const std::uint64_t actual = fullFrameHash(game);
            if (actual == expected) return true;
            std::cerr << "nm_000 " << state << " full-frame hash=0x" << std::hex
                      << actual << ", expected 0x" << expected << std::dec << '\n';
            return false;
        };

        // Runs 13/14 isolate the first safe-zone expiry. Runs 16/17 then
        // isolate the warning while the last safe zone expires; run 19 is the
        // completed warning removal before the actor is visible.
        game.cell(2, 1).safe = true;
        game.cell(3, 3).safe = true;
        if (!expectHash("two safe zones", 0x890c293f7df8a877ull)) return false;
        game.cell(2, 1).safe = false;
        if (!expectHash("last safe zone", 0x29d0a1a8be8da53full)) return false;
        game.enemyWarning_ = true;
        if (!expectHash("warning with safe zone", 0xddd30b30e2d2144dull)) return false;
        game.cell(3, 3).safe = false;
        if (!expectHash("warning after safe-zone expiry", 0x62e80f6dda05399dull)) {
            return false;
        }
        game.enemyWarning_ = false;
        if (!expectHash("warning removed", 0x1766e260202cb0b7ull)) return false;

        Game::Enemy reggie;
        reggie.row = 2;
        reggie.column = 0;
        reggie.fromRow = 2;
        reggie.fromColumn = -1;
        reggie.type = 0;
        reggie.slot = 0;
        reggie.direction = 1;
        reggie.moving = true;
        reggie.entering = true;
        game.enemies_.push_back(reggie);

        constexpr std::array<std::uint64_t, 5> EntryHashes = {
            0x1a90cf1d67b26cdbull, // phase 2: clipped clear box only
            0x869e0dd009334da3ull, // phase 3
            0x2fbd082df523f344ull, // phase 4
            0x1a543c81a846f4d5ull, // phase 5
            0xe42c2a440d9c2fdbull, // phase 6: endpoint middle pose
        };
        const double interval = 3.0 / OriginalSchedulerTicksPerSecond;
        const double total = game.enemyMoveAnimationDuration(1) + interval;
        for (int phase = 2; phase <= 6; ++phase) {
            game.enemies_[0].animationTimer = total - (phase - 1) * interval;
            if (game.enemyMovementPhase(game.enemies_[0]) != phase ||
                !expectHash("left-entry phase", EntryHashes[static_cast<std::size_t>(phase - 2)])) {
                return false;
            }
        }

        // The terminal callback installs the same direction-right middle
        // record at x=24; DOS run 24 consequently merges phase 6 and dwell.
        game.enemies_[0].moving = false;
        game.enemies_[0].entering = false;
        game.enemies_[0].fromColumn = 0;
        return expectHash("left-entry dwell", EntryHashes.back());
    }
    static bool liveNm001RightEntryMatches(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.page_ = Game::Page::Playing;
        game.mode_ = Game::Mode::Multiples;
        game.activeBoardMode_ = Game::Mode::Multiples;
        game.target_ = 13;
        game.level_ = 1;
        game.score_ = 0;
        game.lives_ = 3;
        game.correctRemaining_ = 10;
        game.playerRow_ = 2;
        game.playerColumn_ = 3;
        game.playerTerminalFrame_ = -1;
        game.moving_ = false;
        game.munching_ = false;
        game.deathAnimating_ = false;
        game.playerRecovering_ = false;
        game.enemyWarning_ = false;
        game.attractMode_ = false;
        game.hallPaletteActive_ = false;
        game.attractPostFeedbackBoard_ = false;
        game.enemies_.clear();
        game.enemySlotCount_ = 1;
        for (Game::EnemySlot& slot : game.enemySlots_) slot = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = Game::EnemySlotPhase::Active;
        game.safeZoneJobCount_ = 0;
        for (Game::SafeZoneJob& job : game.safeZoneJobs_) job = {};

        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "39",  "577", "312", "222", "483", "117",
            "416", "380", "65",  "219", "172", "132",
            "130", "442", "608", "",    "84",  "59",
            "331", "82",  "364", "507", "346", "208",
            "493", "465", "432", "248", "192", "183",
        };
        for (std::size_t index = 0; index < Labels.size(); ++index) {
            Game::Cell& current = game.cells_[index];
            current = {};
            current.label = std::string(Labels[index]);
            current.eaten = current.label.empty();
            if (!current.label.empty()) {
                current.correct = std::stoi(current.label) % game.target_ == 0;
            }
        }

        const auto expectHash = [&](std::string_view state,
                                    const std::uint64_t expected) {
            const std::uint64_t actual = fullFrameHash(game);
            if (actual == expected) return true;
            std::cerr << "nm_001 " << state << " full-frame hash=0x" << std::hex
                      << actual << ", expected 0x" << expected << std::dec << '\n';
            return false;
        };

        game.enemyWarning_ = true;
        if (!expectHash("warning", 0xc13d985d68aad47dull)) return false;
        game.cell(1, 2).safe = true;
        if (!expectHash("warning with safe zone", 0xec7121e89ef91215ull)) return false;
        game.enemyWarning_ = false;
        if (!expectHash("warning removed", 0x347c8ff188b271cbull)) return false;

        Game::Enemy reggie;
        reggie.row = 2;
        reggie.column = 5;
        reggie.fromRow = 2;
        reggie.fromColumn = 6;
        reggie.type = 0;
        reggie.slot = 0;
        reggie.direction = 3;
        reggie.moving = true;
        reggie.entering = true;
        game.enemies_.push_back(reggie);

        constexpr std::array<std::uint64_t, 5> EntryHashes = {
            0xba7050b661bb0c15ull,
            0xa44c939bdb119674ull,
            0x78d0a537912e076cull,
            0x9e9bc76341973959ull,
            0xc466af82ade08f12ull,
        };
        const double interval = 3.0 / OriginalSchedulerTicksPerSecond;
        const double total = game.enemyMoveAnimationDuration(3) + interval;
        for (int phase = 2; phase <= 6; ++phase) {
            game.enemies_[0].animationTimer = total - (phase - 1) * interval;
            if (game.enemyMovementPhase(game.enemies_[0]) != phase ||
                !expectHash("right-entry phase", EntryHashes[static_cast<std::size_t>(phase - 2)])) {
                return false;
            }
        }

        game.enemies_[0].moving = false;
        game.enemies_[0].entering = false;
        game.enemies_[0].fromColumn = 5;
        return expectHash("right-entry dwell", 0x4d86e27dd8f949f4ull);
    }
    static bool liveDemoBottomBashfulEntryMatches(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.scorePersistenceEnabled_ = false;
        game.random_.seed(62853u);
        game.startAttract();

        // The first supplied full-demo board deterministically schedules a
        // Bashful arrival from below at row 4, column 0. Source frames
        // 2866-2874 expose all four vertical entry phases; frame 2870 is an
        // incomplete 2x refresh and is intentionally excluded. Frames
        // 2875-2918 then hold the completed resident pose.
        constexpr std::array<std::uint64_t, 4> EntryHashes = {
            0x9b285be3f8494708ull,
            0x4dcb586c49227eb7ull,
            0x596ca0baaab14f93ull,
            0x7d512488e348373cull,
        };
        std::array<bool, EntryHashes.size()> sawPhase{};
        constexpr double FrameSeconds = 1.0 / 70.086;
        for (double elapsed = 0.0; elapsed < 8.0; elapsed += FrameSeconds) {
            game.update(FrameSeconds);
            const auto bashful = std::find_if(
                game.enemies_.begin(), game.enemies_.end(), [](const Game::Enemy& enemy) {
                    return enemy.type == 2 && enemy.row == 4 && enemy.column == 0 &&
                           enemy.fromRow == 5 && enemy.fromColumn == 0 &&
                           enemy.direction == 0;
                });
            if (bashful == game.enemies_.end()) continue;
            if (bashful->entering && bashful->moving) {
                const int phase = game.enemyMovementPhase(*bashful);
                if (phase < 2 || phase > 5) continue;
                const std::uint64_t actual = fullFrameHash(game);
                const std::uint64_t expected =
                    EntryHashes[static_cast<std::size_t>(phase - 2)];
                if (actual != expected) {
                    std::cerr << "full-demo bottom Bashful phase " << phase
                              << " full-frame hash=0x" << std::hex << actual
                              << ", expected 0x" << expected << std::dec << '\n';
                    return false;
                }
                sawPhase[static_cast<std::size_t>(phase - 2)] = true;
                continue;
            }
            if (!bashful->entering && !bashful->moving) {
                const std::uint64_t actual = fullFrameHash(game);
                constexpr std::uint64_t DwellHash = 0xb5e9980aa54703c6ull;
                if (actual != DwellHash) {
                    std::cerr << "full-demo bottom Bashful dwell full-frame hash=0x"
                              << std::hex << actual << ", expected 0x" << DwellHash
                              << std::dec << '\n';
                    return false;
                }
                return std::all_of(sawPhase.begin(), sawPhase.end(),
                                   [](const bool saw) { return saw; });
            }
        }
        return false;
    }
    static bool usesCapturedLaterBottomBashfulEntryFrames() {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "21",  "",    "",    "",   "133", "11",
            "",    "",    "",    "",   "",    "",
            "",    "",    "",    "",   "",    "134",
            "59",  "211", "176", "14", "2",   "230",
            "220", "207", "115", "23", "125", "175",
        };
        struct CapturedState {
            int playerState; // 3/4=down move, 5=terminal, 6=idle
            int bashfulState; // 1=invisible entry, 2..5=visible entry, 6=dwell
            std::uint64_t hash;
        };
        // Global frames 5188-5215 expose another bottom-entry Bashful while
        // the Muncher finishes a downward step, an earlier Reggie dwells at
        // row 0/column 2, and a later slot keeps the warning visible. Seven
        // presentation-complete pages match below. Frame 5196 has twenty
        // nonuniform doubled blocks and remains capture-only.
        static constexpr std::array<CapturedState, 7> CapturedStates = {{
            {3, 1, 0x08b631a66e5d0f0full},
            {4, 1, 0x90456223812679d4ull},
            {5, 2, 0x9c0190c31703db30ull},
            {6, 3, 0xbcaa9e6d0c9ce28bull},
            {6, 4, 0xe1a2c8d0cb0630ebull},
            {6, 5, 0xd35094db8e41f03cull},
            {6, 6, 0x364c0086fe50ea46ull},
        }};
        for (const CapturedState& state : CapturedStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Multiples;
            candidate.mode_ = Game::Mode::Multiples;
            candidate.target_ = 5;
            candidate.level_ = 10;
            candidate.playerRow_ = 3;
            candidate.playerColumn_ = 0;
            candidate.playerTerminalFrame_ = 7;
            candidate.enemyWarning_ = true;
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            if (state.playerState <= 4) {
                candidate.moving_ = true;
                candidate.moveDirection_ = 2;
                candidate.moveFromRow_ = 3;
                candidate.moveFromColumn_ = 0;
                candidate.moveToRow_ = 4;
                candidate.moveToColumn_ = 0;
                candidate.moveTimer_ = candidate.playerMoveAnimationDuration(2) -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            } else {
                candidate.playerRow_ = 4;
                candidate.playerTerminalFrame_ =
                    state.playerState == 5 ? 8 : 7;
            }

            Game::Enemy reggie{};
            reggie.type = 0;
            reggie.slot = 0;
            reggie.row = 0;
            reggie.column = 2;
            reggie.fromRow = 0;
            reggie.fromColumn = 2;
            reggie.direction = 2;
            reggie.dwellFrame = 7;
            candidate.enemies_.push_back(reggie);

            Game::Enemy bashful{};
            bashful.type = 2;
            bashful.slot = 1;
            bashful.row = 4;
            bashful.column = 3;
            bashful.fromRow = 5;
            bashful.fromColumn = 3;
            bashful.direction = 0;
            if (state.bashfulState <= 5) {
                bashful.moving = true;
                bashful.entering = true;
                const double interval =
                    candidate.enemyMoveAnimationDuration(0) / 5.0;
                bashful.animationTimer =
                    candidate.enemyMoveAnimationDuration(0) + interval -
                    static_cast<double>(state.bashfulState - 1) * interval;
            } else {
                bashful.fromRow = 4;
                bashful.dwellFrame = 1;
            }
            candidate.enemies_.push_back(bashful);

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "later bottom-entry Bashful mismatch: player="
                          << state.playerState << " bashful="
                          << state.bashfulState << " hash=0x" << std::hex
                          << actual << " expected=0x" << state.hash
                          << std::dec << '\n';
                return false;
            }
        }
        return true;
    }
    static bool usesCapturedLaterBottomBashfulExitFrames() {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "21",  "",    "5",   "",   "133", "11",
            "",    "",    "",    "",   "",    "",
            "",    "",    "",    "",   "",    "134",
            "59",  "211", "176", "14", "2",   "230",
            "",    "207", "115", "26", "125", "175",
        };
        struct CapturedState {
            int playerState; // 0=standing, 1..5=upward movement
            int bashfulState; // 0=dwell, 1..5=bottom exit, 6=removed
            std::uint64_t hash;
        };
        // Global frames 5687-5718 cover the Bashful's final left-facing
        // dwell, all five downward-exit callbacks, and terminal removal as
        // the Muncher starts upward toward a resident Reggie. Frame 5711 is
        // an exact scanline-90 splice of its completed neighbors and is
        // deliberately absent from this seven-state gate.
        static constexpr std::array<CapturedState, 7> CapturedStates = {{
            {0, 0, 0xe572978e3378b63cull},
            {0, 1, 0x889c0f21b7d634d5ull},
            {1, 2, 0x8f14e90902d728c7ull},
            {2, 3, 0xb55be53e20024918ull},
            {3, 4, 0xe4fec8a6974cfe71ull},
            {4, 5, 0x8abe6082e3593b9full},
            {5, 6, 0xfe9afa1a084c2395ull},
        }};
        for (const CapturedState& state : CapturedStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Multiples;
            candidate.mode_ = Game::Mode::Multiples;
            candidate.target_ = 5;
            candidate.level_ = 10;
            candidate.enemyWarning_ = true;
            candidate.playerRow_ = 2;
            candidate.playerColumn_ = 2;
            candidate.playerTerminalFrame_ = 7;
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            candidate.cell(2, 1).safe = true;
            if (state.bashfulState > 0) {
                candidate.cell(4, 2).label = "105";
                candidate.cell(4, 2).correct = true;
            }
            if (state.playerState > 0) {
                candidate.moving_ = true;
                candidate.moveDirection_ = 0;
                candidate.moveFromRow_ = 2;
                candidate.moveFromColumn_ = 2;
                candidate.moveToRow_ = 1;
                candidate.moveToColumn_ = 2;
                candidate.moveTimer_ = candidate.playerMoveAnimationDuration(0) -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            }

            Game::Enemy reggie{};
            reggie.type = 0;
            reggie.slot = 0;
            reggie.row = 1;
            reggie.column = 2;
            reggie.fromRow = 1;
            reggie.fromColumn = 2;
            reggie.direction = 2;
            reggie.dwellFrame = 15;
            candidate.enemies_.push_back(reggie);

            if (state.bashfulState < 6) {
                Game::Enemy bashful{};
                bashful.type = 2;
                bashful.slot = 1;
                bashful.direction = state.bashfulState == 0 ? 3 : 2;
                bashful.fromRow = 4;
                bashful.fromColumn = 2;
                bashful.row = state.bashfulState == 0 ? 4 : 5;
                bashful.column = 2;
                bashful.savedCell.label = "115";
                bashful.savedCell.eaten = false;
                bashful.savedCell.correct = true;
                bashful.savedCellValid = true;
                if (state.bashfulState == 0) {
                    bashful.dwellFrame = 10;
                } else {
                    bashful.moving = true;
                    bashful.exiting = true;
                    const double interval =
                        candidate.enemyMoveAnimationDuration(2) / 5.0;
                    bashful.animationTimer =
                        candidate.enemyMoveAnimationDuration(2) + interval -
                        static_cast<double>(state.bashfulState - 1) * interval;
                }
                candidate.enemies_.push_back(bashful);
            }

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "later bottom-exit Bashful mismatch: player="
                          << state.playerState << " bashful="
                          << state.bashfulState << " hash=0x" << std::hex
                          << actual << " expected=0x" << state.hash
                          << std::dec << '\n';
                return false;
            }
        }
        return true;
    }
    static bool usesCapturedFirstDemoRightApproachFrames() {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "21",  "",    "5",   "",   "133", "11",
            "",    "",    "",    "",   "",    "",
            "",    "",    "",    "",   "",    "134",
            "59",  "211", "176", "14", "2",   "230",
            "",    "207", "115", "26", "125", "175",
        };
        struct CapturedState {
            int playerState; // 0=standing, 1..5=right move, 6=endpoint, 7=idle
            std::uint64_t hash;
        };
        // Global frames 5660-5687 expose the complete rightward approach
        // immediately before the later bottom-exit interval. The terminal
        // idle page is shared with that interval, so this gate contributes
        // seven new pages while proving all six horizontal callbacks: five
        // interpolated positions and the endpoint record.
        static constexpr std::array<CapturedState, 8> CapturedStates = {{
            {0, 0x257ca66a2aac6e80ull},
            {1, 0x448aa93616a62b0aull},
            {2, 0x7586d4decf80d10dull},
            {3, 0x0529e61003fee875ull},
            {4, 0xce02dc247cbcbbd0ull},
            {5, 0x25d12539700e6796ull},
            {6, 0x9b96da287406f039ull},
            {7, 0xe572978e3378b63cull},
        }};
        for (const CapturedState& state : CapturedStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Multiples;
            candidate.mode_ = Game::Mode::Multiples;
            candidate.target_ = 5;
            candidate.level_ = 10;
            candidate.enemyWarning_ = true;
            candidate.playerRow_ = 2;
            candidate.playerColumn_ = state.playerState >= 6 ? 2 : 1;
            candidate.playerTerminalFrame_ =
                state.playerState == 6 ? 4 : 7;
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            candidate.cell(2, 1).safe = true;
            if (state.playerState >= 1 && state.playerState <= 5) {
                candidate.moving_ = true;
                candidate.moveDirection_ = 1;
                candidate.moveFromRow_ = 2;
                candidate.moveFromColumn_ = 1;
                candidate.moveToRow_ = 2;
                candidate.moveToColumn_ = 2;
                candidate.moveTimer_ = candidate.playerMoveAnimationDuration(1) -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            }

            Game::Enemy reggie{};
            reggie.type = 0;
            reggie.slot = 0;
            reggie.row = 1;
            reggie.column = 2;
            reggie.fromRow = 1;
            reggie.fromColumn = 2;
            reggie.direction = 2;
            reggie.dwellFrame = 15;
            candidate.enemies_.push_back(reggie);

            Game::Enemy bashful{};
            bashful.type = 2;
            bashful.slot = 1;
            bashful.row = 4;
            bashful.column = 2;
            bashful.fromRow = 4;
            bashful.fromColumn = 2;
            bashful.direction = 3;
            bashful.dwellFrame = 10;
            bashful.savedCell.label = "115";
            bashful.savedCell.eaten = false;
            bashful.savedCell.correct = true;
            bashful.savedCellValid = true;
            candidate.enemies_.push_back(bashful);

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "first-Demo right-approach mismatch: player="
                          << state.playerState << " hash=0x" << std::hex
                          << actual << " expected=0x" << state.hash
                          << std::dec << '\n';
                return false;
            }
        }
        return true;
    }
    static bool reproducesCapturedFirstAttractBoard(const Game& game) {
        static constexpr std::array<std::string_view, Game::BoardCellCount> labels = {
            "21", "20", "232", "180", "133", "190",
            "145", "215", "", "180", "130", "35",
            "207", "245", "170", "229", "124", "232",
            "59", "211", "176", "14", "2", "230",
            "80", "220", "188", "92", "146", "85"};
        if (!game.attractMode_ || game.activeBoardMode_ != Game::Mode::Multiples || game.target_ != 5 ||
            game.level_ != 10 || game.attractDifficultyIndex_ != 9 ||
            game.playerRow_ != 1 || game.playerColumn_ != 2 || game.correctRemaining_ != 14 ||
            game.enemySlotCount_ != 3 || game.safeZoneJobCount_ != 1) {
            std::cerr << "Captured demo state mismatch: mode=" << static_cast<int>(game.activeBoardMode_)
                      << " target=" << game.target_ << " relation=" << game.relation_
                      << " level=" << game.level_ << " tier=" << game.attractDifficultyIndex_
                      << " player=" << game.playerRow_ << ',' << game.playerColumn_
                      << " remaining=" << game.correctRemaining_ << " enemies=" << game.enemySlotCount_
                      << " safeJobs=" << game.safeZoneJobCount_ << '\n';
            return false;
        }
        for (int index = 0; index < Game::BoardCellCount; ++index) {
            const Game::Cell& cell = game.cells_[static_cast<std::size_t>(index)];
            if (cell.label != labels[static_cast<std::size_t>(index)] ||
                cell.label.empty() != cell.eaten) {
                std::cerr << "Captured demo cell " << index << " mismatch: label='" << cell.label
                          << "' expected='" << labels[static_cast<std::size_t>(index)]
                          << "' eaten=" << cell.eaten << '\n';
                return false;
            }
        }
        return true;
    }
    static bool usesCapturedDualHelperMovementFrames() {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "7",  "",   "",   "",   "",   "8",
            "2",  "",   "59", "12", "",   "",
            "2",  "14", "1",  "63", "21", "3",
            "21", "8",  "1",  "2",  "48", "12",
            "3",  "8",  "30", "21", "8",  "7",
        };
        struct CapturedEntryState {
            int playerState; // 0=start dwell, 1..5=right move, 6=terminal, 7=idle
            int leftEntryPhase; // 0=not visible, 1..6=entry, 7=dwell
            int rightEntryPhase;
            bool warning;
            std::uint64_t hash;
        };
        // Global frames 19420-19480 expose simultaneous left- and right-edge
        // Helper arrivals over a rightward Muncher walk. Thirteen completed
        // source pages match these logical states. Runs 5 and 11 are torn 2x
        // refreshes; run 2 is a one-frame 26-pixel partial warning-box erase.
        static constexpr std::array<CapturedEntryState, 13> CapturedEntryStates = {{
            {0, 0, 0, true,  0x07f4832492a772e9ull},
            {1, 0, 0, true,  0x005a082303ed2babull},
            {2, 3, 1, false, 0xf7aa52d35dba34b3ull},
            {3, 3, 1, false, 0xd6a522f946bf2c47ull},
            {4, 4, 1, false, 0x6dcfa997077ba745ull},
            {5, 4, 2, false, 0xb9db92597082506eull},
            {6, 5, 2, false, 0xb139b4e58feb6c46ull},
            {7, 5, 3, false, 0xd2d72c6cd483a55bull},
            {7, 6, 3, false, 0xeb29e4f2d7a21b9eull},
            {7, 6, 4, false, 0xdf7db7a8972b3979ull},
            {7, 6, 5, false, 0xe05b93092d8a6ebaull},
            {7, 6, 6, false, 0x9be17feb975ac459ull},
            {7, 7, 7, false, 0x8aa8b06f7ec4da3bull},
        }};
        const auto configureEntryState = [&](Game& entry,
                                             const CapturedEntryState& state) {
            entry.page_ = Game::Page::Attract;
            entry.attractMode_ = true;
            entry.activeBoardMode_ = Game::Mode::Factors;
            entry.mode_ = Game::Mode::Factors;
            entry.target_ = 63;
            entry.level_ = 10;
            entry.playerRow_ = 1;
            entry.playerColumn_ = 4;
            entry.playerTerminalFrame_ = 7;
            if (state.playerState >= 1 && state.playerState <= 5) {
                entry.moving_ = true;
                entry.moveDirection_ = 1;
                entry.moveFromRow_ = 1;
                entry.moveFromColumn_ = 4;
                entry.moveToRow_ = 1;
                entry.moveToColumn_ = 5;
                entry.moveTimer_ = entry.playerMoveAnimationDuration(1) -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            } else if (state.playerState >= 6) {
                entry.playerColumn_ = 5;
                entry.playerTerminalFrame_ = state.playerState == 6 ? 4 : 7;
            }
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = entry.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            // This correct Factors target is still resident throughout the
            // entry/walk interval; the following Demo chew removes it.
            entry.cell(1, 5).label = "63";
            entry.cell(1, 5).eaten = false;
            entry.cell(1, 5).correct = true;
            entry.cell(2, 1).safe = true;
            entry.cell(2, 4).safe = true;
            entry.enemyWarning_ = state.warning;

            const auto addEntry = [&](const bool left, const int phase,
                                      const int slot) {
                if (phase == 0) return;
                Game::Enemy enemy{};
                enemy.type = 3;
                enemy.slot = slot;
                enemy.row = left ? 4 : 2;
                enemy.column = left ? 0 : 5;
                enemy.fromRow = enemy.row;
                enemy.fromColumn = left ? -1 : 6;
                enemy.direction = left ? 1 : 3;
                if (phase <= 6) {
                    enemy.moving = true;
                    enemy.entering = true;
                    const double interval =
                        entry.enemyMoveAnimationDuration(enemy.direction) / 6.0;
                    enemy.animationTimer =
                        entry.enemyMoveAnimationDuration(enemy.direction) +
                        interval - static_cast<double>(phase - 1) * interval;
                } else {
                    enemy.fromColumn = enemy.column;
                    enemy.dwellFrame = enemy.direction * 3 + 1;
                }
                entry.enemies_.push_back(enemy);
            };
            addEntry(true, state.leftEntryPhase, 0);
            addEntry(false, state.rightEntryPhase, 1);
        };
        for (const CapturedEntryState& state : CapturedEntryStates) {
            Game entry;
            configureEntryState(entry, state);
            const std::uint64_t actual = fullFrameHash(entry);
            if (actual != state.hash) {
                std::cerr << "dual-Helper entry state mismatch: player="
                          << state.playerState << " left="
                          << state.leftEntryPhase << " right="
                          << state.rightEntryPhase << " warning="
                          << state.warning << " hash=0x" << std::hex << actual
                          << " expected=0x" << state.hash << std::dec << '\n';
                return false;
            }
        }
        {
            // Source run 2 keeps this otherwise complete retained-player/
            // left-Helper page but has already cleared only the bottom 26
            // pixels of the warning outline. Gate the completed neighboring
            // native surface so that partial erase never becomes a native
            // presentation state.
            constexpr CapturedEntryState CompletedWarningSurface{
                1, 3, 0, true, 0x321eddc11c6bca6eull
            };
            Game entry;
            configureEntryState(entry, CompletedWarningSurface);
            if (fullFrameHash(entry) != CompletedWarningSurface.hash) {
                return false;
            }
        }
        struct LaterHelperState {
            int playerMovePhase; // 0=not moving, otherwise left phase 1..5
            int playerColumn;
            int playerFrame;
            int bottomState; // 0=dwell, 1..6=right, 7=dwell, 8..12=exit, 13=gone
            int rightState; // 0=dwell, 1..5=down, 6=dwell
            bool targetCleared;
            std::uint64_t hash;
        };
        // Global frames 19690-19850 continue the same two Helper jobs. Twenty-
        // seven presentation-complete pages cover a horizontal destructive
        // trail, a leftward player walk, the complete seven-record chew, a
        // vertical destructive trail, and an overlapping bottom exit. Source
        // runs 3 and 21 are torn; run 10 has a 15-pixel incomplete player paint.
        static constexpr std::array<LaterHelperState, 27> LaterHelperStates = {{
            {0, 4, 7,  0, 0, false, 0xa7e11bbf5589d93dull},
            {0, 4, 7,  1, 0, false, 0x45f6c73e4816d7eeull},
            {0, 4, 7,  2, 0, false, 0x92cbd0f8c33d42dbull},
            {0, 4, 7,  3, 0, false, 0xdab0e582c72e7ae0ull},
            {0, 4, 7,  4, 0, false, 0x9ede04df3e141ac1ull},
            {0, 4, 7,  5, 0, false, 0x1bf2e2dcd9fc4ec4ull},
            {0, 4, 7,  6, 0, false, 0x9ec8eb6b65a89811ull},
            {1, 4, -1, 6, 0, false, 0x395197778fe62868ull},
            {2, 4, -1, 7, 0, false, 0xe85483197037acfdull},
            {3, 4, -1, 7, 0, false, 0x416c0289c1f46ce0ull},
            {4, 4, -1, 7, 0, false, 0xfbd47a9775a7cd01ull},
            {5, 4, -1, 7, 0, false, 0x4d0d359dbdd1f15eull},
            {0, 3, 10, 7, 0, false, 0xa7032f4b0697bdd8ull},
            {0, 3, 7,  7, 0, false, 0xb896ffe2b812e1a5ull},
            {0, 3, 12, 7, 0, true,  0x66e29e37d8962617ull},
            {0, 3, 13, 7, 0, true,  0x3ad4bcd952e8e00bull},
            {0, 3, 14, 7, 0, true,  0x66e29e37d8962617ull},
            {0, 3, 13, 7, 1, true,  0x4ea920a9baeba5ecull},
            {0, 3, 12, 7, 1, true,  0xd2522199b3ac4588ull},
            {0, 3, 13, 7, 2, true,  0xd33983e72897d019ull},
            {0, 3, 14, 7, 2, true,  0x1112ceb0aef08d8dull},
            {0, 3, 13, 8, 3, true,  0x2ac17cbca9a231b7ull},
            {0, 3, 13, 9, 4, true,  0x3a09873ec9f0d9a0ull},
            {0, 3, 13, 10, 5, true, 0x864c153a430a0187ull},
            {0, 3, 13, 11, 6, true, 0xd427e384ee41c0c5ull},
            {0, 3, 13, 12, 6, true, 0x79ef2b6f138cc59bull},
            {0, 3, 13, 13, 6, true, 0xd9ab09f4f4958cb5ull},
        }};
        for (const LaterHelperState& state : LaterHelperStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Factors;
            candidate.mode_ = Game::Mode::Factors;
            candidate.target_ = 63;
            candidate.level_ = 10;
            candidate.playerRow_ = 2;
            candidate.playerColumn_ = state.playerColumn;
            candidate.playerTerminalFrame_ = state.playerFrame;
            if (state.playerMovePhase > 0) {
                candidate.moving_ = true;
                candidate.moveDirection_ = 3;
                candidate.moveFromRow_ = 2;
                candidate.moveFromColumn_ = 4;
                candidate.moveToRow_ = 2;
                candidate.moveToColumn_ = 3;
                candidate.moveTimer_ = candidate.playerMoveAnimationDuration(3) -
                    static_cast<double>(state.playerMovePhase) /
                        OriginalSchedulerTicksPerSecond;
            }
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            candidate.cell(2, 1).safe = true;
            candidate.cell(2, 4).safe = true;
            for (const auto& cleared : {
                     std::pair{2, 4}, std::pair{2, 5}, std::pair{4, 0}}) {
                candidate.cell(cleared.first, cleared.second).label.clear();
                candidate.cell(cleared.first, cleared.second).eaten = true;
            }
            if (state.targetCleared) {
                candidate.cell(2, 3).label.clear();
                candidate.cell(2, 3).eaten = true;
            }
            if (state.bottomState >= 1) {
                candidate.cell(4, 1).label.clear();
                candidate.cell(4, 1).eaten = true;
            }
            if (state.bottomState >= 8) {
                candidate.cell(4, 2).label.clear();
                candidate.cell(4, 2).eaten = true;
            }
            if (state.rightState >= 1) {
                candidate.cell(3, 5).label.clear();
                candidate.cell(3, 5).eaten = true;
            }

            if (state.bottomState < 13) {
                Game::Enemy bottom{};
                bottom.type = 3;
                bottom.slot = 0;
                bottom.direction = state.bottomState >= 8 ? 2 : 1;
                if (state.bottomState == 0) {
                    bottom.row = 4;
                    bottom.column = 1;
                    bottom.fromRow = 4;
                    bottom.fromColumn = 1;
                    bottom.dwellFrame = 4;
                } else if (state.bottomState <= 6) {
                    bottom.row = 4;
                    bottom.column = 2;
                    bottom.fromRow = 4;
                    bottom.fromColumn = 1;
                    bottom.moving = true;
                    const double interval =
                        candidate.enemyMoveAnimationDuration(1) / 6.0;
                    bottom.animationTimer =
                        candidate.enemyMoveAnimationDuration(1) -
                        static_cast<double>(state.bottomState - 1) * interval;
                } else if (state.bottomState == 7) {
                    bottom.row = 4;
                    bottom.column = 2;
                    bottom.fromRow = 4;
                    bottom.fromColumn = 2;
                    bottom.dwellFrame = 4;
                } else {
                    bottom.row = 5;
                    bottom.column = 2;
                    bottom.fromRow = 4;
                    bottom.fromColumn = 2;
                    bottom.moving = true;
                    bottom.exiting = true;
                    const double interval =
                        candidate.enemyMoveAnimationDuration(2) / 5.0;
                    bottom.animationTimer =
                        candidate.enemyMoveAnimationDuration(2) + interval -
                        static_cast<double>(state.bottomState - 8) * interval;
                }
                candidate.enemies_.push_back(bottom);
            }

            Game::Enemy right{};
            right.type = 3;
            right.slot = 1;
            right.direction = 2;
            if (state.rightState == 0) {
                right.row = 3;
                right.column = 5;
                right.fromRow = 3;
                right.fromColumn = 5;
                right.dwellFrame = 15;
            } else if (state.rightState <= 5) {
                right.row = 4;
                right.column = 5;
                right.fromRow = 3;
                right.fromColumn = 5;
                right.moving = true;
                const double interval =
                    candidate.enemyMoveAnimationDuration(2) / 5.0;
                right.animationTimer =
                    candidate.enemyMoveAnimationDuration(2) -
                    static_cast<double>(state.rightState - 1) * interval;
            } else {
                right.row = 4;
                right.column = 5;
                right.fromRow = 4;
                right.fromColumn = 5;
                right.dwellFrame = 15;
            }
            candidate.enemies_.push_back(right);

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "later dual-Helper state mismatch: playerMove="
                          << state.playerMovePhase << " playerColumn="
                          << state.playerColumn << " playerFrame="
                          << state.playerFrame << " bottom="
                          << state.bottomState << " right=" << state.rightState
                          << " targetCleared=" << state.targetCleared
                          << " hash=0x" << std::hex << actual
                          << " expected=0x" << state.hash << std::dec << '\n';
                return false;
            }
        }
        enum class PostHelperEvent {
            LeftMove,
            FirstChew,
            SafeExpiryHold,
            DownMove,
            SecondChew,
        };
        struct PostHelperState {
            PostHelperEvent event;
            int state;
            std::uint64_t hash;
        };
        // Global frames 19887-20062 follow the first Helper's removal while
        // the second Helper dwells at row 4, column 5. Thirty completed source
        // pages cover a left step, the exact seven-record chew of row 2,
        // column 2, the first safe-zone expiry on the retained phase-0 page,
        // a down step, and the exact seven-record chew of row 3, column 2.
        // Frames 19961,
        // 19973, and 19997 are torn transitions. Frames 20058-20061 are
        // uniform copies of the final record-13 page; frame 20062 retains the
        // same logical page but has five nonuniform doubled capture blocks.
        static constexpr std::array<PostHelperState, 30> PostHelperStates = {{
            {PostHelperEvent::LeftMove, 1, 0xbaa12a5e87275937ull},
            {PostHelperEvent::LeftMove, 2, 0xb599feb94ca4a9ccull},
            {PostHelperEvent::LeftMove, 3, 0xcfc1d6028a440d05ull},
            {PostHelperEvent::LeftMove, 4, 0xfc6804e5fd0fba5dull},
            {PostHelperEvent::LeftMove, 5, 0xc2d9ad7119d6fef1ull},
            {PostHelperEvent::LeftMove, 6, 0x1aff80cfed734128ull},
            {PostHelperEvent::LeftMove, 7, 0x287e576792f8ac39ull},
            {PostHelperEvent::FirstChew, 12, 0xa48f9572a7c36cedull},
            {PostHelperEvent::FirstChew, 13, 0x2177a67b3d6eafa9ull},
            {PostHelperEvent::FirstChew, 14, 0xa48f9572a7c36cedull},
            {PostHelperEvent::FirstChew, 13, 0x2177a67b3d6eafa9ull},
            {PostHelperEvent::FirstChew, 12, 0xa48f9572a7c36cedull},
            {PostHelperEvent::FirstChew, 13, 0x2177a67b3d6eafa9ull},
            {PostHelperEvent::FirstChew, 14, 0xa48f9572a7c36cedull},
            {PostHelperEvent::FirstChew, 15, 0x2177a67b3d6eafa9ull},
            {PostHelperEvent::SafeExpiryHold, 0, 0x4205f77fe623f871ull},
            {PostHelperEvent::DownMove, 1, 0x305ea179c7999301ull},
            {PostHelperEvent::DownMove, 2, 0x35818149bc8eb091ull},
            {PostHelperEvent::DownMove, 3, 0x12a06652da080f12ull},
            {PostHelperEvent::DownMove, 4, 0xe052125fe78eb41dull},
            {PostHelperEvent::DownMove, 5, 0xf525d8231cb89911ull},
            {PostHelperEvent::DownMove, 6, 0x01e3949a5e8f15c9ull},
            {PostHelperEvent::SecondChew, 12, 0x7c58f0b5238333a9ull},
            {PostHelperEvent::SecondChew, 13, 0x3647a16545236c45ull},
            {PostHelperEvent::SecondChew, 14, 0x7c58f0b5238333a9ull},
            {PostHelperEvent::SecondChew, 13, 0x3647a16545236c45ull},
            {PostHelperEvent::SecondChew, 12, 0x7c58f0b5238333a9ull},
            {PostHelperEvent::SecondChew, 13, 0x3647a16545236c45ull},
            {PostHelperEvent::SecondChew, 14, 0x7c58f0b5238333a9ull},
            {PostHelperEvent::SecondChew, 15, 0x3647a16545236c45ull},
        }};
        for (const PostHelperState& state : PostHelperStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Factors;
            candidate.mode_ = Game::Mode::Factors;
            candidate.target_ = 63;
            candidate.level_ = 10;
            candidate.playerRow_ = 2;
            candidate.playerColumn_ = 3;
            candidate.playerTerminalFrame_ = 13;
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            if (state.event == PostHelperEvent::LeftMove ||
                state.event == PostHelperEvent::FirstChew) {
                candidate.cell(2, 1).safe = true;
            }
            candidate.cell(2, 4).safe = true;
            for (const auto& cleared : {
                     std::pair{2, 3}, std::pair{2, 4},
                     std::pair{2, 5}, std::pair{3, 5},
                     std::pair{4, 0}, std::pair{4, 1},
                     std::pair{4, 2}}) {
                candidate.cell(cleared.first, cleared.second).label.clear();
                candidate.cell(cleared.first, cleared.second).eaten = true;
            }

            if (state.event == PostHelperEvent::LeftMove) {
                if (state.state >= 1 && state.state <= 5) {
                    candidate.moving_ = true;
                    candidate.moveDirection_ = 3;
                    candidate.moveFromRow_ = 2;
                    candidate.moveFromColumn_ = 3;
                    candidate.moveToRow_ = 2;
                    candidate.moveToColumn_ = 2;
                    candidate.moveTimer_ =
                        candidate.playerMoveAnimationDuration(3) -
                        static_cast<double>(state.state) /
                            OriginalSchedulerTicksPerSecond;
                } else {
                    candidate.playerColumn_ = 2;
                    candidate.playerTerminalFrame_ =
                        state.state == 6 ? 10 : 7;
                }
            } else if (state.event == PostHelperEvent::FirstChew) {
                candidate.playerColumn_ = 2;
                candidate.cell(2, 2).label.clear();
                candidate.cell(2, 2).eaten = true;
                candidate.playerTerminalFrame_ =
                    state.state == 15 ? 13 : state.state;
            } else if (state.event == PostHelperEvent::SafeExpiryHold) {
                candidate.playerColumn_ = 2;
                candidate.cell(2, 2).label.clear();
                candidate.cell(2, 2).eaten = true;
                candidate.playerTerminalFrame_ = 13;
            } else if (state.event == PostHelperEvent::DownMove) {
                candidate.playerColumn_ = 2;
                candidate.cell(2, 2).label.clear();
                candidate.cell(2, 2).eaten = true;
                if (state.state <= 4) {
                    candidate.moving_ = true;
                    candidate.moveDirection_ = 2;
                    candidate.moveFromRow_ = 2;
                    candidate.moveFromColumn_ = 2;
                    candidate.moveToRow_ = 3;
                    candidate.moveToColumn_ = 2;
                    candidate.moveTimer_ =
                        candidate.playerMoveAnimationDuration(2) -
                        static_cast<double>(state.state) /
                            OriginalSchedulerTicksPerSecond;
                } else {
                    candidate.playerRow_ = 3;
                    candidate.playerTerminalFrame_ =
                        state.state == 5 ? 8 : 7;
                }
            } else {
                candidate.playerRow_ = 3;
                candidate.playerColumn_ = 2;
                candidate.cell(2, 2).label.clear();
                candidate.cell(2, 2).eaten = true;
                candidate.cell(3, 2).label.clear();
                candidate.cell(3, 2).eaten = true;
                candidate.playerTerminalFrame_ =
                    state.state == 15 ? 13 : state.state;
            }

            Game::Enemy helper{};
            helper.type = 3;
            helper.slot = 1;
            helper.row = 4;
            helper.column = 5;
            helper.fromRow = 4;
            helper.fromColumn = 5;
            helper.direction = 2;
            helper.dwellFrame = 15;
            candidate.enemies_.push_back(helper);

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "post-Helper player state mismatch: event="
                          << static_cast<int>(state.event) << " state="
                          << state.state << " hash=0x" << std::hex << actual
                          << " expected=0x" << state.hash << std::dec << '\n';
                return false;
            }
        }
        enum class FinalBoardTailEvent {
            FirstLeft,
            Down,
            SecondLeft,
            Chew,
            Warning,
        };
        struct FinalBoardTailState {
            FinalBoardTailEvent event;
            int state;
            std::uint64_t hash;
        };
        // Global frames 20188-20527 contain the final gameplay-board tail in
        // the supplied Demo. Twenty-nine presentation-complete pages cover three
        // player moves, the exact seven-record chew of row 3/column 0, its
        // terminal record-13 hold, and the following Troggle warning. Frame
        // 20192 is a one-block nonuniform/incomplete phase-3 border repaint.
        // Frame 20288 is an incomplete terminal-to-idle player repaint, and
        // frame 20389 is a uniform horizontal splice of its adjacent complete
        // chew pages. None of these incomplete source updates is a native state.
        static constexpr std::array<FinalBoardTailState, 29>
            FinalBoardTailStates = {{
                {FinalBoardTailEvent::FirstLeft, 1, 0xf8d2505fc9af5883ull},
                {FinalBoardTailEvent::FirstLeft, 2, 0x8d67de4be0bc2094ull},
                {FinalBoardTailEvent::FirstLeft, 3, 0x99dee31553fb2311ull},
                {FinalBoardTailEvent::FirstLeft, 4, 0xd5a9fe11d11dc74bull},
                {FinalBoardTailEvent::FirstLeft, 5, 0x5fb0ac3f5eaf5adcull},
                {FinalBoardTailEvent::FirstLeft, 6, 0xd927d391ca2d7b8eull},
                {FinalBoardTailEvent::FirstLeft, 7, 0xff72d12b66633813ull},
                {FinalBoardTailEvent::Down, 1, 0x2e0f015fab52a884ull},
                {FinalBoardTailEvent::Down, 2, 0x25d2e2f155528b75ull},
                {FinalBoardTailEvent::Down, 3, 0x1cd43b9ebbb3cf52ull},
                {FinalBoardTailEvent::Down, 4, 0xc3a0c7b6a0aa8999ull},
                {FinalBoardTailEvent::Down, 5, 0x2819efc68bf4f085ull},
                {FinalBoardTailEvent::Down, 6, 0x15740a4a7d463c2dull},
                {FinalBoardTailEvent::SecondLeft, 1, 0xf4182218e992a0b6ull},
                {FinalBoardTailEvent::SecondLeft, 2, 0x2855af3ffda9b44cull},
                {FinalBoardTailEvent::SecondLeft, 3, 0x1eb6875f775f09b5ull},
                {FinalBoardTailEvent::SecondLeft, 4, 0x8b2e3717f7a8e89bull},
                {FinalBoardTailEvent::SecondLeft, 5, 0xffa2a0976ea24ca0ull},
                {FinalBoardTailEvent::SecondLeft, 6, 0xc53655cb27e6b29eull},
                {FinalBoardTailEvent::SecondLeft, 7, 0x7d2581572be7d287ull},
                {FinalBoardTailEvent::Chew, 12, 0xe22858a2a40d0f9dull},
                {FinalBoardTailEvent::Chew, 13, 0x6edfe44064be85cdull},
                {FinalBoardTailEvent::Chew, 14, 0xe22858a2a40d0f9dull},
                {FinalBoardTailEvent::Chew, 13, 0x6edfe44064be85cdull},
                {FinalBoardTailEvent::Chew, 12, 0xe22858a2a40d0f9dull},
                {FinalBoardTailEvent::Chew, 13, 0x6edfe44064be85cdull},
                {FinalBoardTailEvent::Chew, 14, 0xe22858a2a40d0f9dull},
                {FinalBoardTailEvent::Chew, 15, 0x6edfe44064be85cdull},
                {FinalBoardTailEvent::Warning, 0, 0xe5f33a065a5d946bull},
            }};
        for (const FinalBoardTailState& state : FinalBoardTailStates) {
            Game candidate;
            candidate.page_ = Game::Page::Attract;
            candidate.attractMode_ = true;
            candidate.activeBoardMode_ = Game::Mode::Factors;
            candidate.mode_ = Game::Mode::Factors;
            candidate.target_ = 63;
            candidate.level_ = 10;
            candidate.playerRow_ = 2;
            candidate.playerColumn_ = 2;
            candidate.playerTerminalFrame_ = 7;
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = candidate.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            for (const auto& cleared : {
                     std::pair{2, 2}, std::pair{2, 3},
                     std::pair{2, 4}, std::pair{2, 5},
                     std::pair{3, 2}, std::pair{3, 5},
                     std::pair{4, 0}, std::pair{4, 1},
                     std::pair{4, 2}, std::pair{4, 5}}) {
                candidate.cell(cleared.first, cleared.second).label.clear();
                candidate.cell(cleared.first, cleared.second).eaten = true;
            }

            if (state.event == FinalBoardTailEvent::FirstLeft) {
                if (state.state >= 1 && state.state <= 5) {
                    candidate.moving_ = true;
                    candidate.moveDirection_ = 3;
                    candidate.moveFromRow_ = 2;
                    candidate.moveFromColumn_ = 2;
                    candidate.moveToRow_ = 2;
                    candidate.moveToColumn_ = 1;
                    candidate.moveTimer_ =
                        candidate.playerMoveAnimationDuration(3) -
                        static_cast<double>(state.state) /
                            OriginalSchedulerTicksPerSecond;
                } else {
                    candidate.playerColumn_ = 1;
                    candidate.playerTerminalFrame_ =
                        state.state == 6 ? 10 : 7;
                }
            } else if (state.event == FinalBoardTailEvent::Down) {
                candidate.playerColumn_ = 1;
                if (state.state <= 4) {
                    candidate.moving_ = true;
                    candidate.moveDirection_ = 2;
                    candidate.moveFromRow_ = 2;
                    candidate.moveFromColumn_ = 1;
                    candidate.moveToRow_ = 3;
                    candidate.moveToColumn_ = 1;
                    candidate.moveTimer_ =
                        candidate.playerMoveAnimationDuration(2) -
                        static_cast<double>(state.state) /
                            OriginalSchedulerTicksPerSecond;
                } else {
                    candidate.playerRow_ = 3;
                    candidate.playerTerminalFrame_ =
                        state.state == 5 ? 8 : 7;
                }
            } else if (state.event == FinalBoardTailEvent::SecondLeft) {
                candidate.playerRow_ = 3;
                candidate.playerColumn_ = 1;
                if (state.state >= 1 && state.state <= 5) {
                    candidate.moving_ = true;
                    candidate.moveDirection_ = 3;
                    candidate.moveFromRow_ = 3;
                    candidate.moveFromColumn_ = 1;
                    candidate.moveToRow_ = 3;
                    candidate.moveToColumn_ = 0;
                    candidate.moveTimer_ =
                        candidate.playerMoveAnimationDuration(3) -
                        static_cast<double>(state.state) /
                            OriginalSchedulerTicksPerSecond;
                } else {
                    candidate.playerColumn_ = 0;
                    candidate.playerTerminalFrame_ =
                        state.state == 6 ? 10 : 7;
                }
            } else {
                candidate.playerRow_ = 3;
                candidate.playerColumn_ = 0;
                candidate.cell(3, 0).label.clear();
                candidate.cell(3, 0).eaten = true;
                if (state.event == FinalBoardTailEvent::Chew) {
                    candidate.playerTerminalFrame_ =
                        state.state == 15 ? 13 : state.state;
                } else {
                    candidate.playerTerminalFrame_ = 7;
                    candidate.enemyWarning_ = true;
                }
            }

            const std::uint64_t actual = fullFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "final board-tail state mismatch: event="
                          << static_cast<int>(state.event) << " state="
                          << state.state << " hash=0x" << std::hex << actual
                          << " expected=0x" << state.hash << std::dec << '\n';
                return false;
            }
        }
        struct CapturedState {
            int playerState;
            int bottomHelperState;
            int rightHelperState;
            bool playerCallbackComposite;
            std::uint64_t hash;
        };
        // Global frames 19530-19590 contain fifteen uniform source runs.
        // Run zero is the preceding dwell. Ten clean logical composites and
        // three player-owned resident-surface callbacks are exact below. The
        // intervening run between phases 1 and 2 is an exact scanline-81
        // splice of its adjacent states, so it is capture-only raster tearing
        // rather than a third callback framebuffer to present.
        static constexpr std::array<CapturedState, 13> CapturedStates = {{
            {0, 0, 1, false, 0xd4034528da613ff8ull},
            {0, 0, 2, false, 0x1fd266544dff2075ull},
            {0, 1, 3, false, 0x8eda1cee458f0401ull},
            {0, 2, 3, false, 0x4831e8442edb5d40ull},
            {0, 2, 4, false, 0xceb422873cd4a11bull},
            {1, 2, 4, true,  0xae8a73951b9b4aa3ull},
            {2, 3, 5, true,  0xdc02c1bbf91d4bb7ull},
            {3, 3, 5, true,  0xeb6c6357aa1a5774ull},
            {4, 4, 6, false, 0x48e8900a6e905a37ull},
            {5, 4, 6, false, 0x57c79d1ed939f0ffull},
            {6, 5, 6, false, 0xb40ce1bc09770f0aull},
            {6, 6, 6, false, 0x0b17da5bdc7883d7ull},
            {6, 7, 6, false, 0x1378ad0077b72d09ull},
        }};
        std::vector<std::uint64_t> observed;
        for (const CapturedState& state : CapturedStates) {
            Game game;
            game.page_ = Game::Page::Attract;
            game.attractMode_ = true;
            game.activeBoardMode_ = Game::Mode::Factors;
            game.mode_ = Game::Mode::Factors;
            game.target_ = 63;
            game.level_ = 10;
            game.playerRow_ = 1;
            game.playerColumn_ = 5;
            game.playerTerminalFrame_ = 7;
            game.moving_ = false;
            game.munching_ = false;
            if (state.playerState >= 1 && state.playerState <= 4) {
                game.moving_ = true;
                game.moveDirection_ = 2;
                game.moveFromRow_ = 1;
                game.moveFromColumn_ = 5;
                game.moveToRow_ = 2;
                game.moveToColumn_ = 5;
                const double duration = game.playerMoveAnimationDuration(2);
                game.moveTimer_ = duration -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            } else if (state.playerState >= 5) {
                game.playerRow_ = 2;
                game.playerColumn_ = 5;
                game.playerTerminalFrame_ = state.playerState == 5 ? 8 : 7;
            }
            game.enemyWarning_ = false;
            game.enemies_.clear();
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = game.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }
            game.cell(2, 1).safe = true;
            game.cell(2, 4).safe = true;
            if (state.bottomHelperState > 0) {
                game.cell(4, 0).label.clear();
                game.cell(4, 0).eaten = true;
            }
            if (state.rightHelperState > 0) {
                game.cell(2, 5).label.clear();
                game.cell(2, 5).eaten = true;
            }

            const auto helper = [&](const bool bottom, const int helperState,
                                    const int slot) {
                Game::Enemy enemy{};
                enemy.type = 3;
                enemy.slot = slot;
                enemy.direction = bottom ? 1 : 2;
                enemy.fromRow = bottom ? 4 : 2;
                enemy.fromColumn = bottom ? 0 : 5;
                enemy.row = bottom ? 4 : 3;
                enemy.column = bottom ? 1 : 5;
                const int steps = bottom ? 6 : 5;
                if (helperState == 0) {
                    enemy.row = enemy.fromRow;
                    enemy.column = enemy.fromColumn;
                    enemy.dwellFrame = bottom ? 4 : 15;
                } else if (helperState <= steps) {
                    enemy.moving = true;
                    enemy.savedCell.label = "3";
                    enemy.savedCell.eaten = false;
                    enemy.savedCellValid = true;
                    const double total =
                        game.enemyMoveAnimationDuration(enemy.direction);
                    const double interval = total / steps;
                    enemy.animationTimer =
                        total - static_cast<double>(helperState - 1) * interval;
                } else {
                    enemy.dwellFrame = bottom ? 4 : 15;
                }
                return enemy;
            };
            game.enemies_ = {
                helper(true, state.bottomHelperState, 0),
                helper(false, state.rightHelperState, 1),
            };
            Renderer callbackRenderer(GraphicsMode::Vga256);
            if (state.playerCallbackComposite) {
                callbackRenderer.replacePixels(game.capturePlayerCallbackFrame());
            }
            const std::uint64_t actual = state.playerCallbackComposite
                ? renderedFrameHash(callbackRenderer) : fullFrameHash(game);
            if (actual != state.hash ||
                (state.bottomHelperState > 0 && !game.cell(4, 0).eaten) ||
                (state.rightHelperState > 0 && !game.cell(2, 5).eaten)) {
                std::cerr << "dual-Helper captured state mismatch: player="
                          << state.playerState << " bottom="
                          << state.bottomHelperState << " right="
                          << state.rightHelperState << " hash=0x" << std::hex
                          << actual << " expected=0x" << state.hash << std::dec
                          << '\n';
                return false;
            }
            observed.push_back(actual);
        }
        std::sort(observed.begin(), observed.end());
        if (std::adjacent_find(observed.begin(), observed.end()) != observed.end()) {
            return false;
        }

        // Exercise the production scheduler path as well as the isolated
        // compositor. A right-side Helper overlaps the first downward player
        // step, while zero active Troggle jobs guarantee that no later actor
        // callback changes the logical frame on this dispatch.
        Game routed;
        routed.page_ = Game::Page::Attract;
        routed.attractMode_ = true;
        routed.activeBoardMode_ = Game::Mode::Factors;
        routed.mode_ = Game::Mode::Factors;
        routed.target_ = 63;
        routed.level_ = 10;
        routed.playerRow_ = 1;
        routed.playerColumn_ = 5;
        routed.playerTerminalFrame_ = 7;
        routed.moving_ = true;
        routed.moveDirection_ = 2;
        routed.moveFromRow_ = 1;
        routed.moveFromColumn_ = 5;
        routed.moveToRow_ = 2;
        routed.moveToColumn_ = 5;
        routed.moveTimer_ = routed.playerMoveAnimationDuration(2);
        routed.gameplayTickAccumulator_ = 0.0;
        routed.attractActionTimer_ = 10.0;
        routed.enemySlotCount_ = 0;
        Game::Enemy overlappingHelper{};
        overlappingHelper.type = 3;
        overlappingHelper.slot = 0;
        overlappingHelper.direction = 2;
        overlappingHelper.fromRow = 2;
        overlappingHelper.fromColumn = 5;
        overlappingHelper.row = 3;
        overlappingHelper.column = 5;
        overlappingHelper.moving = true;
        overlappingHelper.animationTimer =
            routed.enemyMoveAnimationDuration(2) * 2.0 / 5.0;
        routed.enemies_ = {overlappingHelper};
        routed.update(1.0 / OriginalSchedulerTicksPerSecond + 1e-12);
        if (!routed.moving_ || routed.playerMovementPhase() != 1 ||
            routed.presentationFrames_.size() != 1) {
            std::cerr << "dual-Helper player callback was not routed\n";
            return false;
        }
        const std::vector<std::uint32_t> expectedCallback =
            routed.capturePlayerCallbackFrame();
        const std::vector<std::uint32_t> cleanFrame =
            routed.capturePresentationFrame();
        if (expectedCallback == cleanFrame ||
            routed.presentationFrames_.front() != expectedCallback) {
            std::cerr << "dual-Helper routed callback pixels mismatch\n";
            return false;
        }
        return true;
    }

    static bool usesCapturedHelperBottomExitPlayerMoveFrames() {
        static constexpr std::array<std::string_view, Game::BoardCellCount> Labels = {
            "7",  "",   "",   "",   "",   "8",
            "2",  "",   "59", "12", "",   "",
            "2",  "14", "",   "",   "",   "",
            "21", "8",  "",   "2",  "48", "",
            "",   "",   "",   "21", "8",  "7",
        };
        struct CapturedState {
            int helperState; // 0=dwell, 1..5=downward exit, 6=removed
            int playerState; // 0=standing, 1..5=upward move, 6=terminal
            std::uint64_t hash;
        };
        static constexpr std::array<CapturedState, 12> CapturedStates = {{
            {0, 0, 0xe0bb6c5f518833ddull},
            {1, 0, 0x1172d382b2104a14ull},
            {2, 0, 0x1e62e00601ea5c36ull},
            {3, 0, 0x61e333825df371b4ull},
            {4, 0, 0xcfd92aca64ff0ba5ull},
            {4, 1, 0x2fc4ba91ebf3dcffull},
            {5, 1, 0xd508c19e27d44259ull},
            {5, 2, 0x8c5b9b657a1b4716ull},
            {5, 3, 0x1d5f563c12383951ull},
            {6, 4, 0xce768fdca57065c7ull},
            {6, 5, 0xcbe291fae3bf9b93ull},
            {6, 6, 0x13653c36323b3829ull},
        }};

        for (const CapturedState& state : CapturedStates) {
            Game game;
            game.page_ = Game::Page::Attract;
            game.attractMode_ = true;
            game.activeBoardMode_ = Game::Mode::Factors;
            game.mode_ = Game::Mode::Factors;
            game.target_ = 63;
            game.level_ = 10;
            game.playerRow_ = 3;
            game.playerColumn_ = 2;
            game.playerTerminalFrame_ = 7;
            game.enemyWarning_ = false;
            game.enemies_.clear();
            for (std::size_t index = 0; index < Labels.size(); ++index) {
                Game::Cell& current = game.cells_[index];
                current = {};
                current.label = Labels[index];
                current.eaten = current.label.empty();
            }

            if (state.playerState >= 1 && state.playerState <= 4) {
                game.moving_ = true;
                game.moveDirection_ = 0;
                game.moveFromRow_ = 3;
                game.moveFromColumn_ = 2;
                game.moveToRow_ = 2;
                game.moveToColumn_ = 2;
                game.moveTimer_ = game.playerMoveAnimationDuration(0) -
                    static_cast<double>(state.playerState) /
                        OriginalSchedulerTicksPerSecond;
            } else if (state.playerState == 5) {
                game.playerRow_ = 2;
                game.playerTerminalFrame_ = 2;
            } else if (state.playerState == 6) {
                game.playerRow_ = 2;
                game.playerTerminalFrame_ = 7;
            }

            if (state.helperState > 0) {
                game.cell(4, 5).label.clear();
                game.cell(4, 5).eaten = true;
            }
            if (state.helperState < 6) {
                Game::Enemy helper{};
                helper.type = 3;
                helper.slot = 1;
                helper.direction = 2;
                helper.fromRow = 4;
                helper.fromColumn = 5;
                helper.row = state.helperState == 0 ? 4 : 5;
                helper.column = 5;
                helper.savedCell.label = "7";
                helper.savedCell.eaten = false;
                helper.savedCellValid = true;
                if (state.helperState == 0) {
                    helper.dwellFrame = 15;
                } else {
                    helper.moving = true;
                    helper.exiting = true;
                    const double interval =
                        game.enemyMoveAnimationDuration(2) / 5.0;
                    const double total = game.enemyMoveAnimationDuration(2) + interval;
                    helper.animationTimer = total -
                        static_cast<double>(state.helperState - 1) * interval;
                }
                game.enemies_.push_back(helper);
            }

            Renderer candidate(GraphicsMode::Vga256);
            game.render(candidate);
            const std::uint64_t actual = renderedFrameHash(candidate);
            if (actual != state.hash) {
                std::cerr << "Helper bottom-exit/player-move state mismatch: helper="
                          << state.helperState << " player=" << state.playerState
                          << " hash=0x" << std::hex << actual << " expected=0x"
                          << state.hash << std::dec << '\n';
                return false;
            }
        }
        return true;
    }
};

namespace {

std::uint64_t regionHash(const Renderer& renderer, int left, int top, int right, int bottom) {
    std::uint64_t hash = 1469598103934665603ull;
    const auto& pixels = renderer.pixels();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            hash ^= pixels[static_cast<std::size_t>(y) * Renderer::Width + x];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

bool regionContains(const Renderer& renderer, int left, int top, int right, int bottom, std::uint32_t color) {
    const auto& pixels = renderer.pixels();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if ((pixels[static_cast<std::size_t>(y) * Renderer::Width + x] & 0xffffffu) == color) return true;
        }
    }
    return false;
}

std::uint32_t pixelColor(const Renderer& renderer, int x, int y) {
    return renderer.pixels()[static_cast<std::size_t>(y) * Renderer::Width + x] & 0xffffffu;
}

std::uint64_t imageHash(const Image& image) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint32_t pixel : image.pixels) {
        hash ^= pixel & 0xffffffu;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool usesOnlyCgaPalette(const std::vector<std::uint32_t>& pixels) {
    return std::all_of(pixels.begin(), pixels.end(), [](const std::uint32_t pixel) {
        switch (pixel & 0xffffffu) {
        case 0x000000:
        case 0x0000aa:
        case 0x55ffff:
        case 0xff55ff:
        case 0xffffff: return true;
        default: return false;
        }
    });
}

void maybeDumpCgaFrame(const Renderer& renderer, const std::string_view filename) {
    const char* directoryText = std::getenv("MUNCHERS_DUMP_CGA_DIR");
    if (!directoryText || !*directoryText) return;
    const std::filesystem::path directory(directoryText);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return;
    std::ofstream output(directory / filename, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output << "P6\n" << Renderer::Width << ' ' << Renderer::Height << "\n255\n";
    for (const std::uint32_t pixel : renderer.pixels()) {
        const char rgb[3] = {
            static_cast<char>((pixel >> 16) & 0xffu),
            static_cast<char>((pixel >> 8) & 0xffu),
            static_cast<char>(pixel & 0xffu),
        };
        output.write(rgb, sizeof(rgb));
    }
}

std::array<int, 2> colorMinimum(const Renderer& renderer,
                                int left,
                                int top,
                                int right,
                                int bottom,
                                std::uint32_t color) {
    std::array<int, 2> result{right, bottom};
    const auto& pixels = renderer.pixels();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if ((pixels[static_cast<std::size_t>(y) * Renderer::Width + x] & 0xffffffu) == color) {
                result[0] = std::min(result[0], x);
                result[1] = std::min(result[1], y);
            }
        }
    }
    return result;
}

} // namespace

int main() {
    if (std::getenv("MUNCHERS_AUDIT_FIND_NUMBER_AUTONOMOUS_FINAL_BOARD") != nullptr) {
        return GameTestAccess::diagnoseAutonomousFinalBoardInitializer() ? 0 : 214;
    }
    if (std::getenv("MUNCHERS_AUDIT_FIND_NUMBER_AUTONOMOUS_SECOND_BOARD") != nullptr) {
        return GameTestAccess::diagnoseAutonomousSecondBoardInitializer() ? 0 : 213;
    }
    if (std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_AUTONOMOUS_DIR") != nullptr) {
        return GameTestAccess::dumpAutonomousAttractSequence() ? 0 : 205;
    }
    if (std::getenv("MUNCHERS_AUDIT_DUMP_NUMBER_SMARTY_DIR") != nullptr) {
        return GameTestAccess::dumpSmartyAttractSequence() ? 0 : 203;
    }
    if (std::getenv("MUNCHERS_AUDIT_FIND_SMARTY_SEED") != nullptr) {
        Game probe;
        if (!GameTestAccess::dumpFirstSmartyAttractSeed(probe)) {
            std::cerr << "No 16-bit attract seed produced an initial Smarty slot\n";
            return 201;
        }
        return 0;
    }
    if (const char* seedText = std::getenv("MUNCHERS_AUDIT_NUMBER_ATTRACT_SEED")) {
        char* end = nullptr;
        const auto seed = static_cast<std::uint32_t>(std::strtoul(seedText, &end, 0));
        if (end == seedText || *end != '\0') {
            std::cerr << "Invalid MUNCHERS_AUDIT_NUMBER_ATTRACT_SEED\n";
            return 202;
        }
        Game probe;
        if (std::getenv("MUNCHERS_AUDIT_NUMBER_SOURCE_CFG") != nullptr) {
            GameTestAccess::applyCapturedSmartyConfig(probe);
        }
        GameTestAccess::dumpAttractSeedState(
            probe, seed,
            std::getenv("MUNCHERS_AUDIT_NUMBER_PRIMES_ONLY") != nullptr);
        return 0;
    }
    if (!GameTestAccess::usesCapturedSmartyAttractPresentationSequence()) {
        std::cerr << "Seeded Smarty attract presenter diverged from the lossless DOS capture\n";
        return 204;
    }
    if (!GameTestAccess::usesCapturedAutonomousPlayerEntryWalkSequence()) {
        std::cerr << "Seeded F585 player/late-entry callback sequence diverged from the lossless DOS capture\n";
        return 206;
    }
    if (!GameTestAccess::usesCapturedAutonomousInitialCannibalPlayerSequence()) {
        std::cerr << "Seeded F585 initial cannibal/player callback sequence diverged from the lossless DOS capture\n";
        return 211;
    }
    if (!GameTestAccess::usesCapturedAutonomousConcurrentLeftMovementSequence()) {
        std::cerr << "Seeded F585 concurrent player/Smarty callback sequence diverged from the lossless DOS capture\n";
        return 207;
    }
    if (!GameTestAccess::usesCapturedAutonomousDualEnemyCallbackSequence()) {
        std::cerr << "Seeded F585 dual-enemy callback sequence diverged from the lossless DOS capture\n";
        return 208;
    }
    if (!GameTestAccess::usesCapturedAutonomousTerminalDualEnemySequence()) {
        std::cerr << "Seeded F585 terminal/dual-enemy callback sequence diverged from the lossless DOS capture\n";
        return 209;
    }
    if (!GameTestAccess::usesCapturedAutonomousHorizontalExitHistorySequence()) {
        std::cerr << "Seeded F585 horizontal-exit history sequence diverged from the lossless DOS capture\n";
        return 210;
    }
    if (!GameTestAccess::usesCapturedAutonomousWrongAnswerRetainedSequence()) {
        std::cerr << "Seeded F585 retained wrong-answer chew sequence diverged from the lossless DOS capture\n";
        return 212;
    }
    if (!GameTestAccess::usesCapturedAutonomousSecondBoardSequence()) {
        std::cerr << "Seeded F585 Prime-board continuation diverged from the lossless DOS capture\n";
        return 214;
    }

    GameAssets resourceCheck;
    const Image* startupLogo = resourceCheck.startupLogo();
    const auto troggleArrival = resourceCheck.spriteFrame(1007, 0);
    const auto troggleWalk = resourceCheck.spriteFrame(1007, 3);
    const auto troggleEat = resourceCheck.spriteFrame(1007, 12);
    const auto muncherEat12 = resourceCheck.spriteFrame(1006, 12);
    const auto muncherEat13 = resourceCheck.spriteFrame(1006, 13);
    const auto muncherEat14 = resourceCheck.spriteFrame(1006, 14);
    const auto muncherEat15 = resourceCheck.spriteFrame(1006, 15);
    const auto lifeMuncher = resourceCheck.spriteFrame(1006, 17);
    if (!startupLogo || startupLogo->width != 320 || startupLogo->height != 55 ||
        !troggleArrival || troggleArrival->x != 12 || troggleArrival->y != 137 ||
        troggleArrival->width != 45 || troggleArrival->height != 40 ||
        !troggleWalk || troggleWalk->x != 12 || troggleWalk->y != 17 || troggleWalk->height != 30 ||
        !troggleEat || troggleEat->x != 156 || troggleEat->y != 57 || troggleEat->height != 30 ||
        !muncherEat12 || muncherEat12->sheetId != 1006 || muncherEat12->x != 132 ||
        muncherEat12->y != 137 || muncherEat12->width != 45 || muncherEat12->height != 30 ||
        !muncherEat13 || muncherEat13->sheetId != 1006 || muncherEat13->x != 108 ||
        muncherEat13->y != 17 || muncherEat13->width != 45 || muncherEat13->height != 30 ||
        !muncherEat14 || muncherEat14->sheetId != muncherEat12->sheetId ||
        muncherEat14->x != muncherEat12->x || muncherEat14->y != muncherEat12->y ||
        muncherEat14->width != muncherEat12->width ||
        muncherEat14->height != muncherEat12->height ||
        !muncherEat15 || muncherEat15->sheetId != muncherEat13->sheetId ||
        muncherEat15->x != muncherEat13->x || muncherEat15->y != muncherEat13->y ||
        muncherEat15->width != muncherEat13->width ||
        muncherEat15->height != muncherEat13->height ||
        !lifeMuncher || lifeMuncher->x != 16 || lifeMuncher->y != 140 ||
        lifeMuncher->width != 36 || lifeMuncher->height != 39) {
        std::cerr << "Gameplay BTMP records, including the duplicate chew poses, were not decoded exactly\n";
        return 1;
    }

    // Super Munchers uses an unusual 8-bit PCX variant with no 256-color
    // trailer. SM.RES/EGAT:1 remaps each stored byte into the PCX header's
    // 16-color palette; these fingerprints independently match the ripped
    // source sheets and prevent a generic black-palette decode from passing.
    GameAssets superVga(GraphicsMode::Vga256, GameAssetSet::SuperMunchers);
    const Image* superLogo = superVga.startupLogo();
    const Image* superPlayer = superVga.image(1006);
    const Image* superTransform = superVga.image(1013);
    const Image* superMission = superVga.image(2023);
    const Image* superTitle = superVga.image(6009);
    const auto superRightWide = superVga.spriteFrame(1013, 3);
    const auto superRightNarrow = superVga.spriteFrame(1013, 5);
    const auto superChew = superVga.spriteFrame(1013, 12);
    const auto superLife = superVga.spriteFrame(1013, 17);
    const BlobView superEgat = superVga.gameArchive().find("EGAT", 1);
    const bool superIndexesMapped = superPlayer &&
        std::all_of(superPlayer->indices.begin(), superPlayer->indices.end(),
                    [](const std::uint8_t index) { return index < 16; });
    if (superVga.assetSet() != GameAssetSet::SuperMunchers ||
        !superVga.gameArchive().valid() || superEgat.size != 256 ||
        !superVga.gameArchive().find("CATG", 1) ||
        !superVga.gameArchive().find("DATA", 2) ||
        !superVga.largeFont().valid() || !superVga.smallFont().valid() ||
        !superLogo || superLogo->width != 320 || superLogo->height != 55 ||
        imageHash(*superLogo) != 0x4ecb0ac1626d47b3ull ||
        !superPlayer || superPlayer->width != 197 || superPlayer->height != 166 ||
        imageHash(*superPlayer) != 0xfe05ed2509ccc477ull ||
        !superTransform || superTransform->width != 158 || superTransform->height != 188 ||
        imageHash(*superTransform) != 0x0ea0fe67e179c95aull ||
        !superMission || superMission->width != 320 || superMission->height != 200 ||
        imageHash(*superMission) != 0x0a5a9687cef64bc5ull ||
        !superTitle || superTitle->width != 320 || superTitle->height != 200 ||
        imageHash(*superTitle) != 0x74fa5b8b73056688ull || !superIndexesMapped ||
        !superRightWide || superRightWide->x != 16 || superRightWide->y != 130 ||
        superRightWide->width != 189 || superRightWide->height != 30 ||
        !superRightNarrow || superRightNarrow->x != 72 || superRightNarrow->y != 130 ||
        superRightNarrow->width != 85 || superRightNarrow->height != 30 ||
        !superChew || superChew->x != 92 || superChew->y != 158 ||
        superChew->width != 45 || superChew->height != 30 ||
        !superLife || superLife->x != 0 || superLife->y != 161 ||
        superLife->width != 28 || superLife->height != 27) {
        std::cerr << "Super embedded archive, EGAT palette bridge, fonts, or VGA sheets regressed\n";
        const auto describe = [](const char* name, const Image* image) {
            std::cerr << name << '=';
            if (!image) {
                std::cerr << "missing\n";
                return;
            }
            std::cerr << image->width << 'x' << image->height << " hash=0x"
                      << std::hex << imageHash(*image) << std::dec << '\n';
        };
        std::cerr << "archive=" << superVga.gameArchive().valid()
                  << " egat=" << superEgat.size
                  << " catg=" << static_cast<bool>(superVga.gameArchive().find("CATG", 1))
                  << " data=" << static_cast<bool>(superVga.gameArchive().find("DATA", 2))
                  << " fonts=" << superVga.largeFont().valid() << ','
                  << superVga.smallFont().valid()
                  << " mapped=" << superIndexesMapped << '\n';
        describe("logo", superLogo);
        describe("player", superPlayer);
        describe("transform", superTransform);
        describe("mission", superMission);
        describe("title", superTitle);
        return 215;
    }

    GameAssets superCga(GraphicsMode::Cga4, GameAssetSet::SuperMunchers);
    const Image* superCgaLogo = superCga.startupLogo();
    const Image* superCgaTransform = superCga.image(1013);
    const Image* superCgaMission = superCga.image(2023);
    const Image* superCgaTitle = superCga.image(6009);
    const auto superCgaRight = superCga.spriteFrame(1013, 3);
    const auto superCgaLife = superCga.spriteFrame(1013, 17);
    if (!superCgaLogo || imageHash(*superCgaLogo) != 0xa91894a7427ea9f3ull ||
        !superCgaTransform || superCgaTransform->width != 182 ||
        superCgaTransform->height != 159 ||
        imageHash(*superCgaTransform) != 0xaf69eb65fbb5bf27ull ||
        !superCgaMission || imageHash(*superCgaMission) != 0x36acd64337663c5bull ||
        !superCgaTitle || imageHash(*superCgaTitle) != 0x50ea6eb4cb749635ull ||
        !usesOnlyCgaPalette(superCgaTitle->pixels) ||
        !superCgaRight || superCgaRight->x != 132 || superCgaRight->y != 97 ||
        superCgaRight->width != 73 || superCgaRight->height != 30 ||
        !superCgaLife || superCgaLife->x != 0 || superCgaLife->y != 133 ||
        superCgaLife->width != 28 || superCgaLife->height != 26) {
        std::cerr << "Super CGA logical-ID translation or source-sheet decode regressed\n";
        return 216;
    }

    // Pin every one of Super's 336 BTMP records in each graphics mode, not
    // just the transformed-player and life-record exceptions above.  The
    // aggregate includes resolved continuation sheet, restored rectangle,
    // every raw source index in the crop, and explicit overhang sentinels.
    constexpr std::array<std::pair<std::uint32_t, int>, 19> superAnimations = {{
        {1006, 19}, {1007, 16}, {1008, 16}, {1009, 16}, {1010, 16},
        {1011, 16}, {1013, 19}, {1021, 12}, {2023, 30}, {2025, 42},
        {2027, 28}, {2029, 31}, {2031, 68}, {6003, 1}, {6005, 1},
        {6009, 1}, {6011, 2}, {6051, 1}, {6053, 1},
    }};
    const auto completeSuperBtmpHash = [&](GameAssets& assets) {
        std::uint64_t hash = 1469598103934665603ull;
        const auto append = [&hash](const std::uint32_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        for (const auto& [logicalId, count] : superAnimations) {
            append(logicalId);
            append(static_cast<std::uint32_t>(count));
            for (int frameIndex = 0; frameIndex < count; ++frameIndex) {
                const auto frame = assets.spriteFrame(logicalId, frameIndex);
                if (!frame) return std::uint64_t{};
                const Image* sheet = assets.image(frame->sheetId);
                if (!sheet) return std::uint64_t{};
                for (const std::uint32_t value : {
                        static_cast<std::uint32_t>(frameIndex), frame->sheetId,
                        static_cast<std::uint32_t>(frame->x),
                        static_cast<std::uint32_t>(frame->y),
                        static_cast<std::uint32_t>(frame->width),
                        static_cast<std::uint32_t>(frame->height)}) {
                    append(value);
                }
                for (int y = 0; y < frame->height; ++y) {
                    for (int x = 0; x < frame->width; ++x) {
                        const int sourceX = frame->x + x;
                        const int sourceY = frame->y + y;
                        if (sourceX < 0 || sourceY < 0 || sourceX >= sheet->width ||
                            sourceY >= sheet->height) {
                            append(0xffffffffu);
                            continue;
                        }
                        const std::size_t offset =
                            static_cast<std::size_t>(sourceY * sheet->width + sourceX);
                        append(sheet->sourceIndices.size() == sheet->pixels.size()
                            ? sheet->sourceIndices[offset]
                            : sheet->pixels[offset] & 0xffffffu);
                    }
                }
            }
            if (assets.spriteFrame(logicalId, count)) return std::uint64_t{};
        }
        return hash;
    };
    const std::uint64_t superVgaBtmpHash = completeSuperBtmpHash(superVga);
    const std::uint64_t superCgaBtmpHash = completeSuperBtmpHash(superCga);
    constexpr std::uint64_t ExpectedSuperVgaBtmpHash = 0x4db6707741feeb3aull;
    constexpr std::uint64_t ExpectedSuperCgaBtmpHash = 0x073320ce57b30154ull;
    if (superVgaBtmpHash != ExpectedSuperVgaBtmpHash ||
        superCgaBtmpHash != ExpectedSuperCgaBtmpHash) {
        std::cerr << "Complete Super BTMP/crop hashes VGA=0x" << std::hex
                  << superVgaBtmpHash << " CGA=0x" << superCgaBtmpHash
                  << std::dec << '\n';
        return 217;
    }

    // The CGA resources are parallel physical IDs selected by the original
    // DS:61BE device flag. These hashes were independently decoded from the
    // raw two-bit PCX scanlines using CGA high-intensity palette 1.
    GameAssets cgaResourceCheck(GraphicsMode::Cga4);
    const Image* cgaLogo = cgaResourceCheck.startupLogo();
    const Image* cgaTitle = cgaResourceCheck.image(6009);
    const Image* cgaMuncherSheet = cgaResourceCheck.image(1006);
    const auto cgaMuncherEat = cgaResourceCheck.spriteFrame(1006, 12);
    const auto cgaLifeMuncher = cgaResourceCheck.spriteFrame(1006, 17);
    const auto cgaTroggleArrival = cgaResourceCheck.spriteFrame(1007, 0);
    const auto cgaTroggleWalk = cgaResourceCheck.spriteFrame(1007, 3);
    const auto cgaTroggleEat = cgaResourceCheck.spriteFrame(1007, 12);
    const auto cgaContinuation = cgaResourceCheck.spriteFrame(2013, 22);
    if (!cgaLogo || cgaLogo->width != 320 || cgaLogo->height != 55 ||
        imageHash(*cgaLogo) != 0xa91894a7427ea9f3ull ||
        !cgaTitle || cgaTitle->width != 320 || cgaTitle->height != 200 ||
        imageHash(*cgaTitle) != 0x4446afc742becfd0ull ||
        !cgaMuncherSheet || cgaMuncherSheet->width != 213 || cgaMuncherSheet->height != 182 ||
        imageHash(*cgaMuncherSheet) != 0x3264758d9c758b1full ||
        !cgaMuncherEat || cgaMuncherEat->sheetId != 1000 ||
        cgaMuncherEat->x != 132 || cgaMuncherEat->y != 137 ||
        cgaMuncherEat->width != 45 || cgaMuncherEat->height != 30 ||
        !cgaLifeMuncher || cgaLifeMuncher->sheetId != 1000 ||
        cgaLifeMuncher->x != 16 || cgaLifeMuncher->y != 140 ||
        cgaLifeMuncher->width != 36 || cgaLifeMuncher->height != 39 ||
        !cgaTroggleArrival || cgaTroggleArrival->sheetId != 1001 ||
        cgaTroggleArrival->x != 12 || cgaTroggleArrival->y != 137 ||
        cgaTroggleArrival->width != 45 || cgaTroggleArrival->height != 40 ||
        !cgaTroggleWalk || cgaTroggleWalk->x != 12 || cgaTroggleWalk->y != 17 ||
        cgaTroggleWalk->width != 45 || cgaTroggleWalk->height != 30 ||
        !cgaTroggleEat || cgaTroggleEat->x != 156 || cgaTroggleEat->y != 57 ||
        cgaTroggleEat->width != 45 || cgaTroggleEat->height != 30 ||
        !cgaContinuation || cgaContinuation->sheetId != 3012 ||
        cgaContinuation->x != 4 || cgaContinuation->y != 0 ||
        cgaContinuation->width != 44 || cgaContinuation->height != 34) {
        std::cerr << "CGA logical resource selection, raw PCX decode, or BTMP translation regressed\n";
        return 101;
    }

    constexpr std::array<std::uint32_t, 22> cgaSheetIds = {
        1000, 1001, 1002, 1003, 1004, 1005,
        2012, 2014, 2016, 2018, 2020,
        3012, 3014, 3016, 3018, 3020,
        6000, 6002, 6004, 6008, 6050, 6052,
    };
    std::uint64_t cgaArchiveHash = 1469598103934665603ull;
    for (const std::uint32_t id : cgaSheetIds) {
        const Image* image = cgaResourceCheck.image(id);
        if (!image || !usesOnlyCgaPalette(image->pixels)) {
            std::cerr << "CGA archive sheet " << id << " failed decode or palette confinement\n";
            return 105;
        }
        for (const std::uint32_t value : {id, static_cast<std::uint32_t>(image->width),
                                         static_cast<std::uint32_t>(image->height)}) {
            cgaArchiveHash ^= value;
            cgaArchiveHash *= 1099511628211ull;
        }
        for (const std::uint32_t pixel : image->pixels) {
            cgaArchiveHash ^= pixel & 0xffffffu;
            cgaArchiveHash *= 1099511628211ull;
        }
    }
    if (cgaArchiveHash != 0x4c2863ddc49f4c01ull) {
        std::cerr << "Complete 22-sheet CGA archive hash did not match the independent raw decode\n";
        return 106;
    }

    constexpr std::array<std::pair<std::uint32_t, int>, 17> cgaAnimationCounts = {{
        {1006, 19}, {1007, 16}, {1008, 16}, {1009, 16}, {1010, 16}, {1011, 16},
        {2013, 37}, {2015, 41}, {2017, 28}, {2019, 33}, {2021, 39}, {6000, 20},
        {6002, 1}, {6004, 1}, {6008, 1}, {6050, 1}, {6052, 1},
    }};
    for (const auto& [logicalId, count] : cgaAnimationCounts) {
        for (int frame = 0; frame < count; ++frame) {
            const auto source = cgaResourceCheck.spriteFrame(logicalId, frame);
            if (!source || !cgaResourceCheck.image(source->sheetId) ||
                source->width <= 0 || source->height <= 0) {
                std::cerr << "CGA BTMP " << logicalId << " frame " << frame
                          << " did not resolve to a valid physical sheet region\n";
                return 107;
            }
        }
        if (cgaResourceCheck.spriteFrame(logicalId, count)) {
            std::cerr << "CGA BTMP " << logicalId << " exceeded its recovered frame count\n";
            return 108;
        }
    }

    Renderer cgaPrimitiveRenderer(GraphicsMode::Cga4);
    cgaPrimitiveRenderer.clear(Colors::BoardBlue);
    cgaPrimitiveRenderer.pixel(0, 0, Colors::BrightBlue);
    cgaPrimitiveRenderer.pixel(1, 0, Colors::Green);
    cgaPrimitiveRenderer.pixel(2, 0, Colors::Cyan);
    cgaPrimitiveRenderer.pixel(3, 0, Colors::Red);
    cgaPrimitiveRenderer.pixel(4, 0, Colors::Magenta);
    cgaPrimitiveRenderer.pixel(5, 0, Colors::Yellow);
    cgaPrimitiveRenderer.pixel(6, 0, Colors::White);
    cgaPrimitiveRenderer.pixel(7, 0, Colors::Gray);
    if (pixelColor(cgaPrimitiveRenderer, 0, 0) != 0x55ffff ||
        pixelColor(cgaPrimitiveRenderer, 1, 0) != 0x55ffff ||
        pixelColor(cgaPrimitiveRenderer, 2, 0) != 0x55ffff ||
        pixelColor(cgaPrimitiveRenderer, 3, 0) != 0xff55ff ||
        pixelColor(cgaPrimitiveRenderer, 4, 0) != 0xff55ff ||
        pixelColor(cgaPrimitiveRenderer, 5, 0) != 0xffffff ||
        pixelColor(cgaPrimitiveRenderer, 6, 0) != 0xffffff ||
        pixelColor(cgaPrimitiveRenderer, 7, 0) != 0x000000 ||
        pixelColor(cgaPrimitiveRenderer, 8, 0) != 0x0000aa) {
        std::cerr << "Recovered DOS 16-to-4 CGA primitive color collapse regressed\n";
        return 102;
    }

    Game cgaTitleGame(GraphicsMode::Cga4);
    Renderer cgaGameRenderer;
    cgaTitleGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-version-native.ppm");
    cgaTitleGame.keyDown(VK_SPACE);
    cgaTitleGame.character(L' ');
    cgaTitleGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-splash-native.ppm");
    cgaTitleGame.keyDown(VK_SPACE);
    cgaTitleGame.character(L' ');
    cgaTitleGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-title-native.ppm");
    if (cgaGameRenderer.graphicsMode() != GraphicsMode::Cga4 ||
        !usesOnlyCgaPalette(cgaGameRenderer.pixels()) ||
        std::find(cgaGameRenderer.pixels().begin(), cgaGameRenderer.pixels().end(), 0x55ffff) ==
            cgaGameRenderer.pixels().end() ||
        std::find(cgaGameRenderer.pixels().begin(), cgaGameRenderer.pixels().end(), 0xff55ff) ==
            cgaGameRenderer.pixels().end() ||
        std::find(cgaGameRenderer.pixels().begin(), cgaGameRenderer.pixels().end(), 0xffffff) ==
            cgaGameRenderer.pixels().end()) {
        std::cerr << "CGA title composition escaped the recovered four-color display path\n";
        return 103;
    }

    Game cgaOptionsGame(GraphicsMode::Cga4);
    cgaOptionsGame.keyDown(VK_SPACE);
    cgaOptionsGame.character(L' ');
    cgaOptionsGame.keyDown(VK_SPACE);
    cgaOptionsGame.character(L' ');
    cgaOptionsGame.character(L'4');
    cgaOptionsGame.keyDown(VK_RETURN);
    cgaOptionsGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-options-native.ppm");
    if (!usesOnlyCgaPalette(cgaGameRenderer.pixels())) {
        std::cerr << "CGA Options composition escaped the recovered four-color display path\n";
        return 107;
    }

    Game cgaBoardGame(GraphicsMode::Cga4);
    GameTestAccess::prepareMove(cgaBoardGame);
    cgaBoardGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-board-native.ppm");
    if (!usesOnlyCgaPalette(cgaGameRenderer.pixels()) ||
        pixelColor(cgaGameRenderer, 0, 40) != 0x0000aa ||
        pixelColor(cgaGameRenderer, 18, 24) != 0xff55ff ||
        pixelColor(cgaGameRenderer, 50, 184) != 0xffffff ||
        !regionContains(cgaGameRenderer, 116, 86, 164, 116, 0x55ffff)) {
        std::cerr << "CGA board did not use its exact background/grid/text/sprite palette groups\n";
        return 104;
    }

    GameTestAccess::prepareCapturedCgaFactorsBoard(cgaBoardGame);
    cgaBoardGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-gameplay-first-board-native.ppm");
    const std::uint64_t renderedCapturedCgaBoardHash =
        regionHash(cgaGameRenderer, 0, 0, 320, 200);
    constexpr std::uint64_t CapturedCgaBoardHash = 0xa83b35bb62443113ull;
    if (renderedCapturedCgaBoardHash != CapturedCgaBoardHash ||
        !usesOnlyCgaPalette(cgaGameRenderer.pixels())) {
        std::cerr << "captured CGA Factors board hash mismatch: rendered 0x"
                  << std::hex << renderedCapturedCgaBoardHash << " expected 0x"
                  << CapturedCgaBoardHash << std::dec << '\n';
        return 108;
    }

    Game cgaHallGame(GraphicsMode::Cga4);
    GameTestAccess::prepareCapturedHall(cgaHallGame);
    cgaHallGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-hall-native.ppm");
    const std::uint64_t renderedCgaHallHash = regionHash(cgaGameRenderer, 0, 0, 320, 200);
    GameTestAccess::prepareCapturedEmptyFactorsHall(cgaHallGame);
    cgaHallGame.render(cgaGameRenderer);
    maybeDumpCgaFrame(cgaGameRenderer, "number-hall-empty-factors-native.ppm");
    const std::uint64_t renderedCgaEmptyFactorsHallHash =
        regionHash(cgaGameRenderer, 0, 0, 320, 200);
    GameTestAccess::prepareCapturedHighlightedHall(cgaHallGame);
    cgaHallGame.render(cgaGameRenderer);
    const std::uint64_t renderedCgaHighlightedHallHash =
        regionHash(cgaGameRenderer, 0, 0, 320, 200);
    // Deterministic native compositions for the statically recovered CGA
    // palette branch; unlike the VGA highlighted frame below, these are not
    // hashes of a live-DOS screenshot.
    constexpr std::uint64_t NativeCgaHallHash = 0x6de5ba02b0677e9bull;
    constexpr std::uint64_t NativeCgaEmptyFactorsHallHash = 0xcc8863d9b565938eull;
    constexpr std::uint64_t NativeCgaHighlightedHallHash = 0x797c489e50013b35ull;
    if (renderedCgaHallHash != NativeCgaHallHash ||
        renderedCgaEmptyFactorsHallHash != NativeCgaEmptyFactorsHallHash ||
        renderedCgaHighlightedHallHash != NativeCgaHighlightedHallHash) {
        std::cerr << "CGA Hall composition hash mismatch: normal=0x" << std::hex
                  << renderedCgaHallHash << " empty-factors=0x"
                  << renderedCgaEmptyFactorsHallHash << " highlighted=0x"
                  << renderedCgaHighlightedHallHash
                  << std::dec << '\n';
        return 118;
    }

    const BlobView correctDro = loadEmbeddedResource(IDR_SFX_CORRECT_DRO);
    const BlobView wrongDro = loadEmbeddedResource(IDR_SFX_WRONG_DRO);
    const BlobView collisionDro = loadEmbeddedResource(IDR_SFX_TROGGLE_COLLISION_DRO);
    const BlobView boardStartDro = loadEmbeddedResource(IDR_SFX_BOARD_START_DRO);
    const BlobView levelAdvanceDro = loadEmbeddedResource(IDR_SFX_LEVEL_ADVANCE_DRO);
    const DroInfo correctDroInfo = inspectDro(correctDro.span());
    const DroInfo wrongDroInfo = inspectDro(wrongDro.span());
    const DroInfo collisionDroInfo = inspectDro(collisionDro.span());
    const DroInfo boardStartDroInfo = inspectDro(boardStartDro.span());
    const DroInfo levelAdvanceDroInfo = inspectDro(levelAdvanceDro.span());
    const OplSound decodedCorrectDro = decodeDroSound(correctDro.span());
    const OplSound decodedWrongDro = decodeDroSound(wrongDro.span());
    const OplSound decodedCollisionDro = decodeDroSound(collisionDro.span());
    if (!correctDroInfo.valid || correctDroInfo.durationMilliseconds != 260 ||
        correctDroInfo.pairCount != 112 ||
        !wrongDroInfo.valid || wrongDroInfo.durationMilliseconds != 274 ||
        wrongDroInfo.pairCount != 82 ||
        !collisionDroInfo.valid || collisionDroInfo.durationMilliseconds != 1647 ||
        collisionDroInfo.pairCount != 152 ||
        !boardStartDroInfo.valid || boardStartDroInfo.durationMilliseconds != 219 ||
        boardStartDroInfo.pairCount != 55 ||
        !levelAdvanceDroInfo.valid || levelAdvanceDroInfo.durationMilliseconds != 1112 ||
        levelAdvanceDroInfo.pairCount != 129 ||
        !decodedCorrectDro.valid ||
        decodedCorrectDro.durationMilliseconds != correctDroInfo.durationMilliseconds ||
        decodedCorrectDro.writes.size() != correctDroInfo.registerWriteCount ||
        !decodedWrongDro.valid ||
        decodedWrongDro.durationMilliseconds != wrongDroInfo.durationMilliseconds ||
        decodedWrongDro.writes.size() != wrongDroInfo.registerWriteCount ||
        !decodedCollisionDro.valid ||
        decodedCollisionDro.durationMilliseconds != collisionDroInfo.durationMilliseconds ||
        decodedCollisionDro.writes.size() != collisionDroInfo.registerWriteCount) {
        std::cerr << "Original AdLib cue captures were missing or failed structural validation\n";
        return 80;
    }
    const std::vector<std::uint8_t> renderedCorrect = renderDroToWave(correctDro.span(), 120);
    bool renderedAudioIsNonzero = false;
    for (std::size_t offset = 44; offset + 1 < renderedCorrect.size(); offset += 2) {
        if (renderedCorrect[offset] != 0 || renderedCorrect[offset + 1] != 0) {
            renderedAudioIsNonzero = true;
            break;
        }
    }
    if (renderedCorrect.size() < 1000 ||
        std::memcmp(renderedCorrect.data(), "RIFF", 4) != 0 ||
        std::memcmp(renderedCorrect.data() + 8, "WAVE", 4) != 0 ||
        !renderedAudioIsNonzero) {
        std::cerr << "Original AdLib cue did not render to a non-silent RIFF/WAVE buffer\n";
        return 81;
    }

    const ResArchive originalGameArchive(loadEmbeddedResource(IDR_NM_RES));
    const BlobView originalGSound = originalGameArchive.find("GSND", 12);
    const MeccSound decodedStop = decodeMeccGSound(originalGSound, 0);
    const MeccSound decodedCorrect = decodeMeccGSound(originalGSound, 7);
    const MeccSound decodedWrong = decodeMeccGSound(originalGSound, 8);
    const MeccSound decodedWarning = decodeMeccGSound(originalGSound, 9);
    const MeccSound decodedCollisionStart = decodeMeccGSound(originalGSound, 12);
    const MeccSound decodedCompletion = decodeMeccGSound(originalGSound, 15);
    const MeccSound decodedLongScore = decodeMeccGSound(originalGSound, 16);
    const BlobView originalSpeakerSound = originalGameArchive.find("PSND", 11);
    constexpr std::array<std::uint8_t, 8> longScoreTrackIndices = {
        180, 181, 183, 185, 186, 188, 192, 196
    };
    bool decodedEveryLongScoreTrack = true;

    // Test mode never opens WinMM, but it exercises the exact runtime owner:
    // one continuously clocked score accepts replaceable channel-0 streams,
    // preserves the loop, and releases both ownership states on stop.
    OplStreamPlayer compositePlayer;
    if (!decodedLongScore.valid ||
        !compositePlayer.startLoop(decodedLongScore.writes,
                                   decodedLongScore.durationMilliseconds) ||
        compositePlayer.bufferedMillisecondsForTest() < 120.0 ||
        compositePlayer.bufferedMillisecondsForTest() > 128.0 ||
        !compositePlayer.looping() ||
        compositePlayer.scoreWriteCount() != decodedLongScore.writes.size() ||
        !compositePlayer.playEffect(decodedCorrectDro.writes,
                                    decodedCorrectDro.durationMilliseconds) ||
        !compositePlayer.effectPlaying() || !compositePlayer.looping()) {
        std::cerr << "Single-chip score/effect owner rejected recovered OPL timelines\n";
        return 119;
    }
    const std::vector<std::int16_t> compositeSamples =
        compositePlayer.renderTestSamples(49'715);
    // Production generates the same stream through 512-sample WinMM headers.
    // Split at, immediately before, and immediately after those boundaries so
    // a discontinuity or state reset cannot hide behind the one-call hash.
    OplStreamPlayer chunkedPlayer;
    if (!chunkedPlayer.startLoop(decodedLongScore.writes,
                                 decodedLongScore.durationMilliseconds) ||
        !chunkedPlayer.playEffect(decodedCorrectDro.writes,
                                  decodedCorrectDro.durationMilliseconds)) {
        std::cerr << "Chunked single-chip owner rejected recovered timelines\n";
        return 119;
    }
    std::vector<std::int16_t> chunkedSamples;
    chunkedSamples.reserve(compositeSamples.size());
    constexpr std::array<std::size_t, 5> ChunkSizes = {511, 1, 512, 513, 257};
    std::size_t chunkIndex = 0;
    while (chunkedSamples.size() < compositeSamples.size()) {
        const std::size_t count = std::min(
            ChunkSizes[chunkIndex++ % ChunkSizes.size()],
            compositeSamples.size() - chunkedSamples.size());
        std::vector<std::int16_t> chunk = chunkedPlayer.renderTestSamples(count);
        chunkedSamples.insert(chunkedSamples.end(), chunk.begin(), chunk.end());
    }
    if (chunkedSamples != compositeSamples) {
        std::cerr << "Single-chip PCM changed across device-buffer boundaries\n";
        return 119;
    }
    std::uint64_t compositeHash = 1469598103934665603ull;
    bool compositeNonzero = false;
    for (const std::int16_t sample : compositeSamples) {
        compositeNonzero = compositeNonzero || sample != 0;
        const std::uint16_t bits = static_cast<std::uint16_t>(sample);
        compositeHash ^= static_cast<std::uint8_t>(bits);
        compositeHash *= 1099511628211ull;
        compositeHash ^= static_cast<std::uint8_t>(bits >> 8);
        compositeHash *= 1099511628211ull;
    }
    constexpr std::uint64_t ExpectedSingleChipSecondHash = 0xead26e1488df4680ull;
    if (!compositeNonzero || compositeHash != ExpectedSingleChipSecondHash) {
        std::cerr << "Single-chip score/effect sample hash mismatch: 0x" << std::hex
                  << compositeHash << std::dec << '\n';
        return 119;
    }
    compositePlayer.stopEffect();
    if (compositePlayer.effectPlaying() || !compositePlayer.looping()) {
        std::cerr << "Single-chip effect release interrupted the score\n";
        return 119;
    }
    compositePlayer.stop();
    if (compositePlayer.playing() || compositePlayer.effectPlaying()) {
        std::cerr << "Single-chip owner did not stop both timelines\n";
        return 119;
    }
    // A non-repeating cartoon score has the same independent ownership as the
    // Demo loop: releasing channel 0 must not cancel a score queued for the
    // worker's next audio buffer, while full stop must retire that score.
    OplStreamPlayer oneShotPlayer;
    if (!oneShotPlayer.startIdle() ||
        !oneShotPlayer.playScoreOnce(decodedLongScore.writes,
                                     decodedLongScore.durationMilliseconds) ||
        !oneShotPlayer.playEffect(decodedCorrectDro.writes,
                                  decodedCorrectDro.durationMilliseconds)) {
        std::cerr << "Single-chip one-shot owner rejected recovered timelines\n";
        return 119;
    }
    oneShotPlayer.stopEffect();
    const std::vector<std::int16_t> oneShotSamples =
        oneShotPlayer.renderTestSamples(49'715);
    if (oneShotPlayer.effectPlaying() || oneShotPlayer.looping() ||
        oneShotPlayer.scoreWriteCount() != decodedLongScore.writes.size() ||
        !std::any_of(oneShotSamples.begin(), oneShotSamples.end(),
                     [](const std::int16_t sample) { return sample != 0; })) {
        std::cerr << "Effect release interrupted the one-shot score\n";
        return 119;
    }
    oneShotPlayer.stop();
    if (oneShotPlayer.playing() || oneShotPlayer.effectPlaying() ||
        oneShotPlayer.scoreWriteCount() != 0) {
        std::cerr << "Full stop retained a one-shot score\n";
        return 119;
    }
    for (const std::uint8_t index : longScoreTrackIndices) {
        const MeccSound track = decodeMeccGSound(originalGSound, index);
        if (!track.valid) {
            std::cerr << "GSND score track " << static_cast<int>(index) << " failed to decode\n";
        }
        decodedEveryLongScoreTrack = decodedEveryLongScoreTrack && track.valid;
    }
    bool decodedEveryShortCue = true;
    const auto targetsNonzeroOplChannel = [](const std::uint8_t reg) {
        if ((reg >= 0xa1 && reg <= 0xa8) || (reg >= 0xb1 && reg <= 0xb8) ||
            (reg >= 0xc1 && reg <= 0xc8)) return true;
        const std::uint8_t family = reg & 0xe0u;
        if (family != 0x20u && family != 0x40u && family != 0x60u &&
            family != 0x80u && family != 0xe0u) return false;
        const std::uint8_t offset = reg & 0x1fu;
        constexpr std::array<std::uint8_t, 16> NonzeroChannelOperators = {
            1, 4, 2, 5, 8, 11, 9, 12, 10, 13, 16, 19, 17, 20, 18, 21
        };
        return std::find(NonzeroChannelOperators.begin(),
                         NonzeroChannelOperators.end(), offset) !=
               NonzeroChannelOperators.end();
    };
    bool everyShortCueUsesChannelZero = true;
    for (std::uint8_t index = 0; index <= 15; ++index) {
        const MeccSound decoded = decodeMeccGSound(originalGSound, index);
        const bool valid = decoded.valid;
        if (!valid) std::cerr << "GSND short cue " << static_cast<int>(index) << " failed to decode\n";
        decodedEveryShortCue = decodedEveryShortCue && valid;
        const auto nonzero = std::find_if(decoded.writes.begin(), decoded.writes.end(),
                                          [&](const OplWrite& write) {
                                              return targetsNonzeroOplChannel(write.reg);
                                          });
        // Streams 0/1 are driver-wide stop/control records and deliberately
        // touch other key-on registers. Audible ordinary cues 2-15 own only
        // channel 0.
        if (index >= 2 && nonzero != decoded.writes.end()) {
            std::cerr << "GSND short cue " << static_cast<int>(index)
                      << " writes nonzero channel register 0x" << std::hex
                      << static_cast<int>(nonzero->reg) << std::dec << '\n';
            everyShortCueUsesChannelZero = false;
        }
    }
    auto hasWrite = [](const MeccSound& sound, std::uint32_t milliseconds,
                       std::uint8_t reg, std::uint8_t value) {
        return std::any_of(sound.writes.begin(), sound.writes.end(), [&](const OplWrite& write) {
            return write.milliseconds == milliseconds && write.reg == reg && write.value == value;
        });
    };
    auto hasSpeakerWrite = [](const MeccSound& sound, std::uint32_t milliseconds,
                              std::uint16_t frequencyHz) {
        return std::any_of(sound.speakerWrites.begin(), sound.speakerWrites.end(),
                           [&](const MeccSound::SpeakerWrite& write) {
                               return write.milliseconds == milliseconds &&
                                      write.frequencyHz == frequencyHz;
                           });
    };
    std::array<bool, 8> decodedScoreChannels{};
    for (const OplWrite& write : decodedLongScore.writes) {
        if (write.reg >= 0xa1 && write.reg <= 0xa8) {
            decodedScoreChannels[static_cast<std::size_t>(write.reg - 0xa1)] = true;
        }
    }
    const bool decodedEveryScoreChannel = std::all_of(decodedScoreChannels.begin(),
                                                      decodedScoreChannels.end(),
                                                      [](bool decoded) { return decoded; });
    constexpr std::array<std::uint8_t, 9> StopOperatorOffsets = {
        0, 1, 2, 8, 9, 10, 16, 17, 18
    };
    bool decodedExactStop = decodedStop.valid && decodedStop.writes.size() == 36;
    for (std::size_t channel = 0;
         decodedExactStop && channel < StopOperatorOffsets.size(); ++channel) {
        const std::uint8_t op = StopOperatorOffsets[channel];
        const std::size_t base = channel * 4;
        const auto matches = [&](const std::size_t index, const std::uint8_t reg,
                                 const std::uint8_t value) {
            return decodedStop.writes[index].milliseconds == 0 &&
                   decodedStop.writes[index].reg == reg &&
                   decodedStop.writes[index].value == value;
        };
        decodedExactStop =
            matches(base + 0, static_cast<std::uint8_t>(0xc0 + channel), 0x00) &&
            matches(base + 1, static_cast<std::uint8_t>(0x43 + op), 0x3f) &&
            matches(base + 2, static_cast<std::uint8_t>(0x83 + op), 0xff) &&
            matches(base + 3, static_cast<std::uint8_t>(0xb0 + channel), 0x00);
    }
    const auto oplScheduleHash = [](const MeccSound& sound) {
        std::uint64_t hash = 1469598103934665603ull;
        const auto byte = [&](const std::uint8_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        for (int shift = 0; shift < 32; shift += 8) {
            byte(static_cast<std::uint8_t>(sound.durationMilliseconds >> shift));
        }
        const std::uint64_t count = sound.writes.size();
        for (int shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>(count >> shift));
        }
        for (const OplWrite& write : sound.writes) {
            for (int shift = 0; shift < 32; shift += 8) {
                byte(static_cast<std::uint8_t>(write.milliseconds >> shift));
            }
            byte(write.reg);
            byte(write.value);
        }
        return hash;
    };
    const std::uint64_t longScoreHash = oplScheduleHash(decodedLongScore);
    constexpr std::size_t ExpectedLongScoreWrites = 5'822;
    constexpr std::uint64_t ExpectedLongScoreHash = 0xbfa832bbb6f35d8eull;
    if (!decodedEveryShortCue || !everyShortCueUsesChannelZero ||
        !decodedExactStop || !decodedCorrect.valid || !decodedWrong.valid || !decodedWarning.valid ||
        !decodedCollisionStart.valid || !decodedCompletion.valid ||
        !decodedEveryLongScoreTrack || !decodedLongScore.valid ||
        decodedLongScore.durationMilliseconds != 105431 ||
        decodedLongScore.writes.size() != ExpectedLongScoreWrites ||
        longScoreHash != ExpectedLongScoreHash ||
        !decodedEveryScoreChannel ||
        !hasWrite(decodedCorrect, 0, 0x20, 0x60) ||
        !hasWrite(decodedCorrect, 0, 0xa0, 0x11) ||
        !hasWrite(decodedCorrect, 54, 0xa0, 0x07) ||
        !hasWrite(decodedCorrect, 109, 0xa0, 0x34) ||
        !hasWrite(decodedCorrect, 164, 0xa0, 0x47) ||
        !hasWrite(decodedWrong, 0, 0x20, 0x0a) ||
        !hasWrite(decodedWarning, 0, 0x20, 0x62) ||
        !hasWrite(decodedCollisionStart, 0, 0x20, 0x0f) ||
        !hasWrite(decodedCollisionStart, 0, 0xa0, 0x84) ||
        !hasWrite(decodedCollisionStart, 54, 0xa0, 0x9c) ||
        !hasWrite(decodedCollisionStart, 109, 0xa0, 0xce) ||
        !hasWrite(decodedCompletion, 0, 0x20, 0x21) ||
        !hasSpeakerWrite(decodedCorrect, 0, 450) ||
        !hasSpeakerWrite(decodedCorrect, 54, 440) ||
        !hasSpeakerWrite(decodedCorrect, 109, 523) ||
        !hasSpeakerWrite(decodedCorrect, 164, 554)) {
        std::cerr << "Original GSND bytecode did not decode to the measured OPL register sequence"
                  << " (score duration=" << decodedLongScore.durationMilliseconds
                  << ", writes=" << decodedLongScore.writes.size()
                  << ", hash=0x" << std::hex << longScoreHash << std::dec << ")\n";
        return 82;
    }
    // The sequence helper itself remains covered, but runtime dispatches these
    // two streams from independent wrong-chew and safe-activation call sites.
    constexpr std::array<std::uint8_t, 2> wrongSpeakerSequenceFixture{8, 4};
    const std::vector<std::uint8_t> renderedSpeaker =
        renderMeccSpeakerSoundToWave(originalGSound, 7);
    const std::vector<std::uint8_t> renderedWrongSpeaker =
        renderMeccSpeakerSequenceToWave(originalGSound, wrongSpeakerSequenceFixture);
    auto validNonSilentWave = [](const std::vector<std::uint8_t>& wave) {
        if (wave.size() < 1000 || std::memcmp(wave.data(), "RIFF", 4) != 0 ||
            std::memcmp(wave.data() + 8, "WAVE", 4) != 0) return false;
        for (std::size_t offset = 44; offset + 1 < wave.size(); offset += 2) {
            if (wave[offset] != 0 || wave[offset + 1] != 0) return true;
        }
        return false;
    };
    if (!originalSpeakerSound || !validNonSilentWave(renderedSpeaker) ||
        !validNonSilentWave(renderedWrongSpeaker)) {
        std::cerr << "Original GSND bytecode did not render to the recovered PIT speaker path\n";
        return 85;
    }
    for (std::uint8_t stream = 0; stream <= 37; ++stream) {
        if (!decodeMeccGSound(originalSpeakerSound, stream).valid) {
            std::cerr << "PSND common stream " << static_cast<int>(stream)
                      << " failed to decode\n";
            return 85;
        }
    }
    const std::array scenePatchChecks = {
        std::pair<std::uint8_t, std::uint8_t>{4, 0x21},
        std::pair<std::uint8_t, std::uint8_t>{7, 0x60},
        std::pair<std::uint8_t, std::uint8_t>{13, 0x12},
        std::pair<std::uint8_t, std::uint8_t>{14, 0x06},
        std::pair<std::uint8_t, std::uint8_t>{37, 0x20},
    };
    for (const auto& [stream, expectedOperator] : scenePatchChecks) {
        const MeccSound decoded = decodeMeccGSound(originalSpeakerSound, stream);
        if (!decoded.valid || !hasWrite(decoded, 0, 0x20, expectedOperator)) {
            std::cerr << "Recovered scene patch mapping failed for stream "
                      << static_cast<int>(stream) << '\n';
            return 85;
        }
    }

    struct SceneFixture {
        std::uint32_t scriptId;
        std::uint32_t audioBankId;
        std::vector<std::uint16_t> threads;
        int frameCount;
        int eventCount;
        int terminalTick;
        int bakedCount;
    };
    const std::array sceneFixtures = {
        SceneFixture{2012, 11, {8, 5, 7, 6, 9, 10, 11, 0, 1, 2, 3, 4}, 37, 6, 183, 6},
        SceneFixture{2014, 12, {11, 4, 5, 6, 2, 1, 3, 7, 8, 9}, 41, 12, 199, 7},
        SceneFixture{2016, 13, {7, 9, 10, 12, 4, 0, 5, 1, 6}, 28, 6, 223, 7},
        SceneFixture{2018, 14, {10, 9, 4, 5, 6, 0, 1, 3, 2}, 33, 9, 194, 68},
        SceneFixture{2020, 15, {8, 7, 1, 2, 4, 3, 0}, 39, 10, 186, 43},
    };
    for (const SceneFixture& fixture : sceneFixtures) {
        const BlobView script = originalGameArchive.find("SCPT", fixture.scriptId);
        SceneScript scene;
        if (!scene.load(script, fixture.threads)) {
            std::cerr << "SCPT " << fixture.scriptId << " failed structural load\n";
            return 84;
        }
        int ticks = 0;
        std::vector<std::uint16_t> decodedEvents;
        const BlobView adli = originalGameArchive.find("ADLI", fixture.audioBankId);
        const BlobView psnd = originalGameArchive.find("PSND", 11);
        while (scene.valid() && !scene.finished() && ticks < 20000) {
            scene.tick();
            ++ticks;
            for (const std::uint16_t event : scene.callbackEvents()) {
                if (std::find(decodedEvents.begin(), decodedEvents.end(), event) == decodedEvents.end()) {
                    const std::uint16_t stream = event + 3;
                    if (stream > 0xff ||
                        !decodeMeccGSound(adli, static_cast<std::uint8_t>(stream)).valid ||
                        !decodeMeccGSound(psnd, static_cast<std::uint8_t>(stream)).valid) {
                        std::cerr << "ADLI/PSND scene event " << event << " (stream " << stream
                                  << ") failed to decode\n";
                        return 84;
                    }
                    decodedEvents.push_back(event);
                }
            }
            for (const SceneActor& actor : scene.actors()) {
                if (actor.frame < 0 || actor.frame >= fixture.frameCount) {
                    std::cerr << "SCPT " << fixture.scriptId << " selected invalid frame "
                              << actor.frame << "\n";
                    return 84;
                }
            }
            for (const SceneActor& actor : scene.bakedActors()) {
                if (actor.frame < 0 || actor.frame >= fixture.frameCount ||
                    !actor.visible || actor.ended) {
                    std::cerr << "SCPT " << fixture.scriptId
                              << " produced an invalid baked actor state\n";
                    return 84;
                }
            }
        }
        if (!scene.valid() || !scene.finished() || scene.signal() != 99 || ticks >= 20000 ||
            decodedEvents.size() != static_cast<std::size_t>(fixture.eventCount) ||
            ticks != fixture.terminalTick ||
            scene.bakedActors().size() != static_cast<std::size_t>(fixture.bakedCount)) {
            std::cerr << "SCPT " << fixture.scriptId << " did not reach its recovered signal-99 end"
                      << " state (ticks=" << ticks << ", baked="
                      << scene.bakedActors().size() << ")\n";
            return 84;
        }
    }

    // Small independent SCPT fixture for the exact movement and detach
    // boundaries. A two-pixel move over three ticks rounds from signed 16.16
    // to (1,1,2), not the former whole-path truncation (0,1,2). Opcode 0x0e
    // must retain that placement while the same thread reuses its live actor.
    const std::array<std::uint8_t, 50> movementFixture = {
        0x01, 0x00,             // one thread
        0x06, 0x00, 0x00, 0x00, // thread offset
        0x04, 0x04, 0x07, 0x00, // frame 7
        0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // absolute (0,0)
        0x02, 0x09,             // show
        0x08, 0x02, 0x03, 0x00, 0x02, 0x00, 0xfe, 0xff, // relative +2,-2 / 3
        0x02, 0x0e,             // bake/detach
        0x04, 0x04, 0x08, 0x00, // frame 8
        0x08, 0x03, 0x00, 0x00, 0x0a, 0x00, 0x0a, 0x00, // absolute (10,10)
        0x02, 0x09,             // show
        0x04, 0x01, 0x01, 0x00, // wait 1
        0x02, 0xff,             // end
    };
    SceneScript movementScene;
    constexpr std::array<std::uint16_t, 1> movementThread{0};
    if (!movementScene.load({movementFixture.data(), movementFixture.size()}, movementThread)) {
        std::cerr << "Synthetic SCPT movement fixture did not load\n";
        return 84;
    }
    movementScene.tick();
    if (movementScene.actors()[0].x != 1 || movementScene.actors()[0].y != -1) return 84;
    movementScene.tick();
    if (movementScene.actors()[0].x != 1 || movementScene.actors()[0].y != -1) return 84;
    movementScene.tick();
    if (movementScene.actors()[0].x != 2 || movementScene.actors()[0].y != -2) return 84;
    movementScene.tick();
    if (movementScene.bakedActors().size() != 1 ||
        movementScene.bakedActors()[0].frame != 7 ||
        movementScene.bakedActors()[0].x != 2 || movementScene.bakedActors()[0].y != -2 ||
        !movementScene.actors()[0].visible || movementScene.actors()[0].frame != 8 ||
        movementScene.actors()[0].x != 10 || movementScene.actors()[0].y != 10) return 84;
    movementScene.tick();
    if (!movementScene.finished() || !movementScene.actors()[0].ended ||
        movementScene.paintEvents().size() != 1 ||
        movementScene.paintEvents()[0].dirty) return 84;

    Game game;
    GameTestAccess::seed(game, 0x47414d45u);
    Renderer renderer;
    Game prngGame;
    if (!GameTestAccess::usesOriginalPrng(prngGame)) {
        std::cerr << "Borland LCG seed/output/modulo behavior regressed\n";
        return 89;
    }
    Game reserveHudGame;
    GameTestAccess::prepareCorrectMultiple(reserveHudGame);
    GameTestAccess::setLives(reserveHudGame, 4);
    reserveHudGame.render(renderer);
    const std::uint64_t threeReserveHash = regionHash(renderer, 140, 177, 320, 200);
    GameTestAccess::setLives(reserveHudGame, 5);
    reserveHudGame.render(renderer);
    const std::uint64_t bonusReserveHash = regionHash(renderer, 140, 177, 320, 200);
    GameTestAccess::setLives(reserveHudGame, 3);
    reserveHudGame.render(renderer);
    if (threeReserveHash != bonusReserveHash ||
        threeReserveHash == regionHash(renderer, 140, 177, 320, 200)) {
        std::cerr << "Reserve HUD did not preserve the original three-icon display cap\n";
        return 83;
    }
    constexpr std::uint64_t capturedVersionHash = 0xf69b77d755a716edull;
    constexpr std::uint64_t capturedSplashHash = 0x5af513209bd9c79dull;
    constexpr std::uint64_t capturedTitleHash = 0xedf7e5d8c59eeac7ull;
    constexpr std::uint64_t capturedFirstAttractHash = 0x8785f4ad483807bfull;
    constexpr std::uint64_t capturedInstructionsQuestionHash = 0x070b08aa80400c33ull;
    constexpr std::uint64_t capturedReplayQuestionHash = 0x30b06f51fc871445ull;
    constexpr std::uint64_t capturedModeSelectHash = 0x27cb0924b38428b7ull;
    constexpr std::uint64_t capturedFilteredModeSelectHash = 0x205532588d4bcb79ull;
    constexpr std::uint64_t capturedHallSelectHash = 0x674032c7d6be986aull;
    constexpr std::uint64_t capturedMultiplesHallHash = 0x2b40f6fa572336cbull;
    constexpr std::uint64_t capturedAttractMultiplesHallHash = 0x0dc6649d785d392aull;
    constexpr std::uint64_t capturedOptionsHash = 0xe7612f59fa5211cfull;
    constexpr std::uint64_t capturedDifficultyHash = 0x0bd735be52dd3ca8ull;
    constexpr std::uint64_t capturedContentHash = 0x169a46b84d12a5e3ull;
    constexpr std::uint64_t capturedContentRangeHash = 0xa0df55e9646a99aeull;
    constexpr std::uint64_t capturedContentOtherNumberHash = 0x0d33aa1cb90968c9ull;
    constexpr std::uint64_t capturedContentOperationsHash = 0x58b4fe99f798ec2full;
    constexpr std::uint64_t capturedContentOperationsInequalityHash = 0x03ba3a9c6547610bull;
    constexpr std::array<std::uint64_t, 2> capturedContentHelpHashes = {
        0xbf36086cd8f5fad2ull, 0x7723c5cdda1c1b0eull};
    constexpr std::array<std::uint64_t, 4> capturedContentValidationHashes = {
        0xdce694492c4d2a4cull, 0xd3e815e6978fc6a3ull,
        0x1d6f1e85a0058c5eull, 0xf0901571e0690f47ull};
    constexpr std::uint64_t capturedEraseHallHash = 0x371a2a9753f7671bull;
    constexpr std::uint64_t capturedEraseEntriesHash = 0xed6c5bc858aa9619ull;
    constexpr std::uint64_t capturedEraseAllConfirmHash = 0x2c2ae6525f22f666ull;
    constexpr std::uint64_t capturedEraseEntryDeletedHash = 0xd0996fa8c2911c8dull;
    constexpr std::uint64_t capturedPasswordHash = 0x0e0d28b7d3e121749ull;
    constexpr std::uint64_t capturedTroggleWarningRegionHash = 0x9759fc235f21fe55ull;
    constexpr std::uint64_t capturedQuitConfirmRegionHash = 0x404796a0cc3e470eull;
    constexpr std::uint64_t capturedTimeOutRegionHash = 0xeea4becc2f415c31ull;
    constexpr std::array<std::uint64_t, 6> capturedInformationHashes = {
        0x1e237e9a063186eeull,
        0x5902c6f1bfdae8f7ull,
        0x1e89865236d9e41cull,
        0x1e5e1dfdea013861ull,
        0x1813e52ab947635bull,
        0xa91baa5980c1e565ull,
    };

    game.render(renderer);
    if (!GameTestAccess::isStartupVersion(game) ||
        regionHash(renderer, 0, 0, 320, 200) != capturedVersionHash ||
        pixelColor(renderer, 0, 0) != Colors::White ||
        !regionContains(renderer, 51, 76, 105, 129, 0xb6ae00) ||
        pixelColor(renderer, 128, 100) != Colors::Black) {
        std::cerr << "Startup did not render the captured key-gated MECC/version screen\n";
        return 18;
    }
    constexpr double originalStartupClock =
        1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);
    Game startupTimeout(GraphicsMode::Vga256);
    startupTimeout.update(299.0 / originalStartupClock);
    if (!GameTestAccess::isStartupVersion(startupTimeout)) {
        std::cerr << "Version screen timed out before its 300-tick boundary\n";
        return 19;
    }
    startupTimeout.update(1.0 / originalStartupClock);
    if (!GameTestAccess::isStartupSplash(startupTimeout)) {
        std::cerr << "Version screen did not time out at 300 ticks\n";
        return 19;
    }
    startupTimeout.update(120.0);
    if (!GameTestAccess::isStartupSplash(startupTimeout)) {
        std::cerr << "Startup timeout leaked through the separately gated splash\n";
        return 19;
    }
    // A physical Enter also produces WM_CHAR '\r'. That trailing half belongs
    // to the first gate and must not skip the newly exposed splash.
    game.keyDown(VK_RETURN);
    game.render(renderer);
    if (!GameTestAccess::isStartupSplash(game) ||
        regionHash(renderer, 0, 0, 320, 200) != capturedSplashHash ||
        pixelColor(renderer, 0, 0) != 0xfbffdb ||
        !regionContains(renderer, 0, 0, 320, 180, 0xe7db00)) {
        std::cerr << "Startup did not render the captured key-gated title splash\n";
        return 20;
    }
    game.update(120.0);
    if (!GameTestAccess::isStartupSplash(game)) {
        std::cerr << "Title splash did not remain key-gated\n";
        return 21;
    }
    game.character(L'\r');
    if (!GameTestAccess::isStartupSplash(game)) {
        std::cerr << "Enter's translated character skipped the title splash\n";
        return 106;
    }
    game.keyDown('4');
    if (!GameTestAccess::isStartupSplash(game)) {
        std::cerr << "Printable startup key did not defer to its translated character\n";
        return 107;
    }
    game.character(L'4');
    game.render(renderer);
    if (!GameTestAccess::isTitle(game) ||
        regionHash(renderer, 0, 0, 320, 200) != capturedTitleHash) {
        std::cerr << "Second startup key did not open the Muncher menu\n";
        return 22;
    }

    Game attractGame;
    // The AVI was recorded from a DOS launch whose low-16-bit time seed was
    // 62853 (2026-08-15 01:13:41 EDT). This must now reach the frame through
    // the ordinary demo initializer; there is no runtime board fixture.
    GameTestAccess::seed(attractGame, 62853u);
    attractGame.keyDown(VK_RETURN);
    attractGame.keyDown(VK_RETURN);
    attractGame.update(29.9);
    if (!GameTestAccess::isTitle(attractGame)) {
        std::cerr << "Attract mode started before the measured menu timeout\n";
        return 23;
    }
    attractGame.update(0.11);
    if (!GameTestAccess::isAttract(attractGame) ||
        !GameTestAccess::reproducesCapturedFirstAttractBoard(attractGame)) {
        std::cerr << "Menu idle did not start the captured first demo board at 30 seconds\n";
        return 24;
    }
    if (!GameTestAccess::exercisesRecoveredAttractAudio(attractGame)) {
        std::cerr << "Attract mode did not loop GSND 16 independently of channel-0 effects\n";
        return 85;
    }
    attractGame.render(renderer);
    const auto renderedFirstAttractHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedFirstAttractHash != capturedFirstAttractHash ||
        !regionContains(renderer, 0, 0, 45, 20, Colors::White) ||
        regionContains(renderer, 0, 180, 320, 200, 0xe7db00)) {
        std::cerr << "Attract board did not match the captured first demo frame: rendered 0x"
                  << std::hex << renderedFirstAttractHash << ", expected 0x"
                  << capturedFirstAttractHash << std::dec << '\n';
        return 25;
    }
    attractGame.keyDown(VK_SPACE);
    if (!GameTestAccess::isAttract(attractGame)) {
        std::cerr << "Demo exited before the translated Space event was consumed\n";
        return 108;
    }
    attractGame.character(L' ');
    if (!GameTestAccess::isTitle(attractGame) || !GameTestAccess::audioStopped(attractGame)) {
        std::cerr << "A key did not return attract mode to the Muncher menu and stop demo audio\n";
        return 26;
    }
    Game attractLifecycleGame;
    if (!GameTestAccess::exercisesAttractInterstitialLifecycle(attractLifecycleGame)) {
        std::cerr << "Attract feedback/Hall/logo/random-board lifecycle regressed\n";
        return 87;
    }
    Game attractBoardPaintGame;
    if (!GameTestAccess::rendersCapturedAttractBoardPaint(attractBoardPaintGame)) {
        std::cerr << "Attract logo-to-board torn painter frames regressed\n";
        return 96;
    }
    Game attractControllerGame;
    if (!GameTestAccess::exercisesRecoveredAttractController(attractControllerGame)) {
        std::cerr << "Recovered attract action controller or variable scheduler cadence regressed\n";
        return 88;
    }
    Game capturedAttractReplayGame;
    if (!GameTestAccess::replaysCapturedAttractThroughThirdBoard(capturedAttractReplayGame)) {
        std::cerr << "Captured attract PRNG replay diverged before demo board 3\n";
        return 89;
    }
    constexpr std::uint64_t capturedThirdAttractHash = 0x9816424ec49822b7ull;
    capturedAttractReplayGame.render(renderer);
    const std::uint64_t renderedThirdAttractHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedThirdAttractHash != capturedThirdAttractHash) {
        std::cerr << "Captured demo board 3 did not match its preserved full frame: rendered=0x"
                  << std::hex << renderedThirdAttractHash << " expected=0x"
                  << capturedThirdAttractHash << std::dec << '\n';
        return 90;
    }
    Game capturedRestartReplayGame;
    if (!GameTestAccess::replaysCapturedAttractThroughThirdBoard(
            capturedRestartReplayGame) ||
        !GameTestAccess::replaysCapturedRestartedFourthBoard(
            capturedRestartReplayGame)) {
        std::cerr << "Captured external Demo restart/board 4 replay diverged\n";
        return 230;
    }
    if (!GameTestAccess::replaysCapturedThirdBoardThroughFifthCapture(
            capturedAttractReplayGame)) {
        std::cerr << "Captured demo replay diverged between board 3 and board 5\n";
        return 91;
    }

    Game hallSelectGame;
    Game hallNavigationGame;
    if (!GameTestAccess::usesRecoveredHallNavigation(hallNavigationGame)) {
        std::cerr << "Recovered Hall selector/browser/post-game navigation regressed\n";
        return 95;
    }
    if (!GameTestAccess::usesRecoveredReplayPaletteCarryover()) {
        std::cerr << "Recovered Hall-to-replay palette carry-over regressed\n";
        return 121;
    }
    if (!GameTestAccess::usesRecoveredUserBoardPresenter()) {
        std::cerr << "Recovered ordinary user board presenter regressed\n";
        return 122;
    }
    if (!GameTestAccess::usesRecoveredCgaCartoonPresenter()) {
        std::cerr << "Recovered CGA cartoon presenter regressed\n";
        return 155;
    }
    hallSelectGame.keyDown(VK_RETURN);
    hallSelectGame.keyDown(VK_RETURN);
    hallSelectGame.keyDown(VK_DOWN);
    hallSelectGame.keyDown(VK_RETURN);
    hallSelectGame.render(renderer);
    if (regionHash(renderer, 0, 0, 320, 200) != capturedHallSelectHash) {
        std::cerr << "Hall of Fame selection did not match the captured original frame\n";
        return 29;
    }
    GameTestAccess::prepareCapturedHall(hallSelectGame);
    hallSelectGame.render(renderer);
    const auto renderedHallHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedHallHash != capturedMultiplesHallHash) {
        std::cerr << "Multiples Hall of Fame did not match the captured original frame: rendered 0x"
                  << std::hex << renderedHallHash << ", expected 0x"
                  << capturedMultiplesHallHash << std::dec << '\n';
        return 30;
    }
    Game hallPointerGame;
    GameTestAccess::prepareCapturedHall(hallPointerGame);
    hallPointerGame.pointerButton(0, 0, true);
    if (!GameTestAccess::isHall(hallPointerGame)) {
        std::cerr << "Hall accepted a right release rejected by pAcceptKey\n";
        return 126;
    }
    hallPointerGame.pointerButton(0, 0, false);
    if (!GameTestAccess::isTitle(hallPointerGame)) {
        std::cerr << "Hall did not map a left release to Space\n";
        return 126;
    }
    GameTestAccess::prepareCapturedAttractHall(hallSelectGame);
    hallSelectGame.render(renderer);
    const auto renderedAttractHallHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedAttractHallHash != capturedAttractMultiplesHallHash) {
        std::cerr << "Attract Hall interstitial did not match the lossless original frame: rendered 0x"
                  << std::hex << renderedAttractHallHash << ", expected 0x"
                  << capturedAttractMultiplesHallHash << std::dec << '\n';
        return 86;
    }
    GameTestAccess::prepareCapturedPostGameHall(hallSelectGame);
    hallSelectGame.render(renderer);
    const auto renderedPostGameHallHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedPostGameHallHash != capturedMultiplesHallHash) {
        std::cerr << "Non-demo post-game Hall did not use the original Space-bar footer: rendered 0x"
                  << std::hex << renderedPostGameHallHash << ", expected 0x"
                  << capturedMultiplesHallHash << std::dec << '\n';
        return 96;
    }
    GameTestAccess::prepareCapturedHighlightedHall(hallSelectGame);
    hallSelectGame.render(renderer);
    const auto renderedHighlightedHallHash = regionHash(renderer, 0, 0, 320, 200);
    constexpr std::uint64_t capturedHighlightedHallHash = 0xeb488a361b66de0aull;
    if (renderedHighlightedHallHash != capturedHighlightedHallHash) {
        std::cerr << "Post-admission Hall did not match the captured highlighted frame: rendered 0x"
                  << std::hex << renderedHighlightedHallHash << ", expected 0x"
                  << capturedHighlightedHallHash << std::dec << '\n';
        return 117;
    }
    GameTestAccess::prepareCapturedFullHall(hallSelectGame);
    hallSelectGame.render(renderer);
    const auto renderedFullHallHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedFullHallHash != 0xcf51951674ce4a4cull) {
        std::cerr << "Ten-entry Inequality Hall did not match the captured original frame: rendered 0x"
                  << std::hex << renderedFullHallHash << std::dec << '\n';
        return 76;
    }

    Game optionsGame;
    optionsGame.keyDown(VK_RETURN);
    optionsGame.keyDown(VK_RETURN);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_RETURN);
    optionsGame.render(renderer);
    const auto renderedOptionsHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedOptionsHash != capturedOptionsHash) {
        std::cerr << "Options menu did not match the captured original frame: rendered 0x"
                  << std::hex << renderedOptionsHash << ", expected 0x"
                  << capturedOptionsHash << std::dec << '\n';
        return 31;
    }
    optionsGame.keyDown(VK_RETURN);
    optionsGame.render(renderer);
    const auto renderedDifficultyHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedDifficultyHash != capturedDifficultyHash) {
        std::cerr << "Difficulty menu did not match the captured original frame: rendered 0x"
                  << std::hex << renderedDifficultyHash << ", expected 0x"
                  << capturedDifficultyHash << std::dec << '\n';
        return 32;
    }
    optionsGame.keyDown(VK_ESCAPE);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_RETURN);
    optionsGame.render(renderer);
    const auto renderedContentHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedContentHash != capturedContentHash) {
        std::cerr << "Content menu did not match the captured original frame: rendered 0x"
                  << std::hex << renderedContentHash << ", expected 0x"
                  << capturedContentHash << std::dec << '\n';
        return 33;
    }
    Game contentDialogGame;
    GameTestAccess::prepareContentRange(contentDialogGame);
    contentDialogGame.render(renderer);
    const auto renderedContentRangeHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedContentRangeHash != capturedContentRangeHash) {
        std::cerr << "Range editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedContentRangeHash << ", expected 0x"
                  << capturedContentRangeHash << std::dec << '\n';
        return 47;
    }
    GameTestAccess::prepareContentOtherNumber(contentDialogGame);
    contentDialogGame.render(renderer);
    const auto renderedContentOtherNumberHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedContentOtherNumberHash != capturedContentOtherNumberHash) {
        std::cerr << "Multiples Other editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedContentOtherNumberHash << ", expected 0x"
                  << capturedContentOtherNumberHash << std::dec << '\n';
        return 48;
    }
    GameTestAccess::prepareContentOperations(contentDialogGame);
    contentDialogGame.render(renderer);
    const auto renderedContentOperationsHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedContentOperationsHash != capturedContentOperationsHash) {
        std::cerr << "Operation editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedContentOperationsHash << ", expected 0x"
                  << capturedContentOperationsHash << std::dec << '\n';
        return 49;
    }
    GameTestAccess::prepareContentOperationsInequality(contentDialogGame);
    contentDialogGame.render(renderer);
    const auto renderedContentOperationsInequalityHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedContentOperationsInequalityHash != capturedContentOperationsInequalityHash) {
        std::cerr << "Inequality operation editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedContentOperationsInequalityHash << ", expected 0x"
                  << capturedContentOperationsInequalityHash << std::dec << '\n';
        return 51;
    }
    GameTestAccess::prepareContentHelp(contentDialogGame);
    // 0x150E1 calls the exact DS:0864 wrapper on both help pages. Arrows and
    // right release remain blocked; left release is rewritten to Space.
    for (const UINT ignored : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN}) {
        contentDialogGame.keyDown(ignored);
    }
    contentDialogGame.pointerButton(0, 0, true);
    if (!GameTestAccess::isContentHelpPage(contentDialogGame, 0)) return 58;
    contentDialogGame.pointerButton(0, 0, false);
    if (!GameTestAccess::isContentHelpPage(contentDialogGame, 1)) return 59;
    GameTestAccess::prepareContentHelp(contentDialogGame);
    for (int page = 0; page < static_cast<int>(capturedContentHelpHashes.size()); ++page) {
        contentDialogGame.render(renderer);
        const auto renderedContentHelpHash = regionHash(renderer, 0, 0, 320, 200);
        if (!GameTestAccess::isContentHelpPage(contentDialogGame, page) ||
            renderedContentHelpHash != capturedContentHelpHashes[static_cast<std::size_t>(page)]) {
            std::cerr << "Set Content Help page " << page + 1
                      << " did not match the captured original frame: rendered 0x"
                      << std::hex << renderedContentHelpHash << ", expected 0x"
                      << capturedContentHelpHashes[static_cast<std::size_t>(page)] << std::dec << '\n';
            return 58 + page;
        }
        contentDialogGame.keyDown(VK_SPACE);
    }
    const std::array validationPreparers = {
        &GameTestAccess::prepareNoGamesValidation,
        +[](Game& target) { GameTestAccess::prepareRangeValidation(target, true); },
        +[](Game& target) { GameTestAccess::prepareRangeValidation(target, false); },
        &GameTestAccess::prepareNoOperationsValidation,
    };
    for (std::size_t validation = 0; validation < validationPreparers.size(); ++validation) {
        validationPreparers[validation](contentDialogGame);
        contentDialogGame.render(renderer);
        const auto renderedValidationHash = regionHash(renderer, 0, 0, 320, 200);
        if (renderedValidationHash != capturedContentValidationHashes[validation]) {
            std::cerr << "Set Content validation frame " << validation + 1
                      << " did not match the captured original: rendered 0x" << std::hex
                      << renderedValidationHash << ", expected 0x"
                      << capturedContentValidationHashes[validation] << std::dec << '\n';
            return 60 + static_cast<int>(validation);
        }
        contentDialogGame.keyDown(VK_SPACE);
    }
    Game contentEditorGame;
    const int contentEditorFailure = GameTestAccess::contentEditorFailure(contentEditorGame);
    if (contentEditorFailure != 0) {
        std::cerr << "Set Content did not commit/cancel and drive the four configured generators (stage "
                  << contentEditorFailure << ")\n";
        return 50;
    }
    Game numericEditorGame;
    if (!GameTestAccess::usesRecoveredNumericEditorTransactions(numericEditorGame)) {
        std::cerr << "Set Content numeric editor transaction, retry, digit, or literal-bound behavior regressed\n";
        return 106;
    }
    Game contentValidationGame;
    if (!GameTestAccess::exercisesContentValidation(contentValidationGame)) {
        std::cerr << "Set Content validation did not gate empty games, ranges, and operations\n";
        return 64;
    }
    optionsGame.keyDown(VK_ESCAPE);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_RETURN);
    optionsGame.render(renderer);
    const auto renderedEraseHallHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedEraseHallHash != capturedEraseHallHash) {
        std::cerr << "Erase Hall menu did not match the captured original frame: rendered 0x"
                  << std::hex << renderedEraseHallHash << ", expected 0x"
                  << capturedEraseHallHash << std::dec << '\n';
        return 34;
    }
    Game eraseEntryGame;
    GameTestAccess::prepareEraseEntries(eraseEntryGame);
    eraseEntryGame.render(renderer);
    const auto renderedEraseEntriesHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedEraseEntriesHash != capturedEraseEntriesHash) {
        std::cerr << "Erase Hall entry editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedEraseEntriesHash << ", expected 0x"
                  << capturedEraseEntriesHash << std::dec << '\n';
        return 68;
    }
    eraseEntryGame.keyDown(VK_SPACE);
    eraseEntryGame.render(renderer);
    const auto renderedEraseEntryDeletedHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedEraseEntryDeletedHash != capturedEraseEntryDeletedHash) {
        std::cerr << "Empty transactional erase editor did not match the captured original frame: rendered 0x"
                  << std::hex << renderedEraseEntryDeletedHash << ", expected 0x"
                  << capturedEraseEntryDeletedHash << std::dec << '\n';
        return 69;
    }
    Game eraseAllGame;
    GameTestAccess::prepareEraseAllConfirm(eraseAllGame);
    eraseAllGame.render(renderer);
    const auto renderedEraseAllConfirmHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedEraseAllConfirmHash != capturedEraseAllConfirmHash) {
        std::cerr << "Erase-all confirmation did not match the captured original frame: rendered 0x"
                  << std::hex << renderedEraseAllConfirmHash << ", expected 0x"
                  << capturedEraseAllConfirmHash << std::dec << '\n';
        return 70;
    }
    Game eraseBehaviorGame;
    if (!GameTestAccess::exercisesEraseEditing(eraseBehaviorGame)) {
        std::cerr << "Hall erase editor did not preserve Escape rollback, commit, and default-No erase-all routing\n";
        return 71;
    }
    Game initiallyEmptyEraseGame;
    std::uint64_t initiallyEmptyEraseHash = 0;
    if (!GameTestAccess::usesRecoveredInitiallyEmptyEraseRoute(
            initiallyEmptyEraseGame, initiallyEmptyEraseHash)) {
        std::cerr << "Initially-empty Hall erase route or character consumption regressed\n";
        return 103;
    }
    constexpr std::uint64_t StaticInitiallyEmptyEraseHash = 0x2e472006c43a1a62ull;
    if (initiallyEmptyEraseHash != StaticInitiallyEmptyEraseHash) {
        std::cerr << "Initially-empty Hall erase static frame hash=0x" << std::hex
                  << initiallyEmptyEraseHash << std::dec << '\n';
        return 104;
    }
    optionsGame.keyDown(VK_ESCAPE);
    optionsGame.keyDown(VK_DOWN);
    optionsGame.keyDown(VK_RETURN);
    optionsGame.render(renderer);
    const auto renderedPasswordHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedPasswordHash != capturedPasswordHash) {
        std::cerr << "Password menu did not match the captured original frame: rendered 0x"
                  << std::hex << renderedPasswordHash << ", expected 0x"
                  << capturedPasswordHash << std::dec << '\n';
        return 35;
    }

    Game informationGame;
    GameTestAccess::prepareInformation(informationGame);
    Game informationPointerGame;
    GameTestAccess::prepareInformation(informationPointerGame);
    informationPointerGame.pointerButton(0, 0, true);
    if (!GameTestAccess::isInformationPage(informationPointerGame, 0)) {
        std::cerr << "Information accepted a right release rejected by pAcceptKey\n";
        return 125;
    }
    informationPointerGame.pointerButton(0, 0, false);
    if (!GameTestAccess::isInformationPage(informationPointerGame, 1)) {
        std::cerr << "Information did not map a left release to Space\n";
        return 125;
    }
    for (int page = 0; page < static_cast<int>(capturedInformationHashes.size()); ++page) {
        informationGame.render(renderer);
        const auto renderedInformationHash = regionHash(renderer, 0, 0, 320, 200);
        if (!GameTestAccess::isInformationPage(informationGame, page) ||
            renderedInformationHash != capturedInformationHashes[static_cast<std::size_t>(page)]) {
            std::cerr << "Information page " << page + 1
                      << " did not match the captured original frame: rendered 0x"
                      << std::hex << renderedInformationHash << ", expected 0x"
                      << capturedInformationHashes[static_cast<std::size_t>(page)] << std::dec << '\n';
            return 36 + page;
        }
        informationGame.keyDown(VK_SPACE);
    }
    if (!GameTestAccess::isTitle(informationGame)) {
        std::cerr << "The sixth Information page did not return to the Muncher menu\n";
        return 42;
    }
    Game instructionRoutingGame;
    if (!GameTestAccess::exercisesRecoveredInstructionRouting(instructionRoutingGame)) {
        std::cerr << "Static Y/N, arrow, completion, or Escape instruction routing regressed\n";
        return 93;
    }

    game.keyDown(VK_RETURN); // Play
    game.render(renderer);
    if (regionHash(renderer, 0, 0, 320, 200) != capturedInstructionsQuestionHash) {
        std::cerr << "Instructions question did not match the captured original frame\n";
        return 27;
    }
    Game replayQuestionGame;
    GameTestAccess::prepareCapturedReplayQuestion(replayQuestionGame);
    replayQuestionGame.render(renderer);
    const auto renderedReplayQuestionHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedReplayQuestionHash != capturedReplayQuestionHash) {
        std::cerr << "Replay question did not match the extracted 320x200 original frame: rendered 0x"
                  << std::hex << renderedReplayQuestionHash << ", expected 0x"
                  << capturedReplayQuestionHash << std::dec << '\n';
        return 99;
    }
    game.keyDown(VK_RETURN); // No instructions
    game.render(renderer);
    if (regionHash(renderer, 0, 0, 320, 200) != capturedModeSelectHash) {
        std::cerr << "Game selection did not match the captured original frame\n";
        return 28;
    }
    Game filteredModeGame;
    GameTestAccess::prepareFilteredModeSelect(filteredModeGame);
    filteredModeGame.render(renderer);
    const auto renderedFilteredModeSelectHash = regionHash(renderer, 0, 0, 320, 200);
    if (renderedFilteredModeSelectHash != capturedFilteredModeSelectHash) {
        std::cerr << "Disabled Multiples did not produce the captured renumbered five-game selector: rendered 0x"
                  << std::hex << renderedFilteredModeSelectHash << ", expected 0x"
                  << capturedFilteredModeSelectHash << std::dec << '\n';
        return 52;
    }
    filteredModeGame.keyDown(VK_RETURN);
    if (!GameTestAccess::startedFactors(filteredModeGame)) {
        std::cerr << "The filtered selector did not map entry 1 to Factors\n";
        return 53;
    }
    game.keyDown(VK_RETURN); // Multiples
    game.update(1.0); // complete the original synchronous board presenter
    Game rangeGame;
    Game equalityGame;
    Game inequalityGame;
    Game densityGame;
    Game difficultyPresetGame;
    Game settingsPersistenceGame;
    Game speakerSelectionGame;
    if (!GameTestAccess::usesRecoveredDecoyDistributions(inequalityGame)) {
        std::cerr << "Recovered decoy distribution regression\n";
        return 2;
    }
    Game recoveredGeneratorGame;
    if (!GameTestAccess::usesRecoveredPrimeExpressionAndProgressionRules(recoveredGeneratorGame)) {
        std::cerr << "Recovered Prime, expression-operand, or board-progression regression\n";
        return 2;
    }
    const auto generatorFailure = [](std::string_view rule) {
        std::cerr << "Fresh board or original difficulty/content rule failed: " << rule << '\n';
        return 2;
    };
    if (GameTestAccess::populatedCellCount(game) != 29) return generatorFailure("population");
    if (!GameTestAccess::playerStartsOnBlank(game)) return generatorFailure("blank player start");
    if (GameTestAccess::safeCellCount(game) != 1) return generatorFailure("initial safe count");
    if (!GameTestAccess::usesVariableAnswerDensity(densityGame)) return generatorFailure("answer density");
    if (!GameTestAccess::usesOriginalDifficultyPresets(difficultyPresetGame)) return generatorFailure("difficulty presets");
    if (!GameTestAccess::persistsOptionsAndContent(settingsPersistenceGame)) return generatorFailure("settings persistence");
    Game joystickGame;
    if (!GameTestAccess::usesRecoveredJoystickCalibration(joystickGame)) {
        return generatorFailure("joystick calibration and runtime input");
    }
    if (!GameTestAccess::exercisesSpeakerSelection(speakerSelectionGame)) {
        return generatorFailure("Alt+P speaker selection");
    }
    if (!GameTestAccess::exercisesRecoveredToggleFeedback()) {
        return generatorFailure("Alt+S/Alt+M driver acknowledgement");
    }
    if (!GameTestAccess::exercisesRecoveredGameplayAudioRoutes()) {
        return generatorFailure("resident gameplay-audio routing");
    }
    if (!GameTestAccess::enforcesRecoveredOptionsPassword(settingsPersistenceGame)) {
        return generatorFailure("Options password enforcement");
    }
    if (!GameTestAccess::usesRecoveredHallAdmissionAndTieRules(settingsPersistenceGame)) {
        return generatorFailure("Hall admission and ties");
    }
    if (!GameTestAccess::usesSuppliedContentRanges(rangeGame)) return generatorFailure("content ranges");
    if (!GameTestAccess::generatesFourOperationEquality(equalityGame)) return generatorFailure("Equality");
    if (!GameTestAccess::generatesExpressionInequality(inequalityGame)) return generatorFailure("Inequality");
    if (GameTestAccess::capturedInequalityFeedback(inequalityGame) != "Oops!  \"30+4=34\"") {
        return generatorFailure("Inequality feedback");
    }
    if (!GameTestAccess::usesCapturedFeedbackForms(inequalityGame)) return generatorFailure("feedback forms");
    if (!GameTestAccess::usesRecoveredWrongAnswerFlow(inequalityGame)) {
        return generatorFailure("wrong-answer lifecycle");
    }
    if (!GameTestAccess::validCapturedMultiplesRange(game)) return generatorFailure("Multiples range");
    using GameRule = bool (*)(Game&);
    const std::array<std::pair<std::string_view, GameRule>, 25> gameRules = {{
        {"edge spawn", &GameTestAccess::spawnsFromAnEdge},
        {"ordinary Troggle frame states", &GameTestAccess::usesRecoveredOrdinaryTroggleFrames},
        {"player movement callbacks", &GameTestAccess::usesRecoveredPlayerMovement},
        {"letter movement aliases", &GameTestAccess::usesRecoveredLetterMovementAliases},
        {"cartoon input gate", &GameTestAccess::usesRecoveredCartoonInputGate},
        {"busy-player pause gate", &GameTestAccess::usesRecoveredBusyPauseGate},
        {"player collision state gate", &GameTestAccess::usesRecoveredPlayerCollisionStateGate},
        {"species steering", &GameTestAccess::usesSpeciesMovementRules},
        {"unbounded protected-turn retry", &GameTestAccess::retriesProtectedTurnsWithoutSyntheticCap},
        {"live-pixel steering", &GameTestAccess::usesLivePlayerCoordinatesForEnemySteering},
        {"edge exit", &GameTestAccess::reggieLeavesBoard},
        {"due-job order", &GameTestAccess::preservesEnemyDueJobOrder},
        {"enemy cadence", &GameTestAccess::usesMeasuredEnemyCadence},
        {"recurring slots", &GameTestAccess::usesRecurringEnemySlots},
        {"player-animation concurrency", &GameTestAccess::keepsEnemySchedulerRunningDuringPlayerAnimation},
        {"species cell effects", &GameTestAccess::appliesSpeciesCellRules},
        {"board-complete trail callback abort", &GameTestAccess::abortsTrailCallbacksAfterBoardCompletion},
        {"safe-zone cycles", &GameTestAccess::cyclesIndependentSafeZones},
        {"overlap and safe crossing", &GameTestAccess::usesTroggleOverlapAndSafeCrossingRules},
        {"four-Muncher lifecycle", &GameTestAccess::usesFourMuncherLifeCycle},
        {"recovered cartoon cadence and order", &GameTestAccess::usesRecoveredCartoonCadenceAndOrder},
        {"recovered cartoon framebuffer timelines", &GameTestAccess::rendersRecoveredCartoonTimelines},
        {"recovered cartoon scores", &GameTestAccess::decodesRecoveredCartoonScores},
        {"score table and bonuses", &GameTestAccess::usesReferenceScoreTableAndBonuses},
        {"captured later-level user HUD", &GameTestAccess::usesCapturedLaterLevelUserHud},
    }};
    const char* dumpNumberScenesInCgaText =
        std::getenv("MUNCHERS_DUMP_NUMBER_SCENE_CGA");
    const bool dumpNumberScenesInCga =
        dumpNumberScenesInCgaText && *dumpNumberScenesInCgaText;
    for (const auto& [name, rule] : gameRules) {
        const GraphicsMode graphicsMode =
            dumpNumberScenesInCga &&
                    rule == &GameTestAccess::rendersRecoveredCartoonTimelines
                ? GraphicsMode::Cga4
                : GraphicsMode::Vga256;
        Game ruleGame(graphicsMode);
        if (!rule(ruleGame)) {
            std::cerr << "Recovered gameplay rule regressed: " << name << '\n';
            return 17;
        }
    }
    Game liveCollisionGame;
    if (!GameTestAccess::liveCollisionBiteCompositeMatches(liveCollisionGame)) {
        std::cerr << "Live Number collision bite composite regressed\n";
        return 126;
    }
    Game liveEntryGame;
    if (!GameTestAccess::liveNm000LeftEntryMatches(liveEntryGame)) {
        std::cerr << "Live Number warning/left-edge entry composite regressed\n";
        return 127;
    }
    Game liveRightEntryGame;
    if (!GameTestAccess::liveNm001RightEntryMatches(liveRightEntryGame)) {
        std::cerr << "Live Number warning/right-edge entry composite regressed\n";
        return 128;
    }
    Game liveBottomEntryGame;
    if (!GameTestAccess::liveDemoBottomBashfulEntryMatches(liveBottomEntryGame)) {
        std::cerr << "Live Number bottom-edge Bashful entry composite regressed\n";
        return 129;
    }
    if (!GameTestAccess::usesCapturedLaterBottomBashfulEntryFrames()) {
        std::cerr << "Later live Number bottom-edge Bashful entry composite regressed\n";
        return 199;
    }
    if (!GameTestAccess::usesCapturedLaterBottomBashfulExitFrames()) {
        std::cerr << "Later live Number bottom-exit Bashful/player composite regressed\n";
        return 200;
    }
    if (!GameTestAccess::usesCapturedFirstDemoRightApproachFrames()) {
        std::cerr << "First-Demo rightward player/Bashful/Reggie approach regressed\n";
        return 201;
    }
    Game pointerRoutingGame;
    if (!GameTestAccess::usesRecoveredMenuPointerWidgets()) {
        std::cerr << "Static menu pointer hit boxes, numbered shortcuts, select/activate semantics, or Space filtering regressed\n";
        return 98;
    }
    if (!GameTestAccess::usesRecoveredContentPointerAndChallengeRules()) {
        std::cerr << "Set Content pointer rectangles or Challenge enable rules regressed\n";
        return 100;
    }
    if (!GameTestAccess::usesRecoveredGameplayPointerRouting(pointerRoutingGame)) {
        std::cerr << "Static gameplay pointer target, tie-break, queue, or two-click routing regressed\n";
        return 94;
    }
    if (!GameTestAccess::usesRecoveredMixedGameplayInputRing(pointerRoutingGame)) {
        std::cerr << "Shared gameplay key/pointer FIFO or actor-busy input retention regressed\n";
        return 125;
    }
    GameTestAccess::placePlayer(game, 2, 2);
    GameTestAccess::makePlayerCellMunchable(game);
    game.render(renderer);

    const std::uint64_t idlePlayer = regionHash(renderer, 116, 86, 164, 116);
    const std::uint64_t idleHeader = regionHash(renderer, 0, 0, 320, 24);
    constexpr std::array<int, 7> referenceGridColumns = {20, 68, 116, 164, 212, 260, 308};
    for (const int x : referenceGridColumns) {
        if (pixelColor(renderer, x, 30) != Colors::Magenta) {
            std::cerr << "Board did not render the measured six-column grid\n";
            return 2;
        }
    }
    if (pixelColor(renderer, 18, 24) != Colors::Magenta ||
        pixelColor(renderer, 310, 178) != Colors::Magenta ||
        pixelColor(renderer, 0, 40) != Colors::BoardBlue) {
        std::cerr << "Board did not render the measured double-border geometry\n";
        return 3;
    }
    if (pixelColor(renderer, 50, 184) != Colors::White ||
        pixelColor(renderer, 114, 196) != Colors::White ||
        !regionContains(renderer, 149, 180, 173, 200, 0xe7db00) ||
        !regionContains(renderer, 193, 180, 217, 200, 0xe7db00) ||
        !regionContains(renderer, 237, 180, 261, 200, 0xe7db00)) {
        std::cerr << "Board did not render the measured score field and three-life footer\n";
        return 4;
    }
    if (!regionContains(renderer, 116, 86, 164, 116, 0xe7db00) ||
        regionContains(renderer, 116, 86, 164, 116, 0x00e400)) {
        std::cerr << "Muncher did not use the measured DOS yellow palette remap\n";
        return 5;
    }
    game.update(0.17);
    game.render(renderer);
    if (regionHash(renderer, 116, 86, 164, 116) != idlePlayer) {
        std::cerr << "Standing Muncher did not preserve the captured fixed idle pose\n";
        return 6;
    }

    game.keyDown(VK_SPACE);
    if (!GameTestAccess::usesRecoveredMunchDispatcher()) {
        std::cerr << "Muncher chew dispatcher did not match the recovered callback trace\n";
        return 4;
    }
    std::array<std::uint64_t, 7> munchFrames{};
    constexpr double MunchTickSeconds = 1.0 / OriginalSchedulerTicksPerSecond;
    for (std::size_t phase = 0; phase < munchFrames.size(); ++phase) {
        game.render(renderer);
        munchFrames[phase] = regionHash(renderer, 116, 86, 164, 116);
        if (regionHash(renderer, 0, 0, 320, 24) != idleHeader) {
            std::cerr << "Static board header changed during munch animation\n";
            return 3;
        }
        if (phase + 1 < munchFrames.size()) game.update(MunchTickSeconds + 0.000001);
    }

    if (idlePlayer == munchFrames[0] || munchFrames[0] == munchFrames[1]) {
        std::cerr << "Muncher did not enter distinct squash and stand frames\n";
        return 4;
    }
    if (munchFrames[0] != munchFrames[2] || munchFrames[0] != munchFrames[4] ||
        munchFrames[0] != munchFrames[6] || munchFrames[1] != munchFrames[3] ||
        munchFrames[1] != munchFrames[5]) {
        std::cerr << "Muncher did not follow the recovered seven-tick two-pose cycle\n";
        return 5;
    }
    // Snapshot the complete 48x30 cell after the selected label is erased.
    // These hashes lock exact BTMP crops, transparent blit, measured yellow
    // palette, board-relative placement, and all seven stable pose intervals.
    constexpr std::array<std::uint64_t, 7> ExpectedMunchFrames = {
        0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
        0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
        0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
        0x1c94da58fe5737fdull,
    };
    if (munchFrames != ExpectedMunchFrames) {
        std::cerr << "Muncher chew cell pixels did not match the exact extracted records and placement\n";
        return 5;
    }

    game.update(MunchTickSeconds + 0.000001); // Seventh callback resolves the munch.
    game.render(renderer);
    if (regionHash(renderer, 116, 86, 164, 116) == munchFrames.back()) {
        std::cerr << "Muncher remained in the chew pose after the measured duration\n";
        return 6;
    }

    Game moveGame;
    GameTestAccess::prepareMove(moveGame);
    moveGame.render(renderer);
    const std::uint64_t moveBefore = regionHash(renderer, 116, 86, 212, 116);
    moveGame.keyDown(VK_RIGHT);
    moveGame.update(0.10);
    moveGame.render(renderer);
    const std::uint64_t moveHalfway = regionHash(renderer, 116, 86, 212, 116);
    const auto moveMinimum = colorMinimum(renderer, 20, 26, 308, 176, 0xe7db00);
    if (!GameTestAccess::isMoving(moveGame) || GameTestAccess::moveDirection(moveGame) != 1 ||
        GameTestAccess::playerColumn(moveGame) != 2 ||
        moveBefore == moveHalfway || moveMinimum != std::array<int, 2>{141, 96}) {
        std::cerr << "Player did not use the recovered right-facing movement phase; yellow minimum="
                  << moveMinimum[0] << ',' << moveMinimum[1] << "\n";
        return 7;
    }
    moveGame.update(6.0 / OriginalSchedulerTicksPerSecond - 0.10 + 0.001);
    if (GameTestAccess::isMoving(moveGame) || GameTestAccess::playerColumn(moveGame) != 3) {
        std::cerr << "Player movement did not resolve after six recovered callbacks\n";
        return 8;
    }

    for (const auto& [key, expected, expectedDirection] : std::array{
             std::tuple{VK_UP, std::array<int, 2>{125, 84}, 0},
             std::tuple{VK_DOWN, std::array<int, 2>{125, 108}, 2},
             std::tuple{VK_LEFT, std::array<int, 2>{109, 96}, 3}}) {
        Game directionGame;
        GameTestAccess::prepareMove(directionGame);
        directionGame.keyDown(key);
        directionGame.update(0.10);
        directionGame.render(renderer);
        const auto directionMinimum =
            colorMinimum(renderer, 20, 26, 308, 176, 0xe7db00);
        if (GameTestAccess::moveDirection(directionGame) != expectedDirection ||
            directionMinimum != expected) {
            std::cerr << "Player did not use the recovered direction-specific movement phase "
                      << expectedDirection << "; yellow minimum=" << directionMinimum[0] << ','
                      << directionMinimum[1] << "\n";
            return 9;
        }
    }

    Game quitGame;
    GameTestAccess::prepareMove(quitGame);
    quitGame.keyDown(VK_ESCAPE);
    quitGame.render(renderer);
    const auto renderedQuitConfirmHash = regionHash(renderer, 0, 67, 320, 132);
    if (!GameTestAccess::isQuitConfirm(quitGame) ||
        renderedQuitConfirmHash != capturedQuitConfirmRegionHash) {
        std::cerr << "In-game Escape confirmation did not match the captured original region: rendered 0x"
                  << std::hex << renderedQuitConfirmHash << ", expected 0x"
                  << capturedQuitConfirmRegionHash << std::dec << '\n';
        return 54;
    }
    quitGame.keyDown(VK_RETURN); // Default No.
    if (!GameTestAccess::isPlaying(quitGame)) {
        std::cerr << "Default No did not return from the quit confirmation to the live board\n";
        return 55;
    }
    quitGame.keyDown(VK_ESCAPE);
    quitGame.keyDown(VK_LEFT);
    quitGame.keyDown(VK_RETURN);
    if (!GameTestAccess::isPostGameHall(quitGame)) {
        std::cerr << "Confirmed zero-score quit did not open the current Hall of Fame\n";
        return 56;
    }
    Game scoredQuitGame;
    GameTestAccess::prepareMove(scoredQuitGame);
    GameTestAccess::makeScoreQualify(scoredQuitGame);
    scoredQuitGame.keyDown(VK_ESCAPE);
    scoredQuitGame.keyDown(VK_LEFT);
    scoredQuitGame.keyDown(VK_RETURN);
    if (!GameTestAccess::isPostGameHall(scoredQuitGame)) {
        std::cerr << "Confirmed scored quit incorrectly attempted Hall-of-Fame submission\n";
        return 57;
    }
    Game pauseGame;
    GameTestAccess::prepareMove(pauseGame);
    pauseGame.keyDown(VK_RETURN);
    pauseGame.render(renderer);
    const auto renderedTimeOutHash = regionHash(renderer, 0, 60, 18, 138);
    if (!GameTestAccess::isPaused(pauseGame) || renderedTimeOutHash != capturedTimeOutRegionHash) {
        std::cerr << "Enter time-out marker did not match the captured vertical original region: rendered 0x"
                  << std::hex << renderedTimeOutHash << ", expected 0x"
                  << capturedTimeOutRegionHash << std::dec << '\n';
        return 65;
    }
    pauseGame.update(5.0);
    pauseGame.keyDown(VK_ESCAPE);
    pauseGame.keyDown(VK_RETURN); // Default No.
    if (!GameTestAccess::isPaused(pauseGame)) {
        std::cerr << "No from a paused quit confirmation did not return to Time out\n";
        return 66;
    }
    pauseGame.keyDown(VK_RETURN);
    if (!GameTestAccess::isPlaying(pauseGame)) {
        std::cerr << "Enter did not resume from Time out\n";
        return 67;
    }

    Game safeGame;
    GameTestAccess::prepareMove(safeGame);
    GameTestAccess::placePlayer(safeGame, 2, 2);
    GameTestAccess::placeSafeCell(safeGame, 0, 0);
    safeGame.render(renderer);
    if (pixelColor(renderer, 21, 27) != Colors::White ||
        pixelColor(renderer, 29, 27) != Colors::White ||
        pixelColor(renderer, 21, 35) != Colors::White ||
        pixelColor(renderer, 67, 27) != Colors::White ||
        pixelColor(renderer, 21, 55) != Colors::White ||
        pixelColor(renderer, 67, 55) != Colors::White ||
        pixelColor(renderer, 23, 29) != Colors::BoardBlue) {
        std::cerr << "Safe-zone brackets did not match the measured one-pixel inset/nine-pixel arms\n";
        return 9;
    }

    Game warningGame;
    GameTestAccess::prepareEnemyWarning(warningGame);
    warningGame.update(1.0 / OriginalSchedulerTicksPerSecond + 0.001);
    warningGame.render(renderer);
    const auto renderedTroggleWarningHash = regionHash(renderer, 0, 60, 18, 138);
    const double warningRemaining = GameTestAccess::enemyWarningRemaining(warningGame);
    if (!GameTestAccess::enemyWarning(warningGame) || GameTestAccess::enemyCount(warningGame) != 1 ||
        warningRemaining < 90.0 / OriginalSchedulerTicksPerSecond - 0.01 ||
        warningRemaining > 90.0 / OriginalSchedulerTicksPerSecond ||
        renderedTroggleWarningHash != capturedTroggleWarningRegionHash) {
        std::cerr << "Level-1 Troggle warning did not match the captured vertical marker: rendered 0x"
                  << std::hex << renderedTroggleWarningHash << ", expected 0x"
                  << capturedTroggleWarningRegionHash << std::dec << '\n';
        return 43;
    }
    warningGame.update(warningRemaining + 0.001);
    if (GameTestAccess::enemyWarning(warningGame) || GameTestAccess::enemyCount(warningGame) != 1) {
        std::cerr << "Troggle did not begin its edge-entry move after the warning\n";
        return 45;
    }


    Game correctGame;
    GameTestAccess::prepareCorrectMultiple(correctGame);
    correctGame.keyDown(VK_SPACE);
    correctGame.update(0.251);
    if (GameTestAccess::score(correctGame) != 5 || !GameTestAccess::firstCellEaten(correctGame) ||
        !GameTestAccess::isPlaying(correctGame)) {
        std::cerr << "Correct level-1 munch did not clear the cell and award the captured 5 points\n";
        return 9;
    }

    Game feedbackGame;
    GameTestAccess::prepareWrongMultiple(feedbackGame);
    feedbackGame.render(renderer);
    const std::uint64_t feedbackHeaderBefore = regionHash(renderer, 0, 0, 320, 24);
    const std::uint64_t feedbackStripBefore = regionHash(renderer, 20, 86, 308, 116);

    Game capturedFeedbackFrame;
    GameTestAccess::prepareCapturedInequalityFeedbackFrame(capturedFeedbackFrame);
    capturedFeedbackFrame.render(renderer);
    constexpr std::uint64_t capturedInequalityFeedbackStripHash = 0x873482585c0afb4bull;
    const std::uint64_t renderedInequalityFeedbackStripHash = regionHash(renderer, 20, 86, 308, 116);
    if (renderedInequalityFeedbackStripHash != capturedInequalityFeedbackStripHash) {
        std::cerr << "Inequality feedback strip did not match the lossless original capture: rendered 0x"
                  << std::hex << renderedInequalityFeedbackStripHash << ", expected 0x"
                  << capturedInequalityFeedbackStripHash << std::dec << '\n';
        return 107;
    }
    Game capturedAttractFeedbackFrame;
    GameTestAccess::prepareCapturedAttractCollisionFeedbackFrame(capturedAttractFeedbackFrame);
    capturedAttractFeedbackFrame.render(renderer);
    constexpr std::uint64_t capturedAttractFeedbackStripHash = 0x15a745407e98c823ull;
    const std::uint64_t renderedAttractFeedbackStripHash = regionHash(renderer, 20, 86, 308, 116);
    if (renderedAttractFeedbackStripHash != capturedAttractFeedbackStripHash) {
        std::cerr << "Attract collision feedback strip did not match the lossless original capture: rendered 0x"
                  << std::hex << renderedAttractFeedbackStripHash << ", expected 0x"
                  << capturedAttractFeedbackStripHash << std::dec << '\n';
        return 108;
    }

    feedbackGame.keyDown(VK_SPACE);
    feedbackGame.update(0.251);
    feedbackGame.render(renderer);
    if (!GameTestAccess::isFeedback(feedbackGame) || GameTestAccess::lives(feedbackGame) != 3 ||
        GameTestAccess::feedbackMessage(feedbackGame) != "\"417\" is not a multiple of \"16\"." ||
        !GameTestAccess::firstCellEaten(feedbackGame)) {
        std::cerr << "Wrong answer did not enter the board-preserving feedback state\n";
        return 9;
    }
    const std::uint64_t feedbackHeaderAfter = regionHash(renderer, 0, 0, 320, 24);
    const std::uint64_t feedbackStripAfter = regionHash(renderer, 20, 86, 308, 116);
    if (feedbackHeaderAfter != feedbackHeaderBefore || feedbackStripAfter == feedbackStripBefore) {
        std::cerr << "Wrong-answer feedback did not preserve the header and replace the message strip: header "
                  << feedbackHeaderBefore << " -> " << feedbackHeaderAfter << ", strip "
                  << feedbackStripBefore << " -> " << feedbackStripAfter << '\n';
        return 10;
    }

    if (!GameTestAccess::usesRecoveredFeedbackQuitRoute()) {
        std::cerr << "Feedback Escape/default-No/scored-terminal quit routing regressed\n";
        return 120;
    }

    feedbackGame.keyDown(VK_RIGHT);
    if (!GameTestAccess::isFeedback(feedbackGame) || GameTestAccess::playerRow(feedbackGame) != 0 ||
        GameTestAccess::playerColumn(feedbackGame) != 0) {
        std::cerr << "Wrong-answer feedback accepted an unsupported key\n";
        return 11;
    }
    feedbackGame.keyDown(VK_SPACE);
    if (!GameTestAccess::isPlaying(feedbackGame) || GameTestAccess::target(feedbackGame) != 16) {
        std::cerr << "Space did not resume the preserved board after wrong-answer feedback\n";
        return 12;
    }

    Game deathGame;
    GameTestAccess::prepareTroggleDeath(deathGame);
    deathGame.render(renderer);
    const std::uint64_t deathPoseOpen = regionHash(renderer, 116, 56, 164, 86);
    const std::uint64_t deathStripBefore = regionHash(renderer, 20, 86, 308, 116);
    if (!GameTestAccess::isFeedback(deathGame) || !GameTestAccess::isDeathAnimating(deathGame) ||
        GameTestAccess::lives(deathGame) != 4 ||
        !regionContains(renderer, 116, 56, 164, 86, 0xff5d5d)) {
        std::cerr << "Troggle collision did not start the measured death animation/palette\n";
        return 13;
    }
    deathGame.keyDown(VK_SPACE);
    // Records 12 and 14 are pixel-identical; sample after one tick so the
    // live alternating closed pose (record 13) is observable.
    const double firstBiteSample = 1.01 / OriginalSchedulerTicksPerSecond;
    deathGame.update(firstBiteSample);
    deathGame.render(renderer);
    const std::uint64_t deathPoseClosed = regionHash(renderer, 116, 56, 164, 86);
    if (!GameTestAccess::isDeathAnimating(deathGame) || GameTestAccess::lives(deathGame) != 4 ||
        deathPoseOpen == deathPoseClosed) {
        std::cerr << "Troggle death animation did not advance through its recorded 12/13/14 poses\n";
        return 14;
    }
    deathGame.update(21.0 / OriginalSchedulerTicksPerSecond - firstBiteSample + 0.002);
    deathGame.render(renderer);
    if (GameTestAccess::isDeathAnimating(deathGame) || GameTestAccess::lives(deathGame) != 3 ||
        regionHash(renderer, 20, 86, 308, 116) == deathStripBefore) {
        std::cerr << "Troggle death animation did not reveal feedback after its 21 scheduler ticks\n";
        return 15;
    }
    deathGame.keyDown(VK_SPACE);
    const int recoveryRow = GameTestAccess::playerRow(deathGame);
    const int recoveryColumn = GameTestAccess::playerColumn(deathGame);
    deathGame.keyDown(VK_RIGHT);
    deathGame.character(L'k');
    if (!GameTestAccess::isPlaying(deathGame) || GameTestAccess::enemyCount(deathGame) != 1 ||
        !GameTestAccess::playerRecovering(deathGame) || GameTestAccess::playerMoving(deathGame) ||
        GameTestAccess::queuedPlayerInputCount(deathGame) != 2 ||
        GameTestAccess::playerRow(deathGame) != recoveryRow ||
        GameTestAccess::playerColumn(deathGame) != recoveryColumn) {
        std::cerr << "Troggle feedback did not preserve the enemy and retain recovery input in the shared ring\n";
        return 16;
    }
    // 0x09da5 installs a fresh randomized 30-119-tick dwell for the biter at
    // the terminal bite callback. Advance only until that survivor actually
    // leaves the collision cell instead of assuming the former fixed delay.
    for (int tick = 0; tick < 170 && GameTestAccess::playerRecovering(deathGame); ++tick) {
        deathGame.update(1.0 / OriginalSchedulerTicksPerSecond);
    }
    if (!GameTestAccess::isPlaying(deathGame) || GameTestAccess::enemyCount(deathGame) != 1 ||
        GameTestAccess::playerRecovering(deathGame) || GameTestAccess::playerMoving(deathGame) ||
        GameTestAccess::queuedPlayerInputCount(deathGame) != 2) {
        std::cerr << "Player did not reappear after the colliding Reggie moved away\n";
        return 46;
    }
    deathGame.update(1.0 / OriginalSchedulerTicksPerSecond + 1e-12);
    if (!GameTestAccess::playerMoving(deathGame) ||
        GameTestAccess::queuedPlayerInputCount(deathGame) != 1) {
        std::cerr << "Recovery input did not start on the next player-state-4 callback\n";
        return 126;
    }

    Game lowScoreFinalLoss;
    GameTestAccess::prepareTroggleDeath(lowScoreFinalLoss, 1, 45);
    lowScoreFinalLoss.update(0.731);
    lowScoreFinalLoss.keyDown(VK_SPACE);
    if (!GameTestAccess::isHall(lowScoreFinalLoss)) {
        std::cerr << "Final low-score Troggle loss did not route directly to the Hall\n";
        return 72;
    }

    Game qualifyingFinalLoss;
    GameTestAccess::prepareTroggleDeath(qualifyingFinalLoss, 1, 50);
    qualifyingFinalLoss.update(0.731);
    qualifyingFinalLoss.keyDown(VK_SPACE);
    if (!GameTestAccess::isNameEntry(qualifyingFinalLoss)) {
        std::cerr << "Measured qualifying score 50 did not open Hall-of-Fame name entry after the final loss\n";
        return 73;
    }

    Game capturedNameEntry;
    GameTestAccess::prepareCapturedNameEntry(capturedNameEntry);
    capturedNameEntry.render(renderer);
    const std::uint64_t capturedNameEntryHash = regionHash(renderer, 0, 0, 320, 200);
    if (capturedNameEntryHash != 0xbab9ce334800c231ull) {
        std::cerr << "Hall-of-Fame name entry did not match the captured original full frame: rendered 0x"
                  << std::hex << capturedNameEntryHash << std::dec << '\n';
        return 74;
    }
    if (!GameTestAccess::usesCapturedNameEntryInput(capturedNameEntry)) {
        std::cerr << "Hall-of-Fame name entry did not enforce the original 25-character input\n";
        return 75;
    }
    Game nameEditingGame;
    if (!GameTestAccess::usesRecoveredNameEntryEditing(nameEditingGame)) {
        std::cerr << "Recovered name-entry Left/Home/Escape/default-name behavior regressed\n";
        return 97;
    }

    if (!GameTestAccess::usesCapturedDualHelperMovementFrames()) {
        std::cerr << "Captured dual-Helper movement frames regressed\n";
        return 199;
    }
    if (!GameTestAccess::usesCapturedHelperBottomExitPlayerMoveFrames()) {
        std::cerr << "Captured Helper bottom-exit/player-move frames regressed\n";
        return 200;
    }
    std::cout << "idle=" << idlePlayer
              << " squash=" << munchFrames[0]
              << " stand=" << munchFrames[1] << '\n';
    return 0;
}
