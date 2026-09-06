#include "super_runtime.h"

#include "mecc_sound.h"
#include "resource_ids.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 5> TitleLabels = {
    "Play Super Munchers", "Hall of Heroes", "Information", "Options", "Quit"
};
constexpr std::array<int, 5> TitleRows = {91, 102, 113, 124, 135};
// PCXF 6005 and 6009 preserve their original eight-bit pixel indexes but omit
// a 256-color trailer.  The DOS title presenter installs this resident DAC;
// every populated entry below is recovered from identical raw-index pixels in
// the lossless supplied-executable startup/title capture.
constexpr std::array<std::uint32_t, 256> RecoveredSuperPalette = [] {
    std::array<std::uint32_t, 256> palette{};
    palette[3] = 0xffffff; palette[4] = 0xaa0000; palette[6] = 0xaa0000;
    palette[7] = 0xaaaaaa; palette[8] = 0x555555; palette[9] = 0x5555ff;
    palette[10] = 0x55ff55; palette[11] = 0x0000aa; palette[12] = 0xff5555;
    palette[13] = 0x00aa00; palette[15] = 0xffffff; palette[16] = 0xffffff;
    palette[17] = 0xefefef; palette[18] = 0xdbdbdb; palette[19] = 0xcbcbcb;
    palette[20] = 0xbababa; palette[22] = 0xbababa; palette[23] = 0x868686;
    palette[24] = 0x757575; palette[25] = 0x656565; palette[27] = 0x454545;
    palette[28] = 0x343434; palette[29] = 0x202020; palette[30] = 0x101010;
    palette[34] = 0xff9e9e; palette[35] = 0xff7d7d; palette[36] = 0xff5d5d;
    palette[37] = 0xff4141; palette[38] = 0xff5d5d; palette[39] = 0xff0000;
    palette[41] = 0xcf0000; palette[42] = 0xb60000; palette[43] = 0x9e0000;
    palette[44] = 0x860000; palette[45] = 0x710000; palette[46] = 0x590000;
    palette[47] = 0x410000; palette[57] = 0xcf6100; palette[58] = 0xb65500;
    palette[59] = 0x9e4d00; palette[60] = 0x864100; palette[61] = 0x713800;
    palette[62] = 0x592c00; palette[63] = 0x412000; palette[68] = 0xfffb5d;
    palette[69] = 0xfff741; palette[70] = 0xfffb5d; palette[71] = 0xfff700;
    palette[72] = 0xe7db00; palette[73] = 0xcfc700; palette[74] = 0xb6ae00;
    palette[77] = 0x716d00; palette[78] = 0x595500; palette[79] = 0x414100;
    palette[86] = 0xd3ff5d; palette[87] = 0xa2ff00; palette[88] = 0x92e700;
    palette[89] = 0x82cf00; palette[90] = 0x75b600; palette[91] = 0x619e00;
    palette[92] = 0x518600; palette[93] = 0x457100; palette[94] = 0x345900;
    palette[95] = 0x284100; palette[103] = 0xfff700; palette[104] = 0xe7db00;
    palette[105] = 0xcfc700; palette[108] = 0x868600; palette[109] = 0x716d00;
    palette[110] = 0x595500; palette[111] = 0x414100;
    palette[123] = 0x009e9e; palette[129] = 0xbae3ff;
    palette[130] = 0x9ed7ff; palette[131] = 0x7dcbff; palette[132] = 0x5dbeff;
    palette[133] = 0x41b2ff; palette[134] = 0x5dbeff; palette[135] = 0x009eff;
    palette[136] = 0x008ee7; palette[137] = 0x007dcf; palette[138] = 0x006db6;
    palette[140] = 0x004d86; palette[141] = 0x004171; palette[142] = 0x003059;
    palette[143] = 0x002441; palette[161] = 0xe7baff; palette[162] = 0xdb9eff;
    palette[163] = 0xd37dff; palette[164] = 0xcb5dff; palette[165] = 0xbe41ff;
    palette[166] = 0xcb5dff; palette[167] = 0xaa00ff; palette[168] = 0x9a00e7;
    palette[169] = 0x8200cf; palette[170] = 0x7500b6; palette[171] = 0x61009e;
    palette[172] = 0x510086; palette[173] = 0x450071; palette[179] = 0xff7dff;
    palette[180] = 0xff5dff; palette[181] = 0xff41ff; palette[189] = 0x6d0071;
    palette[215] = 0x82512c; palette[218] = 0x5d4120; palette[219] = 0x553c1c;
    palette[220] = 0x493818;
    return palette;
}();

const std::array<std::uint32_t, 256>& superResidentPalette() {
    static const std::array<std::uint32_t, 256> palette = [] {
        std::array<std::uint32_t, 256> result = RecoveredSuperPalette;

        // Super's trailer-less PCXF files use the same resident 6-bit ramps
        // carried by the collection's palette-bearing Number sheet.  The SM
        // presenter repeats each block's +4 colour at +6, then aliases its
        // Muncher block 96..111 to the yellow block 64..79.  This rule exactly
        // reproduces every slot independently measured in live Super title,
        // board, chew, and Troggle frames; those measurements remain in
        // RecoveredSuperPalette and take precedence below.
        std::array<std::uint32_t, 256> derived{};
        const ResArchive numberGraphics(loadEmbeddedResource(IDR_NMCGA_RES));
        const BlobView sheet = numberGraphics.find("PCXF", 1007);
        if (sheet.size >= 769 && sheet.data[sheet.size - 769] == 12) {
            const std::uint8_t* colors = sheet.data + sheet.size - 768;
            const auto expandDac = [](const std::uint8_t component) {
                const std::uint8_t sixBit = component >> 2;
                return static_cast<std::uint8_t>((sixBit << 2) | (sixBit >> 4));
            };
            for (std::size_t index = 0; index < derived.size(); ++index) {
                derived[index] =
                    (static_cast<std::uint32_t>(expandDac(colors[index * 3])) << 16) |
                    (static_cast<std::uint32_t>(expandDac(colors[index * 3 + 1])) << 8) |
                    static_cast<std::uint32_t>(expandDac(colors[index * 3 + 2]));
            }
            for (std::size_t base = 0; base < derived.size(); base += 16) {
                derived[base + 6] = derived[base + 4];
            }
            for (std::size_t index = 96; index < 112; ++index) {
                derived[index] = derived[index - 32];
            }
            for (std::size_t index = 0; index < result.size(); ++index) {
                if (result[index] == 0) result[index] = derived[index];
            }
        }
        return result;
    }();
    return palette;
}

void drawSuperTitlePage(Renderer& renderer, const Image& image) {
    if (image.sourceIndices.size() != image.pixels.size()) {
        renderer.drawImage(image, 0, 0, false, true);
        return;
    }
    const int width = std::min(Renderer::Width, image.width);
    const int height = std::min(Renderer::Height, image.height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y * image.width + x);
            renderer.pixel(x, y, superResidentPalette()[image.sourceIndices[offset]]);
        }
    }
}

void drawSuperDialogFrame(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);
}
constexpr std::array<std::string_view, 7> GameLabels = {
    "Animals", "Famous Americans", "Food and Health", "Geography",
    "Music", "Odds 'n' Ends", "Challenge"
};
constexpr std::array<std::string_view, 6> OptionLabels = {
    "Set Content", "Turn Answer Vision OFF", "Erase Hall of Heroes",
    "Set Password", "Turn Joystick ON", "Calibrate Joystick"
};
constexpr std::array<std::string_view, 5> ContentOptionLabels = {
    "Quick Set", "Set Games", "Adjust Difficulty", "Turn Negations OFF",
    "Preview Words"
};
constexpr std::array<std::string_view, 6> QuickSetLabels = {
    "No Change", "Grades 3-4", "Grades 5-6", "Grades 7-8",
    "High School", "Ultimate"
};
// SM image 0x29548..0x29565 (DS:2688..26A5), consumed by the Quick Set
// controller at 0x17CF0.  Each preset supplies the inclusive CATG metadata[1]
// ceiling, difficulty selector, and negations flag respectively.
constexpr std::array<int, 5> QuickSetRuleThresholds = {10, 20, 30, 40, 50};
constexpr std::array<int, 5> QuickSetDifficultyModes = {0, 0, 1, 1, 2};
constexpr std::array<bool, 5> QuickSetNegations = {false, false, true, true, true};
constexpr std::array<std::string_view, 4> DifficultyLabels = {
    "Beginner", "Advanced", "Genius", "Player Chooses"
};
constexpr double FeedbackDuration = 75.0 / SuperGame::SchedulerTicksPerSecond;
constexpr double LevelCompleteDuration = 45.0 / SuperGame::SchedulerTicksPerSecond;
constexpr double TransformationDrainPeriod = 10.0 / SuperGame::SchedulerTicksPerSecond;
constexpr double TroggleWarningDuration = 90.0 / SuperGame::SchedulerTicksPerSecond;
constexpr double TitleIdleDuration =
    static_cast<double>(SuperGame::TitleIdleTicks) /
    SuperGame::SchedulerTicksPerSecond;
constexpr std::array<int, 12> EnemyLimits = {
    1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3
};
constexpr std::array<int, 12> EnemyArrivalTicks = {
    300, 300, 270, 270, 240, 210, 180, 150, 120, 90, 60, 30
};
constexpr std::array<int, 12> EnemyMoveTicks = {
    3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1
};
constexpr std::array<int, 12> SafeZoneJobLimits = {
    2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1
};
constexpr std::array<int, 12> InitialSafeZoneCounts = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
constexpr double TroggleCannibalAnimationDuration =
    21.0 / SuperGame::SchedulerTicksPerSecond;
constexpr std::array<std::array<int, 5>, 12> EnemyTypeWeights = {{
    {{100,  0,  0,  0,  0}}, {{90,  0, 10,  0,  0}},
    {{ 80,  0, 10, 10,  0}}, {{70, 10, 10, 10,  0}},
    {{ 60, 10, 10, 10, 10}}, {{50, 15, 10, 15, 10}},
    {{ 45, 15, 15, 15, 10}}, {{40, 15, 15, 15, 15}},
    {{ 35, 15, 20, 15, 15}}, {{30, 15, 20, 20, 15}},
    {{ 25, 20, 20, 20, 15}}, {{20, 20, 20, 20, 20}},
}};

std::uint32_t menuTextColor(const bool selected) {
    return selected ? Colors::Black : Colors::White;
}

std::string superDataEntry(const ResArchive& archive, const std::uint32_t id,
                           const int index) {
    const BlobView data = archive.find("DATA", id);
    if (!data || data.size < 2 || index < 0) return {};
    const auto read16 = [&data](const std::size_t offset) -> std::uint16_t {
        if (offset + 2 > data.size) return 0;
        return static_cast<std::uint16_t>(data.data[offset]) |
               static_cast<std::uint16_t>(
                   static_cast<std::uint16_t>(data.data[offset + 1]) << 8);
    };
    const int count = read16(0);
    if (index >= count || 2u + static_cast<std::size_t>(count) * 2u > data.size) {
        return {};
    }
    const std::size_t begin = read16(2u + static_cast<std::size_t>(index) * 2u);
    const std::size_t bound = index + 1 < count
        ? read16(2u + static_cast<std::size_t>(index + 1) * 2u) : data.size;
    if (begin >= data.size || bound < begin || bound > data.size) return {};
    std::size_t end = begin;
    while (end < bound && data.data[end] != 0) ++end;
    return {reinterpret_cast<const char*>(data.data + begin), end - begin};
}

void replaceMissionPlaceholder(std::string& text, std::string replacement,
                               const bool surroundWithSpaces = false) {
    const std::size_t begin = text.find('x');
    if (begin == std::string::npos) return;
    std::size_t end = begin;
    while (end < text.size() && text[end] == 'x') ++end;
    const std::size_t width = end - begin;
    if (surroundWithSpaces) replacement = " " + replacement + " ";
    if (replacement.size() < width) replacement.append(width - replacement.size(), ' ');
    if (replacement.size() > width) replacement.resize(width);
    text.replace(begin, width, replacement);
}

void drawMissionDataBlock(Renderer& renderer, const GemFont& font,
                          const std::string& text, const int y) {
    std::size_t begin = 0;
    int row = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find('\n', begin);
        const std::string_view line(text.data() + begin,
            (end == std::string::npos ? text.size() : end) - begin);
        renderer.drawText(font, 0, y + row * 10, line, Colors::White);
        ++row;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
}

} // namespace

SuperGame::SuperGame(const GraphicsMode graphicsMode,
                     const std::uint16_t randomSeed,
                     const bool settingsPersistence)
    : graphicsMode_(graphicsMode),
      assets_(graphicsMode, GameAssetSet::SuperMunchers),
      content_(assets_.gameArchive()),
      config_(loadEmbeddedResource(IDR_SM_CFG)),
      random_(randomSeed),
      core_(content_, random_),
      answerVisionOption_(config_.answerVisionEnabled()),
      joystickEnabled_(config_.joystickEnabled()),
      settingsPersistenceEnabled_(settingsPersistence) {
    boardSettings_ = config_.boardSettings(20);
    difficultyMode_ = std::clamp(
        static_cast<int>(config_.gameplayDifficulty()) - 1, 0, 3);
    quickSet_ = std::clamp(static_cast<int>(config_.quickSet()), 0, 4);
    password_ = config_.password();
    passwordHint_ = config_.hint();
    initializeHalls();
    loadSettings();
}

double SuperGame::movementDuration(const int direction) {
    const bool vertical = direction == 0 || direction == 2;
    return static_cast<double>(vertical ? 5 : 6) / SchedulerTicksPerSecond;
}

int SuperGame::movementPhase() const {
    if (!moving_) return 0;
    const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
    const int steps = vertical ? 5 : 6;
    const double duration = movementDuration(moveDirection_);
    const double elapsed = std::clamp(duration - moveTimer_, 0.0, duration);
    return std::clamp(static_cast<int>(
        std::floor(elapsed * SchedulerTicksPerSecond + 1e-7)), 0, steps - 1);
}

int SuperGame::movementFrame(const int direction, const int phase) {
    constexpr std::array<int, 6> offsets = {2, 2, 1, 0, 1, 2};
    return std::clamp(direction, 0, 3) * 3 +
           offsets[static_cast<std::size_t>(
               std::clamp(phase, 0, static_cast<int>(offsets.size()) - 1))];
}

int SuperGame::munchFrame(const int elapsedTicks) {
    constexpr std::array<int, 8> frames = {12, 13, 14, 13, 12, 13, 14, 13};
    return frames[static_cast<std::size_t>(
        std::clamp(elapsedTicks, 0, static_cast<int>(frames.size()) - 1))];
}

int SuperGame::visiblePlayerFrame() const {
    if (munching_) {
        const double elapsed = MunchAnimationDuration - std::max(0.0, munchTimer_);
        const int tick = std::clamp(static_cast<int>(
            std::floor(elapsed * SchedulerTicksPerSecond + 1e-7)),
            0, MunchAnimationTicks - 1);
        return munchFrame(tick);
    }
    if (moving_) return movementFrame(moveDirection_, movementPhase());
    return playerTerminalFrame_;
}

int SuperGame::visibleTransformationFrame() const {
    if (!transforming_) return -1;
    const double duration = static_cast<double>(TransformationAnimationTicks) /
                            SchedulerTicksPerSecond;
    const double elapsed = duration - std::max(0.0, transformationAnimationTimer_);
    return std::clamp(static_cast<int>(
        std::floor(elapsed * SchedulerTicksPerSecond + 1e-7)),
        0, TransformationAnimationTicks - 1);
}

void SuperGame::update(double seconds) {
    seconds = std::max(0.0, seconds);
    // Startup and title waits are synchronous page clocks rather than active
    // board jobs. Preserve their elapsed-time contract exactly.
    if (page_ == SuperGamePage::StartupVersion) {
        startupVersionTickAccumulator_ += seconds * SchedulerTicksPerSecond;
        if (startupVersionTickAccumulator_ >= 300.0) {
            startupVersionTickAccumulator_ = 300.0;
            page_ = SuperGamePage::StartupSplash;
        }
        return;
    }
    if (page_ == SuperGamePage::StartupSplash) {
        // The logo page is a key-held interstitial.  It has no automatic
        // deadline; the common dispatcher advances it on the next key.
        return;
    }
    if (page_ == SuperGamePage::Title) {
        titleIdleTimer_ -= seconds;
        if (titleIdleTimer_ <= 1e-9) startDemo();
        return;
    }
    // The common MECC controller dispatches one shared public scheduler tick;
    // it never subtracts an arbitrary host-frame duration independently from
    // every job. Quantize here so coalesced and split Win32 updates produce
    // identical callback boundaries and PRNG order.
    schedulerTickAccumulator_ += seconds * SchedulerTicksPerSecond;
    while (schedulerTickAccumulator_ + 1e-9 >= 1.0) {
        schedulerTickAccumulator_ = std::max(0.0, schedulerTickAccumulator_ - 1.0);
        updateSchedulerTick();
    }
}

void SuperGame::updateSchedulerTick() {
    constexpr double seconds = 1.0 / SchedulerTicksPerSecond;
    if (page_ == SuperGamePage::StartupVersion) {
        startupVersionTickAccumulator_ += seconds * SchedulerTicksPerSecond;
        if (startupVersionTickAccumulator_ >= 300.0) {
            startupVersionTickAccumulator_ = 300.0;
            page_ = SuperGamePage::StartupSplash;
        }
        return;
    }
    if (page_ == SuperGamePage::StartupSplash) {
        return;
    }
    if (page_ == SuperGamePage::Title) {
        titleIdleTimer_ -= seconds;
        if (titleIdleTimer_ <= 1e-9) startDemo();
        return;
    }

    if (page_ != SuperGamePage::Playing &&
        page_ != SuperGamePage::Feedback &&
        page_ != SuperGamePage::LevelComplete) return;
    if (page_ == SuperGamePage::Playing && core_.actionFrozen()) return;

    // Board setup installs safe-zone records before the Muncher and Troggle
    // records. Preserve that physical visit order on every public tick.
    if (page_ == SuperGamePage::Playing) {
        if (!updateSafeZones(seconds)) return;
        if (page_ != SuperGamePage::Playing) return;
    }

    if (moving_) {
        moveTimer_ -= seconds;
        if (moveTimer_ <= 0.0) {
            moving_ = false;
            moveTimer_ = 0.0;
            playerRow_ = moveToRow_;
            playerColumn_ = moveToColumn_;
            const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
            playerTerminalFrame_ = moveDirection_ * 3 + (vertical ? 2 : 1);
            if (const int slot = enemyAtPlayer(); slot >= 0) {
                collideWithEnemy(slot);
            } else {
                (void)core_.ensureTransformationCell(static_cast<std::size_t>(
                    playerRow_ * BoardColumns + playerColumn_));
                processInputQueue();
            }
        }
    }
    if (munching_) {
        munchTimer_ -= seconds;
        if (munchTimer_ <= 0.0) resolveMunch();
    }
    if (transforming_) {
        transformationAnimationTimer_ -= seconds;
        if (transformationAnimationTimer_ <= 0.0) {
            transforming_ = false;
            transformationAnimationTimer_ = 0.0;
            processInputQueue();
        }
    }
    if (page_ == SuperGamePage::Feedback) {
        feedbackTimer_ -= seconds;
        if (feedbackTimer_ <= 0.0) finishFeedback();
    }
    if (page_ == SuperGamePage::LevelComplete) {
        if (missionScene_.valid()) updateMission(seconds);
        else {
            levelCompleteTimer_ -= seconds;
            if (levelCompleteTimer_ <= 0.0) advanceBoard();
        }
    }
    if (page_ == SuperGamePage::Playing && core_.superForm() && !transforming_ &&
        !collisionActive_ && !core_.actionFrozen()) {
        transformationDrainTimer_ -= seconds;
        while (transformationDrainTimer_ <= 0.0 && core_.superForm()) {
            (void)core_.drainTransformationStep();
            transformationDrainTimer_ += TransformationDrainPeriod;
        }
    }
    if (page_ == SuperGamePage::Playing && !core_.actionFrozen()) {
        updateEnemies(seconds);
    }
    // The fixed selector-5 Demo record is physically appended after safe
    // zones, the Muncher, every Troggle, and the mask-1 controllers. It must
    // consume its reload/direction draws last, including on a tick that starts
    // a collision but leaves the board controller alive.
    if (demoMode_ && page_ == SuperGamePage::Playing) updateDemo();
}

