#include "../src/munchers_app.h"
#include "../src/mecc_sound.h"
#include "../src/resource_ids.h"
#include "../src/win32_shortcuts.h"
#include "../src/word_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

constexpr double WordSchedulerTicksPerSecond =
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
    static void configureAudio(Game& game, const bool sound, const bool music,
                               const bool speaker) {
        game.settingsPersistenceEnabled_ = false;
        game.soundOn_ = sound;
        game.musicOn_ = music;
        game.speakerEffects_ = speaker;
    }
    static bool audioMatches(const Game& game, const bool sound, const bool music,
                             const bool speaker) {
        return game.soundOn_ == sound && game.musicOn_ == music &&
               game.speakerEffects_ == speaker;
    }
    static bool isStartupSplash(const Game& game) {
        return game.page_ == Game::Page::StartupSplash;
    }
    static bool isTitle(const Game& game) { return game.page_ == Game::Page::Title; }
    static bool isAttract(const Game& game) {
        return game.attractMode_ && game.page_ == Game::Page::Attract;
    }
    static void prepareMusicOffAttractBoard(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.musicOn_ = false;
        game.startAttract();
    }
    static void prepareMusicOffAttractBoardToHall(Game& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = Game::Page::Feedback;
        game.attractHallTransitionPhase_ = Game::AttractHallTransitionPhase::Wipe;
    }
    static void prepareMusicOffAttractHallToSplash(Game& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Attract;
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::HallToSplash;
    }
    static void prepareMusicOffAttractSplashToBoard(Game& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = Game::Page::StartupSplash;
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::SplashToBoard;
    }
    static std::size_t attractMusicLoopPlays(const Game& game) {
        return game.attractOplPlayer_.loopPlayCount();
    }
    static bool attractMusicStopped(const Game& game) {
        return !game.attractOplPlayer_.looping() &&
               !game.attractOplPlayer_.effectPlaying() &&
               game.attractOplPlayer_.scoreWriteCount() == 0 &&
               !game.musicPlayer_.playing() &&
               !game.effectPlayer_.playing();
    }
    static void prepareSpeakerAttractBoard(Game& game) {
        game.settingsPersistenceEnabled_ = false;
        game.soundOn_ = true;
        game.musicOn_ = true;
        game.speakerEffects_ = true;
        game.startAttract();
    }
    static void prepareSpeakerAttractBoardToHall(Game& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = Game::Page::Feedback;
        game.attractHallTransitionPhase_ = Game::AttractHallTransitionPhase::Wipe;
    }
    static void prepareSpeakerAttractHallToSplash(Game& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = Game::Page::Hall;
        game.hallContext_ = Game::HallContext::Attract;
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::HallToSplash;
    }
    static void prepareSpeakerAttractSplashToBoard(Game& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = Game::Page::StartupSplash;
        game.attractInterstitialTransition_ =
            Game::AttractInterstitialTransition::SplashToBoard;
    }
    static bool cheatOpen(const Game& game) { return game.cheatOpen_; }
    static void forceSettingsWriteFailure(Game& game) {
        game.settingsPathOverride_ = std::filesystem::temp_directory_path().wstring();
        game.settingsPersistenceEnabled_ = true;
    }
    static bool configurationWriteError(const Game& game) {
        return game.configurationWriteError_;
    }
    static bool configurationContinuationPending(const Game& game) {
        return game.configurationWriteErrorPostDismissContinuation_;
    }
    static int menuSelection(const Game& game) { return game.menuSelection_; }
    static bool exitPending(const Game& game) { return game.exitPending_; }
    static double exitDelayTimer(const Game& game) { return game.exitDelayTimer_; }
};

struct WordGameTestAccess {
    static void configureAudio(WordGame& game, const bool sound, const bool music,
                               const bool speaker) {
        game.settingsPersistenceEnabled_ = false;
        game.soundOn_ = sound;
        game.musicOn_ = music;
        game.speakerEffects_ = speaker;
    }

    static bool audioMatches(const WordGame& game, const bool sound, const bool music,
                             const bool speaker) {
        return game.soundOn_ == sound && game.musicOn_ == music &&
               game.speakerEffects_ == speaker;
    }

    static bool exitPending(const WordGame& game) { return game.exitPending_; }
    static double exitDelayTimer(const WordGame& game) { return game.exitDelayTimer_; }
    static void forceSettingsWriteFailure(WordGame& game) {
        game.settingsPathOverride_ = std::filesystem::temp_directory_path().wstring();
        game.settingsPersistenceEnabled_ = true;
    }
    static bool configurationWriteError(const WordGame& game) {
        return game.configurationWriteError_;
    }
    static bool configurationContinuationPending(const WordGame& game) {
        return game.configurationWriteErrorPostDismissContinuation_;
    }

    static void prepareMusicOffAttractBoard(WordGame& game) {
        game.settingsPersistenceEnabled_ = false;
        game.musicOn_ = false;
        game.startAttract();
    }

    static void prepareMusicOffAttractBoardToHall(WordGame& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = WordGamePage::Feedback;
        game.attractHallTransitionPhase_ =
            WordGame::AttractHallTransitionPhase::Wipe;
    }

    static void prepareMusicOffAttractHallToLogo(WordGame& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = WordGamePage::Hall;
        game.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::HallToLogo;
    }

    static void prepareMusicOffAttractLogoToBoard(WordGame& game) {
        prepareMusicOffAttractBoard(game);
        game.page_ = WordGamePage::AttractLogo;
        game.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::LogoToBoard;
    }

    static std::size_t attractMusicLoopPlays(const WordGame& game) {
        return game.attractOplPlayer_.loopPlayCount();
    }

    static bool attractMusicStopped(const WordGame& game) {
        return !game.attractOplPlayer_.looping() &&
               !game.attractOplPlayer_.effectPlaying() &&
               game.attractOplPlayer_.scoreWriteCount() == 0 &&
               !game.sceneEffectPlayer_.playing();
    }
    static void prepareSpeakerAttractBoard(WordGame& game) {
        game.settingsPersistenceEnabled_ = false;
        game.soundOn_ = true;
        game.musicOn_ = true;
        game.speakerEffects_ = true;
        game.startAttract();
    }
    static void prepareSpeakerAttractBoardToHall(WordGame& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = WordGamePage::Feedback;
        game.attractHallTransitionPhase_ =
            WordGame::AttractHallTransitionPhase::Wipe;
    }
    static void prepareSpeakerAttractHallToLogo(WordGame& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = WordGamePage::Hall;
        game.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::HallToLogo;
    }
    static void prepareSpeakerAttractLogoToBoard(WordGame& game) {
        prepareSpeakerAttractBoard(game);
        game.page_ = WordGamePage::AttractLogo;
        game.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::LogoToBoard;
    }

    static bool prepareCartoonForAlt(WordGame& game) {
        game.startGame(3);
        disableBoardJobs(game);
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            (void)game.core_.clearCellWithoutScore(static_cast<std::size_t>(index));
        }
        if (!game.core_.completeBoardIfEmpty() || !game.core_.cartoonPending()) return false;
        game.handleCompletedBoard();
        while (game.attractInterstitialTransition_ !=
               WordGame::AttractInterstitialTransition::None) {
            game.attractTransitionTimer_ = 0.0;
            game.update(0.0);
        }
        game.update(0.1);
        return game.page_ == WordGamePage::LevelComplete &&
               game.levelCompleteScene_.valid() &&
               game.attractInterstitialTransition_ ==
                   WordGame::AttractInterstitialTransition::None;
    }

    static bool rendersRecoveredCartoonTimelines() {
        static constexpr std::array<int, 6> TerminalTicks = {102, 142, 133, 106, 110, 251};
        // Retained-painter baselines. Every nonterminal distinct state is
        // hash-identical to the lossless live DOS scene captures.
        static constexpr std::array<std::uint64_t, 6> ExpectedVgaTimelineHashes = {
            0x216013727f6b146dull,
            0x747908352019b846ull,
            0x114b4cb70be42f30ull,
            0xe93fd69c52024db4ull,
            0x30a3a0901953a8f5ull,
            0x849f840b9f7a6ddbull,
        };
        // Initialized after dumping the complete deterministic renderer
        // timelines through the original four-colour asset/palette path.
        static constexpr std::array<std::uint64_t, 6> ExpectedCgaTimelineHashes = {
            0x3e8f5042b09823a0ull,
            0x3551771e2e68e600ull,
            0x9a71aab8deb20087ull,
            0x87868fe685545f38ull,
            0x0d0ead41e27fa545ull,
            0x39dec377c50b1573ull,
        };
        const char* dumpCgaText = std::getenv("MUNCHERS_DUMP_WORD_SCENE_CGA");
        const bool dumpCga = dumpCgaText && *dumpCgaText;
        const GraphicsMode graphicsMode =
            dumpCga ? GraphicsMode::Cga4 : GraphicsMode::Vga256;
        std::array<std::uint64_t, 6> timelineHashes{};
        std::array<int, 6> visibleFrames{};

        {
            WordGame timed(graphicsMode, 0x50ffu);
            timed.settingsPersistenceEnabled_ = false;
            timed.soundOn_ = false;
            timed.musicOn_ = false;
            timed.page_ = WordGamePage::LevelComplete;
            if (!timed.loadLevelCompleteScene(0) || timed.sceneTicks_ != 0) return false;
            timed.update(0.099);
            if (timed.sceneTicks_ != 0 ||
                std::abs(timed.sceneTickAccumulator_ - 0.99) > 1e-9) return false;
            timed.update(0.001);
            if (timed.sceneTicks_ != 1 ||
                std::abs(timed.sceneTickAccumulator_) > 1e-9) return false;
        }

        const char* dumpDirectoryText = std::getenv("MUNCHERS_DUMP_WORD_SCENE_DIR");
        const bool dumpFrames = dumpDirectoryText && *dumpDirectoryText;
        const std::filesystem::path dumpDirectory =
            dumpFrames ? std::filesystem::path(dumpDirectoryText) : std::filesystem::path{};
        std::ofstream stateOutput;
        if (dumpFrames) {
            std::error_code error;
            std::filesystem::create_directories(dumpDirectory, error);
            if (error) {
                std::cerr << "Could not create Word scene dump directory: "
                          << dumpDirectory << " (" << error.message() << ")\n";
                return false;
            }
            stateOutput.open(dumpDirectory / "word-scene-states.tsv",
                             std::ios::binary | std::ios::trunc);
            if (!stateOutput) return false;
            stateOutput << "scene\ttick\tkind\tindex\tthread\tframe\tx\ty\tlayer"
                           "\tvisible\tended\n";
        }

        const auto frameHash = [](const Renderer& renderer) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const std::uint32_t pixel : renderer.pixels()) {
                hash ^= pixel;
                hash *= 1099511628211ull;
            }
            return hash;
        };

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
                std::cerr << "Could not create Word scene frame: " << outputPath << '\n';
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
                std::cerr << "Could not write Word scene frame: " << outputPath << '\n';
                return false;
            }
            return true;
        };

        for (int sceneIndex = 0; sceneIndex < 6; ++sceneIndex) {
            WordGame game(graphicsMode,
                          static_cast<std::uint16_t>(0x5100 + sceneIndex));
            game.settingsPersistenceEnabled_ = false;
            game.soundOn_ = false;
            game.musicOn_ = false;
            game.page_ = WordGamePage::LevelComplete;
            if (!game.loadLevelCompleteScene(sceneIndex)) return false;
            game.advanceLevelCompleteScene();
            ++game.sceneTicks_;

            std::uint64_t timeline = 1469598103934665603ull;
            while (game.levelCompleteScene_.valid() &&
                   !game.levelCompleteScene_.finished() && game.sceneTicks_ < 20'000) {
                if (stateOutput) {
                    const auto writeActors = [&](const std::vector<SceneActor>& actors,
                                                 const char* kind) {
                        for (std::size_t index = 0; index < actors.size(); ++index) {
                            const SceneActor& actor = actors[index];
                            stateOutput << sceneIndex << '\t' << game.sceneTicks_ << '\t'
                                        << kind << '\t' << index << '\t' << actor.thread
                                        << '\t' << actor.frame << '\t' << actor.x << '\t'
                                        << actor.y << '\t' << actor.layer << '\t'
                                        << actor.visible << '\t' << actor.ended << '\n';
                        }
                    };
                    writeActors(game.levelCompleteScene_.bakedActors(), "baked");
                    writeActors(game.levelCompleteScene_.actors(), "live");
                }
                Renderer renderer;
                game.render(renderer);
                // The final internal framebuffer run begins on the callback
                // that publishes signal 99. DOS tears the scene down from
                // that signal before presenting this run; retain it here as
                // a VM/painter regression state, not live visual evidence.
                if (game.sceneTicks_ ==
                        TerminalTicks[static_cast<std::size_t>(sceneIndex)] - 1 &&
                    game.levelCompleteScene_.signal() != 99) {
                    return false;
                }
                timeline ^= static_cast<std::uint64_t>(game.sceneTicks_);
                timeline *= 1099511628211ull;
                timeline ^= frameHash(renderer);
                timeline *= 1099511628211ull;
                if (!dumpFrame(renderer, sceneIndex, game.sceneTicks_)) return false;
                ++visibleFrames[static_cast<std::size_t>(sceneIndex)];
                game.advanceLevelCompleteScene();
                ++game.sceneTicks_;
            }
            timelineHashes[static_cast<std::size_t>(sceneIndex)] = timeline;
            if (!game.levelCompleteScene_.valid() ||
                !game.levelCompleteScene_.finished() ||
                game.levelCompleteScene_.signal() != 99 ||
                game.sceneTicks_ != TerminalTicks[static_cast<std::size_t>(sceneIndex)] ||
                visibleFrames[static_cast<std::size_t>(sceneIndex)] != game.sceneTicks_ - 1) {
                return false;
            }
        }

        const auto& expectedTimelineHashes =
            dumpCga ? ExpectedCgaTimelineHashes : ExpectedVgaTimelineHashes;
        if (timelineHashes != expectedTimelineHashes) {
            std::cerr << "Recovered Word cartoon framebuffer timelines:" << std::hex;
            for (const std::uint64_t hash : timelineHashes) std::cerr << " 0x" << hash;
            std::cerr << std::dec << " visible=";
            for (const int count : visibleFrames) std::cerr << ' ' << count;
            std::cerr << '\n';
            return false;
        }
        return true;
    }

    static std::size_t adlibEffectPlays(const WordGame& game) {
        return game.attractOplPlayer_.effectPlayCount();
    }

    static bool spriteSourcesAndMuncherPaletteMatch(WordGame& game) {
        const auto eat12 = game.assets_.spriteFrame(1006, 12);
        const auto eat13 = game.assets_.spriteFrame(1006, 13);
        const auto eat14 = game.assets_.spriteFrame(1006, 14);
        const auto eat15 = game.assets_.spriteFrame(1006, 15);
        if (!eat12 || eat12->sheetId != 1006 || eat12->x != 132 || eat12->y != 137 ||
            eat12->width != 45 || eat12->height != 30 ||
            !eat13 || eat13->sheetId != 1006 || eat13->x != 108 || eat13->y != 17 ||
            eat13->width != 45 || eat13->height != 30 ||
            !eat14 || eat14->sheetId != eat12->sheetId || eat14->x != eat12->x ||
            eat14->y != eat12->y || eat14->width != eat12->width ||
            eat14->height != eat12->height ||
            !eat15 || eat15->sheetId != eat13->sheetId || eat15->x != eat13->x ||
            eat15->y != eat13->y || eat15->width != eat13->width ||
            eat15->height != eat13->height) {
            return false;
        }

        // These are all six records in the two Word cartoons that switch
        // from the logical animation ID to a continuation PCXF sheet.
        for (const auto [logicalId, frameIndex, expectedSource] :
             std::array<std::array<int, 3>, 6>{{
                 {{2003, 13, 3003}},
                 {{2003, 14, 3003}},
                 {{2003, 15, 3003}},
                 {{2003, 16, 3003}},
                 {{2005, 20, 3005}},
                 {{2005, 21, 3005}},
             }}) {
            const auto source = game.assets_.spriteFrame(
                static_cast<std::uint32_t>(logicalId), frameIndex);
            if (!source || source->sheetId != static_cast<std::uint32_t>(expectedSource)) {
                return false;
            }
            const Image* sheet = game.assets_.image(source->sheetId);
            if (!sheet) return false;

            Renderer actual;
            Renderer expected;
            actual.clear(Colors::Magenta);
            expected.clear(Colors::Magenta);
            game.drawSprite(actual, static_cast<std::uint32_t>(logicalId), frameIndex, 7, 9);
            expected.drawImageRegion(*sheet, source->x, source->y,
                                     source->width, source->height,
                                     7, 9, source->width, source->height, true);
            if (actual.pixels() != expected.pixels()) return false;
        }

        // Word and Number carry byte-identical Muncher art. The active board
        // palette maps that art's green ramp to the measured yellow ramp;
        // lock the mapping on one of the eating records as well.
        Renderer muncher;
        muncher.clear(Colors::BoardBlue);
        game.drawSprite(muncher, 1006, 12, 20, 26);
        bool sawYellow = false;
        for (const std::uint32_t pixel : muncher.pixels()) {
            if ((pixel & 0xffffffu) == 0xe7db00u) sawYellow = true;
            if ((pixel & 0xffffffu) == 0x00e400u) return false;
        }
        return sawYellow;
    }

    static void advanceStartup(WordGame& game) {
        if (game.page_ == WordGamePage::StartupVersion) game.keyDown(VK_RETURN);
        if (game.page_ == WordGamePage::StartupSplash) game.keyDown(VK_RETURN);
    }

    static void disableBoardJobs(WordGame& game) {
        game.enemies_.clear();
        for (auto& slot : game.enemySlots_) slot = {};
        for (auto& job : game.safeZoneJobs_) job = {};
        game.enemySlotCount_ = 0;
        game.safeZoneJobCount_ = 0;
        game.safeCells_.fill(false);
        game.enemyWarning_ = false;
    }

    static void prepareCapturedCgaTreeBoard(WordGame& game) {
        static constexpr std::array<std::string_view, WordGame::BoardCellCount> Words = {{
            "said", "them", "went",  "went", "he",  "sleep",
            "help", "be",   "said",  "sleep", "she", "went",
            "them", "get",  "red",   "yet",   "me",  "yes",
            "see",  "",     "she",   "yet",   "yet", "pep",
            "get",  "me",   "sleep", "get",   "we",  "be",
        }};
        const auto record = [](const std::string_view text) {
            WordRecord result;
            std::copy_n(text.begin(), std::min(text.size(), result.bytes.size()),
                        result.bytes.begin());
            return result;
        };
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 3;
        game.playerColumn_ = 1;
        game.playerTerminalFrame_ = -1;
        game.moving_ = false;
        game.munching_ = false;
        game.playerRecovering_ = false;
        auto& board = const_cast<WordBoard&>(game.core_.board());
        board.targetSound = 2;
        board.tupleIndex = 0;
        for (std::size_t index = 0; index < Words.size(); ++index) {
            const std::string_view text = Words[index];
            const bool correct = text == "he" || text == "sleep" || text == "be" ||
                                 text == "she" || text == "me" || text == "see" ||
                                 text == "we";
            const WordBoardCell cell{record(text), correct, correct ? 1 : -1};
            if (!game.core_.restoreCell(index, cell, text.empty())) {
                throw std::runtime_error("could not restore captured CGA Word board cell");
            }
        }
        game.safeCells_.fill(false);
        game.safeCells_[1 * WordGame::BoardColumns + 1] = true;
    }

    static bool usesRecoveredRuntimePrngInterleaving() {
        WordGame game(GraphicsMode::Vga256, 0x1234);
        if (game.random_.calls != 0) return false;
        game.startGame(1);

        std::uint64_t snapshot = 1469598103934665603ull;
        const auto appendByte = [&snapshot](const std::uint8_t value) {
            snapshot ^= value;
            snapshot *= 1099511628211ull;
        };
        const auto appendU32 = [&appendByte](const std::uint32_t value) {
            appendByte(static_cast<std::uint8_t>(value));
            appendByte(static_cast<std::uint8_t>(value >> 8));
            appendByte(static_cast<std::uint8_t>(value >> 16));
            appendByte(static_cast<std::uint8_t>(value >> 24));
        };
        const WordBoard& board = game.core_.board();
        appendU32(static_cast<std::uint32_t>(board.targetSound));
        appendU32(static_cast<std::uint32_t>(board.tupleIndex));
        appendU32(static_cast<std::uint32_t>(board.correctCount));
        appendU32(static_cast<std::uint32_t>(board.targetPoolCount));
        for (std::size_t index = 0; index < board.cells.size(); ++index) {
            const WordBoardCell& cell = board.cells[index];
            for (const std::uint8_t value : cell.record.bytes) appendByte(value);
            appendU32(static_cast<std::uint32_t>(cell.signedSourceIndex));
            appendByte(cell.correct ? 1 : 0);
            appendByte(game.core_.eaten(index) ? 1 : 0);
        }
        appendU32(static_cast<std::uint32_t>(game.safeZoneJobCount_));
        for (int index = 0; index < game.safeZoneJobCount_; ++index) {
            const WordGame::SafeZoneJob& job =
                game.safeZoneJobs_[static_cast<std::size_t>(index)];
            appendU32(static_cast<std::uint32_t>(std::llround(
                job.period * WordSchedulerTicksPerSecond)));
            appendByte(job.active ? 1 : 0);
            appendU32(static_cast<std::uint32_t>(job.cellIndex));
        }
        appendU32(static_cast<std::uint32_t>(game.playerColumn_));
        appendU32(static_cast<std::uint32_t>(game.playerRow_));
        appendU32(static_cast<std::uint32_t>(game.enemySlotCount_));
        for (int index = 0; index < game.enemySlotCount_; ++index) {
            const WordGame::EnemySlot& slot =
                game.enemySlots_[static_cast<std::size_t>(index)];
            appendU32(static_cast<std::uint32_t>(slot.type));
            appendU32(static_cast<std::uint32_t>(std::llround(
                slot.timer * WordSchedulerTicksPerSecond)));
        }

        constexpr std::uint64_t ExpectedStartCalls = 252;
        constexpr std::uint32_t ExpectedStartState = 0x0d0015f8u;
        constexpr std::uint64_t ExpectedStartSnapshot = 0xc3d366b5add062c8ull;
        if (game.random_.calls != ExpectedStartCalls ||
            game.random_.state != ExpectedStartState ||
            snapshot != ExpectedStartSnapshot) {
            std::cerr << "word runtime PRNG start calls=" << game.random_.calls
                      << " state=0x" << std::hex << game.random_.state
                      << " snapshot=0x" << snapshot << std::dec << '\n';
            return false;
        }

        const std::uint64_t callsBeforeWarning = game.random_.calls;
        game.enemySlots_[0].timer = 0.0;
        game.updateEnemies(0.0);
        if (game.enemySlots_[0].phase != WordGame::EnemySlotPhase::Warning ||
            game.enemies_.size() != 1) return false;
        const WordGame::Enemy& warning = game.enemies_.front();
        constexpr std::uint32_t ExpectedWarningState = 0x2490fc6eu;
        constexpr int ExpectedWarningEdge = 2;
        constexpr int ExpectedWarningRow = 0;
        constexpr int ExpectedWarningColumn = 0;
        if (game.random_.calls != callsBeforeWarning + 2 ||
            game.random_.state != ExpectedWarningState ||
            game.enemySlots_[0].edge != ExpectedWarningEdge ||
            warning.row != ExpectedWarningRow ||
            warning.column != ExpectedWarningColumn) {
            std::cerr << "word runtime PRNG warning delta="
                      << (game.random_.calls - callsBeforeWarning)
                      << " state=0x" << std::hex << game.random_.state << std::dec
                      << " edge/row/column=" << game.enemySlots_[0].edge << '/'
                      << warning.row << '/' << warning.column << '\n';
            return false;
        }

        game.enemySlots_[0].timer = 0.0;
        game.updateEnemies(0.0);
        if (!game.enemies_[0].moving) return false;
        const std::uint64_t callsBeforeDwell = game.random_.calls;
        game.enemies_[0].animationTimer = 0.0;
        game.updateEnemies(0.0);
        constexpr std::uint32_t ExpectedDwellState = 0xeb99c6c7u;
        if (game.random_.calls != callsBeforeDwell + 1 ||
            game.random_.state != ExpectedDwellState) {
            std::cerr << "word runtime PRNG dwell delta="
                      << (game.random_.calls - callsBeforeDwell)
                      << " state=0x" << std::hex << game.random_.state << std::dec << '\n';
            return false;
        }
        return true;
    }

    static bool usesRecoveredBoardQuestionAndHud() {
        WordGame game(GraphicsMode::Vga256, 0x4844);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 0;
        game.playerColumn_ = 0;
        auto& board = const_cast<WordBoard&>(game.core_.board());
        auto& score = const_cast<MuncherScore&>(game.core_.scoreState());
        score.restore(12'345, 2);

        const auto regionMatches = [](const Renderer& actual,
                                      const Renderer& expected,
                                      const int top,
                                      const int bottom) {
            for (int y = top; y < bottom; ++y) {
                for (int x = 0; x < Renderer::Width; ++x) {
                    const std::size_t index =
                        static_cast<std::size_t>(y * Renderer::Width + x);
                    if (actual.pixels()[index] != expected.pixels()[index]) return false;
                }
            }
            return true;
        };

        const GemFont& font = game.assets_.largeFont();
        const auto drawExpectedTarget = [&](Renderer& target, const int sound) {
            const std::string_view label =
                wordSoundLabels()[static_cast<std::size_t>(sound)];
            game.drawSprite(target, 6001, sound, 260, 0, false, true,
                            game.assets_.image(6003));
            target.drawCenteredText(font, 164, sound == 5 ? 7 : 6,
                                    label, Colors::White);
            if (sound >= 12) return;
            const int labelX = 164 - target.textWidth(font, label) / 2;
            const int markX = labelX + target.textWidth(font, "/") + 1;
            if ((sound & 1) == 0) {
                const int macronWidth = sound >= 10 ? 15 : 7;
                target.horizontalLine(markX - 1, markX + macronWidth - 2,
                                      6, Colors::White);
            } else if (sound < 10) {
                target.fillRect(markX - 2, 5, 2, 1, Colors::White);
                target.fillRect(markX + 5, 5, 2, 1, Colors::White);
                target.horizontalLine(markX, markX + 4, 6, Colors::White);
            } else {
                constexpr int markedWidth = 13;
                target.fillRect(markX - 1, 5, 2, 1, Colors::White);
                target.fillRect(markX + markedWidth - 3, 5, 2, 1, Colors::White);
                target.horizontalLine(markX + 1, markX + markedWidth - 4,
                                      6, Colors::White);
            }
        };
        for (int sound = 0; sound < static_cast<int>(wordSoundLabels().size()); ++sound) {
            board.targetSound = sound;
            Renderer actual(GraphicsMode::Vga256);
            Renderer expected(GraphicsMode::Vga256);
            game.renderBoard(actual);
            expected.clear(Colors::BoardBlue);
            expected.drawText(font, 0, 6, "Level: 1", Colors::White);
            drawExpectedTarget(expected, sound);
            expected.horizontalLine(92, 234, 2, Colors::White);
            expected.horizontalLine(92, 234, 16, Colors::White);
            if (!regionMatches(actual, expected, 0, 19)) return false;
        }

        Renderer actualFooter(GraphicsMode::Vga256);
        Renderer expectedFooter(GraphicsMode::Vga256);
        game.renderBoard(actualFooter);
        expectedFooter.clear(Colors::BoardBlue);
        expectedFooter.drawText(font, 0, 187, "Score:", Colors::White);
        expectedFooter.outlineRect(50, 184, 65, 13, Colors::White);
        expectedFooter.drawText(font, 56, 187, "12345", Colors::White);
        game.drawSprite(expectedFooter, 1006, 17, 148, 179);
        game.drawSprite(expectedFooter, 1006, 17, 192, 179);
        if (!regionMatches(actualFooter, expectedFooter, 179, Renderer::Height)) return false;

        game.attractMode_ = true;
        game.page_ = WordGamePage::Attract;
        Renderer actualDemo(GraphicsMode::Vga256);
        Renderer expectedDemo(GraphicsMode::Vga256);
        game.renderBoard(actualDemo);
        expectedDemo.clear(Colors::BoardBlue);
        expectedDemo.drawText(font, 30, 6, "Demo", Colors::White);
        drawExpectedTarget(expectedDemo, board.targetSound);
        expectedDemo.horizontalLine(92, 234, 2, Colors::White);
        expectedDemo.horizontalLine(92, 234, 16, Colors::White);
        expectedDemo.drawCenteredText(font, 160, 187,
                                      "Press a key for Muncher Menu", Colors::White);
        return regionMatches(actualDemo, expectedDemo, 0, 19) &&
               regionMatches(actualDemo, expectedDemo, 179, Renderer::Height);
    }

    static bool liveWordAttractBoardRegionsMatch() {
        WordGame game(GraphicsMode::Vga256, 0x574du);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;
        game.attractPostFeedbackBoard_ = false;
        game.playerRow_ = 3;
        game.playerColumn_ = 1;
        game.playerRecovering_ = false;
        game.playerTerminalFrame_ = -1;
        game.moving_ = false;
        game.munching_ = false;
        game.safeCells_.fill(false);
        game.enemies_.clear();
        game.enemyWarning_ = false;

        constexpr std::array<std::string_view, WordGame::BoardCellCount> Words = {
            "he", "we", "red", "be", "see", "be",
            "she", "be", "he", "pep", "see", "he",
            "we", "sleep", "we", "we", "pep", "me",
            "me", "me", "sleep", "see", "them", "them",
            "me", "get", "pep", "get", "me", "get",
        };
        const auto record = [](const std::string_view text) {
            WordRecord result;
            std::copy_n(text.begin(), std::min(text.size(), result.bytes.size()),
                        result.bytes.begin());
            return result;
        };
        auto& board = const_cast<WordBoard&>(game.core_.board());
        board.targetSound = 3;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            const WordBoardCell cell{record(Words[static_cast<std::size_t>(index)]), true, 1};
            if (!game.core_.restoreCell(static_cast<std::size_t>(index), cell, index == 19)) {
                return false;
            }
        }

        Renderer renderer(GraphicsMode::Vga256);
        game.renderBoard(renderer);
        maybeDumpWordOptionsFrame(renderer, "attract-wm009-board-native.ppm");
        const std::uint64_t full = fullFrameHash(renderer);
        const std::uint64_t header = frameRegionHash(renderer, 0, 0, 320, 24);
        const std::uint64_t body = frameRegionHash(renderer, 0, 24, 320, 179);
        const std::uint64_t footer = frameRegionHash(renderer, 0, 179, 320, 200);
        constexpr std::array<std::uint64_t, 4> Expected = {
            0x6426d16846667dacull,
            0x3cda3059237b16ccull,
            0x018f83de58f12485ull,
            0x15935ecd1ff9e561ull,
        };
        const std::array observed = {full, header, body, footer};
        if (observed != Expected) {
            std::cerr << "word live Demo full/header/body/footer=" << std::hex
                      << full << '/' << header << '/' << body << '/' << footer
                      << std::dec << '\n';
            return false;
        }
        return true;
    }

    static bool findFullLiveAttractSeed() {
        constexpr std::array<std::string_view, WordGame::BoardCellCount> ExpectedWords = {
            "pep", "be", "yet", "sleep", "yet", "see",
            "help", "help", "help", "sleep", "she", "pep",
            "be", "see", "sleep", "yes", "sleep", "pep",
            "see", "see", "help", "", "we", "me",
            "me", "we", "said", "them", "yes", "sleep",
        };
        constexpr int ObscuredStartingCell = 21;
        constexpr int ExpectedTargetSound = 3;
        constexpr int ExpectedPlayerRow = 3;
        constexpr int ExpectedPlayerColumn = 3;

        const WordList words(loadEmbeddedResource(IDR_WM_WLIST));
        const WordConfig config(loadEmbeddedResource(IDR_WM_CFG));
        if (!words.valid() || !config.valid()) return false;

        std::vector<std::uint16_t> boardCandidates;
        int bestVisibleMatches = -1;
        std::vector<std::tuple<std::uint16_t, int, WordBoard>> bestBoards;
        for (std::uint32_t rawSeed = 0; rawSeed <= 0xffffu; ++rawSeed) {
            const auto seed = static_cast<std::uint16_t>(rawSeed);
            OriginalRandom random(seed);
            WordGameCore core(words, random);
            if (!core.configure(config)) return false;
            const int level = random.range(9) + 1;
            if (!core.startAtLevel(level, true)) return false;
            const WordBoard& board = core.board();
            if (board.targetSound != ExpectedTargetSound) continue;
            bool matches = true;
            int visibleMatches = 0;
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (index == ObscuredStartingCell) continue;
                if (board.cells[static_cast<std::size_t>(index)].record.text() ==
                    ExpectedWords[static_cast<std::size_t>(index)]) {
                    ++visibleMatches;
                } else {
                    matches = false;
                }
            }
            if (visibleMatches > bestVisibleMatches) {
                bestVisibleMatches = visibleMatches;
                bestBoards.clear();
            }
            if (visibleMatches == bestVisibleMatches && bestBoards.size() < 8) {
                bestBoards.emplace_back(seed, level, board);
            }
            if (matches) boardCandidates.push_back(seed);
        }

        if (boardCandidates.empty()) {
            std::cout << "word_full_attract_best_visible_matches="
                      << bestVisibleMatches << "/29\n";
            for (const auto& [seed, level, board] : bestBoards) {
                std::cout << "candidate seed=" << seed << " level=" << level
                          << " words=";
                for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                    if (index) std::cout << ',';
                    std::cout << board.cells[static_cast<std::size_t>(index)].record.text();
                }
                std::cout << '\n';
            }
        }

        std::vector<std::uint16_t> exactCandidates;
        for (const std::uint16_t seed : boardCandidates) {
            WordGame game(GraphicsMode::Vga256, seed);
            game.startAttract();
            if (game.core_.board().targetSound != ExpectedTargetSound ||
                game.playerRow_ != ExpectedPlayerRow ||
                game.playerColumn_ != ExpectedPlayerColumn) {
                continue;
            }
            bool matches = true;
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (index == ObscuredStartingCell) continue;
                if (game.core_.board().cells[static_cast<std::size_t>(index)].record.text() !=
                    ExpectedWords[static_cast<std::size_t>(index)]) {
                    matches = false;
                    break;
                }
            }
            if (matches && game.core_.eaten(ObscuredStartingCell)) {
                exactCandidates.push_back(seed);
                std::cout << "word_full_attract_seed=" << seed
                          << " level=" << game.core_.level()
                          << " calls=" << game.random_.calls
                          << " state=0x" << std::hex << game.random_.state
                          << std::dec << " obscured_word=\""
                          << game.core_.board().cells[ObscuredStartingCell].record.text()
                          << "\"\n";
            }
        }
        std::cout << "word_full_attract_board_candidates=" << boardCandidates.size()
                  << " exact_candidates=" << exactCandidates.size() << '\n';
        return exactCandidates.size() == 1;
    }

    static bool dumpFullLiveAttractSequence() {
        const char* directoryText =
            std::getenv("MUNCHERS_AUDIT_DUMP_WORD_ATTRACT_DIR");
        if (!directoryText || !*directoryText) return false;
        const std::filesystem::path directory(directoryText);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            std::cerr << "Could not create Word attract dump directory: "
                      << directory << " (" << error.message() << ")\n";
            return false;
        }

        constexpr std::uint16_t LiveSeed = 0x8e74u;
        constexpr int SampleCount = 11'000;
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        WordGame game(GraphicsMode::Vga256, LiveSeed);
        game.settingsPersistenceEnabled_ = false;
        game.startAttract();

        std::cout << "word_full_attract_initial action_ticks="
                  << game.attractActionTimer_ * WordSchedulerTicksPerSecond;
        for (int index = 0; index < game.safeZoneJobCount_; ++index) {
            const auto& job = game.safeZoneJobs_[static_cast<std::size_t>(index)];
            std::cout << " safe" << index << "_ticks="
                      << job.timer * WordSchedulerTicksPerSecond;
        }
        for (int index = 0; index < game.enemySlotCount_; ++index) {
            const auto& slot = game.enemySlots_[static_cast<std::size_t>(index)];
            std::cout << " enemy" << index << "_type=" << slot.type
                      << " enemy" << index << "_ticks="
                      << slot.timer * WordSchedulerTicksPerSecond;
        }
        std::cout << " cells=";
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (index) std::cout << ',';
            const auto& cell =
                game.core_.board().cells[static_cast<std::size_t>(index)];
            std::cout << cell.record.text() << ':' << cell.signedSourceIndex;
        }
        std::cout << '\n';

        std::ofstream states(directory / "word-attract-pages.tsv",
                             std::ios::binary | std::ios::trunc);
        if (!states) return false;
        states << "page\tsample\ttime_seconds\thash\tcontroller_page\tlevel"
                  "\trandom_calls\trandom_state\tplayer_row\tplayer_column"
                  "\tmoving\tmunching\tenemies\thall_phase\thall_wipe_frame"
                  "\tinterstitial\tinterstitial_frame\tenemy_slots\tactors"
                  "\tsafe_jobs\taction_ticks\tterminal_frame\tqueued_frames"
                  "\tmunch_safe_hold\tsafe_hold_ticks\tterminal_hold_ticks"
                  "\tcannibal_hold\tpost_feedback\tcell_state\n";
        std::ofstream sampleStates(directory / "word-attract-samples.tsv",
                                   std::ios::binary | std::ios::trunc);
        if (!sampleStates) return false;
        sampleStates << "sample\tpage\ttick_fraction\tplayer_row\tplayer_column"
                        "\tmoving\tmunching\tterminal_frame\tmunch_ticks"
                        "\taction_ticks\tsafe_jobs\tenemy_slots\tactors\n";

        Renderer renderer(GraphicsMode::Vga256);
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
            sampleStates << sample << '\t' << static_cast<int>(game.page_) << '\t'
                         << game.gameplayTickAccumulator_ << '\t'
                         << game.playerRow_ << '\t' << game.playerColumn_ << '\t'
                         << game.moving_ << '\t' << game.munching_ << '\t'
                         << game.playerTerminalFrame_ << '\t'
                         << game.munchTimer_ * WordSchedulerTicksPerSecond << '\t'
                         << game.attractActionTimer_ * WordSchedulerTicksPerSecond
                         << '\t';
            for (int index = 0; index < game.safeZoneJobCount_; ++index) {
                const auto& job =
                    game.safeZoneJobs_[static_cast<std::size_t>(index)];
                if (index) sampleStates << ';';
                sampleStates << index << ':' << job.active << ':' << job.cellIndex
                             << ':' << job.timer * WordSchedulerTicksPerSecond;
            }
            sampleStates << '\t';
            for (int index = 0; index < game.enemySlotCount_; ++index) {
                const auto& slot =
                    game.enemySlots_[static_cast<std::size_t>(index)];
                if (index) sampleStates << ';';
                sampleStates << index << ':' << slot.type << ':'
                             << static_cast<int>(slot.phase) << ':'
                             << slot.timer * WordSchedulerTicksPerSecond;
            }
            sampleStates << '\t';
            for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                const auto& enemy = game.enemies_[index];
                if (index) sampleStates << ';';
                sampleStates << enemy.slot << ':' << enemy.type << ':'
                             << enemy.row << ':' << enemy.column << ':'
                             << enemy.direction << ':' << enemy.moving << ':'
                             << enemy.entering << ':' << enemy.exiting << ':'
                             << enemy.cannibalizing << ':'
                             << enemy.animationTimer * WordSchedulerTicksPerSecond
                             << ':' << enemy.moveTimer * WordSchedulerTicksPerSecond;
            }
            sampleStates << '\n';
            game.renderPresentation(renderer);
            const std::uint64_t hash = fullFrameHash(renderer);
            if (pageIndex == 0 || hash != previousHash) {
                std::string number = std::to_string(pageIndex);
                number.insert(number.begin(), 5u - std::min<std::size_t>(5u, number.size()), '0');
                if (!writeFrame(directory / ("attract-page-" + number + ".ppm"))) {
                    return false;
                }
                states << pageIndex << '\t' << sample << '\t'
                       << sample * SampleSeconds << "\t0x" << std::hex << hash
                       << std::dec << '\t' << static_cast<int>(game.page_) << '\t'
                       << game.core_.level() << '\t' << game.random_.calls << "\t0x"
                       << std::hex << game.random_.state << std::dec << '\t'
                       << game.playerRow_ << '\t' << game.playerColumn_ << '\t'
                       << game.moving_ << '\t' << game.munching_ << '\t'
                       << game.enemies_.size() << '\t'
                       << static_cast<int>(game.attractHallTransitionPhase_) << '\t'
                       << game.attractHallWipeFrame_ << '\t'
                       << static_cast<int>(game.attractInterstitialTransition_) << '\t'
                       << game.attractInterstitialFrame_ << '\t';
                for (int index = 0; index < game.enemySlotCount_; ++index) {
                    const auto& slot =
                        game.enemySlots_[static_cast<std::size_t>(index)];
                    if (index) states << ';';
                    states << index << ':' << slot.type << ':'
                           << static_cast<int>(slot.phase) << ':'
                           << slot.timer * WordSchedulerTicksPerSecond;
                }
                states << '\t';
                for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                    const auto& enemy = game.enemies_[index];
                    if (index) states << ';';
                    states << enemy.slot << ':' << enemy.type << ':'
                           << enemy.row << ':' << enemy.column << ':'
                           << enemy.direction << ':' << enemy.moving << ':'
                           << enemy.entering << ':' << enemy.exiting << ':'
                           << enemy.cannibalizing << ':'
                           << enemy.animationTimer * WordSchedulerTicksPerSecond
                           << ':' << enemy.moveTimer * WordSchedulerTicksPerSecond;
                }
                states << '\t';
                for (int index = 0; index < game.safeZoneJobCount_; ++index) {
                    const auto& job =
                        game.safeZoneJobs_[static_cast<std::size_t>(index)];
                    if (index) states << ';';
                    states << index << ':' << job.active << ':' << job.cellIndex
                           << ':' << job.timer * WordSchedulerTicksPerSecond;
                }
                states << '\t'
                       << game.attractActionTimer_ * WordSchedulerTicksPerSecond
                       << '\t' << game.playerTerminalFrame_
                       << '\t' << game.presentationFrames_.size()
                       << '\t' << !game.attractMunchSafeZoneHoldPixels_.empty()
                       << '\t' << game.attractSafeZoneEnemyHoldTicks_
                       << '\t' << game.attractPlayerTerminalEnemyHoldTicks_
                       << '\t' << !game.cannibalPlayerPresentationHoldPixels_.empty()
                       << '\t' << game.attractPostFeedbackBoard_
                       << '\t';
                for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                    if (index) states << ';';
                    const std::size_t cellIndex = static_cast<std::size_t>(index);
                    states << game.core_.board().cells[cellIndex].signedSourceIndex
                           << ':' << game.core_.eaten(cellIndex);
                }
                states << '\n';
                previousHash = hash;
                ++pageIndex;
            }
            game.update(SampleSeconds);
        }
        std::cout << "word_full_attract_seed=" << LiveSeed
                  << " samples=" << SampleCount
                  << " changed_pages=" << pageIndex
                  << " final_calls=" << game.random_.calls
                  << " final_state=0x" << std::hex << game.random_.state
                  << std::dec << '\n';
        return true;
    }

    static bool usesRecoveredWordTroggleTables() {
        constexpr std::array<int, 12> counts = {1,1,1,2,2,2,2,3,3,3,3,3};
        constexpr std::array<int, 12> safeJobs = {2,2,2,2,2,2,2,1,1,1,1,1};
        constexpr std::array<int, 12> initialSafe = {1,1,1,0,0,0,0,0,0,0,0,0};
        constexpr std::array<std::array<int, 5>, 12> weights = {{
            {{100,0,0,0,0}}, {{90,0,10,0,0}}, {{80,0,10,10,0}},
            {{70,10,10,10,0}}, {{60,10,10,10,10}}, {{50,15,10,15,10}},
            {{45,15,15,15,10}}, {{40,15,15,15,15}}, {{35,15,20,15,15}},
            {{30,15,20,20,15}}, {{25,20,20,20,15}}, {{20,20,20,20,20}},
        }};
        for (int tier = 0; tier < 12; ++tier) {
            if (WordGame::maximumEnemiesForPressure(tier) != counts[static_cast<std::size_t>(tier)] ||
                WordGame::maximumSafeZoneJobsForPressure(tier) != safeJobs[static_cast<std::size_t>(tier)] ||
                WordGame::initialSafeZonesForPressure(tier) != initialSafe[static_cast<std::size_t>(tier)]) {
                return false;
            }
            int total = 0;
            for (int type = 0; type < 5; ++type) {
                const int value = WordGame::enemyWeightForPressure(tier, type);
                if (value != weights[static_cast<std::size_t>(tier)][static_cast<std::size_t>(type)]) {
                    return false;
                }
                total += value;
            }
            if (total != 100) return false;
        }
        return WordGame::maximumEnemiesForPressure(99) == 3 &&
               WordGame::enemyWeightForPressure(99, 4) == 20;
    }

    static bool retriesProtectedTurnsWithoutSyntheticCap() {
        WordGame game(GraphicsMode::Vga256, 57606u);
        game.page_ = WordGamePage::Title;
        game.safeCells_.fill(false);
        game.safeCells_[static_cast<std::size_t>(2 * WordGame::BoardColumns + 1)] = true;
        game.safeCells_[static_cast<std::size_t>(2 * WordGame::BoardColumns + 3)] = true;
        game.safeCells_[static_cast<std::size_t>(3 * WordGame::BoardColumns + 2)] = true;

        WordGame::Enemy reggie;
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

    static bool usesRecoveredOrdinaryTroggleFrames(WordGame& game) {
        WordGame::Enemy enemy;
        for (int direction = 0; direction < 4; ++direction) {
            enemy.direction = direction;
            enemy.entering = true;
            enemy.moving = false;
            if (WordGame::ordinaryEnemyFrame(enemy) != direction * 3) return false;
            enemy.moving = true;
            if (WordGame::ordinaryEnemyFrame(enemy) != direction * 3) return false;
            enemy.entering = false;
            if (WordGame::ordinaryEnemyFrame(enemy) != direction * 3 + 2) return false;
            enemy.moving = false;
            const int expectedDwell = direction == 2 ? 15 : direction * 3 + 1;
            if (WordGame::ordinaryEnemyFrame(enemy) != expectedDwell) return false;
        }
        constexpr std::array<int, 21> BiteFrames = {
            12, 13, 14, 13, 12, 13, 12, 13, 14, 13, 12,
            13, 12, 13, 14, 13, 12, 13, 12, 13, 14
        };
        for (int tick = 0; tick < static_cast<int>(BiteFrames.size()); ++tick) {
            if (WordGame::troggleBiteFrameAtTick(tick) !=
                BiteFrames[static_cast<std::size_t>(tick)]) return false;
        }
        if (WordGame::troggleBiteFrameAtTick(-1) != 12 ||
            WordGame::troggleBiteFrameAtTick(21) != 13) return false;

        const double interval = 3.0 / WordSchedulerTicksPerSecond;
        WordGame::Enemy vertical;
        vertical.direction = 0;
        vertical.moving = true;
        const double verticalDuration = game.enemyMoveAnimationDuration(0);
        for (int phase = 1; phase <= 5; ++phase) {
            vertical.animationTimer = verticalDuration - (phase - 1) * interval;
            if (game.enemyMovementPhase(vertical) != phase) return false;
        }
        WordGame::Enemy horizontal;
        horizontal.direction = 1;
        horizontal.moving = true;
        const double horizontalDuration = game.enemyMoveAnimationDuration(1);
        for (int phase = 1; phase <= 6; ++phase) {
            horizontal.animationTimer = horizontalDuration - (phase - 1) * interval;
            if (game.enemyMovementPhase(horizontal) != phase) return false;
        }
        vertical.entering = true;
        vertical.animationTimer = verticalDuration;
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

    static bool usesSharedCallbackIntermediatePresentation() {
        WordGame game(GraphicsMode::Vga256, 0x5e71u);
        game.startGame(10);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;

        int correctCell = -1;
        int remainingCorrect = 0;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            const std::size_t cellIndex = static_cast<std::size_t>(index);
            if (!game.core_.eaten(cellIndex) &&
                game.core_.board().cells[cellIndex].signedSourceIndex > 0) {
                if (correctCell < 0) correctCell = index;
                ++remainingCorrect;
            }
        }
        if (correctCell < 0 || remainingCorrect < 2) return false;
        game.playerRow_ = correctCell / WordGame::BoardColumns;
        game.playerColumn_ = correctCell % WordGame::BoardColumns;
        game.beginMunch();
        if (!game.munching_) return false;

        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        game.munchTimer_ = TickSeconds;
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].type = 1;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        WordGame::Enemy worker;
        worker.row = 4;
        worker.column = 5;
        worker.fromRow = 4;
        worker.fromColumn = 6;
        worker.direction = 3;
        worker.type = 1;
        worker.slot = 0;
        worker.entering = true;
        worker.moving = true;
        const double interval = game.enemyMoveAnimationDuration(3) / 6.0;
        const double total = game.enemyMoveAnimationDuration(3);
        worker.animationTimer = total - (2.0 * interval - TickSeconds);
        game.enemies_ = {worker};
        if (game.enemyMovementPhase(game.enemies_[0]) != 2) return false;

        game.update(TickSeconds + 1e-12);
        if (game.munching_ || game.playerTerminalFrame_ != 13 ||
            game.enemyMovementPhase(game.enemies_[0]) != 3 ||
            game.presentationFrames_.size() != 1) {
            std::cerr << "word callback state munch=" << game.munching_
                      << " terminal=" << game.playerTerminalFrame_
                      << " phase=" << game.enemyMovementPhase(game.enemies_[0])
                      << " queued=" << game.presentationFrames_.size() << '\n';
            return false;
        }

        Renderer terminal(GraphicsMode::Vga256);
        game.render(terminal);
        const int terminalFrame = game.playerTerminalFrame_;
        game.playerTerminalFrame_ = WordGame::munchFrameAtTick(
            WordGame::MunchAnimationTicks - 1);
        Renderer expectedIntermediate(GraphicsMode::Vga256);
        game.render(expectedIntermediate);
        game.playerTerminalFrame_ = terminalFrame;
        if (expectedIntermediate.pixels() == terminal.pixels()) return false;

        Renderer presented(GraphicsMode::Vga256);
        game.renderPresentation(presented);
        if (presented.pixels() != expectedIntermediate.pixels() ||
            !game.presentationFrames_.empty()) {
            std::cerr << "word callback presented=" << std::hex
                      << fullFrameHash(presented) << " expected="
                      << fullFrameHash(expectedIntermediate) << std::dec
                      << " queued=" << game.presentationFrames_.size() << '\n';
            return false;
        }
        game.renderPresentation(presented);
        return presented.pixels() == terminal.pixels();
    }

    static bool usesRecoveredPlayerMovement(WordGame& game) {
        constexpr std::array<int, 6> FrameOffsets = {2, 2, 1, 0, 1, 2};
        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        for (int direction = 0; direction < 4; ++direction) {
            const bool vertical = direction == 0 || direction == 2;
            const int stepCount = vertical ? 5 : 6;
            const double duration = WordGame::playerMoveAnimationDuration(direction);
            if (std::abs(duration - stepCount * TickSeconds) > 1e-12) return false;

            game.moving_ = true;
            game.moveDirection_ = direction;
            for (int phase = 0; phase < stepCount; ++phase) {
                game.moveTimer_ = duration - phase * TickSeconds;
                if (game.playerMovementPhase() != phase ||
                    WordGame::playerMoveFrameAtPhase(direction, phase) !=
                        direction * 3 + FrameOffsets[static_cast<std::size_t>(phase)]) {
                    return false;
                }
            }
        }

        game.page_ = WordGamePage::Playing;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        disableBoardJobs(game);
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

    static bool liveWordMovementAndChewCompositeMatches() {
        WordGame game(GraphicsMode::Vga256, 0x574du);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 3;
        game.playerColumn_ = 3;
        game.safeCells_.fill(false);
        game.safeCells_[21] = true;

        const auto record = [](const std::string_view text) {
            WordRecord result;
            std::copy_n(text.begin(), std::min(text.size(), result.bytes.size()),
                        result.bytes.begin());
            return result;
        };
        WordBoardCell blank = game.core_.board().cells[21];
        WordBoardCell said{record("said"), true, 1};
        if (!game.core_.restoreCell(21, blank, true) ||
            !game.core_.restoreCell(22, said, false)) return false;

        const auto hash = [](const Renderer& renderer) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 116; y < 146; ++y) {
                for (int x = 164; x < 260; ++x) {
                    value ^= renderer.pixels()[
                        static_cast<std::size_t>(y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };
        Renderer renderer(GraphicsMode::Vga256);
        game.renderBoard(renderer);
        if (hash(renderer) != 0xa267649814989b5bull) {
            std::cerr << "word live composite: standing 0x" << std::hex
                      << hash(renderer) << std::dec << '\n';
            return false;
        }

        game.moveFromRow_ = 3;
        game.moveFromColumn_ = 3;
        game.moveToRow_ = 3;
        game.moveToColumn_ = 4;
        game.moveDirection_ = 1;
        game.moving_ = true;
        const double duration = game.playerMoveAnimationDuration(1);
        // Capture frame 2465 contains a one-refresh partial dirty-cell update:
        // only the top six rows have lost 20 safe-corner and two grid pixels.
        // Assert the complete logical phase-1 composition, then every stable
        // captured phase verbatim. Reproducing that torn presenter frame would
        // reintroduce the class of partial-frame flicker the native port avoids.
        constexpr std::array<std::uint64_t, 5> ComposedMoving = {
            0x9198cd622d02e095ull,
            0x15eb545083d73b62ull,
            0x09f369bae8a91a8dull,
            0xf7b39e834458194aull,
            0x5580298d6bac0860ull,
        };
        for (int phase = 1; phase <= 5; ++phase) {
            game.moveTimer_ = duration -
                phase / WordSchedulerTicksPerSecond;
            game.renderBoard(renderer);
            const std::uint64_t actual = hash(renderer);
            if (actual != ComposedMoving[static_cast<std::size_t>(phase - 1)]) {
                std::cerr << "word live composite: move phase " << phase
                          << " 0x" << std::hex << actual << std::dec << '\n';
                return false;
            }
        }

        game.moving_ = false;
        game.moveTimer_ = 0.0;
        game.playerColumn_ = 4;
        game.playerTerminalFrame_ = 4;
        game.renderBoard(renderer);
        if (hash(renderer) != 0x1b03b04a282ebb3full) {
            std::cerr << "word live composite: movement terminal 0x" << std::hex
                      << hash(renderer) << std::dec << '\n';
            return false;
        }
        game.playerTerminalFrame_ = -1;
        game.renderBoard(renderer);
        if (hash(renderer) != 0x75582701ffe799e6ull) {
            std::cerr << "word live composite: arrived standing 0x" << std::hex
                      << hash(renderer) << std::dec << '\n';
            return false;
        }

        if (!game.core_.beginMunchCell(22)) return false;
        game.munching_ = true;
        game.munchCellIndex_ = 22;
        constexpr std::array<std::uint64_t, WordGame::MunchAnimationTicks>
            LiveChew = {
                0xa81f55d6192d6bb3ull, 0x0cb9ab674660d563ull,
                0xa81f55d6192d6bb3ull, 0x0cb9ab674660d563ull,
                0xa81f55d6192d6bb3ull, 0x0cb9ab674660d563ull,
                0xa81f55d6192d6bb3ull,
            };
        for (int phase = 0; phase < WordGame::MunchAnimationTicks; ++phase) {
            game.munchTimer_ = WordGame::MunchAnimationDuration -
                phase / WordSchedulerTicksPerSecond;
            game.renderBoard(renderer);
            const std::uint64_t actual = hash(renderer);
            if (actual != LiveChew[static_cast<std::size_t>(phase)]) {
                std::cerr << "word live composite: chew phase " << phase
                          << " 0x" << std::hex << actual << std::dec << '\n';
                return false;
            }
        }
        return true;
    }

    static bool liveWordWm011BoardAndTroggleEntryMatch() {
        // The previously unaudited lossless wm_011 recording supplies an organic
        // second user board plus pause, quit, vertical movement, safe-zone,
        // warning, and top-edge Reggie-entry states. Reconstruct its logical
        // board directly so these gates do not depend on the recording's
        // time-derived PRNG seed.
        WordGame game(GraphicsMode::Vga256, 0x5711u);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 1;
        game.playerColumn_ = 2;
        game.playerTerminalFrame_ = -1;
        game.playerRecovering_ = false;

        constexpr std::array<std::string_view, WordGame::BoardCellCount> Words = {
            "yet", "them", "we", "them", "he", "get",
            "yes", "me", "", "get", "be", "yes",
            "pep", "pep", "help", "she", "he", "get",
            "sleep", "help", "he", "red", "see", "she",
            "me", "she", "she", "red", "me", "them",
        };
        const auto record = [](const std::string_view text) {
            WordRecord result;
            std::copy_n(text.begin(), std::min(text.size(), result.bytes.size()),
                        result.bytes.begin());
            return result;
        };
        auto& board = const_cast<WordBoard&>(game.core_.board());
        board.targetSound = 2; // /e/ as in tree, with the macron overstrike.
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            const std::string_view word = Words[static_cast<std::size_t>(index)];
            const WordBoardCell cell{record(word), !word.empty(), word.empty() ? 0 : 1};
            if (!game.core_.restoreCell(static_cast<std::size_t>(index), cell,
                                        word.empty())) return false;
        }
        auto& score = const_cast<MuncherScore&>(game.core_.scoreState());
        score.restore(0, 3);
        game.safeCells_.fill(false);
        game.safeCells_[8] = true;

        Renderer renderer(GraphicsMode::Vga256);
        const auto stateHash = [&](const std::string_view name) {
            game.render(renderer);
            maybeDumpWordOptionsFrame(
                renderer, "wm011-" + std::string(name) + "-native.ppm");
            return fullFrameHash(renderer);
        };
        const auto expect = [&](const std::string_view name,
                                const std::uint64_t expected) {
            const std::uint64_t actual = stateHash(name);
            if (actual == expected) return true;
            std::cerr << "wm011 " << name << " hash=0x" << std::hex << actual
                      << " expected=0x" << expected << std::dec << '\n';
            return false;
        };

        if (!expect("initial-safe-board", 0xc4364d11e662b99full)) return false;
        game.page_ = WordGamePage::Paused;
        if (!expect("paused", 0x060ad563de6f0ab9ull)) return false;
        game.page_ = WordGamePage::QuitConfirm;
        game.menuSelection_ = 1;
        if (!expect("quit-no", 0x1b87b07181b63185ull)) return false;

        game.page_ = WordGamePage::Playing;
        game.moveFromRow_ = 1;
        game.moveFromColumn_ = 2;
        game.moveToRow_ = 2;
        game.moveToColumn_ = 2;
        game.moveDirection_ = 2;
        game.moving_ = true;
        const double moveDuration = game.playerMoveAnimationDuration(2);
        constexpr std::array<std::uint64_t, 4> Moving = {
            0xd9d7272c2a7b27f3ull, 0x4d368ee30abaddb8ull,
            0x39ed8adad1b97658ull, 0x7e6f2626c9aabe9full,
        };
        for (int phase = 1; phase <= 4; ++phase) {
            game.moveTimer_ = moveDuration - phase / WordSchedulerTicksPerSecond;
            if (!expect("down-phase-" + std::to_string(phase),
                        Moving[static_cast<std::size_t>(phase - 1)])) return false;
        }
        game.moving_ = false;
        game.moveTimer_ = 0.0;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.playerTerminalFrame_ = 8;
        if (!expect("down-terminal", 0x557b5a0efb81f2baull)) return false;
        game.playerTerminalFrame_ = -1;
        if (!expect("down-standing-safe", 0xb19c10dfeea1a0c2ull)) return false;

        game.safeCells_[8] = false;
        if (!expect("safe-off", 0x2538522157185e2aull)) return false;
        game.enemyWarning_ = true;
        if (!expect("warning", 0x32ce10ea97a25480ull)) return false;
        game.enemyWarning_ = false;

        WordGame::Enemy entry;
        entry.row = 0;
        entry.column = 0;
        entry.fromRow = -1;
        entry.fromColumn = 0;
        entry.direction = 2;
        entry.type = 0;
        entry.slot = 0;
        entry.entering = true;
        entry.moving = false;
        game.enemies_ = {entry};
        game.enemyWarning_ = true;
        if (!expect("warning-with-pending-entry", 0x32ce10ea97a25480ull)) {
            return false;
        }
        game.enemyWarning_ = false;
        if (!expect("warning-removed-pending-entry", 0x2538522157185e2aull)) {
            return false;
        }
        game.enemies_[0].moving = true;
        const double interval = 3.0 / WordSchedulerTicksPerSecond;
        const double total = game.enemyMoveAnimationDuration(2);
        constexpr std::array<std::uint64_t, 5> Entry = {
            0xe276384d24494e1dull, 0xfce51c140f43d0d5ull,
            0x35de7193323d8017ull, 0xe1966208d455be5dull,
            0x9811b24a078483f9ull,
        };
        game.enemies_[0].animationTimer = total;
        if (game.enemyMovementPhase(game.enemies_[0]) != 1 ||
            !expect("entry-hidden-phase-1", 0x2538522157185e2aull)) return false;
        for (int phase = 2; phase <= 5; ++phase) {
            game.enemies_[0].animationTimer = total - (phase - 1) * interval;
            if (phase == 4) game.safeCells_[20] = true;
            if (!expect("entry-phase-" + std::to_string(phase),
                        Entry[static_cast<std::size_t>(phase - 2)])) return false;
            if (phase == 3) {
                game.safeCells_[20] = true;
                if (!expect("entry-phase-3-safe-on", 0x195538ad19d455e5ull)) {
                    return false;
                }
                game.safeCells_[20] = false;
            }
        }
        game.enemies_[0].moving = false;
        game.enemies_[0].entering = false;
        game.enemies_[0].animationTimer = 0.0;
        if (!expect("entry-terminal-dwell", Entry[4])) return false;
        return true;
    }

    static bool usesCapturedBottomBashfulEntryGeometry() {
        // Number's lossless first-Demo frames 2866-2918 expose a bottom
        // Bashful arrival using the byte-identical Word actor resources and
        // board painter. Gate the shared palette, y=177 viewport, retained
        // y=176 rule damage, and dwell restoration in this runtime too.
        WordGame game(GraphicsMode::Vga256, 0x5ba5u);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 0;
        game.playerColumn_ = 5;
        game.safeCells_.fill(false);
        game.enemies_.clear();

        WordGame::Enemy bashful;
        bashful.row = 4;
        bashful.column = 0;
        bashful.fromRow = 5;
        bashful.fromColumn = 0;
        bashful.direction = 0;
        bashful.type = 2;
        bashful.slot = 0;
        bashful.entering = true;
        bashful.moving = true;
        game.enemies_.push_back(bashful);

        const auto geometry = [](const Renderer& renderer) {
            std::array<int, 5> cyan{70, 200, -1, -1, 0};
            int bottomBlue = 0;
            int bottomMagenta = 0;
            int outerMagenta = 0;
            for (int y = 150; y < 200; ++y) {
                for (int x = 0; x < 70; ++x) {
                    const std::uint32_t pixel = renderer.pixels()[
                        static_cast<std::size_t>(y * Renderer::Width + x)] &
                        0xffffffu;
                    if (pixel == 0x5dbeffu) {
                        cyan[0] = std::min(cyan[0], x);
                        cyan[1] = std::min(cyan[1], y);
                        cyan[2] = std::max(cyan[2], x);
                        cyan[3] = std::max(cyan[3], y);
                        ++cyan[4];
                    }
                }
            }
            for (int x = 24; x <= 64; ++x) {
                if ((renderer.pixels()[static_cast<std::size_t>(
                         176 * Renderer::Width + x)] & 0xffffffu) ==
                    Colors::BoardBlue) ++bottomBlue;
            }
            for (int x = 20; x <= 68; ++x) {
                if ((renderer.pixels()[static_cast<std::size_t>(
                         176 * Renderer::Width + x)] & 0xffffffu) ==
                    Colors::Magenta) ++bottomMagenta;
            }
            for (int x = 18; x <= 310; ++x) {
                if ((renderer.pixels()[static_cast<std::size_t>(
                         178 * Renderer::Width + x)] & 0xffffffu) ==
                    Colors::Magenta) ++outerMagenta;
            }
            return std::tuple{cyan, bottomBlue, bottomMagenta, outerMagenta};
        };

        constexpr std::array<std::array<int, 5>, 4> ExpectedCyan = {{
            {{33, 170, 41, 177, 31}},
            {{31, 164, 41, 177, 60}},
            {{32, 158, 42, 177, 78}},
            {{30, 153, 40, 171, 74}},
        }};
        constexpr std::array<int, 4> ExpectedBlue = {19, 21, 37, 41};
        const double interval = 3.0 / WordSchedulerTicksPerSecond;
        const double total = game.enemyMoveAnimationDuration(0);
        Renderer renderer(GraphicsMode::Vga256);
        for (int phase = 2; phase <= 5; ++phase) {
            game.enemies_[0].animationTimer = total - (phase - 1) * interval;
            if (game.enemyMovementPhase(game.enemies_[0]) != phase) return false;
            game.renderBoard(renderer);
            const auto [cyan, blue, magenta, outer] = geometry(renderer);
            const std::size_t index = static_cast<std::size_t>(phase - 2);
            if (cyan != ExpectedCyan[index] || blue != ExpectedBlue[index] ||
                magenta != 8 || outer != 293) {
                std::cerr << "word shared bottom-entry phase " << phase
                          << " geometry=" << cyan[0] << ',' << cyan[1] << ','
                          << cyan[2] << ',' << cyan[3] << ',' << cyan[4]
                          << " blue/magenta/outer=" << blue << '/' << magenta
                          << '/' << outer << '\n';
                return false;
            }
        }

        game.enemies_[0].moving = false;
        game.enemies_[0].entering = false;
        game.enemies_[0].animationTimer = 0.0;
        game.renderBoard(renderer);
        const auto [cyan, blue, magenta, outer] = geometry(renderer);
        return cyan == std::array<int, 5>{32, 152, 42, 173, 80} &&
               blue == 0 && magenta == 49 && outer == 293;
    }

    static bool usesCapturedConcurrentRightEntryPhaseSixRule() {
        // The seeded Word source distinguishes two otherwise identical
        // right-entry phase-6 boundaries. With the player idle, source run
        // 736 retains blue damage on x=308; with a concurrent leftward player
        // callback, run 263 restores the complete 29-pixel magenta rule. The
        // latter differs from the old native page 221 at those pixels only.
        WordGame game(GraphicsMode::Vga256, 0x8e74u);
        game.startGame(6);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = true;
        game.safeCells_.fill(false);
        game.playerRow_ = 0;
        game.playerColumn_ = 3;
        game.playerTerminalFrame_ = -1;
        game.enemies_.clear();

        WordGame::Enemy entry;
        entry.row = 3;
        entry.column = 5;
        entry.fromRow = 3;
        entry.fromColumn = 6;
        entry.direction = 3;
        entry.type = 2;
        entry.slot = 0;
        entry.entering = true;
        entry.moving = true;
        const double duration = game.enemyMoveAnimationDuration(3);
        entry.animationTimer = duration / 6.0;
        game.enemies_.push_back(entry);
        if (game.enemyMovementPhase(game.enemies_[0]) != 6) return false;

        const auto ruleMatches = [](const Renderer& renderer,
                                    const std::uint32_t expected) {
            for (int y = 117; y <= 145; ++y) {
                if ((renderer.pixels()[static_cast<std::size_t>(
                         y * Renderer::Width + WordGame::BoardRight)] &
                     0xffffffu) != expected) {
                    return false;
                }
            }
            return true;
        };

        Renderer renderer(GraphicsMode::Vga256);
        game.moving_ = false;
        game.renderBoard(renderer);
        if (!ruleMatches(renderer, Colors::BoardBlue)) {
            std::cerr << "word idle right-entry phase 6 did not retain x=308\n";
            return false;
        }

        game.moveFromRow_ = 0;
        game.moveFromColumn_ = 3;
        game.moveToRow_ = 0;
        game.moveToColumn_ = 2;
        game.moveDirection_ = 3;
        game.moving_ = true;
        game.moveTimer_ = game.playerMoveAnimationDuration(3) -
                          3.0 / WordSchedulerTicksPerSecond;
        if (game.playerMovementPhase() != 3) return false;
        game.renderBoard(renderer);
        if (!ruleMatches(renderer, Colors::BoardBlue)) {
            std::cerr << "word phase-3/right-entry overlap did not retain x=308\n";
            return false;
        }

        game.moveTimer_ = game.playerMoveAnimationDuration(3) -
                          4.0 / WordSchedulerTicksPerSecond;
        if (game.playerMovementPhase() != 4) return false;
        game.renderBoard(renderer);
        if (!ruleMatches(renderer, Colors::Magenta)) {
            std::cerr << "word concurrent right-entry phase 6 did not restore x=308\n";
            return false;
        }
        return true;
    }

    static bool retainsSafeOutlineOnFirstConcurrentAttractChew() {
        // Source run 139 differs from old native page 109 only at the 68 white
        // pixels of the safe marker removed by the earlier selector-6 job.
        // The first selector-5 chew page retains that outline; chew tick 1 does
        // not. Exercise the actual scheduler ordering rather than a render-only
        // fixture so the lifetime of the held resident page is locked too.
        WordGame game(GraphicsMode::Vga256, 0x8e74u);
        game.startGame(6);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;
        game.playerTerminalFrame_ = -1;

        constexpr int SafeCell = 8;
        int chewCell = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (index != SafeCell && !game.core_.eaten(static_cast<std::size_t>(index)) &&
                game.core_.board().cells[static_cast<std::size_t>(index)]
                        .signedSourceIndex > 0) {
                chewCell = index;
                break;
            }
        }
        if (chewCell < 0) return false;
        game.playerRow_ = chewCell / WordGame::BoardColumns;
        game.playerColumn_ = chewCell % WordGame::BoardColumns;
        game.safeZoneJobCount_ = 1;
        game.safeCells_[SafeCell] = true;
        auto& safeJob = game.safeZoneJobs_[0];
        safeJob.active = true;
        safeJob.cellIndex = SafeCell;
        safeJob.period = 211.0 / WordSchedulerTicksPerSecond;
        safeJob.timer = 1.0 / WordSchedulerTicksPerSecond;
        game.attractActionTimer_ = 0.0;
        game.gameplayTickAccumulator_ = 0.0;

        const auto markerPixels = [](const Renderer& renderer) {
            constexpr int x = WordGame::BoardLeft +
                              2 * WordGame::BoardCellWidth;
            constexpr int y = WordGame::BoardTop +
                              1 * WordGame::BoardCellHeight;
            int count = 0;
            for (int py = y + 1; py <= y + WordGame::BoardCellHeight - 1; ++py) {
                for (int px = x + 1; px <= x + WordGame::BoardCellWidth - 1; ++px) {
                    const bool cornerArm =
                        ((py == y + 1 || py == y + WordGame::BoardCellHeight - 1) &&
                         (px <= x + 9 || px >= x + WordGame::BoardCellWidth - 9)) ||
                        ((px == x + 1 || px == x + WordGame::BoardCellWidth - 1) &&
                         (py <= y + 9 || py >= y + WordGame::BoardCellHeight - 9));
                    if (cornerArm &&
                        (renderer.pixels()[static_cast<std::size_t>(
                             py * Renderer::Width + px)] & 0xffffffu) ==
                            Colors::White) {
                        ++count;
                    }
                }
            }
            return count;
        };

        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (game.safeCells_[SafeCell] || !game.munching_ ||
            game.attractMunchSafeZoneHoldPixels_.empty()) {
            return false;
        }
        Renderer logical(GraphicsMode::Vga256);
        game.renderBoard(logical);
        if (markerPixels(logical) != 0) return false;
        Renderer held(GraphicsMode::Vga256);
        game.renderPresentation(held);
        if (markerPixels(held) != 68) return false;

        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        Renderer next(GraphicsMode::Vga256);
        game.renderPresentation(next);
        return markerPixels(next) == 0 &&
               game.attractMunchSafeZoneHoldPixels_.empty();
    }

    static bool delaysSafeZonePaintBehindActiveEnemyJobs() {
        WordGame game(GraphicsMode::Vga256, 0x8e75u);
        game.startGame(6);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;
        game.attractActionTimer_ = 20.0 / WordSchedulerTicksPerSecond;
        game.playerTerminalFrame_ = -1;

        constexpr int SafeCell = 15;
        game.safeZoneJobCount_ = 1;
        game.safeCells_[SafeCell] = true;
        auto& safeJob = game.safeZoneJobs_[0];
        safeJob.active = true;
        safeJob.cellIndex = SafeCell;
        safeJob.period = 211.0 / WordSchedulerTicksPerSecond;
        safeJob.timer = 1.0 / WordSchedulerTicksPerSecond;

        WordGame::Enemy enemy;
        enemy.row = 2;
        enemy.column = 5;
        enemy.fromRow = 2;
        enemy.fromColumn = 4;
        enemy.direction = 1;
        enemy.type = 0;
        enemy.slot = 0;
        enemy.moving = true;
        enemy.animationTimer = game.enemyMoveAnimationDuration(1);
        game.enemies_ = {enemy};
        game.enemySlotCount_ = 1;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].timer = 0.0;
        game.gameplayTickAccumulator_ = 0.0;

        const auto markerPixels = [](const Renderer& renderer) {
            constexpr int x = WordGame::BoardLeft +
                              3 * WordGame::BoardCellWidth;
            constexpr int y = WordGame::BoardTop +
                              2 * WordGame::BoardCellHeight;
            int count = 0;
            for (int py = y + 1; py <= y + WordGame::BoardCellHeight - 1; ++py) {
                for (int px = x + 1; px <= x + WordGame::BoardCellWidth - 1; ++px) {
                    const bool cornerArm =
                        ((py == y + 1 || py == y + WordGame::BoardCellHeight - 1) &&
                         (px <= x + 9 || px >= x + WordGame::BoardCellWidth - 9)) ||
                        ((px == x + 1 || px == x + WordGame::BoardCellWidth - 1) &&
                         (py <= y + 9 || py >= y + WordGame::BoardCellHeight - 9));
                    if (cornerArm &&
                        (renderer.pixels()[static_cast<std::size_t>(
                             py * Renderer::Width + px)] & 0xffffffu) ==
                            Colors::White) {
                        ++count;
                    }
                }
            }
            return count;
        };

        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (game.safeCells_[SafeCell] ||
            game.attractSafeZoneEnemyHoldTicks_ != 3) {
            return false;
        }
        for (int heldTick = 0; heldTick < 3; ++heldTick) {
            Renderer held(GraphicsMode::Vga256);
            game.renderPresentation(held);
            if (markerPixels(held) != 68) return false;
            if (heldTick != 2) {
                game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
            }
        }
        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        Renderer released(GraphicsMode::Vga256);
        game.renderPresentation(released);
        return markerPixels(released) == 0 &&
               game.attractSafeZoneEnemyHoldTicks_ == 0;
    }

    static bool retainsPlayerTerminalBehindLaterEnemyCallback() {
        WordGame game(GraphicsMode::Vga256, 0x8e76u);
        game.startGame(6);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;
        game.attractActionTimer_ = 20.0 / WordSchedulerTicksPerSecond;
        game.playerRow_ = 0;
        game.playerColumn_ = 0;
        game.playerTerminalFrame_ = 10;

        WordGame::Enemy enemy;
        enemy.row = 2;
        enemy.column = 5;
        enemy.fromRow = 2;
        enemy.fromColumn = 4;
        enemy.direction = 1;
        enemy.type = 0;
        enemy.slot = 0;
        enemy.moving = true;
        enemy.animationTimer = 3.0 / WordSchedulerTicksPerSecond;
        game.enemies_ = {enemy};
        game.enemySlotCount_ = 1;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        game.enemySlots_[0].type = 0;
        game.gameplayTickAccumulator_ = 0.0;

        Renderer before(GraphicsMode::Vga256);
        game.renderBoard(before);
        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (game.playerTerminalFrame_ >= 0 ||
            game.attractPlayerTerminalEnemyHoldTicks_ != 1 ||
            game.attractPlayerTerminalEnemyHoldPixels_.empty()) {
            return false;
        }
        Renderer logical(GraphicsMode::Vga256);
        game.renderBoard(logical);
        Renderer held(GraphicsMode::Vga256);
        game.renderPresentation(held);
        const int cellLeft = WordGame::BoardLeft;
        const int cellTop = WordGame::BoardTop;
        bool differsFromLogical = false;
        for (int y = 0; y < Renderer::Height; ++y) {
            for (int x = 0; x < Renderer::Width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(
                    y * Renderer::Width + x);
                const bool playerCell =
                    x >= cellLeft && x < cellLeft + WordGame::BoardCellWidth &&
                    y >= cellTop && y < cellTop + WordGame::BoardCellHeight;
                if (playerCell) {
                    if (held.pixels()[pixel] != before.pixels()[pixel]) return false;
                    differsFromLogical = differsFromLogical ||
                        held.pixels()[pixel] != logical.pixels()[pixel];
                } else if (held.pixels()[pixel] != logical.pixels()[pixel]) {
                    return false;
                }
            }
        }
        if (!differsFromLogical) return false;
        game.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        Renderer released(GraphicsMode::Vga256);
        game.renderPresentation(released);
        return game.attractPlayerTerminalEnemyHoldTicks_ == 0 &&
               game.attractPlayerTerminalEnemyHoldPixels_.empty();
    }

    static bool retainsSeededTerminalPlayerSurfacesBeforeNewEnemyMoves() {
        // DOS source run 184 spans source frames 2375-2376: selector 4 has
        // cleared the upward Muncher terminal, while the later selector-1
        // Troggle move is not visible until run 185. At the exact 70.086304
        // Hz capture cadence this requires the callback surface on both
        // native samples 1384 and 1385, followed by the Troggle page on 1386.
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        constexpr std::uint64_t PlayerCallbackHash = 0xa1593a66477774e5ull;
        constexpr std::uint64_t TroggleCallbackHash = 0x8e72b69b37f5ded1ull;
        // Source run 121 remains resident while selector 5 starts a movement
        // at state zero; run 122 is the next visible callback page. Native
        // formerly inserted a two-sample old-player/new-Troggle composite.
        constexpr std::uint64_t PhaseZeroResidentHash =
            0xfdc338c2098086feull;
        constexpr std::uint64_t PhaseOneCallbackHash =
            0x478ea4976d9b8158ull;
        constexpr std::uint64_t ChewTerminalAfterTroggleHash =
            0xd9877b78692f2f71ull;
        constexpr std::uint64_t ChewTerminalNextHash =
            0x3f189912f5fffcf5ull;
        // The later rightward terminal takes the other captured branch:
        // source run 497 is the retained player/Troggle composite, followed
        // by the next selector-1 pose in run 498. Pin it so the run-184 fix
        // cannot erase an already exact direction-specific page.
        constexpr std::uint64_t RightTerminalCompositeHash =
            0x6eb96b428dea9b0aull;
        constexpr std::uint64_t RightTerminalNextHash =
            0xe0f70227dae65dcbull;
        constexpr std::uint64_t RightArrivalResidentHash =
            0xaca60c8bf750b257ull;
        constexpr std::uint64_t TwoEnemyRightArrivalResidentHash =
            0xb3800a7b09b3a1cdull;
        constexpr std::uint64_t TwoEnemyRightArrivalNextHash =
            0x02cfe352db5d4756ull;
        constexpr std::uint64_t DownArrivalResidentHash =
            0x1f0e31af2f7d9900ull;
        constexpr std::uint64_t DownArrivalNextHash =
            0x07ee9e0add4295dd3ull;
        WordGame game(GraphicsMode::Vga256, 0x8e74u);
        game.settingsPersistenceEnabled_ = false;
        game.startAttract();
        Renderer renderer(GraphicsMode::Vga256);
        for (int sample = 0; sample <= 3546; ++sample) {
            game.renderPresentation(renderer);
            if ((sample == 869 || sample == 870) &&
                fullFrameHash(renderer) != PhaseZeroResidentHash) {
                return false;
            }
            if (sample == 871 &&
                fullFrameHash(renderer) != PhaseOneCallbackHash) {
                return false;
            }
            if ((sample == 1030 || sample == 1031) &&
                fullFrameHash(renderer) != ChewTerminalAfterTroggleHash) {
                return false;
            }
            if (sample == 1035 &&
                fullFrameHash(renderer) != ChewTerminalNextHash) {
                return false;
            }
            if ((sample == 1384 || sample == 1385) &&
                fullFrameHash(renderer) != PlayerCallbackHash) {
                return false;
            }
            if (sample == 1386 &&
                fullFrameHash(renderer) != TroggleCallbackHash) {
                return false;
            }
            if ((sample == 2848 || sample == 2849 || sample == 2850) &&
                fullFrameHash(renderer) !=
                    TwoEnemyRightArrivalResidentHash) {
                return false;
            }
            if (sample == 2851 &&
                fullFrameHash(renderer) != TwoEnemyRightArrivalNextHash) {
                return false;
            }
            if ((sample == 3541 || sample == 3542 || sample == 3543) &&
                fullFrameHash(renderer) != RightArrivalResidentHash) {
                return false;
            }
            if ((sample == 3544 || sample == 3545) &&
                fullFrameHash(renderer) != RightTerminalCompositeHash) {
                return false;
            }
            if (sample == 3546 &&
                fullFrameHash(renderer) != RightTerminalNextHash) {
                return false;
            }
            if ((sample == 3965 || sample == 3966) &&
                fullFrameHash(renderer) != DownArrivalResidentHash) {
                return false;
            }
            if (sample == 3967 &&
                fullFrameHash(renderer) != DownArrivalNextHash) {
                return false;
            }
            game.update(SampleSeconds);
        }
        return true;
    }

    static bool retainsTerminalPlayerForegroundOverResidentEnemySweep() {
        WordGame game(GraphicsMode::Vga256, 0x8e77u);
        game.startGame(6);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;

        constexpr int SourceRow = 2;
        constexpr int SourceColumn = 4;
        constexpr int SourceCell = SourceRow * WordGame::BoardColumns + SourceColumn;
        WordRecord sleep;
        constexpr std::string_view Label = "sleep";
        std::copy(Label.begin(), Label.end(), sleep.bytes.begin());
        if (!game.core_.restoreCell(SourceCell,
                                    WordBoardCell{sleep, false, -1}, false)) {
            return false;
        }

        game.playerRow_ = 1;
        game.playerColumn_ = 0;
        game.moveFromRow_ = 1;
        game.moveFromColumn_ = 0;
        game.moveToRow_ = 0;
        game.moveToColumn_ = 0;
        game.moveDirection_ = 0;
        game.moving_ = true;
        game.moveTimer_ = 1.0 / WordSchedulerTicksPerSecond;
        if (game.playerMovementPhase() != 4) return false;

        WordGame::Enemy enemy;
        enemy.row = SourceRow;
        enemy.column = 5;
        enemy.fromRow = SourceRow;
        enemy.fromColumn = SourceColumn;
        enemy.direction = 1;
        enemy.type = 2;
        enemy.slot = 0;
        enemy.moving = true;
        const double enemyInterval = game.enemyMoveAnimationDuration(1) / 6.0;
        enemy.animationTimer = enemyInterval * 0.5;
        game.enemies_ = {enemy};
        game.enemySlotCount_ = 1;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        game.enemySlots_[0].type = enemy.type;
        if (game.enemyMovementPhase(game.enemies_[0]) != 6) return false;

        Renderer logical(GraphicsMode::Vga256);
        game.renderBoard(logical);
        const std::vector<std::uint32_t> callback =
            game.capturePlayerCallbackFrame();
        int whiteWrites = 0;
        int magentaWrites = 0;
        int outsideWrites = 0;
        const int sourceLeft = WordGame::BoardLeft +
                               SourceColumn * WordGame::BoardCellWidth;
        const int sourceTop = WordGame::BoardTop +
                              SourceRow * WordGame::BoardCellHeight;
        for (int y = 0; y < Renderer::Height; ++y) {
            for (int x = 0; x < Renderer::Width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(
                    y * Renderer::Width + x);
                if (callback[pixel] == logical.pixels()[pixel]) continue;
                const bool inCapturedCell =
                    x >= sourceLeft &&
                    x <= sourceLeft + WordGame::BoardCellWidth &&
                    y >= sourceTop &&
                    y < sourceTop + WordGame::BoardCellHeight;
                if (!inCapturedCell) {
                    ++outsideWrites;
                    continue;
                }
                const std::uint32_t before =
                    logical.pixels()[pixel] & 0xffffffu;
                const std::uint32_t after = callback[pixel] & 0xffffffu;
                if (before != Colors::BoardBlue) return false;
                if (after == Colors::White) ++whiteWrites;
                else if (after == Colors::Magenta) ++magentaWrites;
                else return false;
            }
        }
        if (whiteWrites != 109 || magentaWrites != 29 || outsideWrites != 0) {
            std::cerr << "word terminal player foreground writes white="
                      << whiteWrites << " magenta=" << magentaWrites
                      << " outside=" << outsideWrites << '\n';
            return false;
        }

        // Earlier player or enemy phases do not own this resident surface.
        game.moveTimer_ = 2.0 / WordSchedulerTicksPerSecond;
        Renderer earlierPlayer(GraphicsMode::Vga256);
        game.renderBoard(earlierPlayer);
        if (game.capturePlayerCallbackFrame() != earlierPlayer.pixels()) return false;
        game.moveTimer_ = 1.0 / WordSchedulerTicksPerSecond;
        game.enemies_[0].animationTimer = enemyInterval * 1.5;
        Renderer earlierEnemy(GraphicsMode::Vga256);
        game.renderBoard(earlierEnemy);
        return game.capturePlayerCallbackFrame() == earlierEnemy.pixels();
    }

    static bool usesCapturedLeftBashfulTrailExitGeometry() {
        const auto makeCell = [](const std::string_view text) {
            WordRecord record;
            std::copy(text.begin(), text.end(), record.bytes.begin());
            return WordBoardCell{record, false, -1};
        };
        const auto prepare = [&](WordGame& game) {
            game.startGame(1);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Playing;
            game.attractMode_ = false;
            game.playerRow_ = 4;
            game.playerColumn_ = 1;
            game.safeCells_.fill(false);
            return game.core_.restoreCell(24, makeCell("150"), false);
        };

        // The shared actor painter must keep all six left-exit poses inside
        // the board viewport while its swept blue rectangle hides the newly
        // regenerated payload until the terminal removal callback.
        WordGame geometryGame(GraphicsMode::Vga256, 0x5ba6u);
        if (!prepare(geometryGame)) return false;
        Renderer marginBaseline(GraphicsMode::Vga256);
        geometryGame.renderBoard(marginBaseline);
        int baselineLabelWhite = 0;
        for (int y = 147; y <= 175; ++y) {
            for (int x = 21; x <= 67; ++x) {
                if ((marginBaseline.pixels()[static_cast<std::size_t>(
                         y * Renderer::Width + x)] & 0xffffffu) == Colors::White) {
                    ++baselineLabelWhite;
                }
            }
        }
        if (baselineLabelWhite == 0) return false;
        WordGame::Enemy bashful;
        bashful.row = 4;
        bashful.column = -1;
        bashful.fromRow = 4;
        bashful.fromColumn = 0;
        bashful.direction = 3;
        bashful.type = 2;
        bashful.slot = 0;
        bashful.moving = true;
        bashful.exiting = true;
        geometryGame.enemies_ = {bashful};

        const double interval = 3.0 / WordSchedulerTicksPerSecond;
        const double duration = geometryGame.enemyMoveAnimationDuration(3);
        Renderer renderer(GraphicsMode::Vga256);
        for (int phase = 1; phase <= 6; ++phase) {
            geometryGame.enemies_[0].animationTimer =
                duration - (phase - 1) * interval;
            if (geometryGame.enemyMovementPhase(geometryGame.enemies_[0]) != phase) {
                return false;
            }
            geometryGame.renderBoard(renderer);
            int hiddenLabelWhite = 0;
            for (int y = 147; y <= 175; ++y) {
                for (int x = 21; x <= 67; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(
                        y * Renderer::Width + x);
                    if ((marginBaseline.pixels()[pixel] & 0xffffffu) == Colors::White &&
                        (renderer.pixels()[pixel] & 0xffffffu) == Colors::White) {
                        ++hiddenLabelWhite;
                    }
                }
                for (int x = 0; x < 20; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(
                        y * Renderer::Width + x);
                    if (renderer.pixels()[pixel] != marginBaseline.pixels()[pixel]) {
                        std::cerr << "word left-exit leaked into margin at phase "
                                  << phase << " x=" << x << " y=" << y << '\n';
                        return false;
                    }
                }
            }
            if (hiddenLabelWhite != 0) {
                std::cerr << "word left-exit exposed trail label at phase " << phase
                          << " white=" << hiddenLabelWhite << '\n';
                return false;
            }
        }
        geometryGame.enemies_.clear();
        geometryGame.renderBoard(renderer);
        int terminalLabelWhite = 0;
        for (int y = 147; y <= 175; ++y) {
            for (int x = 21; x <= 67; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(
                    y * Renderer::Width + x);
                if ((marginBaseline.pixels()[pixel] & 0xffffffu) == Colors::White &&
                    (renderer.pixels()[pixel] & 0xffffffu) == Colors::White) {
                    ++terminalLabelWhite;
                }
            }
        }
        if (terminalLabelWhite != baselineLabelWhite) return false;

        // A live trail callback coincident with a player step first presents
        // the new phase-1 Bashful over the player's retained phase-0 pixels,
        // then presents the complete new player frame on the next refresh.
        WordGame presentationGame(GraphicsMode::Vga256, 0x5ba7u);
        if (!prepare(presentationGame)) return false;
        presentationGame.enemySlotCount_ = 1;
        presentationGame.enemySlots_[0] = {};
        presentationGame.enemySlots_[0].type = 2;
        presentationGame.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        bashful = {};
        bashful.row = 4;
        bashful.column = 0;
        bashful.fromRow = 4;
        bashful.fromColumn = 0;
        bashful.direction = 3;
        bashful.type = 2;
        bashful.slot = 0;
        bashful.savedCell = presentationGame.core_.board().cells[24];
        bashful.savedCellEaten = false;
        bashful.savedCellValid = true;
        bashful.moveTimer = 1.0 / WordSchedulerTicksPerSecond;
        presentationGame.enemies_ = {bashful};
        presentationGame.beginMove(4, 2, 1);
        const double retainedPlayerTimer = presentationGame.moveTimer_;
        presentationGame.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (presentationGame.enemies_.size() != 1 ||
            !presentationGame.enemies_[0].moving ||
            !presentationGame.enemies_[0].exiting ||
            presentationGame.enemies_[0].direction != 3 ||
            presentationGame.enemyMovementPhase(presentationGame.enemies_[0]) != 1 ||
            presentationGame.presentationFrames_.size() != 1) {
            return false;
        }

        const double advancedPlayerTimer = presentationGame.moveTimer_;
        presentationGame.moveTimer_ = retainedPlayerTimer;
        Renderer expectedIntermediate(GraphicsMode::Vga256);
        presentationGame.render(expectedIntermediate);
        presentationGame.moveTimer_ = advancedPlayerTimer;
        Renderer complete(GraphicsMode::Vga256);
        presentationGame.render(complete);
        if (expectedIntermediate.pixels() == complete.pixels()) return false;

        Renderer presented(GraphicsMode::Vga256);
        presentationGame.renderPresentation(presented);
        if (presented.pixels() != expectedIntermediate.pixels() ||
            !presentationGame.presentationFrames_.empty()) return false;
        presentationGame.renderPresentation(presented);
        if (presented.pixels() != complete.pixels()) return false;

        // When the player record is already at a movement terminal, state 4
        // first repaints the standing actor. A due later Troggle job then
        // starts its trail/move on that same public tick. The complete
        // player-only callback framebuffer must precede the Troggle result.
        WordGame idleCallbackGame(GraphicsMode::Vga256, 0x5ba8u);
        if (!prepare(idleCallbackGame)) return false;
        idleCallbackGame.playerTerminalFrame_ = 4;
        idleCallbackGame.enemySlotCount_ = 1;
        idleCallbackGame.enemySlots_[0] = {};
        idleCallbackGame.enemySlots_[0].type = 2;
        idleCallbackGame.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        bashful = {};
        bashful.row = 4;
        bashful.column = 0;
        bashful.fromRow = 4;
        bashful.fromColumn = 0;
        bashful.direction = 3;
        bashful.type = 2;
        bashful.slot = 0;
        bashful.savedCell = idleCallbackGame.core_.board().cells[24];
        bashful.savedCellEaten = false;
        bashful.savedCellValid = true;
        bashful.moveTimer = 1.0 / WordSchedulerTicksPerSecond;
        idleCallbackGame.enemies_ = {bashful};
        idleCallbackGame.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (idleCallbackGame.playerTerminalFrame_ != -1 ||
            idleCallbackGame.enemies_.size() != 1 ||
            !idleCallbackGame.enemies_[0].moving ||
            idleCallbackGame.enemyMovementPhase(idleCallbackGame.enemies_[0]) != 1 ||
            idleCallbackGame.presentationFrames_.size() != 1) return false;

        const WordGame::Enemy movedBashful = idleCallbackGame.enemies_[0];
        idleCallbackGame.enemies_[0] = bashful;
        Renderer expectedPlayerCallback(GraphicsMode::Vga256);
        idleCallbackGame.render(expectedPlayerCallback);
        idleCallbackGame.enemies_[0] = movedBashful;
        Renderer completeIdleCallback(GraphicsMode::Vga256);
        idleCallbackGame.render(completeIdleCallback);
        if (expectedPlayerCallback.pixels() == completeIdleCallback.pixels()) return false;
        idleCallbackGame.renderPresentation(presented);
        if (presented.pixels() != expectedPlayerCallback.pixels() ||
            !idleCallbackGame.presentationFrames_.empty()) return false;
        idleCallbackGame.renderPresentation(presented);
        return presented.pixels() == completeIdleCallback.pixels();
    }

    static bool usesCapturedTopBashfulExitGeometry() {
        WordGame game(GraphicsMode::Vga256, 0x5baau);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.safeCells_.fill(false);

        WordRecord record;
        constexpr std::string_view Label = "trail";
        std::copy(Label.begin(), Label.end(), record.bytes.begin());
        if (!game.core_.restoreCell(5, WordBoardCell{record, false, -1}, false)) {
            std::cerr << "word top-exit could not install trail fixture\n";
            return false;
        }

        Renderer baseline(GraphicsMode::Vga256);
        game.renderBoard(baseline);
        std::vector<std::size_t> labelPixels;
        for (int y = WordGame::BoardTop + 1;
             y < WordGame::BoardTop + WordGame::BoardCellHeight; ++y) {
            for (int x = WordGame::BoardLeft + 5 * WordGame::BoardCellWidth + 1;
                 x < WordGame::BoardRight; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(
                    y * Renderer::Width + x);
                if ((baseline.pixels()[pixel] & 0xffffffu) == Colors::White) {
                    labelPixels.push_back(pixel);
                }
            }
        }
        if (labelPixels.empty()) {
            std::cerr << "word top-exit fixture label has no white pixels\n";
            return false;
        }

        WordGame::Enemy bashful;
        bashful.row = -1;
        bashful.column = 5;
        bashful.fromRow = 0;
        bashful.fromColumn = 5;
        bashful.direction = 0;
        bashful.type = 2;
        bashful.slot = 0;
        bashful.moving = true;
        bashful.exiting = true;
        game.enemies_ = {bashful};

        constexpr double Interval = 3.0 / WordSchedulerTicksPerSecond;
        const double duration = game.enemyMoveAnimationDuration(0) + Interval;
        Renderer renderer(GraphicsMode::Vga256);
        for (int phase = 1; phase <= 5; ++phase) {
            game.enemies_[0].animationTimer = duration - (phase - 1) * Interval;
            if (game.enemyMovementPhase(game.enemies_[0]) != phase) {
                std::cerr << "word top-exit phase mismatch at " << phase << '\n';
                return false;
            }
            game.renderBoard(renderer);
            for (int y = 0; y <= WordGame::BoardTop; ++y) {
                for (int x = WordGame::BoardLeft + 5 * WordGame::BoardCellWidth;
                     x <= WordGame::BoardRight + 1; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(
                        y * Renderer::Width + x);
                    if (renderer.pixels()[pixel] != baseline.pixels()[pixel]) {
                        std::cerr << "word top-exit leaked into header at phase "
                                  << phase << " x=" << x << " y=" << y << '\n';
                        return false;
                    }
                }
            }
            const bool retainedLabelPixel = std::any_of(
                labelPixels.begin(), labelPixels.end(), [&](const std::size_t pixel) {
                    return (renderer.pixels()[pixel] & 0xffffffu) == Colors::White;
                });
            if (retainedLabelPixel) {
                std::cerr << "word top-exit exposed trail label at phase "
                          << phase << '\n';
                return false;
            }
        }

        game.enemies_.clear();
        game.renderBoard(renderer);
        const bool restored = std::all_of(
            labelPixels.begin(), labelPixels.end(), [&](const std::size_t pixel) {
                return renderer.pixels()[pixel] == baseline.pixels()[pixel];
            });
        if (!restored) std::cerr << "word top-exit terminal label repaint mismatch\n";
        return restored;
    }

    static bool usesCapturedDualHelperMovementGeometry() {
        struct CapturedState {
            int playerState;
            int bottomHelperState;
            int rightHelperState;
            bool playerCallbackComposite;
        };
        // Number's lossless DOS frames establish ten clean composites plus
        // three resident-surface player callbacks. Word owns different cell
        // content, but shares their actor geometry and callback painter.
        static constexpr std::array<CapturedState, 13> CapturedStates = {{
            {0, 0, 1, false},
            {0, 0, 2, false},
            {0, 1, 3, false},
            {0, 2, 3, false},
            {0, 2, 4, false},
            {1, 2, 4, true},
            {2, 3, 5, true},
            {3, 3, 5, true},
            {4, 4, 6, false},
            {5, 4, 6, false},
            {6, 5, 6, false},
            {6, 6, 6, false},
            {6, 7, 6, false},
        }};
        constexpr int BottomSource = 4 * WordGame::BoardColumns;
        constexpr int RightSource = 2 * WordGame::BoardColumns + 5;
        constexpr std::uint32_t HelperGreen = 0xd3ff5du;
        std::vector<std::uint64_t> observed;

        for (const CapturedState& state : CapturedStates) {
            WordGame game(GraphicsMode::Vga256, 0x5badu);
            game.startGame(1);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Attract;
            game.attractMode_ = true;
            game.safeCells_.fill(false);
            game.safeCells_[13] = true;
            game.safeCells_[16] = true;
            game.playerRow_ = 1;
            game.playerColumn_ = 5;
            game.playerTerminalFrame_ = 7;
            game.moving_ = false;
            game.munching_ = false;

            WordRecord bottomLabel;
            WordRecord rightLabel;
            constexpr std::string_view BottomText = "trail";
            constexpr std::string_view RightText = "erase";
            std::copy(BottomText.begin(), BottomText.end(), bottomLabel.bytes.begin());
            std::copy(RightText.begin(), RightText.end(), rightLabel.bytes.begin());
            const WordBoardCell bottomCell{bottomLabel, false, -1};
            const WordBoardCell rightCell{rightLabel, false, -1};
            if (!game.core_.restoreCell(BottomSource, bottomCell, false) ||
                !game.core_.restoreCell(RightSource, rightCell, false)) {
                std::cerr << "word dual-Helper fixture could not install trails\n";
                return false;
            }

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
                        WordSchedulerTicksPerSecond;
                if (game.playerMovementPhase() != state.playerState) {
                    std::cerr << "word dual-Helper player phase mismatch at "
                              << state.playerState << '\n';
                    return false;
                }
            } else if (state.playerState >= 5) {
                game.playerRow_ = 2;
                game.playerColumn_ = 5;
                game.playerTerminalFrame_ = state.playerState == 5 ? 8 : 7;
            }

            if (state.bottomHelperState > 0 &&
                !game.core_.clearCellWithoutScore(BottomSource)) {
                std::cerr << "word dual-Helper bottom trail did not clear\n";
                return false;
            }
            if (state.rightHelperState > 0 &&
                !game.core_.clearCellWithoutScore(RightSource)) {
                std::cerr << "word dual-Helper right trail did not clear\n";
                return false;
            }

            const auto helper = [&](const bool bottom, const int helperState,
                                    const int slot) {
                WordGame::Enemy enemy;
                enemy.type = 3;
                enemy.slot = slot;
                enemy.direction = bottom ? 1 : 2;
                enemy.fromRow = bottom ? 4 : 2;
                enemy.fromColumn = bottom ? 0 : 5;
                enemy.row = bottom ? 4 : 3;
                enemy.column = bottom ? 1 : 5;
                enemy.savedCell = bottom ? bottomCell : rightCell;
                enemy.savedCellEaten = false;
                enemy.savedCellValid = true;
                const int steps = bottom ? 6 : 5;
                if (helperState == 0) {
                    enemy.row = enemy.fromRow;
                    enemy.column = enemy.fromColumn;
                    enemy.dwellFrame = bottom ? 4 : 15;
                } else if (helperState <= steps) {
                    enemy.moving = true;
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
            for (std::size_t index = 0; index < game.enemies_.size(); ++index) {
                const int expectedPhase = index == 0
                    ? state.bottomHelperState : state.rightHelperState;
                const int stepCount = index == 0 ? 6 : 5;
                if (expectedPhase >= 1 && expectedPhase <= stepCount &&
                    game.enemyMovementPhase(game.enemies_[index]) != expectedPhase) {
                    std::cerr << "word dual-Helper movement phase mismatch for actor "
                              << index << " at " << expectedPhase << '\n';
                    return false;
                }
            }

            Renderer renderer(GraphicsMode::Vga256);
            if (state.playerCallbackComposite) {
                renderer.replacePixels(game.capturePlayerCallbackFrame());
            } else {
                game.renderBoard(renderer);
            }
            const auto hasHelperGreen = [&](const WordGame::Enemy& enemy) {
                int phase = 0;
                if (enemy.moving) phase = game.enemyMovementPhase(enemy);
                const int steps = enemy.direction == 0 || enemy.direction == 2 ? 5 : 6;
                const double progress = enemy.moving
                    ? static_cast<double>(phase) / steps : 1.0;
                const int x = static_cast<int>(std::lround(
                    WordGame::BoardLeft + enemy.fromColumn * WordGame::BoardCellWidth + 4 +
                    (enemy.column - enemy.fromColumn) *
                        WordGame::BoardCellWidth * progress));
                const int y = static_cast<int>(std::lround(
                    WordGame::BoardTop + enemy.fromRow * WordGame::BoardCellHeight + 1 +
                    (enemy.row - enemy.fromRow) *
                        WordGame::BoardCellHeight * progress));
                for (int py = std::max(0, y); py < std::min(Renderer::Height, y + 29); ++py) {
                    for (int px = std::max(0, x); px < std::min(Renderer::Width, x + 41); ++px) {
                        if ((renderer.pixels()[static_cast<std::size_t>(
                                py * Renderer::Width + px)] & 0xffffffu) == HelperGreen) {
                            return true;
                        }
                    }
                }
                return false;
            };
            if (!hasHelperGreen(game.enemies_[0]) ||
                !hasHelperGreen(game.enemies_[1]) ||
                (state.bottomHelperState > 0 && !game.core_.eaten(BottomSource)) ||
                (state.rightHelperState > 0 && !game.core_.eaten(RightSource))) {
                std::cerr << "word dual-Helper actor/trail rendering mismatch: player="
                          << state.playerState << " bottom="
                          << state.bottomHelperState << " right="
                          << state.rightHelperState << '\n';
                return false;
            }
            observed.push_back(fullFrameHash(renderer));
        }

        std::sort(observed.begin(), observed.end());
        if (std::adjacent_find(observed.begin(), observed.end()) != observed.end()) {
            return false;
        }

        WordGame routed(GraphicsMode::Vga256, 0x5baeu);
        routed.startGame(1);
        disableBoardJobs(routed);
        routed.page_ = WordGamePage::Attract;
        routed.attractMode_ = true;
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
        WordGame::Enemy overlappingHelper;
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
        routed.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (!routed.moving_ || routed.playerMovementPhase() != 1 ||
            routed.presentationFrames_.size() != 1) {
            std::cerr << "word dual-Helper player callback was not routed\n";
            return false;
        }
        const std::vector<std::uint32_t> expectedCallback =
            routed.capturePlayerCallbackFrame();
        const std::vector<std::uint32_t> cleanFrame =
            routed.capturePresentationFrame();
        if (expectedCallback == cleanFrame ||
            routed.presentationFrames_.front() != expectedCallback) {
            std::cerr << "word dual-Helper routed callback pixels mismatch\n";
            return false;
        }
        return true;
    }

    static bool retainsSeededFinalPlayerCallbackSurface() {
        // The pinned 0x8E74 route reaches the source-run-351 overlap without a
        // fixture. Its repaired player callback is resident for two 70 Hz
        // samples before the next public scheduler dispatch.
        constexpr double SampleSeconds = 31'250.0 / 2'190'197.0;
        constexpr std::uint64_t RepairedHash = 0xc26cdcf648a6d0dbull;
        constexpr std::uint64_t NextDispatchHash = 0x59a6e9157fa84c7dull;
        WordGame game(GraphicsMode::Vga256, 0x8e74u);
        game.settingsPersistenceEnabled_ = false;
        game.startAttract();
        Renderer renderer(GraphicsMode::Vga256);
        for (int sample = 0; sample <= 2466; ++sample) {
            game.renderPresentation(renderer);
            if ((sample == 2464 || sample == 2465) &&
                fullFrameHash(renderer) != RepairedHash) {
                return false;
            }
            if (sample == 2466 && fullFrameHash(renderer) != NextDispatchHash) {
                return false;
            }
            game.update(SampleSeconds);
        }
        return game.random_.calls == 350;
    }

    static bool routesPlayerCallbackBeforeCannibalBiter() {
        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        const auto prepare = [&](WordGame& game) {
            game.startGame(1);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Playing;
            game.playerRow_ = 4;
            game.playerColumn_ = 0;
            game.playerTerminalFrame_ = 7;
            game.moving_ = true;
            game.moveDirection_ = 0;
            game.moveFromRow_ = 4;
            game.moveFromColumn_ = 0;
            game.moveToRow_ = 3;
            game.moveToColumn_ = 0;
            game.moveTimer_ = game.playerMoveAnimationDuration(0);
            game.gameplayTickAccumulator_ = 0.0;

            game.enemySlotCount_ = 2;
            for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
                game.enemySlots_[static_cast<std::size_t>(slot)].type = 0;
                game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                    WordGame::EnemySlotPhase::Active;
            }
            WordGame::Enemy biter;
            biter.type = 0;
            biter.slot = 0;
            biter.row = 0;
            biter.column = 2;
            biter.direction = 2;
            biter.dwellFrame = 15;
            biter.cannibalizing = true;
            biter.cannibalTimer = 21.0 / WordSchedulerTicksPerSecond;
            WordGame::Enemy victim = biter;
            victim.slot = 1;
            victim.cannibalizing = false;
            victim.cannibalTimer = 0.0;
            victim.collisionHidden = true;
            victim.overlapFrozen = true;
            victim.overlapRetired = true;
            game.enemies_ = {biter, victim};
        };

        WordGame callbackOnly(GraphicsMode::Vga256, 0x53c1u);
        prepare(callbackOnly);
        callbackOnly.updatePlayerMovementTick(TickSeconds);
        const std::vector<std::uint32_t> expectedCallback =
            callbackOnly.capturePlayerCallbackFrame();

        WordGame routed(GraphicsMode::Vga256, 0x53c1u);
        prepare(routed);
        routed.update(TickSeconds + 1e-12);
        if (routed.presentationFrames_.size() != 1 ||
            routed.presentationFrames_.front() != expectedCallback) {
            std::cerr << "word cannibal/player callback ordering was not retained\n";
            return false;
        }
        const std::vector<std::uint32_t> finalFrame =
            routed.capturePresentationFrame();
        if (expectedCallback == finalFrame) return false;
        Renderer callbackPresentation(GraphicsMode::Vga256);
        routed.renderPresentation(callbackPresentation);
        Renderer finalPresentation(GraphicsMode::Vga256);
        routed.renderPresentation(finalPresentation);
        if (callbackPresentation.pixels() != expectedCallback ||
            finalPresentation.pixels() != finalFrame ||
            !routed.presentationFrames_.empty()) {
            return false;
        }

        // The shared retained board page repaints both player endpoint cells
        // while leaving the disjoint biter cell untouched until its later
        // callback. Exercise the Word copy of the Number live-oracle path.
        WordGame residentExpected(GraphicsMode::Vga256, 0x53c1u);
        prepare(residentExpected);
        std::vector<std::uint32_t> expectedResident =
            residentExpected.capturePresentationFrame();
        residentExpected.updatePlayerMovementTick(TickSeconds);
        const std::vector<std::uint32_t> afterResidentPlayer =
            residentExpected.capturePresentationFrame();
        residentExpected.copyBoardCellPixels(
            expectedResident, afterResidentPlayer,
            residentExpected.moveFromRow_, residentExpected.moveFromColumn_);
        residentExpected.copyBoardCellPixels(
            expectedResident, afterResidentPlayer,
            residentExpected.moveToRow_, residentExpected.moveToColumn_);

        WordGame residentRouted(GraphicsMode::Vga256, 0x53c1u);
        prepare(residentRouted);
        residentRouted.cannibalPlayerResidentSurfacePixels_ =
            residentRouted.capturePresentationFrame();
        residentRouted.cannibalPlayerPresentationHoldPixels_ =
            residentRouted.cannibalPlayerResidentSurfacePixels_;
        residentRouted.update(TickSeconds + 1e-12);
        return residentRouted.presentationFrames_.size() == 1 &&
               residentRouted.presentationFrames_.front() == expectedResident &&
               residentRouted.cannibalPlayerPresentationHoldPixels_ ==
                   expectedResident;
    }

    static bool usesCapturedHelperBottomExitPlayerMoveGeometry() {
        struct CapturedState {
            int helperState;
            int playerState;
        };
        static constexpr std::array<CapturedState, 12> CapturedStates = {{
            {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {4, 1},
            {5, 1}, {5, 2}, {5, 3}, {6, 4}, {6, 5}, {6, 6},
        }};
        constexpr int SourceCell = 4 * WordGame::BoardColumns + 5;
        WordRecord trailRecord;
        constexpr std::string_view Trail = "trail";
        std::copy(Trail.begin(), Trail.end(), trailRecord.bytes.begin());
        const WordBoardCell trailCell{trailRecord, false, -1};
        std::vector<std::uint64_t> hashes;

        for (const CapturedState& state : CapturedStates) {
            WordGame game(GraphicsMode::Vga256, 0x5bafu);
            game.startGame(10);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Attract;
            game.attractMode_ = true;
            game.safeCells_.fill(false);
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (!game.core_.clearCellWithoutScore(static_cast<std::size_t>(index))) {
                    return false;
                }
            }
            if (!game.core_.restoreCell(SourceCell, trailCell, false)) return false;

            game.playerRow_ = 3;
            game.playerColumn_ = 2;
            game.playerTerminalFrame_ = 7;
            if (state.playerState >= 1 && state.playerState <= 4) {
                game.moving_ = true;
                game.moveDirection_ = 0;
                game.moveFromRow_ = 3;
                game.moveFromColumn_ = 2;
                game.moveToRow_ = 2;
                game.moveToColumn_ = 2;
                game.moveTimer_ = game.playerMoveAnimationDuration(0) -
                    static_cast<double>(state.playerState) /
                        WordSchedulerTicksPerSecond;
                if (game.playerMovementPhase() != state.playerState) return false;
            } else if (state.playerState == 5) {
                game.playerRow_ = 2;
                game.playerTerminalFrame_ = 2;
            } else if (state.playerState == 6) {
                game.playerRow_ = 2;
                game.playerTerminalFrame_ = 7;
            }

            if (state.helperState > 0 &&
                !game.core_.clearCellWithoutScore(SourceCell)) return false;
            if (state.helperState < 6) {
                WordGame::Enemy helper;
                helper.type = 3;
                helper.slot = 1;
                helper.direction = 2;
                helper.fromRow = 4;
                helper.fromColumn = 5;
                helper.row = state.helperState == 0 ? 4 : 5;
                helper.column = 5;
                helper.savedCell = trailCell;
                helper.savedCellEaten = false;
                helper.savedCellValid = true;
                if (state.helperState == 0) {
                    helper.dwellFrame = 15;
                } else {
                    helper.moving = true;
                    helper.exiting = true;
                    const double interval =
                        game.enemyMoveAnimationDuration(2) / 5.0;
                    const double total =
                        game.enemyMoveAnimationDuration(2) + interval;
                    helper.animationTimer = total -
                        static_cast<double>(state.helperState - 1) * interval;
                    if (game.enemyMovementPhase(helper) != state.helperState) {
                        return false;
                    }
                }
                game.enemies_ = {helper};
            }

            Renderer renderer(GraphicsMode::Vga256);
            game.renderBoard(renderer);
            const std::vector<WordGame::Enemy> savedEnemies = game.enemies_;
            game.enemies_.clear();
            Renderer marginBaseline(GraphicsMode::Vga256);
            game.renderBoard(marginBaseline);
            game.enemies_ = savedEnemies;
            for (int y = WordGame::BoardBottom + 2; y < 184; ++y) {
                for (int x = WordGame::BoardLeft + 5 * WordGame::BoardCellWidth;
                     x <= WordGame::BoardRight + 1; ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(
                        y * Renderer::Width + x);
                    if (renderer.pixels()[pixel] != marginBaseline.pixels()[pixel]) {
                        std::cerr << "word Helper bottom exit leaked below board\n";
                        return false;
                    }
                }
            }
            if (state.helperState > 0 && !game.core_.eaten(SourceCell)) return false;
            hashes.push_back(fullFrameHash(renderer));
        }

        std::sort(hashes.begin(), hashes.end());
        return std::adjacent_find(hashes.begin(), hashes.end()) == hashes.end();
    }

    static bool usesAttractPlayerPhaseZeroHold() {
        WordGame game(GraphicsMode::Vga256, 0x5ba9u);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Attract;
        game.attractMode_ = true;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.playerTerminalFrame_ = -1;
        game.gameplayTickAccumulator_ = 0.0;

        const auto makeCell = [](const std::string_view text, const bool correct,
                                 const int sourceIndex) {
            WordRecord record;
            std::copy(text.begin(), text.end(), record.bytes.begin());
            return WordBoardCell{record, correct, sourceIndex};
        };
        for (const int index : {8, 13, 14, 20}) {
            if (!game.core_.clearCellWithoutScore(static_cast<std::size_t>(index))) {
                return false;
            }
        }
        if (!game.core_.restoreCell(15, makeCell("yes", true, 1), false)) return false;

        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        game.attractActionTimer_ = TickSeconds;
        Renderer before(GraphicsMode::Vga256);
        game.render(before);
        game.update(TickSeconds + 1e-12);
        if (!game.moving_ || game.playerMovementPhase() != 0 ||
            game.attractPlayerPhaseZeroHoldPixels_.empty()) return false;

        Renderer presented(GraphicsMode::Vga256);
        game.renderPresentation(presented);
        if (presented.pixels() != before.pixels() ||
            game.attractPlayerPhaseZeroHoldPixels_.empty()) return false;

        game.update(TickSeconds + 1e-12);
        if (!game.moving_ || game.playerMovementPhase() != 1) return false;
        game.renderPresentation(presented);
        return presented.pixels() != before.pixels() &&
               game.attractPlayerPhaseZeroHoldPixels_.empty();
    }

    static bool usesCompleteAttractChewAfterTroggleComposite() {
        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        const auto prepare = [](WordGame& game) {
            game.startGame(1);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Attract;
            game.attractMode_ = true;
            game.soundOn_ = false;
            game.playerRow_ = 2;
            game.playerColumn_ = 3;
            game.playerTerminalFrame_ = -1;
            game.gameplayTickAccumulator_ = 0.0;
            game.presentationFrames_.clear();
            game.attractPlayerPhaseZeroHoldPixels_.clear();

            WordRecord record;
            constexpr std::string_view Label = "yes";
            std::copy(Label.begin(), Label.end(), record.bytes.begin());
            if (!game.core_.restoreCell(
                    15, WordBoardCell{record, true, 1}, false)) {
                return false;
            }

            game.enemySlotCount_ = 1;
            game.enemySlots_[0] = {};
            game.enemySlots_[0].type = 0;
            game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
            WordGame::Enemy reggie;
            reggie.row = 4;
            reggie.column = 4;
            reggie.fromRow = 4;
            reggie.fromColumn = 4;
            reggie.direction = 1;
            reggie.type = 0;
            reggie.slot = 0;
            reggie.savedCell = game.core_.board().cells[28];
            reggie.savedCellEaten = game.core_.eaten(28);
            reggie.savedCellValid = true;
            reggie.moveTimer = TickSeconds;
            game.enemies_ = {reggie};
            game.attractActionTimer_ = TickSeconds;
            return true;
        };

        WordGame expected(GraphicsMode::Vga256, 0x5babu);
        WordGame actual(GraphicsMode::Vga256, 0x5babu);
        if (!prepare(expected) || !prepare(actual)) return false;
        if (!expected.updateEnemies(TickSeconds) || expected.enemies_.size() != 1 ||
            !expected.enemies_[0].moving ||
            expected.enemyMovementPhase(expected.enemies_[0]) != 1) {
            return false;
        }
        Renderer enemyOnly(GraphicsMode::Vga256);
        expected.render(enemyOnly);
        expected.beginMunch();
        if (!expected.munching_) return false;
        Renderer combined(GraphicsMode::Vga256);
        expected.render(combined);
        if (enemyOnly.pixels() == combined.pixels()) return false;

        actual.update(TickSeconds + 1e-12);
        if (!actual.munching_ || actual.enemies_.size() != 1 ||
            !actual.enemies_[0].moving ||
            actual.enemyMovementPhase(actual.enemies_[0]) != 1 ||
            !actual.presentationFrames_.empty()) {
            return false;
        }
        Renderer presented(GraphicsMode::Vga256);
        actual.renderPresentation(presented);
        return presented.pixels() == combined.pixels() &&
               presented.pixels() != enemyOnly.pixels();
    }

    static bool liveWordDirectionalCompositeMatches() {
        WordGame game(GraphicsMode::Vga256, 0x574du);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.safeCells_.fill(false);
        game.safeCells_[8] = true;

        const auto record = [](const std::string_view text) {
            WordRecord result;
            std::copy(text.begin(), text.end(), result.bytes.begin());
            return result;
        };
        const WordBoardCell help{record("help"), false, -1};
        const WordBoardCell she{record("she"), true, 1};
        const WordBoardCell yes{record("yes"), false, -1};
        if (!game.core_.restoreCell(7, help, false) ||
            !game.core_.restoreCell(8, she, false) ||
            !game.core_.restoreCell(13, yes, false) ||
            !game.core_.clearCellWithoutScore(14)) {
            return false;
        }

        const auto hash = [](const Renderer& renderer) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 56; y < 116; ++y) {
                for (int x = 68; x < 164; ++x) {
                    value ^= renderer.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };
        Renderer renderer(GraphicsMode::Vga256);
        const auto stateHash = [&](const std::string_view label) {
            game.renderBoard(renderer);
            maybeDumpWordOptionsFrame(renderer, std::string("directional-") +
                std::string(label) + ".ppm");
            return hash(renderer);
        };
        if (stateHash("initial") != 0xb1520aa051f2d0e5ull) return false;
        const auto movement = [&](const int direction,
                                  const int toRow,
                                  const int toColumn,
                                  const std::string_view label,
                                  const std::vector<std::uint64_t>& expectedPhases,
                                  const std::uint64_t expectedTerminal,
                                  const std::uint64_t expectedStanding) {
            game.moveFromRow_ = game.playerRow_;
            game.moveFromColumn_ = game.playerColumn_;
            game.moveToRow_ = toRow;
            game.moveToColumn_ = toColumn;
            game.moveDirection_ = direction;
            game.moving_ = true;
            const int steps = (direction == 0 || direction == 2) ? 5 : 6;
            const double duration = game.playerMoveAnimationDuration(direction);
            for (int phase = 1; phase < steps; ++phase) {
                game.moveTimer_ = duration - phase / WordSchedulerTicksPerSecond;
                const std::uint64_t actual = stateHash(
                    std::string(label) + "-phase-" + std::to_string(phase));
                const std::uint64_t expected = expectedPhases[
                    static_cast<std::size_t>(phase - 1)];
                if (expected != 0 && actual != expected) return false;
            }
            game.moving_ = false;
            game.moveTimer_ = 0.0;
            game.playerRow_ = toRow;
            game.playerColumn_ = toColumn;
            const bool vertical = direction == 0 || direction == 2;
            game.playerTerminalFrame_ = direction * 3 + (vertical ? 2 : 1);
            if (expectedTerminal != 0 &&
                stateHash(std::string(label) + "-terminal") != expectedTerminal) {
                return false;
            }
            game.playerTerminalFrame_ = -1;
            return stateHash(std::string(label) + "-standing") == expectedStanding;
        };
        return movement(3, 2, 1, "left", {
                    0x095c18fb4159183full, 0x5c72469d262a7f70ull,
                    0x14c639e8ae73dbd4ull, 0x70837173e555809full,
                    0xdff0a418146468fcull,
                }, 0xfa5ff9d249642eabull, 0xa38181d4bdd7a498ull) &&
               movement(0, 1, 1, "up", {
                    0, 0xcc30702e95532784ull, 0xa83555055de4886bull,
                    0x5fd20038a728ce07ull,
                }, 0x8354513f78ff38a4ull, 0xcf97ebb5a6c7f684ull) &&
               movement(1, 1, 2, "right", {
                    0, 0x20d33b8f7d9dfa80ull, 0x5e4e80a82283b717ull,
                    0x8ec84507adce8e7cull, 0x0a6f3fa7db9fba3aull,
                }, 0x337b69ac6dfa07ddull, 0xe16659a4da7e5470ull) &&
               movement(2, 2, 2, "down", {
                    0xe4b80035436ea023ull, 0x22edcc317bcd8f16ull,
                    0x5c571a132c797eedull, 0x01b266d37b948261ull,
                }, 0xe077dcbbed08bacdull, 0xb1520aa051f2d0e5ull);
    }

    static bool liveWordCollisionBiteCompositeMatches() {
        WordGame game(GraphicsMode::Vga256, 0x5743u);
        game.startGame(1);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.attractMode_ = false;
        game.playerRow_ = 3;
        game.playerColumn_ = 3;
        game.safeCells_.fill(false);
        if (!game.core_.clearCellWithoutScore(21)) return false;

        game.enemySlotCount_ = 1;
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        WordGame::Enemy biter;
        biter.row = 3;
        biter.column = 3;
        biter.fromRow = 3;
        biter.fromColumn = 2;
        biter.direction = 1;
        biter.type = 0;
        biter.slot = 0;
        biter.moving = true;
        biter.moveTimer = 10.0;
        game.enemies_.push_back(biter);

        constexpr std::uint64_t LiveOpenHash = 0x2cc4ae9134dff1eeull;
        constexpr std::uint64_t LiveClosedHash = 0xe98dda1e9e05c039ull;
        const auto cellHash = [](const Renderer& renderer) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 116; y < 147; ++y) {
                for (int x = 164; x < 212; ++x) {
                    value ^= renderer.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };

        Renderer renderer(GraphicsMode::Vga256);
        constexpr std::array<std::uint64_t, 6> LiveApproachHashes = {
            0x8a6ac96bd4d361cbull, 0x55644716910f3d5cull,
            0xd53d0bb11b280463ull, 0x643ce67de4ed616eull,
            0xf6ea119e906493a3ull, 0x0db2a3f1d70b456bull,
        };
        const double moveDuration = game.enemyMoveAnimationDuration(1);
        const double moveInterval = 3.0 / WordSchedulerTicksPerSecond;
        bool approachMatches = true;
        for (int phase = 1; phase <= 6; ++phase) {
            game.enemies_[0].animationTimer =
                moveDuration - (phase - 1) * moveInterval;
            game.render(renderer);
            maybeDumpWordOptionsFrame(
                renderer, "collision-approach-phase-" + std::to_string(phase) + ".ppm");
            const std::uint64_t actual = cellHash(renderer);
            const std::uint64_t expected =
                LiveApproachHashes[static_cast<std::size_t>(phase - 1)];
            if (actual != expected) {
                std::cerr << "word live collision approach phase " << phase
                          << " hash=0x" << std::hex << actual << " expected=0x"
                          << expected << std::dec << '\n';
                approachMatches = false;
            }
        }
        if (!approachMatches) return false;

        game.enemies_[0].moving = false;
        game.enemies_[0].animationTimer = 0.0;
        game.beginTroggleCollision(0, 0);
        for (int tick = 0; tick < WordGame::TroggleEatAnimationTicks; ++tick) {
            game.deathSequenceTicks_ = tick;
            game.render(renderer);
            maybeDumpWordOptionsFrame(
                renderer, "collision-bite-tick-" + std::to_string(tick) + ".ppm");
            const std::uint64_t expected = (tick & 1) == 0
                ? LiveOpenHash : LiveClosedHash;
            const std::uint64_t actual = cellHash(renderer);
            if (actual != expected) {
                std::cerr << "word live collision bite tick " << tick << " hash=0x"
                          << std::hex << actual << " expected=0x" << expected
                          << std::dec << '\n';
                return false;
            }
        }

        // Number's fourth-Demo Bashful collision is byte-isomorphic at the
        // shared 41x29 actor box. Lock its stationary endpoint plus all 21
        // state-5 bite ticks here so Word cannot silently regress a species
        // that its smaller live collision corpus does not expose yet.
        WordGame bashful(GraphicsMode::Vga256, 0x5745u);
        bashful.startGame(1);
        disableBoardJobs(bashful);
        bashful.page_ = WordGamePage::Feedback;
        bashful.attractMode_ = true;
        bashful.playerRow_ = 0;
        bashful.playerColumn_ = 1;
        bashful.safeCells_.fill(false);
        bashful.enemySlotCount_ = 1;
        bashful.enemySlots_[0].type = 2;
        bashful.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        WordGame::Enemy bashfulBiter;
        bashfulBiter.row = 0;
        bashfulBiter.column = 1;
        bashfulBiter.fromRow = -1;
        bashfulBiter.fromColumn = 1;
        bashfulBiter.direction = 2;
        bashfulBiter.dwellFrame = 15;
        bashfulBiter.type = 2;
        bashfulBiter.slot = 0;
        bashful.enemies_.push_back(bashfulBiter);
        bashful.page_ = WordGamePage::Attract;
        bashful.feedbackEnemyType_ = -1;
        bashful.deathAnimating_ = false;
        bashful.enemies_[0].entering = true;
        bashful.enemies_[0].moving = true;
        const double bashfulEntryDuration =
            bashful.enemyMoveAnimationDuration(2);
        static constexpr std::array<std::uint64_t, 4>
            SharedBashfulTopEntryCellHashes = {
                0x52de6b1f5c0884d4ull, 0x9fdfe815f62a6030ull,
                0xc29127a1225db95cull, 0x025ccb235e45f928ull,
            };
        bool bashfulEntryHashesMatch = true;
        for (int phase = 2; phase <= 5; ++phase) {
            bashful.enemies_[0].animationTimer =
                (6 - phase) * bashfulEntryDuration / 5.0;
            bashful.render(renderer);
            std::uint64_t hash = 1469598103934665603ull;
            for (int y = 26; y < 57; ++y) {
                for (int x = 68; x < 116; ++x) {
                    hash ^= renderer.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    hash *= 1099511628211ull;
                }
            }
            const std::uint64_t expected =
                SharedBashfulTopEntryCellHashes[
                    static_cast<std::size_t>(phase - 2)];
            if (hash != expected) {
                std::cerr << "word shared Bashful top-entry phase " << phase
                          << " cell hash=0x" << std::hex << hash
                          << " expected=0x" << expected << std::dec << '\n';
                bashfulEntryHashesMatch = false;
            }
        }
        if (!bashfulEntryHashesMatch) return false;
        bashful.page_ = WordGamePage::Feedback;
        bashful.enemies_[0].entering = false;
        bashful.enemies_[0].moving = false;
        bashful.enemies_[0].animationTimer = 0.0;
        bashful.beginTroggleCollision(2, 0);

        const auto bashfulActorBoxHash = [](const Renderer& frame) {
            std::uint64_t value = 1469598103934665603ull;
            for (int y = 27; y < 56; ++y) {
                for (int x = 72; x < 113; ++x) {
                    value ^= frame.pixels()[static_cast<std::size_t>(
                        y * Renderer::Width + x)];
                    value *= 1099511628211ull;
                }
            }
            return value;
        };
        constexpr std::uint64_t CapturedBashfulDwellActorHash =
            0x25137412abcfe08eull;
        constexpr std::uint64_t CapturedBashfulOpenActorHash =
            0xb177653fb9abfa13ull;
        constexpr std::uint64_t CapturedBashfulClosedActorHash =
            0x85ac982b7b94c05full;
        const int selectedBashfulSlot = bashful.feedbackEnemySlot_;
        bashful.feedbackEnemySlot_ = bashful.enemySlotCount_ + 1;
        bashful.render(renderer);
        const std::uint64_t dwellActorHash = bashfulActorBoxHash(renderer);
        bashful.feedbackEnemySlot_ = selectedBashfulSlot;
        if (dwellActorHash != CapturedBashfulDwellActorHash) {
            std::cerr << "word captured Bashful dwell actor hash=0x" << std::hex
                      << dwellActorHash << " expected=0x"
                      << CapturedBashfulDwellActorHash << std::dec << '\n';
            return false;
        }
        for (int tick = 0; tick < WordGame::TroggleEatAnimationTicks; ++tick) {
            bashful.deathSequenceTicks_ = tick;
            bashful.render(renderer);
            const std::uint64_t actual = bashfulActorBoxHash(renderer);
            const std::uint64_t expected = (tick & 1) == 0
                ? CapturedBashfulOpenActorHash : CapturedBashfulClosedActorHash;
            if (actual != expected) {
                std::cerr << "word captured Bashful bite tick " << tick
                          << " actor hash=0x" << std::hex << actual
                          << " expected=0x" << expected << std::dec << '\n';
                return false;
            }
        }

        // Number's lossless Worker collision terminates in physical slot 1
        // while a slot-2 Reggie entry is due. The state-5 terminal aborts the
        // dispatcher at slot 1, so the later entry must retain its prior
        // painted phase under the feedback overlay. Gate the byte-isomorphic
        // Word scheduler boundary directly.
        WordGame terminal(GraphicsMode::Vga256, 0x5744u);
        terminal.startGame(1);
        disableBoardJobs(terminal);
        terminal.page_ = WordGamePage::Feedback;
        terminal.attractMode_ = false;
        terminal.playerRow_ = 2;
        terminal.playerColumn_ = 2;
        terminal.enemySlotCount_ = 3;
        for (int slot = 0; slot < terminal.enemySlotCount_; ++slot) {
            terminal.enemySlots_[static_cast<std::size_t>(slot)].phase =
                WordGame::EnemySlotPhase::Active;
        }

        WordGame::Enemy earlier;
        earlier.row = 4;
        earlier.column = 5;
        earlier.type = 1;
        earlier.slot = 0;
        earlier.moveTimer = 10.0;
        earlier.dwellFrame = 10;

        WordGame::Enemy selected;
        selected.row = terminal.playerRow_;
        selected.column = terminal.playerColumn_;
        selected.type = 1;
        selected.slot = 1;
        selected.dwellFrame = 15;

        WordGame::Enemy later;
        later.row = 4;
        later.column = 2;
        later.fromRow = 5;
        later.fromColumn = 2;
        later.type = 0;
        later.slot = 2;
        later.direction = 0;
        later.moving = true;
        later.entering = true;
        later.animationTimer = terminal.enemyMoveAnimationDuration(0) * 0.2;

        terminal.enemies_ = {earlier, selected, later};
        terminal.feedbackEnemyType_ = selected.type;
        terminal.feedbackEnemySlot_ = selected.slot;
        terminal.feedbackMessage_.clear();
        terminal.deathAnimating_ = true;
        terminal.deathSequenceTicks_ = WordGame::TroggleEatAnimationTicks - 2;
        terminal.gameplayTickAccumulator_ = 0.0;
        const double earlierTimer = earlier.moveTimer;
        const double laterTimer = later.animationTimer;
        const double tickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        terminal.updateDeathAnimation(tickSeconds + 1e-12);
        const bool finalPoseBoundary = terminal.deathAnimating_ &&
            terminal.deathSequenceTicks_ == WordGame::TroggleEatAnimationTicks - 1 &&
            !terminal.deathResidentSurfacePixels_.empty();
        Renderer resident(GraphicsMode::Vga256);
        terminal.renderPresentation(resident);
        const std::uint64_t residentHash = fullFrameHash(resident);
        const auto laterAtFinalPose = std::find_if(
            terminal.enemies_.begin(), terminal.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 2; });
        const bool laterHeldAtFinalPose =
            laterAtFinalPose != terminal.enemies_.end() &&
            std::abs(laterAtFinalPose->animationTimer - laterTimer) < 1e-12;
        terminal.updateDeathAnimation(tickSeconds + 1e-12);
        const auto earlierAfter = std::find_if(
            terminal.enemies_.begin(), terminal.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 0; });
        const auto laterAfter = std::find_if(
            terminal.enemies_.begin(), terminal.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 2; });
        const bool terminalBoundaryMatches =
            !terminal.deathAnimating_ &&
            terminal.deathSequenceTicks_ == WordGame::TroggleEatAnimationTicks &&
            !terminal.feedbackMessage_.empty() &&
            finalPoseBoundary && laterHeldAtFinalPose && residentHash != 0 &&
            terminal.deathResidentSurfacePixels_.empty() &&
            earlierAfter != terminal.enemies_.end() &&
            laterAfter != terminal.enemies_.end() &&
            std::abs(earlierAfter->moveTimer -
                     (earlierTimer - 2.0 * tickSeconds)) < 1e-9 &&
            std::abs(laterAfter->animationTimer - laterTimer) < 1e-12;
        if (!terminalBoundaryMatches) return false;

        // The selected state-5 record occupies physical slot 1. On ordinary
        // bite ticks, its complete callback page must be queued before a later
        // slot-2 entry repaint. The first clipped entry phase only clears its
        // swept area, so that callback page remains resident until the next
        // bite; later visible phases expose both the queued and final pages.
        WordGame ordered(GraphicsMode::Vga256, 0x5746u);
        ordered.startGame(1);
        disableBoardJobs(ordered);
        ordered.page_ = WordGamePage::Feedback;
        ordered.attractMode_ = false;
        ordered.playerRow_ = 2;
        ordered.playerColumn_ = 2;
        ordered.enemySlotCount_ = 3;
        for (int slot = 0; slot < ordered.enemySlotCount_; ++slot) {
            ordered.enemySlots_[static_cast<std::size_t>(slot)].phase =
                WordGame::EnemySlotPhase::Active;
        }
        earlier.row = 4;
        earlier.column = 5;
        earlier.slot = 0;
        earlier.moveTimer = 10.0;
        selected.row = ordered.playerRow_;
        selected.column = ordered.playerColumn_;
        selected.slot = 1;
        later.row = 4;
        later.column = 2;
        later.fromRow = 5;
        later.fromColumn = 2;
        later.slot = 2;
        later.direction = 0;
        later.entering = true;
        later.moving = true;
        later.animationTimer =
            ordered.enemyMoveAnimationDuration(0) - 2.0 * tickSeconds;
        ordered.enemies_ = {earlier, selected, later};
        ordered.feedbackEnemyType_ = selected.type;
        ordered.feedbackEnemySlot_ = selected.slot;
        ordered.feedbackMessage_.clear();
        ordered.deathAnimating_ = true;
        ordered.deathSequenceTicks_ = 8;
        ordered.gameplayTickAccumulator_ = 0.0;
        ordered.presentationFrames_.clear();
        ordered.deathResidentSurfacePixels_.clear();
        if (ordered.enemyMovementPhase(ordered.enemies_[2]) != 1) {
            std::cerr << "word ordered bite initial later phase="
                      << ordered.enemyMovementPhase(ordered.enemies_[2]) << '\n';
            return false;
        }

        ordered.updateDeathAnimation(tickSeconds + 1e-12);
        if (ordered.enemyMovementPhase(ordered.enemies_[2]) != 2 ||
            ordered.presentationFrames_.size() != 1 ||
            ordered.deathResidentSurfacePixels_ !=
                ordered.presentationFrames_.front()) {
            std::cerr << "word ordered bite first callback phase="
                      << ordered.enemyMovementPhase(ordered.enemies_[2])
                      << " queued=" << ordered.presentationFrames_.size()
                      << " retained="
                      << (!ordered.presentationFrames_.empty() &&
                          ordered.deathResidentSurfacePixels_ ==
                              ordered.presentationFrames_.front()) << '\n';
            return false;
        }
        Renderer firstClippedCallback(GraphicsMode::Vga256);
        Renderer firstClippedResident(GraphicsMode::Vga256);
        ordered.renderPresentation(firstClippedCallback);
        ordered.renderPresentation(firstClippedResident);
        if (fullFrameHash(firstClippedCallback) !=
            fullFrameHash(firstClippedResident)) return false;

        for (int attempt = 0;
             attempt < 8 && ordered.enemyMovementPhase(ordered.enemies_[2]) == 2;
             ++attempt) {
            ordered.gameplayTickAccumulator_ = 0.0;
            ordered.updateDeathAnimation(tickSeconds + 1e-12);
        }
        if (ordered.enemyMovementPhase(ordered.enemies_[2]) != 3 ||
            ordered.presentationFrames_.size() != 1 ||
            ordered.deathResidentSurfacePixels_ ==
                ordered.presentationFrames_.front()) {
            std::cerr << "word ordered bite visible callback phase="
                      << ordered.enemyMovementPhase(ordered.enemies_[2])
                      << " queued=" << ordered.presentationFrames_.size()
                      << " differs="
                      << (!ordered.presentationFrames_.empty() &&
                          ordered.deathResidentSurfacePixels_ !=
                              ordered.presentationFrames_.front()) << '\n';
            return false;
        }
        Renderer visibleCallback(GraphicsMode::Vga256);
        Renderer visibleFinal(GraphicsMode::Vga256);
        ordered.renderPresentation(visibleCallback);
        ordered.renderPresentation(visibleFinal);
        return fullFrameHash(visibleCallback) != fullFrameHash(visibleFinal);
    }

    static bool usesRecoveredLetterMovementAliases() {
        const auto routes = [](const wchar_t key, const int row, const int column,
                               const int direction) {
            WordGame game(GraphicsMode::Vga256, 0x4b45);
            game.startGame(1);
            disableBoardJobs(game);
            game.page_ = WordGamePage::Playing;
            game.playerRow_ = 2;
            game.playerColumn_ = 2;
            game.moving_ = false;
            game.munching_ = false;
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

        WordGame munch(GraphicsMode::Vga256, 0x4b46);
        munch.startGame(1);
        disableBoardJobs(munch);
        int occupied = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!munch.core_.eaten(static_cast<std::size_t>(index)) &&
                munch.core_.board().cells[static_cast<std::size_t>(index)].signedSourceIndex != 0) {
                occupied = index;
                break;
            }
        }
        if (occupied < 0) return false;
        munch.playerRow_ = occupied / WordGame::BoardColumns;
        munch.playerColumn_ = occupied % WordGame::BoardColumns;
        munch.keyDown(VK_SPACE);
        return munch.munching_ && !munch.moving_;
    }

    static bool usesRecoveredMixedGameplayInputRing() {
        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;

        WordGame movement(GraphicsMode::Vga256, 0x4e47);
        movement.startGame(1);
        disableBoardJobs(movement);
        movement.page_ = WordGamePage::Playing;
        movement.playerRow_ = 2;
        movement.playerColumn_ = 2;
        movement.moving_ = false;
        movement.munching_ = false;
        movement.playerRecovering_ = false;
        movement.deathAnimating_ = false;
        movement.pointerCellQueue_.clear();
        movement.gameplayTickAccumulator_ = 0.0;
        movement.keyDown(VK_RIGHT);
        movement.character(L'k');
        movement.keyDown(VK_DOWN);
        if (!movement.moving_ || movement.moveToColumn_ != 3 ||
            movement.pointerCellQueue_ != std::deque<int>{'K', 'M'}) return false;
        movement.update(WordGame::playerMoveAnimationDuration(1) + 0.001);
        if (!movement.moving_ || movement.playerColumn_ != 3 ||
            movement.moveToColumn_ != 4 ||
            movement.pointerCellQueue_ != std::deque<int>{'M'}) return false;
        movement.update(WordGame::playerMoveAnimationDuration(1) + 0.001);
        if (!movement.moving_ || movement.playerColumn_ != 4 ||
            movement.moveToRow_ != 3 || !movement.pointerCellQueue_.empty()) return false;

        WordGame chew(GraphicsMode::Vga256, 0x4557);
        chew.startGame(1);
        disableBoardJobs(chew);
        int correct = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            const WordBoardCell& cell =
                chew.core_.board().cells[static_cast<std::size_t>(index)];
            if (!chew.core_.eaten(static_cast<std::size_t>(index)) && cell.correct &&
                cell.signedSourceIndex > 0) {
                correct = index;
                break;
            }
        }
        if (correct < 0) return false;
        chew.playerRow_ = correct / WordGame::BoardColumns;
        chew.playerColumn_ = correct % WordGame::BoardColumns;
        const bool queueLeft = chew.playerColumn_ > 0;
        const int expectedColumn = chew.playerColumn_ + (queueLeft ? -1 : 1);
        const int scoreBefore = chew.core_.scoreState().score();
        chew.keyDown(VK_SPACE);
        chew.character(queueLeft ? L'j' : L'k');
        if (!chew.munching_ || chew.pointerCellQueue_ !=
                std::deque<int>{queueLeft ? 'J' : 'K'}) return false;
        chew.update(WordGame::MunchAnimationDuration + 0.001);
        if (chew.munching_ || chew.moving_ ||
            chew.core_.scoreState().score() != scoreBefore + 5 ||
            chew.pointerCellQueue_.size() != 1) return false;
        chew.update(TickSeconds + 1e-12);
        if (!chew.moving_ || chew.moveToColumn_ != expectedColumn ||
            !chew.pointerCellQueue_.empty()) return false;

        WordGame recovery(GraphicsMode::Vga256, 0x434f);
        recovery.startGame(1);
        disableBoardJobs(recovery);
        recovery.page_ = WordGamePage::Playing;
        recovery.playerRow_ = 2;
        recovery.playerColumn_ = 2;
        recovery.moving_ = false;
        recovery.munching_ = false;
        recovery.playerRecovering_ = true;
        recovery.deathAnimating_ = false;
        recovery.pointerCellQueue_.clear();
        recovery.gameplayTickAccumulator_ = 0.0;
        for (int index = 0; index < 10; ++index) recovery.character(L'k');
        if (recovery.moving_ || recovery.pointerCellQueue_.size() != 9) return false;
        recovery.playerRecovering_ = false;
        recovery.update(TickSeconds + 1e-12);
        if (!recovery.moving_ || recovery.moveDirection_ != 1 ||
            recovery.moveToColumn_ != 3 || recovery.pointerCellQueue_.size() != 8) {
            return false;
        }

        WordGame pause(GraphicsMode::Vga256, 0x5553);
        pause.startGame(1);
        disableBoardJobs(pause);
        pause.page_ = WordGamePage::Playing;
        pause.playerRow_ = 2;
        pause.playerColumn_ = 2;
        pause.moving_ = false;
        pause.munching_ = false;
        pause.pointerCellQueue_.clear();
        pause.keyDown(VK_RIGHT);
        pause.character(L'k');
        pause.pointerButton(WordGame::BoardLeft + 5 * WordGame::BoardCellWidth + 1,
                            WordGame::BoardTop + 2 * WordGame::BoardCellHeight + 1, false);
        if (pause.pointerCellQueue_.size() != 2) return false;
        pause.keyDown(VK_RETURN);
        if (pause.page_ != WordGamePage::Paused || !pause.moving_ ||
            !pause.pointerCellQueue_.empty()) return false;
        pause.pointerCellQueue_.push_back('J');
        pause.keyDown(VK_RETURN);
        return pause.page_ == WordGamePage::Playing && pause.moving_ &&
               pause.pointerCellQueue_.empty();
    }

    static bool usesRecoveredCartoonInputGate() {
        const auto prepare = [](WordGame& game) {
            game.startGame(3);
            disableBoardJobs(game);
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                (void)game.core_.clearCellWithoutScore(static_cast<std::size_t>(index));
            }
            if (!game.core_.completeBoardIfEmpty() || !game.core_.cartoonPending()) return false;
            game.handleCompletedBoard();
            while (game.attractInterstitialTransition_ !=
                   WordGame::AttractInterstitialTransition::None) {
                game.attractTransitionTimer_ = 0.0;
                game.update(0.0);
            }
            game.update(0.1);
            return game.page_ == WordGamePage::LevelComplete &&
                   game.levelCompleteScene_.valid() &&
                   game.attractInterstitialTransition_ ==
                       WordGame::AttractInterstitialTransition::None;
        };

        WordGame ordinary(GraphicsMode::Vga256, 0x4b47);
        if (!prepare(ordinary)) return false;
        ordinary.keyDown(VK_ESCAPE);
        ordinary.keyDown(VK_SHIFT);
        if (ordinary.page_ != WordGamePage::LevelComplete || ordinary.core_.level() != 3) {
            return false;
        }
        ordinary.keyDown(VK_LEFT);
        if (ordinary.page_ != WordGamePage::Playing || ordinary.core_.level() != 4 ||
            ordinary.core_.cartoonPending()) return false;

        WordGame printable(GraphicsMode::Vga256, 0x4b48);
        if (!prepare(printable)) return false;
        printable.keyDown('Q');
        if (printable.page_ != WordGamePage::LevelComplete) return false;
        printable.character(L'q');
        if (printable.page_ != WordGamePage::Playing || printable.core_.level() != 4 ||
            printable.moving_ || printable.munching_) return false;

        WordGame repeatControl(GraphicsMode::Vga256, 0x4b4e);
        if (!prepare(repeatControl)) return false;
        repeatControl.soundOn_ = false;
        repeatControl.speakerEffects_ = false;
        repeatControl.joystickEnabled_ = true;
        repeatControl.joystickRepeatTicks_ = 4;
        repeatControl.keyDown(VK_OEM_MINUS);
        if (repeatControl.page_ != WordGamePage::LevelComplete ||
            repeatControl.joystickRepeatTicks_ != 4) return false;
        repeatControl.character(L'-');
        if (repeatControl.page_ != WordGamePage::Playing ||
            repeatControl.core_.level() != 4 || repeatControl.joystickRepeatTicks_ != 5 ||
            repeatControl.moving_ || repeatControl.munching_ ||
            repeatControl.lastJoystickRepeatFeedbackHz_ != 750 ||
            !repeatControl.sceneEffectPlayer_.playing() ||
            repeatControl.sceneEffectPlayer_.bufferSize() != 4454) return false;

        WordGame joystickButton(GraphicsMode::Vga256, 0x4b49);
        if (!prepare(joystickButton)) return false;
        joystickButton.keyDown(VK_SPACE);
        if (joystickButton.page_ != WordGamePage::Playing ||
            joystickButton.core_.level() != 4 || joystickButton.munching_) return false;

        WordGame pointer(GraphicsMode::Vga256, 0x4b4d);
        if (!prepare(pointer) || !pointer.pointerPress(true)) return false;
        return pointer.page_ == WordGamePage::Playing && pointer.core_.level() == 4;
    }

    static bool usesRecoveredBusyPauseGate() {
        WordGame moving(GraphicsMode::Vga256, 0x4b4a);
        moving.startGame(1);
        disableBoardJobs(moving);
        moving.page_ = WordGamePage::Playing;
        moving.playerRow_ = 2;
        moving.playerColumn_ = 2;
        moving.beginMove(2, 3, 1);
        const double moveTimer = moving.moveTimer_;
        moving.keyDown(VK_RETURN);
        if (moving.page_ != WordGamePage::Paused || !moving.moving_) return false;
        moving.update(1.0);
        if (moving.moveTimer_ != moveTimer || !moving.moving_) return false;
        moving.keyDown(VK_RETURN);
        moving.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (moving.page_ != WordGamePage::Playing || !moving.moving_ ||
            !(moving.moveTimer_ < moveTimer)) return false;

        WordGame chewing(GraphicsMode::Vga256, 0x4b4b);
        chewing.startGame(1);
        disableBoardJobs(chewing);
        int occupied = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!chewing.core_.eaten(static_cast<std::size_t>(index)) &&
                chewing.core_.board().cells[static_cast<std::size_t>(index)].signedSourceIndex != 0) {
                occupied = index;
                break;
            }
        }
        if (occupied < 0) return false;
        chewing.playerRow_ = occupied / WordGame::BoardColumns;
        chewing.playerColumn_ = occupied % WordGame::BoardColumns;
        chewing.beginMunch();
        const double munchTimer = chewing.munchTimer_;
        chewing.keyDown(VK_RETURN);
        chewing.update(1.0);
        if (chewing.page_ != WordGamePage::Paused || !chewing.munching_ ||
            chewing.munchTimer_ != munchTimer) return false;

        WordGame recovering(GraphicsMode::Vga256, 0x4b4c);
        recovering.startGame(1);
        disableBoardJobs(recovering);
        recovering.playerRecovering_ = true;
        recovering.keyDown(VK_RETURN);
        if (recovering.page_ != WordGamePage::Paused || !recovering.playerRecovering_) {
            return false;
        }

        WordGame pointer(GraphicsMode::Vga256, 0x4b4e);
        pointer.startGame(1);
        disableBoardJobs(pointer);
        if (!pointer.pointerPress(true) || pointer.page_ != WordGamePage::Paused ||
            !pointer.pointerPress(true) || pointer.page_ != WordGamePage::Playing) return false;
        pointer.page_ = WordGamePage::Title;
        return !pointer.pointerPress(true);
    }

    static bool usesRecoveredMunchDispatcher() {
        constexpr std::array<int, 8> Frames = {12, 13, 14, 13, 12, 13, 14, 13};
        if (std::abs(WordGame::MunchAnimationDuration -
                     WordGame::MunchAnimationTicks / WordSchedulerTicksPerSecond) > 1e-12) {
            return false;
        }
        for (int tick = 0; tick <= WordGame::MunchAnimationTicks; ++tick) {
            if (WordGame::munchFrameAtTick(tick) != Frames[static_cast<std::size_t>(tick)]) {
                return false;
            }
        }
        if (WordGame::munchFrameAtTick(-1) != 12 ||
            WordGame::munchFrameAtTick(WordGame::MunchAnimationTicks + 1) != 13) return false;

        WordGame staged(GraphicsMode::Vga256, 0x5a17);
        staged.startGame(1);
        disableBoardJobs(staged);
        int correct = -1;
        int wrong = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (staged.core_.eaten(static_cast<std::size_t>(index))) continue;
            if (staged.core_.board().cells[static_cast<std::size_t>(index)].correct && correct < 0) {
                correct = index;
            }
            if (!staged.core_.board().cells[static_cast<std::size_t>(index)].correct && wrong < 0) {
                wrong = index;
            }
        }
        if (correct < 0 || wrong < 0) return false;
        const int remaining = staged.core_.correctRemaining();
        const WordBoardCell replacement =
            staged.core_.board().cells[static_cast<std::size_t>(wrong)];

        // The presentation-side guard must reject the original zero board
        // word before starting the chew job (and before choosing cue 8).
        if (!staged.core_.restoreCell(static_cast<std::size_t>(correct), {}, false)) return false;
        staged.playerRow_ = correct / WordGame::BoardColumns;
        staged.playerColumn_ = correct % WordGame::BoardColumns;
        staged.beginMunch();
        if (staged.munching_ || staged.munchCellIndex_ != -1 ||
            !staged.core_.restoreCell(static_cast<std::size_t>(correct), replacement, false)) {
            return false;
        }

        // Restore a positive record for the saved-answer terminal test below.
        const auto positive = std::find_if(staged.core_.board().cells.begin(),
                                           staged.core_.board().cells.end(),
            [](const WordBoardCell& cell) { return cell.signedSourceIndex > 0; });
        if (positive == staged.core_.board().cells.end() ||
            !staged.core_.restoreCell(static_cast<std::size_t>(correct), *positive, false)) {
            return false;
        }
        staged.playerRow_ = correct / WordGame::BoardColumns;
        staged.playerColumn_ = correct % WordGame::BoardColumns;
        staged.beginMunch();
        if (!staged.munching_ || !staged.core_.eaten(static_cast<std::size_t>(correct)) ||
            staged.core_.correctRemaining() != remaining) return false;

        // A background board mutation must not change the saved answer or be
        // cleared again by the chew terminal.
        if (!staged.core_.restoreCell(static_cast<std::size_t>(correct), replacement, false) ||
            staged.core_.correctRemaining() != remaining) return false;
        staged.resolveMunch();
        if (staged.munching_ || staged.lastResolution_.kind != WordMunchKind::Correct ||
            staged.lastResolution_.points != 5 || staged.core_.scoreState().score() != 5 ||
            staged.core_.correctRemaining() != remaining - 1 ||
            staged.core_.eaten(static_cast<std::size_t>(correct)) ||
            !(staged.core_.board().cells[static_cast<std::size_t>(correct)] == replacement)) {
            return false;
        }

        const auto prepareFinal = [](WordGame& game, int& finalCell) {
            game.startGame(1);
            disableBoardJobs(game);
            finalCell = -1;
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (!game.core_.eaten(static_cast<std::size_t>(index)) &&
                    game.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                    if (finalCell < 0) {
                        finalCell = index;
                    } else if (!game.core_.clearCellWithoutScore(static_cast<std::size_t>(index))) {
                        return false;
                    }
                }
            }
            if (finalCell < 0 || game.core_.correctRemaining() != 1) return false;
            game.playerRow_ = finalCell / WordGame::BoardColumns;
            game.playerColumn_ = finalCell % WordGame::BoardColumns;
            game.beginMunch();
            game.munchTimer_ = 1.0 / WordSchedulerTicksPerSecond;
            game.gameplayTickAccumulator_ = 0.0;
            return game.munching_;
        };
        WordGame direct(GraphicsMode::Vga256, 0x4d43u);
        WordGame scheduled(GraphicsMode::Vga256, 0x4d43u);
        int directCell = -1;
        int scheduledCell = -1;
        if (!prepareFinal(direct, directCell) || !prepareFinal(scheduled, scheduledCell) ||
            directCell != scheduledCell) return false;
        constexpr double postTerminalTicks = 1.25;
        direct.resolveMunch();
        direct.update(postTerminalTicks / WordSchedulerTicksPerSecond);
        scheduled.update((1.0 + postTerminalTicks) / WordSchedulerTicksPerSecond);
        if (direct.page_ != WordGamePage::Playing ||
            scheduled.page_ != WordGamePage::Playing || direct.core_.level() != 2 ||
            scheduled.core_.level() != 2 ||
            direct.boardGenerationSerial_ != scheduled.boardGenerationSerial_ ||
            direct.random_.state != scheduled.random_.state ||
            direct.random_.calls != scheduled.random_.calls ||
            direct.enemySlotCount_ != scheduled.enemySlotCount_ ||
            direct.safeZoneJobCount_ != scheduled.safeZoneJobCount_ ||
            std::abs(direct.gameplayTickAccumulator_) > 1e-12 ||
            std::abs(scheduled.gameplayTickAccumulator_) > 1e-12 ||
            direct.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToBoard ||
            scheduled.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToBoard ||
            direct.attractInterstitialFrame_ != scheduled.attractInterstitialFrame_ ||
            std::abs(direct.attractTransitionTimer_ -
                     scheduled.attractTransitionTimer_) > 1e-12) {
            return false;
        }
        for (int index = 0; index < direct.enemySlotCount_; ++index) {
            const WordGame::EnemySlot& expected =
                direct.enemySlots_[static_cast<std::size_t>(index)];
            const WordGame::EnemySlot& actual =
                scheduled.enemySlots_[static_cast<std::size_t>(index)];
            if (expected.type != actual.type || expected.phase != actual.phase ||
                expected.timer != actual.timer) return false;
        }
        for (int index = 0; index < direct.safeZoneJobCount_; ++index) {
            const WordGame::SafeZoneJob& expected =
                direct.safeZoneJobs_[static_cast<std::size_t>(index)];
            const WordGame::SafeZoneJob& actual =
                scheduled.safeZoneJobs_[static_cast<std::size_t>(index)];
            if (expected.active != actual.active || expected.cellIndex != actual.cellIndex ||
                expected.period != actual.period || expected.timer != actual.timer) return false;
        }

        WordGame protectedChew(GraphicsMode::Vga256, 0x4348u);
        protectedChew.startGame(1);
        disableBoardJobs(protectedChew);
        protectedChew.enemySlotCount_ = 1;
        protectedChew.enemySlots_[0] = {};
        protectedChew.enemySlots_[0].type = 0;
        protectedChew.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        int protectedCell = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!protectedChew.core_.eaten(static_cast<std::size_t>(index)) &&
                protectedChew.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                protectedCell = index;
                break;
            }
        }
        if (protectedCell < 0) return false;
        protectedChew.playerRow_ = protectedCell / WordGame::BoardColumns;
        protectedChew.playerColumn_ = protectedCell % WordGame::BoardColumns;
        protectedChew.beginMunch();
        protectedChew.munchTimer_ = 2.0 / WordSchedulerTicksPerSecond;
        WordGame::Enemy endpoint;
        endpoint.row = protectedChew.playerRow_;
        endpoint.column = protectedChew.playerColumn_;
        endpoint.fromRow = endpoint.row;
        endpoint.fromColumn = std::max(0, endpoint.column - 1);
        endpoint.type = 0;
        endpoint.slot = 0;
        endpoint.moving = true;
        endpoint.animationTimer = 1.0 / WordSchedulerTicksPerSecond;
        protectedChew.enemies_.push_back(endpoint);
        const int scoreBefore = protectedChew.core_.scoreState().score();
        const int remainingBefore = protectedChew.core_.correctRemaining();
        protectedChew.gameplayTickAccumulator_ = 0.0;
        protectedChew.update(2.25 / WordSchedulerTicksPerSecond);
        if (protectedChew.page_ != WordGamePage::Playing || protectedChew.deathAnimating_ ||
            protectedChew.munching_ ||
            protectedChew.core_.scoreState().score() != scoreBefore + 5 ||
            protectedChew.core_.correctRemaining() != remainingBefore - 1 ||
            protectedChew.enemies_.empty() || protectedChew.enemies_[0].moving ||
            std::abs(protectedChew.gameplayTickAccumulator_ - 0.25) > 1e-12) {
            return false;
        }
        return true;
    }

    static bool usesRecoveredPlayerCollisionStateGate(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.playerRecovering_ = false;
        game.deathAnimating_ = false;
        game.enemies_.clear();
        game.enemySlotCount_ = 1;
        game.enemySlots_[0] = {};
        game.enemySlots_[0].type = 0;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;

        WordGame::Enemy endpoint;
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

    static bool preservesMixedTroggleJobOrder() {
        WordGame game(GraphicsMode::Vga256, 0x4f52);
        game.startGame(10);
        disableBoardJobs(game);
        game.page_ = WordGamePage::Playing;
        game.playerRow_ = 2;
        game.playerColumn_ = 1;
        game.random_.seed(62853u);
        for (int call = 0; call < 185; ++call) (void)game.random_.next();

        game.enemySlotCount_ = 3;
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].type = 0;
            game.enemySlots_[static_cast<std::size_t>(slot)].phase =
                WordGame::EnemySlotPhase::Active;
        }
        game.enemySlots_[1].phase = WordGame::EnemySlotPhase::Waiting;
        game.enemySlots_[1].timer = 0.0;

        WordGame::Enemy endpoint;
        endpoint.fromRow = 4;
        endpoint.fromColumn = 3;
        endpoint.row = 4;
        endpoint.column = 4;
        endpoint.direction = 1;
        endpoint.type = 0;
        endpoint.slot = 2;
        endpoint.moving = true;
        endpoint.animationTimer = 0.0;
        game.enemies_ = {endpoint};
        game.updateEnemies(0.0);

        const auto survivor = std::find_if(game.enemies_.begin(), game.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 2; });
        const auto returning = std::find_if(game.enemies_.begin(), game.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 1; });
        if (game.random_.calls != 188 || survivor == game.enemies_.end() ||
            returning == game.enemies_.end() ||
            std::abs(survivor->moveTimer * WordSchedulerTicksPerSecond - 95.0) >= 0.0001 ||
            !returning->entering || returning->moving || returning->row != 1 ||
            returning->column != 0 || returning->fromRow != 1 ||
            returning->fromColumn != -1 || returning->direction != 1) {
            return false;
        }

        // A collocated non-selected Troggle enters inert state 6, but that
        // cannot abort the scan before a later unrelated recurring job.
        game.random_.seed(0x4f524445u);
        game.page_ = WordGamePage::Playing;
        game.deathAnimating_ = false;
        game.playerRecovering_ = false;
        game.feedbackEnemySlot_ = -1;
        game.playerRow_ = 2;
        game.playerColumn_ = 2;
        game.moving_ = false;
        game.munching_ = false;
        game.enemySlotCount_ = 3;
        for (int slot = 0; slot < game.enemySlotCount_; ++slot) {
            game.enemySlots_[static_cast<std::size_t>(slot)] = {};
            game.enemySlots_[static_cast<std::size_t>(slot)].type = 0;
        }
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        game.enemySlots_[1].phase = WordGame::EnemySlotPhase::Active;
        game.enemySlots_[2].phase = WordGame::EnemySlotPhase::Waiting;
        game.enemySlots_[2].timer = 0.0;

        WordGame::Enemy collider;
        collider.row = game.playerRow_;
        collider.column = game.playerColumn_;
        collider.fromRow = game.playerRow_;
        collider.fromColumn = game.playerColumn_ - 1;
        collider.type = 0;
        collider.slot = 0;
        collider.moving = true;
        collider.animationTimer = 0.0;
        WordGame::Enemy hiddenCollider = collider;
        hiddenCollider.slot = 1;
        hiddenCollider.moving = false;
        hiddenCollider.moveTimer = 0.0;
        game.enemies_ = {collider, hiddenCollider};
        const std::uint64_t callsBeforeCollision = game.random_.calls;
        if (!game.updateEnemies(0.0) || !game.deathAnimating_ ||
            game.feedbackEnemySlot_ != 0 ||
            game.random_.calls != callsBeforeCollision + 3 ||
            game.enemySlots_[2].phase != WordGame::EnemySlotPhase::Warning) {
            return false;
        }
        const auto hidden = std::find_if(game.enemies_.begin(), game.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 1; });
        const auto laterWarning = std::find_if(game.enemies_.begin(), game.enemies_.end(),
            [](const WordGame::Enemy& enemy) { return enemy.slot == 2; });
        return hidden != game.enemies_.end() && hidden->collisionHidden &&
               hidden->overlapFrozen && !hidden->moving && hidden->moveTimer == 0.0 &&
               laterWarning != game.enemies_.end() && laterWarning->entering &&
               !laterWarning->moving;
    }

    static bool preservesNewMunchTimerAfterMovement(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        constexpr int FromRow = 2;
        constexpr int FromColumn = 1;
        constexpr int ToRow = 2;
        constexpr int ToColumn = 2;
        constexpr int Target = ToRow * WordGame::BoardColumns + ToColumn;
        const WordBoardCell destination =
            game.core_.board().cells[static_cast<std::size_t>(Target)];
        if (!game.core_.restoreCell(static_cast<std::size_t>(Target), destination, false)) {
            return false;
        }

        game.playerRow_ = FromRow;
        game.playerColumn_ = FromColumn;
        game.moveFromRow_ = FromRow;
        game.moveFromColumn_ = FromColumn;
        game.moveToRow_ = ToRow;
        game.moveToColumn_ = ToColumn;
        game.moveDirection_ = 1;
        constexpr double TickSeconds = 1.0 / WordSchedulerTicksPerSecond;
        game.moveTimer_ = TickSeconds;
        game.moving_ = true;
        game.munching_ = false;
        game.pointerCellQueue_.clear();
        game.pointerCellQueue_.push_back(-Target);
        game.gameplayTickAccumulator_ = 0.0;

        game.update(1.25 * TickSeconds);
        return !game.moving_ && game.munching_ && game.playerRow_ == ToRow &&
               game.playerColumn_ == ToColumn && game.pointerCellQueue_.empty() &&
               std::abs(game.munchTimer_ - WordGame::MunchAnimationDuration) < 1e-12 &&
               std::abs(game.gameplayTickAccumulator_ - 0.25) < 1e-12;
    }

    static bool initializesAndWarnsOnOriginalJobs(WordGame& game) {
        game.startGame(1);
        if (game.page_ != WordGamePage::Playing || game.enemySlotCount_ != 1 ||
            game.safeZoneJobCount_ != 2 || game.enemySlots_[0].type != 0 ||
            game.enemySlots_[0].phase != WordGame::EnemySlotPhase::Waiting ||
            game.playerRow_ < 1 || game.playerRow_ > 3 ||
            game.playerColumn_ < 1 || game.playerColumn_ > 4) return false;
        const int playerIndex = game.playerRow_ * WordGame::BoardColumns + game.playerColumn_;
        if (!game.core_.eaten(static_cast<std::size_t>(playerIndex))) return false;
        const int safeCount = static_cast<int>(std::count(
            game.safeCells_.begin(), game.safeCells_.end(), true));
        if (safeCount != 1 || !game.safeZoneJobs_[0].active ||
            game.safeZoneJobs_[1].active) return false;
        for (int index = 0; index < game.safeZoneJobCount_; ++index) {
            const double ticks = game.safeZoneJobs_[static_cast<std::size_t>(index)].period *
                                 WordSchedulerTicksPerSecond;
            if (ticks < 209.999 || ticks > 419.001) return false;
        }
        const double arrivalTicks = game.enemySlots_[0].timer * WordSchedulerTicksPerSecond;
        if (arrivalTicks < 299.999 || arrivalTicks > 389.001) return false;

        Renderer renderer;
        game.render(renderer);
        const auto safe = std::find(game.safeCells_.begin(), game.safeCells_.end(), true);
        if (safe == game.safeCells_.end()) return false;
        const int safeIndex = static_cast<int>(safe - game.safeCells_.begin());
        const int safeX = WordGame::BoardLeft + (safeIndex % WordGame::BoardColumns) *
                          WordGame::BoardCellWidth + 1;
        const int safeY = WordGame::BoardTop + (safeIndex / WordGame::BoardColumns) *
                          WordGame::BoardCellHeight + 1;
        if (renderer.pixels()[static_cast<std::size_t>(safeY * Renderer::Width + safeX)] !=
            Colors::White) return false;

        game.enemySlots_[0].timer = 0.0;
        game.updateEnemies(0.0);
        if (!game.enemyWarning_ || game.enemies_.size() != 1 ||
            game.enemySlots_[0].phase != WordGame::EnemySlotPhase::Warning ||
            std::abs(game.enemySlots_[0].timer - WordGame::TroggleWarningDuration) > 1e-9 ||
            !game.enemies_[0].entering || game.enemies_[0].moving) return false;
        game.render(renderer);
        if (renderer.pixels()[62u * Renderer::Width] != Colors::White) return false;

        constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;
        game.updateEnemies(89.0 * tick);
        if (game.enemySlots_[0].phase != WordGame::EnemySlotPhase::Warning) return false;
        game.updateEnemies(tick);
        if (game.enemySlots_[0].phase != WordGame::EnemySlotPhase::Active ||
            !game.enemies_[0].moving || !game.enemies_[0].entering || game.enemyWarning_) {
            return false;
        }
        const WordGame::Enemy& entering = game.enemies_[0];
        if (std::abs(entering.animationTimer -
                     game.enemyMoveAnimationDuration(entering.direction)) > 1e-12 ||
            game.enemyMovementPhase(entering) != 1) {
            return false;
        }
        return true;
    }

    static bool defersCollisionLifeLossToTick21(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        game.enemySlotCount_ = 1;
        game.enemySlots_[0].type = 4;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        WordGame::Enemy enemy;
        enemy.row = game.playerRow_;
        enemy.column = game.playerColumn_;
        enemy.fromRow = enemy.row;
        enemy.fromColumn = enemy.column;
        enemy.type = 4;
        enemy.slot = 0;
        enemy.moveTimer = 10.0;
        game.enemies_.push_back(enemy);
        const int reserves = game.core_.scoreState().reserves();
        game.beginTroggleCollision(4, 0);
        if (game.page_ != WordGamePage::Feedback || !game.deathAnimating_ ||
            game.deathSequenceTicks_ != 0 || game.core_.scoreState().reserves() != reserves) {
            return false;
        }

        constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;
        const std::uint64_t callsBeforeBite = game.random_.calls;
        for (int index = 0; index < WordGame::TroggleEatAnimationTicks - 1; ++index) {
            game.updateDeathAnimation(tick + 1e-12);
        }
        if (!game.deathAnimating_ ||
            game.deathSequenceTicks_ != WordGame::TroggleEatAnimationTicks - 1 ||
            game.core_.scoreState().reserves() != reserves) return false;
        game.updateDeathAnimation(tick + 1e-12);
        if (game.deathAnimating_ ||
            game.deathSequenceTicks_ != WordGame::TroggleEatAnimationTicks ||
            game.core_.scoreState().reserves() != reserves - 1 ||
            game.feedbackMessage_.empty() || game.random_.calls != callsBeforeBite + 2) {
            return false;
        }
        Renderer renderer;
        game.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "collision-feedback-native.ppm");
        if (renderer.pixels()[116u * Renderer::Width + 30u] != Colors::Magenta) return false;
        const std::uint64_t collisionHash = feedbackStripHash(renderer);
        constexpr std::uint64_t ExpectedCollisionFeedbackHash = 0x24575203d8d68f0eull;
        if (collisionHash != ExpectedCollisionFeedbackHash) {
            std::cerr << "word collision-feedback strip hash=0x" << std::hex
                      << collisionHash << std::dec << "\n";
            return false;
        }
        // The controlled live DOS capture used the first phrase and Reggie.
        // Pin that exact composition independently of this fixture's seeded
        // Oops/Smarty scheduler result.
        game.feedbackMessage_ = "Oh, Oh";
        game.feedbackEnemyType_ = 0;
        game.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "collision-feedback-live-state-native.ppm");
        constexpr std::uint64_t ExpectedLiveCollisionFeedbackHash =
            0x9d7e227a62f941ccull;
        const std::uint64_t liveCollisionHash = feedbackStripHash(renderer);
        if (liveCollisionHash != ExpectedLiveCollisionFeedbackHash) {
            std::cerr << "word live collision-feedback strip hash=0x" << std::hex
                      << liveCollisionHash << std::dec << "\n";
            return false;
        }
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::Playing || !game.playerRecovering_) return false;
        const int row = game.playerRow_;
        const int column = game.playerColumn_;
        game.keyDown(VK_RIGHT);
        return !game.moving_ && game.playerRow_ == row && game.playerColumn_ == column;
    }

    static bool preservesDemoCollisionTerminalOrderAndFrameRemainder() {
        WordGame game(GraphicsMode::Vga256, 0x6a31);
        game.startGame(1);
        disableBoardJobs(game);
        game.attractMode_ = true;
        game.page_ = WordGamePage::Attract;
        game.enemySlotCount_ = 1;
        game.enemySlots_[0].type = 4;
        game.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;

        WordGame::Enemy biter;
        biter.row = game.playerRow_;
        biter.column = game.playerColumn_;
        biter.fromRow = biter.row;
        biter.fromColumn = biter.column;
        biter.type = 4;
        biter.slot = 0;
        biter.moveTimer = 10.0;
        game.enemies_.push_back(biter);
        game.beginTroggleCollision(4, 0);

        constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;
        game.deathSequenceTicks_ = WordGame::TroggleEatAnimationTicks - 2;
        game.gameplayTickAccumulator_ = 0.0;
        game.attractActionTimer_ = tick;

        OriginalRandom expected = game.random_;
        (void)expected.range(90); // selected biter's terminal dwell
        (void)expected.range(5);  // feedback phrase
        const int expectedReloadTicks = expected.range(30) + 15;
        const std::uint64_t callsBefore = game.random_.calls;

        // Two whole collision ticks reach the terminal; one quarter tick of
        // the same display update must immediately age the feedback hold.
        game.update(2.25 * tick);
        return !game.deathAnimating_ && game.page_ == WordGamePage::Feedback &&
               game.random_.calls == callsBefore + 3 &&
               game.random_.state == expected.state &&
               std::abs(game.attractActionTimer_ / tick - expectedReloadTicks) < 1e-7 &&
               std::abs(game.attractTransitionTimer_ / tick - 149.75) < 1e-7 &&
               std::abs(game.gameplayTickAccumulator_) < 1e-12;
    }

    static bool appliesWordTrailEffects(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        int wrong = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!game.core_.eaten(static_cast<std::size_t>(index)) &&
                !game.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                wrong = index;
                break;
            }
        }
        if (wrong < 0) return false;
        WordGame::Enemy worker;
        worker.row = wrong / WordGame::BoardColumns;
        worker.column = wrong % WordGame::BoardColumns;
        worker.type = 1;
        worker.savedCell = game.core_.board().cells[static_cast<std::size_t>(wrong)];
        worker.savedCellEaten = false;
        worker.savedCellValid = true;
        const std::uint64_t beforeCalls = game.random_.calls;
        game.applyEnemyCellEffect(worker);
        if (game.random_.calls != beforeCalls + 2 ||
            game.core_.eaten(static_cast<std::size_t>(wrong)) ||
            game.core_.board().cells[static_cast<std::size_t>(wrong)].signedSourceIndex == 0) {
            return false;
        }

        const WordBoardCell saved = game.core_.board().cells[static_cast<std::size_t>(wrong)];
        WordGame::Enemy helper = worker;
        helper.type = 3;
        game.applyEnemyCellEffect(helper);
        if (!game.core_.eaten(static_cast<std::size_t>(wrong))) return false;
        WordGame::Enemy smarty = worker;
        smarty.type = 4;
        smarty.savedCell = saved;
        smarty.savedCellEaten = false;
        game.applyEnemyCellEffect(smarty);
        if (game.core_.eaten(static_cast<std::size_t>(wrong)) ||
            !(game.core_.board().cells[static_cast<std::size_t>(wrong)] == saved)) return false;

        (void)game.core_.clearCellWithoutScore(static_cast<std::size_t>(wrong));
        WordGame::Enemy reggie = worker;
        reggie.type = 0;
        reggie.savedCellEaten = true;
        const std::uint64_t blankCalls = game.random_.calls;
        game.applyEnemyCellEffect(reggie);
        return game.core_.eaten(static_cast<std::size_t>(wrong)) &&
               game.random_.calls == blankCalls;
    }

    static bool abortsTrailCallbacksAfterBoardCompletion(WordGame& safeZone) {
        const auto leaveOnly = [](WordGame& game, const int finalCell) {
            disableBoardJobs(game);
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (index == finalCell || game.core_.eaten(static_cast<std::size_t>(index)) ||
                    !game.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                    continue;
                }
                if (!game.core_.clearCellWithoutScore(static_cast<std::size_t>(index))) {
                    return false;
                }
            }
            game.lastGameplaySound_ = -1;
            game.previousGameplaySound_ = -1;
            return !game.core_.eaten(static_cast<std::size_t>(finalCell)) &&
                   game.core_.board().cells[static_cast<std::size_t>(finalCell)].correct &&
                   game.core_.correctRemaining() == 1;
        };
        const auto helperAt = [](const WordGame& game, const int cellIndex) {
            WordGame::Enemy helper;
            helper.row = cellIndex / WordGame::BoardColumns;
            helper.column = cellIndex % WordGame::BoardColumns;
            helper.fromRow = helper.row;
            helper.fromColumn = helper.column;
            helper.type = 3;
            helper.slot = 0;
            helper.savedCell = game.core_.board().cells[static_cast<std::size_t>(cellIndex)];
            helper.savedCellEaten = false;
            helper.savedCellValid = true;
            return helper;
        };

        safeZone.startGame(1);
        int safeCell = -1;
        for (std::uint32_t seed = 1; seed <= 0xffffu; ++seed) {
            OriginalRandom probe(seed);
            const int candidate = (1 + probe.range(WordGame::BoardRows - 2)) *
                                      WordGame::BoardColumns +
                                  1 + probe.range(WordGame::BoardColumns - 2);
            if (!safeZone.core_.eaten(static_cast<std::size_t>(candidate)) &&
                safeZone.core_.board().cells[static_cast<std::size_t>(candidate)].correct) {
                safeCell = candidate;
                safeZone.random_.seed(seed);
                break;
            }
        }
        if (safeCell < 0 || !leaveOnly(safeZone, safeCell)) return false;
        safeZone.enemySlotCount_ = 1;
        safeZone.enemySlots_[0].type = 3;
        safeZone.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        safeZone.enemies_.push_back(helperAt(safeZone, safeCell));
        safeZone.safeZoneJobCount_ = 1;
        safeZone.safeZoneJobs_[0].period = 10.0;
        safeZone.safeZoneJobs_[0].timer = 0.0;
        WordGame safeBaseline(GraphicsMode::Vga256, 0x4567u);
        safeBaseline.startGame(1);
        if (!leaveOnly(safeBaseline, safeCell)) return false;
        safeBaseline.random_ = safeZone.random_;
        const int baselineSafeCell =
            (1 + safeBaseline.random_.range(WordGame::BoardRows - 2)) *
                WordGame::BoardColumns +
            1 + safeBaseline.random_.range(WordGame::BoardColumns - 2);
        if (baselineSafeCell != safeCell ||
            safeBaseline.applyEnemyCellEffect(helperAt(safeBaseline, safeCell))) {
            return false;
        }
        const std::size_t safePlays = safeZone.attractOplPlayer_.effectPlayCount();
        safeZone.gameplayTickAccumulator_ = 0.0;
        safeZone.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        if (safeZone.page_ != WordGamePage::Playing || safeZone.core_.level() != 2 ||
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

        WordGame direct(GraphicsMode::Vga256, 0x4d56u);
        WordGame scheduled(GraphicsMode::Vga256, 0x4d56u);
        direct.startGame(1);
        scheduled.startGame(1);
        int movementCell = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!direct.core_.eaten(static_cast<std::size_t>(index)) &&
                direct.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                movementCell = index;
                break;
            }
        }
        if (movementCell < 0 || !leaveOnly(direct, movementCell) ||
            !leaveOnly(scheduled, movementCell)) {
            return false;
        }
        WordGame::Enemy directHelper = helperAt(direct, movementCell);
        WordGame::Enemy scheduledHelper = helperAt(scheduled, movementCell);
        scheduledHelper.moveTimer = 0.0;
        scheduled.enemySlotCount_ = 1;
        scheduled.enemySlots_[0].type = 3;
        scheduled.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        scheduled.enemies_.push_back(scheduledHelper);
        if (direct.applyEnemyCellEffect(directHelper)) return false;
        scheduled.gameplayTickAccumulator_ = 0.0;
        scheduled.update(1.0 / WordSchedulerTicksPerSecond + 1e-12);
        return direct.page_ == WordGamePage::Playing &&
               scheduled.page_ == WordGamePage::Playing &&
               direct.core_.level() == 2 && scheduled.core_.level() == 2 &&
               direct.random_.calls == scheduled.random_.calls &&
               direct.random_.state == scheduled.random_.state &&
               direct.lastGameplaySound_ == 15 && scheduled.lastGameplaySound_ == 15 &&
               direct.attractOplPlayer_.effectPlayCount() == 1 &&
               scheduled.attractOplPlayer_.effectPlayCount() == 1 &&
               scheduled.enemies_.empty() &&
               std::abs(scheduled.gameplayTickAccumulator_) <= 1e-12;
    }

    static std::uint64_t feedbackStripHash(const Renderer& renderer) {
        std::uint64_t hash = 1469598103934665603ull;
        for (int y = 86; y < 117; ++y) {
            for (int x = 20; x < 309; ++x) {
                hash ^= renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)];
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    static std::uint64_t fullFrameHash(const Renderer& renderer) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t pixel : renderer.pixels()) {
            hash ^= pixel;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static std::uint64_t frameRegionHash(const Renderer& renderer,
                                         const int left,
                                         const int top,
                                         const int right,
                                         const int bottom) {
        std::uint64_t hash = 1469598103934665603ull;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                hash ^= renderer.pixels()[static_cast<std::size_t>(
                    y * Renderer::Width + x)];
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    static void maybeDumpWordOptionsFrame(const Renderer& renderer,
                                          const std::string_view filename) {
        const char* directory = std::getenv("MUNCHERS_DUMP_WORD_OPTIONS_DIR");
        if (!directory || !*directory) return;
        const std::filesystem::path outputDirectory(directory);
        std::error_code error;
        std::filesystem::create_directories(outputDirectory, error);
        if (error) return;
        std::ofstream output(outputDirectory / filename, std::ios::binary);
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

    static bool usesSharedHallPaletteCarryover() {
        WordGame game(GraphicsMode::Vga256, 0x4a11u);
        game.startGame(1);
        game.attractMode_ = true;
        game.page_ = WordGamePage::Attract;
        game.munching_ = true;
        game.munchTimer_ = WordGame::MunchAnimationDuration;
        game.attractPostFeedbackBoard_ = false;
        game.hallPaletteActive_ = false;

        Renderer renderer;
        game.render(renderer);
        const std::vector<std::uint32_t> initial = renderer.pixels();

        game.hallPaletteActive_ = true;
        game.render(renderer);
        const std::vector<std::uint32_t> afterHall = renderer.pixels();
        // The live Word user chew and first Number Demo board both put
        // 0x414100 in the one occupied index-111 player pixel. Hall state is
        // retained by the controller, but this slot itself stays unchanged.
        if (initial != afterHall) return false;

        // The clean unattended terminal installs the Hall palette before its
        // restored-board hold; the collision route does not expose a player.
        game.hallPaletteActive_ = false;
        game.attractPostFeedbackBoard_ = true;
        game.attractHallWipeVariant_ = WordGame::AttractHallWipeVariant::Clean;
        game.render(renderer);
        if (renderer.pixels() != afterHall) return false;
        game.attractHallWipeVariant_ = WordGame::AttractHallWipeVariant::Collision;
        game.render(renderer);
        if (renderer.pixels() != initial) return false;

        game.hallPaletteActive_ = true;
        game.startAttract();
        if (game.hallPaletteActive_) return false;
        game.beginAttractHall();
        if (!game.hallPaletteActive_) return false;
        game.stopAttract();
        if (game.hallPaletteActive_ || game.page_ != WordGamePage::Title) return false;

        // Post-game Hall -> primitive replay prompt -> ordinary board setup
        // retains the original controller state even though the player pixel
        // is now proven identical on both sides of that state boundary.
        game.page_ = WordGamePage::ReplayQuestion;
        game.menuSelection_ = 0;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Playing || !game.hallPaletteActive_) return false;
        while (game.attractInterstitialTransition_ !=
               WordGame::AttractInterstitialTransition::None) {
            game.attractTransitionTimer_ = 0.0;
            game.update(0.0);
        }
        game.munching_ = true;
        game.munchTimer_ = WordGame::MunchAnimationDuration;
        game.render(renderer);
        const std::vector<std::uint32_t> replayHall = renderer.pixels();
        game.hallPaletteActive_ = false;
        game.render(renderer);
        const std::vector<std::uint32_t> replayInitial = renderer.pixels();
        if (replayInitial != replayHall) return false;
        game.startGame(1);
        return !game.hallPaletteActive_;
    }

    static bool usesRecoveredCgaCartoonPresenter() {
        const auto completeBoard = [](WordGame& candidate) {
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                (void)candidate.core_.clearCellWithoutScore(
                    static_cast<std::size_t>(index));
            }
            return candidate.core_.completeBoardIfEmpty();
        };

        WordGame cartoon(GraphicsMode::Cga4, 0x4213u);
        cartoon.settingsPersistenceEnabled_ = false;
        cartoon.startGame(3);
        if (!completeBoard(cartoon) || !cartoon.core_.cartoonPending()) return false;
        Renderer renderer(GraphicsMode::Cga4);
        cartoon.render(renderer);
        const std::vector<std::uint32_t> source = renderer.pixels();
        cartoon.handleCompletedBoard();
        const int selectedScene = cartoon.pendingCartoonScene_;
        if (cartoon.page_ != WordGamePage::LevelComplete ||
            cartoon.levelCompleteScene_.valid() || cartoon.sceneTicks_ != 0 ||
            selectedScene < 0 ||
            cartoon.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToCartoon ||
            cartoon.attractInterstitialFrame_ != 0 ||
            cartoon.boardPresentationSourcePixels_ != source ||
            cartoon.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 5> hashes{};
        for (int frame = 0; frame < static_cast<int>(hashes.size()); ++frame) {
            cartoon.render(renderer);
            hashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            cartoon.attractTransitionTimer_ = 0.0;
            cartoon.update(0.0);
            if (frame == 0) {
                static constexpr std::array<int, 6> CoveredSamples = {
                    42, 44, 43, 41, 42, 42,
                };
                constexpr double CaptureFrameSeconds = 31250.0 / 2190197.0;
                const double expectedCovered =
                    (CoveredSamples[static_cast<std::size_t>(selectedScene)] - 1) *
                    CaptureFrameSeconds;
                if (std::abs(cartoon.attractTransitionTimer_ - expectedCovered) > 1e-12 ||
                    cartoon.levelCompleteScene_.valid()) return false;
            }
            if (frame == 1 && !cartoon.levelCompleteScene_.valid()) return false;
        }
        static constexpr std::array<std::uint64_t, 5> ExpectedHashes = {
            0x9da441b2f79ca5b0ull, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x0c5c7de5097a6feeull,
            0xf75309bcac42bb83ull,
        };
        if (hashes != ExpectedHashes) {
            std::cerr << "word CGA cartoon presenter hashes:";
            for (const std::uint64_t hash : hashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        cartoon.render(renderer);
        const std::uint64_t clearedSceneHash = fullFrameHash(renderer);
        return cartoon.attractInterstitialTransition_ ==
                   WordGame::AttractInterstitialTransition::None &&
               cartoon.levelCompleteScene_.valid() && cartoon.sceneTicks_ == 0 &&
               cartoon.sceneTickAccumulator_ == 0.0 &&
               clearedSceneHash == 0xf75309bcac42bb83ull &&
               cartoon.acceptsCommonDispatcherInput();
    }

    static bool usesRecoveredUserBoardPresenter() {
        WordGame game(GraphicsMode::Vga256, 0x4211u);
        game.settingsPersistenceEnabled_ = false;
        game.page_ = WordGamePage::Title;
        game.menuSelection_ = 0;

        Renderer renderer;
        game.render(renderer);
        const std::vector<std::uint32_t> source = renderer.pixels();
        game.startGameWithBoardPresentation(1);
        if (game.page_ != WordGamePage::Playing || game.core_.level() != 1 ||
            game.attractMode_ ||
            game.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToBoard ||
            game.attractInterstitialFrame_ != 0 ||
            game.boardPresentationSourcePixels_ != source) return false;

        const int row = game.playerRow_;
        const int column = game.playerColumn_;
        game.keyDown(VK_RIGHT);
        game.character(L'6');
        game.pointerButton(WordGame::BoardLeft + 1, WordGame::BoardTop + 1, false);
        if (game.moving_ || game.munching_ || game.playerRow_ != row ||
            game.playerColumn_ != column || game.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 9> hashes{};
        for (int frame = 0; frame < static_cast<int>(hashes.size()); ++frame) {
            if (game.attractInterstitialTransition_ !=
                    WordGame::AttractInterstitialTransition::UserToBoard ||
                game.attractInterstitialFrame_ != frame) return false;
            game.render(renderer);
            hashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            game.attractTransitionTimer_ = 0.0;
            game.update(0.0);
        }
        if (game.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::None ||
            !game.boardPresentationSourcePixels_.empty() ||
            !game.acceptsCommonDispatcherInput()) return false;

        constexpr std::array<std::uint64_t, 9> ExpectedHashes = {
            0xed78e0058b63147aull, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x8887db592e3c9ca1ull,
            0xb1a8f948642db583ull, 0xb1a8f948642db583ull,
            0xb1a8f948642db583ull, 0xb499d3493ce5716bull,
            0x8374c95b0749e8aeull,
        };
        if (hashes != ExpectedHashes) {
            std::cerr << "word user board presenter hashes:";
            for (const std::uint64_t hash : hashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }

        const auto completeBoard = [](WordGame& candidate) {
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                (void)candidate.core_.clearCellWithoutScore(
                    static_cast<std::size_t>(index));
            }
            return candidate.core_.completeBoardIfEmpty();
        };

        WordGame direct(GraphicsMode::Vga256, 0x4212u);
        direct.startGame(1);
        if (!completeBoard(direct) || direct.core_.cartoonPending()) return false;
        direct.handleCompletedBoard();
        if (direct.core_.level() != 2 || direct.page_ != WordGamePage::Playing ||
            direct.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToBoard) return false;
        while (direct.attractInterstitialTransition_ !=
               WordGame::AttractInterstitialTransition::None) {
            direct.attractTransitionTimer_ = 0.0;
            direct.update(0.0);
        }

        WordGame cartoon(GraphicsMode::Vga256, 0x4213u);
        cartoon.startGame(3);
        if (!completeBoard(cartoon) || !cartoon.core_.cartoonPending()) return false;
        cartoon.render(renderer);
        const std::vector<std::uint32_t> cartoonSource = renderer.pixels();
        const std::size_t loaderStopsBefore =
            cartoon.attractOplPlayer_.allChannelStopCountForTest();
        cartoon.handleCompletedBoard();
        const int selectedCartoonScene = cartoon.pendingCartoonScene_;
        if (cartoon.page_ != WordGamePage::LevelComplete ||
            cartoon.levelCompleteScene_.valid() || cartoon.sceneTicks_ != 0 ||
            cartoon.pendingCartoonScene_ < 0 ||
            cartoon.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToCartoon ||
            cartoon.attractInterstitialFrame_ != 0 ||
            cartoon.boardPresentationSourcePixels_ != cartoonSource ||
            cartoon.sceneTickAccumulator_ != 0.0 ||
            cartoon.attractOplPlayer_.scoreWriteCount() != 0) return false;
        (void)cartoon.attractOplPlayer_.renderTestSamples(1);
        if (cartoon.attractOplPlayer_.allChannelStopCountForTest() !=
                loaderStopsBefore + 1 ||
            cartoon.attractOplPlayer_.effectPlaying()) return false;

        const bool soundBefore = cartoon.soundOn_;
        cartoon.keyDown(VK_LEFT);
        cartoon.character(L'+');
        cartoon.pointerButton(WordGame::BoardLeft + 1,
                              WordGame::BoardTop + 1, false);
        cartoon.toggleSound();
        if (cartoon.soundOn_ != soundBefore || cartoon.core_.level() != 3 ||
            cartoon.sceneTicks_ != 0 || cartoon.acceptsCommonDispatcherInput()) return false;

        std::array<std::uint64_t, 5> cartoonHashes{};
        for (int frame = 0; frame < static_cast<int>(cartoonHashes.size()); ++frame) {
            if (cartoon.attractInterstitialTransition_ !=
                    WordGame::AttractInterstitialTransition::UserToCartoon ||
                cartoon.attractInterstitialFrame_ != frame) return false;
            cartoon.render(renderer);
            cartoonHashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            cartoon.attractTransitionTimer_ = 0.0;
            cartoon.update(0.0);
            if (frame == 0) {
                static constexpr std::array<int, 6> CoveredSamples = {
                    42, 44, 43, 41, 42, 42,
                };
                constexpr double CaptureFrameSeconds = 31250.0 / 2190197.0;
                if (selectedCartoonScene < 0 ||
                    selectedCartoonScene >= static_cast<int>(CoveredSamples.size()) ||
                    std::abs(cartoon.attractTransitionTimer_ -
                             (CoveredSamples[static_cast<std::size_t>(
                                  selectedCartoonScene)] - 1) * CaptureFrameSeconds) > 1e-12 ||
                    cartoon.levelCompleteScene_.valid() ||
                    cartoon.sceneTicks_ != 0 ||
                    cartoon.pendingCartoonScene_ < 0) return false;
                (void)cartoon.attractOplPlayer_.renderTestSamples(745);
                if (cartoon.attractOplPlayer_.effectPlaying()) return false;
            }
            if (frame == 1 &&
                (!cartoon.levelCompleteScene_.valid() ||
                 cartoon.sceneTicks_ != 0 ||
                 cartoon.pendingCartoonScene_ != -1)) return false;
            if (frame == 3 &&
                (std::abs(cartoon.attractTransitionTimer_ - 0.010) > 1e-12 ||
                 cartoon.attractOplPlayer_.scoreWriteCount() != 0)) return false;
        }
        if (cartoon.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::None ||
            !cartoon.boardPresentationSourcePixels_.empty() ||
            !cartoon.acceptsCommonDispatcherInput() || cartoon.sceneTicks_ != 0 ||
            cartoon.sceneTickAccumulator_ != 0.0) return false;
        cartoon.render(renderer);
        const std::uint64_t clearedSceneHash = fullFrameHash(renderer);
        if (clearedSceneHash != 0xf75309bcac42bb83ull) return false;
        cartoon.update(0.099);
        cartoon.render(renderer);
        if (cartoon.sceneTicks_ != 0 ||
            std::abs(cartoon.sceneTickAccumulator_ - 0.99) > 1e-9 ||
            fullFrameHash(renderer) != clearedSceneHash) return false;
        cartoon.update(0.001);
        cartoon.render(renderer);
        if (cartoon.sceneTicks_ != 1 ||
            std::abs(cartoon.sceneTickAccumulator_) > 1e-9 ||
            fullFrameHash(renderer) == clearedSceneHash) return false;
        const BlobView cartoonBank = cartoon.assets_.gameArchive().find(
            "ADLI", static_cast<std::uint32_t>(cartoon.sceneAudioBank_));
        const MeccSound cartoonScore = decodeMeccGSound(
            cartoonBank, 38, MeccSoundProfile::WordMunchers);
        if (!cartoonScore.valid || !cartoon.attractOplPlayer_.playing() ||
            cartoon.attractOplPlayer_.looping() ||
            cartoon.attractOplPlayer_.scoreWriteCount() != cartoonScore.writes.size()) {
            return false;
        }

        constexpr std::array<std::uint64_t, 5> ExpectedCartoonHashes = {
            0xe15bc2ef4e855c6full, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x0c5c7de5097a6feeull,
            0xf75309bcac42bb83ull,
        };
        if (cartoonHashes != ExpectedCartoonHashes) {
            std::cerr << "word user cartoon presenter hashes:";
            for (const std::uint64_t hash : cartoonHashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }

        const auto prepareFinalCartoon = [](WordGame& candidate, int& finalCell) {
            candidate.startGame(3);
            disableBoardJobs(candidate);
            finalCell = -1;
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (!candidate.core_.eaten(static_cast<std::size_t>(index)) &&
                    candidate.core_.board().cells[
                        static_cast<std::size_t>(index)].correct) {
                    if (finalCell < 0) {
                        finalCell = index;
                    } else if (!candidate.core_.clearCellWithoutScore(
                                   static_cast<std::size_t>(index))) {
                        return false;
                    }
                }
            }
            if (finalCell < 0 || candidate.core_.correctRemaining() != 1) return false;
            candidate.playerRow_ = finalCell / WordGame::BoardColumns;
            candidate.playerColumn_ = finalCell % WordGame::BoardColumns;
            candidate.beginMunch();
            candidate.munchTimer_ = 1.0 / WordSchedulerTicksPerSecond;
            candidate.gameplayTickAccumulator_ = 0.0;
            return candidate.munching_;
        };
        WordGame directCartoon(GraphicsMode::Vga256, 0x4d44u);
        WordGame scheduledCartoon(GraphicsMode::Vga256, 0x4d44u);
        int directCell = -1;
        int scheduledCell = -1;
        if (!prepareFinalCartoon(directCartoon, directCell) ||
            !prepareFinalCartoon(scheduledCartoon, scheduledCell) ||
            directCell != scheduledCell) return false;
        constexpr double postCartoonTerminalTicks = 1.25;
        directCartoon.resolveMunch();
        directCartoon.update(postCartoonTerminalTicks /
                             WordSchedulerTicksPerSecond);
        scheduledCartoon.update((1.0 + postCartoonTerminalTicks) /
                                WordSchedulerTicksPerSecond);
        if (directCartoon.page_ != WordGamePage::LevelComplete ||
            scheduledCartoon.page_ != WordGamePage::LevelComplete ||
            directCartoon.core_.level() != 3 ||
            scheduledCartoon.core_.level() != 3 ||
            directCartoon.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToCartoon ||
            scheduledCartoon.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::UserToCartoon ||
            directCartoon.attractInterstitialFrame_ !=
                scheduledCartoon.attractInterstitialFrame_ ||
            std::abs(directCartoon.attractTransitionTimer_ -
                     scheduledCartoon.attractTransitionTimer_) > 1e-12 ||
            directCartoon.sceneTicks_ != 0 || scheduledCartoon.sceneTicks_ != 0 ||
            directCartoon.sceneTickAccumulator_ != 0.0 ||
            scheduledCartoon.sceneTickAccumulator_ != 0.0 ||
            directCartoon.random_.state != scheduledCartoon.random_.state ||
            directCartoon.random_.calls != scheduledCartoon.random_.calls) return false;

        WordGame soundIndependent(GraphicsMode::Vga256, 0x4d45u);
        soundIndependent.soundOn_ = true;
        soundIndependent.musicOn_ = true;
        soundIndependent.speakerEffects_ = false;
        soundIndependent.sceneAudioBank_ = 1;
        soundIndependent.playSceneEvent(1);
        const std::size_t preservedCueCount =
            soundIndependent.attractOplPlayer_.effectPlayCount();
        soundIndependent.soundOn_ = false;
        soundIndependent.playCartoonScore();
        WordGame musicDisabled(GraphicsMode::Vga256, 0x4d46u);
        musicDisabled.musicOn_ = false;
        musicDisabled.speakerEffects_ = false;
        musicDisabled.sceneAudioBank_ = 1;
        musicDisabled.playCartoonScore();
        WordGame speakerSelected(GraphicsMode::Vga256, 0x4d47u);
        speakerSelected.musicOn_ = true;
        speakerSelected.speakerEffects_ = true;
        speakerSelected.sceneAudioBank_ = 1;
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
        cartoon.finishLevelCompleteScene();
        (void)cartoon.attractOplPlayer_.renderTestSamples(745);
        if (cartoon.attractOplPlayer_.allChannelStopCountForTest() !=
            teardownStopsBefore + 1) return false;
        (void)cartoon.attractOplPlayer_.renderTestSamples(1);
        return cartoon.core_.level() == 4 &&
               cartoon.page_ == WordGamePage::Playing &&
               cartoon.attractOplPlayer_.allChannelStopCountForTest() ==
                   teardownStopsBefore + 2 &&
               exactResidentStopApplied(cartoon.attractOplPlayer_, retainedDepth) &&
               cartoon.attractInterstitialTransition_ ==
                   WordGame::AttractInterstitialTransition::UserToBoard;
    }

    static bool hallPresentationMatches(WordGame& game) {
        game.page_ = WordGamePage::Hall;
        game.postGameHall_ = false;
        Renderer renderer(GraphicsMode::Vga256);
        game.render(renderer);
        const std::uint64_t suppliedHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "hall-supplied-native.ppm");
        constexpr std::uint64_t ExpectedSuppliedHash = 0x04616f92e6dfa290ull;
        if (suppliedHash != ExpectedSuppliedHash) {
            std::cerr << "word supplied Hall hash=0x" << std::hex << suppliedHash
                      << std::dec << "\n";
        }

        const std::vector<WordHallScoreEntry> liveAdmissionEntries = {
            {"The Unknown Muncher", 14'660}, {"jennine oct.1", 2'440},
            {"jennine oct.1", 950}, {"The Unknown Muncher", 835},
            {"jennine", 515}, {"The Unknown Muncher", 230},
            {"The Unknown Muncher", 50}, {"CODEX", 50},
        };
        if (!game.hall_.replace(liveAdmissionEntries)) return false;
        game.postGameHall_ = true;
        game.hallHighlightName_ = "CODEX";
        game.hallHighlightScore_ = 50;
        game.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "hall-live-admission-native.ppm");
        const std::uint64_t liveAdmissionHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedLiveAdmissionHash = 0x4c3fda7428cb5224ull;
        if (liveAdmissionHash != ExpectedLiveAdmissionHash) {
            std::cerr << "word live admitted Hall hash=0x" << std::hex
                      << liveAdmissionHash << std::dec << "\n";
            return false;
        }

        const std::vector<WordHallScoreEntry> tenEntries = {
            {"PERFECT MUNCHER", MuncherScore::MaximumScore},
            {"ADA", 99'999}, {"BEA", 88'888}, {"CAL", 77'777},
            {"DEE", 66'666}, {"ELI", 55'555}, {"FOX", 44'444},
            {"GUS", 33'333}, {"HAL", 22'222}, {"IVY", 11'111},
        };
        if (!game.hall_.replace(tenEntries)) return false;
        game.page_ = WordGamePage::Hall;

        game.postGameHall_ = false;
        game.render(renderer);
        const std::uint64_t browseHash = fullFrameHash(renderer);

        game.postGameHall_ = true;
        game.render(renderer);
        const std::uint64_t postGameHash = fullFrameHash(renderer);

        game.hallHighlightName_ = "PERFECT MUNCHER";
        game.hallHighlightScore_ = MuncherScore::MaximumScore;
        game.render(renderer);
        const std::uint64_t highlightedHash = fullFrameHash(renderer);

        if (!game.hall_.replace({})) return false;
        game.hallHighlightName_.clear();
        game.hallHighlightScore_ = 0;
        game.postGameHall_ = false;
        game.render(renderer);
        const std::uint64_t emptyHash = fullFrameHash(renderer);

        constexpr std::array<std::uint64_t, 4> ExpectedHashes = {
            0x00c2d7de350c280dull, 0x00c2d7de350c280dull,
            0xf5a395708aba6a1dull, 0x20cf339fef12ba37ull,
        };
        const std::array hashes = {browseHash, postGameHash, highlightedHash, emptyHash};
        if (hashes != ExpectedHashes) {
            std::cerr << "word VGA Hall browse/post-game/highlighted/empty hashes=" << std::hex
                      << hashes[0] << ',' << hashes[1] << ',' << hashes[2] << ',' << hashes[3]
                      << std::dec << "\n";
        }
        game.page_ = WordGamePage::Hall;
        game.postGameHall_ = false;
        game.pointerButton(0, 0, true);
        if (game.page_ != WordGamePage::Hall) return false;
        game.pointerButton(0, 0, false);
        return suppliedHash == ExpectedSuppliedHash && hashes == ExpectedHashes &&
               game.page_ == WordGamePage::Title;
    }

    static bool informationArchiveAndPagesMatch(WordGame& game) {
        const BlobView data = game.assets_.gameArchive().find("DATA", 1);
        if (!data || data.size != 7750) return false;
        const auto u16 = [&](const std::size_t offset) {
            return static_cast<std::uint16_t>(
                data.data[offset] | (static_cast<std::uint16_t>(data.data[offset + 1]) << 8));
        };
        const auto i16 = [&](const std::size_t offset) {
            return static_cast<std::int16_t>(u16(offset));
        };
        const auto line = [&](const int group, const int page, const int lineIndex) {
            const std::size_t offset = u16(2 + static_cast<std::size_t>(group) * 2) +
                static_cast<std::size_t>(page) * 0x2c0 +
                static_cast<std::size_t>(lineIndex) * 40;
            std::size_t length = 0;
            while (length < 40 && data.data[offset + length] != 0) ++length;
            return std::string_view(reinterpret_cast<const char*>(data.data + offset), length);
        };
        const auto placement = [&](const int group, const int page, const int sprite) {
            const std::size_t offset = u16(2 + static_cast<std::size_t>(group) * 2) +
                static_cast<std::size_t>(page) * 0x2c0 + 0x2a8 +
                static_cast<std::size_t>(sprite) * 4;
            return std::pair{i16(offset), i16(offset + 2)};
        };
        if (u16(0) != 2 || u16(2) != 6 || u16(4) != 4230 ||
            line(0, 0, 2) != "Word Munchers is a game that helps " ||
            line(0, 5, 14) != "           Jim Thompson " ||
            line(1, 0, 11) != "       (Munchicus scripticus)" ||
            line(1, 4, 14) != " down the Alt Key and pressing M." ||
            placement(0, 1, 0) != std::pair<std::int16_t, std::int16_t>{120, 48} ||
            placement(0, 1, 1) != std::pair<std::int16_t, std::int16_t>{120, 212} ||
            placement(1, 0, 0) != std::pair<std::int16_t, std::int16_t>{80, 136} ||
            placement(1, 1, 5) != std::pair<std::int16_t, std::int16_t>{72, 128}) {
            return false;
        }

        // Complete muted raw DOS screenshots now lock the question and all
        // eleven DATA:1 pages. The Troggle-bearing pages retain the title
        // palette installed by the calling menu; the Muncher-only page already
        // followed its separately recovered sprite-ramp remap.
        constexpr std::array<std::uint64_t, 6> ExpectedInformationHashes = {
            0x19132d0ad87ad94full, 0x1fea2ed3cff0adf0ull,
            0xf7fe41a965eb0a00ull, 0x980eafe6efbce961ull,
            0xbd3ec3a29d500521ull, 0x061d6e48d17e1f79ull,
        };
        constexpr std::array<std::uint64_t, 5> ExpectedInstructionHashes = {
            0x0c5ee9e438c025a9ull, 0xfcfe7362eb6ea8beull,
            0xd48c0f88e905a11aull, 0x7ad219a332ba7fccull,
            0x0239185c800a79ddull,
        };
        Renderer renderer;
        game.page_ = WordGamePage::InstructionsQuestion;
        game.menuSelection_ = 1;
        game.render(renderer);
        if (fullFrameHash(renderer) != 0x070b08aa80400c33ull) return false;
        for (int group = 0; group < 2; ++group) {
            game.page_ = WordGamePage::Information;
            game.informationGroup_ = group;
            const int pageCount = group == 0 ? 6 : 5;
            for (int page = 0; page < pageCount; ++page) {
                game.informationPage_ = page;
                game.render(renderer);
                const std::uint64_t hash = fullFrameHash(renderer);
                const std::uint64_t expected = group == 0
                    ? ExpectedInformationHashes[static_cast<std::size_t>(page)]
                    : ExpectedInstructionHashes[static_cast<std::size_t>(page)];
                if (hash != expected) {
                    std::cerr << "word information group=" << group << " page=" << page
                              << " hash=0x" << std::hex << hash << std::dec << "\n";
                    return false;
                }
            }
        }
        game.page_ = WordGamePage::Information;
        game.informationStartsGame_ = false;
        game.informationGroup_ = 0;
        game.informationPage_ = 0;
        game.pointerButton(0, 0, true);
        if (game.page_ != WordGamePage::Information || game.informationPage_ != 0) {
            return false;
        }
        game.pointerButton(0, 0, false);
        return game.page_ == WordGamePage::Information && game.informationPage_ == 1;
    }

    static bool optionsContentAndDifficultyRoute(WordGame& game) {
        advanceStartup(game);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 0 ||
            game.wordDifficulty_ != 0 || game.selectedSoundCount(game.selectedSounds_) != 2 ||
            game.targetWordCount(game.selectedSounds_, game.wordDifficulty_) <= 0) {
            return false;
        }

        Renderer renderer;
        game.render(renderer);
        const std::uint64_t optionsHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "options-native.ppm");
        constexpr std::uint64_t ExpectedOptionsHash = 0xcaf1504d701741dcull;
        if (optionsHash != ExpectedOptionsHash) {
            std::cerr << "word options frame hash=0x" << std::hex << optionsHash
                      << std::dec << "\n";
        }

        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 0) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 0 ||
            game.contentDraftDifficulty_ != 0) return false;
        game.render(renderer);
        const std::uint64_t contentHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "set-content-native.ppm");
        constexpr std::uint64_t ExpectedContentHash = 0x2c1f159c04211bbfull;
        if (contentHash != ExpectedContentHash) {
            std::cerr << "word set-content frame hash=0x" << std::hex << contentHash
                      << std::dec << "\n";
        }

        // With no child editor acceptance, image 0x1382D bypasses both the
        // target-count check and reconfiguration and returns to Options.
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 0 ||
            game.contentDraftChanged_ || game.contentInsufficientMessage_) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 0 ||
            game.contentDraftChanged_ || game.contentInsufficientMessage_) return false;

        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsDifficulty || game.menuSelection_ != 0) {
            return false;
        }
        game.render(renderer);
        const std::uint64_t difficultyHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "difficulty-native.ppm");
        constexpr std::uint64_t ExpectedDifficultyHash = 0x9fceccd8c9179a03ull;
        if (difficultyHash != ExpectedDifficultyHash) {
            std::cerr << "word difficulty frame hash=0x" << std::hex << difficultyHash
                      << std::dec << "\n";
        }

        game.keyDown(VK_DOWN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsContent ||
            game.contentDraftDifficulty_ != 3 || game.wordDifficulty_ != 0 ||
            !game.contentDraftChanged_) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.wordDifficulty_ != 3 ||
            !game.core_.configured() || game.contentDraftChanged_) return false;

        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::OptionsContent ||
            game.contentDraftDifficulty_ != 3) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.wordDifficulty_ != 3) return false;

        game.menuSelection_ = 3;
        const bool originalJoystick = game.joystickEnabled_;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options ||
            game.joystickEnabled_ == originalJoystick) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Title || game.menuSelection_ != 0) return false;

        WordGame warning(GraphicsMode::Vga256, 0x5678);
        warning.settingsPersistenceEnabled_ = false;
        warning.page_ = WordGamePage::OptionsContent;
        warning.menuSelection_ = 2;
        warning.contentDraftDifficulty_ = 7;
        warning.contentDraftSounds_.fill(false);
        warning.contentDraftChanged_ = true;
        warning.contentInsufficientMessage_ = false;
        warning.keyDown(VK_ESCAPE);
        if (warning.page_ != WordGamePage::OptionsContent ||
            !warning.contentDraftChanged_ || !warning.contentInsufficientMessage_ ||
            warning.menuSelection_ != 2) return false;

        warning.render(renderer);
        const std::uint64_t warningHash = fullFrameHash(renderer);
        // Static/native composition baseline for the executable-proven
        // pDisplayText(0xC3) layout; no live DOS frame is claimed here.
        constexpr std::uint64_t ExpectedWarningHash = 0x1b663d673bf6f73dull;
        if (warningHash != ExpectedWarningHash) {
            std::cerr << "word insufficient-target warning hash=0x" << std::hex
                      << warningHash << std::dec << "\n";
            return false;
        }

        // A muted original run starts with two Group-2 sounds available at
        // difficulty 1, accepts difficulty 0, and then reaches the zero-target
        // warning. The original has already discarded those now-unavailable
        // selections by the time it paints the underlying Set Content page.
        WordGame liveWarning(GraphicsMode::Vga256, 0x5679);
        liveWarning.settingsPersistenceEnabled_ = false;
        liveWarning.page_ = WordGamePage::OptionsContent;
        liveWarning.menuSelection_ = 0;
        liveWarning.contentDraftDifficulty_ = 0;
        liveWarning.contentDraftSounds_.fill(false);
        liveWarning.contentDraftChanged_ = true;
        liveWarning.contentInsufficientMessage_ = true;
        liveWarning.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "insufficient-targets-live-native.ppm");
        constexpr std::uint64_t ExpectedLiveWarningHash = 0x607c6b32d1a263c9ull;
        if (fullFrameHash(renderer) != ExpectedLiveWarningHash) {
            std::cerr << "word live insufficient-target warning hash=0x" << std::hex
                      << fullFrameHash(renderer) << std::dec << "\n";
            return false;
        }

        // DS:14D4 accepts only Escape, Enter, Space. Right release remains
        // 0xFD and is inert; left release is rewritten to Space.
        warning.keyDown(VK_RIGHT);
        warning.keyDown('A');
        warning.character(L'a');
        warning.pointerButton(0, 0, true);
        if (!warning.contentInsufficientMessage_ ||
            warning.page_ != WordGamePage::OptionsContent) return false;
        warning.pointerButton(0, 0, false);
        if (warning.contentInsufficientMessage_ ||
            warning.page_ != WordGamePage::OptionsContent ||
            warning.menuSelection_ != 2) return false;
        warning.keyDown(VK_ESCAPE);
        if (!warning.contentInsufficientMessage_) return false;
        warning.keyDown(VK_RETURN);
        if (warning.contentInsufficientMessage_) return false;
        warning.keyDown(VK_ESCAPE);
        if (!warning.contentInsufficientMessage_) return false;
        warning.character(L' ');
        if (warning.contentInsufficientMessage_) return false;

        warning.contentDraftDifficulty_ = 0;
        warning.contentDraftSounds_.fill(false);
        warning.contentDraftSounds_[0] = true;
        warning.contentDraftSounds_[1] = true;
        warning.keyDown(VK_ESCAPE);
        if (warning.page_ != WordGamePage::Options || warning.menuSelection_ != 0 ||
            warning.contentDraftChanged_ || warning.contentInsufficientMessage_ ||
            warning.wordDifficulty_ != 0 ||
            warning.selectedSounds_ != warning.contentDraftSounds_) return false;

        // The vowel editor returns AL=1 on a valid Enter even when its final
        // bit pattern matches the entry snapshot, so the parent changed byte
        // must still become set.
        WordGame unchangedVowels(GraphicsMode::Vga256, 0x6789);
        unchangedVowels.settingsPersistenceEnabled_ = false;
        unchangedVowels.page_ = WordGamePage::OptionsVowels;
        unchangedVowels.contentDraftDifficulty_ = 0;
        unchangedVowels.contentDraftSounds_.fill(false);
        unchangedVowels.contentDraftSounds_[0] = true;
        unchangedVowels.contentDraftSounds_[1] = true;
        unchangedVowels.vowelEditorOriginalSounds_ = unchangedVowels.contentDraftSounds_;
        unchangedVowels.contentDraftChanged_ = false;
        unchangedVowels.vowelMessage_ = 0;
        unchangedVowels.keyDown(VK_RETURN);
        if (unchangedVowels.page_ != WordGamePage::OptionsContent ||
            !unchangedVowels.contentDraftChanged_ || unchangedVowels.vowelMessage_ != 0) {
            return false;
        }

        return optionsHash == ExpectedOptionsHash && contentHash == ExpectedContentHash &&
               difficultyHash == ExpectedDifficultyHash &&
               warningHash == ExpectedWarningHash;
    }

    static bool fixedMenuPointerRoutes() {
        WordGame options(GraphicsMode::Vga256, 0x1234);
        advanceStartup(options);
        options.page_ = WordGamePage::Options;
        options.menuSelection_ = 0;

        // DS:1EA6 is (baseline 80,left 50). The source pointer table contains
        // five visible strings; its sixth/result-6 entry is an empty Escape
        // sentinel, not a fabricated Main Menu row.
        options.pointerButton(49, 90, false);
        if (options.page_ != WordGamePage::Options || options.menuSelection_ != 0) return false;
        options.pointerButton(50, 89, false);
        if (options.menuSelection_ != 0) return false;
        options.pointerButton(235, 90, false);
        if (options.menuSelection_ != 0) return false;
        options.pointerButton(234, 90, false);
        if (options.menuSelection_ != 1) return false;
        options.pointerButton(50, 90, false);
        if (options.page_ != WordGamePage::OptionsEraseHall) return false;

        WordGame content(GraphicsMode::Vga256, 0x1234);
        advanceStartup(content);
        content.page_ = WordGamePage::Options;
        content.menuSelection_ = 0;
        content.pointerButton(50, 80, false);
        if (content.page_ != WordGamePage::OptionsContent || content.menuSelection_ != 0) {
            return false;
        }
        // Runtime layout DS:2938 is (baseline 96,left 50).
        content.pointerButton(195, 117, false);
        if (content.page_ != WordGamePage::OptionsContent || content.menuSelection_ != 0) {
            return false;
        }
        content.pointerButton(194, 117, false);
        if (content.page_ != WordGamePage::OptionsContent || content.menuSelection_ != 2) {
            return false;
        }
        content.pointerButton(50, 117, false);
        if (content.page_ != WordGamePage::OptionsPreview) return false;
        content.keyDown(VK_ESCAPE);
        content.pointerButton(50, 94, false);
        if (content.menuSelection_ != 2) return false;
        content.pointerButton(50, 96, false);
        if (content.menuSelection_ != 0) return false;
        content.pointerButton(50, 96, false);
        if (content.page_ != WordGamePage::OptionsDifficulty || content.menuSelection_ != 0) {
            return false;
        }

        // The difficulty widget rewrites its copied layout to baseline 64,
        // left 50. A different release selects; a repeated release accepts.
        content.pointerButton(50, 96, false);
        if (content.page_ != WordGamePage::OptionsDifficulty || content.menuSelection_ != 3) {
            return false;
        }
        content.pointerButton(50, 96, false);
        if (content.page_ != WordGamePage::OptionsContent ||
            content.contentDraftDifficulty_ != 3) return false;

        WordGame wrapping(GraphicsMode::Vga256, 0x1234);
        advanceStartup(wrapping);
        wrapping.page_ = WordGamePage::Options;
        wrapping.menuSelection_ = 0;
        wrapping.keyDown(VK_UP);
        if (wrapping.menuSelection_ != 4) return false;
        wrapping.keyDown(VK_DOWN);
        if (wrapping.menuSelection_ != 0) return false;
        wrapping.pointerButton(50, 135, false);
        if (wrapping.page_ != WordGamePage::Options || wrapping.menuSelection_ != 0) {
            return false;
        }
        return true;
    }

    static bool fixedMenuNumberedShortcuts() {
        WordGame game(GraphicsMode::Vga256, 0x1234);
        game.settingsPersistenceEnabled_ = false;
        advanceStartup(game);

        // The shared list widget maps Left/Up to previous, Right/Down to
        // next, and Home/End to first/last without activating the row.
        game.page_ = WordGamePage::Title;
        game.menuSelection_ = 2;
        game.keyDown(VK_LEFT);
        game.keyDown(VK_RIGHT);
        if (game.page_ != WordGamePage::Title || game.menuSelection_ != 2) return false;
        game.keyDown(VK_HOME);
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::Title || game.menuSelection_ != 4) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_END);
        if (game.menuSelection_ != 4) return false;

        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 0;
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 4) return false;
        game.keyDown(VK_HOME);
        game.keyDown(VK_END);
        if (game.menuSelection_ != 4) return false;

        game.page_ = WordGamePage::OptionsContent;
        game.menuSelection_ = 1;
        game.keyDown(VK_HOME);
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 2) return false;
        game.keyDown(VK_RIGHT);
        if (game.menuSelection_ != 0) return false;

        game.page_ = WordGamePage::OptionsDifficulty;
        game.menuSelection_ = 0;
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::OptionsDifficulty || game.menuSelection_ != 7) {
            return false;
        }
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_END);
        if (game.menuSelection_ != 7) return false;

        for (const WordGamePage promptPage : {
                 WordGamePage::InstructionsQuestion, WordGamePage::ReplayQuestion,
                 WordGamePage::QuitConfirm}) {
            game.page_ = promptPage;
            game.menuSelection_ = 1;
            game.keyDown(VK_UP);
            game.keyDown(VK_DOWN);
            if (game.page_ != promptPage || game.menuSelection_ != 1) return false;
            game.keyDown(VK_HOME);
            if (game.page_ != promptPage || game.menuSelection_ != 0) return false;
            game.keyDown(VK_END);
            if (game.page_ != promptPage || game.menuSelection_ != 1) return false;
            game.keyDown(VK_LEFT);
            game.keyDown(VK_RIGHT);
            if (game.page_ != promptPage || game.menuSelection_ != 1) return false;
            game.character(L'4');
            if (game.page_ != promptPage || game.menuSelection_ != 0) return false;
            game.character(L'8');
            if (game.menuSelection_ != 1) return false;
            game.character(L'2');
            if (game.menuSelection_ != 0) return false;
            game.character(L'6');
            if (game.page_ != promptPage || game.menuSelection_ != 1) return false;
            game.character(L'$');
            if (game.menuSelection_ != 1) return false;
        }

        game.page_ = WordGamePage::Title;
        game.menuSelection_ = 4;
        game.character(L'2');
        if (game.page_ != WordGamePage::Title || game.menuSelection_ != 1) return false;
        game.character(L'!');
        game.character(L'0');
        if (game.page_ != WordGamePage::Title || game.menuSelection_ != 1) return false;

        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 4;
        game.character(L'3');
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 2) return false;

        game.page_ = WordGamePage::OptionsContent;
        game.menuSelection_ = 2;
        game.character(L'1');
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 0) {
            return false;
        }

        game.page_ = WordGamePage::OptionsDifficulty;
        game.menuSelection_ = 0;
        game.character(L'8');
        if (game.page_ != WordGamePage::OptionsDifficulty || game.menuSelection_ != 7) {
            return false;
        }

        game.eraseDraft_.clear();
        for (int index = 0; index < 10; ++index) {
            game.eraseDraft_.push_back({"entry" + std::to_string(index),
                                        static_cast<std::uint32_t>(100 - index)});
        }
        game.page_ = WordGamePage::OptionsEraseHall;
        game.eraseSelection_ = 5;
        game.keyDown(VK_HOME);
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::OptionsEraseHall || game.eraseSelection_ != 9 ||
            game.eraseDraft_.size() != 10) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_END);
        if (game.eraseSelection_ != 9 || game.eraseDraft_.size() != 10) return false;
        game.eraseSelection_ = 5;
        game.character(L'1');
        game.character(L'0');
        return game.page_ == WordGamePage::OptionsEraseHall &&
               game.eraseSelection_ == 9 && game.eraseDraft_.size() == 10;
    }

    static bool customWidgetPointerRoutes() {
        WordGame vowels(GraphicsMode::Vga256, 0x1234);
        vowels.settingsPersistenceEnabled_ = false;
        vowels.page_ = WordGamePage::OptionsVowels;
        vowels.contentDraftDifficulty_ = 0;
        vowels.contentDraftSounds_.fill(false);
        vowels.vowelSelection_ = 5;
        vowels.vowelMessage_ = 0;

        // DS:1D22 uses inclusive 12x12 checkbox rectangles. Motion, labels,
        // and a one-pixel miss are inert; one left release selects and toggles.
        vowels.pointerMove(26, 69);
        if (vowels.vowelSelection_ != 5) return false;
        vowels.pointerButton(25, 69, false);
        if (vowels.vowelSelection_ != 5 || vowels.contentDraftSounds_[0] ||
            vowels.contentDraftSounds_[1]) return false;
        vowels.pointerButton(26, 69, false);
        if (vowels.vowelSelection_ != 0 || !vowels.contentDraftSounds_[0] ||
            !vowels.contentDraftSounds_[1]) return false;
        vowels.pointerButton(37, 80, false);
        if (vowels.contentDraftSounds_[0] || vowels.contentDraftSounds_[1]) return false;
        vowels.pointerButton(45, 73, false);
        if (vowels.contentDraftSounds_[0] || vowels.contentDraftSounds_[1]) return false;

        // Unavailable choices remain selectable in the original raw state,
        // and the custom loop has no right-release/0xFD acceptance branch.
        if (vowels.vowelChoiceState(6) != 2) return false;
        vowels.pointerButton(106, 69, false);
        if (vowels.vowelSelection_ != 6 || vowels.vowelChoiceState(6) != 3) return false;
        vowels.pointerButton(0, 0, true);
        if (vowels.page_ != WordGamePage::OptionsVowels) return false;

        // Validation messages use the shared accepted-key waiter too: an
        // unsupported key pair and right release are inert, while left
        // release is rewritten to Space.
        vowels.vowelMessage_ = 1;
        vowels.keyDown('A');
        vowels.character(L'a');
        vowels.pointerButton(0, 0, true);
        if (vowels.vowelMessage_ != 1) return false;
        vowels.pointerButton(0, 0, false);
        if (vowels.vowelMessage_ != 0) return false;

        vowels.page_ = WordGamePage::OptionsVowelsHelp;
        vowels.pointerButton(0, 0, true);
        if (vowels.page_ != WordGamePage::OptionsVowelsHelp) return false;
        vowels.pointerButton(0, 0, false);
        if (vowels.page_ != WordGamePage::OptionsVowels) return false;

        WordGame eraser(GraphicsMode::Vga256, 0x1234);
        eraser.settingsPersistenceEnabled_ = false;
        eraser.page_ = WordGamePage::OptionsEraseHall;
        eraser.eraseSelection_ = 0;
        eraser.eraseDraft_ = {{"first", 100}, {"second", 50}};
        eraser.pointerMove(40, 48);
        if (eraser.eraseSelection_ != 0) return false;
        eraser.pointerButton(39, 48, false);
        if (eraser.eraseSelection_ != 0) return false;
        eraser.pointerButton(40, 48, false);
        if (eraser.eraseSelection_ != 1 || eraser.eraseDraft_.size() != 2) return false;
        eraser.pointerButton(40, 48, false);
        if (eraser.page_ != WordGamePage::OptionsEraseHall ||
            eraser.eraseSelection_ != 0 || eraser.eraseDraft_.size() != 1 ||
            eraser.eraseDraft_[0].name != "first") return false;
        eraser.pointerButton(0, 0, true);
        return eraser.page_ == WordGamePage::Options && eraser.menuSelection_ == 1 &&
               eraser.hall_.entries().size() == 1 &&
               eraser.hall_.entries()[0].name == "first";
    }

    static bool vowelEditorRoutes(WordGame& game) {
        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 0;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsVowels || game.vowelSelection_ != 0 ||
            !game.contentDraftSounds_[2] || !game.contentDraftSounds_[3]) return false;
        // Keep the raw-state composition independent of the prior Options
        // persistence route: difficulty 0 makes Group 1 available and the
        // first Group 2 choice unavailable in the executable's +6 table.
        game.contentDraftDifficulty_ = 0;
        if (game.vowelChoiceState(0) != 0 || game.vowelChoiceState(1) != 1 ||
            game.vowelChoiceState(6) != 2) {
            std::cerr << "word vowel raw states=" << static_cast<int>(game.vowelChoiceState(0))
                      << ',' << static_cast<int>(game.vowelChoiceState(1)) << ','
                      << static_cast<int>(game.vowelChoiceState(6)) << "\n";
            return false;
        }
        const auto liveEditorSounds = game.contentDraftSounds_;
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[12] = true;
        if (game.vowelChoiceState(6) != 3) {
            std::cerr << "word vowel selected-unavailable state="
                      << static_cast<int>(game.vowelChoiceState(6)) << "\n";
            return false;
        }
        if (game.vowelValidationMessage() != 1) {
            std::cerr << "word selected-unavailable vowel counted as available\n";
            return false;
        }
        game.contentDraftDifficulty_ = 1;
        if (game.vowelChoiceState(6) != 1 || game.vowelValidationMessage() != 2) {
            std::cerr << "word available Group-2 singleton validation regressed\n";
            return false;
        }
        game.contentDraftSounds_[12] = false;
        game.contentDraftSounds_[15] = true;
        if (game.vowelChoiceState(9) != 1 || game.vowelValidationMessage() != 3) {
            std::cerr << "word available Group-3 singleton validation regressed\n";
            return false;
        }
        game.contentDraftSounds_[15] = false;
        game.contentDraftSounds_ = liveEditorSounds;
        game.contentDraftDifficulty_ = 0;

        // The custom editor uses a one-based selection internally.  Lock the
        // complete graph recovered at 0x1452C (right), 0x14579 (left), the
        // adjacent per-column Up/Down branches, and generic Home/End targets
        // 0x0F36D/0x0F378. In particular, Group 3's Left route caps
        // current-3 at 9; using max instead of min strands its lower rows in
        // the wrong column.
        static constexpr std::array<int, 14> Up = {
            5, 0, 1, 2, 3, 4, 8, 6, 7, 13, 9, 10, 11, 12,
        };
        static constexpr std::array<int, 14> Down = {
            1, 2, 3, 4, 5, 0, 7, 8, 6, 10, 11, 12, 13, 9,
        };
        static constexpr std::array<int, 14> Left = {
            9, 10, 11, 12, 13, 13, 0, 1, 2, 6, 7, 8, 8, 8,
        };
        static constexpr std::array<int, 14> Right = {
            6, 7, 8, 8, 8, 8, 9, 10, 11, 0, 1, 2, 3, 4,
        };
        static constexpr std::array<int, 14> Home = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        };
        static constexpr std::array<int, 14> End = {
            13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
        };
        const auto graphMatches = [&](const UINT key,
                                      const std::array<int, 14>& expected,
                                      const char* label) {
            const auto sounds = game.contentDraftSounds_;
            for (int source = 0; source < 14; ++source) {
                game.vowelSelection_ = source;
                game.keyDown(key);
                if (game.page_ != WordGamePage::OptionsVowels ||
                    game.vowelSelection_ != expected[static_cast<std::size_t>(source)] ||
                    game.contentDraftSounds_ != sounds) {
                    std::cerr << "word vowel " << label << " graph source=" << source
                              << " actual=" << game.vowelSelection_ << " expected="
                              << expected[static_cast<std::size_t>(source)] << "\n";
                    return false;
                }
            }
            return true;
        };
        if (!graphMatches(VK_UP, Up, "up") ||
            !graphMatches(VK_DOWN, Down, "down") ||
            !graphMatches(VK_LEFT, Left, "left") ||
            !graphMatches(VK_RIGHT, Right, "right") ||
            !graphMatches(VK_HOME, Home, "home") ||
            !graphMatches(VK_END, End, "end")) {
            return false;
        }
        const auto digitGraphMatches = [&](const UINT virtualKey,
                                           const wchar_t characterValue,
                                           const std::array<int, 14>& expected,
                                           const char* label) {
            const auto sounds = game.contentDraftSounds_;
            for (int source = 0; source < 14; ++source) {
                game.vowelSelection_ = source;
                game.keyDown(virtualKey);
                game.character(characterValue);
                if (game.page_ != WordGamePage::OptionsVowels ||
                    game.vowelSelection_ != expected[static_cast<std::size_t>(source)] ||
                    game.contentDraftSounds_ != sounds) {
                    std::cerr << "word vowel " << label << " digit graph source="
                              << source << " actual=" << game.vowelSelection_
                              << " expected="
                              << expected[static_cast<std::size_t>(source)] << "\n";
                    return false;
                }
            }
            return true;
        };
        if (!digitGraphMatches('2', L'2', Down, "down") ||
            !digitGraphMatches('4', L'4', Left, "left") ||
            !digitGraphMatches('6', L'6', Right, "right") ||
            !digitGraphMatches('8', L'8', Up, "up")) {
            return false;
        }
        game.vowelSelection_ = 0;

        Renderer renderer;
        game.render(renderer);
        const std::uint64_t editorHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "vowels-native.ppm");
        constexpr std::uint64_t ExpectedEditorHash = 0xa57416c9f7e33637ull;
        if (editorHash != ExpectedEditorHash) {
            std::cerr << "word vowel editor frame hash=0x" << std::hex << editorHash
                      << std::dec << "\n";
        }
        game.keyDown(VK_F1);
        if (game.page_ != WordGamePage::OptionsVowelsHelp) return false;
        game.render(renderer);
        const std::uint64_t helpHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "vowels-help-native.ppm");
        constexpr std::uint64_t ExpectedHelpHash = 0x33598e143043cb06ull;
        if (helpHash != ExpectedHelpHash) {
            std::cerr << "word vowel help frame hash=0x" << std::hex << helpHash
                      << std::dec << "\n";
        }
        // The blocking waiter accepts exactly Escape, Enter, and Space. An
        // unsupported physical key includes both its key-down and translated
        // character, and neither half may fall through to global shortcuts.
        game.keyDown('A');
        game.character(L'a');
        game.keyDown(VK_RIGHT);
        game.joystickEnabled_ = true;
        game.joystickRepeatTicks_ = 4;
        game.keyDown(VK_OEM_PLUS);
        game.character(L'+');
        if (game.page_ != WordGamePage::OptionsVowelsHelp ||
            game.joystickRepeatTicks_ != 4) return false;
        const int helpSelection = game.vowelSelection_;
        game.keyDown(VK_SPACE);
        game.character(L' ');
        if (game.page_ != WordGamePage::OptionsVowels ||
            game.vowelSelection_ != helpSelection) return false;

        game.keyDown(VK_F3);
        if (game.selectedSoundCount(game.contentDraftSounds_) != 0) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsVowels || game.vowelMessage_ != 1) return false;
        game.render(renderer);
        const std::uint64_t validationHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedValidationHash = 0x1853b48ff3866660ull;
        if (validationHash != ExpectedValidationHash) {
            std::cerr << "word vowel validation frame hash=0x" << std::hex
                      << validationHash << std::dec << "\n";
        }

        const auto capturedValidationHash = [&](const int difficulty,
                                                const int selectedSound,
                                                const int message,
                                                const std::uint64_t expected,
                                                const char* dumpName) {
            WordGame captured(GraphicsMode::Vga256, 0x6790);
            captured.settingsPersistenceEnabled_ = false;
            captured.page_ = WordGamePage::OptionsVowels;
            captured.contentDraftDifficulty_ = difficulty;
            captured.contentDraftSounds_.fill(false);
            if (selectedSound >= 0) {
                captured.contentDraftSounds_[static_cast<std::size_t>(selectedSound)] = true;
            }
            captured.vowelSelection_ = 0;
            captured.vowelMessage_ = message;
            captured.render(renderer);
            maybeDumpWordOptionsFrame(renderer, dumpName);
            const std::uint64_t actual = fullFrameHash(renderer);
            if (actual != expected) {
                std::cerr << "word captured vowel validation message=" << message
                          << " hash=0x" << std::hex << actual << ", expected=0x"
                          << expected << std::dec << "\n";
                return false;
            }
            return true;
        };
        if (!capturedValidationHash(0, -1, 1, 0x1853b48ff3866660ull,
                                    "vowels-none-live-native.ppm") ||
            !capturedValidationHash(1, 12, 2, 0x00476e702721659cull,
                                    "vowels-group2-live-native.ppm") ||
            !capturedValidationHash(1, 15, 3, 0x8d3fc297b93bd8acull,
                                    "vowels-group3-live-native.ppm")) {
            return false;
        }
        game.keyDown('A');
        game.character(L'a');
        game.pointerButton(0, 0, true);
        if (game.vowelMessage_ != 1) return false;
        game.pointerButton(0, 0, false);
        if (game.vowelMessage_ != 0) return false;
        game.keyDown(VK_F2);
        if (game.selectedSoundCount(game.contentDraftSounds_) != 20) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 1) {
            std::cerr << "word vowel accept route page=" << static_cast<int>(game.page_)
                      << " selection=" << game.menuSelection_ << "\n";
            return false;
        }

        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options ||
            game.selectedSoundCount(game.selectedSounds_) != 20) {
            std::cerr << "word vowel commit route page=" << static_cast<int>(game.page_)
                      << " selected=" << game.selectedSoundCount(game.selectedSounds_) << "\n";
            return false;
        }

        game.keyDown(VK_RETURN);
        game.keyDown(VK_DOWN);
        game.keyDown(VK_RETURN);
        const auto beforeCancel = game.contentDraftSounds_;
        game.keyDown(VK_SPACE);
        if (game.contentDraftSounds_ == beforeCancel) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::OptionsContent ||
            game.contentDraftSounds_ != beforeCancel || game.menuSelection_ != 1) {
            std::cerr << "word vowel cancel route page=" << static_cast<int>(game.page_)
                      << " selection=" << game.menuSelection_ << "\n";
            return false;
        }

        return editorHash == ExpectedEditorHash && helpHash == ExpectedHelpHash &&
               validationHash == ExpectedValidationHash;
    }

    static bool previewEraseAndPasswordRoutes(WordGame& game) {
        Renderer renderer;

        std::array<bool, 20> availabilityProbe{};
        availabilityProbe[0] = true;
        if (game.targetWordCount(availabilityProbe, 4) != 114) return false;
        availabilityProbe.fill(false);
        availabilityProbe[12] = true;
        if (game.targetWordCount(availabilityProbe, 0) != 0 ||
            game.targetWordCount(availabilityProbe, 1) != 13) return false;

        // Exercise the recovered ten-row/six-column grid through Set Content.
        // Record 0 is reserved for the centered exemplar; the grid starts at 1.
        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 0;
        game.keyDown(VK_RETURN);
        game.contentDraftDifficulty_ = 0;
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[0] = true;
        game.menuSelection_ = 2;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPreview || game.previewSound_ != 0 ||
            game.previewWordOffset_ != 0) return false;
        game.render(renderer);
        const std::uint64_t previewHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "preview-sound0-native.ppm");
        constexpr std::uint64_t ExpectedPreviewHash = 0xd25d03ab58d3a5a9ull;
        if (previewHash != ExpectedPreviewHash) {
            std::cerr << "word preview frame hash=0x" << std::hex << previewHash
                      << std::dec << "\n";
        }

        game.contentDraftDifficulty_ = 0;
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[2] = true;
        game.contentDraftSounds_[3] = true;
        game.page_ = WordGamePage::OptionsPreview;
        game.previewSound_ = 2;
        game.previewWordOffset_ = 0;
        game.render(renderer);
        const std::uint64_t livePreviewStateHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "preview-live-state-native.ppm");
        constexpr std::uint64_t ExpectedLivePreviewStateHash = 0xb6b873dcd7ab0ec7ull;
        if (livePreviewStateHash != ExpectedLivePreviewStateHash) {
            std::cerr << "word live-state preview frame hash=0x" << std::hex
                      << livePreviewStateHash << std::dec << "\n";
        }
        game.contentDraftDifficulty_ = 1;
        game.page_ = WordGamePage::OptionsPreview;
        game.previewSound_ = 1;
        game.previewWordOffset_ = 0;
        game.render(renderer);
        const std::uint64_t densePreviewHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "preview-dense-live-state-native.ppm");
        constexpr std::uint64_t ExpectedDensePreviewHash = 0x0c2b7fb6cac342baull;
        if (densePreviewHash != ExpectedDensePreviewHash) {
            std::cerr << "word dense live-state preview frame hash=0x" << std::hex
                      << densePreviewHash << std::dec << "\n";
        }
        game.page_ = WordGamePage::OptionsPreview;
        game.previewSound_ = 0;
        game.previewWordOffset_ = 0;
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[0] = true;
        game.pointerButton(0, 0, true);
        if (game.page_ != WordGamePage::OptionsPreview || game.previewWordOffset_ != 0) {
            return false;
        }
        game.pointerButton(0, 0, false);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 2) {
            return false;
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPreview) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 2) {
            return false;
        }

        // DS:26F6, rather than the physical WLIST record count, controls both
        // availability and Preview pagination. Sound 12 is unavailable at
        // difficulty 0; the live compact-font grid has six ten-row columns,
        // so sound 0 exposes 114 words at difficulty 4 in offsets 0 and 60.
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[12] = true;
        game.contentDraftDifficulty_ = 0;
        game.page_ = WordGamePage::OptionsContent;
        game.beginWordPreview();
        if (game.page_ != WordGamePage::OptionsContent || game.previewSound_ != -1 ||
            !game.contentInsufficientMessage_) return false;
        game.keyDown(VK_SPACE);
        if (game.contentInsufficientMessage_ ||
            game.page_ != WordGamePage::OptionsContent) return false;
        game.contentDraftSounds_.fill(false);
        game.contentDraftSounds_[0] = true;
        game.contentDraftDifficulty_ = 4;
        game.beginWordPreview();
        if (game.page_ != WordGamePage::OptionsPreview || game.previewWordOffset_ != 0) return false;
        game.advanceWordPreview();
        if (game.previewWordOffset_ != 60) return false;
        game.advanceWordPreview();
        if (game.page_ != WordGamePage::OptionsContent || game.menuSelection_ != 2) return false;

        // Hall deletion is transactional: Escape discards the draft, while
        // Enter commits it. Reaching an empty draft has its distinct message.
        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 1;
        const std::vector<WordHallScoreEntry> originalHall = game.hall_.entries();
        if (originalHall.size() < 2) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsEraseHall ||
            game.eraseDraft_ != originalHall || game.eraseSelection_ != 0) return false;
        game.render(renderer);
        const std::uint64_t eraseHash = fullFrameHash(renderer);
        maybeDumpWordOptionsFrame(renderer, "hall-eraser-native.ppm");
        constexpr std::uint64_t ExpectedEraseHash = 0xcda041489b7a9b2bull;
        if (eraseHash != ExpectedEraseHash) {
            std::cerr << "word erase-Hall frame hash=0x" << std::hex << eraseHash
                      << std::dec << "\n";
        }
        game.keyDown(VK_DOWN);
        game.keyDown(VK_SPACE);
        if (game.eraseDraft_.size() != originalHall.size() - 1 ||
            game.hall_.entries() != originalHall) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 1 ||
            game.hall_.entries() != originalHall) return false;

        game.keyDown(VK_RETURN);
        game.keyDown(VK_SPACE);
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 1 ||
            game.hall_.entries().size() != originalHall.size() - 1 ||
            game.hall_.entries().front() != originalHall[1]) return false;

        game.keyDown(VK_RETURN);
        while (!game.eraseDraft_.empty()) game.keyDown(VK_SPACE);
        game.render(renderer);
        const std::uint64_t emptyEraseHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedEmptyEraseHash = 0x4159caa222636bbfull;
        if (emptyEraseHash != ExpectedEmptyEraseHash) {
            std::cerr << "word empty erase-Hall frame hash=0x" << std::hex
                      << emptyEraseHash << std::dec << "\n";
        }
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::OptionsEraseHall || game.eraseDraft_.size() != 0 ||
            game.hall_.entries().empty()) {
            return false;
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options || !game.hall_.entries().empty()) return false;

        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsEraseHall || !game.eraseDraft_.empty()) return false;
        game.render(renderer);
        const std::uint64_t initiallyEmptyEraseHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedInitiallyEmptyEraseHash = 0x0bfc01284bfe5e29ull;
        if (initiallyEmptyEraseHash != ExpectedInitiallyEmptyEraseHash) {
            std::cerr << "word initially-empty erase-Hall frame hash=0x" << std::hex
                      << initiallyEmptyEraseHash << std::dec << "\n";
        }

        WordGame liveInitiallyEmpty(GraphicsMode::Vga256, 0x77a0);
        liveInitiallyEmpty.settingsPersistenceEnabled_ = false;
        if (!liveInitiallyEmpty.hall_.replace({})) return false;
        liveInitiallyEmpty.page_ = WordGamePage::OptionsEraseHall;
        liveInitiallyEmpty.menuSelection_ = 1;
        liveInitiallyEmpty.eraseSelection_ = 0;
        liveInitiallyEmpty.eraseDraft_.clear();
        liveInitiallyEmpty.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "hall-initially-empty-live-native.ppm");
        constexpr std::uint64_t ExpectedLiveInitiallyEmptyHash = 0x0bfc01284bfe5e29ull;
        if (fullFrameHash(renderer) != ExpectedLiveInitiallyEmptyHash) {
            std::cerr << "word live initially-empty Hall hash=0x" << std::hex
                      << fullFrameHash(renderer) << std::dec << "\n";
            return false;
        }
        game.keyDown('3');
        if (game.page_ != WordGamePage::OptionsEraseHall) return false;
        game.character(L'3');
        game.keyDown(VK_RIGHT);
        game.pointerButton(0, 0, true);
        if (game.page_ != WordGamePage::OptionsEraseHall) return false;
        game.pointerButton(0, 0, false);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 1) return false;

        // Set a password and hint, prove a rejected attempt reaches the
        // original modal, then prove comparison is case-insensitive.
        game.menuSelection_ = 2;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPassword || game.passwordEditingHint_) {
            return false;
        }
        game.render(renderer);
        const std::uint64_t setPasswordHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedSetPasswordHash = 0xe0d28b7d3e121749ull;
        if (setPasswordHash != ExpectedSetPasswordHash) {
            std::cerr << "word set-password frame hash=0x" << std::hex
                      << setPasswordHash << std::dec << "\n";
        }
        game.character(L' ');
        if (!game.passwordDraft_.empty()) return false;
        for (int index = 0; index < 11; ++index) game.character(L'p');
        if (game.passwordDraft_ != std::string(10, 'p')) return false;
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            game.keyDown(ignored);
            if (game.passwordDraft_ != std::string(10, 'p')) return false;
        }
        game.keyDown(VK_LEFT);
        if (game.passwordDraft_ != std::string(9, 'p')) return false;
        game.character(L'p');
        game.keyDown(VK_HOME);
        game.character(L'A');
        game.character(L'b');
        game.character(L'C');
        game.keyDown(VK_RETURN);
        if (!game.passwordEditingHint_) return false;
        game.character(L' ');
        if (game.hintDraft_ != " ") return false;
        game.keyDown(VK_HOME);
        for (int index = 0; index < 51; ++index) game.character(L'h');
        if (game.hintDraft_ != std::string(50, 'h')) return false;
        game.keyDown(VK_HOME);
        game.character(L'h');
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options || game.optionPassword_ != "AbC" ||
            game.optionHint_ != "h") return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Title) return false;

        game.menuSelection_ = 3;
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPasswordPrompt ||
            game.passwordRejected_) return false;
        game.render(renderer);
        const std::uint64_t promptHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedPromptHash = 0x4626c6b56719b3e1ull;
        if (promptHash != ExpectedPromptHash) {
            std::cerr << "word password-prompt frame hash=0x" << std::hex
                      << promptHash << std::dec << "\n";
        }
        game.character(L'x');
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPasswordPrompt ||
            !game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        game.render(renderer);
        const std::uint64_t rejectedHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedRejectedHash = 0x00694bb174f63f0cull;
        if (rejectedHash != ExpectedRejectedHash) {
            std::cerr << "word rejected-password frame hash=0x" << std::hex
                      << rejectedHash << std::dec << "\n";
        }


        WordGame livePassword(GraphicsMode::Vga256, 0x77a1);
        livePassword.settingsPersistenceEnabled_ = false;
        livePassword.page_ = WordGamePage::OptionsPassword;
        livePassword.passwordEditingHint_ = false;
        livePassword.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "set-password-live-native.ppm");
        if (fullFrameHash(renderer) != 0xe0d28b7d3e121749ull) return false;
        livePassword.optionPassword_ = "test";
        livePassword.optionHint_ = "hint";
        livePassword.page_ = WordGamePage::OptionsPasswordPrompt;
        livePassword.passwordAttempt_.clear();
        livePassword.passwordRejected_ = false;
        livePassword.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "password-gate-live-native.ppm");
        if (fullFrameHash(renderer) != 0xf77f4595e849048full) {
            std::cerr << "word live password gate hash=0x" << std::hex
                      << fullFrameHash(renderer) << std::dec << "\n";
            return false;
        }
        livePassword.passwordRejected_ = true;
        livePassword.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "password-rejected-live-native.ppm");
        if (fullFrameHash(renderer) != 0x00694bb174f63f0cull) {
            std::cerr << "word live rejected-password hash=0x" << std::hex
                      << fullFrameHash(renderer) << std::dec << "\n";
            return false;
        }
        game.keyDown(VK_SPACE);
        if (game.passwordRejected_) return false;
        game.character(L' ');
        if (game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;

        game.character(L'z');
        game.character(L'y');
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            game.keyDown(ignored);
            if (game.passwordAttempt_ != "zy") return false;
        }
        game.keyDown(VK_LEFT);
        if (game.passwordAttempt_ != "z") return false;
        game.character(L'\b');
        if (!game.passwordAttempt_.empty()) return false;
        game.character(L'x');
        game.keyDown(VK_HOME);
        if (!game.passwordAttempt_.empty()) return false;

        // The terminating-NUL comparison rejects a longer attempt even when
        // its prefix matches case-insensitively.
        game.character(L'a');
        game.character(L'B');
        game.character(L'c');
        game.character(L'X');
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsPasswordPrompt ||
            !game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        game.keyDown('Q');
        if (!game.passwordRejected_) return false;
        game.character(L'q');
        game.keyDown(VK_RIGHT);
        game.pointerButton(0, 0, true);
        if (!game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        game.pointerButton(0, 0, false);
        if (game.passwordRejected_ || !game.passwordAttempt_.empty()) return false;
        game.character(L' ');
        if (!game.passwordAttempt_.empty()) return false;
        game.character(L'a');
        game.character(L'B');
        game.character(L'c');
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Options || !game.passwordAttempt_.empty() ||
            game.menuSelection_ != 0) return false;

        // Escape from the editor must preserve the active password and hint.
        game.menuSelection_ = 2;
        game.keyDown(VK_RETURN);
        game.character(L'x');
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.optionPassword_ != "AbC" ||
            game.optionHint_ != "h") return false;

        return previewHash == ExpectedPreviewHash &&
               livePreviewStateHash == ExpectedLivePreviewStateHash &&
               densePreviewHash == ExpectedDensePreviewHash &&
               eraseHash == ExpectedEraseHash &&
               emptyEraseHash == ExpectedEmptyEraseHash &&
               initiallyEmptyEraseHash == ExpectedInitiallyEmptyEraseHash &&
               setPasswordHash == ExpectedSetPasswordHash && promptHash == ExpectedPromptHash &&
               rejectedHash == ExpectedRejectedHash;
    }

    static bool joystickCalibrationAndInputRoutes(WordGame& game) {
        const auto fail = [](const char* stage) {
            std::cerr << "word joystick regression: " << stage << '\n';
            return false;
        };
        if (game.joystickEnabled_ || game.joystickCalibrated_) return false;
        Renderer renderer;
        std::array<std::uint64_t, 7> hashes{};
        constexpr std::array<std::uint64_t, 7> ExpectedHashes = {
            0x761c0d49d5d02fa6ull, 0x27ff5a6703f2c142ull,
            0x46644e94b2fe9619ull, 0xdffe93105501d39bull,
            0x97830740c746ee01ull, 0xe30346353ed1593eull,
            0x72aab5108b701944ull,
        };
        const auto capture = [&](const std::size_t index) {
            game.render(renderer);
            hashes[index] = fullFrameHash(renderer);
            if (hashes[index] != ExpectedHashes[index]) {
                std::cerr << "word joystick prompt " << index << " frame hash=0x"
                          << std::hex << hashes[index] << std::dec << "\n";
            }
        };

        game.page_ = WordGamePage::Options;
        game.menuSelection_ = 4;
        game.setJoystickState(false, 0, 0, 0);
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::OptionsJoystickCalibration ||
            game.joystickCalibrationStep_ != -1) return false;
        capture(0);

        WordGame liveAttach(GraphicsMode::Vga256, 0x76fb);
        liveAttach.settingsPersistenceEnabled_ = false;
        liveAttach.page_ = WordGamePage::OptionsJoystickCalibration;
        liveAttach.menuSelection_ = 4;
        liveAttach.joystickEnabled_ = true;
        liveAttach.joystickCalibrated_ = false;
        liveAttach.joystickCalibrationStep_ = -1;
        liveAttach.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "calibration-attach-live-native.ppm");
        if (fullFrameHash(renderer) != 0x761c0d49d5d02fa6ull) {
            std::cerr << "word live joystick-attach hash=0x" << std::hex
                      << fullFrameHash(renderer) << std::dec << "\n";
            return false;
        }
        game.keyDown('A');
        if (game.joystickCalibrationStep_ != -1) return false;
        game.setJoystickState(true, 0, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != -1) return false;
        game.setJoystickState(false, 0, 0, 0);
        game.setJoystickState(true, 1000, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 0) return false;
        capture(1);

        game.pointerButton(0, 0, true);
        if (game.joystickCalibrationStep_ != 0) return fail("calibration right release");
        game.pointerButton(0, 0, false);
        if (game.joystickCalibrationStep_ != 1) return false;
        capture(2);
        game.setJoystickState(true, 200, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 2 ||
            game.joystickDraftLeftThreshold_ != 600) return false;
        capture(3);
        game.setJoystickState(true, 1800, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 3 ||
            game.joystickDraftRightThreshold_ != 1400) return false;
        capture(4);
        game.setJoystickState(true, 1000, 400, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 4 ||
            game.joystickDraftUpThreshold_ != 1200) return false;
        capture(5);
        game.setJoystickState(true, 1000, 3600, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickCalibrationStep_ != 5 ||
            game.joystickDraftDownThreshold_ != 2800 || game.joystickEnabled_) return false;
        capture(6);
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 4 ||
            !game.joystickCalibrated_ || game.joystickEnabled_ ||
            game.joystickLeftThreshold_ != 600 || game.joystickRightThreshold_ != 1400 ||
            game.joystickUpThreshold_ != 1200 || game.joystickDownThreshold_ != 2800) {
            return false;
        }

        // Escape discards a partial recalibration without altering the
        // installed thresholds.
        game.setJoystickState(true, 1000, 2000, 0);
        game.keyDown(VK_RETURN);
        game.keyDown(VK_SPACE);
        game.setJoystickState(true, 400, 2000, 0);
        game.keyDown(VK_SPACE);
        if (game.joystickDraftLeftThreshold_ == 600) return false;
        game.keyDown(VK_ESCAPE);
        if (game.page_ != WordGamePage::Options || game.menuSelection_ != 4 ||
            game.joystickLeftThreshold_ != 600 || game.joystickRightThreshold_ != 1400 ||
            game.joystickUpThreshold_ != 1200 || game.joystickDownThreshold_ != 2800) {
            return false;
        }

        const auto prepareJoystick = [](WordGame& target) {
            target.startGame(1);
            disableBoardJobs(target);
            target.joystickEnabled_ = true;
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
        constexpr std::array<std::array<std::uint32_t, 2>, 4> axes = {{
            {{1000, 1000}}, {{1600, 2000}}, {{1000, 3000}}, {{500, 2000}},
        }};
        constexpr std::array<std::array<int, 2>, 4> deltas = {{
            {{-1, 0}}, {{0, 1}}, {{1, 0}}, {{0, -1}},
        }};
        for (std::size_t index = 0; index < axes.size(); ++index) {
            WordGame directionGame(GraphicsMode::Vga256,
                                   static_cast<std::uint16_t>(0x7100 + index));
            prepareJoystick(directionGame);
            const int row = directionGame.playerRow_;
            const int column = directionGame.playerColumn_;
            directionGame.setJoystickState(true, axes[index][0], axes[index][1], 0);
            directionGame.updateJoystickInput(0.0);
            if (directionGame.moving_) return fail("axis dispatched before deadline");
            directionGame.updateJoystickInput(4.1 / WordSchedulerTicksPerSecond);
            if (!directionGame.moving_ ||
                directionGame.moveToRow_ != row + deltas[index][0] ||
                directionGame.moveToColumn_ != column + deltas[index][1]) {
                return fail("queued cardinal direction");
            }
        }

        WordGame diagonal(GraphicsMode::Vga256, 0x7200);
        prepareJoystick(diagonal);
        diagonal.setJoystickState(true, 500, 1000, 0);
        diagonal.updateJoystickInput(0.0);
        diagonal.updateJoystickInput(4.1 / WordSchedulerTicksPerSecond);
        if (diagonal.moving_) return fail("diagonal rejection");

        // Held directions repeat every four scheduler ticks.
        WordGame repeat(GraphicsMode::Vga256, 0x7300);
        repeat.page_ = WordGamePage::Options;
        repeat.menuSelection_ = 0;
        repeat.joystickEnabled_ = true;
        repeat.joystickCalibrated_ = true;
        repeat.joystickLeftThreshold_ = 600;
        repeat.joystickRightThreshold_ = 1400;
        repeat.joystickUpThreshold_ = 1200;
        repeat.joystickDownThreshold_ = 2800;
        repeat.joystickClockFraction_ = 0.0;
        repeat.joystickClockTicks_ = 100;
        repeat.joystickCachedClockTicks_ = 100;
        repeat.joystickNextRepeatTicks_ = 104;
        repeat.joystickPendingDirection_ = 0;
        repeat.joystickLastSampledDirection_ = 0;
        repeat.setJoystickState(true, 1000, 2000, 0);
        repeat.updateJoystickInput(0.0);
        repeat.setJoystickState(true, 1000, 3000, 0);
        repeat.updateJoystickInput(0.0);
        if (repeat.menuSelection_ != 0 ||
            repeat.joystickPendingDirection_ != VK_DOWN) return fail("pending sample");
        repeat.updateJoystickInput(4.1 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ != 1 || repeat.joystickPendingDirection_ != 0) {
            return fail("first pending delivery");
        }
        repeat.updateJoystickInput(0.0);
        repeat.updateJoystickInput(0.1);
        if (repeat.menuSelection_ != 1) return fail("held repeat fired early");
        repeat.updateJoystickInput(0.04);
        if (repeat.menuSelection_ != 2) return fail("held repeat missed deadline");

        // Word's isomorphic resident dispatcher is likewise absent from every
        // synchronous modal page. Exhaustively lock that boundary, including
        // the two private attract page values when the Demo flag is not live.
        constexpr std::array<WordGamePage, 21> modalPages = {
            WordGamePage::StartupVersion, WordGamePage::StartupSplash,
            WordGamePage::Title, WordGamePage::InstructionsQuestion,
            WordGamePage::Information, WordGamePage::Options,
            WordGamePage::OptionsContent, WordGamePage::OptionsDifficulty,
            WordGamePage::OptionsVowels, WordGamePage::OptionsVowelsHelp,
            WordGamePage::OptionsPreview, WordGamePage::OptionsEraseHall,
            WordGamePage::OptionsPassword, WordGamePage::OptionsPasswordPrompt,
            WordGamePage::OptionsJoystickCalibration, WordGamePage::Hall,
            WordGamePage::Attract, WordGamePage::AttractLogo,
            WordGamePage::QuitConfirm, WordGamePage::NameEntry,
            WordGamePage::ReplayQuestion,
        };
        std::uint16_t modalSeed = 0x7310;
        for (const WordGamePage page : modalPages) {
            WordGame modal(GraphicsMode::Vga256, modalSeed++);
            modal.page_ = page;
            modal.attractMode_ = false;
            modal.cheatOpen_ = false;
            modal.joystickEnabled_ = true;
            modal.joystickCalibrated_ = true;
            modal.joystickRepeatTicks_ = 4;
            const bool sound = modal.soundOn_;
            const bool music = modal.musicOn_;
            const bool speaker = modal.speakerEffects_;
            const std::size_t plays = modal.sceneEffectPlayer_.playCount();
            if (modal.acceptsCommonDispatcherInput()) return fail("modal dispatcher predicate");
            modal.character(L'+');
            modal.toggleSound();
            modal.toggleMusic();
            modal.toggleSpeaker();
            if (modal.joystickRepeatTicks_ != 4 ||
                modal.sceneEffectPlayer_.playCount() != plays ||
                modal.soundOn_ != sound || modal.musicOn_ != music ||
                modal.speakerEffects_ != speaker) return fail("modal shortcut leakage");
        }
        for (const WordGamePage page : {WordGamePage::Playing, WordGamePage::Paused,
                                        WordGamePage::Feedback,
                                        WordGamePage::LevelComplete}) {
            WordGame live(GraphicsMode::Vga256, modalSeed++);
            live.page_ = page;
            if (!live.acceptsCommonDispatcherInput()) return fail("live dispatcher predicate");
            live.cheatOpen_ = true;
            if (live.acceptsCommonDispatcherInput()) return fail("cheat dispatcher isolation");
        }

        WordGame repeatControl(GraphicsMode::Vga256, 0x7301);
        repeatControl.page_ = WordGamePage::Playing;
        repeatControl.joystickEnabled_ = false;
        repeatControl.joystickCalibrated_ = false;
        if (repeatControl.joystickRepeatTicks_ != 4) {
            std::cerr << "word joystick repeat: default\n";
            return false;
        }
        for (const wchar_t key : {L'+', L'=', L'-', L'_'}) repeatControl.character(key);
        if (repeatControl.joystickRepeatTicks_ != 4) {
            std::cerr << "word joystick repeat: disabled gate\n";
            return false;
        }
        repeatControl.joystickCalibrated_ = true;
        const std::size_t feedbackStarts = repeatControl.sceneEffectPlayer_.playCount();
        repeatControl.character(L'_');
        repeatControl.character(L'=');
        if (repeatControl.joystickRepeatTicks_ != 4) {
            std::cerr << "word joystick repeat: shifted key aliases\n";
            return false;
        }
        if (repeatControl.lastJoystickRepeatFeedbackHz_ != 800 ||
            repeatControl.sceneEffectPlayer_.playCount() != feedbackStarts + 2 ||
            repeatControl.sceneEffectPlayer_.bufferSize() != 4454) {
            std::cerr << "word joystick repeat: direct PIT feedback\n";
            return false;
        }
        for (int expected = 5; expected <= 10; ++expected) {
            repeatControl.character(L'-');
            if (repeatControl.joystickRepeatTicks_ != expected ||
                repeatControl.lastJoystickRepeatFeedbackHz_ != 1000 - 50 * expected ||
                repeatControl.sceneEffectPlayer_.bufferSize() != 4454) {
                std::cerr << "word joystick repeat: feedback pitch sweep up\n";
                return false;
            }
        }
        if (repeatControl.joystickRepeatTicks_ != 10) {
            std::cerr << "word joystick repeat: upper clamp\n";
            return false;
        }
        const std::size_t upperBoundStarts = repeatControl.sceneEffectPlayer_.playCount();
        repeatControl.character(L'_');
        if (repeatControl.joystickRepeatTicks_ != 10) {
            std::cerr << "word joystick repeat: shifted upper clamp\n";
            return false;
        }
        if (repeatControl.sceneEffectPlayer_.playCount() != upperBoundStarts) {
            std::cerr << "word joystick repeat: upper bound tone suppression\n";
            return false;
        }
        for (int expected = 9; expected >= 1; --expected) {
            repeatControl.character(L'+');
            if (repeatControl.joystickRepeatTicks_ != expected ||
                repeatControl.lastJoystickRepeatFeedbackHz_ != 1000 - 50 * expected ||
                repeatControl.sceneEffectPlayer_.bufferSize() != 4454) {
                std::cerr << "word joystick repeat: feedback pitch sweep down\n";
                return false;
            }
        }
        if (repeatControl.joystickRepeatTicks_ != 1) {
            std::cerr << "word joystick repeat: lower clamp\n";
            return false;
        }
        const std::size_t lowerBoundStarts = repeatControl.sceneEffectPlayer_.playCount();
        repeatControl.character(L'=');
        if (repeatControl.joystickRepeatTicks_ != 1) {
            std::cerr << "word joystick repeat: unshifted lower clamp\n";
            return false;
        }
        if (repeatControl.sceneEffectPlayer_.playCount() != lowerBoundStarts) {
            std::cerr << "word joystick repeat: lower bound tone suppression\n";
            return false;
        }
        repeatControl.joystickCalibrated_ = false;
        repeatControl.joystickEnabled_ = true;
        repeatControl.character(L'-');
        if (repeatControl.joystickRepeatTicks_ != 2) {
            std::cerr << "word joystick repeat: enabled-only gate\n";
            return false;
        }

        WordGame demoRepeat(GraphicsMode::Vga256, 0x7302);
        demoRepeat.startAttract();
        demoRepeat.joystickEnabled_ = true;
        demoRepeat.joystickCalibrated_ = false;
        demoRepeat.joystickRepeatTicks_ = 4;
        demoRepeat.keyDown(VK_OEM_PLUS);
        if (!demoRepeat.attractMode_ || demoRepeat.joystickRepeatTicks_ != 4) {
            return fail("Demo translated repeat deferral");
        }
        demoRepeat.character(L'+');
        if (demoRepeat.attractMode_ || demoRepeat.page_ != WordGamePage::Title ||
            demoRepeat.joystickRepeatTicks_ != 3 ||
            demoRepeat.lastJoystickRepeatFeedbackHz_ != 850 ||
            !demoRepeat.sceneEffectPlayer_.playing() ||
            demoRepeat.sceneEffectPlayer_.bufferSize() != 4454) {
            return fail("Demo repeat-before-exit ordering");
        }

        repeat.joystickRepeatTicks_ = 10;
        repeat.joystickClockFraction_ = 0.0;
        repeat.joystickCachedClockTicks_ = repeat.joystickClockTicks_;
        repeat.joystickNextRepeatTicks_ = repeat.joystickClockTicks_ + 10;
        repeat.joystickPendingDirection_ = 0;
        repeat.joystickLastSampledDirection_ = 0;
        repeat.setJoystickState(true, 1000, 2000, 0);
        repeat.updateJoystickInput(0.0);
        repeat.setJoystickState(true, 1000, 3000, 0);
        repeat.updateJoystickInput(0.0);
        const int slowFirst = repeat.menuSelection_;
        // Accumulate the ten-tick deadline across the same 100 ms-or-smaller
        // controller slices used by the production app owner.
        for (int frame = 0; frame < 3; ++frame) repeat.updateJoystickInput(0.1);
        repeat.updateJoystickInput(0.9 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ != slowFirst) {
            std::cerr << "word joystick repeat: slow fired early\n";
            return false;
        }
        repeat.updateJoystickInput(0.5 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ == slowFirst) {
            std::cerr << "word joystick repeat: slow missed deadline\n";
            return false;
        }

        repeat.joystickRepeatTicks_ = 1;
        repeat.joystickClockFraction_ = 0.0;
        repeat.joystickCachedClockTicks_ = repeat.joystickClockTicks_;
        repeat.joystickNextRepeatTicks_ = repeat.joystickClockTicks_ + 1;
        repeat.joystickPendingDirection_ = 0;
        repeat.joystickLastSampledDirection_ = 0;
        repeat.setJoystickState(true, 1000, 2000, 0);
        repeat.updateJoystickInput(0.0);
        repeat.setJoystickState(true, 1000, 3000, 0);
        repeat.updateJoystickInput(0.0);
        const int fastFirst = repeat.menuSelection_;
        repeat.updateJoystickInput(0.9 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ != fastFirst) {
            std::cerr << "word joystick repeat: fast fired early\n";
            return false;
        }
        repeat.updateJoystickInput(0.2 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ == fastFirst) {
            std::cerr << "word joystick repeat: fast missed deadline\n";
            return false;
        }

        const int beforeOverrun = repeat.menuSelection_;
        repeat.updateJoystickInput(0.1);
        if (repeat.menuSelection_ == beforeOverrun ||
            repeat.joystickNextRepeatTicks_ !=
                repeat.joystickCachedClockTicks_ + 1) {
            std::cerr << "word joystick repeat: overdue deadline reset\n";
            return false;
        }
        const int afterOverrun = repeat.menuSelection_;
        repeat.joystickClockFraction_ = 0.0;
        repeat.updateJoystickInput(0.0);
        repeat.updateJoystickInput(0.9 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ != afterOverrun) {
            std::cerr << "word joystick repeat: overdue follow-on fired early\n";
            return false;
        }
        repeat.updateJoystickInput(0.2 / WordSchedulerTicksPerSecond);
        if (repeat.menuSelection_ == afterOverrun) {
            std::cerr << "word joystick repeat: overdue follow-on missed deadline\n";
            return false;
        }

        WordGame movementGate(GraphicsMode::Vga256, 0x7350);
        prepareJoystick(movementGate);
        movementGate.setJoystickState(true, 1600, 2000, 0);
        movementGate.joystickRepeatTicks_ = 4;
        movementGate.joystickCachedClockTicks_ = movementGate.joystickClockTicks_;
        movementGate.joystickNextRepeatTicks_ = movementGate.joystickClockTicks_ + 1;
        movementGate.joystickPendingDirection_ = VK_RIGHT;
        movementGate.joystickLastSampledDirection_ = VK_RIGHT;
        movementGate.moving_ = true;
        const std::uint64_t gatedCachedClock = movementGate.joystickCachedClockTicks_;
        const std::uint64_t gatedDeadline = movementGate.joystickNextRepeatTicks_;
        movementGate.updateJoystickInput(2.0 / WordSchedulerTicksPerSecond);
        if (!movementGate.moving_ ||
            movementGate.joystickCachedClockTicks_ != gatedCachedClock ||
            movementGate.joystickNextRepeatTicks_ != gatedDeadline ||
            movementGate.joystickPendingDirection_ != VK_RIGHT) {
            std::cerr << "word joystick repeat: movement direction gate\n";
            return false;
        }
        movementGate.moving_ = false;
        const int movementColumn = movementGate.playerColumn_;
        movementGate.updateJoystickInput(0.0);
        if (!movementGate.moving_ ||
            movementGate.moveToColumn_ != movementColumn + 1 ||
            movementGate.joystickNextRepeatTicks_ !=
                movementGate.joystickCachedClockTicks_ + 4 ||
            movementGate.joystickPendingDirection_ != 0) {
            std::cerr << "word joystick repeat: movement terminal eligibility\n";
            return false;
        }

        WordGame buttonZero(GraphicsMode::Vga256, 0x7400);
        prepareJoystick(buttonZero);
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!buttonZero.core_.eaten(static_cast<std::size_t>(index))) {
                buttonZero.playerRow_ = index / WordGame::BoardColumns;
                buttonZero.playerColumn_ = index % WordGame::BoardColumns;
                break;
            }
        }
        buttonZero.setJoystickState(true, 1000, 2000, 0x03);
        buttonZero.update(0.0);
        if (!buttonZero.munching_ || buttonZero.page_ != WordGamePage::Playing) return false;

        WordGame buttonOne(GraphicsMode::Vga256, 0x7500);
        prepareJoystick(buttonOne);
        buttonOne.setJoystickState(true, 1000, 2000, 0x02);
        buttonOne.update(0.0);
        if (buttonOne.page_ != WordGamePage::Paused) return false;
        buttonOne.setJoystickState(true, 1000, 2000, 0);
        buttonOne.update(0.0);
        if (buttonOne.page_ != WordGamePage::Paused) return false;
        buttonOne.setJoystickState(true, 1000, 2000, 0x02);
        buttonOne.update(0.0);
        if (buttonOne.page_ != WordGamePage::Playing) return false;

        WordGame gated(GraphicsMode::Vga256, 0x7600);
        prepareJoystick(gated);
        gated.joystickEnabled_ = false;
        gated.setJoystickState(true, 1600, 2000, 0x01);
        gated.update(0.0);
        if (gated.moving_ || gated.munching_) return false;
        gated.joystickEnabled_ = true;
        gated.setJoystickState(false, 0, 0, 0);
        gated.update(0.0);
        if (gated.moving_ || gated.munching_) return false;

        return hashes == ExpectedHashes;
    }

    static bool settingsPersistenceRoundTrip(WordGame& game) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("word-munchers-native-settings-" + std::to_string(GetCurrentProcessId()) + ".txt");
        const std::filesystem::path cancelPath = std::filesystem::temp_directory_path() /
            ("word-munchers-password-cancel-" + std::to_string(GetCurrentProcessId()) + ".txt");
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(cancelPath, error);

        // At image 0x0F7DE, accepting the password field sets the routine's
        // result byte.  A later hint Escape restores both strings but leaves
        // that result set, so the Options caller at 0x0FC55 saves the unchanged
        // configuration.  Escaping the first field never sets the result.
        WordGame passwordCancel(GraphicsMode::Vga256, 0x76ff);
        passwordCancel.settingsPathOverride_ = cancelPath.wstring();
        passwordCancel.settingsPersistenceEnabled_ = true;
        passwordCancel.optionPassword_ = "Munch";
        passwordCancel.optionHint_ = "five letters";
        passwordCancel.passwordDraft_ = "Other";
        passwordCancel.hintDraft_ = "changed hint";
        passwordCancel.page_ = WordGamePage::OptionsPassword;
        passwordCancel.passwordEditingHint_ = false;
        passwordCancel.keyDown(VK_ESCAPE);
        if (std::filesystem::exists(cancelPath)) return false;

        passwordCancel.passwordDraft_ = "Other";
        passwordCancel.hintDraft_ = "changed hint";
        passwordCancel.page_ = WordGamePage::OptionsPassword;
        passwordCancel.passwordEditingHint_ = true;
        passwordCancel.keyDown(VK_ESCAPE);
        if (!std::filesystem::exists(cancelPath) ||
            passwordCancel.optionPassword_ != "Munch" ||
            passwordCancel.optionHint_ != "five letters") {
            std::filesystem::remove(cancelPath, error);
            return false;
        }
        WordGame restoredCancel(GraphicsMode::Vga256, 0x76fe);
        restoredCancel.settingsPathOverride_ = cancelPath.wstring();
        restoredCancel.settingsPersistenceEnabled_ = true;
        restoredCancel.loadSettings();
        if (restoredCancel.optionPassword_ != "Munch" ||
            restoredCancel.optionHint_ != "five letters") {
            std::filesystem::remove(cancelPath, error);
            return false;
        }
        std::filesystem::remove(cancelPath, error);

        // The original save wrapper at 0x0FCE7 reports a failed write through
        // the blocking common alert at 0x07E21.  An existing directory is an
        // intentionally invalid std::ofstream destination and needs no device
        // or GUI interaction to exercise the native branch.
        WordGame writeFailure(GraphicsMode::Vga256, 0x76fd);
        writeFailure.settingsPathOverride_ = std::filesystem::temp_directory_path().wstring();
        writeFailure.settingsPersistenceEnabled_ = true;
        writeFailure.page_ = WordGamePage::OptionsPassword;
        writeFailure.passwordEditingHint_ = true;
        writeFailure.optionPassword_ = "Munch";
        writeFailure.optionHint_ = "five letters";
        writeFailure.passwordDraft_ = "Other";
        writeFailure.hintDraft_ = "changed hint";
        writeFailure.keyDown(VK_ESCAPE);
        if (!writeFailure.configurationWriteError_ ||
            writeFailure.configurationWriteErrorBackgroundPage_ !=
                WordGamePage::OptionsPassword ||
            writeFailure.page_ != WordGamePage::Options) {
            return false;
        }
        Renderer writeFailureRenderer;
        writeFailure.render(writeFailureRenderer);
        const std::uint64_t writeFailureHash = fullFrameHash(writeFailureRenderer);
        constexpr std::uint64_t ExpectedWriteFailureHash = 0x6328558d63e2cf11ull;
        if (writeFailureHash != ExpectedWriteFailureHash) {
            std::cerr << "word configuration-write alert frame hash=0x" << std::hex
                      << writeFailureHash << std::dec << "\n";
            return false;
        }
        writeFailure.pointerButton(0, 0, false);
        if (!writeFailure.configurationWriteError_) return false;
        writeFailure.keyDown('Q');
        if (!writeFailure.configurationWriteError_) return false;
        writeFailure.character(L'q');
        if (writeFailure.configurationWriteError_ ||
            writeFailure.page_ != WordGamePage::Options) {
            return false;
        }

        // The muted original write-protection run toggles the supplied
        // joystick-enabled state from ON to OFF on Options row 4. Reproduce
        // that exact underlying page rather than the password-editor fixture
        // used above.
        WordGame liveWriteFailure(GraphicsMode::Vga256, 0x76f0);
        liveWriteFailure.settingsPathOverride_ =
            std::filesystem::temp_directory_path().wstring();
        liveWriteFailure.settingsPersistenceEnabled_ = true;
        liveWriteFailure.page_ = WordGamePage::Options;
        liveWriteFailure.menuSelection_ = 3;
        liveWriteFailure.joystickEnabled_ = true;
        liveWriteFailure.keyDown(VK_RETURN);
        if (!liveWriteFailure.configurationWriteError_ ||
            liveWriteFailure.joystickEnabled_) return false;
        Renderer liveWriteFailureRenderer;
        liveWriteFailure.render(liveWriteFailureRenderer);
        maybeDumpWordOptionsFrame(liveWriteFailureRenderer,
                                  "write-error-live-native.ppm");
        constexpr std::uint64_t ExpectedLiveWriteFailureHash = 0x1a6ebbc842249539ull;
        if (fullFrameHash(liveWriteFailureRenderer) != ExpectedLiveWriteFailureHash) {
            std::cerr << "word live configuration-write alert hash=0x" << std::hex
                      << fullFrameHash(liveWriteFailureRenderer) << std::dec << "\n";
            return false;
        }

        WordGame cgaWriteFailure(GraphicsMode::Cga4, 0x76fc);
        cgaWriteFailure.settingsPathOverride_ =
            std::filesystem::temp_directory_path().wstring();
        cgaWriteFailure.settingsPersistenceEnabled_ = true;
        cgaWriteFailure.page_ = WordGamePage::Options;
        cgaWriteFailure.menuSelection_ = 3;
        cgaWriteFailure.keyDown(VK_RETURN);
        if (!cgaWriteFailure.configurationWriteError_ ||
            !cgaWriteFailure.joystickEnabled_) {
            return false;
        }
        Renderer cgaWriteFailureRenderer(GraphicsMode::Cga4);
        cgaWriteFailure.render(cgaWriteFailureRenderer);
        const std::uint64_t cgaWriteFailureHash = fullFrameHash(cgaWriteFailureRenderer);
        constexpr std::uint64_t ExpectedCgaWriteFailureHash = 0x5cd9d111ff029539ull;
        if (cgaWriteFailureHash != ExpectedCgaWriteFailureHash) {
            std::cerr << "word CGA configuration-write alert frame hash=0x" << std::hex
                      << cgaWriteFailureHash << std::dec << "\n";
            return false;
        }
        cgaWriteFailure.keyDown(VK_F2);
        if (cgaWriteFailure.configurationWriteError_) return false;

        game.settingsPathOverride_ = path.wstring();
        game.settingsPersistenceEnabled_ = true;
        game.wordDifficulty_ = 5;
        game.selectedSounds_.fill(false);
        game.selectedSounds_[0] = true;
        game.selectedSounds_[1] = true;
        game.contentDraftDifficulty_ = game.wordDifficulty_;
        game.contentDraftSounds_ = game.selectedSounds_;
        if (!game.core_.configure(game.selectedSounds_, game.wordDifficulty_)) return false;
        game.soundOn_ = false;
        game.musicOn_ = false;
        game.speakerEffects_ = true;
        game.joystickEnabled_ = true;
        game.joystickCalibrated_ = true;
        game.joystickLeftThreshold_ = 600;
        game.joystickRightThreshold_ = 1400;
        game.joystickUpThreshold_ = 1200;
        game.joystickDownThreshold_ = 2800;
        game.optionPassword_ = "Munch";
        game.optionHint_ = std::string(50, 'H');
        const std::vector<WordHallScoreEntry> hall = {
            {"Alpha Muncher", 9'500}, {"Beta Muncher", 50},
        };
        if (!game.hall_.replace(hall)) return false;
        game.saveSettings();
        if (!std::filesystem::exists(path)) return false;

        WordGame loaded(GraphicsMode::Vga256, 0x7700);
        loaded.settingsPathOverride_ = path.wstring();
        loaded.settingsPersistenceEnabled_ = true;
        loaded.loadSettings();
        const bool matches = loaded.wordDifficulty_ == 5 && loaded.selectedSounds_[0] &&
            loaded.selectedSounds_[1] && loaded.selectedSoundCount(loaded.selectedSounds_) == 2 &&
            loaded.core_.configured() && !loaded.soundOn_ && !loaded.musicOn_ &&
            loaded.speakerEffects_ && loaded.joystickEnabled_ && loaded.joystickCalibrated_ &&
            loaded.joystickLeftThreshold_ == 600 && loaded.joystickRightThreshold_ == 1400 &&
            loaded.joystickUpThreshold_ == 1200 && loaded.joystickDownThreshold_ == 2800 &&
            loaded.optionPassword_ == "Munch" && loaded.optionHint_ == std::string(50, 'H') &&
            loaded.hall_.entries() == hall;
        if (!matches) {
            std::cerr << "word persistence loaded diff=" << loaded.wordDifficulty_
                      << " selected=" << loaded.selectedSoundCount(loaded.selectedSounds_)
                      << " core=" << loaded.core_.configured()
                      << " sound=" << loaded.soundOn_ << " music=" << loaded.musicOn_
                      << " speaker=" << loaded.speakerEffects_
                      << " joystick=" << loaded.joystickEnabled_
                      << " calibrated=" << loaded.joystickCalibrated_
                      << " thresholds=" << loaded.joystickLeftThreshold_ << ','
                      << loaded.joystickRightThreshold_ << ',' << loaded.joystickUpThreshold_
                      << ',' << loaded.joystickDownThreshold_
                      << " password=" << loaded.optionPassword_ << " hint=" << loaded.optionHint_
                      << " hall=" << loaded.hall_.entries().size() << "\n";
        }
        WordGame invalidSource(GraphicsMode::Vga256, 0x7701);
        invalidSource.settingsPathOverride_ = path.wstring();
        invalidSource.settingsPersistenceEnabled_ = true;
        invalidSource.optionPassword_ = "Munch";
        invalidSource.optionHint_ = std::string(51, 'H');
        invalidSource.saveSettings();
        WordGame rejected(GraphicsMode::Vga256, 0x7702);
        rejected.settingsPathOverride_ = path.wstring();
        rejected.settingsPersistenceEnabled_ = true;
        rejected.loadSettings();
        const bool rejectsOverlongHint = rejected.optionHint_.empty();
        std::filesystem::remove(path, error);
        game.settingsPersistenceEnabled_ = false;
        return matches && rejectsOverlongHint;
    }

    static bool gameplayAudioRoutes(WordGame& game) {
        const auto fail = [](const char* stage) {
            std::cerr << "word gameplay-audio stage: " << stage << "\n";
            return false;
        };
        const BlobView gameplayBank = game.assets_.gameArchive().find("GSND", 2);
        if (!gameplayBank || gameplayBank.size != 2147) return fail("bank");
        constexpr std::array<std::uint8_t, 11> patchRegisters = {
            0x20, 0x23, 0xc0, 0xe0, 0xe3, 0x40, 0x43, 0x60, 0x63, 0x80, 0x83,
        };
        constexpr std::array<std::uint8_t, 11> patchE8 = {
            0x00, 0x00, 0x06, 0x00, 0x00, 0x01, 0x07, 0xc9, 0xc4, 0x74, 0xf7,
        };
        constexpr std::array<std::uint8_t, 11> patchE9 = {
            0x00, 0x00, 0x06, 0x00, 0x00, 0x0a, 0x0a, 0xc9, 0xc4, 0x74, 0xf7,
        };
        const auto containsPatch = [&](const MeccSound& sound,
                                       const std::array<std::uint8_t, 11>& patch) {
            if (sound.writes.size() < patch.size()) return false;
            for (std::size_t start = 0; start + patch.size() <= sound.writes.size(); ++start) {
                bool matches = true;
                for (std::size_t index = 0; index < patch.size(); ++index) {
                    const OplWrite& write = sound.writes[start + index];
                    if (write.reg != patchRegisters[index] || write.value != patch[index]) {
                        matches = false;
                        break;
                    }
                }
                if (matches) return true;
            }
            return false;
        };
        constexpr std::array<std::uint8_t, 12> streams = {
            3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        };
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
        for (const std::uint8_t stream : streams) {
            const MeccSound decoded = decodeMeccGSound(
                gameplayBank, stream, MeccSoundProfile::WordMunchers);
            const std::vector<std::uint8_t> adlib = renderMeccGSoundToWave(
                gameplayBank, stream, 300, MeccSoundProfile::WordMunchers);
            const std::vector<std::uint8_t> speaker = renderMeccSpeakerSoundToWave(
                gameplayBank, stream, 40, MeccSoundProfile::WordMunchers);
            const bool channelZeroOnly = std::none_of(
                decoded.writes.begin(), decoded.writes.end(), [&](const OplWrite& write) {
                    return targetsNonzeroOplChannel(write.reg);
                });
            if (!decoded.valid || !channelZeroOnly ||
                adlib.size() <= 44 || speaker.size() <= 44) {
                std::cerr << "word gameplay-audio decode stream=" << static_cast<int>(stream)
                          << " valid=" << decoded.valid << " ch0=" << channelZeroOnly
                          << " adlib=" << adlib.size()
                          << " speaker=" << speaker.size() << "\n";
                return fail("decode");
            }
            if ((stream == 4 && !containsPatch(decoded, patchE8)) ||
                (stream == 5 && !containsPatch(decoded, patchE9))) {
                return fail("Word-only OPL patch");
            }
        }

        const auto placeOnCell = [](WordGame& target, const bool correct) {
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (!target.core_.eaten(static_cast<std::size_t>(index)) &&
                    target.core_.board().cells[static_cast<std::size_t>(index)].correct == correct) {
                    target.playerRow_ = index / WordGame::BoardColumns;
                    target.playerColumn_ = index % WordGame::BoardColumns;
                    return true;
                }
            }
            return false;
        };

        WordGame correct(GraphicsMode::Vga256, 0x7700);
        correct.startGame(1);
        if (correct.lastGameplaySound_ != -1 ||
            adlibEffectPlays(correct) != 0) return fail("silent board start");
        disableBoardJobs(correct);
        if (!placeOnCell(correct, true)) return fail("correct cell");
        std::size_t plays = adlibEffectPlays(correct);
        correct.beginMunch();
        if (!correct.munching_ || correct.lastGameplaySound_ != 7 ||
            adlibEffectPlays(correct) != plays + 1 ||
            !correct.attractOplPlayer_.playing() || correct.attractOplPlayer_.looping() ||
            correct.sceneEffectPlayer_.playing()) return fail("correct cue");

        WordGame wrong(GraphicsMode::Vga256, 0x7701);
        wrong.startGame(1);
        disableBoardJobs(wrong);
        if (!placeOnCell(wrong, false)) return fail("wrong cell");
        plays = adlibEffectPlays(wrong);
        wrong.beginMunch();
        if (!wrong.munching_ || wrong.lastGameplaySound_ != 8 ||
            adlibEffectPlays(wrong) != plays + 1) return fail("wrong cue");

        WordGame warning(GraphicsMode::Vga256, 0x7702);
        warning.startGame(1);
        if (warning.enemySlotCount_ < 1 ||
            warning.enemySlots_[0].phase != WordGame::EnemySlotPhase::Waiting) return fail("warning setup");
        plays = adlibEffectPlays(warning);
        if (!warning.beginEnemyWarning(0) || warning.lastGameplaySound_ != 9 ||
            adlibEffectPlays(warning) != plays + 1) return fail("warning cue");
        plays = adlibEffectPlays(warning);
        warning.spawnPendingEnemy(0);
        if (warning.enemySlots_[0].phase != WordGame::EnemySlotPhase::Active ||
            warning.lastGameplaySound_ != 3 ||
            adlibEffectPlays(warning) != plays + 1) return fail("entry cue");

        WordGame information(GraphicsMode::Vga256, 0x7707);
        advanceStartup(information);
        information.keyDown(VK_RETURN);
        plays = adlibEffectPlays(information);
        information.keyDown('Y');
        if (information.page_ != WordGamePage::InstructionsQuestion ||
            information.menuSelection_ != 0 ||
            adlibEffectPlays(information) != plays) return fail("information selection");
        information.keyDown(VK_RETURN);
        if (information.page_ != WordGamePage::Information || information.informationGroup_ != 1 ||
            information.informationPage_ != 0 || information.lastGameplaySound_ != 13 ||
            adlibEffectPlays(information) != plays + 1) return fail("information cue 13");
        information.keyDown(VK_SPACE);
        if (information.informationPage_ != 1 || information.lastGameplaySound_ != 14 ||
            information.previousGameplaySound_ != 13 ||
            adlibEffectPlays(information) != plays + 2) return fail("information cue 14");

        WordGame safe(GraphicsMode::Vga256, 0x7703);
        safe.startGame(1);
        disableBoardJobs(safe);
        safe.safeZoneJobCount_ = 1;
        safe.safeZoneJobs_[0].active = true;
        safe.safeZoneJobs_[0].cellIndex = 7;
        safe.safeZoneJobs_[0].period = 10.0;
        safe.safeZoneJobs_[0].timer = 0.0;
        safe.safeCells_[7] = true;
        plays = adlibEffectPlays(safe);
        safe.updateSafeZones(0.0);
        if (safe.safeZoneJobs_[0].active || safe.safeCells_[7] ||
            safe.lastGameplaySound_ != 5 ||
            adlibEffectPlays(safe) != plays + 1) return fail("safe off cue");

        const OriginalRandom safeRandom = safe.random_;
        const int activatedCell = safe.chooseSafeZoneCell();
        safe.random_ = safeRandom;
        WordGame::Enemy displaced;
        displaced.row = activatedCell / WordGame::BoardColumns;
        displaced.column = activatedCell % WordGame::BoardColumns;
        displaced.fromRow = displaced.row;
        displaced.fromColumn = displaced.column;
        displaced.type = 4;
        displaced.slot = 0;
        displaced.savedCell = safe.core_.board().cells[static_cast<std::size_t>(activatedCell)];
        displaced.savedCellEaten = safe.core_.eaten(static_cast<std::size_t>(activatedCell));
        displaced.savedCellValid = true;
        safe.enemySlotCount_ = 1;
        safe.enemySlots_[0].type = 4;
        safe.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        safe.enemies_.push_back(displaced);
        safe.safeZoneJobs_[0].timer = 0.0;
        plays = adlibEffectPlays(safe);
        safe.updateSafeZones(0.0);
        if (!safe.safeZoneJobs_[0].active || safe.safeZoneJobs_[0].cellIndex != activatedCell ||
            !safe.enemies_.empty() || safe.lastGameplaySound_ != 4 ||
            safe.previousGameplaySound_ != 11 ||
            adlibEffectPlays(safe) != plays + 2) return fail("safe on/retire cues");

        WordGame cannibal(GraphicsMode::Vga256, 0x7704);
        cannibal.startGame(1);
        disableBoardJobs(cannibal);
        cannibal.enemySlotCount_ = 2;
        for (int slot = 0; slot < 2; ++slot) {
            cannibal.enemySlots_[static_cast<std::size_t>(slot)].type = slot;
            cannibal.enemySlots_[static_cast<std::size_t>(slot)].phase =
                WordGame::EnemySlotPhase::Active;
        }
        WordGame::Enemy survivor;
        survivor.row = 0;
        survivor.column = 0;
        survivor.type = 0;
        survivor.slot = 0;
        survivor.direction = 1;
        survivor.cannibalizing = true;
        survivor.cannibalTimer = 0.0;
        WordGame::Enemy victim = survivor;
        victim.type = 1;
        victim.slot = 1;
        victim.cannibalizing = false;
        victim.overlapFrozen = true;
        victim.overlapRetired = true;
        cannibal.enemies_ = {survivor, victim};
        plays = adlibEffectPlays(cannibal);
        cannibal.updateEnemies(0.0);
        if (cannibal.enemies_.size() != 1 || cannibal.enemies_[0].slot != 0 ||
            cannibal.enemies_[0].direction != 1 ||
            cannibal.enemies_[0].dwellFrame != 15 ||
            cannibal.ordinaryEnemyFrame(cannibal.enemies_[0]) != 15 ||
            cannibal.lastGameplaySound_ != 11 ||
            adlibEffectPlays(cannibal) != plays + 1) return fail("cannibal retire cue");

        WordGame collision(GraphicsMode::Vga256, 0x7705);
        collision.startGame(1);
        disableBoardJobs(collision);
        collision.enemySlotCount_ = 2;
        for (int slot = 0; slot < 2; ++slot) {
            collision.enemySlots_[static_cast<std::size_t>(slot)].type = 4 - slot;
            collision.enemySlots_[static_cast<std::size_t>(slot)].phase =
                WordGame::EnemySlotPhase::Active;
            WordGame::Enemy enemy;
            enemy.row = collision.playerRow_;
            enemy.column = collision.playerColumn_;
            enemy.fromRow = enemy.row;
            enemy.fromColumn = enemy.column;
            enemy.type = 4 - slot;
            enemy.slot = slot;
            enemy.moveTimer = 10.0;
            collision.enemies_.push_back(enemy);
        }
        static constexpr std::array<std::string_view, 5> Exclamations = {
            "Yikes", "Oops", "Aargh", "Oh, Oh", "Rats",
        };
        OriginalRandom phraseProbe = collision.random_;
        (void)phraseProbe.range(90); // Survivor dwell precedes the phrase.
        const std::string expectedPhrase(Exclamations[
            static_cast<std::size_t>(phraseProbe.range(5))]);
        const std::uint64_t collisionCalls = collision.random_.calls;
        plays = adlibEffectPlays(collision);
        collision.beginTroggleCollision(4, 0);
        if (!collision.deathAnimating_ || collision.lastGameplaySound_ != 12 ||
            adlibEffectPlays(collision) != plays + 1) return fail("collision start cue");
        constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;
        for (int index = 0; index < WordGame::TroggleEatAnimationTicks; ++index) {
            collision.updateDeathAnimation(tick + 1e-12);
        }
        if (collision.deathAnimating_ || collision.feedbackMessage_ != expectedPhrase ||
            collision.random_.calls != collisionCalls + 3 ||
            collision.lastGameplaySound_ != 11 ||
            collision.previousGameplaySound_ != 10 || collision.enemies_.size() != 1 ||
            collision.enemies_[0].slot != 0 ||
            collision.enemySlots_[1].phase != WordGame::EnemySlotPhase::Waiting ||
            adlibEffectPlays(collision) != plays + 3) return fail("collision terminal cues");

        const auto prepareTerminalCollision = [&](WordGame& target, const bool demo,
                                                   const int reserves) {
            target.startGame(1);
            disableBoardJobs(target);
            auto& score = const_cast<MuncherScore&>(target.core_.scoreState());
            score.restore(score.score(), reserves);
            target.attractMode_ = demo;
            target.page_ = demo ? WordGamePage::Attract : WordGamePage::Playing;
            target.attractActionTimer_ = 100.0;
            target.enemySlotCount_ = 2;
            for (int slot = 0; slot < 2; ++slot) {
                target.enemySlots_[static_cast<std::size_t>(slot)].type = 4 - slot;
                target.enemySlots_[static_cast<std::size_t>(slot)].phase =
                    WordGame::EnemySlotPhase::Active;
                WordGame::Enemy enemy;
                enemy.row = target.playerRow_;
                enemy.column = target.playerColumn_;
                enemy.fromRow = enemy.row;
                enemy.fromColumn = enemy.column;
                enemy.type = 4 - slot;
                enemy.slot = slot;
                enemy.moveTimer = 10.0;
                target.enemies_.push_back(enemy);
            }
        };
        const auto finishCollision = [](WordGame& target) {
            constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;
            for (int index = 0; index < WordGame::TroggleEatAnimationTicks; ++index) {
                target.updateDeathAnimation(tick + 1e-12);
            }
        };

        WordGame demoCollision(GraphicsMode::Vga256, 0x7709);
        prepareTerminalCollision(demoCollision, true, 3);
        plays = adlibEffectPlays(demoCollision);
        demoCollision.beginTroggleCollision(4, 0);
        finishCollision(demoCollision);
        if (demoCollision.enemies_.size() != 2 ||
            !demoCollision.enemies_[1].collisionHidden ||
            demoCollision.enemySlots_[1].phase != WordGame::EnemySlotPhase::Active ||
            demoCollision.previousGameplaySound_ != 12 ||
            demoCollision.lastGameplaySound_ != 10 ||
            adlibEffectPlays(demoCollision) != plays + 2) {
            return fail("Demo collision terminal boundary");
        }

        WordGame finalCollision(GraphicsMode::Vga256, 0x770a);
        prepareTerminalCollision(finalCollision, false, 0);
        plays = adlibEffectPlays(finalCollision);
        finalCollision.beginTroggleCollision(4, 0);
        finishCollision(finalCollision);
        if (!finalCollision.lastResolution_.gameOver ||
            finalCollision.core_.scoreState().reserves() != -1 ||
            finalCollision.enemies_.size() != 2 ||
            !finalCollision.enemies_[1].collisionHidden ||
            finalCollision.enemySlots_[1].phase != WordGame::EnemySlotPhase::Active ||
            finalCollision.previousGameplaySound_ != 12 ||
            finalCollision.lastGameplaySound_ != 10 ||
            adlibEffectPlays(finalCollision) != plays + 2) {
            return fail("final-life collision terminal boundary");
        }

        WordGame interstitial(GraphicsMode::Vga256, 0x770b);
        interstitial.soundOn_ = true;
        interstitial.musicOn_ = false;
        interstitial.speakerEffects_ = false;
        interstitial.attractMode_ = true;
        interstitial.page_ = WordGamePage::Hall;
        interstitial.attractTransitionTimer_ = 0.0;
        plays = adlibEffectPlays(interstitial);
        interstitial.beginAttractInterstitialTransition(
            WordGame::AttractInterstitialTransition::HallToLogo);
        interstitial.updateAttractInterstitialTransition(1.0);
        if (interstitial.page_ != WordGamePage::AttractLogo ||
            interstitial.previousGameplaySound_ != 12 ||
            interstitial.lastGameplaySound_ != 13 ||
            adlibEffectPlays(interstitial) != plays + 2) {
            return fail("Hall-to-logo cue pair");
        }

        WordGame complete(GraphicsMode::Vga256, 0x7706);
        complete.startGame(1);
        disableBoardJobs(complete);
        plays = adlibEffectPlays(complete);
        complete.handleCompletedBoard();
        if (complete.lastGameplaySound_ != 15 || complete.previousGameplaySound_ != -1 ||
            adlibEffectPlays(complete) != plays + 1) {
            std::cerr << "word gameplay-audio complete last=" << complete.lastGameplaySound_
                      << " previous=" << complete.previousGameplaySound_ << " plays="
                      << adlibEffectPlays(complete) << " before=" << plays << "\n";
            return fail("complete/respawn cues");
        }

        WordGame persistent(GraphicsMode::Vga256, 0x7708);
        persistent.startGame(1);
        persistent.playGameplaySound(7);
        if (!persistent.attractOplPlayer_.playing() ||
            persistent.attractOplPlayer_.looping() ||
            !persistent.attractOplPlayer_.effectPlaying() ||
            persistent.sceneEffectPlayer_.playing()) return fail("persistent AdLib start");
        std::vector<std::int16_t> persistentSamples =
            persistent.attractOplPlayer_.renderTestSamples(49'715);
        persistent.playGameplaySound(8);
        const std::vector<std::int16_t> persistentSecond =
            persistent.attractOplPlayer_.renderTestSamples(49'715);
        persistentSamples.insert(persistentSamples.end(),
                                 persistentSecond.begin(), persistentSecond.end());
        std::uint64_t persistentHash = 1469598103934665603ull;
        bool persistentNonzero = false;
        for (const std::int16_t sample : persistentSamples) {
            persistentNonzero = persistentNonzero || sample != 0;
            const std::uint16_t bits = static_cast<std::uint16_t>(sample);
            persistentHash ^= static_cast<std::uint8_t>(bits);
            persistentHash *= 1099511628211ull;
            persistentHash ^= static_cast<std::uint8_t>(bits >> 8);
            persistentHash *= 1099511628211ull;
        }
        constexpr std::uint64_t ExpectedWordPersistentCueHash = 0x5fa8ed9d1584b20aull;
        if (!persistentNonzero || persistentHash != ExpectedWordPersistentCueHash) {
            std::cerr << "word persistent correct/wrong cue hash=0x" << std::hex
                      << persistentHash << std::dec << '\n';
            return fail("persistent AdLib sample history");
        }
        persistent.toggleSound();
        if (persistent.soundOn_ || !persistent.attractOplPlayer_.playing() ||
            persistent.attractOplPlayer_.looping() ||
            !persistent.attractOplPlayer_.effectPlaying()) {
            return fail("persistent AdLib sound-off");
        }

        game.speakerEffects_ = true;
        plays = game.sceneEffectPlayer_.playCount();
        game.playGameplaySound(7);
        if (game.lastGameplaySound_ != 7 || game.sceneEffectPlayer_.playCount() != plays + 1 ||
            game.sceneEffectPlayer_.bufferSize() <= 44) return fail("speaker cue");
        game.soundOn_ = false;
        plays = game.sceneEffectPlayer_.playCount();
        game.playGameplaySound(8);
        return game.lastGameplaySound_ == 7 &&
               game.sceneEffectPlayer_.playCount() == plays;
    }

    static bool toggleFeedbackRoutes() {
        const auto fail = [](const char* stage) {
            std::cerr << "word toggle-feedback stage: " << stage << '\n';
            return false;
        };
        const auto prepare = [](WordGame& game) {
            game.settingsPersistenceEnabled_ = false;
            game.page_ = WordGamePage::Playing;
            game.attractMode_ = false;
            game.soundOn_ = true;
            game.musicOn_ = false;
            game.speakerEffects_ = false;
        };
        const auto expectedOpl = [](const WordGame& game, const std::uint8_t stream,
                                    const std::uint32_t delay) {
            const BlobView bank = game.assets_.gameArchive().find("GSND", 2);
            MeccSound sound = decodeMeccGSound(
                bank, stream, MeccSoundProfile::WordMunchers);
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

        WordGame soundOff(GraphicsMode::Vga256, 0x7710);
        prepare(soundOff);
        soundOff.toggleSound();
        const bool downSubmitted = soundOff.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> down =
            soundOff.attractOplPlayer_.renderTestSamples(49'715);
        const std::vector<std::int16_t> expectedDown = expectedOpl(soundOff, 1, 0);
        if (soundOff.soundOn_ || !downSubmitted ||
            soundOff.attractOplPlayer_.effectPlayCount() != 1 ||
            down != expectedDown) {
            std::cerr << "word down flags=" << soundOff.soundOn_ << ','
                      << soundOff.attractOplPlayer_.effectPlaying() << ','
                      << soundOff.attractOplPlayer_.effectPlayCount()
                      << " samples=" << down.size() << ',' << expectedDown.size()
                      << " equal=" << (down == expectedDown) << '\n';
            return fail("Alt+S off OPL stream 1");
        }

        WordGame soundOn(GraphicsMode::Vga256, 0x7711);
        prepare(soundOn);
        soundOn.soundOn_ = false;
        soundOn.toggleSound();
        const bool upSubmitted = soundOn.attractOplPlayer_.effectPlaying();
        const std::vector<std::int16_t> up =
            soundOn.attractOplPlayer_.renderTestSamples(49'715);
        if (!soundOn.soundOn_ || !upSubmitted ||
            soundOn.attractOplPlayer_.effectPlayCount() != 1 ||
            up != expectedOpl(soundOn, 2, 0) || up == down) {
            return fail("Alt+S on OPL stream 2");
        }

        WordGame musicOff(GraphicsMode::Vga256, 0x7712);
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
            std::cerr << "word delayed first audible="
                      << std::distance(delayed.begin(), firstAudible) << '\n';
            return fail("Alt+M off delayed OPL stream 1");
        }

        WordGame musicOn(GraphicsMode::Vga256, 0x7713);
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

        const BlobView bank = soundOff.assets_.gameArchive().find("GSND", 2);
        const std::vector<std::uint8_t> speakerDown = renderMeccSpeakerSoundToWave(
            bank, 1, 40, MeccSoundProfile::WordMunchers);
        const std::vector<std::uint8_t> speakerUp = renderMeccSpeakerSoundToWave(
            bank, 2, 40, MeccSoundProfile::WordMunchers);
        const std::vector<std::uint8_t> speakerDelayed = renderMeccSpeakerSoundToWave(
            bank, 1, 40, MeccSoundProfile::WordMunchers, 50);
        WordGame speakerSoundOff(GraphicsMode::Vga256, 0x7714);
        prepare(speakerSoundOff);
        speakerSoundOff.speakerEffects_ = true;
        speakerSoundOff.toggleSound();
        if (!speakerSoundOff.sceneEffectPlayer_.playing() ||
            speakerSoundOff.sceneEffectPlayer_.bufferHash() !=
                waveHash(speakerDown)) {
            return fail("Alt+S off speaker stream 1");
        }
        WordGame speakerSoundOn(GraphicsMode::Vga256, 0x7715);
        prepare(speakerSoundOn);
        speakerSoundOn.speakerEffects_ = true;
        speakerSoundOn.soundOn_ = false;
        speakerSoundOn.toggleSound();
        if (!speakerSoundOn.sceneEffectPlayer_.playing() ||
            speakerSoundOn.sceneEffectPlayer_.bufferHash() !=
                waveHash(speakerUp)) {
            return fail("Alt+S on speaker stream 2");
        }
        WordGame speakerMusicOff(GraphicsMode::Vga256, 0x7716);
        prepare(speakerMusicOff);
        speakerMusicOff.speakerEffects_ = true;
        speakerMusicOff.musicOn_ = true;
        speakerMusicOff.toggleMusic();
        if (!speakerMusicOff.sceneEffectPlayer_.playing() ||
            speakerMusicOff.sceneEffectPlayer_.bufferHash() !=
                waveHash(speakerDelayed) ||
            speakerDelayed.size() != speakerDown.size() + 4'410) {
            std::cerr << "word speaker delay sizes=" << speakerDown.size()
                      << ',' << speakerDelayed.size() << '\n';
            return fail("Alt+M off delayed speaker stream 1");
        }
        return true;
    }

    static bool attractDemoRoutes() {
        const auto fail = [](const char* stage) {
            std::cerr << "word attract stage: " << stage << "\n";
            return false;
        };
        constexpr double tick = 1.0 / WordSchedulerTicksPerSecond;

        WordGame idle(GraphicsMode::Vga256, 0x5a17);
        advanceStartup(idle);
        for (int frame = 0; frame < 299; ++frame) idle.update(0.1);
        if (idle.page_ != WordGamePage::Title || idle.attractMode_) return fail("early idle");
        idle.update(0.1);
        if (idle.page_ == WordGamePage::Title) idle.update(0.0001);
        if (!idle.attractMode_ || idle.page_ != WordGamePage::Attract || !idle.core_.active() ||
            idle.core_.level() < 1 || idle.core_.level() > 9 ||
            std::abs(idle.attractActionTimer_ / tick - 30.0) > 1e-6 ||
            !idle.attractOplPlayer_.playing() || !idle.attractOplPlayer_.looping() ||
            idle.attractOplPlayer_.loopPlayCount() != 1 ||
            idle.attractOplPlayer_.scoreWriteCount() < 1'000 ||
            idle.sceneEffectPlayer_.playing()) {
            std::cerr << "word attract idle page=" << static_cast<int>(idle.page_)
                      << " mode=" << idle.attractMode_ << " active=" << idle.core_.active()
                      << " level=" << idle.core_.level() << " actionTicks="
                      << idle.attractActionTimer_ / tick << " musicPlaying="
                      << idle.attractOplPlayer_.playing() << " looping="
                      << idle.attractOplPlayer_.looping() << " plays="
                      << idle.attractOplPlayer_.loopPlayCount() << " writes="
                      << idle.attractOplPlayer_.scoreWriteCount() << "\n";
            return fail("idle entry/music");
        }
        const BlobView bank = idle.assets_.gameArchive().find("GSND", 2);
        const MeccSound score = decodeMeccGSound(
            bank, 16, MeccSoundProfile::WordMunchers);
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
        const std::uint64_t scoreHash = oplScheduleHash(score);
        const std::size_t secondSectionWrites = static_cast<std::size_t>(std::count_if(
            score.writes.begin(), score.writes.end(), [](const OplWrite& write) {
                return write.milliseconds == 112'459;
            }));
        constexpr std::size_t ExpectedScoreWrites = 13'400;
        constexpr std::uint64_t ExpectedScoreHash = 0x152a3244c88a359bull;
        if (!score.valid || score.durationMilliseconds != 224'919 ||
            score.writes.size() != ExpectedScoreWrites ||
            scoreHash != ExpectedScoreHash || secondSectionWrites < 40) {
            std::cerr << "word score duration=" << score.durationMilliseconds
                      << " writes=" << score.writes.size() << " hash=0x" << std::hex
                      << scoreHash << std::dec << " section2=" << secondSectionWrites << '\n';
            return fail("score decode");
        }

        // Word's eight score tracks and an ordinary channel-0 cue must be
        // generated by one continuously clocked chip. This locks the actual
        // signed PCM samples, not merely the two logical ownership flags.
        const std::size_t effectStarts = idle.attractOplPlayer_.effectPlayCount();
        idle.playGameplaySound(7);
        if (!idle.attractOplPlayer_.effectPlaying() ||
            idle.attractOplPlayer_.effectPlayCount() != effectStarts + 1 ||
            idle.sceneEffectPlayer_.playing()) return fail("single-chip cue routing");
        const std::vector<std::int16_t> compositeSamples =
            idle.attractOplPlayer_.renderTestSamples(49'715);
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
        constexpr std::uint64_t ExpectedWordSingleChipSecondHash = 0xfc21539d1eb4c238ull;
        if (!compositeNonzero || compositeHash != ExpectedWordSingleChipSecondHash) {
            std::cerr << "word single-chip score/effect sample hash=0x" << std::hex
                      << compositeHash << std::dec << '\n';
            return fail("single-chip sample output");
        }

        Renderer renderer;
        idle.render(renderer);
        int leftHeaderInk = 0;
        int demoHeaderInk = 0;
        int footerInk = 0;
        for (int y = 4; y < 15; ++y) {
            for (int x = 0; x < 30; ++x) {
                if (renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)] !=
                    Colors::BoardBlue) ++leftHeaderInk;
            }
            for (int x = 30; x < 75; ++x) {
                if (renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)] !=
                    Colors::BoardBlue) ++demoHeaderInk;
            }
        }
        for (int y = 184; y < 198; ++y) {
            for (int x = 0; x < Renderer::Width; ++x) {
                if (renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)] !=
                    Colors::BoardBlue) ++footerInk;
            }
        }
        // The lossless wm_009 Demo board proves that the Demo branch omits
        // Score/reserves but replaces them with the centered Muncher Menu
        // prompt; this footer is not an empty blue band.
        if (leftHeaderInk != 0 || demoHeaderInk == 0 || footerInk == 0) {
            return fail("Demo HUD");
        }

        // The pinned 0x8E74 DOS memory trace proves the first later mixed-job
        // boundary: the blue Troggle owns call 360, then selector 5 owns calls
        // 361/362 and starts a downward move through an already-eaten cell.
        // This catches an entry job held for one synthetic endpoint interval:
        // that bug let selector 5 steal call 360 and changed the whole Demo.
        WordGame seededScheduler(GraphicsMode::Vga256, 0x8e74);
        seededScheduler.settingsPersistenceEnabled_ = false;
        seededScheduler.startAttract();
        bool sawEnemyTerminalCall = false;
        for (int publicTick = 0;
             publicTick < 5'000 && seededScheduler.random_.calls < 362;
             ++publicTick) {
            seededScheduler.update(tick);
            if (seededScheduler.random_.calls == 360) {
                sawEnemyTerminalCall = true;
                if (seededScheduler.random_.state != 0x4da2324cu ||
                    seededScheduler.moving_ || seededScheduler.playerRow_ != 0 ||
                    seededScheduler.playerColumn_ != 0) {
                    return fail("seeded selector-1 terminal call");
                }
            }
        }
        if (!sawEnemyTerminalCall || seededScheduler.random_.calls != 362 ||
            seededScheduler.random_.state != 0x90e2c222u ||
            !seededScheduler.moving_ || seededScheduler.moveDirection_ != 2 ||
            seededScheduler.moveFromRow_ != 0 || seededScheduler.moveToRow_ != 1 ||
            seededScheduler.moveFromColumn_ != 0 || seededScheduler.moveToColumn_ != 0) {
            return fail("seeded selector-5 call order");
        }

        WordGame controller(GraphicsMode::Vga256, 0x5a18);
        controller.startAttract();
        disableBoardJobs(controller);
        const int current = controller.playerRow_ * WordGame::BoardColumns +
                            controller.playerColumn_;
        auto findCell = [&](const bool correct) -> WordBoardCell {
            for (const WordBoardCell& cell : controller.core_.board().cells) {
                if (cell.signedSourceIndex != 0 && cell.correct == correct) return cell;
            }
            return {};
        };
        const WordBoardCell correctCell = findCell(true);
        const WordBoardCell wrongCell = findCell(false);
        if (correctCell.signedSourceIndex <= 0 || wrongCell.signedSourceIndex >= 0) {
            return fail("cell fixtures");
        }
        (void)controller.core_.restoreCell(static_cast<std::size_t>(current), correctCell, false);
        controller.random_.seed(0x1111);
        controller.attractActionTimer_ = 0.0;
        const std::uint64_t beforeCorrect = controller.random_.calls;
        controller.updateAttractPlayer();
        const double reloadTicks = controller.attractActionTimer_ / tick;
        if (!controller.munching_ || controller.random_.calls != beforeCorrect + 1 ||
            reloadTicks < 14.999 || reloadTicks > 44.001) return fail("correct current cell");

        controller.resolveMunch();
        (void)controller.core_.restoreCell(static_cast<std::size_t>(current), wrongCell, false);
        std::uint16_t wrongSeed = 1;
        for (; wrongSeed != 0; ++wrongSeed) {
            OriginalRandom probe(wrongSeed);
            (void)probe.range(30);
            if (probe.range(10) == 0) break;
        }
        if (wrongSeed == 0) return fail("wrong seed");
        controller.random_.seed(wrongSeed);
        controller.attractActionTimer_ = 0.0;
        const std::uint64_t beforeWrong = controller.random_.calls;
        controller.updateAttractPlayer();
        if (!controller.munching_ || controller.random_.calls != beforeWrong + 2) {
            return fail("one-in-ten wrong cell");
        }

        controller.resolveMunch();
        controller.page_ = WordGamePage::Attract;
        controller.attractMode_ = true;
        controller.moving_ = true;
        (void)controller.core_.restoreCell(static_cast<std::size_t>(current), correctCell, false);
        controller.random_.seed(0x2222);
        controller.attractActionTimer_ = 0.0;
        const std::uint64_t beforeBusy = controller.random_.calls;
        controller.updateAttractPlayer();
        if (!controller.moving_ || controller.munching_ ||
            controller.random_.calls != beforeBusy + 1) return fail("busy callback");

        controller.moving_ = false;
        controller.playerRow_ = 2;
        controller.playerColumn_ = 2;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            (void)controller.core_.clearCellWithoutScore(static_cast<std::size_t>(index));
        }
        const int right = controller.playerRow_ * WordGame::BoardColumns +
                          controller.playerColumn_ + 1;
        (void)controller.core_.restoreCell(static_cast<std::size_t>(right), correctCell, false);
        std::uint16_t upSeed = 1;
        for (; upSeed != 0; ++upSeed) {
            OriginalRandom probe(upSeed);
            (void)probe.range(30);
            if (probe.range(4) == 0) break;
        }
        if (upSeed == 0) return fail("direction seed");
        controller.random_.seed(upSeed);
        controller.attractActionTimer_ = 0.0;
        const std::uint64_t beforeSeek = controller.random_.calls;
        controller.updateAttractPlayer();
        if (!controller.moving_ || controller.moveDirection_ != 1 ||
            controller.random_.calls != beforeSeek + 2) return fail("clockwise seek");

        WordGame bite(GraphicsMode::Vga256, 0x5a1b);
        bite.startAttract();
        disableBoardJobs(bite);
        bite.enemySlotCount_ = 1;
        bite.enemySlots_[0].type = 4;
        bite.enemySlots_[0].phase = WordGame::EnemySlotPhase::Active;
        WordGame::Enemy biter;
        biter.row = bite.playerRow_;
        biter.column = bite.playerColumn_;
        biter.fromRow = biter.row;
        biter.fromColumn = biter.column;
        biter.type = 4;
        biter.slot = 0;
        biter.moveTimer = 10.0;
        bite.enemies_.push_back(biter);
        const int reservesBeforeBite = bite.core_.scoreState().reserves();
        bite.beginTroggleCollision(4, 0);
        for (int index = 0; index < WordGame::TroggleEatAnimationTicks; ++index) {
            bite.updateDeathAnimation(tick + 1e-12);
        }
        if (bite.deathAnimating_ || bite.page_ != WordGamePage::Feedback ||
            !bite.core_.active() ||
            bite.core_.scoreState().reserves() != reservesBeforeBite - 1 ||
            std::abs(bite.attractTransitionTimer_ / tick - 150.0) > 1e-6 ||
            bite.feedbackMessage_.empty()) return fail("first-bite terminal route");

        // The shared closing Wipe retains one collision sample before the
        // departed biter disappears from the restored board. It is not the
        // former fabricated 20-tick wait.
        constexpr double captureFrame = 31250.0 / 2190197.0;
        bite.attractTransitionTimer_ = 0.0;
        bite.update(0.0);
        if (bite.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::CollisionTransient ||
            bite.attractHallWipeVariant_ != WordGame::AttractHallWipeVariant::Collision ||
            std::abs(bite.attractTransitionTimer_ - captureFrame) > 1e-9) {
            return fail("collision transient phase");
        }
        bite.render(renderer);
        const std::uint64_t collisionTransientHash = fullFrameHash(renderer);
        bite.attractTransitionTimer_ = 0.0;
        bite.update(0.0);
        if (bite.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::BoardHold ||
            std::abs(bite.attractTransitionTimer_ / captureFrame - 39.0) > 1e-6) {
            return fail("collision restored-board phase");
        }
        bite.render(renderer);
        const std::uint64_t collisionBoardHash = fullFrameHash(renderer);
        if (collisionBoardHash == collisionTransientHash) {
            return fail("collision actor retirement");
        }
        bite.attractTransitionTimer_ = 0.0;
        bite.update(0.0);
        std::array<std::uint64_t, 6> collisionWipeHashes{};
        for (int frame = 0; frame < static_cast<int>(collisionWipeHashes.size()); ++frame) {
            if (bite.attractHallTransitionPhase_ !=
                    WordGame::AttractHallTransitionPhase::Wipe ||
                bite.attractHallWipeFrame_ != frame) {
                return fail("collision Wipe phase");
            }
            bite.render(renderer);
            if (frame == 4 &&
                !std::all_of(renderer.pixels().begin(), renderer.pixels().end(),
                             [](const std::uint32_t pixel) {
                                 return pixel == Colors::BoardBlue;
                             })) {
                return fail("collision Wipe navy presentation");
            }
            collisionWipeHashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            bite.attractTransitionTimer_ = 0.0;
            bite.update(0.0);
        }
        if (bite.page_ != WordGamePage::Hall ||
            bite.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::None) {
            return fail("collision Wipe terminal");
        }
        // The transient retains the type-4 survivor in its terminal dwell
        // record. Gameplay maps all five shared Troggle highlight slots
        // through the recovered resident DAC; the restored board and every
        // Wipe frame contain no such actor.
        constexpr std::uint64_t ExpectedCollisionTransientHash = 0x8194c5b193d433e6ull;
        constexpr std::uint64_t ExpectedCollisionBoardHash = 0x252a190addd3b886ull;
        constexpr std::array<std::uint64_t, 6> ExpectedCollisionWipeHashes = {
            0xa21a6f2b9a056312ull, 0x2cc687e766578183ull,
            0x8a6d8c6b73f1ac1bull, 0xba04247ef217e4adull,
            0xb1a8f948642db583ull, 0xa10344d988a0cd65ull,
        };
        if (collisionTransientHash != ExpectedCollisionTransientHash ||
            collisionBoardHash != ExpectedCollisionBoardHash ||
            collisionWipeHashes != ExpectedCollisionWipeHashes) {
            std::cerr << "word collision transient=0x" << std::hex
                      << collisionTransientHash << " board=0x" << collisionBoardHash
                      << " Wipe:";
            for (const std::uint64_t value : collisionWipeHashes) {
                std::cerr << " 0x" << value;
            }
            std::cerr << std::dec << '\n';
            return fail("collision Wipe hashes");
        }

        // A clean transition can follow either a wrong answer or a correct
        // board completion.  Only the wrong-answer route retains open-mouth
        // record 12; source run 595 holds the correct route's closed terminal
        // record 13 for all 40 pre-Wipe samples.
        WordGame correctHold(GraphicsMode::Vga256, 0x5a1c);
        correctHold.startAttract();
        disableBoardJobs(correctHold);
        correctHold.page_ = WordGamePage::Feedback;
        correctHold.attractPostFeedbackBoard_ = true;
        correctHold.attractHallWipeVariant_ =
            WordGame::AttractHallWipeVariant::Clean;
        correctHold.playerTerminalFrame_ = 13;
        correctHold.lastResolution_.kind = WordMunchKind::Correct;
        correctHold.render(renderer);
        const std::vector<std::uint32_t> correctHoldPixels = renderer.pixels();
        correctHold.playerTerminalFrame_ = 7;
        correctHold.render(renderer);
        if (renderer.pixels() != correctHoldPixels) {
            return fail("correct clean hold terminal record");
        }
        correctHold.playerTerminalFrame_ = 13;
        correctHold.lastResolution_.kind = WordMunchKind::Wrong;
        correctHold.render(renderer);
        int wrongHoldDifferences = 0;
        for (std::size_t pixel = 0; pixel < renderer.pixels().size(); ++pixel) {
            wrongHoldDifferences += renderer.pixels()[pixel] != correctHoldPixels[pixel];
        }
        if (wrongHoldDifferences != 240) {
            return fail("wrong clean hold open-mouth record");
        }

        WordGame terminal(GraphicsMode::Vga256, 0x5a19);
        terminal.startAttract();
        disableBoardJobs(terminal);
        int wrongIndex = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!terminal.core_.eaten(static_cast<std::size_t>(index)) &&
                terminal.core_.board().cells[static_cast<std::size_t>(index)].signedSourceIndex < 0) {
                wrongIndex = index;
                break;
            }
        }
        if (wrongIndex < 0) return fail("terminal wrong cell");
        terminal.playerRow_ = wrongIndex / WordGame::BoardColumns;
        terminal.playerColumn_ = wrongIndex % WordGame::BoardColumns;
        terminal.beginMunch();
        terminal.resolveMunch();
        if (terminal.page_ != WordGamePage::Feedback || terminal.deathAnimating_ ||
            terminal.attractPostFeedbackBoard_ || terminal.lastResolution_.gameOver ||
            std::abs(terminal.attractTransitionTimer_ / tick - 150.0) > 1e-6) {
            return fail("terminal feedback hold");
        }
        terminal.render(renderer);
        for (int y = 111; y <= 115; ++y) {
            for (int x = WordGame::BoardLeft + 1; x < WordGame::BoardRight; ++x) {
                if (renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)] !=
                    Colors::BoardBlue) return fail("Demo feedback footer");
            }
        }
        // Source run 651 holds the final open chew record throughout the
        // wrong-answer explanation.  Compare its player box with an explicit
        // record-12 render while keeping the overlay outside this row.
        terminal.playerRow_ = 0;
        terminal.playerColumn_ = 0;
        terminal.render(renderer);
        const std::vector<std::uint32_t> wrongFeedbackPixels = renderer.pixels();
        terminal.lastResolution_.kind = WordMunchKind::Correct;
        terminal.playerTerminalFrame_ = 12;
        terminal.render(renderer);
        for (int y = WordGame::BoardTop + 1; y < WordGame::BoardTop + 30; ++y) {
            for (int x = WordGame::BoardLeft + 4; x < WordGame::BoardLeft + 45; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(
                    y * Renderer::Width + x);
                if (wrongFeedbackPixels[pixel] != renderer.pixels()[pixel]) {
                    return fail("wrong feedback retained chew record");
                }
            }
        }
        terminal.lastResolution_.kind = WordMunchKind::Wrong;
        terminal.playerTerminalFrame_ = -1;
        terminal.playerRow_ = wrongIndex / WordGame::BoardColumns;
        terminal.playerColumn_ = wrongIndex % WordGame::BoardColumns;
        terminal.attractTransitionTimer_ = 0.0;
        terminal.update(0.0);
        if (!terminal.attractPostFeedbackBoard_ || terminal.page_ != WordGamePage::Feedback ||
            terminal.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::BoardHold) {
            return fail("post-feedback board phase");
        }
        if (std::abs(terminal.attractTransitionTimer_ / captureFrame - 39.0) > 1e-6) {
            return fail("post-feedback board duration");
        }
        terminal.render(renderer);
        const std::uint64_t cleanBoardHoldHash = fullFrameHash(renderer);

        terminal.attractTransitionTimer_ = 0.0;
        terminal.update(0.0);
        std::array<std::uint64_t, 7> cleanWipeHashes{};
        for (int frame = 0; frame < static_cast<int>(cleanWipeHashes.size()); ++frame) {
            if (terminal.attractHallTransitionPhase_ !=
                    WordGame::AttractHallTransitionPhase::Wipe ||
                terminal.attractHallWipeFrame_ != frame) {
                return fail("clean terminal Wipe phase");
            }
            terminal.render(renderer);
            if ((frame == 4 || frame == 5) &&
                !std::all_of(renderer.pixels().begin(), renderer.pixels().end(),
                             [](const std::uint32_t pixel) {
                                 return pixel == Colors::BoardBlue;
                             })) {
                return fail("clean Wipe navy presentation");
            }
            cleanWipeHashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            terminal.attractTransitionTimer_ = 0.0;
            terminal.update(0.0);
        }
        if (terminal.page_ != WordGamePage::Hall || terminal.postGameHall_ ||
            terminal.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::None ||
            std::abs(terminal.attractTransitionTimer_ / tick - 450.0) > 1e-6) {
            return fail("attract Hall");
        }

        terminal.attractTransitionTimer_ = 0.0;
        terminal.update(0.0);
        std::array<std::uint64_t, 6> hallToLogoHashes{};
        for (int frame = 0; frame < static_cast<int>(hallToLogoHashes.size()); ++frame) {
            if (terminal.attractInterstitialTransition_ !=
                    WordGame::AttractInterstitialTransition::HallToLogo ||
                terminal.attractInterstitialFrame_ != frame) {
                return fail("Hall-to-logo Wipe phase");
            }
            terminal.render(renderer);
            hallToLogoHashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            terminal.attractTransitionTimer_ = 0.0;
            terminal.update(0.0);
        }
        if (terminal.page_ != WordGamePage::AttractLogo ||
            terminal.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::None ||
            std::abs(terminal.attractTransitionTimer_ / tick - 450.0) > 1e-6) {
            return fail("attract logo");
        }
        terminal.render(renderer);
        const std::uint32_t logoCenter = renderer.pixels()[100u * Renderer::Width + 160u];
        if (logoCenter == Colors::Black || logoCenter == Colors::BoardBlue) {
            return fail("logo paint");
        }

        terminal.attractTransitionTimer_ = 0.0;
        terminal.update(0.0);
        std::array<std::uint64_t, 9> logoToBoardHashes{};
        for (int frame = 0; frame < static_cast<int>(logoToBoardHashes.size()); ++frame) {
            if (terminal.attractInterstitialTransition_ !=
                    WordGame::AttractInterstitialTransition::LogoToBoard ||
                terminal.attractInterstitialFrame_ != frame) {
                return fail("logo-to-board Wipe phase");
            }
            terminal.render(renderer);
            logoToBoardHashes[static_cast<std::size_t>(frame)] = fullFrameHash(renderer);
            terminal.attractTransitionTimer_ = 0.0;
            terminal.update(0.0);
        }
        if (terminal.page_ != WordGamePage::Attract || !terminal.attractMode_ ||
            terminal.attractInterstitialTransition_ !=
                WordGame::AttractInterstitialTransition::None ||
            std::abs(terminal.attractActionTimer_ / tick - 30.0) > 1e-6 ||
            !terminal.attractOplPlayer_.looping() ||
            terminal.attractOplPlayer_.effectPlaying()) {
            return fail("next demo board");
        }

        constexpr std::uint64_t ExpectedCleanBoardHoldHash = 0x393c8a391146b029ull;
        constexpr std::array<std::uint64_t, 7> ExpectedCleanWipeHashes = {
            0xd81d8bb53bef919full, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x413bd71b36091f2bull,
            0xb1a8f948642db583ull, 0xb1a8f948642db583ull,
            0xa10344d988a0cd65ull,
        };
        constexpr std::array<std::uint64_t, 6> ExpectedHallToLogoHashes = {
            0x45a265abe211217eull, 0x2cc687e766578183ull,
            0x3f124d04d181aa5eull, 0xf79c067ba1bc34f6ull,
            0xf75309bcac42bb83ull, 0xf75309bcac42bb83ull,
        };
        constexpr std::array<std::uint64_t, 9> ExpectedLogoToBoardHashes = {
            0x17a36c8fbd9b2169ull, 0x2cc687e766578183ull,
            0x2cc687e766578183ull, 0x8887db592e3c9ca1ull,
            0xb1a8f948642db583ull, 0xb1a8f948642db583ull,
            0xb1a8f948642db583ull, 0x0ea080bbaf76a84full,
            0xaf94baed0b7650aeull,
        };
        if (cleanBoardHoldHash != ExpectedCleanBoardHoldHash ||
            cleanWipeHashes != ExpectedCleanWipeHashes ||
            hallToLogoHashes != ExpectedHallToLogoHashes ||
            logoToBoardHashes != ExpectedLogoToBoardHashes) {
            const auto dump = [](const auto& values) {
                for (const std::uint64_t value : values) {
                    std::cerr << " 0x" << std::hex << value;
                }
                std::cerr << std::dec << '\n';
            };
            std::cerr << "word attract clean board=0x" << std::hex
                      << cleanBoardHoldHash << std::dec << "\nclean Wipe:";
            dump(cleanWipeHashes);
            std::cerr << "Hall-to-logo:";
            dump(hallToLogoHashes);
            std::cerr << "logo-to-board:";
            dump(logoToBoardHashes);
            return fail("Wipe frame hashes");
        }
        const std::size_t musicPlays = terminal.attractOplPlayer_.loopPlayCount();
        const std::size_t effectPlaysBeforeToggle = terminal.attractOplPlayer_.effectPlayCount();
        terminal.playGameplaySound(7);
        if (!terminal.attractOplPlayer_.effectPlaying() ||
            terminal.attractOplPlayer_.effectPlayCount() != effectPlaysBeforeToggle + 1 ||
            terminal.sceneEffectPlayer_.playing()) {
            return fail("music-toggle effect setup");
        }
        terminal.toggleMusic();
        if (!terminal.attractOplPlayer_.playing() ||
            terminal.attractOplPlayer_.looping() ||
            !terminal.attractOplPlayer_.effectPlaying() ||
            terminal.attractOplPlayer_.effectPlayCount() != effectPlaysBeforeToggle + 2 ||
            terminal.sceneEffectPlayer_.playing()) {
            return fail("music off delayed acknowledgement");
        }
        terminal.toggleMusic();
        if (!terminal.attractOplPlayer_.looping() ||
            !terminal.attractOplPlayer_.effectPlaying() ||
            terminal.attractOplPlayer_.effectPlayCount() != effectPlaysBeforeToggle + 3 ||
            terminal.attractOplPlayer_.loopPlayCount() != musicPlays + 1) {
            return fail("music resume acknowledgement");
        }

        // Alt+S replaces channel 0 with its acknowledgement while retaining
        // the score. Alt+P is asymmetric: selecting speaker with music enabled
        // issues all-channel stop; restoring AdLib in state 2 restarts score.
        terminal.playGameplaySound(7);
        terminal.toggleSound();
        if (terminal.soundOn_ || !terminal.attractOplPlayer_.effectPlaying() ||
            !terminal.attractOplPlayer_.looping()) return fail("sound off channel-0 release");
        terminal.toggleSound();
        terminal.playGameplaySound(7);
        (void)terminal.attractOplPlayer_.renderTestSamples(1);
        terminal.toggleSpeaker();
        (void)terminal.attractOplPlayer_.renderTestSamples(1);
        constexpr std::array<std::uint8_t, 9> OperatorOffsets = {
            0, 1, 2, 8, 9, 10, 16, 17, 18
        };
        bool channelsRetired = true;
        for (std::uint8_t channel = 0; channel < OperatorOffsets.size(); ++channel) {
            const std::uint8_t op = OperatorOffsets[channel];
            channelsRetired = channelsRetired &&
                terminal.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0xc0 + channel)) == 0x00 &&
                terminal.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0x43 + op)) == 0x3f &&
                terminal.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0x83 + op)) == 0xff &&
                terminal.attractOplPlayer_.registerValueForTest(
                    static_cast<std::uint8_t>(0xb0 + channel)) == 0x00;
        }
        if (!terminal.speakerEffects_ || !terminal.attractOplPlayer_.playing() ||
            terminal.attractOplPlayer_.looping() ||
            terminal.attractOplPlayer_.effectPlaying() ||
            terminal.attractOplPlayer_.scoreWriteCount() != 0 || !channelsRetired ||
            terminal.attractOplPlayer_.registerValueForTest(0xbd) != 0xc0) {
            std::cerr << "word stop flags speaker=" << terminal.speakerEffects_
                      << " playing=" << terminal.attractOplPlayer_.playing()
                      << " looping=" << terminal.attractOplPlayer_.looping()
                      << " effect=" << terminal.attractOplPlayer_.effectPlaying()
                      << " scoreWrites=" << terminal.attractOplPlayer_.scoreWriteCount()
                      << " retired=" << channelsRetired << " bd=0x" << std::hex
                      << static_cast<int>(terminal.attractOplPlayer_.registerValueForTest(0xbd))
                      << std::dec << '\n';
            return fail("speaker switch all-channel stop");
        }
        terminal.playGameplaySound(7);
        if (!terminal.sceneEffectPlayer_.playing() ||
            !terminal.attractOplPlayer_.playing() ||
            terminal.attractOplPlayer_.looping()) return fail("speaker cue without score");
        const std::size_t scoreRestarts = terminal.attractOplPlayer_.loopPlayCount();
        terminal.toggleSpeaker();
        if (!terminal.sceneEffectPlayer_.playing() || !terminal.attractOplPlayer_.looping() ||
            terminal.attractOplPlayer_.loopPlayCount() != scoreRestarts + 1) {
            return fail("AdLib state-2 score restore");
        }
        terminal.keyDown(VK_SPACE);
        if (!terminal.attractMode_ || terminal.page_ == WordGamePage::Title) {
            return fail("translated any-key key-down deferral");
        }
        terminal.character(L' ');
        if (terminal.attractMode_ || terminal.page_ != WordGamePage::Title ||
            !terminal.attractOplPlayer_.playing() ||
            terminal.attractOplPlayer_.looping() ||
            terminal.attractOplPlayer_.effectPlaying() ||
            terminal.attractOplPlayer_.scoreWriteCount() != 0 ||
            terminal.sceneEffectPlayer_.playing()) {
            return fail("any-key exit");
        }

        WordGame complete(GraphicsMode::Vga256, 0x5a1a);
        complete.startAttract();
        std::vector<std::size_t> completedBoardTargets;
        for (std::size_t index = 0; index < complete.core_.board().cells.size(); ++index) {
            if (complete.core_.board().cells[index].correct) {
                completedBoardTargets.push_back(index);
            }
        }
        if (completedBoardTargets.size() < 2) return fail("completed-board targets");
        for (std::size_t index = 0; index + 1 < completedBoardTargets.size(); ++index) {
            if (complete.core_.munchCell(completedBoardTargets[index]).kind !=
                    WordMunchKind::Correct) {
                return fail("completed-board setup");
            }
        }
        const std::uint64_t callsBeforeCompletion = complete.random_.calls;
        const WordMunchResolution silentCompletion =
            complete.core_.munchCell(completedBoardTargets.back());
        if (!silentCompletion.boardComplete || silentCompletion.cartoonScene != -1 ||
            complete.core_.cartoonPending() ||
            complete.random_.calls != callsBeforeCompletion) {
            return fail("Demo completion PRNG bypass");
        }
        const int lastSound = complete.lastGameplaySound_;
        const std::size_t effectPlays = complete.attractOplPlayer_.effectPlayCount();
        complete.handleCompletedBoard();
        if (complete.page_ != WordGamePage::Feedback ||
            !complete.attractPostFeedbackBoard_ || complete.lastGameplaySound_ != lastSound ||
            complete.attractOplPlayer_.effectPlayCount() != effectPlays ||
            complete.attractHallTransitionPhase_ !=
                WordGame::AttractHallTransitionPhase::BoardHold ||
            std::abs(complete.attractTransitionTimer_ / captureFrame - 39.0) > 1e-6) {
            return fail("silent completed board route");
        }
        complete.pointerButton(0, 0, false);
        if (complete.attractMode_ || complete.page_ != WordGamePage::Title) {
            return fail("pointer transition exit");
        }

        // Every new transition primitive must stay within the three fixed
        // foreground colors plus the page-selected CGA background register.
        WordGame cga(GraphicsMode::Cga4, 0x5a1c);
        cga.startAttract();
        disableBoardJobs(cga);
        cga.feedbackEnemyType_ = -1;
        cga.beginAttractHallTransition();
        const std::set<std::uint32_t> cgaPalette = {
            0x000000u, 0x0000aau, 0x55ffffu, 0xff55ffu, 0xffffffu,
        };
        const auto cgaFrameValid = [&](WordGame& game) {
            Renderer cgaRenderer(GraphicsMode::Cga4);
            game.render(cgaRenderer);
            const std::set<std::uint32_t> colors(
                cgaRenderer.pixels().begin(), cgaRenderer.pixels().end());
            return std::includes(cgaPalette.begin(), cgaPalette.end(),
                                 colors.begin(), colors.end());
        };
        cga.attractHallTransitionPhase_ =
            WordGame::AttractHallTransitionPhase::Wipe;
        for (int frame = 0; frame < 7; ++frame) {
            cga.attractHallWipeFrame_ = frame;
            if (!cgaFrameValid(cga)) return fail("CGA terminal Wipe palette");
        }
        cga.beginAttractHall();
        cga.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::HallToLogo;
        for (int frame = 0; frame < 6; ++frame) {
            cga.attractInterstitialFrame_ = frame;
            if (!cgaFrameValid(cga)) return fail("CGA Hall-to-logo palette");
        }
        cga.page_ = WordGamePage::AttractLogo;
        cga.attractInterstitialTransition_ =
            WordGame::AttractInterstitialTransition::LogoToBoard;
        for (int frame = 0; frame < 9; ++frame) {
            cga.attractInterstitialFrame_ = frame;
            if (frame == 7) cga.startNextAttractBoard();
            if (!cgaFrameValid(cga)) return fail("CGA logo-to-board palette");
        }
        return true;
    }

    static bool wrongFeedbackAndPostGameRoute(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        for (int loss = 0; loss < 3; ++loss) {
            if (game.core_.loseMuncher()) return false;
        }
        int wrong = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!game.core_.eaten(static_cast<std::size_t>(index)) &&
                !game.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                wrong = index;
                break;
            }
        }
        if (wrong < 0) return false;
        game.playerRow_ = wrong / WordGame::BoardColumns;
        game.playerColumn_ = wrong % WordGame::BoardColumns;
        game.beginMunch();
        if (!game.munching_ || !game.core_.eaten(static_cast<std::size_t>(wrong))) return false;
        game.resolveMunch();
        if (game.page_ != WordGamePage::Feedback || !game.lastResolution_.gameOver ||
            game.core_.scoreState().reserves() != -1) return false;

        Renderer renderer;
        game.render(renderer);
        if (renderer.pixels()[116u * Renderer::Width + 30u] != Colors::Magenta) return false;
        const std::uint64_t hash = feedbackStripHash(renderer);
        // Static/native composition hash for the exact one-row Word overlay
        // recovered at image 0x0d2e1; this is not a live-DOS framebuffer hash.
        constexpr std::uint64_t ExpectedWrongFeedbackHash = 0xf8e2b6a72d62d842ull;
        if (hash != ExpectedWrongFeedbackHash) {
            std::cerr << "word wrong-feedback strip hash=0x" << std::hex << hash << std::dec << "\n";
            return false;
        }

        // Complete muted original capture: target sound 2 uses exemplar
        // "tree", and the attempted word is "went". This discriminates the
        // executable's exemplar-only line from the longer board-HUD label and
        // locks the original period-inside-the-closing-quote composition.
        WordGame liveFeedback(GraphicsMode::Vga256, 0x9182);
        liveFeedback.startGame(1);
        disableBoardJobs(liveFeedback);
        auto& liveBoard = const_cast<WordBoard&>(liveFeedback.core_.board());
        liveBoard.targetSound = 2;
        WordRecord attempted{};
        attempted.bytes[0] = 'w';
        attempted.bytes[1] = 'e';
        attempted.bytes[2] = 'n';
        attempted.bytes[3] = 't';
        liveBoard.cells[12].record = attempted;
        liveBoard.cells[12].correct = false;
        liveBoard.cells[12].signedSourceIndex = -1;
        liveFeedback.playerRow_ = 2;
        liveFeedback.playerColumn_ = 0;
        liveFeedback.lastResolution_.kind = WordMunchKind::Wrong;
        liveFeedback.feedbackEnemyType_ = -1;
        liveFeedback.deathAnimating_ = false;
        liveFeedback.page_ = WordGamePage::Feedback;
        liveFeedback.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "wrong-feedback-live-state-native.ppm");
        const std::uint64_t liveFeedbackHash = feedbackStripHash(renderer);
        constexpr std::uint64_t ExpectedLiveFeedbackHash = 0x59cea3cabab4e368ull;
        if (liveFeedbackHash != ExpectedLiveFeedbackHash) {
            std::cerr << "word live wrong-feedback strip hash=0x" << std::hex
                      << liveFeedbackHash << std::dec << "\n";
            return false;
        }

        game.keyDown(VK_RIGHT);
        if (game.page_ != WordGamePage::Feedback) return false;
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::Hall || !game.postGameHall_ ||
            !game.hallHighlightName_.empty() || game.hallHighlightScore_ != 0) return false;
        game.keyDown('A');
        if (game.page_ != WordGamePage::Hall) return false;
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 0 ||
            !game.hallHighlightName_.empty() || game.hallHighlightScore_ != 0) return false;
        game.render(renderer);
        const std::uint64_t replayHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedReplayHash = 0xdff55d7dfe18637eull;
        if (replayHash != ExpectedReplayHash) {
            std::cerr << "word replay frame hash=0x" << std::hex << replayHash << std::dec << "\n";
            return false;
        }
        game.keyDown(VK_RIGHT);
        game.render(renderer);
        const std::uint64_t replayNoHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedReplayNoHash = 0x4e398cfe7456fd82ull;
        if (replayNoHash != ExpectedReplayNoHash) {
            std::cerr << "word replay No frame hash=0x" << std::hex
                      << replayNoHash << std::dec << "\n";
            return false;
        }
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Title) return false;

        game.page_ = WordGamePage::ReplayQuestion;
        game.menuSelection_ = 0;
        game.keyDown('N');
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 1) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_RIGHT);
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 1) return false;
        game.keyDown(VK_SPACE);
        game.keyDown(VK_UP);
        game.keyDown(VK_DOWN);
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 1) return false;
        game.keyDown('Y');
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 0) return false;
        game.keyDown(VK_LEFT);
        game.keyDown(VK_LEFT);
        if (game.page_ != WordGamePage::ReplayQuestion || game.menuSelection_ != 0) return false;
        game.keyDown(VK_RETURN);
        return game.page_ == WordGamePage::Playing && game.core_.active() &&
               game.core_.level() == 1 && game.hallHighlightName_.empty() &&
               game.hallHighlightScore_ == 0;
    }

    static bool usesRecoveredFeedbackQuitRoute() {
        const auto enterWrongFeedback = [](WordGame& game, const int score,
                                           const int reserves) {
            game.settingsPersistenceEnabled_ = false;
            game.startGame(1);
            disableBoardJobs(game);
            (void)game.hall_.replace({});
            auto& scoreState = const_cast<MuncherScore&>(game.core_.scoreState());
            scoreState.restore(score, reserves);
            int wrong = -1;
            for (int index = 0; index < WordGame::BoardCellCount; ++index) {
                if (!game.core_.eaten(static_cast<std::size_t>(index)) &&
                    !game.core_.board().cells[static_cast<std::size_t>(index)].correct) {
                    wrong = index;
                    break;
                }
            }
            if (wrong < 0) return false;
            game.playerRow_ = wrong / WordGame::BoardColumns;
            game.playerColumn_ = wrong % WordGame::BoardColumns;
            game.beginMunch();
            game.resolveMunch();
            return game.page_ == WordGamePage::Feedback &&
                   game.lastResolution_.kind == WordMunchKind::Wrong;
        };

        // The common waiter accepts keyboard Enter and Space, maps a left
        // release to Space, and rejects the untouched right-release code
        // 0xFD.
        WordGame enterResume(GraphicsMode::Vga256, 0x8100);
        if (!enterWrongFeedback(enterResume, 0, 3)) return false;
        enterResume.keyDown(VK_RETURN);
        if (enterResume.page_ != WordGamePage::Playing) return false;

        WordGame pointerResume(GraphicsMode::Vga256, 0x8100);
        if (!enterWrongFeedback(pointerResume, 0, 3)) return false;
        if (pointerResume.pointerPress(true)) return false;
        pointerResume.pointerButton(0, 0, true);
        if (pointerResume.page_ != WordGamePage::Feedback) return false;
        if (pointerResume.pointerPress(false)) return false;
        pointerResume.pointerButton(0, 0, false);
        if (pointerResume.page_ != WordGamePage::Playing) return false;

        WordGame cancel(GraphicsMode::Vga256, 0x8101);
        if (!enterWrongFeedback(cancel, 0, 3)) return false;
        const std::size_t quitStopsBefore =
            cancel.attractOplPlayer_.allChannelStopCountForTest();
        cancel.keyDown(VK_ESCAPE);
        (void)cancel.attractOplPlayer_.renderTestSamples(1);
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 1 ||
            !cancel.quitFromFeedback_ || cancel.attractOplPlayer_.effectPlaying() ||
            cancel.attractOplPlayer_.allChannelStopCountForTest() !=
                quitStopsBefore + 1) return false;
        cancel.keyDown(VK_LEFT);
        cancel.keyDown(VK_LEFT);
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown(VK_RIGHT);
        cancel.keyDown(VK_RIGHT);
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 1) return false;
        cancel.keyDown('Y');
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown(VK_SPACE);
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 0) return false;
        cancel.keyDown('N');
        cancel.keyDown(VK_UP);
        cancel.keyDown(VK_DOWN);
        if (cancel.page_ != WordGamePage::QuitConfirm || cancel.menuSelection_ != 1) return false;
        cancel.keyDown(VK_ESCAPE);
        if (cancel.page_ != WordGamePage::Playing || cancel.quitFromFeedback_ ||
            !cancel.feedbackMessage_.empty() || cancel.feedbackEnemyType_ != -1 ||
            cancel.lastResolution_.kind != WordMunchKind::Invalid ||
            cancel.core_.scoreState().reserves() != 2) return false;

        // Feedback Yes invokes the ordinary scored terminal, including Hall
        // admission, rather than the title-return path used by a normal quit.
        WordGame confirm(GraphicsMode::Vga256, 0x8102);
        if (!enterWrongFeedback(confirm, 55, 3)) return false;
        confirm.keyDown(VK_ESCAPE);
        confirm.keyDown('Y');
        if (confirm.page_ != WordGamePage::QuitConfirm || confirm.menuSelection_ != 0) {
            return false;
        }
        confirm.keyDown(VK_RETURN);
        if (confirm.page_ != WordGamePage::NameEntry || !confirm.pendingHallAdmission_ ||
            confirm.quitFromFeedback_ || confirm.core_.scoreState().score() != 55) return false;

        // Choosing the default No on the last reserve returns through the
        // feedback caller, which still performs the normal final-loss route.
        WordGame finalLoss(GraphicsMode::Vga256, 0x8103);
        if (!enterWrongFeedback(finalLoss, 0, 0)) return false;
        if (!finalLoss.lastResolution_.gameOver) return false;
        finalLoss.keyDown(VK_ESCAPE);
        finalLoss.keyDown(VK_RETURN);
        return finalLoss.page_ == WordGamePage::Hall && finalLoss.postGameHall_ &&
               !finalLoss.pendingHallAdmission_ && !finalLoss.quitFromFeedback_;
    }

    static bool maximumFinalAnswerRoutesBeforeBoardCompletion() {
        WordGame game(GraphicsMode::Vga256, 0x4d58u);
        game.settingsPersistenceEnabled_ = false;
        game.startGame(18);
        disableBoardJobs(game);
        (void)game.hall_.replace({});

        std::vector<std::size_t> correct;
        for (std::size_t index = 0; index < game.core_.board().cells.size(); ++index) {
            if (game.core_.board().cells[index].correct && !game.core_.eaten(index)) {
                correct.push_back(index);
            }
        }
        if (correct.empty()) return false;
        for (std::size_t index = 0; index + 1 < correct.size(); ++index) {
            if (game.core_.munchCell(correct[index]).kind != WordMunchKind::Correct) {
                return false;
            }
        }

        auto& score = const_cast<MuncherScore&>(game.core_.scoreState());
        score.restore(MuncherScore::MaximumScore - MuncherScore::pointsForLevel(18), 3);
        const std::size_t finalCell = correct.back();
        game.playerRow_ = static_cast<int>(finalCell) / WordGame::BoardColumns;
        game.playerColumn_ = static_cast<int>(finalCell) % WordGame::BoardColumns;
        game.beginMunch();
        if (!game.munching_ || game.lastGameplaySound_ != 7) return false;
        const std::uint64_t callsBeforeTerminal = game.random_.calls;
        game.resolveMunch();
        return game.page_ == WordGamePage::NameEntry && game.pendingHallAdmission_ &&
               game.nameEntryMessageType_ == 3 && game.lastResolution_.maximumScore &&
               !game.lastResolution_.boardComplete && game.lastResolution_.cartoonScene == -1 &&
               game.core_.scoreState().score() == MuncherScore::MaximumScore &&
               game.core_.correctRemaining() == 0 && !game.core_.active() &&
               !game.core_.boardComplete() && !game.core_.cartoonPending() &&
               !game.levelCompleteScene_.valid() && game.lastGameplaySound_ == 7 &&
               game.random_.calls == callsBeforeTerminal;
    }

    static bool nameEntryAndHallAdmissionRoute(WordGame& game) {
        game.startGame(1);
        disableBoardJobs(game);
        auto& score = const_cast<MuncherScore&>(game.core_.scoreState());
        score.restore(20'000, 3);
        const std::size_t originalEntries = game.hall_.entries().size();
        game.beginPostGame();
        if (game.page_ != WordGamePage::NameEntry || !game.pendingHallAdmission_ ||
            game.nameEntryMessageType_ != 2) return false;
        Renderer renderer;
        game.render(renderer);
        const std::uint64_t nameHash = fullFrameHash(renderer);
        constexpr std::uint64_t ExpectedBestScoreNameHash = 0x12dced805d013d4bull;
        if (nameHash != ExpectedBestScoreNameHash) {
            std::cerr << "word best-score name frame hash=0x" << std::hex
                      << nameHash << std::dec << "\n";
            return false;
        }

        WordGame liveName(GraphicsMode::Vga256, 0x9182);
        liveName.startGame(1);
        disableBoardJobs(liveName);
        liveName.page_ = WordGamePage::NameEntry;
        liveName.pendingHallAdmission_ = true;
        liveName.nameEntryMessageType_ = 1;
        liveName.nameInput_.clear();
        liveName.nameCursorVisible_ = false;
        liveName.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "name-entry-live-blank-native.ppm");
        const std::uint64_t blankModalHash = frameRegionHash(renderer, 0, 45, 320, 155);
        constexpr std::uint64_t ExpectedBlankModalHash = 0x05eec9c3f1ef5ec3ull;
        if (blankModalHash != ExpectedBlankModalHash) {
            std::cerr << "word live blank name-entry modal hash=0x" << std::hex
                      << blankModalHash << std::dec << "\n";
            return false;
        }
        for (const wchar_t character : std::wstring_view(L"CODEX")) {
            liveName.character(character);
        }
        liveName.render(renderer);
        maybeDumpWordOptionsFrame(renderer, "name-entry-live-typed-native.ppm");
        const std::uint64_t typedModalHash = frameRegionHash(renderer, 0, 45, 320, 155);
        constexpr std::uint64_t ExpectedTypedModalHash = 0x285a173c1222b63dull;
        if (typedModalHash != ExpectedTypedModalHash) {
            std::cerr << "word live typed name-entry modal hash=0x" << std::hex
                      << typedModalHash << std::dec << "\n";
            return false;
        }
        liveName.update(15.0 / WordSchedulerTicksPerSecond);
        if (!liveName.nameCursorVisible_) return false;
        liveName.update(1.0 / WordSchedulerTicksPerSecond);
        if (liveName.nameCursorVisible_) return false;
        liveName.character(L'\b');
        if (!liveName.nameCursorVisible_ || liveName.nameCursorBlinkTimer_ != 0.0) {
            return false;
        }
        for (int index = 0; index < 26; ++index) game.character(L'x');
        if (game.nameInput_ != std::string(25, 'x')) return false;
        for (const UINT ignored : {VK_RIGHT, VK_END, VK_DELETE, VK_UP, VK_DOWN}) {
            game.keyDown(ignored);
            if (game.nameInput_ != std::string(25, 'x')) return false;
        }
        game.keyDown(VK_LEFT);
        if (game.nameInput_ != std::string(24, 'x')) return false;
        game.character(L'\b');
        if (game.nameInput_ != std::string(23, 'x')) return false;
        game.keyDown(VK_HOME);
        if (!game.nameInput_.empty()) return false;
        game.character(L'A');
        game.character(L'd');
        game.character(L'x');
        game.keyDown(VK_LEFT);
        game.character(L'a');
        game.keyDown(VK_RETURN);
        if (game.page_ != WordGamePage::Hall || game.pendingHallAdmission_ ||
            game.hall_.entries().size() != originalEntries + 1 ||
            game.hall_.entries().front() != WordHallScoreEntry{"Ada", 20'000} ||
            game.hallHighlightName_ != "Ada" || game.hallHighlightScore_ != 20'000) {
            return false;
        }
        game.keyDown(VK_SPACE);
        if (game.page_ != WordGamePage::ReplayQuestion ||
            !game.hallHighlightName_.empty() || game.hallHighlightScore_ != 0) return false;

        WordGame fallback(GraphicsMode::Vga256, 0x4567);
        fallback.startGame(1);
        auto& fallbackScore = const_cast<MuncherScore&>(fallback.core_.scoreState());
        fallbackScore.restore(50, 3);
        const std::size_t fallbackEntries = fallback.hall_.entries().size();
        fallback.beginPostGame();
        if (fallback.page_ != WordGamePage::NameEntry || fallback.nameEntryMessageType_ != 1) {
            return false;
        }
        // DS:16BC stores a zero threshold: empty Enter exits, and the caller
        // substitutes the original fallback name.
        fallback.keyDown(VK_RETURN);
        if (fallback.page_ != WordGamePage::Hall ||
            fallback.hall_.entries().size() != fallbackEntries + 1 ||
            fallback.hall_.entries().back() != WordHallScoreEntry{"The Unknown Muncher", 50} ||
            fallback.hallHighlightName_ != "The Unknown Muncher" ||
            fallback.hallHighlightScore_ != 50) {
            return false;
        }

        WordGame partialEscape(GraphicsMode::Vga256, 0x4568);
        partialEscape.startGame(1);
        auto& partialScore = const_cast<MuncherScore&>(partialEscape.core_.scoreState());
        partialScore.restore(55, 3);
        partialEscape.beginPostGame();
        partialEscape.character(L'x');
        partialEscape.character(L'y');
        partialEscape.keyDown(VK_ESCAPE);
        const auto& partialEntries = partialEscape.hall_.entries();
        if (partialEscape.page_ != WordGamePage::Hall ||
            std::find(partialEntries.begin(), partialEntries.end(),
                      WordHallScoreEntry{"xy", 55}) == partialEntries.end()) {
            return false;
        }

        WordGame perfect(GraphicsMode::Vga256, 0x5678);
        perfect.startGame(1);
        auto& perfectScore = const_cast<MuncherScore&>(perfect.core_.scoreState());
        perfectScore.restore(MuncherScore::MaximumScore, 3);
        perfect.beginPostGame();
        return perfect.page_ == WordGamePage::NameEntry &&
               perfect.nameEntryMessageType_ == 3;
    }
};

struct SuperGameTestAccess {
    static std::uint64_t fullFrameHash(const Renderer& renderer) {
        std::uint64_t hash = 1469598103934665603ull;
        for (const std::uint32_t pixel : renderer.pixels()) {
            hash ^= pixel;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static void maybeDumpFrame(const Renderer& renderer,
                               const std::string_view filename) {
        const char* directory = std::getenv("MUNCHERS_DUMP_SUPER_DIR");
        if (!directory || !*directory) return;
        const std::filesystem::path outputDirectory(directory);
        std::error_code error;
        std::filesystem::create_directories(outputDirectory, error);
        if (error) return;
        std::ofstream output(outputDirectory / filename, std::ios::binary);
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

    static bool startupAndInformationRoute() {
        SuperGame game(GraphicsMode::Vga256, 0x69ffu, false);
        Renderer renderer(GraphicsMode::Vga256);
        if (game.page_ != SuperGamePage::StartupVersion) return false;
        game.render(renderer);
        maybeDumpFrame(renderer, "super-version-native.ppm");
        constexpr std::uint64_t CapturedVersionHash = 0x252bf149cd778836ull;
        const std::set<std::uint32_t> versionColors(renderer.pixels().begin(),
                                                    renderer.pixels().end());
        if (!versionColors.contains(Colors::White) ||
            !versionColors.contains(Colors::Black) ||
            fullFrameHash(renderer) != CapturedVersionHash) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::StartupSplash) return false;
        game.render(renderer);
        maybeDumpFrame(renderer, "super-splash-native.ppm");
        constexpr std::uint64_t CapturedSplashHash = 0x4629938e5e5bcadeull;
        if (fullFrameHash(renderer) != CapturedSplashHash) return false;
        game.update(5.0);
        if (game.page_ != SuperGamePage::StartupSplash) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Title) return false;
        game.render(renderer);
        maybeDumpFrame(renderer, "super-title-native.ppm");
        constexpr std::uint64_t CapturedTitleHash = 0x6c2833b00363de98ull;
        if (fullFrameHash(renderer) != CapturedTitleHash) return false;
        game.menuSelection_ = 2;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Information || game.informationPage_ != 0) {
            return false;
        }
        for (int page = 0; page < 13; ++page) {
            if (game.page_ != SuperGamePage::Information ||
                game.informationPage_ != page) return false;
            game.render(renderer);
            if (std::count(renderer.pixels().begin(), renderer.pixels().end(),
                           Colors::White) < 25) return false;
            game.keyDown(VK_SPACE);
        }
        if (game.page_ != SuperGamePage::Title || game.menuSelection_ != 2) return false;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_ESCAPE);
        if (game.page_ != SuperGamePage::Title || game.menuSelection_ != 2) return false;

        SuperGame cga(GraphicsMode::Cga4, 0x69ffu, false);
        Renderer cgaRenderer(GraphicsMode::Cga4);
        cga.render(cgaRenderer);
        maybeDumpFrame(cgaRenderer, "super-cga-version-native.ppm");
        if (fullFrameHash(cgaRenderer) != 0x244a67ce82ba6562ull) return false;
        cga.keyDown(VK_RETURN);
        cga.render(cgaRenderer);
        maybeDumpFrame(cgaRenderer, "super-cga-splash-native.ppm");
        if (fullFrameHash(cgaRenderer) != 0x12869f99f37d987eull) return false;
        cga.update(5.0);
        if (cga.page_ != SuperGamePage::StartupSplash) return false;
        cga.keyDown(VK_RETURN);
        cga.render(cgaRenderer);
        maybeDumpFrame(cgaRenderer, "super-cga-title-native.ppm");
        return cga.page_ == SuperGamePage::Title &&
               fullFrameHash(cgaRenderer) == 0x00cb0fdfaeb841a5ull;
    }

    static bool attractDemoRoute() {
        SuperGame game(GraphicsMode::Vga256, 0x6a0eu, false);
        game.soundOn_ = false;
        game.musicOn_ = true;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Title || game.demoMode_) return false;
        for (int tick = 0; tick < SuperGame::TitleIdleTicks - 1; ++tick) {
            game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        }
        if (game.demoMode_ || game.page_ != SuperGamePage::Title) {
            std::cerr << "Super Demo title timer fired before 450 ticks\n";
            return false;
        }
        OriginalRandom startProbe(0x6a0eu);
        const int expectedLevel = startProbe.range(9) + 1;
        game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        if (!game.demoMode_ || game.page_ != SuperGamePage::Playing ||
            !game.core_.active() || game.core_.level() != expectedLevel ||
            game.core_.transformationMeter() != 16 || game.demoActionTicks_ != 30 ||
            game.oplPlayer_.looping()) {
            std::cerr << "Super Demo state mode/page/active/level/meter/timer/opl/loop="
                      << game.demoMode_ << '/' << static_cast<int>(game.page_) << '/'
                      << game.core_.active() << '/' << game.core_.level() << '/'
                      << game.core_.transformationMeter() << '/'
                      << game.demoActionTicks_ << '/' << game.oplPlayer_.playing() << '/'
                      << game.oplPlayer_.looping() << '\n';
            return false;
        }

        // The installed record counts down for exactly 30 public ticks. Safe
        // and Troggle timers cannot become due in this interval, so the 30th
        // tick isolates selector 5's reload and key-choice PRNG tail.
        for (int tick = 0; tick < 29; ++tick) {
            game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        }
        if (game.demoActionTicks_ != 1 || game.moving_ || game.munching_) {
            std::cerr << "Super Demo initial 30-tick countdown regressed\n";
            return false;
        }
        OriginalRandom dueProbe = game.random_;
        const int expectedReload = dueProbe.range(30) + 15;
        const int current = game.playerRow_ * SuperGame::BoardColumns +
                            game.playerColumn_;
        const int signedValue = game.core_.eaten(static_cast<std::size_t>(current))
            ? 0 : game.core_.board().cells[static_cast<std::size_t>(current)]
                      .signedSourceIndex;
        if (signedValue < 0) {
            if (dueProbe.range(10) != 0) (void)dueProbe.range(4);
        } else if (signedValue == 0) {
            (void)dueProbe.range(4);
        }
        game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        if (game.demoActionTicks_ != expectedReload ||
            game.random_.state != dueProbe.state ||
            game.random_.calls != dueProbe.calls ||
            (!game.moving_ && !game.munching_)) {
            std::cerr << "Super Demo due callback reload/PRNG/action regressed\n";
            return false;
        }

        const auto prepareController = [](SuperGame& target,
                                          const std::uint16_t seed) {
            target.soundOn_ = false;
            target.startGame(0, 1);
            target.random_.seed(seed);
            target.demoMode_ = true;
            target.page_ = SuperGamePage::Playing;
            target.demoActionTicks_ = 1;
            target.safeZoneJobCount_ = 0;
            target.safeZoneJobs_.fill({});
            target.safeCells_.fill(false);
            target.enemySlotCount_ = 0;
            target.enemySlots_.fill({});
            target.enemies_.clear();
            target.inputQueue_.clear();
            target.playerRow_ = 2;
            target.playerColumn_ = 2;
            target.moving_ = false;
            target.munching_ = false;
            target.transforming_ = false;
            target.collisionActive_ = false;
            target.playerRecovering_ = false;
            target.core_.active_ = true;
            target.core_.boardComplete_ = false;
            target.core_.maximumScoreReached_ = false;
            target.core_.stagedMunchCell_ = -1;
            target.core_.stagedMunchValue_ = {};
            target.core_.stagedTransformation_ = false;
            target.core_.transformationCell_ = -1;
            target.core_.eaten_.fill(true);
            target.core_.correctRemaining_ = 0;
            for (SuperBoardCell& cell : target.core_.board_.cells) {
                cell = {"fixture", false, -1};
            }
        };
        const auto setLive = [](SuperGame& target, const int row, const int column,
                                const int value) {
            const std::size_t cell = static_cast<std::size_t>(
                row * SuperGame::BoardColumns + column);
            target.core_.board_.cells[cell] = {
                "fixture", value > 0, static_cast<std::int16_t>(value)};
            target.core_.eaten_[cell] = false;
            if (value > 0) ++target.core_.correctRemaining_;
        };

        // Positive values always inject Space after only the reload draw.
        SuperGame positive(GraphicsMode::Vga256, 0x6a40u, false);
        prepareController(positive, 0x1201u);
        setLive(positive, 2, 2, 1);
        OriginalRandom positiveProbe = positive.random_;
        const int positiveReload = positiveProbe.range(30) + 15;
        positive.updateDemo();
        if (!positive.munching_ || positive.moveDirection_ != 1 ||
            positive.demoActionTicks_ != positiveReload ||
            positive.random_.state != positiveProbe.state ||
            positive.random_.calls != positiveProbe.calls) return false;

        // Find a deterministic 1-in-10 incorrect-answer roll and require the
        // same Space injection after reload + chance draw.
        std::uint16_t wrongMunchSeed = 1;
        for (;; ++wrongMunchSeed) {
            OriginalRandom probe(wrongMunchSeed);
            (void)probe.range(30);
            if (probe.range(10) == 0) break;
        }
        SuperGame wrongMunch(GraphicsMode::Vga256, 0x6a41u, false);
        prepareController(wrongMunch, wrongMunchSeed);
        setLive(wrongMunch, 2, 2, -1);
        OriginalRandom wrongMunchProbe = wrongMunch.random_;
        const int wrongReload = wrongMunchProbe.range(30) + 15;
        (void)wrongMunchProbe.range(10);
        wrongMunch.updateDemo();
        if (!wrongMunch.munching_ || wrongMunch.demoActionTicks_ != wrongReload ||
            wrongMunch.random_.state != wrongMunchProbe.state ||
            wrongMunch.random_.calls != wrongMunchProbe.calls) return false;

        // A failed incorrect-answer roll chooses one initial direction and
        // rotates clockwise until the first adjacent positive value.
        std::uint16_t rotateSeed = 1;
        int initialDirection = 0;
        for (;; ++rotateSeed) {
            OriginalRandom probe(rotateSeed);
            (void)probe.range(30);
            if (probe.range(10) == 0) continue;
            initialDirection = probe.range(4);
            break;
        }
        const int expectedDirection = (initialDirection + 2) % 4;
        static constexpr std::array<int, 4> RowDelta = {-1, 0, 1, 0};
        static constexpr std::array<int, 4> ColumnDelta = {0, 1, 0, -1};
        SuperGame rotated(GraphicsMode::Vga256, 0x6a42u, false);
        prepareController(rotated, rotateSeed);
        setLive(rotated, 2, 2, -1);
        setLive(rotated, 2 + RowDelta[static_cast<std::size_t>(expectedDirection)],
                2 + ColumnDelta[static_cast<std::size_t>(expectedDirection)], 1);
        OriginalRandom rotateProbe = rotated.random_;
        const int rotateReload = rotateProbe.range(30) + 15;
        (void)rotateProbe.range(10);
        (void)rotateProbe.range(4);
        rotated.updateDemo();
        if (!rotated.moving_ || rotated.moveDirection_ != expectedDirection ||
            rotated.demoActionTicks_ != rotateReload ||
            rotated.random_.state != rotateProbe.state ||
            rotated.random_.calls != rotateProbe.calls) return false;

        // With no positive neighbor, four rotations wrap to and inject the
        // original direction. A busy player still consumes the callback's
        // reload/selection draws but cannot accept the chosen key.
        SuperGame fallback(GraphicsMode::Vga256, 0x6a43u, false);
        prepareController(fallback, 0x2201u);
        OriginalRandom fallbackProbe = fallback.random_;
        const int fallbackReload = fallbackProbe.range(30) + 15;
        const int fallbackDirection = fallbackProbe.range(4);
        fallback.updateDemo();
        if (!fallback.moving_ || fallback.moveDirection_ != fallbackDirection ||
            fallback.demoActionTicks_ != fallbackReload ||
            fallback.random_.state != fallbackProbe.state ||
            fallback.random_.calls != fallbackProbe.calls) return false;

        SuperGame busy(GraphicsMode::Vga256, 0x6a44u, false);
        prepareController(busy, 0x3301u);
        setLive(busy, 2, 2, 1);
        busy.moving_ = true;
        busy.moveDirection_ = 3;
        busy.moveTimer_ = 5.0 / SuperGame::SchedulerTicksPerSecond;
        OriginalRandom busyProbe = busy.random_;
        const int busyReload = busyProbe.range(30) + 15;
        busy.updateDemo();
        if (!busy.moving_ || busy.munching_ || busy.moveDirection_ != 3 ||
            busy.demoActionTicks_ != busyReload ||
            busy.random_.state != busyProbe.state ||
            busy.random_.calls != busyProbe.calls) return false;

        game.keyDown(VK_SPACE);
        return !game.demoMode_ && game.page_ == SuperGamePage::Title &&
               !game.oplPlayer_.playing();
    }

    static bool streamingAudioRoute() {
        SuperGame game(GraphicsMode::Vga256, 0x6a0fu, false);
        game.soundOn_ = true;
        game.musicOn_ = true;
        game.speakerEffects_ = false;
        game.startGame(0);
        const std::size_t initialEffects = game.oplPlayer_.effectPlayCount();
        game.playGameplaySound(5);
        if (!game.oplPlayer_.playing() || !game.oplPlayer_.effectPlaying() ||
            game.oplPlayer_.effectPlayCount() != initialEffects + 1 ||
            game.effectPlayer_.playCount() != 0) {
            std::cerr << "Super streaming gameplay state=" << game.oplPlayer_.playing()
                      << '/' << game.oplPlayer_.effectPlaying() << " effects="
                      << initialEffects << "->" << game.oplPlayer_.effectPlayCount()
                      << " waves=" << game.effectPlayer_.playCount() << '\n';
            return false;
        }
        if (!game.startMission(4) || game.oplPlayer_.scoreWriteCount() != 0) return false;
        const std::size_t priorEffects = game.oplPlayer_.effectPlayCount();
        std::set<std::uint16_t> missionEvents;
        for (int tick = 0; tick < 300 &&
             game.oplPlayer_.effectPlayCount() == priorEffects; ++tick) {
            game.advanceMissionTick();
            missionEvents.insert(game.missionScene_.callbackEvents().begin(),
                                 game.missionScene_.callbackEvents().end());
        }
        if (game.oplPlayer_.effectPlayCount() == priorEffects) {
            std::cerr << "Super streaming mission emitted no callback effect; events=";
            for (const std::uint16_t event : missionEvents) std::cerr << event << ',';
            std::cerr << '\n';
            return false;
        }
        game.page_ = SuperGamePage::Playing;
        const std::size_t toggleEffects = game.oplPlayer_.effectPlayCount();
        game.toggleSound();
        if (game.soundOn_ || !game.oplPlayer_.effectPlaying() ||
            game.oplPlayer_.effectPlayCount() != toggleEffects + 1) return false;
        game.toggleMusic();
        if (game.musicOn_) return false;
        const std::size_t stopCount = game.oplPlayer_.allChannelStopCountForTest();
        (void)game.oplPlayer_.renderTestSamples(1);
        if (game.oplPlayer_.allChannelStopCountForTest() != stopCount + 1) return false;
        game.toggleMusic();
        return game.musicOn_ && game.oplPlayer_.effectPlaying();
    }

    static bool schedulerCoalescingRoute() {
        const auto signature = [](const SuperGame& game) {
            std::ostringstream out;
            out << std::setprecision(17)
                << static_cast<int>(game.page_) << ' ' << game.random_.state << ' '
                << game.random_.calls << ' ' << game.core_.level() << ' '
                << game.core_.scoreState().score() << ' '
                << game.core_.scoreState().reserves() << ' '
                << game.core_.transformationMeter() << ' '
                << game.playerRow_ << ' ' << game.playerColumn_ << ' '
                << game.moving_ << ' ' << game.moveTimer_ << ' '
                << game.munching_ << ' ' << game.munchTimer_ << ' '
                << game.transforming_ << ' ' << game.transformationAnimationTimer_ << ' '
                << game.collisionActive_ << ' ' << game.collisionSlot_ << ' '
                << game.collisionTimer_ << ' ' << game.enemySlotCount_ << ' '
                << game.enemyWarning_ << ' ' << game.safeZoneJobCount_ << ' '
                << game.demoMode_ << ' ' << game.demoActionTicks_ << ' ';
            for (std::size_t cell = 0; cell < SuperGame::BoardCellCount; ++cell) {
                out << game.core_.board().cells[cell].signedSourceIndex << ':'
                    << game.core_.eaten(cell) << ',';
            }
            for (const bool safe : game.safeCells_) out << safe;
            for (const SuperGame::SafeZoneJob& job : game.safeZoneJobs_) {
                out << ' ' << job.active << ':' << job.cellIndex << ':'
                    << job.period << ':' << job.timer;
            }
            for (const SuperGame::EnemySlot& slot : game.enemySlots_) {
                out << ' ' << static_cast<int>(slot.phase) << ':' << slot.type << ':'
                    << slot.edge << ':' << slot.row << ':' << slot.column << ':'
                    << slot.timer;
            }
            for (const SuperGame::Enemy& enemy : game.enemies_) {
                out << ' ' << enemy.slot << ':' << enemy.type << ':' << enemy.row << ':'
                    << enemy.column << ':' << enemy.fromRow << ':' << enemy.fromColumn
                    << ':' << enemy.direction << ':' << enemy.timer << ':'
                    << enemy.animationTimer << ':' << enemy.moving << ':'
                    << enemy.entering << ':' << enemy.exiting << ':' << enemy.biting
                    << ':' << enemy.cannibalizing << ':' << enemy.cannibalTimer << ':'
                    << enemy.overlapFrozen << ':' << enemy.overlapRetired;
            }
            return out.str();
        };

        SuperGame coalesced(GraphicsMode::Vga256, 0x6a30u, false);
        SuperGame split(GraphicsMode::Vga256, 0x6a30u, false);
        coalesced.soundOn_ = split.soundOn_ = false;
        coalesced.startGame(0, 7);
        split.startGame(0, 7);
        constexpr int ticks = 360;
        coalesced.update(static_cast<double>(ticks) / SuperGame::SchedulerTicksPerSecond);
        for (int tick = 0; tick < ticks; ++tick) {
            split.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        }
        if (signature(coalesced) != signature(split)) {
            std::cerr << "Super coalesced scheduler diverged from split ticks\n";
            return false;
        }

        // A bite replaces its physical Troggle slot; an earlier slot remains
        // live, while the bite terminal aborts the later traversal.
        SuperGame collision(GraphicsMode::Vga256, 0x6a31u, false);
        collision.soundOn_ = false;
        collision.startGame(0, 9);
        collision.enemies_.clear();
        collision.enemySlotCount_ = 2;
        for (int slot = 0; slot < 2; ++slot) {
            collision.enemySlots_[static_cast<std::size_t>(slot)].phase =
                SuperGame::EnemySlotPhase::Active;
        }
        SuperGame::Enemy earlier;
        earlier.slot = 0;
        earlier.type = 0;
        earlier.row = 1;
        earlier.column = 1;
        earlier.timer = 1.0 / SuperGame::SchedulerTicksPerSecond;
        SuperGame::Enemy biter;
        biter.slot = 1;
        biter.type = 0;
        biter.row = collision.playerRow_;
        biter.column = collision.playerColumn_;
        collision.enemies_ = {earlier, biter};
        collision.collideWithEnemy(1);
        const double biteBefore = collision.collisionTimer_;
        collision.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        const auto earlierAfter = std::find_if(collision.enemies_.begin(),
            collision.enemies_.end(), [](const SuperGame::Enemy& enemy) {
                return enemy.slot == 0;
            });
        return earlierAfter != collision.enemies_.end() && earlierAfter->moving &&
               collision.collisionActive_ && collision.collisionTimer_ < biteBefore;
    }

    static bool optionsAndPersistenceRoute() {
        SuperGame admin(GraphicsMode::Vga256, 0x6a10u, false);
        admin.keyDown(VK_RETURN);
        admin.keyDown(VK_RETURN);
        admin.menuSelection_ = 3;
        admin.keyDown(VK_RETURN);
        admin.pointerMove(160, 105);
        if (admin.page_ != SuperGamePage::Options || admin.menuSelection_ != 3) {
            return false;
        }
        Renderer optionsRenderer(GraphicsMode::Vga256);
        admin.menuSelection_ = 0;
        admin.render(optionsRenderer);
        maybeDumpFrame(optionsRenderer, "super-options-native.ppm");
        if (fullFrameHash(optionsRenderer) != 0x5ec15fd100e375f0ull) return false;
        admin.menuSelection_ = 3;
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsSetPassword) return false;
        admin.passwordEntry_.clear();
        for (const wchar_t value : std::wstring_view(L"open")) admin.character(value);
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsSetPassword ||
            !admin.passwordEditingHint_) return false;
        admin.passwordEntry_.clear();
        for (const wchar_t value : std::wstring_view(L"four letters")) admin.character(value);
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::Options || admin.password_ != "open" ||
            admin.passwordHint_ != "four letters") {
            return false;
        }
        admin.keyDown(VK_ESCAPE);
        admin.menuSelection_ = 3;
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsPasswordPrompt) return false;
        admin.character(L'x');
        admin.keyDown(VK_RETURN);
        if (!admin.passwordError_ || admin.page_ != SuperGamePage::OptionsPasswordPrompt) {
            return false;
        }
        for (const wchar_t value : std::wstring_view(L"open")) admin.character(value);
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::Options) return false;
        admin.menuSelection_ = 2;
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsEraseHallSelect) return false;
        admin.halls_[0] = {{"Keep", 200}, {"Delete", 100}};
        admin.menuSelection_ = 1;
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsEraseEntries ||
            admin.menuSelection_ != 0 || admin.eraseDraft_.size() != 2) return false;
        admin.keyDown(VK_DOWN);
        admin.keyDown(VK_SPACE);
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::Options ||
            admin.halls_[0] != std::vector<SuperGame::HallEntry>{{"Keep", 200}}) {
            return false;
        }
        admin.menuSelection_ = 4;
        admin.keyDown(VK_RETURN);
        if (!admin.joystickEnabled_) return false;
        admin.menuSelection_ = 5;
        admin.keyDown(VK_RETURN);
        if (admin.page_ != SuperGamePage::OptionsCalibration ||
            !admin.calibrationActive_) return false;
        admin.setJoystickState(true, 500u, 1000u, 0u);
        admin.setJoystickState(true, 60'000u, 62'000u, 1u);
        if (admin.page_ != SuperGamePage::Options || !admin.joystickCalibrated_ ||
            admin.calibrationActive_ ||
            admin.joystickLeftThreshold_ != 500u + (60'000u - 500u) / 3u ||
            admin.joystickRightThreshold_ != 60'000u - (60'000u - 500u) / 3u ||
            admin.joystickUpThreshold_ != 1000u + (62'000u - 1000u) / 3u ||
            admin.joystickDownThreshold_ != 62'000u - (62'000u - 1000u) / 3u) {
            return false;
        }

        SuperGame options(GraphicsMode::Vga256, 0x6a11u, false);
        options.keyDown(VK_RETURN);
        options.keyDown(VK_RETURN);
        options.menuSelection_ = 3;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::Options) return false;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsContent) return false;
        options.menuSelection_ = 2;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsDifficulty) return false;
        options.menuSelection_ = 2;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsContent ||
            options.boardSettings_.difficulty != 30) return false;
        options.menuSelection_ = 3;
        options.keyDown(VK_RETURN);
        if (!options.boardSettings_.negations) return false;
        options.menuSelection_ = 1;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsGames) return false;
        options.menuSelection_ = 5;
        options.keyDown(VK_SPACE);
        if (options.boardSettings_.selectedTopics[5]) return false;
        options.menuSelection_ = 0;
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsRules ||
            options.ruleTopic_ != 0 || options.menuSelection_ != 0) return false;
        options.keyDown(VK_F2);
        options.keyDown(VK_RETURN);
        if (options.page_ != SuperGamePage::OptionsRuleWords) return false;
        options.keyDown(VK_TAB);
        if (options.menuSelection_ != 1) return false;
        options.keyDown(VK_ESCAPE);
        options.keyDown(VK_ESCAPE);
        if (options.page_ != SuperGamePage::OptionsGames ||
            options.menuSelection_ != 0) return false;
        options.keyDown(VK_ESCAPE);

        constexpr std::array<int, 5> quickThresholds = {10, 20, 30, 40, 50};
        constexpr std::array<int, 5> quickDifficulties = {0, 0, 1, 1, 2};
        constexpr std::array<bool, 5> quickNegations = {
            false, false, true, true, true
        };
        for (int preset = 0; preset < 5; ++preset) {
            SuperGame quick(GraphicsMode::Vga256,
                            static_cast<std::uint16_t>(0x6a20u + preset), false);
            quick.page_ = SuperGamePage::OptionsQuickSet;
            quick.menuSelection_ = preset + 1;
            quick.boardSettings_.selectedTopics = {
                true, false, true, false, true, false
            };
            const auto topicsBefore = quick.boardSettings_.selectedTopics;
            quick.keyDown(VK_RETURN);
            if (quick.page_ != SuperGamePage::OptionsContent ||
                quick.quickSet_ != preset ||
                quick.difficultyMode_ != quickDifficulties[static_cast<std::size_t>(preset)] ||
                quick.boardSettings_.difficulty !=
                    (preset < 2 ? 10 : preset < 4 ? 20 : 30) ||
                quick.boardSettings_.negations !=
                    quickNegations[static_cast<std::size_t>(preset)] ||
                quick.boardSettings_.selectedTopics != topicsBefore) {
                std::cerr << "Super Quick Set state mismatch at preset " << preset << '\n';
                return false;
            }
            for (std::size_t topic = 0; topic < SuperTopicCount; ++topic) {
                const SuperCategorySet* set = quick.content_.set(topic);
                if (!set) return false;
                for (std::size_t rule = 0; rule < SuperMaximumRulesPerTopic; ++rule) {
                    const bool expected = rule < set->rules.size() &&
                        set->rules[rule].quickSetThreshold() <=
                            quickThresholds[static_cast<std::size_t>(preset)];
                    if (quick.boardSettings_.selectedRules[topic][rule] != expected) {
                        std::cerr << "Super Quick Set rule mismatch at preset/topic/rule "
                                  << preset << '/' << topic << '/' << rule << '\n';
                        return false;
                    }
                }
            }
        }

        SuperGame noQuickChange(GraphicsMode::Vga256, 0x6a25u, false);
        noQuickChange.page_ = SuperGamePage::OptionsQuickSet;
        noQuickChange.menuSelection_ = 0;
        const auto noQuickSettings = noQuickChange.boardSettings_;
        const int noQuickMode = noQuickChange.difficultyMode_;
        const int noQuickPreset = noQuickChange.quickSet_;
        noQuickChange.keyDown(VK_RETURN);
        if (noQuickChange.page_ != SuperGamePage::OptionsContent ||
            noQuickChange.boardSettings_.selectedTopics != noQuickSettings.selectedTopics ||
            noQuickChange.boardSettings_.selectedRules != noQuickSettings.selectedRules ||
            noQuickChange.boardSettings_.difficulty != noQuickSettings.difficulty ||
            noQuickChange.boardSettings_.negations != noQuickSettings.negations ||
            noQuickChange.difficultyMode_ != noQuickMode ||
            noQuickChange.quickSet_ != noQuickPreset) return false;

        SuperGame difficultyGate(GraphicsMode::Vga256, 0x6a26u, false);
        difficultyGate.page_ = SuperGamePage::OptionsDifficulty;
        difficultyGate.menuSelection_ = 0;
        difficultyGate.boardSettings_.selectedTopics.fill(false);
        difficultyGate.boardSettings_.selectedTopics[0] = true;
        difficultyGate.boardSettings_.selectedRules[0].fill(false);
        const SuperCategorySet* difficultySet = difficultyGate.content_.set(0);
        if (!difficultySet) return false;
        const auto geniusRule = std::find_if(difficultySet->rules.begin(),
            difficultySet->rules.end(), [](const SuperRule& rule) {
                return rule.minimumDifficulty() == 30;
            });
        if (geniusRule == difficultySet->rules.end()) return false;
        const std::size_t geniusRuleIndex = static_cast<std::size_t>(
            geniusRule - difficultySet->rules.begin());
        difficultyGate.boardSettings_.selectedRules[0][geniusRuleIndex] = true;
        const int difficultyBefore = difficultyGate.difficultyMode_;
        difficultyGate.keyDown(VK_RETURN);
        if (difficultyGate.page_ != SuperGamePage::OptionsDifficulty ||
            difficultyGate.difficultyErrorTopic_ != 0 ||
            difficultyGate.difficultyMode_ != difficultyBefore) return false;
        Renderer difficultyRenderer(GraphicsMode::Vga256);
        difficultyGate.render(difficultyRenderer);
        if (std::count(difficultyRenderer.pixels().begin(),
                       difficultyRenderer.pixels().end(), Colors::Yellow) < 5) return false;
        difficultyGate.keyDown(VK_SPACE);
        difficultyGate.menuSelection_ = 2;
        difficultyGate.keyDown(VK_RETURN);
        if (difficultyGate.page_ != SuperGamePage::OptionsContent ||
            difficultyGate.difficultyMode_ != 2 ||
            difficultyGate.boardSettings_.difficulty != 30) return false;

        const std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            L"super-munchers-native-settings-roundtrip.txt";
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.wstring() + L".tmp", error);

        SuperGame saved(GraphicsMode::Vga256, 0x6a12u, false);
        saved.settingsPersistenceEnabled_ = true;
        saved.settingsPathOverride_ = path.wstring();
        saved.soundOn_ = false;
        saved.musicOn_ = false;
        saved.speakerEffects_ = true;
        saved.answerVisionOption_ = false;
        saved.joystickEnabled_ = true;
        saved.joystickCalibrated_ = true;
        saved.joystickLeftThreshold_ = 600;
        saved.joystickRightThreshold_ = 1400;
        saved.joystickUpThreshold_ = 1200;
        saved.joystickDownThreshold_ = 2800;
        saved.password_ = "secret";
        saved.passwordHint_ = "six letters";
        saved.boardSettings_ = options.boardSettings_;
        saved.difficultyMode_ = options.difficultyMode_;
        saved.quickSet_ = 3;
        saved.halls_[2].insert(saved.halls_[2].begin(), {"Round Trip", 4321});
        std::stable_sort(saved.halls_[2].begin(), saved.halls_[2].end(),
                         [](const SuperGame::HallEntry& left,
                            const SuperGame::HallEntry& right) {
                             return left.score > right.score;
                         });
        if (saved.halls_[2].size() > SuperHallTable::Capacity) {
            saved.halls_[2].resize(SuperHallTable::Capacity);
        }
        saved.saveSettings();
        if (!std::filesystem::is_regular_file(path)) return false;

        SuperGame loaded(GraphicsMode::Vga256, 0x6a13u, false);
        loaded.settingsPersistenceEnabled_ = true;
        loaded.settingsPathOverride_ = path.wstring();
        loaded.loadSettings();
        const bool matches = !loaded.soundOn_ && !loaded.musicOn_ &&
            loaded.speakerEffects_ && !loaded.answerVisionOption_ &&
            loaded.joystickEnabled_ && loaded.joystickCalibrated_ &&
            loaded.joystickLeftThreshold_ == 600 &&
            loaded.joystickRightThreshold_ == 1400 &&
            loaded.joystickUpThreshold_ == 1200 &&
            loaded.joystickDownThreshold_ == 2800 &&
            loaded.password_ == "secret" && loaded.passwordHint_ == "six letters" &&
            loaded.boardSettings_.difficulty == 30 &&
            loaded.boardSettings_.negations &&
            loaded.boardSettings_.selectedTopics == saved.boardSettings_.selectedTopics &&
            loaded.boardSettings_.selectedRules == saved.boardSettings_.selectedRules &&
            loaded.difficultyMode_ == 2 && loaded.quickSet_ == 3 &&
            loaded.halls_ == saved.halls_;
        if (!matches) {
            std::cerr << "Super persistence loaded sound/music/speaker/vision="
                      << loaded.soundOn_ << '/' << loaded.musicOn_ << '/'
                      << loaded.speakerEffects_ << '/' << loaded.answerVisionOption_
                      << " difficulty/mode/neg/quick=" << loaded.boardSettings_.difficulty
                      << '/' << loaded.difficultyMode_ << '/'
                      << loaded.boardSettings_.negations << '/' << loaded.quickSet_
                      << " topics=";
            for (const bool topic : loaded.boardSettings_.selectedTopics) {
                std::cerr << topic;
            }
            std::cerr << " hallEqual=" << (loaded.halls_ == saved.halls_) << '\n';
        }
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.wstring() + L".tmp", error);
        return matches;
    }

    static bool nativeChewAndLevelSelectRoute() {
        SuperGame game(GraphicsMode::Vga256, 0xf585u, false);
        game.soundOn_ = false;
        game.keyDown(VK_RETURN);
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Title || game.shouldQuit_) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::InstructionsQuestion ||
            game.menuSelection_ != 1) return false;
        game.pointerMove(120, 108);
        if (game.menuSelection_ != 0) return false;
        game.pointerMove(180, 108);
        if (game.menuSelection_ != 1) return false;
        Renderer routeRenderer(GraphicsMode::Vga256);
        game.render(routeRenderer);
        maybeDumpFrame(routeRenderer, "super-instructions-question-native.ppm");
        if (fullFrameHash(routeRenderer) != 0x070b08aa80400c33ull) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::GameDifficultySelect ||
            game.menuSelection_ != 0) return false;
        game.pointerMove(120, 108);
        if (game.menuSelection_ != 2) return false;
        game.pointerMove(120, 86);
        if (game.menuSelection_ != 0) return false;
        game.render(routeRenderer);
        maybeDumpFrame(routeRenderer, "super-difficulty-select-native.ppm");
        if (fullFrameHash(routeRenderer) != 0x59cf4295ac513588ull) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::GameSelect || game.menuSelection_ != 0) return false;
        game.pointerMove(120, 134);
        if (game.menuSelection_ != 6) return false;
        game.pointerMove(120, 68);
        if (game.menuSelection_ != 0) return false;
        game.render(routeRenderer);
        maybeDumpFrame(routeRenderer, "super-game-select-native.ppm");
        if (fullFrameHash(routeRenderer) != 0x5a3a2d415acd556aull) return false;
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Playing || !game.core_.active() ||
            game.core_.board().selection.topic != 0) return false;

        SuperGame presentation(GraphicsMode::Vga256, 0xf586u, false);
        presentation.soundOn_ = false;
        presentation.startGame(0, 1);
        constexpr std::array<std::string_view, SuperGame::BoardCellCount> CapturedWords = {
            "peacock", "pigeon", "mouse", "owl", "lobster", "pelican",
            "flounder", "toad", "sheep", "stinkbug", "guppy", "hawk",
            "sheep", "cricket", "flea", "chicken", "firefly", "snow~leopard",
            "leopard", "roundworm", "penguin", "", "bluebird", "parrot",
            "allosaurus", "moth", "swan", "mosquito", "chicken", "sparrow",
        };
        presentation.core_.board_.displayedRule = "birds";
        presentation.core_.eaten_.fill(false);
        for (int index = 0; index < SuperGame::BoardCellCount; ++index) {
            presentation.core_.board_.cells[static_cast<std::size_t>(index)].text =
                std::string(CapturedWords[static_cast<std::size_t>(index)]);
        }
        presentation.playerRow_ = 3;
        presentation.playerColumn_ = 3;
        presentation.playerTerminalFrame_ = 7;
        presentation.moving_ = false;
        presentation.munching_ = false;
        presentation.transforming_ = false;
        presentation.safeCells_.fill(false);
        presentation.safeCells_[10] = true;
        presentation.enemies_.clear();
        presentation.enemyWarning_ = false;
        Renderer boardRenderer(GraphicsMode::Vga256);
        presentation.render(boardRenderer);
        maybeDumpFrame(boardRenderer, "super-first-board-native.ppm");
        if (fullFrameHash(boardRenderer) != 0x7e5cf95c68a2efa9ull) {
            std::cerr << "Super live-oracle first-board hash=0x" << std::hex
                      << fullFrameHash(boardRenderer) << std::dec << '\n';
            return false;
        }

        SuperGame cgaPresentation(GraphicsMode::Cga4, 0xf587u, false);
        cgaPresentation.soundOn_ = false;
        cgaPresentation.startGame(0, 1);
        constexpr std::array<std::string_view, SuperGame::BoardCellCount>
            CapturedCgaWords = {
                "duck", "blue~whale", "woodchuck", "beaver", "peacock", "lobster",
                "porpoise", "goat", "", "blue jay", "cardinal", "frog",
                "wood-~pecker", "wolf", "pigeon", "penguin", "blue jay", "peacock",
                "sparrow", "water~bug", "donkey", "peacock", "eagle", "ostrich",
                "duck", "robin", "stork", "trout", "dragonfly", "coral~snake",
            };
        cgaPresentation.core_.board_.displayedRule = "birds";
        cgaPresentation.core_.eaten_.fill(false);
        for (int index = 0; index < SuperGame::BoardCellCount; ++index) {
            cgaPresentation.core_.board_.cells[static_cast<std::size_t>(index)].text =
                std::string(CapturedCgaWords[static_cast<std::size_t>(index)]);
        }
        cgaPresentation.playerRow_ = 1;
        cgaPresentation.playerColumn_ = 2;
        cgaPresentation.playerTerminalFrame_ = 7;
        cgaPresentation.moving_ = false;
        cgaPresentation.munching_ = false;
        cgaPresentation.transforming_ = false;
        cgaPresentation.safeCells_.fill(false);
        cgaPresentation.safeCells_[14] = true;
        cgaPresentation.enemies_.clear();
        cgaPresentation.enemyWarning_ = false;
        Renderer cgaBoardRenderer(GraphicsMode::Cga4);
        cgaPresentation.render(cgaBoardRenderer);
        maybeDumpFrame(cgaBoardRenderer, "super-cga-first-board-native.ppm");
        if (fullFrameHash(cgaBoardRenderer) != 0x4e35eefdc3d4372dull) {
            std::cerr << "Super CGA live-oracle first-board hash=0x" << std::hex
                      << fullFrameHash(cgaBoardRenderer) << std::dec << '\n';
            return false;
        }

        SuperGame chewPresentation(GraphicsMode::Vga256, 0xf588u, false);
        chewPresentation.soundOn_ = false;
        chewPresentation.startGame(0, 1);
        constexpr std::array<std::string_view, SuperGame::BoardCellCount>
            CapturedChewWords = {
                "moth", "seagull", "coral~snake", "zebra", "crab", "mud~puppy",
                "garter~snake", "beaded~lizard", "coral~snake", "cow",
                "beaded~lizard", "cobra",
                "earthworm", "seagull", "ant", "baboon", "crocodile", "roundworm",
                "stork", "crocodile", "bee", "", "alligator", "zebra",
                "cobra", "king~snake", "copperhead", "crab", "grass-~hopper", "cat",
            };
        chewPresentation.core_.board_.displayedRule = "reptiles";
        chewPresentation.core_.eaten_.fill(false);
        for (int index = 0; index < SuperGame::BoardCellCount; ++index) {
            chewPresentation.core_.board_.cells[static_cast<std::size_t>(index)].text =
                std::string(CapturedChewWords[static_cast<std::size_t>(index)]);
        }
        chewPresentation.playerRow_ = 3;
        chewPresentation.playerColumn_ = 4;
        chewPresentation.playerTerminalFrame_ = -1;
        chewPresentation.moving_ = false;
        chewPresentation.munching_ = true;
        chewPresentation.munchCellIndex_ = 22;
        chewPresentation.munchTimer_ = SuperGame::MunchAnimationDuration;
        chewPresentation.transforming_ = false;
        chewPresentation.safeCells_.fill(false);
        chewPresentation.safeCells_[22] = true;
        chewPresentation.enemies_.clear();
        chewPresentation.enemyWarning_ = false;
        Renderer chewRenderer(GraphicsMode::Vga256);
        chewPresentation.render(chewRenderer);
        maybeDumpFrame(chewRenderer, "super-vga-chew-frame-12-native.ppm");
        if (fullFrameHash(chewRenderer) != 0x482dd1311847b8d1ull) return false;
        chewPresentation.munchTimer_ = SuperGame::MunchAnimationDuration -
            1.0 / SuperGame::SchedulerTicksPerSecond;
        chewPresentation.render(chewRenderer);
        maybeDumpFrame(chewRenderer, "super-vga-chew-frame-13-native.ppm");
        if (fullFrameHash(chewRenderer) != 0xf74ec4e5374f33b1ull) return false;

        SuperGame trogglePresentation(GraphicsMode::Vga256, 0xf589u, false);
        trogglePresentation.soundOn_ = false;
        trogglePresentation.startGame(0, 1);
        constexpr std::array<std::string_view, SuperGame::BoardCellCount>
            CapturedTroggleWords = {
                "bear", "flea", "newt", "crocodile", "wolf", "raccoon",
                "beaver", "newt", "flea", "chicken", "gray~whale", "leopard",
                "crocodile", "rat", "sheep", "cow", "turkey", "elk",
                "shrimp", "", "bee", "crocodile", "flea", "pig",
                "cougar", "flounder", "wasp", "wood-~pecker", "wasp", "cougar",
            };
        trogglePresentation.core_.board_.displayedRule = "four-legged animals";
        trogglePresentation.core_.eaten_.fill(false);
        for (int index = 0; index < SuperGame::BoardCellCount; ++index) {
            trogglePresentation.core_.board_.cells[static_cast<std::size_t>(index)].text =
                std::string(CapturedTroggleWords[static_cast<std::size_t>(index)]);
        }
        trogglePresentation.playerRow_ = 3;
        trogglePresentation.playerColumn_ = 1;
        trogglePresentation.playerTerminalFrame_ = 7;
        trogglePresentation.moving_ = false;
        trogglePresentation.munching_ = false;
        trogglePresentation.transforming_ = false;
        trogglePresentation.safeCells_.fill(false);
        trogglePresentation.enemyWarning_ = false;
        trogglePresentation.enemySlotCount_ = 1;
        trogglePresentation.enemies_.clear();
        SuperGame::Enemy capturedTroggle;
        capturedTroggle.type = 0;
        capturedTroggle.slot = 0;
        capturedTroggle.row = capturedTroggle.fromRow = 0;
        capturedTroggle.column = capturedTroggle.fromColumn = 4;
        capturedTroggle.direction = 3;
        capturedTroggle.entering = false;
        capturedTroggle.moving = false;
        trogglePresentation.enemies_.push_back(capturedTroggle);
        Renderer troggleRenderer(GraphicsMode::Vga256);
        trogglePresentation.render(troggleRenderer);
        maybeDumpFrame(troggleRenderer, "super-vga-troggle-frame-native.ppm");
        if (fullFrameHash(troggleRenderer) != 0x4eb8736860f8f311ull) return false;

        const int smartyCell = game.playerRow_ * SuperGame::BoardColumns +
            ((game.playerColumn_ + 2) % SuperGame::BoardColumns);
        const SuperBoardCell smartySaved =
            game.core_.board().cells[static_cast<std::size_t>(smartyCell)];
        if (!game.core_.clearCellWithoutScore(static_cast<std::size_t>(smartyCell))) {
            return false;
        }
        const auto randomBeforeSmarty = std::pair{game.random_.state, game.random_.calls};
        SuperGame::Enemy smarty;
        smarty.row = smartyCell / SuperGame::BoardColumns;
        smarty.column = smartyCell % SuperGame::BoardColumns;
        smarty.type = 4;
        smarty.savedCell = smartySaved;
        smarty.savedCellEaten = false;
        smarty.savedCellValid = true;
        if (!game.applyEnemyTrail(smarty)) return false;
        if (game.core_.eaten(static_cast<std::size_t>(smartyCell)) ||
            game.core_.board().cells[static_cast<std::size_t>(smartyCell)] != smartySaved ||
            std::pair{game.random_.state, game.random_.calls} != randomBeforeSmarty) {
            std::cerr << "Super Smarty trail restore regressed\n";
            return false;
        }

        int chewCell = -1;
        for (int index = 0; index < SuperGame::BoardCellCount; ++index) {
            if (!game.core_.eaten(static_cast<std::size_t>(index)) &&
                index != game.core_.transformationCell() &&
                game.core_.board().cells[static_cast<std::size_t>(index)].signedSourceIndex != 0) {
                chewCell = index;
                break;
            }
        }
        if (chewCell < 0) return false;
        game.playerRow_ = chewCell / SuperGame::BoardColumns;
        game.playerColumn_ = chewCell % SuperGame::BoardColumns;
        static constexpr std::array<int, 7> ExpectedChewFrames = {
            12, 13, 14, 13, 12, 13, 14
        };
        game.keyDown(VK_SPACE);
        if (!game.munching_ || game.visiblePlayerFrame() != ExpectedChewFrames[0]) {
            return false;
        }
        for (std::size_t tick = 1; tick < ExpectedChewFrames.size(); ++tick) {
            game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
            if (!game.munching_ || game.visiblePlayerFrame() != ExpectedChewFrames[tick]) {
                return false;
            }
        }
        game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        if (game.munching_) {
            std::cerr << "Super chew did not resolve\n";
            return false;
        }

        for (int frame = 0; frame < SuperGame::TransformationAnimationTicks; ++frame) {
            if (!game.assets_.spriteFrame(1021, frame)) return false;
        }
        if (!game.assets_.spriteFrame(1013, 3) ||
            !game.assets_.spriteFrame(1013, 5) ||
            !game.assets_.spriteFrame(1013, 12)) return false;
        game.transforming_ = true;
        game.transformationAnimationTimer_ =
            static_cast<double>(SuperGame::TransformationAnimationTicks) /
            SuperGame::SchedulerTicksPerSecond;
        for (int frame = 0; frame < SuperGame::TransformationAnimationTicks; ++frame) {
            if (!game.transforming_ || game.visibleTransformationFrame() != frame) {
                std::cerr << "Super transform frame expected=" << frame << " got="
                          << game.visibleTransformationFrame() << " active="
                          << game.transforming_ << '\n';
                return false;
            }
            game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        }
        if (game.transforming_ || game.visibleTransformationFrame() != -1) {
            std::cerr << "Super transformation terminal regressed\n";
            return false;
        }

        game.toggleCheatMenu();
        if (!game.cheatOpen_) return false;
        game.menuSelection_ = 9;
        game.keyDown(VK_RETURN);
        if (game.cheatOpen_ || game.page_ != SuperGamePage::Playing ||
            game.core_.level() != 9) {
            std::cerr << "Super cheat start regressed\n";
            return false;
        }

        // Isolate arrival cadence from an incidental seeded player overlap.
        game.collisionActive_ = true;
        game.collisionSlot_ = -1;
        game.collisionTimer_ = 1000.0;
        game.update(8.0);
        if (game.enemies_.empty() || !game.enemyWarning_) {
            std::cerr << "Super enemy warning stage count=" << game.enemies_.size()
                      << " warning=" << game.enemyWarning_ << '\n';
            return false;
        }
        game.update(4.0);
        if (game.enemies_.empty() ||
            std::none_of(game.enemySlots_.begin(),
                         game.enemySlots_.begin() + game.enemySlotCount_,
                         [](const SuperGame::EnemySlot& slot) {
                             return slot.phase == SuperGame::EnemySlotPhase::Active;
                         })) {
            std::cerr << "Super enemy arrival stage count=" << game.enemies_.size()
                      << " page=" << static_cast<int>(game.page_) << '\n';
            return false;
        }
        game.collisionActive_ = false;
        game.collisionTimer_ = 0.0;

        game.enemies_.clear();
        SuperGame::Enemy biter;
        biter.row = game.playerRow_;
        biter.column = game.playerColumn_;
        biter.fromRow = biter.row;
        biter.fromColumn = biter.column;
        biter.type = 0;
        biter.direction = 1;
        biter.slot = 0;
        game.enemies_.push_back(biter);
        const int reservesBeforeCollision = game.core_.scoreState().reserves();
        game.collideWithEnemy(0);
        if (!game.collisionActive_ || game.page_ != SuperGamePage::Playing ||
            game.core_.scoreState().reserves() != reservesBeforeCollision) {
            std::cerr << "Super collision did not enter deferred bite\n";
            return false;
        }
        constexpr std::array<int, 6> BiteFrames = {12, 13, 14, 13, 12, 13};
        for (int tick = 0; tick < 21; ++tick) {
            if (!game.collisionActive_ ||
                game.enemyFrame(game.enemies_.front()) !=
                    BiteFrames[static_cast<std::size_t>(tick % BiteFrames.size())] ||
                game.core_.scoreState().reserves() != reservesBeforeCollision) {
                std::cerr << "Super collision tick=" << tick << " active="
                          << game.collisionActive_ << " frame="
                          << game.enemyFrame(game.enemies_.front()) << " reserves="
                          << game.core_.scoreState().reserves() << '\n';
                return false;
            }
            game.update(1.0 / SuperGame::SchedulerTicksPerSecond);
        }
        if (game.collisionActive_ || game.page_ != SuperGamePage::Feedback ||
            game.core_.scoreState().reserves() != reservesBeforeCollision - 1) {
            std::cerr << "Super collision terminal active/page/reserves="
                      << game.collisionActive_ << '/' << static_cast<int>(game.page_)
                      << '/' << game.core_.scoreState().reserves() << '\n';
            return false;
        }

        Renderer renderer(GraphicsMode::Vga256);
        game.render(renderer);
        const std::set<std::uint32_t> colors(renderer.pixels().begin(),
                                             renderer.pixels().end());
        if (!colors.contains(Colors::BoardBlue) ||
            !colors.contains(Colors::Magenta) ||
            !colors.contains(Colors::White)) return false;

        auto& score = const_cast<MuncherScore&>(game.core_.scoreState());
        score.restore(100, 3);
        const std::size_t priorHallSize = game.halls_[0].size();
        game.selectedGame_ = 0;
        game.beginPostGame();
        if (game.page_ != SuperGamePage::NameEntry) return false;
        game.character(L'B');
        game.character(L'o');
        game.character(L'b');
        game.keyDown(VK_RETURN);
        if (game.page_ != SuperGamePage::Hall ||
            game.halls_[0].size() != priorHallSize + 1 ||
            game.halls_[0].front() != SuperGame::HallEntry{"Bob", 100}) return false;
        game.keyDown(VK_SPACE);
        if (game.page_ != SuperGamePage::ReplayQuestion) return false;
        game.keyDown(VK_RIGHT);
        game.keyDown(VK_RETURN);
        return game.page_ == SuperGamePage::Title;
    }

    static bool missionPresentationMatches() {
        constexpr std::array<int, 5> ExpectedTicks = {59, 104, 180, 321, 185};
        constexpr std::array<std::uint64_t, 5> ExpectedTimelineHashes = {
            0xe4104a0d877c2ad0ull, 0xface8e5f026a6a61ull,
            0xc7c91c8dfd51ca94ull, 0x87fcc7058e9870a4ull,
            0xf9aee02640d7e011ull,
        };
        std::array<std::uint64_t, 5> timelineHashes{};
        for (int mission = 0; mission < 5; ++mission) {
            SuperGame game(GraphicsMode::Vga256,
                           static_cast<std::uint16_t>(0x7100 + mission), false);
            game.soundOn_ = false;
            game.musicOn_ = false;
            game.startGame(0, mission + 1);
            if (!game.startMission(mission)) return false;
            game.page_ = SuperGamePage::LevelComplete;
            std::uint64_t timeline = 1469598103934665603ull;
            int ticks = 0;
            while (game.missionScene_.valid() &&
                   !game.missionScene_.finished() && ticks < 1000) {
                game.advanceMissionTick();
                ++ticks;
                std::uint64_t frame = 1469598103934665603ull;
                for (const std::uint32_t pixel : game.missionSurfacePixels_) {
                    frame ^= pixel;
                    frame *= 1099511628211ull;
                }
                for (int shift = 0; shift < 64; shift += 8) {
                    timeline ^= static_cast<std::uint8_t>(frame >> shift);
                    timeline *= 1099511628211ull;
                }
            }
            if (ticks != ExpectedTicks[static_cast<std::size_t>(mission)] ||
                !game.missionScene_.finished()) return false;
            timelineHashes[static_cast<std::size_t>(mission)] = timeline;
        }
        if (timelineHashes != ExpectedTimelineHashes) {
            std::cerr << "Super mission painter hashes:";
            for (const std::uint64_t hash : timelineHashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        return true;
    }

    static bool missionCadenceAndInteractionRoute() {
        const auto completeBoard = [](SuperGame& game) {
            std::vector<std::size_t> correct;
            for (std::size_t index = 0; index < game.core_.board().cells.size(); ++index) {
                if (game.core_.board().cells[index].correct && !game.core_.eaten(index)) {
                    correct.push_back(index);
                }
            }
            if (correct.empty()) return false;
            while (correct.size() > 1) {
                const std::size_t cell = correct.front();
                correct.erase(correct.begin());
                const SuperMunchResolution result = game.core_.munchCell(cell);
                if (result.kind != SuperMunchKind::Correct || result.boardComplete) {
                    return false;
                }
            }
            const std::size_t final = correct.front();
            if (!game.core_.beginMunchCell(final)) return false;
            game.munchCellIndex_ = static_cast<int>(final);
            game.munching_ = true;
            game.resolveMunch();
            return game.lastResolution_.boardComplete;
        };

        SuperGame cadence(GraphicsMode::Vga256, 0x7200u, false);
        cadence.soundOn_ = false;
        cadence.musicOn_ = false;
        cadence.startGame(0);
        for (int level = 1; level <= 3; ++level) {
            if (!completeBoard(cadence)) return false;
            if (level < 3) {
                if (cadence.page_ != SuperGamePage::LevelComplete ||
                    cadence.missionIndex_ != -1 ||
                    cadence.levelsSinceMission_ != level) return false;
                cadence.keyDown(VK_RETURN);
                if (cadence.page_ != SuperGamePage::Playing ||
                    cadence.core_.level() != level + 1) return false;
            }
        }
        if (cadence.page_ != SuperGamePage::MissionIntro ||
            cadence.missionIndex_ != 0 || cadence.levelsSinceMission_ != 0) {
            std::cerr << "Super mission cadence page/index/count="
                      << static_cast<int>(cadence.page_) << '/'
                      << cadence.missionIndex_ << '/' << cadence.levelsSinceMission_ << '\n';
            return false;
        }
        Renderer renderer(GraphicsMode::Vga256);
        cadence.render(renderer);
        if (std::none_of(renderer.pixels().begin(), renderer.pixels().end(),
                         [](const std::uint32_t pixel) { return pixel == Colors::Yellow; })) {
            return false;
        }
        cadence.keyDown(VK_RETURN);
        if (cadence.page_ != SuperGamePage::LevelComplete) return false;
        while (!cadence.missionScene_.finished()) cadence.advanceMissionTick();
        cadence.finishMission();
        if (cadence.page_ != SuperGamePage::MissionQuestion ||
            cadence.missionChoices_.size() != 4 ||
            cadence.missionCorrectSelection_ < 0 ||
            cadence.missionCorrectSelection_ >= 4) return false;
        cadence.pointerMove(160, 96 + cadence.missionCorrectSelection_ * 18);
        if (cadence.menuSelection_ != cadence.missionCorrectSelection_) return false;
        cadence.keyDown(VK_RETURN);
        if (cadence.page_ != SuperGamePage::MissionResult ||
            !cadence.missionSucceeded_ || cadence.missionProgress_ != 1) return false;
        cadence.render(renderer);
        cadence.keyDown(VK_SPACE);
        if (cadence.page_ != SuperGamePage::Playing || cadence.core_.level() != 4 ||
            cadence.missionIndex_ != -1) return false;

        SuperGame retry(GraphicsMode::Vga256, 0x7201u, false);
        retry.soundOn_ = false;
        retry.musicOn_ = false;
        retry.startGame(0);
        if (!retry.startMission(1)) return false;
        retry.page_ = SuperGamePage::MissionIntro;
        retry.keyDown(VK_RETURN);
        while (!retry.missionScene_.finished()) retry.advanceMissionTick();
        retry.finishMission();
        if (retry.page_ != SuperGamePage::MissionQuestion ||
            retry.missionChoices_ != std::vector<std::string>{"tree", "bush", "stump", "rock"}) {
            return false;
        }
        retry.menuSelection_ = (retry.missionCorrectSelection_ + 1) % 4;
        retry.keyDown(VK_RETURN);
        if (retry.page_ != SuperGamePage::MissionResult ||
            retry.missionSucceeded_ || retry.missionProgress_ != 0) return false;

        SuperGame password(GraphicsMode::Vga256, 0x7202u, false);
        password.soundOn_ = false;
        password.startGame(0);
        if (!password.startMission(2)) return false;
        password.prepareMissionQuestion();
        if (password.missionChoices_.size() != 4 ||
            std::set<std::string>(password.missionChoices_.begin(),
                                  password.missionChoices_.end()).size() != 4 ||
            password.missionCorrectSelection_ < 0 ||
            password.missionCorrectSelection_ >= 4) return false;

        SuperGame catcher(GraphicsMode::Vga256, 0x7203u, false);
        catcher.soundOn_ = false;
        catcher.startGame(0);
        if (!catcher.startMission(3)) return false;
        catcher.page_ = SuperGamePage::LevelComplete;
        const int initialX = catcher.missionPlayerX_;
        catcher.keyDown(VK_LEFT);
        if (catcher.missionPlayerX_ != initialX - 8) return false;
        catcher.pointerMove(300, 170);
        if (catcher.missionPlayerX_ != 275) return false;
        catcher.joystickEnabled_ = true;
        catcher.setJoystickState(true, 0x1000u, 0x8000u, 0);
        if (catcher.missionPlayerX_ != 267) return false;
        catcher.render(renderer);
        catcher.missionCatchFailed_ = true;
        catcher.missionCatchFailureReason_ = 2;
        catcher.finishMission();
        if (catcher.page_ != SuperGamePage::MissionResult ||
            catcher.missionSucceeded_ || catcher.missionProgress_ != 0) return false;

        SuperGame finale(GraphicsMode::Vga256, 0x7204u, false);
        finale.soundOn_ = false;
        finale.startGame(0);
        finale.missionProgress_ = 4;
        if (!finale.startMission(4)) return false;
        finale.finishMission();
        return finale.page_ == SuperGamePage::MissionResult &&
               finale.missionSucceeded_ && finale.missionProgress_ == 0;
    }

    static bool missionTextFramesMatch() {
        const auto hashFrame = [](const Renderer& renderer) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const std::uint32_t pixel : renderer.pixels()) {
                hash ^= pixel;
                hash *= 1099511628211ull;
            }
            return hash;
        };

        SuperGame game(GraphicsMode::Vga256, 0x72f0u, false);
        game.soundOn_ = false;
        game.musicOn_ = false;
        Renderer renderer(GraphicsMode::Vga256);
        std::array<std::uint64_t, 17> hashes{};
        std::size_t output = 0;
        for (int mission = 0; mission < 5; ++mission) {
            game.missionIndex_ = mission;
            game.page_ = SuperGamePage::MissionIntro;
            game.render(renderer);
            hashes[output++] = hashFrame(renderer);
        }
        for (int mission = 0; mission < 5; ++mission) {
            game.missionIndex_ = mission;
            game.missionSucceeded_ = true;
            game.page_ = SuperGamePage::MissionResult;
            game.render(renderer);
            hashes[output++] = hashFrame(renderer);
        }
        const std::array<std::vector<std::string>, 3> choices = {{
            {"Bashful", "Worker", "Reggie", "Smartie"},
            {"tree", "bush", "stump", "rock"},
            {"bear", "frog", "lion", "skunk"},
        }};
        const std::array<int, 3> selections = {2, 3, 1};
        for (int mission = 0; mission < 3; ++mission) {
            game.missionIndex_ = mission;
            game.missionSucceeded_ = false;
            game.missionChoices_ = choices[static_cast<std::size_t>(mission)];
            game.missionCorrectSelection_ = selections[static_cast<std::size_t>(mission)];
            game.render(renderer);
            hashes[output++] = hashFrame(renderer);
        }
        game.missionIndex_ = 3;
        game.missionSucceeded_ = false;
        for (int reason = 0; reason < 4; ++reason) {
            game.missionCatchFailureReason_ = reason;
            game.render(renderer);
            hashes[output++] = hashFrame(renderer);
        }

        constexpr std::array<std::uint64_t, 17> Expected = {
            0x50779cb767c9c7d4ull, 0xe391cc7d05db2d95ull,
            0x27e2a55e8df0eaacull, 0xae2384e7f396fd10ull,
            0x62f83782cbe413e0ull, 0x98c498655320e160ull,
            0x5967d94418964b96ull, 0x4c8d1267f5e30802ull,
            0xa1bdfa2437569688ull, 0xef1151093c02c92aull,
            0xb24cb277d36d3ad5ull, 0x2902ff34ac032b15ull,
            0x93a43018ad98a34bull, 0xb78a05f36a88c991ull,
            0x46f4c0ad0297cb6bull, 0x7a57243bf9905945ull,
            0x1aaab37c5c800fddull,
        };
        if (hashes != Expected) {
            std::cerr << "Super mission DATA2 text frame hashes:";
            for (const std::uint64_t hash : hashes) {
                std::cerr << " 0x" << std::hex << hash;
            }
            std::cerr << std::dec << '\n';
            return false;
        }
        return true;
    }

    static bool safeZoneAndTroggleLifecycleRoute() {
        SuperGame safe(GraphicsMode::Vga256, 0x7300u, false);
        safe.soundOn_ = false;
        safe.startGame(0, 1);
        if (safe.safeZoneJobCount_ != 2 || !safe.safeZoneJobs_[0].active ||
            safe.safeZoneJobs_[1].active ||
            safe.safeZoneJobs_[0].cellIndex < 0) return false;
        const int activeCell = safe.safeZoneJobs_[0].cellIndex;
        const int activeRow = activeCell / SuperGame::BoardColumns;
        const int activeColumn = activeCell % SuperGame::BoardColumns;
        if (activeRow < 1 || activeRow > 3 || activeColumn < 1 || activeColumn > 4 ||
            !safe.safeAt(activeRow, activeColumn)) return false;
        Renderer renderer(GraphicsMode::Vga256);
        safe.render(renderer);
        const int markerX = SuperGame::BoardLeft + activeColumn * SuperGame::BoardCellWidth + 1;
        const int markerY = SuperGame::BoardTop + activeRow * SuperGame::BoardCellHeight + 1;
        if (renderer.pixels()[static_cast<std::size_t>(markerY * Renderer::Width + markerX)] !=
            Colors::White) return false;

        SuperGame protectedMove(GraphicsMode::Vga256, 0x7301u, false);
        protectedMove.soundOn_ = false;
        protectedMove.startGame(0, 5);
        protectedMove.safeCells_.fill(false);
        SuperGame::Enemy walker;
        walker.row = 2;
        walker.column = 2;
        walker.direction = 1;
        walker.type = 0;
        protectedMove.safeCells_[static_cast<std::size_t>(2 * SuperGame::BoardColumns + 3)] = true;
        protectedMove.chooseEnemyMove(walker);
        if (!walker.moving || (walker.row == 2 && walker.column == 3) ||
            protectedMove.safeAt(walker.row, walker.column)) return false;

        SuperGame activation(GraphicsMode::Vga256, 0x7302u, false);
        activation.soundOn_ = false;
        activation.startGame(0, 1);
        activation.enemies_.clear();
        activation.enemySlotCount_ = 1;
        activation.enemySlots_[0].phase = SuperGame::EnemySlotPhase::Active;
        SuperGame::Enemy crossing;
        crossing.slot = 0;
        crossing.type = 4;
        crossing.row = 2;
        crossing.column = 2;
        crossing.fromRow = 2;
        crossing.fromColumn = 1;
        crossing.moving = true;
        crossing.savedCell = activation.core_.board().cells[14];
        crossing.savedCellEaten = activation.core_.eaten(14);
        crossing.savedCellValid = true;
        activation.enemies_.push_back(crossing);
        activation.safeCells_.fill(false);
        activation.safeZoneJobCount_ = 1;
        activation.safeZoneJobs_[0].active = false;
        activation.safeZoneJobs_[0].cellIndex = -1;
        activation.safeZoneJobs_[0].period = 1.0;
        activation.safeZoneJobs_[0].timer = 0.0;
        // Fix the selector's next candidate so the crossing actor is caught.
        activation.safeCells_.fill(true);
        activation.safeCells_[14] = false;
        if (!activation.updateSafeZones(0.0) || !activation.enemies_.empty() ||
            activation.enemySlots_[0].phase != SuperGame::EnemySlotPhase::Waiting ||
            !activation.safeCells_[14]) return false;

        SuperGame cannibal(GraphicsMode::Vga256, 0x7303u, false);
        cannibal.soundOn_ = false;
        cannibal.startGame(0, 9);
        cannibal.enemies_.clear();
        cannibal.enemySlotCount_ = 2;
        for (int slot = 0; slot < 2; ++slot) {
            cannibal.enemySlots_[static_cast<std::size_t>(slot)].phase =
                SuperGame::EnemySlotPhase::Active;
        }
        SuperGame::Enemy resident;
        resident.slot = 0;
        resident.type = 0;
        resident.row = 2;
        resident.column = 2;
        resident.timer = 10.0;
        SuperGame::Enemy mover = resident;
        mover.slot = 1;
        mover.type = 1;
        mover.fromRow = 2;
        mover.fromColumn = 1;
        mover.moving = true;
        mover.animationTimer = 1.0 / SuperGame::SchedulerTicksPerSecond;
        cannibal.enemies_.push_back(resident);
        cannibal.enemies_.push_back(mover);
        cannibal.updateEnemies(1.0 / SuperGame::SchedulerTicksPerSecond);
        const auto survivor = std::find_if(cannibal.enemies_.begin(), cannibal.enemies_.end(),
            [](const SuperGame::Enemy& enemy) { return enemy.slot == 1; });
        if (survivor == cannibal.enemies_.end() || !survivor->cannibalizing ||
            cannibal.enemies_.size() != 2) return false;
        cannibal.updateEnemies(21.0 / SuperGame::SchedulerTicksPerSecond);
        if (cannibal.enemies_.size() != 1 || cannibal.enemies_.front().slot != 1 ||
            cannibal.enemies_.front().cannibalizing ||
            cannibal.enemySlots_[0].phase != SuperGame::EnemySlotPhase::Waiting) {
            return false;
        }

        SuperGame recovery(GraphicsMode::Vga256, 0x7304u, false);
        recovery.soundOn_ = false;
        recovery.startGame(0, 5);
        recovery.enemies_.clear();
        recovery.enemySlotCount_ = 1;
        recovery.enemySlots_[0].phase = SuperGame::EnemySlotPhase::Active;
        SuperGame::Enemy biter;
        biter.slot = 0;
        biter.row = recovery.playerRow_;
        biter.column = recovery.playerColumn_;
        recovery.enemies_.push_back(biter);
        recovery.collideWithEnemy(0);
        recovery.update(21.0 / SuperGame::SchedulerTicksPerSecond);
        if (recovery.page_ != SuperGamePage::Feedback) return false;
        recovery.keyDown(VK_RETURN);
        if (recovery.page_ != SuperGamePage::Playing || !recovery.playerRecovering_) {
            return false;
        }
        recovery.enemies_.front().timer = 0.0;
        recovery.updateEnemies(0.0);
        return !recovery.playerRecovering_;
    }

    static bool pauseAliasesAndAnswerVisionRoute() {
        SuperGame pause(GraphicsMode::Vga256, 0x7400u, false);
        pause.soundOn_ = false;
        pause.startGame(0, 1);
        pause.playerRow_ = 2;
        pause.playerColumn_ = 2;
        pause.keyDown(VK_RIGHT);
        if (!pause.moving_) return false;
        const double timer = pause.moveTimer_;
        pause.keyDown(VK_RETURN);
        if (pause.page_ != SuperGamePage::Paused) return false;
        pause.update(1.0);
        if (!pause.moving_ || pause.moveTimer_ != timer) return false;
        pause.keyDown(VK_RETURN);
        pause.update(SuperGame::movementDuration(1));
        if (pause.page_ != SuperGamePage::Playing || pause.moving_ ||
            pause.playerColumn_ != 3) return false;

        const auto aliasMoves = [](const UINT key, const int row, const int column) {
            SuperGame game(GraphicsMode::Vga256,
                           static_cast<std::uint16_t>(0x7410u + key), false);
            game.soundOn_ = false;
            game.startGame(0, 1);
            game.playerRow_ = 2;
            game.playerColumn_ = 2;
            game.keyDown(key);
            game.update(SuperGame::movementDuration(
                row < 2 ? 0 : column > 2 ? 1 : row > 2 ? 2 : 3));
            return game.playerRow_ == row && game.playerColumn_ == column;
        };
        for (const UINT key : {'I', 'A'}) if (!aliasMoves(key, 1, 2)) return false;
        if (!aliasMoves('J', 2, 1) || !aliasMoves('K', 2, 3)) return false;
        for (const UINT key : {'M', 'Z'}) if (!aliasMoves(key, 3, 2)) return false;

        SuperGame vision(GraphicsMode::Vga256, 0x7420u, false);
        vision.soundOn_ = false;
        vision.startGame(0, 1);
        vision.playerRow_ = 2;
        vision.playerColumn_ = 2;
        vision.core_.superForm_ = true;
        vision.core_.transformationMeter_ = 10;
        vision.core_.answerVisionOption_ = true;
        vision.keyDown('V');
        if (!vision.core_.answerVision() || !vision.core_.actionFrozen()) return false;
        vision.update(5.0);
        if (!vision.core_.answerVision() || vision.core_.transformationMeter_ != 10 ||
            vision.moving_) return false;
        vision.keyDown(VK_RIGHT);
        if (vision.core_.answerVision() || vision.moving_) return false;
        vision.keyDown(VK_RIGHT);
        if (!vision.moving_) return false;

        SuperGame pointerVision(GraphicsMode::Vga256, 0x7421u, false);
        pointerVision.soundOn_ = false;
        pointerVision.startGame(0, 1);
        pointerVision.core_.superForm_ = true;
        pointerVision.core_.transformationMeter_ = 10;
        pointerVision.core_.answerVisionOption_ = true;
        pointerVision.keyDown('V');
        pointerVision.pointerButton(100, 100, false);
        return !pointerVision.core_.answerVision() &&
               pointerVision.inputQueue_.empty();
    }
};

namespace {

std::uint64_t regionHash(const Renderer& renderer, const int left, const int top,
                         const int right, const int bottom) {
    std::uint64_t hash = 1469598103934665603ull;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            hash ^= renderer.pixels()[static_cast<std::size_t>(y * Renderer::Width + x)];
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

struct IndexedPcxAudit {
    int width{};
    int height{};
    int stride{};
    std::vector<std::uint8_t> indexes;
    std::array<std::array<std::uint8_t, 3>, 256> palette{};
    bool valid{};
};

IndexedPcxAudit decodeIndexedPcxForAudit(const BlobView blob) {
    IndexedPcxAudit result;
    const auto word = [&blob](const std::size_t offset) {
        return static_cast<std::uint16_t>(blob.data[offset]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(blob.data[offset + 1]) << 8);
    };
    if (!blob || blob.size < 128 + 769 || blob.data[0] != 10 ||
        blob.data[2] != 1 || blob.data[3] != 8 || blob.data[65] != 1 ||
        blob.data[blob.size - 769] != 12) {
        return result;
    }
    result.width = word(8) - word(4) + 1;
    result.height = word(10) - word(6) + 1;
    result.stride = word(66);
    if (result.width <= 0 || result.height <= 0 || result.stride < result.width) return result;

    const std::size_t expected = static_cast<std::size_t>(result.stride) * result.height;
    std::vector<std::uint8_t> rows;
    rows.reserve(expected);
    std::size_t position = 128;
    const std::size_t dataEnd = blob.size - 769;
    while (rows.size() < expected && position < dataEnd) {
        std::uint8_t value = blob.data[position++];
        std::size_t count = 1;
        if ((value & 0xc0u) == 0xc0u) {
            count = value & 0x3fu;
            if (position >= dataEnd) return result;
            value = blob.data[position++];
        }
        rows.insert(rows.end(), std::min(count, expected - rows.size()), value);
    }
    if (rows.size() != expected) return result;

    result.indexes.reserve(static_cast<std::size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
        const auto row = rows.begin() + static_cast<std::size_t>(y) * result.stride;
        result.indexes.insert(result.indexes.end(), row, row + result.width);
    }
    const std::size_t paletteOffset = blob.size - 768;
    for (std::size_t index = 0; index < result.palette.size(); ++index) {
        std::copy_n(blob.data + paletteOffset + index * 3, 3, result.palette[index].begin());
    }
    result.valid = true;
    return result;
}

bool wordPagePaletteSourcesMatch() {
    const ResArchive wordArchive(loadEmbeddedResource(IDR_WMCGA_RES));
    const ResArchive numberArchive(loadEmbeddedResource(IDR_NMCGA_RES));
    if (!wordArchive.valid() || !numberArchive.valid()) {
        std::cerr << "page palette: archive\n";
        return false;
    }

    const BlobView wordHall = wordArchive.find("PCXF", 6003);
    const BlobView numberHall = numberArchive.find("PCXF", 6003);
    if (!wordHall || wordHall.size != numberHall.size ||
        !std::equal(wordHall.data, wordHall.data + wordHall.size, numberHall.data)) {
        std::cerr << "page palette: hall bytes\n";
        return false;
    }

    const IndexedPcxAudit wordTitle = decodeIndexedPcxForAudit(wordArchive.find("PCXF", 6009));
    const IndexedPcxAudit numberTitle = decodeIndexedPcxForAudit(numberArchive.find("PCXF", 6009));
    const IndexedPcxAudit wordSplash = decodeIndexedPcxForAudit(wordArchive.find("PCXF", 6005));
    const IndexedPcxAudit numberSplash = decodeIndexedPcxForAudit(numberArchive.find("PCXF", 6005));
    const IndexedPcxAudit wordHallImage = decodeIndexedPcxForAudit(wordHall);
    const IndexedPcxAudit numberHallImage = decodeIndexedPcxForAudit(numberHall);
    const IndexedPcxAudit wordPlayer = decodeIndexedPcxForAudit(wordArchive.find("PCXF", 1006));
    const IndexedPcxAudit numberPlayer = decodeIndexedPcxForAudit(numberArchive.find("PCXF", 1006));
    if (!wordTitle.valid || !numberTitle.valid || !wordSplash.valid || !numberSplash.valid ||
        !wordHallImage.valid || !numberHallImage.valid ||
        !wordPlayer.valid || !numberPlayer.valid ||
        wordTitle.palette != numberTitle.palette || wordSplash.width != 320 ||
        wordSplash.height != 200 || numberSplash.width != 320 || numberSplash.height != 200) {
        std::cerr << "page palette: decode/title palette/dimensions "
                  << wordTitle.valid << numberTitle.valid << wordSplash.valid << numberSplash.valid
                  << " titleEqual=" << (wordTitle.palette == numberTitle.palette)
                  << " dimensions=" << wordSplash.width << 'x' << wordSplash.height << ','
                  << numberSplash.width << 'x' << numberSplash.height << '\n';
        return false;
    }

    // Index 111 is the darkest occupied Muncher slot isolated by Number's
    // later-Demo capture. Word shares both the player source and every page
    // palette value at that index, so the captured carry-over applies without
    // a Word-specific color inference.
    constexpr std::size_t MuncherDarkIndex = 111;
    constexpr std::array<std::uint8_t, 3> PlayerDarkSource = {4, 64, 0};
    constexpr std::array<std::uint8_t, 3> PageDarkSource = {7, 67, 0};
    if (wordPlayer.palette[MuncherDarkIndex] != PlayerDarkSource ||
        numberPlayer.palette[MuncherDarkIndex] != PlayerDarkSource ||
        wordHallImage.palette[MuncherDarkIndex] != PageDarkSource ||
        numberHallImage.palette[MuncherDarkIndex] != PageDarkSource ||
        wordSplash.palette[MuncherDarkIndex] != PageDarkSource ||
        numberSplash.palette[MuncherDarkIndex] != PageDarkSource ||
        wordTitle.palette[MuncherDarkIndex] != PageDarkSource ||
        numberTitle.palette[MuncherDarkIndex] != PageDarkSource) {
        std::cerr << "page palette: shared Muncher index 111\n";
        return false;
    }

    const auto countIndexInRect = [](const IndexedPcxAudit& image,
                                     const int left, const int top,
                                     const int right, const int bottom,
                                     const std::uint8_t needle) {
        int count = 0;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                if (image.indexes[static_cast<std::size_t>(y * image.width + x)] == needle) {
                    ++count;
                }
            }
        }
        return count;
    };
    // The measured slot occurs once in open-mouth record 12 but not at all in
    // reserve record 17. The VGA DAC change is global, yet the life icons are
    // therefore correctly unchanged on a post-Hall user replay.
    if (wordPlayer.width != 211 || wordPlayer.height != 179 ||
        numberPlayer.width != 211 || numberPlayer.height != 179 ||
        countIndexInRect(wordPlayer, 132, 137, 177, 167, MuncherDarkIndex) != 1 ||
        countIndexInRect(numberPlayer, 132, 137, 177, 167, MuncherDarkIndex) != 1 ||
        countIndexInRect(wordPlayer, 16, 140, 52, 179, MuncherDarkIndex) != 0 ||
        countIndexInRect(numberPlayer, 16, 140, 52, 179, MuncherDarkIndex) != 0) {
        std::cerr << "page palette: active-player/reserve index-111 occupancy\n";
        return false;
    }

    std::array<bool, 256> wordUsed{};
    std::array<bool, 256> numberUsed{};
    for (const std::uint8_t index : wordSplash.indexes) wordUsed[index] = true;
    for (const std::uint8_t index : numberSplash.indexes) numberUsed[index] = true;
    int paletteDifferences = 0;
    for (std::size_t index = 0; index < wordSplash.palette.size(); ++index) {
        if (wordUsed[index] && wordSplash.palette[index] != numberSplash.palette[index]) {
            std::cerr << "page palette: used difference " << index << '\n';
            return false;
        }
        if (wordSplash.palette[index] != numberSplash.palette[index]) {
            ++paletteDifferences;
            if (index != 252 || wordUsed[index]) {
                std::cerr << "page palette: unexpected difference " << index
                          << " wordUsed=" << wordUsed[index]
                          << " numberUsed=" << numberUsed[index] << '\n';
                return false;
            }
        }
    }

    constexpr std::array<std::uint8_t, 3> MissingRampSource = {7, 207, 0};
    const auto countRampPixels = [&MissingRampSource](const IndexedPcxAudit& image) {
        return static_cast<int>(std::count_if(
            image.indexes.begin(), image.indexes.end(),
            [&image, &MissingRampSource](const std::uint8_t index) {
                return image.palette[index] == MissingRampSource;
            }));
    };
    const int wordRamp = countRampPixels(wordSplash);
    const int numberSplashRamp = countRampPixels(numberSplash);
    const int numberTitleRamp = countRampPixels(numberTitle);
    if (paletteDifferences != 1 || wordRamp != 121 || numberSplashRamp != 0 ||
        numberTitleRamp != 0) {
        std::cerr << "page palette: summary differences=" << paletteDifferences
                  << " ramps=" << wordRamp << ',' << numberSplashRamp << ','
                  << numberTitleRamp << '\n';
        return false;
    }
    return true;
}

bool wordTitleAndInputRoutesMatch() {
    if (!wordPagePaletteSourcesMatch()) {
        std::cerr << "word runtime stage: raw page palettes\n";
        return false;
    }
    WordGame game(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::spriteSourcesAndMuncherPaletteMatch(game)) {
        std::cerr << "word runtime stage: sprite source/palette\n";
        return false;
    }
    if (!WordGameTestAccess::usesSharedHallPaletteCarryover()) {
        std::cerr << "word runtime stage: Hall palette carry-over\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredUserBoardPresenter()) {
        std::cerr << "word runtime stage: ordinary user board presenter\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredCgaCartoonPresenter()) {
        std::cerr << "word runtime stage: CGA cartoon presenter\n";
        return false;
    }
    Renderer renderer;
    if (game.page() != WordGamePage::StartupVersion || game.shouldQuit()) return false;
    game.render(renderer);
    const std::uint64_t startupVersionHash = regionHash(renderer, 0, 0, 320, 200);
    constexpr double originalStartupClock =
        1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);
    WordGame startupTimeout(GraphicsMode::Vga256, 0x1234);
    startupTimeout.update(299.0 / originalStartupClock);
    if (startupTimeout.page() != WordGamePage::StartupVersion) return false;
    startupTimeout.update(1.0 / originalStartupClock);
    if (startupTimeout.page() != WordGamePage::StartupSplash) return false;
    startupTimeout.update(60.0);
    if (startupTimeout.page() != WordGamePage::StartupSplash) return false;
    // TranslateMessage produces a second message for character keys. Enter's
    // trailing control byte must not also dismiss the newly exposed splash.
    game.keyDown(VK_RETURN);
    if (game.page() != WordGamePage::StartupSplash) return false;
    game.character(L'\r');
    if (game.page() != WordGamePage::StartupSplash) return false;
    game.render(renderer);
    const std::uint64_t startupSplashHash = regionHash(renderer, 0, 0, 320, 200);
    const bool hasRecoveredWordSplashRamp =
        std::find(renderer.pixels().begin(), renderer.pixels().end(), 0xcfc700u) !=
            renderer.pixels().end() &&
        std::find(renderer.pixels().begin(), renderer.pixels().end(), 0x04cf00u) ==
            renderer.pixels().end();
    if (!hasRecoveredWordSplashRamp) {
        std::cerr << "word runtime stage: splash palette index\n";
        return false;
    }
    game.update(60.0);
    if (game.page() != WordGamePage::StartupSplash) return false;
    // A printable key defers to WM_CHAR, which performs exactly one gate
    // transition and is consumed before Title can apply its numeric shortcut.
    game.keyDown('4');
    if (game.page() != WordGamePage::StartupSplash) return false;
    game.character(L'4');
    constexpr std::array<std::uint64_t, 2> ExpectedStartupHashes = {
        0x0f20a0f87c2b1bb8ull, 0x4cd75eb6d6fea514ull,
    };
    const std::array startupHashes = {startupVersionHash, startupSplashHash};
    if (startupHashes != ExpectedStartupHashes) {
        std::cerr << "word VGA startup version/splash hashes=0x" << std::hex
                  << startupHashes[0] << ',' << startupHashes[1] << std::dec << "\n";
        return false;
    }
    if (game.page() != WordGamePage::Title || game.menuSelection() != 0) return false;
    WordGame startupPointer(GraphicsMode::Vga256, 0x1234);
    startupPointer.pointerButton(0, 0, false);
    if (startupPointer.page() != WordGamePage::StartupSplash) return false;
    startupPointer.pointerButton(0, 0, false);
    if (startupPointer.page() != WordGamePage::Title) return false;
    WordGame startupCheat(GraphicsMode::Vga256, 0x1234);
    startupCheat.toggleCheatMenu();
    if (startupCheat.page() != WordGamePage::Title || !startupCheat.cheatOpen()) return false;
    startupCheat.keyDown(VK_RIGHT);
    startupCheat.keyDown(VK_RETURN);
    if (startupCheat.page() != WordGamePage::Playing || startupCheat.cheatOpen() ||
        startupCheat.core().level() != 2) return false;
    WordGame informationAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::informationArchiveAndPagesMatch(informationAudit)) return false;
    WordGame optionsAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::optionsContentAndDifficultyRoute(optionsAudit)) return false;
    if (!WordGameTestAccess::fixedMenuPointerRoutes()) {
        std::cerr << "word runtime stage: fixed-menu pointer routing\n";
        return false;
    }
    if (!WordGameTestAccess::fixedMenuNumberedShortcuts()) {
        std::cerr << "word runtime stage: fixed-menu numbered shortcuts\n";
        return false;
    }
    if (!WordGameTestAccess::customWidgetPointerRoutes()) {
        std::cerr << "word runtime stage: custom-widget pointer routing\n";
        return false;
    }
    WordGame vowelAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::vowelEditorRoutes(vowelAudit)) return false;
    WordGame optionFlowAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::previewEraseAndPasswordRoutes(optionFlowAudit)) return false;
    WordGame joystickAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::joystickCalibrationAndInputRoutes(joystickAudit)) return false;
    WordGame persistenceAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::settingsPersistenceRoundTrip(persistenceAudit)) return false;
    WordGame gameplayAudioAudit(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::gameplayAudioRoutes(gameplayAudioAudit)) {
        std::cerr << "word runtime stage: gameplay audio\n";
        return false;
    }
    if (!WordGameTestAccess::toggleFeedbackRoutes()) {
        std::cerr << "word runtime stage: Alt+S/Alt+M acknowledgement audio\n";
        return false;
    }
    if (!WordGameTestAccess::attractDemoRoutes()) {
        std::cerr << "word runtime stage: attract demo\n";
        return false;
    }
    game.render(renderer);
    const std::uint64_t titleHash = regionHash(renderer, 0, 0, 320, 200);
    // Static composition gate: exact Word PCXF 6009, BIT8X8.GFT text,
    // recovered title baselines, and the shorter selected Play-row extent.
    constexpr std::uint64_t expectedWordTitleHash = 0xd0862a8ebebc6146ull;
    if (titleHash != expectedWordTitleHash || renderer.pixels().size() != 320u * 200u) {
        std::cerr << "word runtime stage: title render\n";
        return false;
    }

    // The original INT 33h mask excludes ordinary motion. First release on a
    // different item selects; the second release activates that same item.
    game.pointerMove(60, 105);
    if (game.menuSelection() != 0) return false;
    game.pointerButton(60, 105, false);
    if (game.page() != WordGamePage::Title || game.menuSelection() != 1) return false;
    game.pointerButton(60, 105, false);
    if (game.page() != WordGamePage::Hall) {
        std::cerr << "word runtime stage: title pointer\n";
        return false;
    }
    game.keyDown(VK_SPACE);
    if (game.page() != WordGamePage::Title) return false;

    WordGame instructionGame(GraphicsMode::Vga256, 0x1234);
    WordGameTestAccess::advanceStartup(instructionGame);
    instructionGame.keyDown(VK_RETURN);
    instructionGame.keyDown(VK_SPACE);
    if (instructionGame.page() != WordGamePage::InstructionsQuestion ||
        instructionGame.menuSelection() != 1) return false;
    instructionGame.keyDown(VK_UP);
    instructionGame.keyDown(VK_DOWN);
    if (instructionGame.page() != WordGamePage::InstructionsQuestion ||
        instructionGame.menuSelection() != 1) return false;
    instructionGame.keyDown(VK_LEFT);
    instructionGame.keyDown(VK_LEFT);
    if (instructionGame.page() != WordGamePage::InstructionsQuestion ||
        instructionGame.menuSelection() != 0) return false;
    instructionGame.keyDown(VK_RIGHT);
    instructionGame.keyDown(VK_RIGHT);
    if (instructionGame.page() != WordGamePage::InstructionsQuestion ||
        instructionGame.menuSelection() != 1) return false;
    instructionGame.keyDown('Y');
    if (instructionGame.page() != WordGamePage::InstructionsQuestion ||
        instructionGame.menuSelection() != 0) return false;
    instructionGame.keyDown(VK_RETURN);
    if (instructionGame.page() != WordGamePage::Information) return false;
    instructionGame.keyDown(VK_RIGHT);
    if (instructionGame.page() != WordGamePage::Information) return false;
    for (int page = 0; page < 4; ++page) instructionGame.keyDown(VK_SPACE);
    if (instructionGame.page() != WordGamePage::Information) return false;
    instructionGame.keyDown(VK_SPACE);
    if (instructionGame.page() != WordGamePage::Playing || !instructionGame.core().active()) {
        return false;
    }

    game.keyDown(VK_RETURN);
    if (game.page() != WordGamePage::InstructionsQuestion || game.menuSelection() != 1) {
        std::cerr << "word runtime stage: start core\n";
        return false;
    }
    game.keyDown(VK_SPACE);
    if (game.page() != WordGamePage::InstructionsQuestion || game.menuSelection() != 1) {
        return false;
    }
    game.keyDown('N');
    if (game.page() != WordGamePage::InstructionsQuestion || game.menuSelection() != 1) {
        return false;
    }
    game.keyDown(VK_RETURN);
    if (game.page() != WordGamePage::Playing || !game.core().active() ||
        game.core().level() != 1 || game.playerRow() < 1 || game.playerRow() > 3 ||
        game.playerColumn() < 1 || game.playerColumn() > 4 ||
        !game.core().eaten(static_cast<std::size_t>(
            game.playerRow() * WordGame::BoardColumns + game.playerColumn()))) {
        return false;
    }
    game.update(0.1);
    game.update(0.1); // complete the original synchronous board presenter

    // Four direction records and the recovered six-callback horizontal
    // movement are hosted by the Word presenter rather than bypassing the
    // core.
    const int movementStartRow = game.playerRow();
    const int movementStartColumn = game.playerColumn();
    game.keyDown(VK_RIGHT);
    if (!game.moving()) return false;
    game.update(0.1);
    if (!game.moving() || game.playerColumn() != movementStartColumn) return false;
    game.update(0.1);
    if (!game.moving() || game.playerColumn() != movementStartColumn) return false;
    game.update(0.01);
    if (game.moving() || game.playerColumn() != movementStartColumn + 1 ||
        game.playerRow() != movementStartRow) {
        std::cerr << "word runtime stage: movement terminal\n";
        return false;
    }

    // Word's shared actor dispatcher runs seven one-tick transitions. The
    // terminal tick reaches record 13 and resolves before another paint.
    game.keyDown(VK_SPACE);
    if (!game.munching()) return false;
    std::vector<std::uint64_t> chewFrames;
    for (int phase = 0; phase < WordGame::MunchAnimationTicks; ++phase) {
        game.render(renderer);
        chewFrames.push_back(regionHash(
            renderer,
            WordGame::BoardLeft + game.playerColumn() * WordGame::BoardCellWidth,
            WordGame::BoardTop + game.playerRow() * WordGame::BoardCellHeight,
            WordGame::BoardLeft + (game.playerColumn() + 1) * WordGame::BoardCellWidth,
            WordGame::BoardTop + (game.playerRow() + 1) * WordGame::BoardCellHeight));
        if (phase + 1 < WordGame::MunchAnimationTicks) {
            game.update(1.0 / WordSchedulerTicksPerSecond + 0.000001);
        }
    }
    if (chewFrames[0] == chewFrames[1] || chewFrames[0] != chewFrames[2] ||
        chewFrames[0] != chewFrames[4] || chewFrames[0] != chewFrames[6] ||
        chewFrames[1] != chewFrames[3] || chewFrames[1] != chewFrames[5]) {
        std::cerr << "word runtime stage: chew frame record order\n";
        return false;
    }
    constexpr std::array<std::uint64_t, WordGame::MunchAnimationTicks>
        LiveFirstBoardChew = {
            0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
            0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
            0x1c94da58fe5737fdull, 0x3fb6cdbad7ba0655ull,
            0x1c94da58fe5737fdull,
        };
    if (!std::equal(chewFrames.begin(), chewFrames.end(),
                    LiveFirstBoardChew.begin(), LiveFirstBoardChew.end())) {
        std::cerr << "word runtime stage: live first-board chew pixels\n";
        return false;
    }
    game.update(1.0 / WordSchedulerTicksPerSecond + 0.000001);
    if (game.munching() || !game.core().eaten(
            static_cast<std::size_t>(game.playerRow() * WordGame::BoardColumns + game.playerColumn()))) {
        std::cerr << "word runtime stage: chew terminal page=" << static_cast<int>(game.page())
                  << " munching=" << game.munching() << "\n";
        return false;
    }

    // Ctrl+Alt+F1 is routed by the Win32 shell to this non-original overlay;
    // its level selector must actually reinitialize Word gameplay at the
    // chosen level instead of being a decorative menu.
    game.toggleCheatMenu();
    if (!game.cheatOpen()) return false;
    game.keyDown(VK_RIGHT);
    game.keyDown(VK_RETURN);
    if (game.cheatOpen() || game.page() != WordGamePage::Playing || game.core().level() != 2) {
        std::cerr << "word runtime stage: cheat start level page=" << static_cast<int>(game.page())
                  << " level=" << game.core().level() << "\n";
        return false;
    }

    game.keyDown(VK_ESCAPE);
    if (game.page() != WordGamePage::QuitConfirm || game.menuSelection() != 1) return false;
    game.keyDown(VK_LEFT);
    game.keyDown(VK_RETURN);
    if (game.page() != WordGamePage::Title) return false;
    game.keyDown(VK_ESCAPE);
    if (game.shouldQuit() || !WordGameTestAccess::exitPending(game) ||
        std::abs(WordGameTestAccess::exitDelayTimer(game) - 0.015) > 1e-12) {
        std::cerr << "word runtime stage: title exit delay start\n";
        return false;
    }

    // The original delay helper is blocking. No keyboard, translated
    // character, mouse, or native cheat input can mutate the exposed Title
    // while controller action 6 owns those exact 15 milliseconds.
    const int exitSelection = game.menuSelection();
    game.keyDown(VK_DOWN);
    game.character(L'5');
    game.pointerMove(60, 105);
    if (!game.pointerPress(false)) return false;
    game.pointerButton(60, 105, false);
    game.toggleCheatMenu();
    if (game.menuSelection() != exitSelection || game.cheatOpen() || game.shouldQuit() ||
        !WordGameTestAccess::exitPending(game)) {
        std::cerr << "word runtime stage: title exit delay input ownership\n";
        return false;
    }
    game.update(0.014);
    if (game.shouldQuit() || !WordGameTestAccess::exitPending(game) ||
        std::abs(WordGameTestAccess::exitDelayTimer(game) - 0.001) > 1e-12) {
        std::cerr << "word runtime stage: title exit delay hold\n";
        return false;
    }
    game.update(0.001);
    if (!game.shouldQuit() || WordGameTestAccess::exitPending(game) ||
        WordGameTestAccess::exitDelayTimer(game) != 0.0) {
        std::cerr << "word runtime stage: title exit delay terminal\n";
        return false;
    }
    return true;
}

bool altShortcutRoutesMatch() {
    MunchersApp launcher(GraphicsMode::Vga256);
    if (launcher.handleAltShortcut('A') || !launcher.handleAltShortcut('S') ||
        launcher.mode() != MunchersAppMode::Launcher || launcher.shouldQuit()) {
        std::cerr << "Alt shortcut route: launcher ownership\n";
        return false;
    }

    // Outside the resident dispatcher, Alt+key is one inert non-character
    // event. Startup's any-event waiters acknowledge it, while Title and the
    // cheat overlay do not expose audio toggles.
    MunchersApp number(GraphicsMode::Vga256);
    number.keyDown(VK_RETURN);
    if (!number.numberGame()) return false;
    Game& numberGame = *const_cast<Game*>(number.numberGame());
    GameTestAccess::configureAudio(numberGame, true, true, false);
    if (!number.handleAltShortcut('S') ||
        !GameTestAccess::isStartupSplash(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, false)) {
        std::cerr << "Alt shortcut route: Number version acknowledgment\n";
        return false;
    }
    if (!number.handleAltShortcut('M') || !GameTestAccess::isTitle(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, false)) {
        std::cerr << "Alt shortcut route: Number splash acknowledgment\n";
        return false;
    }
    if (!number.handleAltShortcut('P') || !GameTestAccess::isTitle(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, false)) {
        std::cerr << "Alt shortcut route: Number modal suppression\n";
        return false;
    }
    number.toggleCheatMenu();
    if (!GameTestAccess::cheatOpen(numberGame) || !number.handleAltShortcut('S') ||
        !GameTestAccess::cheatOpen(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, false)) {
        std::cerr << "Alt shortcut route: Number cheat ownership\n";
        return false;
    }
    number.toggleCheatMenu();

    number.update(30.01);
    if (!GameTestAccess::isAttract(numberGame) || !number.handleAltShortcut('S') ||
        !GameTestAccess::isAttract(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, false, true, false)) {
        std::cerr << "Alt shortcut route: Number Demo Alt+S direct return\n";
        return false;
    }
    if (!number.handleAltShortcut('S') || !GameTestAccess::isAttract(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, false) ||
        !number.handleAltShortcut('P') || !GameTestAccess::isTitle(numberGame) ||
        !GameTestAccess::audioMatches(numberGame, true, true, true)) {
        std::cerr << "Alt shortcut route: Number Demo Alt+P fallthrough\n";
        return false;
    }

    MunchersApp numberMusic(GraphicsMode::Vga256);
    numberMusic.keyDown(VK_RETURN);
    numberMusic.keyDown(VK_RETURN);
    numberMusic.keyDown(VK_RETURN);
    if (!numberMusic.numberGame()) return false;
    Game& numberMusicGame = *const_cast<Game*>(numberMusic.numberGame());
    GameTestAccess::configureAudio(numberMusicGame, true, true, false);
    numberMusic.update(30.01);
    if (!GameTestAccess::isAttract(numberMusicGame) ||
        !numberMusic.handleAltShortcut('M') ||
        !GameTestAccess::isTitle(numberMusicGame) ||
        !GameTestAccess::audioMatches(numberMusicGame, true, false, false)) {
        std::cerr << "Alt shortcut route: Number Demo Alt+M fallthrough\n";
        return false;
    }

    // Exercise the same app-level route through Word, including the state-6
    // distinction: S returns with the cartoon live; M toggles then skips it.
    MunchersApp wordCartoon(GraphicsMode::Vga256);
    wordCartoon.keyDown(VK_DOWN);
    wordCartoon.keyDown(VK_RETURN);
    if (!wordCartoon.wordGame()) return false;
    WordGame& wordGame = *const_cast<WordGame*>(wordCartoon.wordGame());
    WordGameTestAccess::configureAudio(wordGame, true, true, false);
    if (!WordGameTestAccess::prepareCartoonForAlt(wordGame) ||
        !wordCartoon.handleAltShortcut('S') ||
        wordGame.page() != WordGamePage::LevelComplete ||
        !WordGameTestAccess::audioMatches(wordGame, false, true, false)) {
        std::cerr << "Alt shortcut route: Word cartoon Alt+S direct return\n";
        return false;
    }
    if (!wordCartoon.handleAltShortcut('M') ||
        wordGame.page() != WordGamePage::Playing || wordGame.core().level() != 4 ||
        !WordGameTestAccess::audioMatches(wordGame, false, false, false)) {
        std::cerr << "Alt shortcut route: Word cartoon Alt+M fallthrough\n";
        return false;
    }

    MunchersApp wordDemo(GraphicsMode::Vga256);
    wordDemo.keyDown(VK_DOWN);
    wordDemo.keyDown(VK_RETURN);
    wordDemo.keyDown(VK_RETURN);
    wordDemo.keyDown(VK_RETURN);
    if (!wordDemo.wordGame()) return false;
    WordGame& wordDemoGame = *const_cast<WordGame*>(wordDemo.wordGame());
    WordGameTestAccess::configureAudio(wordDemoGame, true, true, false);
    for (int frame = 0; frame < 301; ++frame) wordDemo.update(0.1);
    if (wordDemoGame.page() != WordGamePage::Attract ||
        !wordDemo.handleAltShortcut('S') ||
        wordDemoGame.page() != WordGamePage::Attract ||
        !WordGameTestAccess::audioMatches(wordDemoGame, false, true, false) ||
        !wordDemo.handleAltShortcut('P') ||
        wordDemoGame.page() != WordGamePage::Title ||
        !WordGameTestAccess::audioMatches(wordDemoGame, false, true, true)) {
        std::cerr << "Alt shortcut route: Word Demo S/P control flow\n";
        return false;
    }

    // A failed Alt+S save blocks on the original writer alert, but Alt+S's
    // direct-return path must not gain M/P's selector-state continuation.
    MunchersApp numberSoundFailure(GraphicsMode::Vga256);
    numberSoundFailure.keyDown(VK_RETURN);
    if (!numberSoundFailure.numberGame()) return false;
    Game& numberSoundFailureGame =
        *const_cast<Game*>(numberSoundFailure.numberGame());
    GameTestAccess::prepareSpeakerAttractBoard(numberSoundFailureGame);
    GameTestAccess::forceSettingsWriteFailure(numberSoundFailureGame);
    if (!numberSoundFailure.handleAltShortcut('S') ||
        !GameTestAccess::configurationWriteError(numberSoundFailureGame) ||
        GameTestAccess::configurationContinuationPending(numberSoundFailureGame) ||
        !GameTestAccess::isAttract(numberSoundFailureGame)) {
        std::cerr << "Alt shortcut route: Number failed Alt+S alert ownership\n";
        return false;
    }
    numberSoundFailure.keyDown(VK_F2);
    if (GameTestAccess::configurationWriteError(numberSoundFailureGame) ||
        GameTestAccess::configurationContinuationPending(numberSoundFailureGame) ||
        !GameTestAccess::isAttract(numberSoundFailureGame)) {
        std::cerr << "Alt shortcut route: Number failed Alt+S direct return\n";
        return false;
    }

    // Alt+P does rejoin that controller, but only after a later physical key
    // dismisses the synchronous writer alert. A printable key's WM_KEYDOWN is
    // deferred to WM_CHAR and must still count as just one acknowledgement.
    MunchersApp numberSpeakerFailure(GraphicsMode::Vga256);
    numberSpeakerFailure.keyDown(VK_RETURN);
    if (!numberSpeakerFailure.numberGame()) return false;
    Game& numberSpeakerFailureGame =
        *const_cast<Game*>(numberSpeakerFailure.numberGame());
    GameTestAccess::prepareSpeakerAttractBoard(numberSpeakerFailureGame);
    GameTestAccess::forceSettingsWriteFailure(numberSpeakerFailureGame);
    if (!numberSpeakerFailure.handleAltShortcut('P') ||
        !GameTestAccess::configurationWriteError(numberSpeakerFailureGame) ||
        !GameTestAccess::configurationContinuationPending(numberSpeakerFailureGame) ||
        !GameTestAccess::isAttract(numberSpeakerFailureGame)) {
        std::cerr << "Alt shortcut route: Number failed Alt+P deferred continuation\n";
        return false;
    }
    numberSpeakerFailure.update(1.0);
    numberSpeakerFailure.keyDown('Q');
    if (!GameTestAccess::configurationWriteError(numberSpeakerFailureGame) ||
        !GameTestAccess::configurationContinuationPending(numberSpeakerFailureGame) ||
        !GameTestAccess::isAttract(numberSpeakerFailureGame)) {
        std::cerr << "Alt shortcut route: Number failed Alt+P paired-key wait\n";
        return false;
    }
    numberSpeakerFailure.character(L'q');
    if (GameTestAccess::configurationWriteError(numberSpeakerFailureGame) ||
        GameTestAccess::configurationContinuationPending(numberSpeakerFailureGame) ||
        !GameTestAccess::isTitle(numberSpeakerFailureGame)) {
        std::cerr << "Alt shortcut route: Number failed Alt+P post-alert fallthrough\n";
        return false;
    }

    // Word's state-6 cartoon owns the analogous M/P fallthrough. It remains
    // frozen under the alert and advances only after the acknowledgment.
    MunchersApp wordMusicFailure(GraphicsMode::Vga256);
    wordMusicFailure.keyDown(VK_DOWN);
    wordMusicFailure.keyDown(VK_RETURN);
    if (!wordMusicFailure.wordGame()) return false;
    WordGame& wordMusicFailureGame =
        *const_cast<WordGame*>(wordMusicFailure.wordGame());
    WordGameTestAccess::configureAudio(wordMusicFailureGame, true, true, false);
    if (!WordGameTestAccess::prepareCartoonForAlt(wordMusicFailureGame)) return false;
    WordGameTestAccess::forceSettingsWriteFailure(wordMusicFailureGame);
    if (!wordMusicFailure.handleAltShortcut('M') ||
        !WordGameTestAccess::configurationWriteError(wordMusicFailureGame) ||
        !WordGameTestAccess::configurationContinuationPending(wordMusicFailureGame) ||
        wordMusicFailureGame.page() != WordGamePage::LevelComplete) {
        std::cerr << "Alt shortcut route: Word failed Alt+M deferred continuation\n";
        return false;
    }
    wordMusicFailure.update(1.0);
    if (wordMusicFailureGame.page() != WordGamePage::LevelComplete) {
        std::cerr << "Alt shortcut route: Word write alert cartoon freeze\n";
        return false;
    }
    wordMusicFailure.keyDown(VK_F2);
    if (WordGameTestAccess::configurationWriteError(wordMusicFailureGame) ||
        WordGameTestAccess::configurationContinuationPending(wordMusicFailureGame) ||
        wordMusicFailureGame.page() != WordGamePage::Playing ||
        wordMusicFailureGame.core().level() != 4) {
        std::cerr << "Alt shortcut route: Word failed Alt+M post-alert fallthrough\n";
        return false;
    }
    return true;
}

bool superAudioProfileMatches() {
    GameAssets assets(GraphicsMode::Vga256, GameAssetSet::SuperMunchers);
    std::uint64_t hash = 1469598103934665603ull;
    std::size_t validStreams = 0;
    const auto hashByte = [&hash](const std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    const auto hash32 = [&hashByte](const std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            hashByte(static_cast<std::uint8_t>(value >> shift));
        }
    };
    const auto auditBank = [&](const std::string_view tag, const std::uint32_t id) {
        const BlobView bank = assets.gameArchive().find(tag, id);
        if (!bank) return false;
        for (int stream = 0; stream < 256; ++stream) {
            const MeccSound sound = decodeMeccGSound(
                bank, static_cast<std::uint8_t>(stream),
                MeccSoundProfile::SuperMunchers);
            if (!sound.valid) continue;
            ++validStreams;
            for (const char character : tag) hashByte(static_cast<std::uint8_t>(character));
            hash32(id);
            hashByte(static_cast<std::uint8_t>(stream));
            hash32(sound.durationMilliseconds);
            hash32(static_cast<std::uint32_t>(sound.writes.size()));
            for (const OplWrite& write : sound.writes) {
                hash32(write.milliseconds);
                hashByte(write.reg);
                hashByte(write.value);
            }
            hash32(static_cast<std::uint32_t>(sound.speakerWrites.size()));
            for (const auto& write : sound.speakerWrites) {
                hash32(write.milliseconds);
                hash32(write.frequencyHz);
            }
        }
        return true;
    };
    if (!auditBank("GSND", 22)) return false;
    for (std::uint32_t bank = 21; bank <= 25; ++bank) {
        if (!auditBank("ADLI", bank) || !auditBank("PSND", bank)) return false;
    }
    constexpr std::size_t ExpectedValidStreams = 98;
    constexpr std::uint64_t ExpectedHash = 0xbe02d265f6bafbe1ull;
    if (validStreams != ExpectedValidStreams || hash != ExpectedHash) {
        std::cerr << "Super audio valid=" << validStreams << " hash=0x"
                  << std::hex << hash << std::dec << '\n';
        return false;
    }
    return true;
}

bool altMusicResumeSelectorStateMatches() {
    using NumberPrepare = void (*)(Game&);
    struct NumberCase {
        const char* label;
        NumberPrepare prepare;
        std::size_t expectedResumeCount;
    };
    const std::array numberCases = {
        NumberCase{"state-2 board", GameTestAccess::prepareMusicOffAttractBoard, 1},
        NumberCase{"state-2 board-to-Hall Wipe",
                   GameTestAccess::prepareMusicOffAttractBoardToHall, 1},
        NumberCase{"state-3 Hall-to-splash Wipe",
                   GameTestAccess::prepareMusicOffAttractHallToSplash, 0},
        NumberCase{"state-1 splash-to-board Wipe",
                   GameTestAccess::prepareMusicOffAttractSplashToBoard, 0},
    };
    for (const NumberCase& test : numberCases) {
        MunchersApp app(GraphicsMode::Vga256);
        app.keyDown(VK_RETURN);
        if (!app.numberGame()) return false;
        Game& game = *const_cast<Game*>(app.numberGame());
        test.prepare(game);
        const std::size_t plays = GameTestAccess::attractMusicLoopPlays(game);
        if (plays != 0 || !app.handleAltShortcut('M') ||
            !GameTestAccess::isTitle(game) ||
            !GameTestAccess::audioMatches(game, true, true, false) ||
            GameTestAccess::attractMusicLoopPlays(game) !=
                plays + test.expectedResumeCount ||
            !GameTestAccess::attractMusicStopped(game)) {
            std::cerr << "Alt+M selector resume: Number " << test.label
                      << " plays=" << GameTestAccess::attractMusicLoopPlays(game)
                      << " expected=" << plays + test.expectedResumeCount << '\n';
            return false;
        }
    }

    using WordPrepare = void (*)(WordGame&);
    struct WordCase {
        const char* label;
        WordPrepare prepare;
        std::size_t expectedResumeCount;
    };
    const std::array wordCases = {
        WordCase{"state-2 board", WordGameTestAccess::prepareMusicOffAttractBoard, 1},
        WordCase{"state-2 board-to-Hall Wipe",
                 WordGameTestAccess::prepareMusicOffAttractBoardToHall, 1},
        WordCase{"state-3 Hall-to-logo Wipe",
                 WordGameTestAccess::prepareMusicOffAttractHallToLogo, 0},
        WordCase{"state-1 logo-to-board Wipe",
                 WordGameTestAccess::prepareMusicOffAttractLogoToBoard, 0},
    };
    for (const WordCase& test : wordCases) {
        MunchersApp app(GraphicsMode::Vga256);
        app.keyDown(VK_DOWN);
        app.keyDown(VK_RETURN);
        if (!app.wordGame()) return false;
        WordGame& game = *const_cast<WordGame*>(app.wordGame());
        test.prepare(game);
        const std::size_t plays = WordGameTestAccess::attractMusicLoopPlays(game);
        if (plays != 0 || !app.handleAltShortcut('M') ||
            game.page() != WordGamePage::Title ||
            !WordGameTestAccess::audioMatches(game, true, true, false) ||
            WordGameTestAccess::attractMusicLoopPlays(game) !=
                plays + test.expectedResumeCount ||
            !WordGameTestAccess::attractMusicStopped(game)) {
            std::cerr << "Alt+M selector resume: Word " << test.label
                      << " plays=" << WordGameTestAccess::attractMusicLoopPlays(game)
                      << " expected=" << plays + test.expectedResumeCount << '\n';
            return false;
        }
    }
    return true;
}

bool altSpeakerResumeSelectorStateMatches() {
    using NumberPrepare = void (*)(Game&);
    struct NumberCase {
        const char* label;
        NumberPrepare prepare;
        std::size_t expectedResumeCount;
    };
    const std::array numberCases = {
        NumberCase{"state-2 board", GameTestAccess::prepareSpeakerAttractBoard, 1},
        NumberCase{"state-2 board-to-Hall Wipe",
                   GameTestAccess::prepareSpeakerAttractBoardToHall, 1},
        NumberCase{"state-3 Hall-to-splash Wipe",
                   GameTestAccess::prepareSpeakerAttractHallToSplash, 0},
        NumberCase{"state-1 splash-to-board Wipe",
                   GameTestAccess::prepareSpeakerAttractSplashToBoard, 0},
    };
    for (const NumberCase& test : numberCases) {
        MunchersApp app(GraphicsMode::Vga256);
        app.keyDown(VK_RETURN);
        if (!app.numberGame()) return false;
        Game& game = *const_cast<Game*>(app.numberGame());
        test.prepare(game);
        const std::size_t plays = GameTestAccess::attractMusicLoopPlays(game);
        if (plays != 0 || !app.handleAltShortcut('P') ||
            !GameTestAccess::isTitle(game) ||
            !GameTestAccess::audioMatches(game, true, true, false) ||
            GameTestAccess::attractMusicLoopPlays(game) !=
                plays + test.expectedResumeCount ||
            !GameTestAccess::attractMusicStopped(game)) {
            std::cerr << "Alt+P selector resume: Number " << test.label
                      << " plays=" << GameTestAccess::attractMusicLoopPlays(game)
                      << " expected=" << plays + test.expectedResumeCount << '\n';
            return false;
        }
    }

    using WordPrepare = void (*)(WordGame&);
    struct WordCase {
        const char* label;
        WordPrepare prepare;
        std::size_t expectedResumeCount;
    };
    const std::array wordCases = {
        WordCase{"state-2 board", WordGameTestAccess::prepareSpeakerAttractBoard, 1},
        WordCase{"state-2 board-to-Hall Wipe",
                 WordGameTestAccess::prepareSpeakerAttractBoardToHall, 1},
        WordCase{"state-3 Hall-to-logo Wipe",
                 WordGameTestAccess::prepareSpeakerAttractHallToLogo, 0},
        WordCase{"state-1 logo-to-board Wipe",
                 WordGameTestAccess::prepareSpeakerAttractLogoToBoard, 0},
    };
    for (const WordCase& test : wordCases) {
        MunchersApp app(GraphicsMode::Vga256);
        app.keyDown(VK_DOWN);
        app.keyDown(VK_RETURN);
        if (!app.wordGame()) return false;
        WordGame& game = *const_cast<WordGame*>(app.wordGame());
        test.prepare(game);
        const std::size_t plays = WordGameTestAccess::attractMusicLoopPlays(game);
        if (plays != 0 || !app.handleAltShortcut('P') ||
            game.page() != WordGamePage::Title ||
            !WordGameTestAccess::audioMatches(game, true, true, false) ||
            WordGameTestAccess::attractMusicLoopPlays(game) !=
                plays + test.expectedResumeCount ||
            !WordGameTestAccess::attractMusicStopped(game)) {
            std::cerr << "Alt+P selector resume: Word " << test.label
                      << " plays=" << WordGameTestAccess::attractMusicLoopPlays(game)
                      << " expected=" << plays + test.expectedResumeCount << '\n';
            return false;
        }
    }
    return true;
}

bool sharedLauncherRoutesMatch() {
    // A delayed WM_TIMER must not discard time beyond the former 100 ms
    // ceiling. The shared owner slices the full interval, so one 350 ms Word
    // update reaches the same 30-second Demo boundary as 100+100+100+50 ms.
    MunchersApp coalescedWordIdle(GraphicsMode::Vga256);
    MunchersApp splitWordIdle(GraphicsMode::Vga256);
    const auto reachWordTitle = [](MunchersApp& owner) {
        owner.keyDown(VK_DOWN);
        owner.keyDown(VK_RETURN);
        owner.keyDown(VK_RETURN);
        owner.keyDown(VK_RETURN);
    };
    reachWordTitle(coalescedWordIdle);
    reachWordTitle(splitWordIdle);
    for (int slice = 0; slice < 298; ++slice) {
        coalescedWordIdle.update(0.1);
        splitWordIdle.update(0.1);
    }
    coalescedWordIdle.update(0.35);
    splitWordIdle.update(0.1);
    splitWordIdle.update(0.1);
    splitWordIdle.update(0.1);
    splitWordIdle.update(0.05);
    if (!coalescedWordIdle.wordGame() || !splitWordIdle.wordGame() ||
        coalescedWordIdle.wordGame()->page() != WordGamePage::Attract ||
        splitWordIdle.wordGame()->page() != WordGamePage::Attract) {
        std::cerr << "launcher route: coalesced Word elapsed time was discarded\n";
        return false;
    }

    MunchersApp app(GraphicsMode::Vga256);
    Renderer renderer;
    app.render(renderer);
    const std::uint64_t launcherHash = regionHash(renderer, 0, 0, 320, 200);
    constexpr std::uint64_t expectedLauncherHash = 0xfa2a7967d1823417ull;
    if (launcherHash != expectedLauncherHash || app.mode() != MunchersAppMode::Launcher ||
        app.launcherSelection() != 0 || app.shouldQuit() ||
        app.acceptsCommonDispatcherInput()) {
        std::cerr << "launcher hash=0x" << std::hex << launcherHash << std::dec << '\n';
        return false;
    }

    app.keyDown(VK_DOWN);
    app.keyDown(VK_RETURN);
    if (app.mode() != MunchersAppMode::WordMunchers || !app.wordGame() ||
        app.wordGame()->page() != WordGamePage::StartupVersion ||
        app.acceptsCommonDispatcherInput()) {
        return false;
    }
    app.keyDown(VK_RETURN); // Word version -> splash.
    if (app.wordGame()->page() != WordGamePage::StartupSplash) return false;
    app.keyDown(VK_RETURN); // Word splash -> title.
    if (app.wordGame()->page() != WordGamePage::Title) return false;
    // Word's main-menu Escape enters original controller action 6. The app
    // retains the game through its literal 15 ms cleanup delay, then consumes
    // the exit and returns to the collection menu without terminating.
    app.keyDown(VK_ESCAPE);
    if (app.mode() != MunchersAppMode::WordMunchers || !app.wordGame() ||
        !WordGameTestAccess::exitPending(*app.wordGame()) || app.shouldQuit()) {
        return false;
    }
    app.update(0.014);
    if (app.mode() != MunchersAppMode::WordMunchers || !app.wordGame() ||
        !WordGameTestAccess::exitPending(*app.wordGame()) || app.shouldQuit()) return false;
    app.update(0.001);
    if (app.mode() != MunchersAppMode::Launcher || app.shouldQuit() ||
        app.launcherSelection() != 1) return false;

    app.keyDown(VK_UP);
    app.keyDown(VK_RETURN);
    if (app.mode() != MunchersAppMode::NumberMunchers) return false;
    app.keyDown(VK_RETURN); // Number version -> splash.
    app.keyDown(VK_RETURN); // Number splash -> title.
    app.keyDown(VK_ESCAPE); // Number title -> controller cleanup delay.
    if (app.mode() != MunchersAppMode::NumberMunchers || !app.numberGame() ||
        !GameTestAccess::exitPending(*app.numberGame()) ||
        std::abs(GameTestAccess::exitDelayTimer(*app.numberGame()) - 0.015) > 1e-12 ||
        app.shouldQuit()) return false;

    const int numberExitSelection = GameTestAccess::menuSelection(*app.numberGame());
    app.keyDown(VK_DOWN);
    app.character(L'5');
    app.pointerMove(60, 105);
    if (!app.pointerPress(false)) return false;
    app.pointerButton(60, 105, false);
    app.toggleCheatMenu();
    if (!app.numberGame() || !GameTestAccess::isTitle(*app.numberGame()) ||
        !GameTestAccess::exitPending(*app.numberGame()) ||
        GameTestAccess::menuSelection(*app.numberGame()) != numberExitSelection ||
        GameTestAccess::cheatOpen(*app.numberGame())) {
        std::cerr << "launcher route: Number terminal delay lost input ownership\n";
        return false;
    }
    app.update(0.014);
    if (app.mode() != MunchersAppMode::NumberMunchers || !app.numberGame() ||
        !GameTestAccess::exitPending(*app.numberGame()) || app.shouldQuit()) return false;
    app.update(0.001); // delay terminal -> collection launcher.
    if (app.mode() != MunchersAppMode::Launcher || app.shouldQuit() ||
        app.launcherSelection() != 0) {
        return false;
    }

    // A physical press consumed during the terminal delay can outlive the
    // game controller. Its matching release must not activate the launcher;
    // only that one release is swallowed, for either physical button.
    MunchersApp numberDelayedLeftRelease(GraphicsMode::Vga256);
    numberDelayedLeftRelease.keyDown(VK_RETURN); // Launch Number.
    numberDelayedLeftRelease.keyDown(VK_RETURN); // Version -> splash.
    numberDelayedLeftRelease.keyDown(VK_RETURN); // Splash -> title.
    numberDelayedLeftRelease.keyDown(VK_ESCAPE); // Begin 15 ms exit.
    if (!numberDelayedLeftRelease.pointerPress(false)) return false;
    numberDelayedLeftRelease.update(0.015);
    if (numberDelayedLeftRelease.mode() != MunchersAppMode::Launcher ||
        numberDelayedLeftRelease.launcherSelection() != 0) return false;
    numberDelayedLeftRelease.pointerButton(160, 76, false);
    if (numberDelayedLeftRelease.mode() != MunchersAppMode::Launcher ||
        numberDelayedLeftRelease.launcherSelection() != 0) {
        std::cerr << "launcher route: delayed Number left release leaked\n";
        return false;
    }
    if (numberDelayedLeftRelease.pointerPress(false)) return false;
    numberDelayedLeftRelease.pointerButton(160, 76, false);
    if (numberDelayedLeftRelease.mode() != MunchersAppMode::NumberMunchers) {
        std::cerr << "launcher route: Number left suppression exceeded one release\n";
        return false;
    }

    MunchersApp wordDelayedRightRelease(GraphicsMode::Vga256);
    wordDelayedRightRelease.keyDown(VK_DOWN);
    wordDelayedRightRelease.keyDown(VK_RETURN); // Launch Word.
    wordDelayedRightRelease.keyDown(VK_RETURN); // Version -> splash.
    wordDelayedRightRelease.keyDown(VK_RETURN); // Splash -> title.
    wordDelayedRightRelease.keyDown(VK_ESCAPE); // Begin 15 ms exit.
    if (!wordDelayedRightRelease.pointerPress(true)) return false;
    wordDelayedRightRelease.update(0.015);
    if (wordDelayedRightRelease.mode() != MunchersAppMode::Launcher ||
        wordDelayedRightRelease.launcherSelection() != 1) return false;
    wordDelayedRightRelease.pointerButton(0, 0, true);
    if (wordDelayedRightRelease.mode() != MunchersAppMode::Launcher ||
        wordDelayedRightRelease.launcherSelection() != 1) {
        std::cerr << "launcher route: delayed Word right release leaked\n";
        return false;
    }
    if (wordDelayedRightRelease.pointerPress(true)) return false;
    wordDelayedRightRelease.pointerButton(0, 0, true);
    if (wordDelayedRightRelease.mode() != MunchersAppMode::WordMunchers) {
        std::cerr << "launcher route: Word right suppression exceeded one release\n";
        return false;
    }

    // A confirmed quit during Number gameplay follows the original Hall and
    // replay-question route.  Only the subsequent terminal title Escape is
    // consumed by the collection shell, and it must return to the launcher.
    MunchersApp numberInGame(GraphicsMode::Vga256);
    numberInGame.keyDown(VK_RETURN); // Launch Number Munchers.
    numberInGame.keyDown(VK_RETURN); // Version -> splash.
    numberInGame.keyDown(VK_RETURN); // Splash -> title.
    numberInGame.keyDown(VK_RETURN); // Play -> instructions question (No).
    numberInGame.keyDown(VK_RETURN); // No -> mode selector.
    numberInGame.keyDown(VK_RETURN); // Default mode -> playing.
    numberInGame.update(1.0);        // synchronous board Wipe/painter.
    if (!numberInGame.acceptsCommonDispatcherInput()) return false;
    numberInGame.keyDown(VK_ESCAPE); // Playing -> quit confirmation (No).
    if (numberInGame.acceptsCommonDispatcherInput()) return false;
    numberInGame.keyDown(VK_LEFT);   // Select Yes.
    numberInGame.keyDown(VK_RETURN); // Yes -> post-game Hall.
    if (numberInGame.mode() != MunchersAppMode::NumberMunchers ||
        numberInGame.shouldQuit()) {
        std::cerr << "launcher route: Number quit escaped collection early\n";
        return false;
    }
    numberInGame.keyDown(VK_ESCAPE); // Hall -> replay question.
    numberInGame.keyDown(VK_ESCAPE); // Replay question -> title.
    if (numberInGame.mode() != MunchersAppMode::NumberMunchers ||
        numberInGame.shouldQuit()) {
        std::cerr << "launcher route: Number replay exit escaped collection early\n";
        return false;
    }
    numberInGame.keyDown(VK_ESCAPE); // Title -> controller cleanup delay.
    if (numberInGame.mode() != MunchersAppMode::NumberMunchers ||
        !numberInGame.numberGame() ||
        !GameTestAccess::exitPending(*numberInGame.numberGame()) ||
        numberInGame.shouldQuit()) {
        std::cerr << "launcher route: Number terminal delay missing\n";
        return false;
    }
    numberInGame.update(0.014);
    if (numberInGame.mode() != MunchersAppMode::NumberMunchers) return false;
    numberInGame.update(0.001); // delay terminal -> collection launcher.
    if (numberInGame.mode() != MunchersAppMode::Launcher ||
        numberInGame.shouldQuit() || numberInGame.launcherSelection() != 0) {
        std::cerr << "launcher route: Number terminal exit missed launcher\n";
        return false;
    }

    // Word's confirmed in-game quit returns to its title directly.  Its title
    // Escape is likewise consumed by the shell instead of closing the process.
    MunchersApp wordInGame(GraphicsMode::Vga256);
    wordInGame.keyDown(VK_DOWN);
    wordInGame.keyDown(VK_RETURN); // Launch Word Munchers.
    wordInGame.keyDown(VK_RETURN); // Version -> splash.
    wordInGame.keyDown(VK_RETURN); // Splash -> title.
    wordInGame.keyDown(VK_RETURN); // Play -> instructions question (No).
    wordInGame.keyDown(VK_RETURN); // No -> playing.
    wordInGame.update(0.1);
    wordInGame.update(0.1);        // synchronous board Wipe/painter.
    if (!wordInGame.wordGame() || wordInGame.wordGame()->page() != WordGamePage::Playing) {
        std::cerr << "launcher route: Word did not reach gameplay\n";
        return false;
    }
    if (!wordInGame.acceptsCommonDispatcherInput()) return false;
    wordInGame.keyDown(VK_ESCAPE); // Playing -> quit confirmation (No).
    if (wordInGame.acceptsCommonDispatcherInput()) return false;
    wordInGame.keyDown(VK_LEFT);   // Select Yes.
    wordInGame.keyDown(VK_RETURN); // Yes -> title.
    if (wordInGame.mode() != MunchersAppMode::WordMunchers ||
        !wordInGame.wordGame() || wordInGame.wordGame()->page() != WordGamePage::Title ||
        wordInGame.shouldQuit()) {
        std::cerr << "launcher route: Word confirmed quit missed title\n";
        return false;
    }
    wordInGame.keyDown(VK_ESCAPE); // Title -> controller cleanup delay.
    if (wordInGame.mode() != MunchersAppMode::WordMunchers ||
        !wordInGame.wordGame() ||
        !WordGameTestAccess::exitPending(*wordInGame.wordGame()) ||
        wordInGame.shouldQuit()) {
        std::cerr << "launcher route: Word terminal delay missing\n";
        return false;
    }
    wordInGame.update(0.014);
    if (wordInGame.mode() != MunchersAppMode::WordMunchers) return false;
    wordInGame.update(0.001); // delay terminal -> collection launcher.
    if (wordInGame.mode() != MunchersAppMode::Launcher ||
        wordInGame.shouldQuit() || wordInGame.launcherSelection() != 1) {
        std::cerr << "launcher route: Word terminal exit missed launcher\n";
        return false;
    }

    // The visible Quit items use the same original action-6 boundary as
    // Escape, rather than bypassing cleanup through a native shortcut.
    MunchersApp numberMenuExit(GraphicsMode::Vga256);
    numberMenuExit.keyDown(VK_RETURN);
    numberMenuExit.keyDown(VK_RETURN);
    numberMenuExit.keyDown(VK_RETURN);
    for (int row = 0; row < 4; ++row) numberMenuExit.keyDown(VK_DOWN);
    numberMenuExit.keyDown(VK_RETURN);
    if (!numberMenuExit.numberGame() ||
        !GameTestAccess::exitPending(*numberMenuExit.numberGame()) ||
        std::abs(GameTestAccess::exitDelayTimer(*numberMenuExit.numberGame()) - 0.015) > 1e-12) {
        std::cerr << "launcher route: Number Quit item missed terminal delay\n";
        return false;
    }
    numberMenuExit.update(0.015);
    if (numberMenuExit.mode() != MunchersAppMode::Launcher ||
        numberMenuExit.launcherSelection() != 0 || numberMenuExit.shouldQuit()) return false;

    MunchersApp wordMenuExit(GraphicsMode::Vga256);
    wordMenuExit.keyDown(VK_DOWN);
    wordMenuExit.keyDown(VK_RETURN);
    wordMenuExit.keyDown(VK_RETURN);
    wordMenuExit.keyDown(VK_RETURN);
    for (int row = 0; row < 4; ++row) wordMenuExit.keyDown(VK_DOWN);
    wordMenuExit.keyDown(VK_RETURN);
    if (!wordMenuExit.wordGame() ||
        !WordGameTestAccess::exitPending(*wordMenuExit.wordGame()) ||
        std::abs(WordGameTestAccess::exitDelayTimer(*wordMenuExit.wordGame()) - 0.015) > 1e-12) {
        std::cerr << "launcher route: Word Quit item missed terminal delay\n";
        return false;
    }
    wordMenuExit.update(0.015);
    if (wordMenuExit.mode() != MunchersAppMode::Launcher ||
        wordMenuExit.launcherSelection() != 1 || wordMenuExit.shouldQuit()) return false;

    // Super owns the same one-window lifecycle: the third launcher entry
    // reaches its native startup/title and terminal Escape returns here.
    MunchersApp superMenuExit(GraphicsMode::Vga256);
    superMenuExit.keyDown(VK_DOWN);
    superMenuExit.keyDown(VK_DOWN);
    superMenuExit.keyDown(VK_RETURN);
    if (superMenuExit.mode() != MunchersAppMode::SuperMunchers ||
        !superMenuExit.superGame() ||
        superMenuExit.superGame()->page() != SuperGamePage::StartupVersion) {
        std::cerr << "launcher route: Super entry missed version page\n";
        return false;
    }
    superMenuExit.keyDown(VK_RETURN);
    if (!superMenuExit.superGame() ||
        superMenuExit.superGame()->page() != SuperGamePage::StartupSplash) {
        std::cerr << "launcher route: Super version missed splash\n";
        return false;
    }
    superMenuExit.update(5.0);
    if (!superMenuExit.superGame() ||
        superMenuExit.superGame()->page() != SuperGamePage::StartupSplash) {
        std::cerr << "launcher route: Super splash timed out without a key\n";
        return false;
    }
    superMenuExit.keyDown(VK_RETURN);
    if (!superMenuExit.superGame() ||
        superMenuExit.superGame()->page() != SuperGamePage::Title) {
        std::cerr << "launcher route: Super splash missed title page="
                  << (superMenuExit.superGame()
                      ? static_cast<int>(superMenuExit.superGame()->page()) : -1) << '\n';
        return false;
    }
    superMenuExit.keyDown(VK_ESCAPE);
    if (superMenuExit.mode() != MunchersAppMode::Launcher ||
        superMenuExit.launcherSelection() != 2 || superMenuExit.shouldQuit()) {
        std::cerr << "launcher route: Super terminal exit missed launcher\n";
        return false;
    }

    // Ctrl+Alt+F1 is owned by the Win32 shell.  At the launcher its forwarded
    // action is deliberately inert and cannot launch or close anything.
    MunchersApp launcherHotkey(GraphicsMode::Vga256);
    launcherHotkey.toggleCheatMenu();
    if (launcherHotkey.mode() != MunchersAppMode::Launcher ||
        launcherHotkey.launcherSelection() != 0 || launcherHotkey.shouldQuit()) {
        std::cerr << "launcher route: cheat hotkey mutated launcher\n";
        return false;
    }

    // Process termination belongs only to the launcher's explicit Exit item.
    MunchersApp finalExit(GraphicsMode::Vga256);
    finalExit.keyDown('E');
    if (finalExit.launcherSelection() != 3 || finalExit.shouldQuit()) return false;
    finalExit.keyDown(VK_RETURN);
    return finalExit.mode() == MunchersAppMode::Launcher && finalExit.shouldQuit();
}

bool wordCgaPresentationUsesHardwareColors() {
    Renderer renderer(GraphicsMode::Cga4);
    const std::set<std::uint32_t> exactPalette = {
        0x000000u, 0x0000aau, 0x55ffffu, 0xff55ffu, 0xffffffu,
    };
    std::set<std::uint32_t> observedPalette;
    std::array<std::uint64_t, 7> hashes{};
    constexpr std::array<std::string_view, 7> dumpNames = {
        "word-version-native.ppm", "word-splash-native.ppm",
        "word-title-native.ppm", "word-options-native.ppm",
        "word-hall-native.ppm", "word-board-native.ppm",
        "word-gameplay-first-board-native.ppm",
    };
    const auto maybeDump = [&](const std::size_t index) {
        const char* directoryText = std::getenv("MUNCHERS_DUMP_CGA_DIR");
        if (!directoryText || !*directoryText || index >= dumpNames.size()) return;
        const std::filesystem::path directory(directoryText);
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return;
        std::ofstream output(directory / dumpNames[index],
                             std::ios::binary | std::ios::trunc);
        if (!output) return;
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
    };
    const auto capture = [&](WordGame& game, const std::size_t index) {
        game.render(renderer);
        const std::set<std::uint32_t> colors(renderer.pixels().begin(), renderer.pixels().end());
        if (!std::includes(exactPalette.begin(), exactPalette.end(),
                           colors.begin(), colors.end())) return false;
        observedPalette.insert(colors.begin(), colors.end());
        hashes[index] = regionHash(renderer, 0, 0, 320, 200);
        maybeDump(index);
        return true;
    };

    WordGame title(GraphicsMode::Cga4, 0x1234);
    if (!capture(title, 0) || title.page() != WordGamePage::StartupVersion) return false;
    title.keyDown(VK_RETURN);
    if (!capture(title, 1) || title.page() != WordGamePage::StartupSplash) return false;
    title.keyDown(VK_RETURN);
    if (!capture(title, 2) || title.page() != WordGamePage::Title) return false;

    WordGame options(GraphicsMode::Cga4, 0x1234);
    WordGameTestAccess::advanceStartup(options);
    options.keyDown(VK_DOWN);
    options.keyDown(VK_DOWN);
    options.keyDown(VK_DOWN);
    options.keyDown(VK_RETURN);
    if (!capture(options, 3) || options.page() != WordGamePage::Options) return false;

    WordGame hall(GraphicsMode::Cga4, 0x1234);
    WordGameTestAccess::advanceStartup(hall);
    hall.keyDown(VK_DOWN);
    hall.keyDown(VK_RETURN);
    if (!capture(hall, 4) || hall.page() != WordGamePage::Hall) return false;

    WordGame board(GraphicsMode::Cga4, 0x1234);
    WordGameTestAccess::advanceStartup(board);
    board.keyDown(VK_RETURN);
    board.keyDown(VK_RETURN);
    board.update(0.1);
    board.update(0.1);
    if (!capture(board, 5) || board.page() != WordGamePage::Playing) return false;

    WordGame capturedBoard(GraphicsMode::Cga4, 0x1234);
    WordGameTestAccess::prepareCapturedCgaTreeBoard(capturedBoard);
    if (!capture(capturedBoard, 6) ||
        capturedBoard.page() != WordGamePage::Playing) return false;

    constexpr std::array<std::uint64_t, 7> expectedHashes = {
        0x8ed7651c7827ba8cull, 0x3043b05c7674e388ull,
        0x20f7bd4383d7775cull, 0xcaf1504d701741dcull,
        0x80795be2acc95ec2ull, 0x512ba7bf24df5501ull,
        0x4748e03008e2ab7aull,
    };
    if (hashes != expectedHashes) {
        std::cerr << "word CGA version/splash/title/options/Hall/boards hashes=" << std::hex
                  << hashes[0] << ',' << hashes[1] << ',' << hashes[2] << ',' << hashes[3]
                  << ',' << hashes[4] << ',' << hashes[5] << ',' << hashes[6]
                  << std::dec << "\n";
    }
    return observedPalette == exactPalette && hashes == expectedHashes;
}

bool munchWordCellViaPointer(WordGame& game, const int index) {
    const int row = index / WordGame::BoardColumns;
    const int column = index % WordGame::BoardColumns;
    const int x = WordGame::BoardLeft + column * WordGame::BoardCellWidth +
                  WordGame::BoardCellWidth / 2;
    const int y = WordGame::BoardTop + row * WordGame::BoardCellHeight +
                  WordGame::BoardCellHeight / 2;

    game.pointerButton(x, y, false);
    for (int step = 0; step < 100 && game.moving(); ++step) game.update(0.1);
    if (game.moving()) return false;
    if (!game.munching()) game.pointerButton(x, y, false);
    if (!game.munching()) return false;
    for (int step = 0; step < 10 && game.munching(); ++step) game.update(0.1);
    return !game.munching() && game.core().eaten(static_cast<std::size_t>(index));
}

int exerciseWordScriptedScene(const std::uint16_t seed, const int startingLevel,
                              const bool useSpeaker) {
    WordGame game(GraphicsMode::Vga256, seed);
    WordGameTestAccess::advanceStartup(game);
    game.keyDown(VK_RETURN); // Play Word Munchers.
    game.keyDown(VK_RETURN); // Default No: begin ordinary play.
    if (game.page() != WordGamePage::Playing || game.core().level() != 1) return -1;
    game.update(0.1);
    game.update(0.1);

    // Exercise the requested developer level selector, but complete the board
    // through the same pointer-walk and seven-tick chew path used by play.
    game.toggleCheatMenu();
    for (int level = 1; level < startingLevel; ++level) game.keyDown(VK_RIGHT);
    game.keyDown(VK_RETURN);
    if (game.page() != WordGamePage::Playing || game.core().level() != startingLevel) return -1;
    WordGameTestAccess::disableBoardJobs(game);
    if (useSpeaker) game.toggleSpeaker();

    while (game.core().correctRemaining() > 0) {
        int correctIndex = -1;
        for (int index = 0; index < WordGame::BoardCellCount; ++index) {
            if (!game.core().eaten(static_cast<std::size_t>(index)) &&
                game.core().board().cells[static_cast<std::size_t>(index)].correct) {
                correctIndex = index;
                break;
            }
        }
        if (correctIndex < 0 || !munchWordCellViaPointer(game, correctIndex)) {
            std::cerr << "word scene stage: completing level " << startingLevel << " board\n";
            return -1;
        }
    }

    const int sceneIndex = game.core().pendingCartoonScene();
    constexpr std::array<int, 6> terminalTicks = {102, 142, 133, 106, 110, 251};
    for (int update = 0;
         update < 40 && game.page() == WordGamePage::LevelComplete &&
         (!game.sceneActive() || game.sceneTicks() < 1);
         ++update) {
        game.update(0.05);
    }
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(terminalTicks.size()) ||
        game.page() != WordGamePage::LevelComplete || !game.sceneActive() ||
        game.sceneGraphicId() != 2001 + sceneIndex * 2 || game.sceneTicks() != 1) {
        std::cerr << "word scene stage: trigger/index/first dispatch index=" << sceneIndex
                  << " page=" << static_cast<int>(game.page())
                  << " graphic=" << game.sceneGraphicId()
                  << " ticks=" << game.sceneTicks() << "\n";
        return -1;
    }

    Renderer renderer;
    game.render(renderer);
    const std::uint64_t firstFrame = regionHash(renderer, 0, 0, 320, 200);
    bool sawPaintedPixel = false;
    bool sawFrameChange = false;
    for (int update = 0; update < 1'000 && game.page() == WordGamePage::LevelComplete;
         ++update) {
        game.update(0.1);
        if (game.page() != WordGamePage::LevelComplete) break;
        game.render(renderer);
        const std::uint64_t frame = regionHash(renderer, 0, 0, 320, 200);
        sawFrameChange = sawFrameChange || frame != firstFrame;
        for (const std::uint32_t pixel : renderer.pixels()) {
            if (pixel != Colors::Black) {
                sawPaintedPixel = true;
                break;
            }
        }
    }

    if (game.page() != WordGamePage::Playing || game.core().level() != startingLevel + 1 ||
        game.core().cartoonPending() || game.sceneActive() ||
        game.sceneTicks() != terminalTicks[static_cast<std::size_t>(sceneIndex)] ||
        game.sceneAudioPlayCount() == 0 || !sawPaintedPixel || !sawFrameChange) {
        std::cerr << "word scene stage: terminal lifecycle index=" << sceneIndex
                  << " page=" << static_cast<int>(game.page())
                  << " level=" << game.core().level()
                  << " ticks=" << game.sceneTicks()
                  << " audio=" << game.sceneAudioPlayCount()
                  << " painted=" << sawPaintedPixel
                  << " changed=" << sawFrameChange << "\n";
        return -1;
    }
    return sceneIndex;
}

bool wordScriptedSceneHostMatches() {
    if (!WordGameTestAccess::rendersRecoveredCartoonTimelines()) return false;
    std::array<bool, 6> seen{};
    for (std::uint16_t seed = 1; seed <= 64; ++seed) {
        const int sceneIndex = exerciseWordScriptedScene(seed, 3, false);
        if (sceneIndex < 0 || sceneIndex >= 5) return false;
        seen[static_cast<std::size_t>(sceneIndex)] = true;
        if (std::all_of(seen.begin(), seen.begin() + 5, [](const bool value) { return value; })) {
            break;
        }
    }
    if (!std::all_of(seen.begin(), seen.begin() + 5, [](const bool value) { return value; })) {
        std::cerr << "word scene stage: seeds did not cover ordinary scene permutation\n";
        return false;
    }

    // Level 18 bypasses the shuffled ordinary selection and always hosts
    // scene 5. Route this pass through PSND so both runtime device banks are
    // proven to reach the headless WavePlayer without opening an audio device.
    const int specialScene = exerciseWordScriptedScene(0x1818, 18, true);
    seen[5] = specialScene == 5;
    return std::all_of(seen.begin(), seen.end(), [](const bool value) { return value; });
}

bool wordTroggleRuntimeMatches() {
    if (!WordGameTestAccess::usesRecoveredRuntimePrngInterleaving()) {
        std::cerr << "word Troggle stage: integrated PRNG interleaving\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredWordTroggleTables()) {
        std::cerr << "word Troggle stage: recovered tables\n";
        return false;
    }
    if (!WordGameTestAccess::retriesProtectedTurnsWithoutSyntheticCap()) {
        std::cerr << "word Troggle stage: unbounded protected-turn retry\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredBoardQuestionAndHud()) {
        std::cerr << "word Troggle stage: board question/HUD composition\n";
        return false;
    }
    if (!WordGameTestAccess::liveWordAttractBoardRegionsMatch()) {
        std::cerr << "word Troggle stage: live Demo board regions\n";
        return false;
    }
    WordGame initialization(GraphicsMode::Vga256, 0x1234);
    if (!WordGameTestAccess::usesRecoveredOrdinaryTroggleFrames(initialization)) {
        std::cerr << "word Troggle stage: ordinary frame states\n";
        return false;
    }
    if (!WordGameTestAccess::usesSharedCallbackIntermediatePresentation()) {
        std::cerr << "word Troggle stage: callback-intermediate presentation\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredPlayerMovement(initialization)) {
        std::cerr << "word Troggle stage: player movement callbacks\n";
        return false;
    }
    if (!WordGameTestAccess::liveWordMovementAndChewCompositeMatches()) {
        std::cerr << "word Troggle stage: live board/movement/chew composite\n";
        return false;
    }
    if (!WordGameTestAccess::liveWordWm011BoardAndTroggleEntryMatch()) {
        std::cerr << "word Troggle stage: wm011 board/warning/entry composite\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedBottomBashfulEntryGeometry()) {
        std::cerr << "word Troggle stage: shared bottom Bashful entry geometry\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedConcurrentRightEntryPhaseSixRule()) {
        std::cerr << "word Troggle stage: concurrent right-entry phase-6 rule\n";
        return false;
    }
    if (!WordGameTestAccess::retainsSafeOutlineOnFirstConcurrentAttractChew()) {
        std::cerr << "word Troggle stage: safe-zone/first-chew resident page\n";
        return false;
    }
    if (!WordGameTestAccess::delaysSafeZonePaintBehindActiveEnemyJobs()) {
        std::cerr << "word Troggle stage: safe-zone/enemy resident delay\n";
        return false;
    }
    if (!WordGameTestAccess::retainsPlayerTerminalBehindLaterEnemyCallback()) {
        std::cerr << "word Troggle stage: terminal-player/enemy resident page\n";
        return false;
    }
    if (!WordGameTestAccess::retainsSeededTerminalPlayerSurfacesBeforeNewEnemyMoves()) {
        std::cerr << "word Troggle stage: seeded terminal/new-move residencies\n";
        return false;
    }
    if (!WordGameTestAccess::retainsTerminalPlayerForegroundOverResidentEnemySweep()) {
        std::cerr << "word Troggle stage: terminal-player foreground resident page\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedLeftBashfulTrailExitGeometry()) {
        std::cerr << "word Troggle stage: shared left Bashful trail/exit geometry\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedTopBashfulExitGeometry()) {
        std::cerr << "word Troggle stage: shared top Bashful exit geometry\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedDualHelperMovementGeometry()) {
        std::cerr << "word Troggle stage: shared dual-Helper movement geometry\n";
        return false;
    }
    if (!WordGameTestAccess::retainsSeededFinalPlayerCallbackSurface()) {
        std::cerr << "word Troggle stage: seeded final-player callback residency\n";
        return false;
    }
    if (!WordGameTestAccess::routesPlayerCallbackBeforeCannibalBiter()) {
        std::cerr << "word Troggle stage: cannibal/player callback routing\n";
        return false;
    }
    if (!WordGameTestAccess::usesCapturedHelperBottomExitPlayerMoveGeometry()) {
        std::cerr << "word Troggle stage: shared Helper bottom-exit/player move\n";
        return false;
    }
    if (!WordGameTestAccess::usesAttractPlayerPhaseZeroHold()) {
        std::cerr << "word Troggle stage: Demo player phase-zero framebuffer hold\n";
        return false;
    }
    if (!WordGameTestAccess::usesCompleteAttractChewAfterTroggleComposite()) {
        std::cerr << "word Troggle stage: complete Demo chew/Troggle composite\n";
        return false;
    }
    if (!WordGameTestAccess::liveWordDirectionalCompositeMatches()) {
        std::cerr << "word Troggle stage: live four-direction composite\n";
        return false;
    }
    if (!WordGameTestAccess::liveWordCollisionBiteCompositeMatches()) {
        std::cerr << "word Troggle stage: live collision bite composite\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredLetterMovementAliases()) {
        std::cerr << "word Troggle stage: letter movement aliases\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredMixedGameplayInputRing()) {
        std::cerr << "word Troggle stage: shared gameplay key/pointer FIFO\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredCartoonInputGate()) {
        std::cerr << "word Troggle stage: cartoon input gate\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredBusyPauseGate()) {
        std::cerr << "word Troggle stage: busy-player pause gate\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredMunchDispatcher()) {
        std::cerr << "word Troggle stage: player chew callbacks\n";
        return false;
    }
    if (!WordGameTestAccess::preservesNewMunchTimerAfterMovement(initialization)) {
        std::cerr << "word Troggle stage: queued chew creation boundary\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredPlayerCollisionStateGate(initialization)) {
        std::cerr << "word Troggle stage: player collision state gate\n";
        return false;
    }
    if (!WordGameTestAccess::preservesMixedTroggleJobOrder()) {
        std::cerr << "word Troggle stage: mixed scheduler job order\n";
        return false;
    }
    if (!WordGameTestAccess::initializesAndWarnsOnOriginalJobs(initialization)) {
        std::cerr << "word Troggle stage: board jobs/warning/render\n";
        return false;
    }
    WordGame collision(GraphicsMode::Vga256, 0x2345);
    if (!WordGameTestAccess::defersCollisionLifeLossToTick21(collision)) {
        std::cerr << "word Troggle stage: collision terminal/recovery\n";
        return false;
    }
    if (!WordGameTestAccess::preservesDemoCollisionTerminalOrderAndFrameRemainder()) {
        std::cerr << "word Troggle stage: Demo collision terminal scheduler order\n";
        return false;
    }
    WordGame trails(GraphicsMode::Vga256, 0x3456);
    if (!WordGameTestAccess::appliesWordTrailEffects(trails)) {
        std::cerr << "word Troggle stage: species trails\n";
        return false;
    }
    WordGame completion(GraphicsMode::Vga256, 0x4567);
    if (!WordGameTestAccess::abortsTrailCallbacksAfterBoardCompletion(completion)) {
        std::cerr << "word Troggle stage: board-complete callback abort\n";
        return false;
    }
    return true;
}

bool wordFeedbackAndTerminalRoutesMatch() {
    if (!WordGameTestAccess::maximumFinalAnswerRoutesBeforeBoardCompletion()) {
        std::cerr << "word terminal stage: maximum/final-answer ordering\n";
        return false;
    }
    if (!WordGameTestAccess::usesRecoveredFeedbackQuitRoute()) {
        std::cerr << "word terminal stage: feedback Escape/Yes-No routing\n";
        return false;
    }
    WordGame wrong(GraphicsMode::Vga256, 0x4567);
    if (!WordGameTestAccess::wrongFeedbackAndPostGameRoute(wrong)) {
        std::cerr << "word terminal stage: wrong feedback/final loss/replay\n";
        return false;
    }
    WordGame hall(GraphicsMode::Vga256, 0x5678);
    if (!WordGameTestAccess::nameEntryAndHallAdmissionRoute(hall)) {
        std::cerr << "word terminal stage: name entry/Hall admission\n";
        return false;
    }
    return true;
}

bool wordHallPresentationMatches() {
    WordGame hall(GraphicsMode::Vga256, 0x6789);
    return WordGameTestAccess::hallPresentationMatches(hall);
}

bool requestedToggleHotkeyEdgesMatch() {
    constexpr LPARAM AltContext = static_cast<LPARAM>(1) << 29;
    constexpr LPARAM PreviousKeyState = static_cast<LPARAM>(1) << 30;
    return isInitialWin32KeyPress(0) &&
           isInitialWin32KeyPress(AltContext) &&
           !isInitialWin32KeyPress(PreviousKeyState) &&
           !isInitialWin32KeyPress(AltContext | PreviousKeyState);
}

} // namespace

int main() {
    if (std::getenv("MUNCHERS_AUDIT_DUMP_WORD_ATTRACT_DIR")) {
        return WordGameTestAccess::dumpFullLiveAttractSequence() ? 0 : 13;
    }
    if (std::getenv("MUNCHERS_AUDIT_FIND_WORD_ATTRACT_SEED")) {
        return WordGameTestAccess::findFullLiveAttractSeed() ? 0 : 12;
    }
    if (!requestedToggleHotkeyEdgesMatch()) {
        std::cerr << "Requested shell hotkey edge filtering regressed\n";
        return 11;
    }
    if (!wordTitleAndInputRoutesMatch()) {
        std::cerr << "Word Munchers native title/input/chew runtime regressed\n";
        return 1;
    }
    if (!sharedLauncherRoutesMatch()) {
        std::cerr << "Shared Munchers launcher/return lifecycle regressed\n";
        return 2;
    }
    if (!SuperGameTestAccess::nativeChewAndLevelSelectRoute()) {
        std::cerr << "Super Munchers native board/chew/level-select runtime regressed\n";
        return 14;
    }
    if (!SuperGameTestAccess::startupAndInformationRoute()) {
        std::cerr << "Super Munchers startup/Information route regressed\n";
        return 22;
    }
    if (!SuperGameTestAccess::optionsAndPersistenceRoute()) {
        std::cerr << "Super Munchers options/settings persistence regressed\n";
        return 17;
    }
    if (!SuperGameTestAccess::streamingAudioRoute()) {
        std::cerr << "Super Munchers continuous audio route regressed\n";
        return 18;
    }
    if (!SuperGameTestAccess::schedulerCoalescingRoute()) {
        std::cerr << "Super Munchers public scheduler ordering regressed\n";
        return 23;
    }
    if (!SuperGameTestAccess::attractDemoRoute()) {
        std::cerr << "Super Munchers attract Demo route regressed\n";
        return 19;
    }
    if (!SuperGameTestAccess::missionPresentationMatches()) {
        std::cerr << "Super Munchers native mission presentation regressed\n";
        return 15;
    }
    if (!SuperGameTestAccess::missionCadenceAndInteractionRoute()) {
        std::cerr << "Super Munchers mission cadence/input/progression regressed\n";
        return 20;
    }
    if (!SuperGameTestAccess::missionTextFramesMatch()) {
        std::cerr << "Super Munchers exact mission DATA2 text regressed\n";
        return 24;
    }
    if (!SuperGameTestAccess::safeZoneAndTroggleLifecycleRoute()) {
        std::cerr << "Super Munchers safe-zone/Troggle lifecycle regressed\n";
        return 21;
    }
    if (!SuperGameTestAccess::pauseAliasesAndAnswerVisionRoute()) {
        std::cerr << "Super Munchers pause/aliases/Answer Vision regressed\n";
        return 23;
    }
    if (!superAudioProfileMatches()) {
        std::cerr << "Super Munchers native audio profile regressed\n";
        return 16;
    }
    if (!altShortcutRoutesMatch()) {
        std::cerr << "Shared Number/Word Alt shortcut control flow regressed\n";
        return 8;
    }
    if (!altMusicResumeSelectorStateMatches()) {
        std::cerr << "Shared Number/Word Alt+M selector-state resume regressed\n";
        return 9;
    }
    if (!altSpeakerResumeSelectorStateMatches()) {
        std::cerr << "Shared Number/Word Alt+P selector-state resume regressed\n";
        return 10;
    }
    if (!wordCgaPresentationUsesHardwareColors()) {
        std::cerr << "Word Munchers CGA runtime escaped its hardware color presentation\n";
        return 3;
    }
    if (!wordHallPresentationMatches()) {
        std::cerr << "Word Munchers Hall presentation regressed\n";
        return 4;
    }
    if (!wordScriptedSceneHostMatches()) {
        std::cerr << "Word Munchers scripted-scene host regressed\n";
        return 5;
    }
    if (!wordTroggleRuntimeMatches()) {
        std::cerr << "Word Munchers Troggle runtime regressed\n";
        return 6;
    }
    if (!wordFeedbackAndTerminalRoutesMatch()) {
        std::cerr << "Word Munchers feedback/post-game runtime regressed\n";
        return 7;
    }
    std::cout << "Shared launcher and Word Munchers headless runtime tests passed\n";
    return 0;
}