void SuperGame::activateTitleSelection() {
    switch (menuSelection_) {
    case 0:
        informationFromPlay_ = false;
        difficultyChosenForPlay_ = false;
        page_ = SuperGamePage::InstructionsQuestion;
        menuSelection_ = 1;
        break;
    case 1:
        hallPostGame_ = false;
        page_ = SuperGamePage::HallSelect;
        menuSelection_ = 0;
        break;
    case 2:
        informationFromPlay_ = false;
        page_ = SuperGamePage::Information;
        informationPage_ = 0;
        break;
    case 3:
        if (password_.empty()) {
            page_ = SuperGamePage::Options;
            menuSelection_ = 0;
        } else {
            passwordEntry_.clear();
            passwordError_ = false;
            page_ = SuperGamePage::OptionsPasswordPrompt;
        }
        break;
    case 4:
        shouldQuit_ = true;
        break;
    default: break;
    }
}

void SuperGame::activateGameSelection() {
    if (menuSelection_ >= 0 && menuSelection_ < static_cast<int>(gameChoices_.size())) {
        const int game = gameChoices_[static_cast<std::size_t>(menuSelection_)];
        if (difficultyMode_ == 3 && difficultyChosenForPlay_) startGame(game, 1);
        else requestStartGame(game);
    }
}

void SuperGame::beginPlaySelection() {
    if (difficultyMode_ == 3) {
        page_ = SuperGamePage::GameDifficultySelect;
        menuSelection_ = 0;
        return;
    }
    prepareGameChoices();
    if (gameChoices_.size() == 1) requestStartGame(gameChoices_.front());
    else {
        page_ = SuperGamePage::GameSelect;
        menuSelection_ = 0;
    }
}

void SuperGame::prepareGameChoices() {
    gameChoices_.clear();
    for (int topic = 0; topic < static_cast<int>(SuperTopicCount); ++topic) {
        if (boardSettings_.selectedTopics[static_cast<std::size_t>(topic)]) {
            gameChoices_.push_back(topic);
        }
    }
    if (gameChoices_.size() >= 2) gameChoices_.push_back(6);
    if (gameChoices_.empty()) gameChoices_.push_back(0);
}

void SuperGame::requestStartGame(const int gameIndex, const int level) {
    if (difficultyMode_ == 3) {
        pendingGame_ = std::clamp(gameIndex, 0, 6);
        pendingLevel_ = std::max(1, level);
        page_ = SuperGamePage::GameDifficultySelect;
        menuSelection_ = 1;
        return;
    }
    static constexpr std::array<int, 3> Difficulties = {10, 20, 30};
    boardSettings_.difficulty = Difficulties[static_cast<std::size_t>(
        std::clamp(difficultyMode_, 0, 2))];
    startGame(gameIndex, level);
}

void SuperGame::startGame(const int gameIndex, const int level) {
    demoMode_ = false;
    oplPlayer_.stop();
    missionIndex_ = -1;
    missionProgress_ = 0;
    levelsSinceMission_ = 0;
    missionScene_ = SceneScript{};
    missionCallbacks_.reset();
    selectedGame_ = std::clamp(gameIndex, 0, 6);
    if (!core_.configure(boardSettings_, selectedGame_, false,
                         answerVisionOption_) ||
        !core_.startAtLevel(level)) {
        feedbackMessage_ = "Unable to load game content.";
        feedbackTimer_ = FeedbackDuration;
        page_ = SuperGamePage::Feedback;
        return;
    }
    initializeSafeZones();
    initializePlayer();
    initializeEnemies();
    inputQueue_.clear();
    lastResolution_ = {};
    feedbackMessage_.clear();
    page_ = SuperGamePage::Playing;
    cheatOpen_ = false;
    transformationDrainTimer_ = TransformationDrainPeriod;
}

void SuperGame::startDemo() {
    prepareGameChoices();
    selectedGame_ = 6;
    // Image 0x08CD3 draws the zero-based 0..8 level before the Demo board
    // generator consumes its difficulty/rule/pool draws.
    const int level = random_.range(9) + 1;
    if (!core_.configure(boardSettings_, selectedGame_, true,
                         answerVisionOption_) || !core_.startAtLevel(level)) {
        titleIdleTimer_ = TitleIdleDuration;
        return;
    }
    initializeSafeZones();
    initializePlayer();
    initializeEnemies();
    inputQueue_.clear();
    lastResolution_ = {};
    feedbackMessage_.clear();
    page_ = SuperGamePage::Playing;
    demoMode_ = true;
    // DS:03B6 installs selector 5 with a 30-public-tick first countdown.
    demoActionTicks_ = 30;
    transformationDrainTimer_ = TransformationDrainPeriod;
    playAttractMusic();
}

void SuperGame::playAttractMusic() {
    // GSND 22 has no callable stream 16 conductor (the selector-table entry
    // is the resident empty sentinel), so Super's Demo does not install the
    // Number/Word attract loop. Gameplay and mission effects still share the
    // continuously buffered chip when they occur.
}

void SuperGame::updateDemo() {
    if (!core_.active()) {
        returnFromDemo();
        return;
    }

    // Callback 07DC:0821 begins by reloading the current record to a random
    // 15..44 ticks. The independent job keeps consuming PRNG and selecting a
    // key while the Muncher is busy; the ordinary player owner simply cannot
    // accept that injected key at the time.
    if (--demoActionTicks_ > 0) return;
    demoActionTicks_ = random_.range(30) + 15;

    const bool acceptsInjectedKey = page_ == SuperGamePage::Playing &&
        !moving_ && !munching_ && !transforming_ && !collisionActive_ &&
        !core_.actionFrozen() && enemyAtPlayer() < 0;
    const int current = playerRow_ * BoardColumns + playerColumn_;
    int signedValue = 0;
    if (current >= 0 && current < BoardCellCount &&
        !core_.eaten(static_cast<std::size_t>(current))) {
        signedValue = core_.board().cells[
            static_cast<std::size_t>(current)].signedSourceIndex;
    }

    if (signedValue > 0 ||
        (signedValue < 0 && random_.range(10) == 0)) {
        if (acceptsInjectedKey) beginMunch();
        return;
    }

    const int initialDirection = random_.range(4);
    int direction = initialDirection;
    for (int attempt = 0; attempt < 4; ++attempt) {
        int row = playerRow_;
        int column = playerColumn_;
        if (direction == 0) --row;
        if (direction == 1) ++column;
        if (direction == 2) ++row;
        if (direction == 3) --column;
        if (row >= 0 && row < BoardRows && column >= 0 && column < BoardColumns) {
            const int neighbor = row * BoardColumns + column;
            if (!core_.eaten(static_cast<std::size_t>(neighbor)) &&
                core_.board().cells[static_cast<std::size_t>(neighbor)]
                        .signedSourceIndex > 0) {
                break;
            }
        }
        direction = (direction + 1) % 4;
    }

    if (!acceptsInjectedKey) return;
    if (direction == 0) beginMove(playerRow_ - 1, playerColumn_, 0);
    if (direction == 1) beginMove(playerRow_, playerColumn_ + 1, 1);
    if (direction == 2) beginMove(playerRow_ + 1, playerColumn_, 2);
    if (direction == 3) beginMove(playerRow_, playerColumn_ - 1, 3);
}

void SuperGame::returnFromDemo() {
    demoMode_ = false;
    oplPlayer_.stop();
    effectPlayer_.stop();
    missionSoundPlayer_.stop();
    inputQueue_.clear();
    moving_ = false;
    munching_ = false;
    transforming_ = false;
    collisionActive_ = false;
    playerRecovering_ = false;
    safeCells_.fill(false);
    safeZoneJobs_.fill({});
    safeZoneJobCount_ = 0;
    page_ = SuperGamePage::Title;
    menuSelection_ = 0;
    titleIdleTimer_ = TitleIdleDuration;
}

void SuperGame::initializePlayer() {
    // Shared engine routine chooses the interior column before the interior row.
    playerColumn_ = random_.range(4) + 1;
    playerRow_ = random_.range(3) + 1;
    (void)core_.clearCellWithoutScore(static_cast<std::size_t>(
        playerRow_ * BoardColumns + playerColumn_));
    moveFromRow_ = moveToRow_ = playerRow_;
    moveFromColumn_ = moveToColumn_ = playerColumn_;
    moveDirection_ = 1;
    playerTerminalFrame_ = 7;
    moving_ = false;
    munching_ = false;
    transforming_ = false;
    collisionActive_ = false;
    moveTimer_ = 0.0;
    munchTimer_ = 0.0;
    transformationAnimationTimer_ = 0.0;
    collisionTimer_ = 0.0;
    collisionSlot_ = -1;
    feedbackEnemySlot_ = -1;
    playerRecovering_ = false;
    munchCellIndex_ = -1;
}

void SuperGame::beginMove(int row, int column, const int direction) {
    if (page_ != SuperGamePage::Playing || moving_ || munching_ ||
        transforming_ || collisionActive_ || core_.actionFrozen()) return;
    row = std::clamp(row, 0, BoardRows - 1);
    column = std::clamp(column, 0, BoardColumns - 1);
    if (row == playerRow_ && column == playerColumn_) return;
    moveFromRow_ = playerRow_;
    moveFromColumn_ = playerColumn_;
    moveToRow_ = row;
    moveToColumn_ = column;
    moveDirection_ = std::clamp(direction, 0, 3);
    moveTimer_ = movementDuration(moveDirection_);
    playerTerminalFrame_ = -1;
    moving_ = true;
}

void SuperGame::processInputQueue() {
    if (inputQueue_.empty() || page_ != SuperGamePage::Playing || moving_ ||
        munching_ || transforming_ || collisionActive_ || core_.actionFrozen()) return;
    const int input = inputQueue_.front();
    if (input > 0) {
        inputQueue_.pop_front();
        switch (input) {
        case 'I': beginMove(playerRow_ - 1, playerColumn_, 0); break;
        case 'K': beginMove(playerRow_, playerColumn_ + 1, 1); break;
        case 'M': beginMove(playerRow_ + 1, playerColumn_, 2); break;
        case 'J': beginMove(playerRow_, playerColumn_ - 1, 3); break;
        case ' ': beginMunch(); break;
        default: break;
        }
        return;
    }
    const int target = -input - 1;
    if (target < 0 || target >= BoardCellCount) {
        inputQueue_.pop_front();
        return;
    }
    const int current = playerRow_ * BoardColumns + playerColumn_;
    if (target == current) {
        inputQueue_.pop_front();
        beginMunch();
        return;
    }
    const int targetRow = target / BoardColumns;
    const int targetColumn = target % BoardColumns;
    const int rowDelta = targetRow - playerRow_;
    const int columnDelta = targetColumn - playerColumn_;
    int direction = rowDelta < 0 ? 0 : 2;
    if (columnDelta > 0 && columnDelta > std::abs(rowDelta)) direction = 1;
    if (columnDelta < 0 && -columnDelta > std::abs(rowDelta)) direction = 3;
    int row = playerRow_;
    int column = playerColumn_;
    if (direction == 0) --row;
    if (direction == 1) ++column;
    if (direction == 2) ++row;
    if (direction == 3) --column;
    if (row * BoardColumns + column == target) inputQueue_.pop_front();
    beginMove(row, column, direction);
}

void SuperGame::beginMunch() {
    if (page_ != SuperGamePage::Playing || moving_ || munching_ ||
        transforming_ || collisionActive_ || core_.actionFrozen()) return;
    const int index = playerRow_ * BoardColumns + playerColumn_;
    if (core_.eaten(static_cast<std::size_t>(index))) return;
    const bool transformation = core_.transformationCell() == index;
    const SuperBoardCell& cell = core_.board().cells[static_cast<std::size_t>(index)];
    if (!transformation && cell.signedSourceIndex == 0) return;
    if (!core_.beginMunchCell(static_cast<std::size_t>(index))) return;
    playGameplaySound(transformation || cell.correct ? 7 : 8);
    munchCellIndex_ = index;
    munchTimer_ = MunchAnimationDuration;
    playerTerminalFrame_ = -1;
    munching_ = true;
}

void SuperGame::resolveMunch() {
    if (!munching_ || munchCellIndex_ < 0) return;
    const int index = munchCellIndex_;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    playerTerminalFrame_ = 13;
    lastResolution_ = core_.resolveMunchCell(static_cast<std::size_t>(index));
    if (lastResolution_.kind == SuperMunchKind::Wrong) {
        feedbackMessage_ = lastResolution_.gameOver ? "Game over." : "That does not match.";
        feedbackTimer_ = FeedbackDuration;
        page_ = SuperGamePage::Feedback;
        return;
    }
    if (lastResolution_.maximumScore) {
        feedbackMessage_ = "Perfect score!";
        feedbackTimer_ = FeedbackDuration;
        page_ = SuperGamePage::Feedback;
        return;
    }
    if (lastResolution_.transformed) {
        transformationDrainTimer_ = TransformationDrainPeriod;
        transformationAnimationTimer_ =
            static_cast<double>(TransformationAnimationTicks) /
            SchedulerTicksPerSecond;
        transforming_ = true;
    }
    if (lastResolution_.boardComplete) {
        handleCompletedBoard();
        return;
    }
    (void)core_.ensureTransformationCell(static_cast<std::size_t>(index));
    processInputQueue();
}

void SuperGame::handleCompletedBoard() {
    enemies_.clear();
    enemySlots_.fill({});
    safeCells_.fill(false);
    safeZoneJobs_.fill({});
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    moving_ = false;
    munching_ = false;
    transforming_ = false;
    collisionActive_ = false;
    playerRecovering_ = false;
    inputQueue_.clear();
    page_ = SuperGamePage::LevelComplete;
    if (demoMode_) {
        levelCompleteTimer_ = LevelCompleteDuration;
    } else if (++levelsSinceMission_ >= 3 && startMission(missionProgress_)) {
        levelsSinceMission_ = 0;
        page_ = SuperGamePage::MissionIntro;
    } else {
        levelCompleteTimer_ = LevelCompleteDuration;
    }
    playGameplaySound(15);
}

void SuperGame::finishFeedback() {
    if (!core_.active() || lastResolution_.maximumScore) {
        if (demoMode_) {
            returnFromDemo();
            return;
        }
        beginPostGame();
        return;
    }
    page_ = SuperGamePage::Playing;
    feedbackMessage_.clear();
    playerRecovering_ = feedbackEnemySlot_ >= 0 && enemyAtPlayer() >= 0;
    feedbackEnemySlot_ = -1;
    lastResolution_ = {};
}

void SuperGame::advanceBoard() {
    if (!core_.advanceBoard()) {
        if (demoMode_) returnFromDemo();
        else {
            page_ = SuperGamePage::Title;
            menuSelection_ = 0;
            titleIdleTimer_ = TitleIdleDuration;
        }
        return;
    }
    initializeSafeZones();
    initializePlayer();
    initializeEnemies();
    page_ = SuperGamePage::Playing;
    transformationDrainTimer_ = TransformationDrainPeriod;
}

void SuperGame::initializeHalls() {
    for (std::size_t hall = 0; hall < halls_.size(); ++hall) {
        halls_[hall].clear();
        for (const SuperHallEntry& source : config_.halls()[hall].activeEntries()) {
            const std::string name = source.name();
            if (!name.empty() && source.score <= MuncherScore::MaximumScore) {
                halls_[hall].push_back({name, source.score});
            }
        }
        std::stable_sort(halls_[hall].begin(), halls_[hall].end(),
            [](const HallEntry& left, const HallEntry& right) {
                return left.score > right.score;
            });
    }
}

void SuperGame::beginPostGame() {
    oplPlayer_.stop();
    effectPlayer_.stop();
    missionSoundPlayer_.stop();
    enemies_.clear();
    enemySlots_.fill({});
    safeCells_.fill(false);
    safeZoneJobs_.fill({});
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    playerRecovering_ = false;
    inputQueue_.clear();
    const std::uint32_t score = static_cast<std::uint32_t>(
        std::clamp(core_.scoreState().score(), 0, MuncherScore::MaximumScore));
    auto& hall = halls_[static_cast<std::size_t>(selectedGame_)];
    const bool qualifies = score >= 50 &&
        (hall.size() < SuperHallTable::Capacity ||
         score > hall.back().score);
    hallPostGame_ = true;
    if (qualifies) {
        nameEntry_.clear();
        page_ = SuperGamePage::NameEntry;
    } else {
        page_ = SuperGamePage::Hall;
    }
}

void SuperGame::submitHallName() {
    std::string name = nameEntry_.empty() ? "The Unknown Muncher" : nameEntry_;
    if (name.size() > 25) name.resize(25);
    HallEntry entry{name, static_cast<std::uint32_t>(
        std::clamp(core_.scoreState().score(), 0, MuncherScore::MaximumScore))};
    auto& hall = halls_[static_cast<std::size_t>(selectedGame_)];
    const auto position = std::find_if(hall.begin(), hall.end(),
        [&entry](const HallEntry& current) { return entry.score > current.score; });
    hall.insert(position, std::move(entry));
    if (hall.size() > SuperHallTable::Capacity) hall.resize(SuperHallTable::Capacity);
    saveSettings();
    page_ = SuperGamePage::Hall;
    hallPostGame_ = true;
}

bool SuperGame::startMission(const int missionIndex) {
    static constexpr std::array<std::array<std::uint16_t, 10>, 5> orders = {{
        {{0, 1, 2, 3, 4, 5, 6}},
        {{1, 0, 5, 6}},
        {{3, 4, 5, 6, 0, 1, 2}},
        {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}},
        {{4, 7, 3, 5, 6, 8, 0, 1, 2, 9}},
    }};
    static constexpr std::array<std::size_t, 5> orderSizes = {7, 4, 7, 10, 10};
    if (missionIndex < 0 || missionIndex >= 5) return false;
    missionIndex_ = missionIndex;
    missionGraphicId_ = 2023 + missionIndex * 2;
    missionTickAccumulator_ = 0.0;
    missionSurfacePixels_.assign(
        static_cast<std::size_t>(Renderer::Width * Renderer::Height), Colors::Black);
    missionChoices_.clear();
    missionCorrectSelection_ = 0;
    missionPlayerX_ = 136;
    missionCaughtNumbers_ = 0;
    missionCatchFailureReason_ = 0;
    missionSucceeded_ = false;
    missionCatchFailed_ = false;
    missionFallingInZone_.fill(false);
    const int missionDifficulty = std::min(6, 3 + (core_.level() - 1) / 5);
    missionCallbacks_ = std::make_unique<SuperSceneCallbacks>(
        missionIndex_, missionDifficulty, random_, [this](const SceneActor& actor) {
            const auto frame = assets_.spriteFrame(
                static_cast<std::uint32_t>(missionGraphicId_), actor.frame);
            return actor.x + (frame ? frame->width - 1 : 0);
        });
    missionScene_.setExtendedCallback(
        [this](const std::uint16_t value, SceneActor& actor, const bool firstVisit) {
            return missionCallbacks_ &&
                   missionCallbacks_->invoke(value, actor, firstVisit);
        });
    const BlobView script = assets_.gameArchive().find(
        "SCPT", static_cast<std::uint32_t>(missionGraphicId_ - 1));
    const std::span<const std::uint16_t> order(
        orders[static_cast<std::size_t>(missionIndex_)].data(),
        orderSizes[static_cast<std::size_t>(missionIndex_)]);
    if (!missionScene_.load(script, order)) {
        missionCallbacks_.reset();
        missionIndex_ = -1;
        missionGraphicId_ = 0;
        return false;
    }
    // Unlike the Number/Word cartoons, these five ADLI banks do not expose a
    // callable stream 38 score. Their controller-visible streams are the
    // opcode-0D effects dispatched below; the other high indexes are resident
    // track bodies rather than host-callable music selectors.
    return true;
}

void SuperGame::updateMission(const double seconds) {
    missionTickAccumulator_ += std::max(0.0, seconds) * SchedulerTicksPerSecond;
    while (missionTickAccumulator_ >= 1.0 && missionScene_.valid() &&
           !missionScene_.finished()) {
        missionTickAccumulator_ -= 1.0;
        advanceMissionTick();
    }
    if (!missionScene_.valid() || missionScene_.finished()) finishMission();
}

void SuperGame::advanceMissionTick() {
    missionScene_.tick();
    if (missionIndex_ == 3) updateMissionCatchState();
    applyMissionPaintEvents();
    for (const std::uint16_t event : missionScene_.callbackEvents()) {
        playMissionSound(event);
    }
}

void SuperGame::applyMissionPaintEvents() {
    if (!missionScene_.valid()) return;
    if (missionSurfacePixels_.size() !=
        static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        missionSurfacePixels_.assign(
            static_cast<std::size_t>(Renderer::Width * Renderer::Height), Colors::Black);
    }
    Renderer surface(graphicsMode_);
    surface.replacePixels(missionSurfacePixels_);
    const auto clearActor = [&](const SceneActor& actor) {
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(missionGraphicId_), actor.frame);
        if (!frame) return;
        int x = actor.x >= Renderer::Width ? Renderer::Width - 1 : actor.x;
        int y = actor.y >= Renderer::Height ? Renderer::Height - 1 : actor.y;
        surface.fillRect(x, y, frame->width, frame->height, Colors::Black);
    };
    const auto redraw = [&] {
        const auto& actors = missionScene_.actors();
        for (auto actor = actors.rbegin(); actor != actors.rend(); ++actor) {
            if (!actor->visible || actor->ended) continue;
            drawSprite(surface, static_cast<std::uint32_t>(missionGraphicId_),
                       actor->frame, actor->x, actor->y);
        }
    };
    for (const ScenePaintEvent& event : missionScene_.paintEvents()) {
        if (!event.dirty) continue;
        if (event.previous.visible && !event.previous.ended) clearActor(event.previous);
        if (event.current.visible && !event.current.ended) clearActor(event.current);
        redraw();
    }
    missionSurfacePixels_ = surface.pixels();
}

void SuperGame::playMissionSound(const std::uint16_t event) {
    if (!soundOn_ || event > 0xffu || missionIndex_ < 0) return;
    const BlobView bank = assets_.gameArchive().find(
        speakerEffects_ ? "PSND" : "ADLI",
        static_cast<std::uint32_t>(21 + missionIndex_));
    // Super's opcode-0D payload is already the driver selector. Mission 5's
    // executable-backed timeline emits 7, which is the only matching audible
    // selector in ADLI 25; applying the Number/Word +3 host bias incorrectly
    // lands on absent stream 10.
    const std::uint8_t stream = static_cast<std::uint8_t>(event);
    if (!speakerEffects_) {
        MeccSound decoded = decodeMeccGSound(
            bank, stream, MeccSoundProfile::SuperMunchers);
        if (decoded.valid &&
            (oplPlayer_.playing() || oplPlayer_.startIdle())) {
            (void)oplPlayer_.playEffect(
                std::move(decoded.writes), decoded.durationMilliseconds);
        }
        return;
    }
    std::vector<std::uint8_t> wave = renderMeccSpeakerSoundToWave(
        bank, stream, 40, MeccSoundProfile::SuperMunchers);
    if (!wave.empty()) (void)missionSoundPlayer_.play(std::move(wave));
}

void SuperGame::finishMission() {
    oplPlayer_.stopAllChannels();
    missionSoundPlayer_.stop();
    missionTickAccumulator_ = 0.0;
    missionSurfacePixels_.clear();
    if (missionIndex_ >= 0 && missionIndex_ <= 2) {
        prepareMissionQuestion();
        page_ = SuperGamePage::MissionQuestion;
        menuSelection_ = 0;
        return;
    }
    completeMission(missionIndex_ == 4 ||
                    (missionIndex_ == 3 && !missionCatchFailed_ &&
                     missionCaughtNumbers_ > 0));
}

void SuperGame::prepareMissionQuestion() {
    missionChoices_.clear();
    if (!missionCallbacks_) return;
    missionCorrectSelection_ = std::clamp(missionCallbacks_->variant(), 0, 3);
    if (missionIndex_ == 0) {
        missionChoices_ = {"Bashful", "Worker", "Reggie", "Smartie"};
    } else if (missionIndex_ == 1) {
        missionChoices_ = {"tree", "bush", "stump", "rock"};
    } else if (missionIndex_ == 2) {
        static constexpr std::array<std::string_view, 10> PasswordWords = {
            "bear", "frog", "lion", "skunk", "crow",
            "duck", "snake", "deer", "eagle", "bee"
        };
        for (const std::uint8_t index : missionCallbacks_->sequence()) {
            missionChoices_.emplace_back(PasswordWords[index]);
        }
    }
}

void SuperGame::completeMission(const bool success) {
    missionSucceeded_ = success;
    if (success) missionProgress_ = (missionProgress_ + 1) % 5;
    missionCallbacks_.reset();
    missionGraphicId_ = 0;
    page_ = SuperGamePage::MissionResult;
    menuSelection_ = 0;
}

void SuperGame::updateMissionCatchState() {
    for (const SceneActor& actor : missionScene_.actors()) {
        const std::size_t slot = static_cast<std::size_t>(actor.thread) %
                                 missionFallingInZone_.size();
        const bool falling = actor.visible && !actor.ended &&
            actor.frame >= 12 && actor.frame <= 19;
        if (!falling || actor.y < 145) {
            missionFallingInZone_[slot] = false;
            continue;
        }
        if (missionFallingInZone_[slot]) continue;
        missionFallingInZone_[slot] = true;
        const auto source = assets_.spriteFrame(
            static_cast<std::uint32_t>(missionGraphicId_), actor.frame);
        const int width = source ? source->width : 16;
        const bool caught = actor.x + width >= missionPlayerX_ &&
                            actor.x <= missionPlayerX_ + 45;
        if (actor.frame <= 16) {
            if (caught) ++missionCaughtNumbers_;
            else {
                missionCatchFailed_ = true;
                if (missionCatchFailureReason_ == 0) missionCatchFailureReason_ = 1;
            }
        } else if (caught) {
            missionCatchFailed_ = true;
            if (missionCatchFailureReason_ == 0) {
                missionCatchFailureReason_ = actor.frame == 17 ? 2 : 3;
            }
        }
    }
}

int SuperGame::chooseEnemyType() {
    const int tier = std::clamp(core_.pressureTier(), 0, 11);
    int choice = random_.range(100);
    for (int type = 0; type < 5; ++type) {
        const int weight = EnemyTypeWeights[static_cast<std::size_t>(tier)]
                                          [static_cast<std::size_t>(type)];
        if (choice < weight) return type;
        choice -= weight;
    }
    return 0;
}

double SuperGame::enemySpawnDelay() {
    const int tier = std::clamp(core_.pressureTier(), 0, 11);
    const int ticks = EnemyArrivalTicks[static_cast<std::size_t>(tier)] +
                      random_.range(90);
    return static_cast<double>(ticks) / SchedulerTicksPerSecond;
}

double SuperGame::enemyDwellDelay() {
    return static_cast<double>(31 + random_.range(90)) / SchedulerTicksPerSecond;
}

double SuperGame::enemyMoveDuration(const int direction) const {
    const int tier = std::clamp(core_.pressureTier(), 0, 11);
    const int steps = direction == 0 || direction == 2 ? 5 : 6;
    return static_cast<double>(steps * EnemyMoveTicks[static_cast<std::size_t>(tier)]) /
           SchedulerTicksPerSecond;
}

int SuperGame::enemyMovePhase(const Enemy& enemy) const {
    if (!enemy.moving) return 0;
    const bool vertical = enemy.direction == 0 || enemy.direction == 2;
    const int steps = vertical ? 5 : 6;
    const int tier = std::clamp(core_.pressureTier(), 0, 11);
    const double interval = static_cast<double>(
        EnemyMoveTicks[static_cast<std::size_t>(tier)]) / SchedulerTicksPerSecond;
    const bool terminal = enemy.exiting && vertical;
    const double total = enemyMoveDuration(enemy.direction) +
                         (terminal ? interval : 0.0);
    const double elapsed = std::max(0.0,
        total - std::max(0.0, enemy.animationTimer));
    return std::clamp(static_cast<int>(std::floor(elapsed / interval + 1e-7)) + 1,
                      0, steps);
}

int SuperGame::enemyFrame(const Enemy& enemy) const {
    const int first = std::clamp(enemy.direction, 0, 3) * 3;
    if (enemy.biting || enemy.cannibalizing) {
        constexpr std::array<int, 6> BiteFrames = {12, 13, 14, 13, 12, 13};
        const double duration = TroggleCannibalAnimationDuration;
        const double remaining = enemy.cannibalizing
            ? enemy.cannibalTimer : collisionTimer_;
        const int tick = std::clamp(static_cast<int>(std::floor(
            (duration - std::max(0.0, remaining)) *
            SchedulerTicksPerSecond + 1e-7)), 0, 20);
        return BiteFrames[static_cast<std::size_t>(tick % BiteFrames.size())];
    }
    if (enemy.moving) {
        const int phase = enemyMovePhase(enemy);
        if (enemy.entering) {
            constexpr std::array<int, 5> ArrivalOffsets = {1, 2, 1, 0, 1};
            return first + ArrivalOffsets[static_cast<std::size_t>(
                std::clamp(phase - 2, 0, 4))];
        }
        constexpr std::array<int, 6> MovementOffsets = {2, 1, 0, 1, 2, 1};
        return first + MovementOffsets[static_cast<std::size_t>(
            std::clamp(phase - 1, 0, 5))];
    }
    if (enemy.entering) return first;
    return enemy.direction == 2 ? 15 : first + 1;
}

int SuperGame::chooseSafeZoneCell() {
    for (;;) {
        const int row = 1 + random_.range(BoardRows - 2);
        const int column = 1 + random_.range(BoardColumns - 2);
        const int index = row * BoardColumns + column;
        if (!safeCells_[static_cast<std::size_t>(index)]) return index;
    }
}

void SuperGame::initializeSafeZones() {
    safeCells_.fill(false);
    safeZoneJobs_.fill({});
    const int tier = std::clamp(core_.pressureTier(), 0, 11);
    safeZoneJobCount_ = SafeZoneJobLimits[static_cast<std::size_t>(tier)];
    const int initiallyActive = InitialSafeZoneCounts[static_cast<std::size_t>(tier)];
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        const int ticks = 210 + random_.range(210);
        job.period = static_cast<double>(ticks) / SchedulerTicksPerSecond;
        job.timer = std::max(0.0, job.period - 1.0 / SchedulerTicksPerSecond);
        if (index < initiallyActive) {
            job.active = true;
            job.cellIndex = chooseSafeZoneCell();
            safeCells_[static_cast<std::size_t>(job.cellIndex)] = true;
        }
    }
}

bool SuperGame::enemyIntersectsCell(const Enemy& enemy,
                                    const int row, const int column) {
    if (enemy.row == row && enemy.column == column) return true;
    return (enemy.moving || enemy.entering) &&
           enemy.fromRow == row && enemy.fromColumn == column;
}

bool SuperGame::safeAt(const int row, const int column) const {
    return row >= 0 && row < BoardRows && column >= 0 && column < BoardColumns &&
           safeCells_[static_cast<std::size_t>(row * BoardColumns + column)];
}

bool SuperGame::updateSafeZones(const double seconds) {
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        job.timer -= seconds;
        while (job.timer <= 1e-9) {
            if (job.active) {
                if (job.cellIndex >= 0 && job.cellIndex < BoardCellCount) {
                    safeCells_[static_cast<std::size_t>(job.cellIndex)] = false;
                }
                job.active = false;
                job.cellIndex = -1;
                playGameplaySound(5);
            } else {
                job.active = true;
                job.cellIndex = chooseSafeZoneCell();
                safeCells_[static_cast<std::size_t>(job.cellIndex)] = true;
                const int row = job.cellIndex / BoardColumns;
                const int column = job.cellIndex % BoardColumns;
                for (std::size_t enemyIndex = 0; enemyIndex < enemies_.size();) {
                    const Enemy enemy = enemies_[enemyIndex];
                    if (!enemyIntersectsCell(enemy, row, column)) {
                        ++enemyIndex;
                        continue;
                    }
                    if (!applyEnemyTrail(enemy)) return false;
                    if (collisionActive_ && enemy.slot == collisionSlot_) {
                        collisionActive_ = false;
                        collisionTimer_ = 0.0;
                        collisionSlot_ = -1;
                    }
                    rearmEnemy(enemy.slot);
                    enemies_.erase(enemies_.begin() +
                                   static_cast<std::ptrdiff_t>(enemyIndex));
                    playGameplaySound(11);
                }
                if (playerRecovering_ && enemyAtPlayer() < 0) {
                    playerRecovering_ = false;
                }
                playGameplaySound(4);
            }
            job.timer += job.period;
        }
    }
    return true;
}

void SuperGame::initializeEnemies() {
    enemies_.clear();
    enemySlots_.fill({});
    enemySlotCount_ = EnemyLimits[static_cast<std::size_t>(
        std::clamp(core_.pressureTier(), 0, 11))];
    for (int slot = 0; slot < enemySlotCount_; ++slot) {
        EnemySlot& record = enemySlots_[static_cast<std::size_t>(slot)];
        record.type = chooseEnemyType();
        record.phase = EnemySlotPhase::Waiting;
    }
    for (int slot = 0; slot < enemySlotCount_; ++slot) {
        enemySlots_[static_cast<std::size_t>(slot)].timer = enemySpawnDelay();
    }
    enemyWarning_ = false;
}

void SuperGame::beginEnemyWarning(const int slot) {
    if (slot < 0 || slot >= enemySlotCount_) return;
    EnemySlot& record = enemySlots_[static_cast<std::size_t>(slot)];
    if (record.phase != EnemySlotPhase::Waiting) return;
    record.edge = random_.range(4);
    record.row = record.edge == 0 ? BoardRows - 1
        : record.edge == 2 ? 0 : random_.range(BoardRows);
    record.column = record.edge == 1 ? 0
        : record.edge == 3 ? BoardColumns - 1 : random_.range(BoardColumns);
    record.phase = EnemySlotPhase::Warning;
    record.timer = TroggleWarningDuration;
    Enemy enemy;
    enemy.row = record.row;
    enemy.column = record.column;
    enemy.fromRow = record.row + (record.edge == 0 ? 1 : record.edge == 2 ? -1 : 0);
    enemy.fromColumn = record.column + (record.edge == 1 ? -1 : record.edge == 3 ? 1 : 0);
    enemy.direction = record.edge == 0 ? 0 : record.edge == 1 ? 1
        : record.edge == 2 ? 2 : 3;
    enemy.type = record.type;
    enemy.slot = slot;
    enemy.entering = true;
    enemies_.push_back(enemy);
    enemyWarning_ = true;
    playGameplaySound(9);
}

void SuperGame::spawnEnemy(const int slot) {
    if (slot < 0 || slot >= enemySlotCount_) return;
    EnemySlot& record = enemySlots_[static_cast<std::size_t>(slot)];
    const auto found = std::find_if(enemies_.begin(), enemies_.end(),
        [slot](const Enemy& enemy) { return enemy.slot == slot && enemy.entering; });
    if (record.phase != EnemySlotPhase::Warning || found == enemies_.end()) return;
    found->moving = true;
    found->animationTimer = enemyMoveDuration(found->direction);
    record.phase = EnemySlotPhase::Active;
    record.timer = 0.0;
    playGameplaySound(3);
}

void SuperGame::rearmEnemy(const int slot) {
    if (slot < 0 || slot >= enemySlotCount_) return;
    EnemySlot& record = enemySlots_[static_cast<std::size_t>(slot)];
    record.type = chooseEnemyType();
    record.phase = EnemySlotPhase::Waiting;
    record.timer = enemySpawnDelay();
}

void SuperGame::beginEnemyMove(Enemy& enemy, const int row, const int column) {
    enemy.fromRow = enemy.row;
    enemy.fromColumn = enemy.column;
    if (row < enemy.row) enemy.direction = 0;
    else if (column > enemy.column) enemy.direction = 1;
    else if (row > enemy.row) enemy.direction = 2;
    else enemy.direction = 3;
    enemy.row = row;
    enemy.column = column;
    enemy.exiting = row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns;
    enemy.moving = true;
    enemy.animationTimer = enemyMoveDuration(enemy.direction);
    if (enemy.exiting && (enemy.direction == 0 || enemy.direction == 2)) {
        const int tier = std::clamp(core_.pressureTier(), 0, 11);
        enemy.animationTimer += static_cast<double>(
            EnemyMoveTicks[static_cast<std::size_t>(tier)]) / SchedulerTicksPerSecond;
    }
}

void SuperGame::chooseEnemyMove(Enemy& enemy) {
    constexpr std::array<int, 4> rowDelta = {-1, 0, 1, 0};
    constexpr std::array<int, 4> columnDelta = {0, 1, 0, -1};
    int playerPixelX = playerColumn_ * BoardCellWidth;
    int playerPixelY = playerRow_ * BoardCellHeight;
    if (moving_) {
        const int phase = movementPhase();
        if (moveDirection_ == 0 || moveDirection_ == 2) {
            playerPixelY = moveFromRow_ * BoardCellHeight +
                (moveToRow_ - moveFromRow_) * 6 * phase;
            playerPixelX = moveFromColumn_ * BoardCellWidth;
        } else {
            playerPixelX = moveFromColumn_ * BoardCellWidth +
                (moveToColumn_ - moveFromColumn_) * 8 * phase;
            playerPixelY = moveFromRow_ * BoardCellHeight;
        }
    }
    const int enemyPixelX = enemy.column * BoardCellWidth;
    const int enemyPixelY = enemy.row * BoardCellHeight;
    const int horizontal = std::abs(enemyPixelX - playerPixelX);
    const int vertical = std::abs(enemyPixelY - playerPixelY);
    const int distance = horizontal + vertical;
    const int toward = horizontal >= vertical
        ? (enemy.column > playerColumn_ ? 3 : 1)
        : (enemy.row > playerRow_ ? 0 : 2);
    int roll = 9;
    if (enemy.type == 1 || enemy.type == 3 ||
        (enemy.type == 2 && distance > 96)) roll = random_.range(10);
    else if (enemy.type == 4 && distance > 96 && distance <= 192) roll = random_.range(2);
    const auto turn = [](const int value, const int amount) {
        return (value + amount + 4) % 4;
    };
    if (enemy.type == 1 || enemy.type == 3) {
        if (roll == 0) enemy.direction = turn(enemy.direction, -1);
        else if (roll == 1) enemy.direction = turn(enemy.direction, 1);
    } else if (enemy.type == 2) {
        if (distance <= 96) enemy.direction = turn(toward, 2);
        else if (roll < 2) enemy.direction = turn(enemy.direction, 1);
        else if (roll < 4) enemy.direction = turn(enemy.direction, -1);
        else if (roll < 6) enemy.direction = turn(enemy.direction, 2);
    } else if (enemy.type == 4 &&
               (distance <= 96 || (distance <= 192 && roll == 0))) {
        enemy.direction = toward;
    }
    for (;;) {
        const int row = enemy.row + rowDelta[static_cast<std::size_t>(enemy.direction)];
        const int column = enemy.column +
                           columnDelta[static_cast<std::size_t>(enemy.direction)];
        if (row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns ||
            !safeAt(row, column)) {
            beginEnemyMove(enemy, row, column);
            return;
        }
        enemy.direction = (enemy.direction + (random_.range(2) == 0 ? 3 : 1)) % 4;
    }
}

bool SuperGame::applyEnemyTrail(const Enemy& enemy) {
    if (enemy.row < 0 || enemy.row >= BoardRows || enemy.column < 0 ||
        enemy.column >= BoardColumns || core_.boardComplete()) return false;
    const std::size_t cell = static_cast<std::size_t>(
        enemy.row * BoardColumns + enemy.column);
    const SuperBoardCell savedCell = enemy.savedCellValid
        ? enemy.savedCell : core_.board().cells[cell];
    const bool savedEaten = enemy.savedCellValid
        ? enemy.savedCellEaten : core_.eaten(cell);
    if (enemy.type == 4) {
        (void)core_.restoreCell(cell, savedCell, savedEaten);
    } else if (enemy.type == 3 ||
        ((enemy.type == 0 || enemy.type == 2) && savedEaten)) {
        (void)core_.clearCellWithoutScore(cell);
    } else {
        (void)core_.regenerateCell(cell);
    }
    if (core_.completeBoardIfEmpty()) {
        handleCompletedBoard();
        return false;
    }
    return true;
}

int SuperGame::enemyAtPlayer() const {
    const auto found = std::find_if(enemies_.begin(), enemies_.end(),
        [this](const Enemy& enemy) {
            return !enemy.entering && !enemy.exiting &&
                   enemy.row == playerRow_ && enemy.column == playerColumn_;
        });
    return found == enemies_.end() ? -1 : found->slot;
}

void SuperGame::collideWithEnemy(const int slot) {
    const auto found = std::find_if(enemies_.begin(), enemies_.end(),
        [slot](const Enemy& enemy) { return enemy.slot == slot; });
    if (found == enemies_.end() || playerRecovering_) return;
    moving_ = false;
    munching_ = false;
    inputQueue_.clear();
    if (core_.superForm()) {
        (void)core_.defeatTroggle();
        enemies_.erase(found);
        rearmEnemy(slot);
        playGameplaySound(11);
        return;
    }
    if (collisionActive_) return;
    found->moving = false;
    found->entering = false;
    found->exiting = false;
    found->biting = true;
    found->timer = 0.0;
    collisionSlot_ = slot;
    collisionTimer_ = 21.0 / SchedulerTicksPerSecond;
    collisionActive_ = true;
    playGameplaySound(12);
}

void SuperGame::finishEnemyCollision() {
    if (!collisionActive_) return;
    collisionActive_ = false;
    collisionTimer_ = 0.0;
    const auto found = std::find_if(enemies_.begin(), enemies_.end(),
        [this](const Enemy& enemy) { return enemy.slot == collisionSlot_; });
    if (found != enemies_.end()) {
        found->biting = false;
        found->timer = enemyDwellDelay();
    }
    feedbackEnemySlot_ = collisionSlot_;
    collisionSlot_ = -1;
    lastResolution_ = {};
    lastResolution_.gameOver = core_.loseMuncher();
    feedbackMessage_ = lastResolution_.gameOver ? "Game over." : "Yikes";
    feedbackTimer_ = FeedbackDuration;
    page_ = SuperGamePage::Feedback;
}

void SuperGame::updateEnemies(const double seconds) {
    for (Enemy& mover : enemies_) {
        if (!mover.moving || mover.animationTimer - seconds > 1e-9 ||
            mover.exiting || mover.row < 0 || mover.row >= BoardRows ||
            mover.column < 0 || mover.column >= BoardColumns) continue;
        for (Enemy& resident : enemies_) {
            if (&resident == &mover || resident.entering || resident.moving ||
                resident.overlapFrozen || resident.cannibalizing ||
                resident.row != mover.row || resident.column != mover.column) continue;
            resident.overlapFrozen = true;
            resident.overlapRetired = true;
        }
    }
    for (int slot = 0; slot < enemySlotCount_; ++slot) {
        EnemySlot& record = enemySlots_[static_cast<std::size_t>(slot)];
        if (record.phase == EnemySlotPhase::Waiting) {
            record.timer -= seconds;
            if (record.timer <= 0.0) beginEnemyWarning(slot);
            continue;
        }
        if (record.phase == EnemySlotPhase::Warning) {
            record.timer -= seconds;
            if (record.timer <= 0.0) spawnEnemy(slot);
            continue;
        }
        if (record.phase != EnemySlotPhase::Active) continue;
        const auto found = std::find_if(enemies_.begin(), enemies_.end(),
            [slot](const Enemy& enemy) { return enemy.slot == slot; });
        if (found == enemies_.end()) {
            rearmEnemy(slot);
            continue;
        }
        Enemy& enemy = *found;
        // A player-bite state replaces this physical Troggle slot's ordinary
        // controller. Earlier slots have already run; a terminal bite aborts
        // the later slots just as the original scheduler traversal does.
        if (enemy.biting) {
            if (collisionActive_ && collisionSlot_ == slot) {
                collisionTimer_ -= seconds;
                if (collisionTimer_ <= 1e-9) {
                    finishEnemyCollision();
                    if (page_ != SuperGamePage::Playing) return;
                }
            }
            continue;
        }
        if (enemy.overlapFrozen) continue;
        if (enemy.cannibalizing) {
            enemy.cannibalTimer -= seconds;
            if (enemy.cannibalTimer > 1e-9) continue;
            enemy.cannibalizing = false;
            enemy.cannibalTimer = 0.0;
            enemy.timer = enemyDwellDelay();
            const int survivorRow = enemy.row;
            const int survivorColumn = enemy.column;
            std::vector<int> victimSlots;
            for (const Enemy& candidate : enemies_) {
                if (candidate.slot != slot && candidate.overlapRetired &&
                    candidate.row == survivorRow &&
                    candidate.column == survivorColumn) {
                    victimSlots.push_back(candidate.slot);
                }
            }
            enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                [&](const Enemy& candidate) {
                    return candidate.slot != slot && candidate.overlapRetired &&
                           candidate.row == survivorRow &&
                           candidate.column == survivorColumn;
                }), enemies_.end());
            for (const int victimSlot : victimSlots) {
                rearmEnemy(victimSlot);
                playGameplaySound(11);
            }
            continue;
        }
        if (enemy.moving) {
            enemy.animationTimer -= seconds;
            if (enemy.animationTimer > 0.0) continue;
            enemy.moving = false;
            enemy.animationTimer = 0.0;
            if (enemy.exiting) {
                enemies_.erase(found);
                rearmEnemy(slot);
                if (playerRecovering_ && enemyAtPlayer() < 0) {
                    playerRecovering_ = false;
                }
                continue;
            }
            enemy.entering = false;
            const std::size_t cell = static_cast<std::size_t>(
                enemy.row * BoardColumns + enemy.column);
            enemy.savedCell = core_.board().cells[cell];
            enemy.savedCellEaten = core_.eaten(cell);
            enemy.savedCellValid = true;
            enemy.timer = enemyDwellDelay();
            const bool overlap = std::any_of(enemies_.begin(), enemies_.end(),
                [&](const Enemy& candidate) {
                    return &candidate != &enemy && candidate.overlapRetired &&
                           candidate.row == enemy.row &&
                           candidate.column == enemy.column;
                });
            if (overlap) {
                enemy.cannibalizing = true;
                enemy.cannibalTimer = TroggleCannibalAnimationDuration;
            }
            if (!moving_ && !munching_ && !transforming_ &&
                enemy.row == playerRow_ && enemy.column == playerColumn_) {
                collideWithEnemy(slot);
            }
            continue;
        }
        enemy.timer -= seconds;
        if (enemy.timer <= 0.0) {
            if (!applyEnemyTrail(enemy)) return;
            chooseEnemyMove(enemy);
            if (playerRecovering_ && enemyAtPlayer() < 0) {
                playerRecovering_ = false;
            }
        }
    }
    enemyWarning_ = std::any_of(enemySlots_.begin(),
        enemySlots_.begin() + enemySlotCount_, [](const EnemySlot& slot) {
            return slot.phase == EnemySlotPhase::Warning;
        });
}

void SuperGame::keyDown(const UINT virtualKey) {
    if (demoMode_) {
        (void)virtualKey;
        returnFromDemo();
        return;
    }
    if (cheatOpen_) {
        if (virtualKey == VK_ESCAPE) {
            cheatOpen_ = false;
            return;
        }
        if (virtualKey == VK_LEFT) menuSelection_ = std::max(1, menuSelection_ - 1);
        if (virtualKey == VK_RIGHT) menuSelection_ = std::min(20, menuSelection_ + 1);
        if (virtualKey == VK_UP) menuSelection_ = std::min(20, menuSelection_ + 1);
        if (virtualKey == VK_DOWN) menuSelection_ = std::max(1, menuSelection_ - 1);
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            startGame(selectedGame_, menuSelection_);
            cheatOpen_ = false;
        }
        return;
    }
    if (page_ == SuperGamePage::StartupVersion) {
        page_ = SuperGamePage::StartupSplash;
        return;
    }
    if (page_ == SuperGamePage::StartupSplash) {
        page_ = SuperGamePage::Title;
        menuSelection_ = 0;
        return;
    }
    if (page_ == SuperGamePage::Title) {
        titleIdleTimer_ = TitleIdleDuration;
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 4) % 5;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 5;
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) activateTitleSelection();
        if (virtualKey == VK_ESCAPE) shouldQuit_ = true;
        return;
    }
    if (page_ == SuperGamePage::InstructionsQuestion) {
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT ||
            virtualKey == VK_UP || virtualKey == VK_DOWN) {
            menuSelection_ = 1 - menuSelection_;
        }
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Title;
            menuSelection_ = 0;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            if (menuSelection_ == 0) {
                informationFromPlay_ = true;
                informationPage_ = 0;
                page_ = SuperGamePage::Information;
            } else {
                beginPlaySelection();
            }
        }
        return;
    }
    if (page_ == SuperGamePage::GameSelect) {
        const int count = std::max(1, static_cast<int>(gameChoices_.size()));
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + count - 1) % count;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % count;
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) activateGameSelection();
        if (virtualKey == VK_ESCAPE) {
            page_ = difficultyMode_ == 3
                ? SuperGamePage::GameDifficultySelect
                : SuperGamePage::Title;
            menuSelection_ = 0;
        }
        return;
    }
    if (page_ == SuperGamePage::GameDifficultySelect) {
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 2) % 3;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 3;
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::InstructionsQuestion;
            menuSelection_ = 1;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            static constexpr std::array<int, 3> Difficulties = {10, 20, 30};
            boardSettings_.difficulty = Difficulties[static_cast<std::size_t>(menuSelection_)];
            difficultyChosenForPlay_ = true;
            prepareGameChoices();
            if (gameChoices_.size() == 1) startGame(gameChoices_.front(), 1);
            else {
                page_ = SuperGamePage::GameSelect;
                menuSelection_ = 0;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::HallSelect) {
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 6) % 7;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 7;
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Title;
            menuSelection_ = 1;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            selectedGame_ = std::clamp(menuSelection_, 0, 6);
            page_ = SuperGamePage::Hall;
        }
        return;
    }
    if (page_ == SuperGamePage::Hall) {
        if (hallPostGame_) {
            page_ = SuperGamePage::ReplayQuestion;
            menuSelection_ = 0;
        } else {
            page_ = SuperGamePage::Title;
            menuSelection_ = 1;
        }
        return;
    }
    if (page_ == SuperGamePage::Information) {
        if (virtualKey == VK_ESCAPE) {
            informationFromPlay_ = false;
            page_ = SuperGamePage::Title;
            menuSelection_ = 2;
        } else if (informationPage_ >= 12) {
            if (informationFromPlay_) {
                informationFromPlay_ = false;
                beginPlaySelection();
            } else {
                page_ = SuperGamePage::Title;
                menuSelection_ = 2;
            }
        } else {
            ++informationPage_;
        }
        return;
    }
    if (page_ == SuperGamePage::Options) {
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Title;
            menuSelection_ = 3;
            return;
        }
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 5) % 6;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 6;
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            if (menuSelection_ == 0) {
                page_ = SuperGamePage::OptionsContent;
                menuSelection_ = 0;
            } else if (menuSelection_ == 1) {
                answerVisionOption_ = !answerVisionOption_;
                saveSettings();
            } else if (menuSelection_ == 2) {
                page_ = SuperGamePage::OptionsEraseHallSelect;
                menuSelection_ = 0;
            } else if (menuSelection_ == 3) {
                passwordEntry_ = password_;
                passwordDraft_ = password_;
                passwordEditingHint_ = false;
                passwordError_ = false;
                page_ = SuperGamePage::OptionsSetPassword;
            } else if (menuSelection_ == 4) {
                joystickEnabled_ = !joystickEnabled_;
                saveSettings();
            } else if (menuSelection_ == 5) {
                calibrationActive_ = true;
                calibrationMinX_ = calibrationMinY_ = 0xffffffffu;
                calibrationMaxX_ = calibrationMaxY_ = 0;
                page_ = SuperGamePage::OptionsCalibration;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsPasswordPrompt) {
        if (virtualKey == VK_ESCAPE) {
            passwordEntry_.clear();
            passwordError_ = false;
            page_ = SuperGamePage::Title;
            menuSelection_ = 3;
        } else if (virtualKey == VK_BACK && !passwordEntry_.empty()) {
            passwordEntry_.pop_back();
        } else if (virtualKey == VK_RETURN) {
            if (passwordEntry_ == password_) {
                passwordEntry_.clear();
                passwordError_ = false;
                page_ = SuperGamePage::Options;
                menuSelection_ = 0;
            } else {
                passwordEntry_.clear();
                passwordError_ = true;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsSetPassword) {
        if (virtualKey == VK_ESCAPE) {
            passwordEntry_.clear();
            passwordDraft_.clear();
            passwordEditingHint_ = false;
            page_ = SuperGamePage::Options;
            menuSelection_ = 3;
        } else if (virtualKey == VK_BACK && !passwordEntry_.empty()) {
            passwordEntry_.pop_back();
        } else if (virtualKey == VK_RETURN) {
            if (!passwordEditingHint_) {
                passwordDraft_ = passwordEntry_;
                passwordEntry_ = passwordHint_;
                passwordEditingHint_ = true;
            } else {
                password_ = passwordDraft_;
                passwordHint_ = passwordEntry_;
                passwordEntry_.clear();
                passwordDraft_.clear();
                passwordEditingHint_ = false;
                saveSettings();
                page_ = SuperGamePage::Options;
                menuSelection_ = 3;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsEraseHallSelect) {
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 7) % 8;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 8;
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Options;
            menuSelection_ = 2;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            eraseHallIndex_ = menuSelection_ == 0 ? -1 : menuSelection_ - 1;
            if (eraseHallIndex_ < 0) {
                page_ = SuperGamePage::OptionsEraseConfirm;
                menuSelection_ = 1;
            } else {
                eraseDraft_ = halls_[static_cast<std::size_t>(eraseHallIndex_)];
                page_ = SuperGamePage::OptionsEraseEntries;
                menuSelection_ = 0;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsEraseEntries) {
        const int count = static_cast<int>(eraseDraft_.size());
        if (virtualKey == VK_ESCAPE) {
            eraseDraft_.clear();
            page_ = SuperGamePage::OptionsEraseHallSelect;
            menuSelection_ = eraseHallIndex_ + 1;
        } else if (virtualKey == VK_UP && count > 0) {
            menuSelection_ = (menuSelection_ + count - 1) % count;
        } else if (virtualKey == VK_DOWN && count > 0) {
            menuSelection_ = (menuSelection_ + 1) % count;
        } else if (virtualKey == VK_SPACE && count > 0) {
            eraseDraft_.erase(eraseDraft_.begin() + menuSelection_);
            if (eraseDraft_.empty()) menuSelection_ = 0;
            else menuSelection_ = std::min(menuSelection_,
                                           static_cast<int>(eraseDraft_.size()) - 1);
        } else if (virtualKey == VK_RETURN) {
            halls_[static_cast<std::size_t>(eraseHallIndex_)] = eraseDraft_;
            eraseDraft_.clear();
            saveSettings();
            page_ = SuperGamePage::Options;
            menuSelection_ = 2;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsEraseConfirm) {
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT ||
            virtualKey == VK_UP || virtualKey == VK_DOWN) {
            menuSelection_ = 1 - menuSelection_;
        }
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsEraseHallSelect;
            menuSelection_ = eraseHallIndex_ < 0 ? 0 : eraseHallIndex_ + 1;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            if (menuSelection_ == 0) {
                if (eraseHallIndex_ < 0) {
                    for (auto& hall : halls_) hall.clear();
                } else {
                    halls_[static_cast<std::size_t>(eraseHallIndex_)].clear();
                }
                saveSettings();
            }
            page_ = SuperGamePage::Options;
            menuSelection_ = 2;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsCalibration) {
        if (virtualKey == VK_ESCAPE) {
            calibrationActive_ = false;
            page_ = SuperGamePage::Options;
            menuSelection_ = 5;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            const bool validRange = joystickConnected_ &&
                calibrationMinX_ < calibrationMaxX_ &&
                calibrationMinY_ < calibrationMaxY_ &&
                calibrationMaxX_ - calibrationMinX_ >= 1024u &&
                calibrationMaxY_ - calibrationMinY_ >= 1024u;
            if (validRange) {
                const std::uint32_t xRange = calibrationMaxX_ - calibrationMinX_;
                const std::uint32_t yRange = calibrationMaxY_ - calibrationMinY_;
                joystickLeftThreshold_ = calibrationMinX_ + xRange / 3u;
                joystickRightThreshold_ = calibrationMaxX_ - xRange / 3u;
                joystickUpThreshold_ = calibrationMinY_ + yRange / 3u;
                joystickDownThreshold_ = calibrationMaxY_ - yRange / 3u;
                joystickCalibrated_ = true;
                calibrationActive_ = false;
                saveSettings();
                page_ = SuperGamePage::Options;
                menuSelection_ = 5;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsContent) {
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Options;
            menuSelection_ = 0;
            return;
        }
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 4) % 5;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 5;
        if (virtualKey != VK_RETURN && virtualKey != VK_SPACE) return;
        if (menuSelection_ == 0) {
            page_ = SuperGamePage::OptionsQuickSet;
            menuSelection_ = 0;
        } else if (menuSelection_ == 1) {
            page_ = SuperGamePage::OptionsGames;
            menuSelection_ = 0;
        } else if (menuSelection_ == 2) {
            page_ = SuperGamePage::OptionsDifficulty;
            difficultyErrorTopic_ = -1;
            menuSelection_ = boardSettings_.difficulty == 10 ? 0
                : boardSettings_.difficulty == 20 ? 1 : 2;
        } else if (menuSelection_ == 3) {
            boardSettings_.negations = !boardSettings_.negations;
            saveSettings();
        } else {
            previewTopic_ = 0;
            previewOffset_ = 0;
            page_ = SuperGamePage::OptionsPreview;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsQuickSet) {
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsContent;
            menuSelection_ = 0;
            return;
        }
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 5) % 6;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 6;
        if (virtualKey != VK_RETURN && virtualKey != VK_SPACE) return;
        if (menuSelection_ > 0) {
            quickSet_ = menuSelection_ - 1;
            const std::size_t preset = static_cast<std::size_t>(quickSet_);
            for (std::size_t topic = 0; topic < SuperTopicCount; ++topic) {
                const SuperCategorySet* set = content_.set(topic);
                for (std::size_t rule = 0; rule < SuperMaximumRulesPerTopic; ++rule) {
                    boardSettings_.selectedRules[topic][rule] =
                        set && rule < set->rules.size() &&
                        set->rules[rule].quickSetThreshold() <=
                            QuickSetRuleThresholds[preset];
                }
            }
            difficultyMode_ = QuickSetDifficultyModes[preset];
            boardSettings_.difficulty = difficultyMode_ == 0 ? 10
                : difficultyMode_ == 2 ? 30 : 20;
            boardSettings_.negations = QuickSetNegations[preset];
            saveSettings();
        }
        page_ = SuperGamePage::OptionsContent;
        menuSelection_ = 0;
        return;
    }
    if (page_ == SuperGamePage::OptionsGames) {
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsContent;
            menuSelection_ = 1;
            saveSettings();
            return;
        }
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 5) % 6;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 6;
        if (virtualKey == VK_F2) {
            boardSettings_.selectedTopics.fill(true);
            return;
        }
        if (virtualKey == VK_F3) {
            boardSettings_.selectedTopics.fill(false);
            boardSettings_.selectedTopics[static_cast<std::size_t>(menuSelection_)] = true;
            return;
        }
        if (virtualKey == VK_SPACE) {
            const std::size_t topic = static_cast<std::size_t>(menuSelection_);
            const int selectedCount = static_cast<int>(std::count(
                boardSettings_.selectedTopics.begin(),
                boardSettings_.selectedTopics.end(), true));
            if (boardSettings_.selectedTopics[topic] && selectedCount > 1) {
                boardSettings_.selectedTopics[topic] = false;
            } else if (!boardSettings_.selectedTopics[topic]) {
                boardSettings_.selectedTopics[topic] = true;
            }
        } else if (virtualKey == VK_RETURN) {
            ruleTopic_ = menuSelection_;
            ruleOffset_ = 0;
            menuSelection_ = 0;
            page_ = SuperGamePage::OptionsRules;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsRules) {
        const SuperCategorySet* set = content_.set(static_cast<std::size_t>(ruleTopic_));
        const int count = set ? static_cast<int>(set->rules.size()) : 0;
        if (count <= 0) {
            page_ = SuperGamePage::OptionsGames;
            menuSelection_ = ruleTopic_;
            return;
        }
        const auto available = [this, set](const int rule) {
            const int difficulty = difficultyMode_ == 3 ? 30 : boardSettings_.difficulty;
            return rule >= 0 && rule < static_cast<int>(set->rules.size()) &&
                set->rules[static_cast<std::size_t>(rule)].minimumDifficulty() <= difficulty;
        };
        const auto keepVisible = [this] {
            if (menuSelection_ < ruleOffset_) ruleOffset_ = menuSelection_;
            if (menuSelection_ >= ruleOffset_ + 10) ruleOffset_ = menuSelection_ - 9;
        };
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsGames;
            menuSelection_ = ruleTopic_;
            saveSettings();
            return;
        }
        if (virtualKey == VK_UP) {
            menuSelection_ = (menuSelection_ + count - 1) % count;
            keepVisible();
        }
        if (virtualKey == VK_DOWN) {
            menuSelection_ = (menuSelection_ + 1) % count;
            keepVisible();
        }
        if (virtualKey == VK_PRIOR) {
            menuSelection_ = std::max(0, menuSelection_ - 10);
            keepVisible();
        }
        if (virtualKey == VK_NEXT) {
            menuSelection_ = std::min(count - 1, menuSelection_ + 10);
            keepVisible();
        }
        if (virtualKey == VK_F2) {
            for (int rule = 0; rule < count; ++rule) {
                if (available(rule)) {
                    boardSettings_.selectedRules[static_cast<std::size_t>(ruleTopic_)]
                                                [static_cast<std::size_t>(rule)] = true;
                }
            }
        }
        if (virtualKey == VK_F3) {
            auto& rules = boardSettings_.selectedRules[static_cast<std::size_t>(ruleTopic_)];
            rules.fill(false);
            int retained = available(menuSelection_) ? menuSelection_ : -1;
            if (retained < 0) {
                for (int rule = 0; rule < count && retained < 0; ++rule) {
                    if (available(rule)) retained = rule;
                }
            }
            if (retained >= 0) rules[static_cast<std::size_t>(retained)] = true;
        }
        if (virtualKey == VK_SPACE && available(menuSelection_)) {
            auto& rules = boardSettings_.selectedRules[static_cast<std::size_t>(ruleTopic_)];
            const int selected = static_cast<int>(std::count_if(
                rules.begin(), rules.begin() + count,
                [index = 0, &available](const bool chosen) mutable {
                    const bool result = chosen && available(index);
                    ++index;
                    return result;
                }));
            bool& choice = rules[static_cast<std::size_t>(menuSelection_)];
            if (!choice || selected > 1) choice = !choice;
        }
        if (virtualKey == VK_RETURN) {
            previewOffset_ = 0;
            page_ = SuperGamePage::OptionsRuleWords;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsRuleWords) {
        const SuperCategorySet* set = content_.set(static_cast<std::size_t>(ruleTopic_));
        const int rule = menuSelection_;
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsRules;
            return;
        }
        if (virtualKey == VK_TAB && set && !set->rules.empty()) {
            menuSelection_ = (rule + 1) % static_cast<int>(set->rules.size());
            ruleOffset_ = std::clamp(menuSelection_ - 4, 0,
                std::max(0, static_cast<int>(set->rules.size()) - 10));
            previewOffset_ = 0;
            return;
        }
        if (virtualKey == VK_SPACE || virtualKey == VK_RETURN ||
            virtualKey == VK_RIGHT || virtualKey == VK_DOWN || virtualKey == VK_NEXT) {
            previewOffset_ += 60;
        } else if (virtualKey == VK_LEFT || virtualKey == VK_UP || virtualKey == VK_PRIOR ||
                   virtualKey == 'B') {
            previewOffset_ = std::max(0, previewOffset_ - 60);
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsDifficulty) {
        if (difficultyErrorTopic_ >= 0) {
            difficultyErrorTopic_ = -1;
            return;
        }
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsContent;
            menuSelection_ = 2;
            return;
        }
        if (virtualKey == VK_UP) menuSelection_ = (menuSelection_ + 3) % 4;
        if (virtualKey == VK_DOWN) menuSelection_ = (menuSelection_ + 1) % 4;
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            // 0x16450 checks every enabled topic before 0x18C45 accepts a new
            // difficulty. Its 1-based selector table is 10,20,30,40; the
            // fourth value lets Player Chooses retain any source rule.
            static constexpr std::array<int, 4> Availability = {10, 20, 30, 40};
            const int ceiling = Availability[static_cast<std::size_t>(menuSelection_)];
            for (std::size_t topic = 0; topic < SuperTopicCount; ++topic) {
                if (!boardSettings_.selectedTopics[topic]) continue;
                const SuperCategorySet* set = content_.set(topic);
                bool hasRule = false;
                if (set) {
                    for (std::size_t rule = 0; rule < set->rules.size(); ++rule) {
                        if (boardSettings_.selectedRules[topic][rule] &&
                            set->rules[rule].minimumDifficulty() <= ceiling) {
                            hasRule = true;
                            break;
                        }
                    }
                }
                if (!hasRule) {
                    difficultyErrorTopic_ = static_cast<int>(topic);
                    return;
                }
            }
            // Player Chooses resolves at game start; Advanced is its initial
            // choice, matching the original default prompt selection.
            boardSettings_.difficulty = menuSelection_ == 0 ? 10
                : menuSelection_ == 2 ? 30 : 20;
            difficultyMode_ = menuSelection_;
            saveSettings();
            page_ = SuperGamePage::OptionsContent;
            menuSelection_ = 2;
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsPreview) {
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::OptionsContent;
            menuSelection_ = 4;
        } else if (virtualKey == VK_SPACE || virtualKey == VK_RETURN ||
                   virtualKey == VK_RIGHT || virtualKey == VK_DOWN) {
            previewOffset_ += 60;
            const SuperCategorySet* set = content_.set(
                static_cast<std::size_t>(previewTopic_));
            if (!set || previewOffset_ >= static_cast<int>(set->words.size())) {
                previewOffset_ = 0;
                previewTopic_ = (previewTopic_ + 1) % 6;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::Paused) {
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE || virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Playing;
        }
        return;
    }
    if (page_ == SuperGamePage::QuitConfirm) {
        if (virtualKey == 'Y' || virtualKey == VK_RETURN) {
            beginPostGame();
        } else if (virtualKey == 'N' || virtualKey == VK_ESCAPE) {
            page_ = quitReturnPage_;
        }
        return;
    }
    if (page_ == SuperGamePage::NameEntry) {
        if (virtualKey == VK_BACK && !nameEntry_.empty()) nameEntry_.pop_back();
        if (virtualKey == VK_RETURN || virtualKey == VK_ESCAPE) submitHallName();
        return;
    }
    if (page_ == SuperGamePage::ReplayQuestion) {
        if (virtualKey == VK_LEFT || virtualKey == VK_RIGHT ||
            virtualKey == VK_UP || virtualKey == VK_DOWN) {
            menuSelection_ = 1 - menuSelection_;
        }
        if (virtualKey == VK_ESCAPE) {
            page_ = SuperGamePage::Title;
            menuSelection_ = 0;
            hallPostGame_ = false;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            if (menuSelection_ == 0) requestStartGame(selectedGame_);
            else {
                page_ = SuperGamePage::Title;
                menuSelection_ = 0;
                hallPostGame_ = false;
            }
        }
        return;
    }
    if (page_ == SuperGamePage::MissionIntro) {
        if (virtualKey == VK_ESCAPE) {
            completeMission(false);
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            page_ = SuperGamePage::LevelComplete;
        }
        return;
    }
    if (page_ == SuperGamePage::MissionQuestion) {
        const int count = std::max(1, static_cast<int>(missionChoices_.size()));
        if (virtualKey == VK_LEFT || virtualKey == VK_UP) {
            menuSelection_ = (menuSelection_ + count - 1) % count;
        }
        if (virtualKey == VK_RIGHT || virtualKey == VK_DOWN) {
            menuSelection_ = (menuSelection_ + 1) % count;
        }
        if (virtualKey >= static_cast<UINT>('1') &&
            virtualKey < static_cast<UINT>('1' + count)) {
            menuSelection_ = static_cast<int>(virtualKey - '1');
        }
        if (virtualKey == VK_ESCAPE) completeMission(false);
        else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            completeMission(menuSelection_ == missionCorrectSelection_);
        }
        return;
    }
    if (page_ == SuperGamePage::MissionResult) {
        missionIndex_ = -1;
        missionScene_ = SceneScript{};
        advanceBoard();
        return;
    }
    if (page_ == SuperGamePage::Feedback || page_ == SuperGamePage::LevelComplete) {
        if (page_ == SuperGamePage::Feedback) finishFeedback();
        else if (missionScene_.valid() && missionIndex_ == 3) {
            if (virtualKey == VK_LEFT) missionPlayerX_ = std::max(0, missionPlayerX_ - 8);
            else if (virtualKey == VK_RIGHT) missionPlayerX_ = std::min(275, missionPlayerX_ + 8);
            else if (virtualKey == VK_ESCAPE) {
                missionCatchFailed_ = true;
                finishMission();
            }
        } else if (missionScene_.valid()) finishMission();
        else advanceBoard();
        return;
    }
    if (page_ != SuperGamePage::Playing) return;
    if (core_.actionFrozen()) {
        (void)core_.toggleAnswerVision();
        return;
    }
    if (virtualKey == VK_ESCAPE) {
        quitReturnPage_ = SuperGamePage::Playing;
        page_ = SuperGamePage::QuitConfirm;
        return;
    }
    if (virtualKey == VK_RETURN) {
        page_ = SuperGamePage::Paused;
        return;
    }
    if (virtualKey == 'V') {
        (void)core_.toggleAnswerVision();
        return;
    }
    int input = 0;
    if (virtualKey == VK_UP || virtualKey == 'I' || virtualKey == 'A') input = 'I';
    if (virtualKey == VK_RIGHT || virtualKey == 'K') input = 'K';
    if (virtualKey == VK_DOWN || virtualKey == 'M' || virtualKey == 'Z') input = 'M';
    if (virtualKey == VK_LEFT || virtualKey == 'J') input = 'J';
    if (virtualKey == VK_SPACE) input = ' ';
    if (input != 0 && inputQueue_.size() < 9) {
        inputQueue_.push_back(input);
        processInputQueue();
    }
}

void SuperGame::character(const wchar_t value) {
    if (demoMode_) {
        (void)value;
        returnFromDemo();
        return;
    }
    if (page_ == SuperGamePage::NameEntry) {
        if (value >= 32 && value <= 126 && nameEntry_.size() < 25) {
            nameEntry_.push_back(static_cast<char>(value));
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsPasswordPrompt ||
        page_ == SuperGamePage::OptionsSetPassword) {
        const std::size_t limit = page_ == SuperGamePage::OptionsSetPassword &&
                                  passwordEditingHint_ ? 58u : 10u;
        if (value >= 32 && value <= 126 && passwordEntry_.size() < limit) {
            passwordEntry_.push_back(static_cast<char>(value));
            passwordError_ = false;
        }
        return;
    }
    if (value == L'v' || value == L'V') keyDown('V');
    if (value == L'y' || value == L'Y') keyDown('Y');
    if (value == L'n' || value == L'N') keyDown('N');
    if (value == L' ') keyDown(VK_SPACE);
}

int SuperGame::cellAtPoint(const int x, const int y) const {
    if (x < BoardLeft || x >= BoardRight || y < BoardTop || y >= BoardBottom) return -1;
    return (y - BoardTop) / BoardCellHeight * BoardColumns +
           (x - BoardLeft) / BoardCellWidth;
}

void SuperGame::pointerMove(const int x, const int y) {
    if (page_ == SuperGamePage::Title) {
        titleIdleTimer_ = TitleIdleDuration;
        for (int index = 0; index < static_cast<int>(TitleRows.size()); ++index) {
            if (y >= TitleRows[static_cast<std::size_t>(index)] - 2 &&
                y <= TitleRows[static_cast<std::size_t>(index)] + 9 &&
                x >= 42 && x <= 278) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::InstructionsQuestion &&
               y >= 102 && y <= 116) {
        if (x >= 105 && x < 158) menuSelection_ = 0;
        if (x >= 158 && x <= 215) menuSelection_ = 1;
    } else if (page_ == SuperGamePage::GameSelect && x >= 80 && x <= 296) {
        for (int index = 0; index < static_cast<int>(gameChoices_.size()); ++index) {
            const int row = 68 + index * 11;
            if (y >= row - 1 && y <= row + 9) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::GameDifficultySelect && x >= 80 && x <= 284) {
        for (int index = 0; index < 3; ++index) {
            const int row = 86 + index * 11;
            if (y >= row - 1 && y <= row + 9) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::HallSelect && x >= 36 && x <= 284) {
        for (int index = 0; index < 7; ++index) {
            const int row = 61 + index * 14;
            if (y >= row - 2 && y <= row + 9) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::Options && x >= 24 && x <= 296) {
        for (int index = 0; index < 6; ++index) {
            const int row = 72 + index * 11;
            if (y >= row - 1 && y <= row + 9) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::OptionsContent && x >= 24 && x <= 296) {
        for (int index = 0; index < 5; ++index) {
            const int row = 75 + index * 18;
            if (y >= row - 2 && y <= row + 10) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::OptionsQuickSet && x >= 24 && x <= 296) {
        for (int index = 0; index < 6; ++index) {
            const int row = 55 + index * 18;
            if (y >= row - 2 && y <= row + 10) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::OptionsGames && x >= 36 && x <= 296) {
        for (int index = 0; index < 6; ++index) {
            const int row = 52 + index * 18;
            if (y >= row - 2 && y <= row + 10) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::OptionsRules && x >= 0 && x < 320) {
        const SuperCategorySet* set = content_.set(static_cast<std::size_t>(ruleTopic_));
        for (int visible = 0; visible < 10; ++visible) {
            const int rule = ruleOffset_ + visible;
            if (!set || rule >= static_cast<int>(set->rules.size())) break;
            const int row = 48 + visible * 12;
            if (y >= row - 2 && y <= row + 9) menuSelection_ = rule;
        }
    } else if (page_ == SuperGamePage::OptionsDifficulty && x >= 36 && x <= 284) {
        for (int index = 0; index < 4; ++index) {
            const int row = 67 + index * 23;
            if (y >= row - 2 && y <= row + 10) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::OptionsEraseEntries &&
               x >= 18 && x <= 302) {
        for (int index = 0; index < static_cast<int>(eraseDraft_.size()); ++index) {
            const int row = 48 + index * 13;
            if (y >= row - 2 && y <= row + 9) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::ReplayQuestion && y >= 98 && y <= 120) {
        if (x >= 95 && x < 158) menuSelection_ = 0;
        if (x >= 158 && x <= 225) menuSelection_ = 1;
    } else if (page_ == SuperGamePage::MissionQuestion &&
               x >= 24 && x <= 296) {
        for (int index = 0; index < static_cast<int>(missionChoices_.size()); ++index) {
            const int row = 96 + index * 18;
            if (y >= row - 2 && y <= row + 10) menuSelection_ = index;
        }
    } else if (page_ == SuperGamePage::LevelComplete &&
               missionIndex_ == 3 && missionScene_.valid()) {
        missionPlayerX_ = std::clamp(x - 22, 0, 275);
    }
}

bool SuperGame::pointerPress(const bool secondary) {
    (void)secondary;
    return false;
}

void SuperGame::pointerButton(const int x, const int y, const bool secondary) {
    if (demoMode_) {
        returnFromDemo();
        return;
    }
    if (page_ != SuperGamePage::Playing) pointerMove(x, y);
    if (secondary) {
        if (page_ == SuperGamePage::OptionsGames ||
            page_ == SuperGamePage::OptionsRules) keyDown(VK_SPACE);
        else keyDown(VK_RETURN);
        return;
    }
    if (page_ == SuperGamePage::Playing) {
        if (core_.actionFrozen()) {
            (void)core_.toggleAnswerVision();
            return;
        }
        const int cell = cellAtPoint(x, y);
        if (cell >= 0 && inputQueue_.size() < 9) {
            inputQueue_.push_back(-(cell + 1));
            processInputQueue();
        }
        return;
    }
    if (page_ == SuperGamePage::OptionsEraseEntries && !secondary) {
        keyDown(VK_SPACE);
        return;
    }
    keyDown(VK_RETURN);
}

void SuperGame::setJoystickState(const bool connected, const std::uint32_t x,
                                 const std::uint32_t y, const std::uint32_t buttons) {
    const std::uint32_t pressed = buttons & ~joystickButtons_;
    joystickConnected_ = connected;
    joystickButtons_ = connected ? (buttons & 3u) : 0;
    if (connected) {
        joystickX_ = x;
        joystickY_ = y;
        if (page_ == SuperGamePage::OptionsCalibration && calibrationActive_) {
            calibrationMinX_ = std::min(calibrationMinX_, x);
            calibrationMaxX_ = std::max(calibrationMaxX_, x);
            calibrationMinY_ = std::min(calibrationMinY_, y);
            calibrationMaxY_ = std::max(calibrationMaxY_, y);
            if (pressed != 0) keyDown(VK_RETURN);
            return;
        }
    }
    if (!connected || !joystickEnabled_) return;
    if (page_ == SuperGamePage::LevelComplete && missionIndex_ == 3 &&
        missionScene_.valid()) {
        if (x < joystickLeftThreshold_) keyDown(VK_LEFT);
        else if (x > joystickRightThreshold_) keyDown(VK_RIGHT);
        return;
    }
    if (page_ != SuperGamePage::Playing) return;
    if ((pressed & 1u) != 0) keyDown(VK_SPACE);
    if ((pressed & 2u) != 0) keyDown(VK_RETURN);
    if (x < joystickLeftThreshold_) keyDown(VK_LEFT);
    else if (x > joystickRightThreshold_) keyDown(VK_RIGHT);
    else if (y < joystickUpThreshold_) keyDown(VK_UP);
    else if (y > joystickDownThreshold_) keyDown(VK_DOWN);
}

void SuperGame::toggleCheatMenu() {
    if (demoMode_) {
        returnFromDemo();
        return;
    }
    if (page_ == SuperGamePage::StartupVersion ||
        page_ == SuperGamePage::StartupSplash) page_ = SuperGamePage::Title;
    cheatOpen_ = !cheatOpen_;
    menuSelection_ = core_.active() ? core_.level() : 1;
}

void SuperGame::toggleSound() {
    if (!acceptsCommonDispatcherInput()) return;
    soundOn_ = !soundOn_;
    playToggleFeedback(soundOn_);
    saveSettings();
}

void SuperGame::toggleMusic() {
    if (!acceptsCommonDispatcherInput()) return;
    musicOn_ = !musicOn_;
    if (!musicOn_) {
        oplPlayer_.stopAllChannels();
        missionSoundPlayer_.stop();
        playToggleFeedback(false, 50);
    } else {
        playToggleFeedback(true);
    }
    saveSettings();
}

void SuperGame::toggleSpeaker() {
    if (!acceptsCommonDispatcherInput()) return;
    speakerEffects_ = !speakerEffects_;
    if (speakerEffects_ && musicOn_) {
        oplPlayer_.stopAllChannels();
        missionSoundPlayer_.stop();
    }
    saveSettings();
}

bool SuperGame::acceptsCommonDispatcherInput() const {
    return page_ == SuperGamePage::Title || page_ == SuperGamePage::Playing ||
           page_ == SuperGamePage::Feedback || page_ == SuperGamePage::LevelComplete;
}

void SuperGame::playGameplaySound(const std::uint8_t stream) {
    if (!soundOn_) return;
    const BlobView sound = assets_.gameArchive().find("GSND", 22);
    if (!sound) return;
    if (!speakerEffects_) {
        MeccSound decoded = decodeMeccGSound(
            sound, stream, MeccSoundProfile::SuperMunchers);
        if (decoded.valid &&
            (oplPlayer_.playing() || oplPlayer_.startIdle())) {
            (void)oplPlayer_.playEffect(
                std::move(decoded.writes), decoded.durationMilliseconds);
        }
        return;
    }
    std::vector<std::uint8_t> wave = renderMeccSpeakerSoundToWave(
        sound, stream, 40, MeccSoundProfile::SuperMunchers);
    if (!wave.empty()) (void)effectPlayer_.play(std::move(wave));
}

void SuperGame::playToggleFeedback(const bool enabled,
                                   const std::uint32_t startDelayMilliseconds) {
    const BlobView sound = assets_.gameArchive().find("GSND", 22);
    if (!sound) return;
    const std::uint8_t stream = enabled ? 2 : 1;
    if (speakerEffects_) {
        std::vector<std::uint8_t> wave = renderMeccSpeakerSoundToWave(
            sound, stream, 40, MeccSoundProfile::SuperMunchers,
            startDelayMilliseconds);
        if (!wave.empty()) (void)effectPlayer_.play(std::move(wave));
        return;
    }
    MeccSound decoded = decodeMeccGSound(
        sound, stream, MeccSoundProfile::SuperMunchers);
    if (!decoded.valid) return;
    for (OplWrite& write : decoded.writes) {
        write.milliseconds += startDelayMilliseconds;
    }
    decoded.durationMilliseconds += startDelayMilliseconds;
    if (!oplPlayer_.playing() && !oplPlayer_.startIdle()) return;
    (void)oplPlayer_.playEffect(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

std::wstring SuperGame::settingsPath() const {
    if (!settingsPathOverride_.empty()) return settingsPathOverride_;
    PWSTR localApplicationData = nullptr;
    std::wstring result = L"super-munchers-settings.txt";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &localApplicationData))) {
        std::filesystem::path directory(localApplicationData);
        CoTaskMemFree(localApplicationData);
        directory /= L"NumberMunchersNative";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error) result = (directory / L"super-settings.txt").wstring();
    }
    return result;
}

void SuperGame::loadSettings() {
    if (!settingsPersistenceEnabled_) return;
    std::ifstream input{std::filesystem::path(settingsPath())};
    std::string signature;
    int version = 0;
    int sound = 0;
    int music = 0;
    int speaker = 0;
    int answerVision = 0;
    if (!(input >> signature >> version >> sound >> music >> speaker >> answerVision) ||
        signature != "SUPER_MUNCHERS_NATIVE_SETTINGS" ||
        (version < 1 || version > 7)) return;
    const auto flag = [](const int value) { return value == 0 || value == 1; };
    if (!flag(sound) || !flag(music) || !flag(speaker) || !flag(answerVision)) return;
    SuperBoardSettings candidateBoard = boardSettings_;
    int candidateQuickSet = quickSet_;
    int candidateDifficultyMode = difficultyMode_;
    bool candidateJoystickEnabled = joystickEnabled_;
    bool candidateJoystickCalibrated = joystickCalibrated_;
    std::uint32_t candidateLeft = joystickLeftThreshold_;
    std::uint32_t candidateRight = joystickRightThreshold_;
    std::uint32_t candidateUp = joystickUpThreshold_;
    std::uint32_t candidateDown = joystickDownThreshold_;
    std::string candidatePassword = password_;
    std::string candidateHint = passwordHint_;
    if (version >= 3) {
        int negations = 0;
        if (!(input >> candidateBoard.difficulty >> negations >> candidateQuickSet) ||
            (candidateBoard.difficulty != 10 && candidateBoard.difficulty != 20 &&
             candidateBoard.difficulty != 30) || !flag(negations) ||
            candidateQuickSet < 0 || candidateQuickSet > 4) return;
        candidateDifficultyMode = candidateBoard.difficulty == 10 ? 0
            : candidateBoard.difficulty == 30 ? 2 : 1;
        if (version >= 4 &&
            (!(input >> candidateDifficultyMode) || candidateDifficultyMode < 0 ||
             candidateDifficultyMode > 3)) return;
        candidateBoard.negations = negations != 0;
        for (bool& topic : candidateBoard.selectedTopics) {
            int stored = 0;
            if (!(input >> stored) || !flag(stored)) return;
            topic = stored != 0;
        }
        if (std::none_of(candidateBoard.selectedTopics.begin(),
                         candidateBoard.selectedTopics.end(),
                         [](const bool value) { return value; })) return;
        for (auto& rules : candidateBoard.selectedRules) {
            for (bool& rule : rules) {
                int stored = 0;
                if (!(input >> stored) || !flag(stored)) return;
                rule = stored != 0;
            }
        }
        for (std::size_t topic = 0; topic < SuperTopicCount; ++topic) {
            if (!candidateBoard.selectedTopics[topic]) continue;
            const SuperCategorySet* set = content_.set(topic);
            bool eligible = false;
            for (std::size_t rule = 0; set && rule < set->rules.size(); ++rule) {
                eligible = eligible ||
                    (candidateBoard.selectedRules[topic][rule] &&
                     set->rules[rule].minimumDifficulty() <= candidateBoard.difficulty);
            }
            if (!eligible) return;
        }
        if (version >= 5) {
            int joystick = 0;
            if (!(input >> joystick >> std::quoted(candidatePassword)) ||
                !flag(joystick) || candidatePassword.size() > 10) return;
            candidateJoystickEnabled = joystick != 0;
            if (version >= 6) {
                int calibrated = 0;
                std::uint64_t left = 0;
                std::uint64_t right = 0;
                std::uint64_t up = 0;
                std::uint64_t down = 0;
                if (!(input >> calibrated >> left >> right >> up >> down) ||
                    !flag(calibrated) || left > 65535u || right > 65535u ||
                    up > 65535u || down > 65535u || left >= right || up >= down) {
                    return;
                }
                candidateJoystickCalibrated = calibrated != 0;
                candidateLeft = static_cast<std::uint32_t>(left);
                candidateRight = static_cast<std::uint32_t>(right);
                candidateUp = static_cast<std::uint32_t>(up);
                candidateDown = static_cast<std::uint32_t>(down);
            }
            if (version >= 7 &&
                (!(input >> std::quoted(candidateHint)) || candidateHint.size() > 58)) {
                return;
            }
        }
    }
    auto candidateHalls = halls_;
    if (version >= 2) {
        for (auto& hall : candidateHalls) {
            std::size_t count = 0;
            if (!(input >> count) || count > SuperHallTable::Capacity) return;
            std::vector<HallEntry> entries;
            entries.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                std::string name;
                std::uint64_t score = 0;
                if (!(input >> std::quoted(name) >> score) || name.empty() ||
                    name.size() > 25 || score > MuncherScore::MaximumScore) return;
                entries.push_back({std::move(name), static_cast<std::uint32_t>(score)});
            }
            if (!std::is_sorted(entries.begin(), entries.end(),
                [](const HallEntry& left, const HallEntry& right) {
                    return left.score > right.score;
                })) return;
            hall = std::move(entries);
        }
    }
    input >> std::ws;
    if (!input.eof()) return;
    soundOn_ = sound != 0;
    musicOn_ = music != 0;
    speakerEffects_ = speaker != 0;
    answerVisionOption_ = answerVision != 0;
    boardSettings_ = candidateBoard;
    difficultyMode_ = candidateDifficultyMode;
    quickSet_ = candidateQuickSet;
    joystickEnabled_ = candidateJoystickEnabled;
    joystickCalibrated_ = candidateJoystickCalibrated;
    joystickLeftThreshold_ = candidateLeft;
    joystickRightThreshold_ = candidateRight;
    joystickUpThreshold_ = candidateUp;
    joystickDownThreshold_ = candidateDown;
    password_ = std::move(candidatePassword);
    passwordHint_ = std::move(candidateHint);
    halls_ = std::move(candidateHalls);
}

void SuperGame::saveSettings() const {
    if (!settingsPersistenceEnabled_) return;
    const std::filesystem::path target(settingsPath());
    std::filesystem::path temporary = target;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return;
        output << "SUPER_MUNCHERS_NATIVE_SETTINGS 7\n"
               << (soundOn_ ? 1 : 0) << ' ' << (musicOn_ ? 1 : 0) << ' '
               << (speakerEffects_ ? 1 : 0) << ' '
               << (answerVisionOption_ ? 1 : 0) << '\n';
        output << boardSettings_.difficulty << ' '
               << (boardSettings_.negations ? 1 : 0) << ' ' << quickSet_ << ' '
               << difficultyMode_ << '\n';
        for (const bool topic : boardSettings_.selectedTopics) {
            output << (topic ? 1 : 0) << ' ';
        }
        output << '\n';
        for (const auto& rules : boardSettings_.selectedRules) {
            for (const bool rule : rules) output << (rule ? 1 : 0) << ' ';
            output << '\n';
        }
        output << (joystickEnabled_ ? 1 : 0) << ' '
               << std::quoted(password_) << '\n';
        output << (joystickCalibrated_ ? 1 : 0) << ' '
               << joystickLeftThreshold_ << ' ' << joystickRightThreshold_ << ' '
               << joystickUpThreshold_ << ' ' << joystickDownThreshold_ << '\n';
        output << std::quoted(passwordHint_) << '\n';
        for (const auto& hall : halls_) {
            output << hall.size() << '\n';
            for (const HallEntry& entry : hall) {
                output << std::quoted(entry.name) << ' ' << entry.score << '\n';
            }
        }
        output.flush();
        if (!output) return;
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        (void)DeleteFileW(temporary.c_str());
    }
}

void SuperGame::drawSprite(Renderer& renderer, const std::uint32_t sheetId,
                           const int frame, const int x, const int y,
                           const bool transparentBlack) {
    const auto source = assets_.spriteFrame(sheetId, frame);
    const std::uint32_t sourceSheet = source && source->sheetId ? source->sheetId : sheetId;
    const Image* image = assets_.image(sourceSheet);
    if (!source || !image) return;

    if (renderer.graphicsMode() == GraphicsMode::Vga256 &&
        image->sourceIndices.size() == image->pixels.size()) {
        // SM's eight-bit PCXF sprites retain the indexes written by the art
        // tools but omit a local 256-entry palette.  EGAT is only a fallback
        // bridge into the PCX header's 16 colours; the live game instead
        // presents those original indexes through its resident VGA DAC.
        // The BTMP presenter treats slot zero as its colour key, leaving the
        // active page (board blue in gameplay, black in missions) untouched.
        for (int row = 0; row < source->height; ++row) {
            const int sourceY = source->y + row;
            if (sourceY < 0 || sourceY >= image->height) continue;
            for (int column = 0; column < source->width; ++column) {
                const int sourceX = source->x + column;
                if (sourceX < 0 || sourceX >= image->width) continue;
                const std::size_t offset =
                    static_cast<std::size_t>(sourceY * image->width + sourceX);
                const std::uint8_t index = image->sourceIndices[offset];
                // PCXF index zero is the sprite colour key.  Leaving the
                // destination untouched also preserves board grid strokes
                // crossed by the actor's rectangular BTMP record.
                if (index == 0) continue;
                std::uint32_t color = superResidentPalette()[index];
                // The live startup/board captures establish every resident
                // slot exercised by Muncher art.  Until another actor slot is
                // measured, retain its decoded header-palette colour instead
                // of silently painting an unknown nonzero index black.
                if (color == Colors::Black && index != 0 &&
                    (image->pixels[offset] & 0xffffffu) != 0) {
                    color = image->pixels[offset];
                }
                renderer.pixel(x + column, y + row, color);
            }
        }
        return;
    }
    renderer.drawImageRegion(*image, source->x, source->y,
                             source->width, source->height, x, y,
                             source->width, source->height,
                             transparentBlack, sheetId == 1006 || sheetId == 1013,
                             false, false,
                             sheetId >= 1007 && sheetId <= 1011);
}

void SuperGame::drawCellText(Renderer& renderer, const int index,
                             const std::string_view text,
                             const std::uint32_t color) {
    const GemFont& font = assets_.smallFont();
    const int column = index % BoardColumns;
    const int row = index / BoardColumns;
    const int x = BoardLeft + column * BoardCellWidth + BoardCellWidth / 2;
    const int y = BoardTop + row * BoardCellHeight;
    const auto drawOriginalCentered = [&](const int centerX, const int textY,
                                          const std::string_view value) {
        // SM's text owner rounds an even advance width toward the right-hand
        // pixel. Renderer::drawCenteredText uses the opposite integer tie,
        // which moved about half of the captured board words one pixel left.
        const int width = renderer.textWidth(font, value);
        renderer.drawText(font, centerX - (width - 1) / 2, textY, value, color);
    };
    const std::size_t split = text.find('~');
    if (split == std::string_view::npos) {
        drawOriginalCentered(x, y + 12, text);
    } else {
        drawOriginalCentered(x, y + 8, text.substr(0, split));
        drawOriginalCentered(x, y + 17, text.substr(split + 1));
    }
}

void SuperGame::renderStartupVersion(Renderer& renderer) {
    renderer.clear(Colors::White);
    const GemFont& font = assets_.largeFont();
    const BlobView product = loadEmbeddedResource(IDR_SM_PRODUCT_PF);
    std::string productName = "Super Munchers";
    std::string copyright = "Copyright 1991, MECC";
    int versionMajor = 1;
    // PRODUCT.PF's second word is an edition field in this release.  The live
    // startup painter identifies the supplied executable as Version 1.0.
    int versionMinor = 0;
    const auto read16 = [](const std::uint8_t* bytes) {
        return static_cast<int>(bytes[0] |
            (static_cast<std::uint16_t>(bytes[1]) << 8));
    };
    const auto field = [&product](const std::size_t offset) {
        if (!product || offset >= product.size) return std::string{};
        const std::uint8_t* begin = product.data + offset;
        const std::uint8_t* end = product.data + product.size;
        const std::uint8_t* terminator = std::find(
            begin, end, static_cast<std::uint8_t>(0));
        return std::string(reinterpret_cast<const char*>(begin),
                           reinterpret_cast<const char*>(terminator));
    };
    if (product && product.size >= 0x143) {
        versionMajor = read16(product.data + 8);
        if (std::string value = field(0x16); !value.empty()) copyright = std::move(value);
        if (std::string value = field(0x135); !value.empty()) productName = std::move(value);
    }
    renderer.drawCenteredText(font, 159, 35, productName, Colors::Black);
    renderer.drawCenteredText(font, 159, 47,
                              "Version " + std::to_string(versionMajor) + "." +
                                  std::to_string(versionMinor),
                              Colors::Black);
    if (const Image* logo = assets_.startupLogo()) renderer.drawImage(*logo, 0, 75);
    renderer.drawCenteredText(font, 159, 185, copyright, Colors::Black);
}

void SuperGame::renderStartup(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (const Image* logo = assets_.image(6005)) {
        if (graphicsMode_ == GraphicsMode::Vga256) drawSuperTitlePage(renderer, *logo);
        else renderer.drawImage(*logo, 0, 0);
    }
    renderer.drawCenteredText(assets_.largeFont(), 159, 185,
                              "Press a key for Muncher Menu", Colors::White);
}

void SuperGame::renderTitle(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (const Image* title = assets_.image(6009)) {
        if (graphicsMode_ == GraphicsMode::Vga256) drawSuperTitlePage(renderer, *title);
        else renderer.drawImage(*title, 0, 0);
    }
    const GemFont& font = assets_.largeFont();
    for (int index = 0; index < static_cast<int>(TitleLabels.size()); ++index) {
        const int y = TitleRows[static_cast<std::size_t>(index)];
        if (menuSelection_ == index) {
            renderer.drawText(font, 57, y, std::to_string(index + 1) + ".",
                              Colors::White);
            const std::string_view label = TitleLabels[static_cast<std::size_t>(index)];
            renderer.fillRect(71, y - 1, renderer.textWidth(font, label) + 21,
                              10, Colors::White);
            renderer.drawText(font, 81, y, label, Colors::Black);
        } else {
            renderer.drawText(font, 57, y,
                              std::to_string(index + 1) + ". " +
                                  std::string(TitleLabels[static_cast<std::size_t>(index)]),
                              Colors::White);
        }
    }
    renderer.drawText(font, 10, 170,
                      "Use Arrows to move, then press Enter.", Colors::White);
}

void SuperGame::renderInstructionsQuestion(Renderer& renderer) {
    drawSuperDialogFrame(renderer);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 60, 85, "Do you want instructions?", Colors::White);
    const auto choice = [&](const int x, const std::string_view label,
                            const bool selected) {
        if (selected) {
            renderer.fillRect(x - 2, 104, renderer.textWidth(font, label) + 5,
                              10, Colors::White);
        }
        renderer.drawText(font, x, 105, label,
                          selected ? Colors::Black : Colors::White);
    };
    choice(115, " Yes ", menuSelection_ == 0);
    choice(165, " No ", menuSelection_ == 1);
}

void SuperGame::renderGameSelect(Renderer& renderer) {
    drawSuperDialogFrame(renderer);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 16, 20, "Which Muncher game would you", Colors::White);
    renderer.drawText(font, 16, 29, "like to play?", Colors::White);
    for (int index = 0; index < static_cast<int>(gameChoices_.size()); ++index) {
        const int game = gameChoices_[static_cast<std::size_t>(index)];
        const int y = 68 + index * 11;
        renderer.drawText(font, 88, y, std::to_string(index + 1) + ".",
                          Colors::White);
        const std::string_view label = GameLabels[static_cast<std::size_t>(game)];
        if (menuSelection_ == index) {
            renderer.fillRect(102, y - 1, renderer.textWidth(font, label) + 21,
                              10, Colors::White);
        }
        renderer.drawText(font, 112, y, label,
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 13, 160,
                      "Use Arrows to move, then press Enter.", Colors::White);
}

void SuperGame::renderGameDifficultySelect(Renderer& renderer) {
    drawSuperDialogFrame(renderer);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 24, 20, "Choose a difficulty level.", Colors::White);
    for (int index = 0; index < 3; ++index) {
        const int y = 86 + index * 11;
        renderer.drawText(font, 88, y, std::to_string(index + 1) + ".",
                          Colors::White);
        const std::string_view label = DifficultyLabels[static_cast<std::size_t>(index)];
        if (menuSelection_ == index) {
            renderer.fillRect(102, y - 1, renderer.textWidth(font, label) + 21,
                              10, Colors::White);
        }
        renderer.drawText(font, 112, y, label,
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 13, 160,
                      "Use Arrows to move, then press Enter.", Colors::White);
}

void SuperGame::renderHallSelect(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 18, "Which Hall of Heroes would you", Colors::White);
    renderer.drawCenteredText(font, 159, 29, "like to see?", Colors::White);
    for (int index = 0; index < 7; ++index) {
        const std::string label = " " + std::to_string(index + 1) + ". " +
            std::string(GameLabels[static_cast<std::size_t>(index)]) + " ";
        const int y = 61 + index * 14;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) {
            renderer.fillRect(x, y, width, font.height(), Colors::White);
        }
        renderer.drawText(font, x, y, label, menuTextColor(menuSelection_ == index));
    }
    renderer.drawCenteredText(font, 159, 177, "Escape: Main Menu", Colors::White);
}

void SuperGame::renderHall(Renderer& renderer) {
    const bool cga = renderer.graphicsMode() == GraphicsMode::Cga4;
    renderer.clear(cga ? Colors::Black : Colors::White);
    if (const Image* hall = assets_.image(6003)) renderer.drawImage(*hall, 0, 0, false, false, true);
    const GemFont& font = assets_.largeFont();
    const std::uint32_t color = cga ? Colors::White : Colors::Black;
    renderer.drawCenteredText(font, 159, 35, "SUPER MUNCHERS", color);
    renderer.drawCenteredText(font, 159, 62, "Hall of Heroes", color);
    const auto& entries = halls_[static_cast<std::size_t>(selectedGame_)];
    if (entries.empty()) {
        renderer.drawCenteredText(font, 159, 120, "There are no entries", color);
        renderer.drawCenteredText(font, 159, 129, "in the Hall of Heroes.", color);
    } else {
        int y = 91;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            renderer.drawText(font, 54, y, std::to_string(index + 1) + ". " + entries[index].name, color);
            renderer.drawText(font, 236, y,
                              entries[index].score == MuncherScore::MaximumScore
                                  ? "Perfect" : std::to_string(entries[index].score),
                              color);
            y += 9;
        }
    }
    renderer.drawCenteredText(font, 159, 185, "Press Space Bar to continue.", color);
}

void SuperGame::renderNameEntry(Renderer& renderer) {
    renderBoard(renderer);
    const GemFont& font = assets_.largeFont();
    renderer.fillRect(40, 57, 240, 86, Colors::Black);
    renderer.outlineRect(40, 57, 240, 86, Colors::White);
    renderer.drawCenteredText(font, 159, 67, "Congratulations!", Colors::Yellow);
    renderer.drawCenteredText(font, 159, 82,
                              "Type your name, and press Enter.", Colors::White);
    renderer.outlineRect(57, 101, 206, 19, Colors::White);
    renderer.drawText(font, 63, 106, nameEntry_, Colors::White);
    renderer.fillRect(63 + renderer.textWidth(font, nameEntry_), 112, 7, 2,
                      Colors::White);
}

void SuperGame::renderReplayQuestion(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 71, "Do you want to play", Colors::White);
    renderer.drawCenteredText(font, 159, 82, "again?", Colors::White);
    const auto choice = [&](const int x, const std::string_view text, const bool selected) {
        if (selected) renderer.fillRect(x - 2, 104,
            renderer.textWidth(font, text) + 4, font.height(), Colors::White);
        renderer.drawText(font, x, 105, text,
                          selected ? Colors::Black : Colors::White);
    };
    choice(115, " Yes ", menuSelection_ == 0);
    choice(165, " No ", menuSelection_ == 1);
}

void SuperGame::renderInformation(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    if (informationPage_ == 1) {
        drawSprite(renderer, 1006, 7, 54, 132, true);
        drawSprite(renderer, 1007, 7, 216, 132, true);
    } else if (informationPage_ == 8) {
        drawSprite(renderer, 1006, 7, 137, 58, true);
        drawSprite(renderer, 1013, 7, 137, 139, true);
    } else if (informationPage_ == 9) {
        drawSprite(renderer, 1007, 7, 54, 55, true);
        drawSprite(renderer, 1009, 7, 216, 55, true);
        drawSprite(renderer, 1011, 7, 137, 105, true);
        drawSprite(renderer, 1010, 7, 54, 145, true);
        drawSprite(renderer, 1008, 7, 216, 145, true);
    }

    const BlobView data = assets_.gameArchive().find("DATA", 1);
    std::string pageText;
    if (data && data.size >= 4) {
        const auto read16 = [&data](const std::size_t offset) {
            return static_cast<std::uint16_t>(data.data[offset] |
                (static_cast<std::uint16_t>(data.data[offset + 1]) << 8));
        };
        const int count = std::max(0, static_cast<int>(read16(0)) - 1);
        const int page = std::clamp(informationPage_, 0, std::max(0, count - 1));
        const std::size_t tableOffset = 2u + static_cast<std::size_t>(page) * 2u;
        if (page < count && tableOffset + 1 < data.size) {
            const std::size_t beginOffset = read16(tableOffset);
            const std::size_t endOffset = page + 1 < count
                ? read16(tableOffset + 2u) : data.size;
            if (beginOffset < endOffset && endOffset <= data.size) {
                const std::uint8_t* begin = data.data + beginOffset;
                const std::uint8_t* end = data.data + endOffset;
                const std::uint8_t* zero = std::find(
                    begin, end, static_cast<std::uint8_t>(0));
                pageText.assign(reinterpret_cast<const char*>(begin),
                                reinterpret_cast<const char*>(zero));
            }
        }
    }
    std::size_t position = 0;
    int row = 0;
    while (position <= pageText.size() && row < 18) {
        const std::size_t newline = pageText.find('\n', position);
        std::string_view line(pageText.data() + position,
            (newline == std::string::npos ? pageText.size() : newline) - position);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        renderer.drawText(font, 0, 5 + row * 10, line, Colors::White);
        ++row;
        if (newline == std::string::npos) break;
        position = newline + 1;
    }
    renderer.drawText(assets_.smallFont(), 270, 188,
                      informationPage_ < 12 ? "--more--" : "Main Menu",
                      Colors::White);
}

void SuperGame::renderOptions(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 5, "Options", Colors::White);
    renderer.horizontalLine(2, 317, 17, Colors::Cyan);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.horizontalLine(0, 319, 162, Colors::Cyan);
    renderer.horizontalLine(2, 317, 164, Colors::Cyan);
    renderer.drawText(font, 5, 32, "Choose an option:", Colors::White);
    for (int index = 0; index < static_cast<int>(OptionLabels.size()); ++index) {
        std::string option(OptionLabels[static_cast<std::size_t>(index)]);
        if (index == 1) {
            option = answerVisionOption_ ? "Turn Answer Vision OFF"
                                         : "Turn Answer Vision ON";
        } else if (index == 4) {
            option = joystickEnabled_ ? "Turn Joystick OFF" : "Turn Joystick ON";
        }
        const int y = 72 + index * 11;
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, 74 - renderer.textWidth(font, prefix), y,
                          prefix, Colors::White);
        if (menuSelection_ == index) {
            renderer.fillRect(72, y - 1, renderer.textWidth(font, option) + 21,
                              10, Colors::White);
        }
        renderer.drawText(font, 82, y, option,
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 0, 167,
                      "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Main Menu", Colors::White);
}

void SuperGame::renderOptionsPassword(Renderer& renderer, const bool prompt) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    const bool hint = !prompt && passwordEditingHint_;
    renderer.drawCenteredText(font, 159, 28,
        prompt ? "Enter the password:"
               : hint ? "Enter a hint:" : "Enter a new password:", Colors::White);
    renderer.drawCenteredText(font, 159, 47,
        "Type entry, then press Enter.", Colors::White);
    renderer.outlineRect(hint ? 15 : 83, 76, hint ? 290 : 154, 20, Colors::White);
    const std::string displayed = hint
        ? passwordEntry_ : std::string(passwordEntry_.size(), '*');
    renderer.drawText(hint ? assets_.smallFont() : font,
                      hint ? 21 : 90, hint ? 83 : 82,
                      displayed, Colors::White);
    if (passwordError_) {
        renderer.drawCenteredText(font, 159, 113,
            "That password is not correct.", Colors::Yellow);
        renderer.drawCenteredText(font, 159, 124,
            "Please try again.", Colors::Yellow);
    }
    if (prompt && !passwordHint_.empty()) {
        renderer.drawCenteredText(assets_.smallFont(), 159, 146,
                                  "Hint: " + passwordHint_, Colors::White);
    }
    renderer.drawCenteredText(font, 159, 177,
        prompt ? "Escape: Main Menu" : "Escape: Options Menu", Colors::White);
}

void SuperGame::renderOptionsEraseHallSelect(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 5, "Select Hall of Heroes", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.drawCenteredText(font, 159, 29,
        "Choose a Hall of Heroes to erase:", Colors::White);
    for (int index = 0; index < 8; ++index) {
        const std::string option = index == 0 ? "Erase ALL Lists"
            : std::string(GameLabels[static_cast<std::size_t>(index - 1)]);
        const std::string label = " " + std::to_string(index + 1) + ". " + option + " ";
        const int y = 49 + index * 15;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) {
            renderer.fillRect(x, y, width, font.height(), Colors::White);
        }
        renderer.drawText(font, x, y, label, menuTextColor(menuSelection_ == index));
    }
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);
}

void SuperGame::renderOptionsEraseEntries(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    const int hall = std::clamp(eraseHallIndex_, 0, 6);
    renderer.drawCenteredText(font, 159, 5, "Erase Hall of Heroes", Colors::White);
    renderer.drawCenteredText(font, 159, 20,
                              GameLabels[static_cast<std::size_t>(hall)],
                              Colors::Cyan);
    if (eraseDraft_.empty()) {
        renderer.drawCenteredText(font, 159, 77,
                                  "There are currently no entries in the",
                                  Colors::White);
        renderer.drawCenteredText(font, 159, 89, "Hall of Heroes.", Colors::White);
    } else {
        for (int index = 0; index < static_cast<int>(eraseDraft_.size()); ++index) {
            const HallEntry& entry = eraseDraft_[static_cast<std::size_t>(index)];
            const int y = 48 + index * 13;
            const std::string label = entry.name + "  " + std::to_string(entry.score);
            if (menuSelection_ == index) {
                renderer.fillRect(18, y - 1, 284, 10, Colors::White);
            }
            renderer.drawText(font, 22, y, label,
                              menuTextColor(menuSelection_ == index));
        }
    }
    renderer.drawText(assets_.smallFont(), 4, 171,
                      "Use arrows to move, Space Bar to delete.", Colors::White);
    renderer.drawText(assets_.smallFont(), 4, 181,
                      "Enter: accept changes   Escape: cancel", Colors::White);
}

void SuperGame::renderOptionsEraseConfirm(Renderer& renderer) {
    renderOptionsEraseHallSelect(renderer);
    const GemFont& font = assets_.largeFont();
    renderer.fillRect(25, 61, 270, 80, Colors::Black);
    renderer.outlineRect(25, 61, 270, 80, Colors::White);
    renderer.drawCenteredText(font, 159, 72,
        "You are about to PERMANENTLY delete", Colors::White);
    renderer.drawCenteredText(font, 159, 84,
        eraseHallIndex_ < 0 ? "all Hall of Heroes entries."
                            : "this Hall of Heroes list.", Colors::White);
    renderer.drawCenteredText(font, 159, 99,
        "Do you really want to do this?", Colors::White);
    const auto choice = [&](const int x, const std::string_view label, const bool selected) {
        if (selected) renderer.fillRect(x - 2, 119,
            renderer.textWidth(font, label) + 4, font.height(), Colors::White);
        renderer.drawText(font, x, 120, label,
                          selected ? Colors::Black : Colors::White);
    };
    choice(111, " Yes ", menuSelection_ == 0);
    choice(168, " No ", menuSelection_ == 1);
}

void SuperGame::renderOptionsCalibration(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 30, "Calibrate Joystick", Colors::White);
    if (!joystickConnected_) {
        renderer.drawCenteredText(font, 159, 74,
                                  "Attach joystick to computer.", Colors::White);
        renderer.drawCenteredText(font, 159, 115,
                                  "Escape: Options Menu", Colors::White);
        return;
    }
    renderer.drawCenteredText(font, 159, 61,
                              "Move the joystick to every edge.", Colors::White);
    renderer.drawCenteredText(font, 159, 75,
                              "Then press Space or a joystick button.", Colors::White);
    renderer.drawCenteredText(assets_.smallFont(), 159, 103,
        "X " + std::to_string(joystickX_) + "   Y " + std::to_string(joystickY_),
        Colors::White);
    const bool sampled = calibrationMinX_ != 0xffffffffu &&
                         calibrationMinY_ != 0xffffffffu;
    if (sampled) {
        renderer.drawCenteredText(assets_.smallFont(), 159, 117,
            "X range " + std::to_string(calibrationMinX_) + ".." +
                std::to_string(calibrationMaxX_), Colors::White);
        renderer.drawCenteredText(assets_.smallFont(), 159, 129,
            "Y range " + std::to_string(calibrationMinY_) + ".." +
                std::to_string(calibrationMaxY_), Colors::White);
    }
    renderer.drawCenteredText(font, 159, 166, "Escape: Cancel", Colors::White);
}

void SuperGame::renderOptionsContent(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 5, "Set Content", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.drawText(font, 5, 32, "Current Difficulty: " +
        std::string(DifficultyLabels[static_cast<std::size_t>(
            std::clamp(difficultyMode_, 0, 3))]),
        Colors::White);
    for (int index = 0; index < static_cast<int>(ContentOptionLabels.size()); ++index) {
        std::string option(ContentOptionLabels[static_cast<std::size_t>(index)]);
        if (index == 3) option = boardSettings_.negations
            ? "Turn Negations OFF" : "Turn Negations ON";
        const std::string label = " " + std::to_string(index + 1) + ". " + option + " ";
        const int y = 75 + index * 18;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) renderer.fillRect(x, y, width, font.height(), Colors::White);
        renderer.drawText(font, x, y, label, menuTextColor(menuSelection_ == index));
    }
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);
}

void SuperGame::renderOptionsQuickSet(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 5, "Quick Set", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.drawCenteredText(font, 159, 31, "Choose an option:", Colors::White);
    for (int index = 0; index < static_cast<int>(QuickSetLabels.size()); ++index) {
        const std::string label = " " + std::to_string(index + 1) + ". " +
            std::string(QuickSetLabels[static_cast<std::size_t>(index)]) + " ";
        const int y = 55 + index * 18;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) renderer.fillRect(x, y, width, font.height(), Colors::White);
        renderer.drawText(font, x, y, label, menuTextColor(menuSelection_ == index));
    }
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void SuperGame::renderOptionsGames(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 5, "Set Games", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.drawText(font, 5, 30, "Choose topics to be used:", Colors::White);
    for (int index = 0; index < 6; ++index) {
        const bool enabled = boardSettings_.selectedTopics[static_cast<std::size_t>(index)];
        const std::string label = std::string(enabled ? "[X] " : "[ ] ") +
            std::string(GameLabels[static_cast<std::size_t>(index)]);
        const int y = 52 + index * 18;
        if (menuSelection_ == index) {
            renderer.fillRect(48, y, renderer.textWidth(font, label) + 8,
                              font.height(), Colors::White);
        }
        renderer.drawText(font, 52, y, label,
                          menuTextColor(menuSelection_ == index));
    }
    renderer.drawText(font, 0, 176, "Space: select", Colors::White);
    renderer.drawText(font, 125, 176, "Enter: define", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void SuperGame::renderOptionsRules(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    const GemFont& small = assets_.smallFont();
    const SuperCategorySet* set = content_.set(static_cast<std::size_t>(ruleTopic_));
    renderer.drawCenteredText(font, 159, 4, "Set Games Menu", Colors::White);
    renderer.horizontalLine(0, 319, 17, Colors::Cyan);
    renderer.drawText(font, 3, 22,
        "Difficulty: " + std::string(DifficultyLabels[static_cast<std::size_t>(
            std::clamp(difficultyMode_, 0, 3))]), Colors::White);
    renderer.drawText(font, 3, 33,
        "Choose rules to be used: " + (set ? set->title : std::string{}),
        Colors::White);
    if (set) {
        const int count = static_cast<int>(set->rules.size());
        const int end = std::min(count, ruleOffset_ + 10);
        const int activeDifficulty = difficultyMode_ == 3 ? 30 : boardSettings_.difficulty;
        for (int rule = ruleOffset_; rule < end; ++rule) {
            const SuperRule& source = set->rules[static_cast<std::size_t>(rule)];
            const bool available = source.minimumDifficulty() <= activeDifficulty;
            const bool selected = boardSettings_.selectedRules[
                static_cast<std::size_t>(ruleTopic_)][static_cast<std::size_t>(rule)];
            std::string label = selected ? "[X] " : "[ ] ";
            label += source.text;
            label += " (class " + std::to_string(
                std::clamp(static_cast<int>(source.minimumDifficulty()) / 10, 1, 3)) + ")";
            if (label.size() > 51) label.resize(51);
            const int y = 48 + (rule - ruleOffset_) * 12;
            if (menuSelection_ == rule) {
                renderer.fillRect(1, y - 1, 317, 10, Colors::White);
            }
            renderer.drawText(small, 4, y, label,
                menuSelection_ == rule ? Colors::Black
                    : available ? Colors::White : Colors::Gray);
        }
        if (end < count) renderer.drawText(font, 264, 170, "-more-", Colors::White);
    }
    renderer.drawText(small, 0, 181,
        "Space: change  Enter: view  F2: all  F3: one  Esc: games", Colors::White);
}

void SuperGame::renderOptionsRuleWords(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    const GemFont& small = assets_.smallFont();
    const SuperCategorySet* set = content_.set(static_cast<std::size_t>(ruleTopic_));
    renderer.drawCenteredText(font, 159, 4, "View Words", Colors::White);
    renderer.horizontalLine(0, 319, 17, Colors::Cyan);
    std::vector<std::string_view> targets;
    if (set && menuSelection_ >= 0 &&
        menuSelection_ < static_cast<int>(set->rules.size())) {
        renderer.drawText(small, 3, 22, "Topic: " + set->title, Colors::White);
        std::string ruleLine = "Rule: " +
            set->rules[static_cast<std::size_t>(menuSelection_)].text;
        if (ruleLine.size() > 61) ruleLine.resize(61);
        renderer.drawText(small, 3, 31, ruleLine, Colors::White);
        const int activeDifficulty = difficultyMode_ == 3 ? 30 : boardSettings_.difficulty;
        for (const SuperWord& word : set->words) {
            if (word.difficultyRank <= activeDifficulty &&
                static_cast<std::size_t>(menuSelection_) < word.membership.size() &&
                word.membership[static_cast<std::size_t>(menuSelection_)] ==
                    SuperMembership::Match) {
                targets.push_back(word.text);
            }
        }
    }
    if (!targets.empty()) {
        const int offset = previewOffset_ >= static_cast<int>(targets.size())
            ? 0 : previewOffset_;
        const int end = std::min(static_cast<int>(targets.size()), offset + 60);
        for (int index = offset; index < end; ++index) {
            const int display = index - offset;
            renderer.drawText(small, 4 + (display / 12) * 63,
                              45 + (display % 12) * 10,
                              targets[static_cast<std::size_t>(index)], Colors::White);
        }
    } else {
        renderer.drawCenteredText(font, 159, 89, "There are no words.", Colors::White);
    }
    renderer.drawText(small, 0, 178,
        "Space: more  Tab: next rule  Page Up/Down: page", Colors::White);
    renderer.drawText(small, 0, 188, "Escape: Define Game Menu", Colors::White);
}

void SuperGame::renderOptionsDifficulty(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    if (difficultyErrorTopic_ >= 0) {
        const SuperCategorySet* set = content_.set(
            static_cast<std::size_t>(difficultyErrorTopic_));
        renderer.drawCenteredText(font, 159, 35, "There are no rules defined in",
                                  Colors::White);
        renderer.drawCenteredText(font, 159, 52, set ? set->title : "this game",
                                  Colors::Yellow);
        renderer.drawCenteredText(font, 159, 82,
            "You must deselect or redefine that", Colors::White);
        renderer.drawCenteredText(font, 159, 99,
            "game before selecting this level.", Colors::White);
        renderer.drawCenteredText(font, 159, 145, "Press any key.", Colors::White);
        return;
    }
    renderer.drawCenteredText(font, 159, 5, "Adjust Difficulty", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.drawCenteredText(font, 159, 34, "Choose a difficulty level.", Colors::White);
    for (int index = 0; index < 4; ++index) {
        const std::string label = " " + std::to_string(index + 1) + ". " +
            std::string(DifficultyLabels[static_cast<std::size_t>(index)]) + " ";
        const int y = 67 + index * 23;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) renderer.fillRect(x, y, width, font.height(), Colors::White);
        renderer.drawText(font, x, y, label, menuTextColor(menuSelection_ == index));
    }
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void SuperGame::renderOptionsPreview(Renderer& renderer) {
    renderer.clear(Colors::Black);
    const GemFont& font = assets_.largeFont();
    const GemFont& small = assets_.smallFont();
    renderer.drawCenteredText(font, 159, 5, "Preview Words", Colors::White);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    const SuperCategorySet* set = content_.set(static_cast<std::size_t>(previewTopic_));
    renderer.drawCenteredText(font, 159, 28,
                              set ? set->title : "", Colors::White);
    if (set) {
        const int end = std::min(static_cast<int>(set->words.size()), previewOffset_ + 60);
        for (int index = previewOffset_; index < end; ++index) {
            const int display = index - previewOffset_;
            const int column = display / 12;
            const int row = display % 12;
            renderer.drawText(small, 4 + column * 63, 45 + row * 10,
                              set->words[static_cast<std::size_t>(index)].text,
                              Colors::White);
        }
    }
    renderer.drawText(font, 0, 176, "Press Space Bar to see more.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void SuperGame::renderBoard(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.smallFont();
    renderer.drawText(font, 0, 3, demoMode_ ? "Demo" : "Level:", Colors::White);
    if (!demoMode_) {
        renderer.drawText(font, 9, 12, std::to_string(core_.level()), Colors::White);
    }
    renderer.horizontalLine(44, 252, 2, Colors::White);
    renderer.horizontalLine(44, 252, 16, Colors::White);
    renderer.drawCenteredText(font, 148, 6, core_.board().displayedRule,
                              Colors::White);
    renderer.outlineRect(1, 24, 317, 155, Colors::Magenta);
    for (int row = 0; row <= BoardRows; ++row) {
        renderer.horizontalLine(BoardLeft, BoardRight,
                                BoardTop + row * BoardCellHeight, Colors::Magenta);
    }
    for (int column = 0; column <= BoardColumns; ++column) {
        renderer.verticalLine(BoardLeft + column * BoardCellWidth,
                              BoardTop, BoardBottom, Colors::Magenta);
    }
    for (int index = 0; index < BoardCellCount; ++index) {
        if (core_.eaten(static_cast<std::size_t>(index))) continue;
        // The source board owner clears the selected word before it starts
        // the eight-tick chew actor. Safe-zone corners remain visible because
        // they are painted by the later overlay pass below.
        if (munching_ && index == munchCellIndex_) continue;
        const bool occupiedByVisibleTroggle = std::any_of(
            enemies_.begin(), enemies_.end(), [this, index](const Enemy& enemy) {
                if (enemy.overlapRetired ||
                    enemy.row * BoardColumns + enemy.column != index) return false;
                return !enemy.entering ||
                       (enemy.moving && enemyMovePhase(enemy) >= 2);
            });
        // The Troggle owner saves the cell record, clears its display while
        // the actor occupies that cell, then applies the type-specific trail
        // when the actor leaves. Painting the word underneath exposed white
        // fragments through the transparent sprite key.
        if (occupiedByVisibleTroggle) continue;
        if (index == core_.transformationCell()) {
            const int x = BoardLeft + index % BoardColumns * BoardCellWidth;
            const int y = BoardTop + index / BoardColumns * BoardCellHeight;
            drawSprite(renderer, 1021, 0, x + 4, y);
        } else if (!core_.answerVision() || core_.board().cells[static_cast<std::size_t>(index)].correct) {
            drawCellText(renderer, index,
                         core_.board().cells[static_cast<std::size_t>(index)].text,
                         Colors::White);
        }
    }
    for (int index = 0; index < BoardCellCount; ++index) {
        if (!safeCells_[static_cast<std::size_t>(index)]) continue;
        const int x = BoardLeft + index % BoardColumns * BoardCellWidth;
        const int y = BoardTop + index / BoardColumns * BoardCellHeight;
        constexpr int arm = 8;
        constexpr int left = 1;
        constexpr int right = BoardCellWidth - 1;
        constexpr int top = 1;
        constexpr int bottom = BoardCellHeight - 1;
        renderer.horizontalLine(x + left, x + left + arm, y + top, Colors::White);
        renderer.verticalLine(x + left, y + top, y + top + arm, Colors::White);
        renderer.horizontalLine(x + right - arm, x + right, y + top, Colors::White);
        renderer.verticalLine(x + right, y + top, y + top + arm, Colors::White);
        renderer.horizontalLine(x + left, x + left + arm, y + bottom, Colors::White);
        renderer.verticalLine(x + left, y + bottom - arm, y + bottom, Colors::White);
        renderer.horizontalLine(x + right - arm, x + right, y + bottom, Colors::White);
        renderer.verticalLine(x + right, y + bottom - arm, y + bottom, Colors::White);
    }
    if (enemyWarning_) {
        renderer.outlineRect(0, 62, 16, 75, Colors::White);
        constexpr std::string_view warning = "Troggle!";
        for (std::size_t index = 0; index < warning.size(); ++index) {
            renderer.drawText(font, 5, 67 + static_cast<int>(index) * 8,
                              warning.substr(index, 1), Colors::White);
        }
    }

    renderer.drawText(font, 0, 187, "Score: " +
                      std::to_string(core_.scoreState().score()), Colors::White);
    const int lifeStart = graphicsMode_ == GraphicsMode::Cga4 ? 80 : 81;
    const int lifeSpacing = graphicsMode_ == GraphicsMode::Cga4 ? 32 : 33;
    for (int life = 0; life < core_.scoreState().visibleReserves(); ++life) {
        drawSprite(renderer, 1006, 17, lifeStart + life * lifeSpacing, 179);
    }
    renderer.drawText(font, 214, 182, "Munchmeter:", Colors::White);
    renderer.outlineRect(214, 190, 103, 9,
                         graphicsMode_ == GraphicsMode::Cga4
                             ? Colors::White : 0xff5555u);
    const int meter = std::clamp(core_.transformationMeter(), 0, 20);
    renderer.fillRect(216, 192, meter * 5, 5,
                      core_.superForm() ? Colors::Yellow : Colors::Cyan);
    if (demoMode_) {
        renderer.drawCenteredText(assets_.smallFont(), 159, 179,
            "Press a key for Muncher Menu", Colors::White);
    }

    int frame = visiblePlayerFrame();
    const int playerInset = graphicsMode_ == GraphicsMode::Cga4 ? 5 : 8;
    double x = BoardLeft + playerColumn_ * BoardCellWidth + playerInset;
    double y = BoardTop + playerRow_ * BoardCellHeight + 1;
    if (moving_) {
        const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
        const double progress = static_cast<double>(movementPhase()) / (vertical ? 5.0 : 6.0);
        x = BoardLeft + moveFromColumn_ * BoardCellWidth + playerInset +
            (moveToColumn_ - moveFromColumn_) * BoardCellWidth * progress;
        y = BoardTop + moveFromRow_ * BoardCellHeight + 1 +
            (moveToRow_ - moveFromRow_) * BoardCellHeight * progress;
    }
    if (transforming_) {
        drawSprite(renderer, 1021, visibleTransformationFrame(),
                   BoardLeft + playerColumn_ * BoardCellWidth + playerInset,
                   BoardTop + playerRow_ * BoardCellHeight);
    } else {
        drawSprite(renderer, core_.superForm() ? 1013u : 1006u, frame,
                   static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    }

    for (const Enemy& enemy : enemies_) {
        if (enemy.overlapRetired) continue;
        if (enemy.entering &&
            (!enemy.moving || enemyMovePhase(enemy) < 2)) continue;
        const int enemyInset = graphicsMode_ == GraphicsMode::Cga4 ? 5 : 8;
        double enemyX = BoardLeft + enemy.column * BoardCellWidth + enemyInset;
        double enemyY = BoardTop + enemy.row * BoardCellHeight + 1;
        if (enemy.entering && !enemy.moving) {
            enemyX = BoardLeft + enemy.fromColumn * BoardCellWidth + enemyInset;
            enemyY = BoardTop + enemy.fromRow * BoardCellHeight + 1;
        } else if (enemy.moving) {
            const bool vertical = enemy.direction == 0 || enemy.direction == 2;
            const int steps = vertical ? 5 : 6;
            const double progress = static_cast<double>(enemyMovePhase(enemy)) /
                                    static_cast<double>(steps);
            enemyX = BoardLeft + enemy.fromColumn * BoardCellWidth + enemyInset +
                (enemy.column - enemy.fromColumn) * BoardCellWidth * progress;
            enemyY = BoardTop + enemy.fromRow * BoardCellHeight + 1 +
                (enemy.row - enemy.fromRow) * BoardCellHeight * progress;
        }
        drawSprite(renderer, static_cast<std::uint32_t>(1007 + enemy.type),
                   enemyFrame(enemy), static_cast<int>(std::lround(enemyX)),
                   static_cast<int>(std::lround(enemyY)));
    }

    if (page_ == SuperGamePage::Feedback && !feedbackMessage_.empty()) {
        const int width = renderer.textWidth(font, feedbackMessage_) + 20;
        renderer.fillRect((Renderer::Width - width) / 2, 87, width, 26, Colors::Black);
        renderer.outlineRect((Renderer::Width - width) / 2, 87, width, 26, Colors::White);
        renderer.drawCenteredText(font, 159, 96, feedbackMessage_, Colors::White);
    }
    if (page_ == SuperGamePage::LevelComplete && !missionScene_.valid()) {
        renderer.fillRect(72, 87, 176, 26, Colors::Black);
        renderer.outlineRect(72, 87, 176, 26, Colors::White);
        renderer.drawCenteredText(font, 159, 96, "Mission complete!", Colors::White);
    }
}

void SuperGame::renderPause(Renderer& renderer) {
    renderer.fillRect(103, 87, 114, 27, Colors::Black);
    renderer.outlineRect(103, 87, 114, 27, Colors::White);
    renderer.drawCenteredText(assets_.largeFont(), 159, 96, "Paused", Colors::White);
}

void SuperGame::renderQuitConfirm(Renderer& renderer) {
    renderer.fillRect(57, 78, 206, 50, Colors::Black);
    renderer.outlineRect(57, 78, 206, 50, Colors::White);
    renderer.drawCenteredText(assets_.largeFont(), 159, 90,
                              "Do you really want to quit?", Colors::White);
    renderer.drawCenteredText(assets_.largeFont(), 159, 108,
                              "Yes       No", Colors::White);
}

void SuperGame::renderMissionIntro(Renderer& renderer) {
    static constexpr std::array<std::string_view, 5> Titles = {
        "Mission 1:  Find the Map",
        "Mission 2:  Find the Key",
        "Mission 3:  Find the Password",
        "Mission 4:  Reach the Castle",
        "Mission 5:  Destroy the Monster!"
    };
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.smallFont();
    const int mission = std::clamp(missionIndex_, 0, 4);
    renderer.drawCenteredText(assets_.largeFont(), 159, 10,
                              Titles[static_cast<std::size_t>(mission)],
                              Colors::Yellow);
    drawMissionDataBlock(renderer, font,
        superDataEntry(assets_.gameArchive(), 2,
                       mission == 4 ? 12 : mission * 3), 34);
    renderer.drawCenteredText(font, 159, 184,
                              "Enter: begin mission   Esc: skip",
                              Colors::White);
}

void SuperGame::renderMissionQuestion(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    if (missionIndex_ == 0) {
        renderer.drawCenteredText(font, 159, 25,
                                  "Which disguised Troggle", Colors::White);
        renderer.drawCenteredText(font, 159, 37, "has the map?", Colors::White);
    } else if (missionIndex_ == 1) {
        renderer.drawCenteredText(font, 159, 31,
                                  "Where did the Troggle hide the key?",
                                  Colors::White);
    } else {
        renderer.drawCenteredText(font, 159, 20,
                                  "The password was hidden behind", Colors::White);
        renderer.drawCenteredText(font, 159, 32,
                                  "xxxxx.  Which word was put there?",
                                  Colors::White);
    }
    for (int index = 0; index < static_cast<int>(missionChoices_.size()); ++index) {
        const std::string label = " " + std::to_string(index + 1) + ". " +
                                  missionChoices_[static_cast<std::size_t>(index)] + " ";
        const int y = 96 + index * 18;
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (menuSelection_ == index) {
            renderer.fillRect(x, y, width, font.height(), Colors::White);
        }
        renderer.drawText(font, x, y, label,
                          menuTextColor(menuSelection_ == index));
    }
    renderer.drawCenteredText(assets_.smallFont(), 159, 184,
                              "Choose an answer and press Enter", Colors::White);
}

void SuperGame::renderMissionResult(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.smallFont();
    const int mission = std::clamp(missionIndex_, 0, 4);
    renderer.drawCenteredText(assets_.largeFont(), 159, 12,
                               missionSucceeded_ ? "Mission accomplished!" : "Mission failed",
                               missionSucceeded_ ? Colors::Yellow : Colors::White);
    std::string text = superDataEntry(
        assets_.gameArchive(), 2, mission * 3 + (missionSucceeded_ ? 1 : 2));
    if (!missionSucceeded_) {
        if (mission <= 2 &&
            missionCorrectSelection_ >= 0 &&
            missionCorrectSelection_ < static_cast<int>(missionChoices_.size())) {
            const std::string answer = missionChoices_[
                static_cast<std::size_t>(missionCorrectSelection_)];
            replaceMissionPlaceholder(text, answer, mission == 0);
        } else if (mission == 3) {
            std::string reason = "did not reach the castle";
            if (missionCatchFailureReason_ == 1) reason = "missed a number";
            if (missionCatchFailureReason_ == 2) reason = "got hit with a rock";
            if (missionCatchFailureReason_ == 3) reason = "got hit with an anvil";
            replaceMissionPlaceholder(text, std::move(reason));
        }
    }
    drawMissionDataBlock(renderer, font, text, 35);
    renderer.drawCenteredText(font, 159, 184,
                               "Press a key to continue", Colors::White);
}

void SuperGame::renderCheat(Renderer& renderer) {
    renderer.fillRect(72, 69, 176, 62, Colors::Black);
    renderer.outlineRect(72, 69, 176, 62, Colors::Yellow);
    renderer.drawCenteredText(assets_.largeFont(), 159, 78,
                              "LEVEL SELECT", Colors::Yellow);
    renderer.drawCenteredText(assets_.largeFont(), 159, 96,
                              "<  Level " + std::to_string(menuSelection_) + "  >",
                              Colors::White);
    renderer.drawCenteredText(assets_.smallFont(), 159, 116,
                              "Enter: start   Esc: close", Colors::White);
}

void SuperGame::render(Renderer& renderer) {
    renderer.setGraphicsMode(graphicsMode_);
    switch (page_) {
    case SuperGamePage::StartupVersion: renderStartupVersion(renderer); break;
    case SuperGamePage::StartupSplash: renderStartup(renderer); break;
    case SuperGamePage::Title: renderTitle(renderer); break;
    case SuperGamePage::InstructionsQuestion: renderInstructionsQuestion(renderer); break;
    case SuperGamePage::GameSelect: renderGameSelect(renderer); break;
    case SuperGamePage::GameDifficultySelect: renderGameDifficultySelect(renderer); break;
    case SuperGamePage::HallSelect: renderHallSelect(renderer); break;
    case SuperGamePage::Hall: renderHall(renderer); break;
    case SuperGamePage::Information: renderInformation(renderer); break;
    case SuperGamePage::Options: renderOptions(renderer); break;
    case SuperGamePage::OptionsPasswordPrompt: renderOptionsPassword(renderer, true); break;
    case SuperGamePage::OptionsSetPassword: renderOptionsPassword(renderer, false); break;
    case SuperGamePage::OptionsEraseHallSelect: renderOptionsEraseHallSelect(renderer); break;
    case SuperGamePage::OptionsEraseEntries: renderOptionsEraseEntries(renderer); break;
    case SuperGamePage::OptionsEraseConfirm: renderOptionsEraseConfirm(renderer); break;
    case SuperGamePage::OptionsCalibration: renderOptionsCalibration(renderer); break;
    case SuperGamePage::OptionsContent: renderOptionsContent(renderer); break;
    case SuperGamePage::OptionsQuickSet: renderOptionsQuickSet(renderer); break;
    case SuperGamePage::OptionsGames: renderOptionsGames(renderer); break;
    case SuperGamePage::OptionsRules: renderOptionsRules(renderer); break;
    case SuperGamePage::OptionsRuleWords: renderOptionsRuleWords(renderer); break;
    case SuperGamePage::OptionsDifficulty: renderOptionsDifficulty(renderer); break;
    case SuperGamePage::OptionsPreview: renderOptionsPreview(renderer); break;
    case SuperGamePage::Playing:
    case SuperGamePage::Feedback: renderBoard(renderer); break;
    case SuperGamePage::LevelComplete:
        if (missionScene_.valid() &&
            missionSurfacePixels_.size() == renderer.pixels().size()) {
            renderer.clear(Colors::Black);
            renderer.replacePixels(missionSurfacePixels_);
            if (missionIndex_ == 3) {
                drawSprite(renderer, 1013, 7, missionPlayerX_, 165, true);
                renderer.drawText(assets_.smallFont(), 4, 188,
                                  "Caught: " + std::to_string(missionCaughtNumbers_),
                                  Colors::White);
            }
        } else {
            renderBoard(renderer);
        }
        break;
    case SuperGamePage::Paused: renderBoard(renderer); renderPause(renderer); break;
    case SuperGamePage::QuitConfirm: renderBoard(renderer); renderQuitConfirm(renderer); break;
    case SuperGamePage::NameEntry: renderNameEntry(renderer); break;
    case SuperGamePage::ReplayQuestion: renderReplayQuestion(renderer); break;
    case SuperGamePage::MissionIntro: renderMissionIntro(renderer); break;
    case SuperGamePage::MissionQuestion: renderMissionQuestion(renderer); break;
    case SuperGamePage::MissionResult: renderMissionResult(renderer); break;
    }
    if (cheatOpen_) renderCheat(renderer);
}
