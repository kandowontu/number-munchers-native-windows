#include "word_runtime.h"

#include "menu_input.h"
#include "mecc_sound.h"
#include "resource_ids.h"

#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 5> TitleLabels = {
    "Play Word Munchers", "Hall of Fame", "Information", "Options", "Quit"
};

// Main-controller actions 3 and 6 both call 0x11BC4. Its first operation at
// 0x11BCA-0x11BCE is the resident driver's literal 15 ms delay. Action 3 is
// already contained by the measured Demo-to-Hall transition; action 6 is the
// terminal title exit and needs its own native blocking boundary.
constexpr double MainControllerCleanupDelay = 0.015;

constexpr std::array<int, 5> TitleRows = {91, 102, 113, 124, 135};

constexpr std::array<std::string_view, 5> OptionsLabels = {
    "Set Content", "Erase Hall of Fame", "Set Password",
    "Turn Joystick ON ", "Calibrate Joystick"
};

constexpr std::array<std::string_view, 3> ContentLabels = {
    "Select Word Difficulty", "Select Vowel Sounds", "Preview Words"
};

constexpr std::array<std::string_view, 8> DifficultyLabels = {
    "1st Grade Easy", "1st Grade Advanced", "2nd Grade Easy",
    "2nd Grade Advanced", "3rd Grade Easy", "3rd Grade Advanced",
    "4th Grade", "5th Grade and Above"
};

constexpr std::array<std::string_view, 14> VowelChoiceLabels = {
    "a,a", "e,e", "i,i", "o,o", "u,u", "oo,oo",
    "ou/ow", "au/aw", "oi/oy",
    "ar", "air", "eer", "er/ir/ur", "or"
};

// DS:26F6 is the executable's exact 20x8 target-availability table. It is
// intentionally not identical to WLIST's physical cumulative record counts:
// several advanced sounds retain low-grade records for distractor use while
// exposing zero selectable target words in Options/Preview.
constexpr std::array<std::array<std::uint16_t, 8>, 20> WordTargetAvailabilityCounts = {{
    {{6, 30, 81, 119, 114, 164, 174, 180}},
    {{23, 50, 111, 142, 157, 170, 175, 180}},
    {{7, 29, 63, 104, 130, 143, 152, 170}},
    {{9, 37, 69, 97, 120, 136, 154, 166}},
    {{5, 31, 68, 92, 118, 131, 146, 152}},
    {{16, 47, 90, 131, 153, 162, 171, 180}},
    {{5, 26, 58, 96, 116, 139, 158, 174}},
    {{8, 24, 54, 67, 85, 95, 111, 118}},
    {{1, 2, 5, 10, 14, 16, 18, 23}},
    {{19, 40, 73, 114, 136, 160, 175, 180}},
    {{7, 14, 38, 63, 83, 96, 114, 131}},
    {{3, 14, 18, 21, 25, 25, 25, 25}},
    {{0, 13, 27, 33, 40, 52, 63, 78}},
    {{0, 10, 19, 27, 33, 47, 56, 76}},
    {{0, 0, 6, 14, 16, 18, 22, 29}},
    {{0, 7, 23, 30, 43, 52, 60, 67}},
    {{0, 5, 14, 20, 25, 29, 32, 33}},
    {{0, 5, 9, 10, 14, 18, 26, 37}},
    {{0, 13, 20, 35, 52, 72, 88, 103}},
    {{0, 9, 17, 33, 47, 62, 73, 81}},
}};

int targetAvailabilityCount(const int sound, const int difficulty) {
    if (sound < 0 || sound >= static_cast<int>(WordTargetAvailabilityCounts.size()) ||
        difficulty < 0 || difficulty >= 8) return 0;
    return WordTargetAvailabilityCounts[static_cast<std::size_t>(sound)]
                                       [static_cast<std::size_t>(difficulty)];
}

[[nodiscard]] bool virtualKeyDefersToCharacter(const UINT virtualKey) {
    // Win32 emits a following WM_CHAR for these keys. DOS exposes that
    // physical press as one event, so blocking gates defer the transition to
    // character() when the translated byte could otherwise leak onward.
    return (virtualKey >= '0' && virtualKey <= '9') ||
           (virtualKey >= 'A' && virtualKey <= 'Z') ||
           (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_DIVIDE) ||
           (virtualKey >= VK_OEM_1 && virtualKey <= VK_OEM_8) ||
           virtualKey == VK_OEM_102;
}

[[nodiscard]] bool belongsToDeferredBlockingKey(const wchar_t character) {
    // TranslateMessage also emits control bytes for several keys whose
    // WM_KEYDOWN half already satisfied the gate. Do not let that trailing
    // byte satisfy the following startup page as a second DOS event.
    return character != L'\b' && character != L'\t' && character != L'\n' &&
           character != L'\r' && character != L'\x1b' && character != L'\x7f';
}

struct WordVowelChoiceGeometry {
    int top;
    int left;
    int bottom;
    int right;
    int textY;
    int textX;
};

// DS:1D22 contains the fourteen inclusive checkbox rectangles. DS:1CE6
// supplies the paired text coordinates used by the original selector widget.
constexpr std::array<WordVowelChoiceGeometry, 14> VowelChoiceGeometry = {{
    {69, 26, 80, 37, 73, 45}, {82, 26, 93, 37, 86, 45},
    {95, 26, 106, 37, 99, 45}, {108, 26, 119, 37, 112, 45},
    {121, 26, 132, 37, 125, 45}, {134, 26, 145, 37, 138, 45},
    {69, 106, 80, 117, 73, 125}, {82, 106, 93, 117, 86, 125},
    {95, 106, 106, 117, 99, 125},
    {69, 196, 80, 207, 73, 215}, {82, 196, 93, 207, 86, 215},
    {95, 196, 106, 207, 99, 215}, {108, 196, 119, 207, 112, 215},
    {121, 196, 132, 207, 125, 215},
}};

void drawVowelStateBox(Renderer& renderer,
                       const int top,
                       const int left,
                       const int bottom,
                       const int right,
                       const std::uint8_t state) {
    const int width = right - left + 1;
    const int height = bottom - top + 1;
    renderer.fillRect(left, top, width, height, Colors::Black);

    // State bit 1 selects BGI LTSLASH_FILL. The exact driver pattern is the
    // eight-byte table at VGA256.BGI:0x6E8 and CGA.BGI:0xB68.
    if ((state & 0x02u) != 0) {
        constexpr std::array<std::uint8_t, 8> LightSlash = {
            0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
        };
        for (int y = top; y <= bottom; ++y) {
            const std::uint8_t row = LightSlash[static_cast<std::size_t>(y & 7)];
            for (int x = left; x <= right; ++x) {
                if ((row & static_cast<std::uint8_t>(0x80u >> (x & 7))) != 0) {
                    renderer.pixel(x, y, Colors::White);
                }
            }
        }
    }
    renderer.outlineRect(left, top, width, height, Colors::White);

    // 14977 calls the BGI getimage/NOT_PUT helper with deltas 7,7. BGI uses
    // inclusive endpoints, so the selected mark inverts an exact 8x8 area.
    if ((state & 0x01u) != 0) {
        for (int y = top + 2; y <= top + 9; ++y) {
            for (int x = left + 2; x <= left + 9; ++x) {
                const std::uint32_t source = renderer.pixels()[
                    static_cast<std::size_t>(y) * Renderer::Width +
                    static_cast<std::size_t>(x)];
                renderer.pixel(x, y, (~source) & 0x00ffffffu);
            }
        }
    }
}

// The Word executable installs the same PIT divisor and 30-way scheduler
// divider recovered from the Number executable. SCPT wait/movement counts are
// expressed in these public scheduler ticks.
constexpr double OriginalSceneTicksPerSecond =
    1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);
// SCPT actors are dispatched by the cartoon driver's separate 100 ms clock,
// not by the 29.1375 Hz public gameplay-job clock above. Lossless live Word
// captures expose seven 70.086304 Hz video samples per ordinary scene tick;
// the 102/142/133/106/110/251 terminal counts also align with their original
// 11.8/16.0/15.6/12.0/14.0/28.0-second score banks only at 10 Hz.
constexpr double OriginalCartoonTicksPerSecond = 10.0;
constexpr double JobTimerEpsilon = 1e-9;
// Word's startup controller at image 0x1193c passes literal 0x012c to the
// common event-or-clock waiter. Input may finish it early; absent input, the
// version page yields to the separately input-gated device splash at tick 300.
constexpr double StartupVersionWaitTicks = 300.0;

// Word installs the byte-for-byte same selector-5 demo record as Number:
// countdown 30, then random(0,30)+15 on every callback. The feedback and
// main-screen records retain the shared 150/450-tick lifecycle. There is no
// intervening 20-tick record: controller action 3 calls 0x11993, whose type-1
// PCX path uses the same logical 6051/6053 Wipe resources and driver as Number
// (6050/6052 are their CGA partners).
constexpr double AttractTitleIdleDuration = 30.0;
constexpr double AttractInitialActionDelay = 30.0 / OriginalSceneTicksPerSecond;
// The seeded 0x8E74 DOS trajectory measures a 30-public-tick phase advance
// each time selector 1 enters its arrival-wait state.  The initial two
// literal-295 records warn at selector-5 tick 265, consume calls 267..270,
// and only then does Demo consume calls 271/272; a later retired slot exposes
// the same phase before calls 309..315.  Keep the stored 210..389 values exact
// for static/content purposes and model the recovered dispatcher phase here.
constexpr double EnemyArrivalJobPhaseAdvance =
    30.0 / OriginalSceneTicksPerSecond;
constexpr double AttractFeedbackDuration = 150.0 / OriginalSceneTicksPerSecond;
constexpr double AttractCaptureFrameDuration = 31250.0 / 2190197.0;
constexpr int AttractPostFeedbackBoardFrames = 39;
constexpr double AttractPostFeedbackBoardDuration =
    AttractPostFeedbackBoardFrames * AttractCaptureFrameDuration;
constexpr int AttractCollisionHallWipeFrames = 6;
constexpr int AttractCleanHallWipeFrames = 7;
constexpr int AttractHallToLogoFrames = 6;
constexpr int AttractLogoToBoardFrames = 9;
// Both originals wrap an initialized every-third-board scene in the shared
// type-2/type-1 Wipe. Six lossless reference-control captures preserve the
// production loader/painter while selecting each scene. The covered cyan page
// remains visible for these exact capture-sample counts; the spread reflects
// the bank-specific synchronous resource load on the reference machine.
constexpr int UserBoardToCartoonFrames = 5;
constexpr double CartoonResourceLoadDelay = 0.015;
constexpr double CartoonScoreStartDelay = 0.010;
constexpr std::array<int, 6> WordCartoonCoveredCaptureSamples = {
    42, 44, 43, 41, 42, 42,
};
constexpr double AttractHallDuration = 450.0 / OriginalSceneTicksPerSecond;
constexpr double AttractLogoDuration = 450.0 / OriginalSceneTicksPerSecond;

bool isActiveBoardPage(const WordGamePage page) {
    return page == WordGamePage::Playing || page == WordGamePage::Attract;
}

constexpr std::array<std::string_view, 6> JoystickCalibrationPrompts = {
    "Center your joystick.",
    "Position your joystick: left",
    "Position your joystick: right",
    "Position your joystick: up",
    "Position your joystick: down",
    "Your joystick is ready for use.",
};

constexpr std::array<int, 12> EnemyLimits = {
    1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3
};
constexpr std::array<int, 12> EnemyArrivalTicks = {
    300, 300, 270, 270, 240, 210, 180, 150, 120, 90, 60, 30
};
constexpr std::array<std::array<int, 5>, 12> EnemyTypeWeights = {{
    {{100,  0,  0,  0,  0}},
    {{ 90,  0, 10,  0,  0}},
    {{ 80,  0, 10, 10,  0}},
    {{ 70, 10, 10, 10,  0}},
    {{ 60, 10, 10, 10, 10}},
    {{ 50, 15, 10, 15, 10}},
    {{ 45, 15, 15, 15, 10}},
    {{ 40, 15, 15, 15, 15}},
    {{ 35, 15, 20, 15, 15}},
    {{ 30, 15, 20, 20, 15}},
    {{ 25, 20, 20, 20, 15}},
    {{ 20, 20, 20, 20, 20}},
}};
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
    21.0 / OriginalSceneTicksPerSecond;
constexpr double NameCursorBlinkDuration = 16.0 / OriginalSceneTicksPerSecond;

constexpr std::array<std::string_view, 5> FailureExclamations = {
    "Yikes", "Oops", "Aargh", "Oh, Oh", "Rats"
};
constexpr std::array<std::string_view, 5> EnemySpecies = {
    "normalus", "laborus", "timidus", "assistus", "smarticus"
};

constexpr std::array<int, 6> SceneGraphicIds = {2001, 2003, 2005, 2007, 2009, 2011};

constexpr std::size_t InformationPageBytes = 0x2c0;
constexpr std::size_t InformationTextBytes = 0x2a8;
constexpr int InformationLineCount = 17;
constexpr int InformationLineBytes = 40;
constexpr int InformationSpriteCount = 6;
constexpr std::array<int, 2> InformationPageCounts = {6, 5};
constexpr std::array<std::uint32_t, InformationSpriteCount> InformationSpriteSheets = {
    1006, 1007, 1008, 1009, 1010, 1011
};

std::uint16_t readInformationU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::int16_t readInformationI16(const std::uint8_t* data) {
    return static_cast<std::int16_t>(readInformationU16(data));
}

const std::uint8_t* informationPage(const BlobView data, const int group, const int page) {
    if (!data || data.size < 6 || readInformationU16(data.data) != 2 || group < 0 ||
        group >= static_cast<int>(InformationPageCounts.size()) || page < 0 ||
        page >= InformationPageCounts[static_cast<std::size_t>(group)]) {
        return nullptr;
    }
    const std::size_t groupOffset = readInformationU16(data.data + 2 + group * 2);
    const std::size_t pageOffset = groupOffset + static_cast<std::size_t>(page) * InformationPageBytes;
    if (pageOffset + InformationPageBytes > data.size) return nullptr;
    return data.data + pageOffset;
}

int textWidth(const GemFont& font, const std::string_view text) {
    int width = 0;
    for (const unsigned char character : text) width += font.glyphWidth(character);
    return width;
}

} // namespace

WordGame::WordGame(const GraphicsMode graphicsMode, const std::uint16_t randomSeed)
    : assets_(graphicsMode, GameAssetSet::WordMunchers),
      random_(randomSeed),
      words_(loadEmbeddedResource(IDR_WM_WLIST)),
      config_(loadEmbeddedResource(IDR_WM_CFG)),
      core_(words_, random_) {
    wordDifficulty_ = std::clamp<int>(config_.difficulty(), 0, 7);
    for (std::size_t index = 0; index < selectedSounds_.size(); ++index) {
        selectedSounds_[index] = config_.selectedSounds()[index] != 0;
    }
    contentDraftDifficulty_ = wordDifficulty_;
    contentDraftSounds_ = selectedSounds_;
    joystickEnabled_ = config_.joystickEnabled();
    joystickCalibrated_ = (config_.joystickFlags() & 0x02u) != 0;
    joystickLeftThreshold_ = config_.joystickCalibration().horizontalLow;
    joystickRightThreshold_ = config_.joystickCalibration().horizontalHigh;
    joystickUpThreshold_ = config_.joystickCalibration().verticalLow;
    joystickDownThreshold_ = config_.joystickCalibration().verticalHigh;
    optionPassword_ = config_.password();
    optionHint_ = config_.hint();
    (void)core_.configure(config_);
    (void)hall_.load(config_);
#ifdef NUMBER_MUNCHERS_TESTING
    settingsPersistenceEnabled_ = false;
#endif
    loadSettings();
}

void WordGame::startGame(const int level, const bool preserveHallPalette) {
    // Word's non-Demo board setup at 0x0958F calls its resident GSND-0
    // wrapper, retiring logical channels without resetting the YM3812.
    attractOplPlayer_.stopAllChannels();
    attractMode_ = false;
    hallPaletteActive_ = preserveHallPalette;
    attractPostFeedbackBoard_ = false;
    attractTransitionTimer_ = 0.0;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    titleIdleTimer_ = 0.0;
    startGameSession(level, false);
}

void WordGame::startGameWithBoardPresentation(
    const int level, const bool preserveHallPalette) {
    captureBoardPresentationSource();
    startGame(level, preserveHallPalette);
    beginBoardPresentation();
}

void WordGame::startGameSession(const int level, const bool demo) {
    pointerCellQueue_.clear();
    moving_ = false;
    playerTerminalFrame_ = -1;
    munching_ = false;
    munchCellIndex_ = -1;
    lastResolution_ = {};
    feedbackTimer_ = 0.0;
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    feedbackMessage_.clear();
    postGameHall_ = false;
    pendingHallAdmission_ = false;
    hallHighlightName_.clear();
    hallHighlightScore_ = 0;
    nameEntryMessageType_ = 1;
    nameInput_.clear();
    deathAnimating_ = false;
    deathSequenceTicks_ = 0;
    playerRecovering_ = false;
    gameplayTickAccumulator_ = 0.0;
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeCells_.fill(false);
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    levelCompleteScene_ = SceneScript{};
    sceneGraphicId_ = 0;
    sceneAudioBank_ = 0;
    sceneTicks_ = 0;
    sceneTickAccumulator_ = 0.0;
    sceneEffectPlayer_.stop();
    attractOplPlayer_.stopEffect();
    if (core_.startAtLevel(std::clamp(level, 1, 20), demo)) {
        initializeBoardRuntime();
        page_ = demo ? WordGamePage::Attract : WordGamePage::Playing;
    }
}

void WordGame::startAttract() {
    attractMode_ = true;
    hallPaletteActive_ = false;
    attractPostFeedbackBoard_ = false;
    attractTransitionTimer_ = 0.0;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    attractActionTimer_ = AttractInitialActionDelay;
    // Event 0x90 is dispatched before the demo tier draw at 0x095b3.
    playAttractMusic();
    // Image 0x095c3 performs upper-exclusive random(0,9), stores that
    // zero-based pressure tier, and paints tier+1 as the visible level.
    const int level = random_.range(9) + 1;
    startGameSession(level, true);
}

void WordGame::stopAttract() {
    attractOplPlayer_.stopAllChannels();
    sceneEffectPlayer_.stop();
    attractMode_ = false;
    hallPaletteActive_ = false;
    attractPostFeedbackBoard_ = false;
    attractActionTimer_ = 0.0;
    attractTransitionTimer_ = 0.0;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    titleIdleTimer_ = 0.0;
    pointerCellQueue_.clear();
    moving_ = false;
    munching_ = false;
    deathAnimating_ = false;
    deathSequenceTicks_ = 0;
    playerRecovering_ = false;
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeCells_.fill(false);
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    feedbackMessage_.clear();
    lastResolution_ = {};
    page_ = WordGamePage::Title;
    menuSelection_ = 0;
}

void WordGame::beginAttractHallTransition() {
    pointerCellQueue_.clear();
    moving_ = false;
    moveTimer_ = 0.0;
    munching_ = false;
    munchTimer_ = 0.0;
    attractPostFeedbackBoard_ = true;
    attractHallWipeVariant_ = feedbackEnemyType_ >= 0
        ? AttractHallWipeVariant::Collision
        : AttractHallWipeVariant::Clean;
    attractHallTransitionPhase_ = attractHallWipeVariant_ ==
            AttractHallWipeVariant::Collision
        ? AttractHallTransitionPhase::CollisionTransient
        : AttractHallTransitionPhase::BoardHold;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    page_ = WordGamePage::Feedback;
    attractTransitionTimer_ = attractHallWipeVariant_ ==
            AttractHallWipeVariant::Collision
        ? AttractCaptureFrameDuration
        : AttractPostFeedbackBoardDuration;
}

void WordGame::beginAttractHall() {
    pointerCellQueue_.clear();
    moving_ = false;
    munching_ = false;
    deathAnimating_ = false;
    playerRecovering_ = false;
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeCells_.fill(false);
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    postGameHall_ = false;
    hallPaletteActive_ = true;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    attractPostFeedbackBoard_ = false;
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    feedbackMessage_.clear();
    page_ = WordGamePage::Hall;
    attractTransitionTimer_ = AttractHallDuration;
}

void WordGame::updateAttractHallTransition(const double seconds) {
    attractTransitionTimer_ -= seconds;
    if (attractHallTransitionPhase_ == AttractHallTransitionPhase::CollisionTransient &&
        attractTransitionTimer_ <= 0.0) {
        attractHallTransitionPhase_ = AttractHallTransitionPhase::BoardHold;
        attractTransitionTimer_ += AttractPostFeedbackBoardDuration;
    }
    if (attractHallTransitionPhase_ == AttractHallTransitionPhase::BoardHold &&
        attractTransitionTimer_ <= 0.0) {
        attractHallTransitionPhase_ = AttractHallTransitionPhase::Wipe;
        attractHallWipeFrame_ = 0;
        attractTransitionTimer_ += AttractCaptureFrameDuration;
    }
    while (attractHallTransitionPhase_ == AttractHallTransitionPhase::Wipe &&
           attractTransitionTimer_ <= 0.0) {
        ++attractHallWipeFrame_;
        const int frameCount = attractHallWipeVariant_ ==
                AttractHallWipeVariant::Collision
            ? AttractCollisionHallWipeFrames
            : AttractCleanHallWipeFrames;
        if (attractHallWipeFrame_ >= frameCount) {
            beginAttractHall();
            return;
        }
        attractTransitionTimer_ += AttractCaptureFrameDuration;
    }
}

void WordGame::beginAttractInterstitialTransition(
    const AttractInterstitialTransition transition) {
    attractInterstitialTransition_ = transition;
    attractInterstitialFrame_ = 0;
    attractTransitionTimer_ += AttractCaptureFrameDuration;
    updateAttractInterstitialTransition(0.0);
}

void WordGame::updateAttractInterstitialTransition(const double seconds) {
    attractTransitionTimer_ -= seconds;
    while (attractInterstitialTransition_ != AttractInterstitialTransition::None &&
           attractTransitionTimer_ <= 0.0) {
        ++attractInterstitialFrame_;
        const int frameCount = attractInterstitialTransition_ ==
                AttractInterstitialTransition::HallToLogo
            ? AttractHallToLogoFrames
            : attractInterstitialTransition_ ==
                    AttractInterstitialTransition::UserToCartoon
                ? UserBoardToCartoonFrames
                : AttractLogoToBoardFrames;
        const int pendingSceneBeforeAdvance = pendingCartoonScene_;
        if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::UserToCartoon &&
            attractInterstitialFrame_ == 2 && pendingCartoonScene_ >= 0) {
            // Word 0x114E0/0x114EB performs the 15 ms quiet wait and then the
            // synchronous bank-specific resource load while the page remains
            // covered. Install the VM at the end of that measured hold, but
            // leave its retained target black: the first 10 Hz scene callback
            // occurs only after the type-1 Wipe has opened.
            const int sceneIndex = pendingCartoonScene_;
            pendingCartoonScene_ = -1;
            if (!loadLevelCompleteScene(sceneIndex)) {
                finishLevelCompleteScene();
                return;
            }
        }
        if (attractInterstitialFrame_ >= frameCount) {
            const bool startingCartoonScore = attractInterstitialTransition_ ==
                AttractInterstitialTransition::UserToCartoon;
            if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::HallToLogo) {
                attractInterstitialTransition_ = AttractInterstitialTransition::None;
                attractInterstitialFrame_ = 0;
                page_ = WordGamePage::AttractLogo;
                attractTransitionTimer_ += AttractLogoDuration;
                // Main-screen action 1 at 0x11f6e performs the Hall Wipe,
                // installs the logo/interstitial, then submits ordinary
                // gameplay-bank streams 12 and 13 in that order.
                playGameplaySound(12);
                playGameplaySound(13);
            } else {
                attractInterstitialTransition_ = AttractInterstitialTransition::None;
                attractInterstitialFrame_ = 0;
                boardPresentationSourcePixels_.clear();
                if (startingCartoonScore) playCartoonScore();
            }
            return;
        }

        // The every-board presenter at 0x10a66 performs the same closing
        // type-2 / clear / opening type-1 / dynamic-paint sequence as Number.
        // Construct the next Demo board only when its first torn paint becomes
        // visible; user boards have already been initialized synchronously.
        if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::LogoToBoard &&
            attractInterstitialFrame_ == 7) {
            startNextAttractBoard();
        }
        attractTransitionTimer_ += attractInterstitialTransition_ ==
                    AttractInterstitialTransition::UserToCartoon &&
                    attractInterstitialFrame_ == 1 &&
                    pendingSceneBeforeAdvance >= 0 &&
                    pendingSceneBeforeAdvance < static_cast<int>(
                        WordCartoonCoveredCaptureSamples.size())
                ? std::max(
                      CartoonResourceLoadDelay,
                      (WordCartoonCoveredCaptureSamples[
                           static_cast<std::size_t>(pendingSceneBeforeAdvance)] - 1) *
                          AttractCaptureFrameDuration)
            : attractInterstitialTransition_ ==
                      AttractInterstitialTransition::UserToCartoon &&
                      attractInterstitialFrame_ == frameCount - 1
                ? CartoonScoreStartDelay
                : AttractCaptureFrameDuration;
    }
}

void WordGame::startNextAttractBoard() {
    attractPostFeedbackBoard_ = false;
    attractTransitionTimer_ = 0.0;
    attractActionTimer_ = AttractInitialActionDelay;
    const int level = random_.range(9) + 1;
    startGameSession(level, true);
    // startGameSession releases only channel 0. The continuously clocked
    // channel-1..8 score survives the Hall/logo/next-board cycle.
    if (!attractOplPlayer_.playing()) playAttractMusic();
}

void WordGame::captureBoardPresentationSource() {
    Renderer source(assets_.graphicsMode());
    render(source);
    boardPresentationSourcePixels_ = source.pixels();
}

void WordGame::beginBoardPresentation() {
    attractInterstitialTransition_ = AttractInterstitialTransition::UserToBoard;
    attractInterstitialFrame_ = 0;
    attractTransitionTimer_ = AttractCaptureFrameDuration;
}

void WordGame::beginCartoonPresentation() {
    attractInterstitialTransition_ = AttractInterstitialTransition::UserToCartoon;
    attractInterstitialFrame_ = 0;
    attractTransitionTimer_ = AttractCaptureFrameDuration;
}

bool WordGame::userPresentationActive() const {
    return attractInterstitialTransition_ ==
               AttractInterstitialTransition::UserToBoard ||
           attractInterstitialTransition_ ==
               AttractInterstitialTransition::UserToCartoon;
}

void WordGame::initializeBoardRuntime() {
    ++boardGenerationSerial_;
    gameplayTickAccumulator_ = 0.0;
    initializeSafeZones();
    initializePlayer();
    initializeEnemySlots();
}

void WordGame::initializePlayer() {
    // Word image 0x096e1 makes the upper-exclusive column draw first, then
    // the row draw, and deliberately does not reject an active safe square.
    playerColumn_ = 1 + random_.range(BoardColumns - 2);
    playerRow_ = 1 + random_.range(BoardRows - 2);
    (void)core_.clearCellWithoutScore(
        static_cast<std::size_t>(playerRow_ * BoardColumns + playerColumn_));
}

void WordGame::update(double seconds) {
    seconds = std::max(0.0, seconds);
    // The DOS save wrapper owns a synchronous "Press any key" alert.  Freeze
    // the controller while the native asynchronous equivalent is visible.
    if (configurationWriteError_) return;
    if (exitPending_) {
        exitDelayTimer_ -= seconds;
        if (exitDelayTimer_ <= 0.0) {
            exitPending_ = false;
            exitDelayTimer_ = 0.0;
            shouldQuit_ = true;
        }
        return;
    }
    if (page_ == WordGamePage::StartupVersion) {
        startupVersionTickAccumulator_ += seconds * OriginalSceneTicksPerSecond;
        if (startupVersionTickAccumulator_ + JobTimerEpsilon >=
            StartupVersionWaitTicks) {
            startupVersionTickAccumulator_ = StartupVersionWaitTicks;
            page_ = WordGamePage::StartupSplash;
        }
        return;
    }
    if (attractMode_ &&
        attractHallTransitionPhase_ != AttractHallTransitionPhase::None) {
        updateAttractHallTransition(seconds);
        updateJoystickInput(seconds);
        return;
    }
    if (attractInterstitialTransition_ !=
            AttractInterstitialTransition::None) {
        updateAttractInterstitialTransition(seconds);
        updateJoystickInput(seconds);
        return;
    }
    if (page_ == WordGamePage::NameEntry) {
        nameCursorBlinkTimer_ += seconds;
        while (nameCursorBlinkTimer_ + JobTimerEpsilon >= NameCursorBlinkDuration) {
            nameCursorBlinkTimer_ -= NameCursorBlinkDuration;
            nameCursorVisible_ = !nameCursorVisible_;
        }
        return;
    }
    const bool activeBoardAtFrameStart = isActiveBoardPage(page_);
    const bool titleAtFrameStart = page_ == WordGamePage::Title;
    const bool hallAtFrameStart = page_ == WordGamePage::Hall;
    const bool logoAtFrameStart = page_ == WordGamePage::AttractLogo;
    const bool feedbackAtFrameStart = page_ == WordGamePage::Feedback;
    const bool levelCompleteAtFrameStart = page_ == WordGamePage::LevelComplete;
    const bool deathAtFrameStart = page_ == WordGamePage::Feedback && deathAnimating_;
    double feedbackTransitionSeconds = seconds;
    if (isActiveBoardPage(page_) && activeBoardAtFrameStart) {
        constexpr double tickSeconds = 1.0 / OriginalSceneTicksPerSecond;
        double remainingTicks = seconds * OriginalSceneTicksPerSecond;
        while (remainingTicks > JobTimerEpsilon && isActiveBoardPage(page_) &&
               attractInterstitialTransition_ ==
                   AttractInterstitialTransition::None) {
            const double ticksUntilDispatch = std::max(0.0, 1.0 - gameplayTickAccumulator_);
            if (remainingTicks + JobTimerEpsilon < ticksUntilDispatch) {
                gameplayTickAccumulator_ += remainingTicks;
                remainingTicks = 0.0;
                break;
            }
            remainingTicks = std::max(0.0, remainingTicks - ticksUntilDispatch);
            // Charge the public dispatch before any callback can construct a
            // board and reset the accumulator.
            gameplayTickAccumulator_ = 0.0;
            const bool attractActionMayStartOnThisDispatch =
                attractMode_ &&
                attractActionTimer_ - tickSeconds <= JobTimerEpsilon &&
                !moving_ && !munching_ && !deathAnimating_ &&
                isActiveBoardPage(page_);
            const std::vector<std::uint32_t> beforeAttractDispatchPixels =
                attractActionMayStartOnThisDispatch &&
                        presentationFrames_.empty() &&
                        attractPlayerCallbackHoldTicks_ == 0 &&
                        attractSafeZoneEnemyHoldTicks_ == 0 &&
                        attractPlayerTerminalEnemyHoldTicks_ == 0 &&
                        cannibalPlayerPresentationHoldPixels_.empty()
                    ? capturePresentationFrame()
                    : std::vector<std::uint32_t>{};
            if (attractSafeZoneEnemyHoldTicks_ > 0 &&
                --attractSafeZoneEnemyHoldTicks_ == 0) {
                attractSafeZoneEnemyHoldOldPixels_.clear();
                attractSafeZoneEnemyHoldNewPixels_.clear();
            }
            if (attractPlayerTerminalEnemyHoldTicks_ > 0 &&
                --attractPlayerTerminalEnemyHoldTicks_ == 0) {
                attractPlayerTerminalEnemyHoldPixels_.clear();
            }
            if (attractPlayerCallbackHoldTicks_ > 0 &&
                --attractPlayerCallbackHoldTicks_ == 0) {
                attractPlayerCallbackHoldPixels_.clear();
            }

            // Word 0x09895 installs selector-6 safe-zone records before
            // player job 4 at 0x096e1; 0x09abc then appends selector-1
            // Troggles. Fixed selectors 7-10 and optional Demo selector 5
            // follow, so this is the original physical scheduler-slot order.
            const bool safeZonePainted = attractMode_ && std::any_of(
                safeZoneJobs_.begin(),
                safeZoneJobs_.begin() + safeZoneJobCount_,
                [&](const SafeZoneJob& job) {
                    return job.timer - tickSeconds <= JobTimerEpsilon;
                });
            const std::vector<std::uint32_t> beforeSafeZonePixels =
                safeZonePainted ? capturePresentationFrame()
                                : std::vector<std::uint32_t>{};
            if (!updateSafeZones(tickSeconds)) continue;
            const std::vector<std::uint32_t> afterSafeZonePixels =
                safeZonePainted ? capturePresentationFrame()
                                : std::vector<std::uint32_t>{};
            const bool clearingPlayerTerminal =
                !moving_ && !munching_ && playerTerminalFrame_ >= 0;
            const int clearingPlayerTerminalFrame =
                clearingPlayerTerminal ? playerTerminalFrame_ : -1;
            const bool playerMovementCallback = moving_;
            const auto cannibalBeforeTroggles = std::find_if(
                enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
                    return enemy.cannibalizing;
                });
            const bool cannibalBiteActiveBeforeTroggles =
                cannibalBeforeTroggles != enemies_.end();
            const int cannibalSlotBeforeTroggles =
                cannibalBiteActiveBeforeTroggles
                    ? cannibalBeforeTroggles->slot : -1;
            const int cannibalRowBeforeTroggles =
                cannibalBiteActiveBeforeTroggles
                    ? cannibalBeforeTroggles->row : -1;
            const int cannibalColumnBeforeTroggles =
                cannibalBiteActiveBeforeTroggles
                    ? cannibalBeforeTroggles->column : -1;
            const std::vector<std::uint32_t> beforeCannibalJobsPixels =
                cannibalBiteActiveBeforeTroggles && attractMode_
                    ? capturePresentationFrame()
                    : std::vector<std::uint32_t>{};
            const bool playerMayRepaint =
                moving_ || munching_ || clearingPlayerTerminal;
            const std::vector<std::uint32_t> beforePlayerPixels =
                playerMayRepaint ? capturePresentationFrame()
                                 : std::vector<std::uint32_t>{};
            std::array<bool, std::tuple_size_v<decltype(enemySlots_)>> enemyWasMoving{};
            for (const Enemy& enemy : enemies_) {
                if (enemy.slot >= 0 &&
                    static_cast<std::size_t>(enemy.slot) < enemyWasMoving.size()) {
                    enemyWasMoving[static_cast<std::size_t>(enemy.slot)] = enemy.moving;
                }
            }
            bool munchTerminalCallback = false;
            int preTerminalMunchFrame = -1;
            if (moving_) {
                updatePlayerMovementTick(tickSeconds);
            } else if (munching_) {
                munchTimer_ -= tickSeconds;
                if (munchTimer_ <= JobTimerEpsilon) {
                    munchTerminalCallback = true;
                    preTerminalMunchFrame =
                        munchFrameAtTick(MunchAnimationTicks - 1);
                    const std::uint64_t boardBeforeTerminal = boardGenerationSerial_;
                    resolveMunch();
                    if (boardGenerationSerial_ != boardBeforeTerminal) continue;
                    if (lastResolution_.kind == WordMunchKind::Correct &&
                        isActiveBoardPage(page_)) {
                        playerTerminalFrame_ = munchFrameAtTick(MunchAnimationTicks);
                    }
                }
            } else {
                playerTerminalFrame_ = -1;
                processPointerCellQueue();
            }
            const std::vector<std::uint32_t> afterPlayerPixels =
                !beforePlayerPixels.empty() ? capturePresentationFrame()
                                            : std::vector<std::uint32_t>{};
            const std::vector<std::uint32_t> afterPlayerCallbackPixels =
                playerMovementCallback && !afterPlayerPixels.empty() &&
                        afterPlayerPixels != beforePlayerPixels
                    ? capturePlayerCallbackFrame()
                    : std::vector<std::uint32_t>{};
            const bool playerCallbackRepairsResidentEnemy =
                !afterPlayerCallbackPixels.empty() &&
                terminalResidentEnemyForPlayerCallback() != nullptr;
            const bool collisionStarted = page_ == WordGamePage::Feedback && deathAnimating_;
            if (!isActiveBoardPage(page_) && !collisionStarted) break;
            if (!updateEnemies(tickSeconds)) continue;
            const bool enemyMoveStarted = std::any_of(
                enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                    return enemy.moving && enemy.slot >= 0 &&
                           static_cast<std::size_t>(enemy.slot) < enemyWasMoving.size() &&
                           !enemyWasMoving[static_cast<std::size_t>(enemy.slot)];
                });
            const std::vector<std::uint32_t> afterTrogglesForPlayerCallback =
                (!afterPlayerCallbackPixels.empty() ||
                 !cannibalPlayerResidentSurfacePixels_.empty())
                    ? capturePresentationFrame()
                    : std::vector<std::uint32_t>{};
            const bool solitaryEnemyDepartsAfterArrival =
                enemies_.size() == 1 && !enemies_.front().moving &&
                !enemies_.front().entering && !enemies_.front().exiting &&
                static_cast<int>(std::llround(
                    enemies_.front().moveTimer * OriginalSceneTicksPerSecond)) == 1;
            const bool oneOfTwoOrdinaryEnemiesMoving =
                enemies_.size() == 2 &&
                std::count_if(enemies_.begin(), enemies_.end(),
                              [](const Enemy& enemy) {
                                  return enemy.moving;
                              }) == 1 &&
                std::none_of(enemies_.begin(), enemies_.end(),
                             [](const Enemy& enemy) {
                                 return enemy.entering || enemy.exiting ||
                                        enemy.cannibalizing;
                             });
            const bool downwardArrivalBetweenExitAndEntry =
                enemies_.size() == 2 &&
                std::count_if(enemies_.begin(), enemies_.end(),
                              [](const Enemy& enemy) {
                                  return enemy.moving && enemy.exiting;
                              }) == 1 &&
                std::count_if(enemies_.begin(), enemies_.end(),
                              [](const Enemy& enemy) {
                                  return !enemy.moving && enemy.entering;
                              }) == 1;
            if (attractMode_ && playerMovementCallback && !moving_ &&
                !beforePlayerPixels.empty() &&
                ((playerTerminalFrame_ == 4 &&
                  (solitaryEnemyDepartsAfterArrival ||
                   oneOfTwoOrdinaryEnemiesMoving)) ||
                 (playerTerminalFrame_ == 8 &&
                  downwardArrivalBetweenExitAndEntry))) {
                // Runs 394/396 and 496/497 keep the prior resident page
                // through a rightward player arrival when a later ordinary
                // Troggle callback owns the visible transition. The complete
                // terminal-player/current-enemy recompositions (old native
                // pages 335 and 421) are never presented.
                // Runs 561/562 expose the corresponding downward arrival
                // while an exiting actor and a waiting entering actor straddle
                // selector 4; its complete terminal composite is likewise not
                // a presented DOS page.
                attractPlayerCallbackHoldPixels_ = beforePlayerPixels;
                attractPlayerCallbackHoldTicks_ = 1;
            }
            bool residentCannibalPlayerHandled = false;
            if (!cannibalPlayerResidentSurfacePixels_.empty() &&
                !afterTrogglesForPlayerCallback.empty()) {
                std::vector<std::uint32_t> residentAfterPlayer =
                    cannibalPlayerResidentSurfacePixels_;
                const bool playerDirtyCallback =
                    (playerMovementCallback || clearingPlayerTerminal) &&
                    !afterPlayerPixels.empty();
                if (playerDirtyCallback) {
                    copyBoardCellPixels(residentAfterPlayer, afterPlayerPixels,
                                        moveFromRow_, moveFromColumn_);
                    copyBoardCellPixels(residentAfterPlayer, afterPlayerPixels,
                                        moveToRow_, moveToColumn_);
                }

                std::vector<std::uint32_t> residentAfterBiter =
                    residentAfterPlayer;
                if (cannibalBiteActiveBeforeTroggles) {
                    copyBoardCellPixels(residentAfterBiter,
                                        afterTrogglesForPlayerCallback,
                                        cannibalRowBeforeTroggles,
                                        cannibalColumnBeforeTroggles);
                }
                const auto cannibalAfterTroggles = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        return enemy.slot == cannibalSlotBeforeTroggles &&
                               enemy.cannibalizing;
                    });
                const bool cannibalTerminalCallback =
                    cannibalBiteActiveBeforeTroggles &&
                    cannibalAfterTroggles == enemies_.end();
                const int cannibalTicksRemaining =
                    cannibalAfterTroggles == enemies_.end()
                        ? 0
                        : std::max(0, static_cast<int>(std::llround(
                              cannibalAfterTroggles->cannibalTimer *
                              OriginalSceneTicksPerSecond)));
                const int movementPhaseAfterCallback =
                    moving_ ? playerMovementPhase() : -1;

                if (cannibalTerminalCallback && playerDirtyCallback &&
                    cannibalPlayerRetainedPenultimatePaint_) {
                    int minimumX = Renderer::Width;
                    int minimumY = Renderer::Height;
                    int maximumX = -1;
                    int maximumY = -1;
                    for (int y = BoardTop; y < BoardBottom; ++y) {
                        for (int x = BoardLeft; x < BoardRight; ++x) {
                            const std::size_t pixel = static_cast<std::size_t>(
                                y * Renderer::Width + x);
                            if (residentAfterPlayer[pixel] ==
                                cannibalPlayerResidentSurfacePixels_[pixel]) {
                                continue;
                            }
                            minimumX = std::min(minimumX, x);
                            minimumY = std::min(minimumY, y);
                            maximumX = std::max(maximumX, x);
                            maximumY = std::max(maximumY, y);
                        }
                    }
                    if (maximumX >= minimumX && maximumY >= minimumY) {
                        int stripLeft = minimumX;
                        int stripTop = minimumY;
                        int stripRight = maximumX;
                        int stripBottom = maximumY;
                        if (moveDirection_ == 0) {
                            stripBottom = std::min(
                                maximumY,
                                minimumY + BoardCellHeight / 5 - 1);
                        } else if (moveDirection_ == 1) {
                            stripLeft = std::max(
                                minimumX,
                                maximumX - BoardCellWidth / 6 + 1);
                        } else if (moveDirection_ == 2) {
                            stripTop = std::max(
                                minimumY,
                                maximumY - BoardCellHeight / 5 + 1);
                        } else {
                            stripRight = std::min(
                                maximumX,
                                minimumX + BoardCellWidth / 6 - 1);
                        }
                        for (int y = stripTop; y <= stripBottom; ++y) {
                            for (int x = stripLeft; x <= stripRight; ++x) {
                                const std::size_t pixel =
                                    static_cast<std::size_t>(
                                        y * Renderer::Width + x);
                                residentAfterPlayer[pixel] =
                                    cannibalPlayerResidentSurfacePixels_[pixel];
                            }
                        }
                        residentAfterBiter = residentAfterPlayer;
                        copyBoardCellPixels(residentAfterBiter,
                                            afterTrogglesForPlayerCallback,
                                            cannibalRowBeforeTroggles,
                                            cannibalColumnBeforeTroggles);
                    }
                    cannibalPlayerRetainedPenultimatePaint_ = false;
                }

                const auto queueResidentPage = [&](
                    const std::vector<std::uint32_t>& pixels) {
                    if (pixels.empty()) return;
                    enqueuePresentationFrame(pixels);
                    cannibalPlayerPresentationHoldPixels_ = pixels;
                };

                if (playerDirtyCallback) {
                    if (!cannibalBiteActiveBeforeTroggles) {
                        cannibalPlayerResidentSurfacePixels_.clear();
                        cannibalPlayerPresentationHoldPixels_.clear();
                        cannibalPlayerRetainedPenultimatePaint_ = false;
                    } else if (cannibalTerminalCallback) {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterPlayer);
                        const std::vector<std::uint32_t>& terminalPage =
                            !afterPlayerCallbackPixels.empty()
                                ? afterPlayerCallbackPixels
                                : residentAfterBiter;
                        queueResidentPage(terminalPage);
                        cannibalPlayerResidentSurfacePixels_ = terminalPage;
                    } else if (playerMovementCallback && !moving_) {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterBiter);
                        queueResidentPage(residentAfterPlayer);
                        cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    } else if (clearingPlayerTerminal) {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterPlayer);
                        queueResidentPage(residentAfterBiter);
                        cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    } else if (cannibalTicksRemaining == 18 ||
                               cannibalTicksRemaining == 3) {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterBiter);
                        queueResidentPage(residentAfterPlayer);
                        cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    } else {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterPlayer);
                        if (playerMovementCallback && moving_ &&
                            movementPhaseAfterCallback == 3 &&
                            cannibalTicksRemaining == 1) {
                            cannibalPlayerResidentSurfacePixels_ =
                                residentAfterPlayer;
                            cannibalPlayerRetainedPenultimatePaint_ = true;
                        } else {
                            cannibalPlayerResidentSurfacePixels_ =
                                residentAfterBiter;
                            cannibalPlayerRetainedPenultimatePaint_ = false;
                        }
                    }
                } else if (cannibalBiteActiveBeforeTroggles) {
                    cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    cannibalPlayerPresentationHoldPixels_ = residentAfterBiter;
                }
            }
            if (!residentCannibalPlayerHandled &&
                cannibalBiteActiveBeforeTroggles &&
                !afterPlayerCallbackPixels.empty() &&
                afterPlayerCallbackPixels != afterTrogglesForPlayerCallback) {
                // Player record 4 precedes every Troggle slot. Preserve the
                // callback-local Muncher paint over the previous cannibal pose
                // before the later biter callback advances on this tick.
                enqueuePresentationFrame(afterPlayerCallbackPixels);
            } else if (!residentCannibalPlayerHandled && munchTerminalCallback &&
                       preTerminalMunchFrame >= 0) {
                const std::vector<std::uint32_t> afterTroggles = capturePresentationFrame();
                const int terminalFrame = playerTerminalFrame_;
                playerTerminalFrame_ = preTerminalMunchFrame;
                const std::vector<std::uint32_t> retainedChewAfterTroggles =
                    capturePresentationFrame();
                playerTerminalFrame_ = terminalFrame;
                const bool laterTroggleChangedSurface =
                    !afterPlayerPixels.empty() &&
                    afterTroggles != afterPlayerPixels;
                if (retainedChewAfterTroggles != afterTroggles &&
                    (!attractMode_ || !laterTroggleChangedSurface)) {
                    // When a later selector-1 callback changes the same
                    // dispatch, DOS never presents a synthetic combination of
                    // that new Troggle state and selector 4's retained open
                    // chew. Source runs 146 -> 147 advance directly; old
                    // native page 115 inserted the unsupported combination.
                    enqueuePresentationFrame(retainedChewAfterTroggles);
                }
            } else if (!residentCannibalPlayerHandled &&
                       !afterPlayerCallbackPixels.empty() &&
                       afterTrogglesForPlayerCallback == afterPlayerPixels &&
                       afterPlayerCallbackPixels != afterPlayerPixels) {
                // Player record 4 writes over the resident Troggle surface.
                // Preserve that callback result only when no later selector-1
                // record changed the logical frame on the same dispatch.
                enqueuePresentationFrame(afterPlayerCallbackPixels);
                if (playerCallbackRepairsResidentEnemy) {
                    // Source run 351 holds this exact repaired page for both
                    // captured refreshes. Keep it resident until the next
                    // scheduler dispatch instead of falling back for one
                    // refresh to a synthetic recomposition without the label
                    // and rule repair.
                    attractPlayerCallbackHoldPixels_ = afterPlayerCallbackPixels;
                    attractPlayerCallbackHoldTicks_ = 1;
                }
            } else if (!residentCannibalPlayerHandled && enemyMoveStarted &&
                       clearingPlayerTerminal &&
                       !afterPlayerPixels.empty()) {
                const std::vector<std::uint32_t> afterTroggles =
                    capturePresentationFrame();
                if (afterPlayerPixels != afterTroggles) {
                    const bool solitaryRightTerminalDeparture =
                        attractMode_ && clearingPlayerTerminalFrame == 4 &&
                        enemyMoveStarted && enemies_.size() == 1;
                    if (!solitaryRightTerminalDeparture) {
                        enqueuePresentationFrame(afterPlayerPixels);
                    }
                    if (attractMode_ && clearingPlayerTerminalFrame == 2) {
                        // Source run 184 holds the complete state-4 player
                        // callback for both refreshes when a later selector-1
                        // record starts a Troggle move on the same dispatch.
                        // Keep that exact callback page resident until the
                        // next public tick; recomposing old player pixels over
                        // the new Troggle state produces a frame DOS never
                        // displays (old native page 151).
                        attractPlayerTerminalEnemyHoldPixels_ =
                            afterPlayerPixels;
                        attractPlayerTerminalEnemyHoldTicks_ = 1;
                    }
                }
            } else if (!residentCannibalPlayerHandled && enemyMoveStarted &&
                       !beforePlayerPixels.empty() &&
                       afterPlayerPixels != beforePlayerPixels) {
                const std::vector<std::uint32_t> afterTroggles = capturePresentationFrame();
                if (afterTroggles != afterPlayerPixels) {
                    std::vector<std::uint32_t> retainedPlayerAfterTroggles =
                        beforePlayerPixels;
                    for (std::size_t pixel = 0; pixel < afterTroggles.size(); ++pixel) {
                        if (afterTroggles[pixel] != afterPlayerPixels[pixel]) {
                            retainedPlayerAfterTroggles[pixel] = afterTroggles[pixel];
                        }
                    }
                    if (retainedPlayerAfterTroggles != afterTroggles) {
                        enqueuePresentationFrame(retainedPlayerAfterTroggles);
                    }
                }
            }
            if (attractMode_ && clearingPlayerTerminal &&
                attractPlayerTerminalEnemyHoldTicks_ == 0 &&
                !beforePlayerPixels.empty() &&
                !afterPlayerPixels.empty()) {
                const std::vector<std::uint32_t> afterTroggles =
                    capturePresentationFrame();
                if (afterTroggles != afterPlayerPixels) {
                    std::vector<std::uint32_t> retainedTerminal = afterTroggles;
                    for (std::size_t pixel = 0; pixel < retainedTerminal.size(); ++pixel) {
                        // Restore only pixels owned by the earlier state-4 player
                        // callback and untouched by a later Troggle painter.
                        if (beforePlayerPixels[pixel] != afterPlayerPixels[pixel] &&
                            afterTroggles[pixel] == afterPlayerPixels[pixel]) {
                            retainedTerminal[pixel] = beforePlayerPixels[pixel];
                        }
                    }
                    if (retainedTerminal != afterTroggles) {
                        // Native page 355 versus source run 419 isolates this
                        // surface: 71 changed pixels are all in the top-left
                        // Muncher record while both enemy poses are already current.
                        attractPlayerTerminalEnemyHoldPixels_ =
                            std::move(retainedTerminal);
                        attractPlayerTerminalEnemyHoldTicks_ = 1;
                    }
                }
            }
            // DS:13A2 is appended after the safe-zone and Troggle records, so
            // its PRNG/input callback is the last recurring gameplay job.
            if (attractMode_ &&
                (isActiveBoardPage(page_) ||
                 (page_ == WordGamePage::Feedback && deathAnimating_))) {
                attractActionTimer_ -= tickSeconds;
                const bool demoMayStartPlayerAction =
                    attractActionTimer_ <= JobTimerEpsilon && !moving_ && !munching_ &&
                    !deathAnimating_ && isActiveBoardPage(page_);
                const std::vector<std::uint32_t> beforeDemoPixels =
                    demoMayStartPlayerAction ? capturePresentationFrame()
                                             : std::vector<std::uint32_t>{};
                const bool playerWasMovingBeforeDemo = moving_;
                const bool playerWasMunchingBeforeDemo = munching_;
                updateAttractPlayer();
                if (!playerWasMovingBeforeDemo && moving_ &&
                    !beforeDemoPixels.empty()) {
                    if (cannibalBiteActiveBeforeTroggles &&
                        !beforeCannibalJobsPixels.empty()) {
                        attractPlayerPhaseZeroHoldPixels_ =
                            beforeCannibalJobsPixels;
                        cannibalPlayerPresentationHoldPixels_ =
                            beforeCannibalJobsPixels;
                        cannibalPlayerResidentSurfacePixels_ = beforeDemoPixels;
                        cannibalPlayerRetainedPenultimatePaint_ = false;
                    } else {
                        const bool dualEnteringTroggleCallbacks =
                            enemies_.size() == 2 &&
                            std::all_of(
                                enemies_.begin(), enemies_.end(),
                                [](const Enemy& enemy) {
                                    return enemy.entering && enemy.moving;
                                });
                        // In the captured dual-entry interval, selector 5's
                        // movement-start callback installs state zero without
                        // repainting the resident page. Source run 121 remains
                        // visible while both earlier selector-1 callbacks
                        // update the nonresident surface; capturing after them
                        // invented old native page 93. Other phase-zero starts
                        // retain their already source-exact callback surface.
                        attractPlayerPhaseZeroHoldPixels_ =
                            dualEnteringTroggleCallbacks &&
                                    !beforeAttractDispatchPixels.empty()
                                ? beforeAttractDispatchPixels
                                : beforeDemoPixels;
                    }
                }
                if (!playerWasMunchingBeforeDemo && munching_ && safeZonePainted &&
                    !beforeSafeZonePixels.empty() && !afterSafeZonePixels.empty()) {
                    // On source run 139, selector 6 has logically removed the
                    // row-1/column-2 safe zone before selector 5 starts the
                    // Muncher's chew, but the first chew page still owns the
                    // pre-safe outline. It differs from old native page 109 at
                    // exactly those 68 white pixels. Apply only the safe-job
                    // delta to the final post-Troggle/post-Demo framebuffer so
                    // unrelated callbacks remain current.
                    attractMunchSafeZoneHoldPixels_ = capturePresentationFrame();
                    for (std::size_t pixel = 0;
                         pixel < attractMunchSafeZoneHoldPixels_.size(); ++pixel) {
                        if (beforeSafeZonePixels[pixel] != afterSafeZonePixels[pixel]) {
                            attractMunchSafeZoneHoldPixels_[pixel] =
                                beforeSafeZonePixels[pixel];
                        }
                    }
                }
            }
            if (safeZonePainted && !beforeSafeZonePixels.empty() &&
                !afterSafeZonePixels.empty() &&
                beforeSafeZonePixels != afterSafeZonePixels &&
                !moving_ && !munching_ &&
                std::any_of(enemies_.begin(), enemies_.end(),
                            [](const Enemy& enemy) { return enemy.moving; })) {
                // Two independent seeded intervals expose the same three-tick
                // resident-page delay in opposite directions. Runs 422-424
                // retain a just-removed row-2/column-3 outline; run 501 keeps a
                // newly activated row-1/column-1 outline absent. Current actor
                // poses continue to advance, so retain only the selector-6
                // delta rather than freezing the whole framebuffer.
                attractSafeZoneEnemyHoldOldPixels_ = beforeSafeZonePixels;
                attractSafeZoneEnemyHoldNewPixels_ = afterSafeZonePixels;
                attractSafeZoneEnemyHoldTicks_ = 3;
            }
            if (collisionStarted ||
                (page_ == WordGamePage::Feedback && deathAnimating_)) break;
        }
        if (userPresentationActive() && remainingTicks > 0.0) {
            updateAttractInterstitialTransition(remainingTicks * tickSeconds);
        } else if (page_ == WordGamePage::Feedback && deathAnimating_ &&
                   remainingTicks > 0.0) {
            gameplayTickAccumulator_ += remainingTicks;
        }
    } else if (page_ == WordGamePage::Feedback && deathAnimating_ && deathAtFrameStart) {
        updateDeathAnimation(seconds);
        // A collision terminal can occur partway through this presentation
        // update. The unconsumed scheduler fraction belongs to the newly
        // installed 150-tick feedback hold, not to the bite job that just
        // ended. This is the same shared-driver boundary used by Number.
        feedbackTransitionSeconds = deathAnimating_
            ? 0.0 : gameplayTickAccumulator_ / OriginalSceneTicksPerSecond;
        if (!deathAnimating_) gameplayTickAccumulator_ = 0.0;
    }

    if (attractMode_ && page_ == WordGamePage::Feedback && !deathAnimating_ &&
        feedbackAtFrameStart) {
        attractTransitionTimer_ -= feedbackTransitionSeconds;
        if (attractTransitionTimer_ <= JobTimerEpsilon) {
            beginAttractHallTransition();
        }
    }

    if (attractMode_ && hallAtFrameStart && page_ == WordGamePage::Hall) {
        attractTransitionTimer_ -= seconds;
        if (attractTransitionTimer_ <= JobTimerEpsilon) {
            beginAttractInterstitialTransition(
                AttractInterstitialTransition::HallToLogo);
        }
    } else if (attractMode_ && logoAtFrameStart && page_ == WordGamePage::AttractLogo) {
        attractTransitionTimer_ -= seconds;
        if (attractTransitionTimer_ <= JobTimerEpsilon) {
            beginAttractInterstitialTransition(
                AttractInterstitialTransition::LogoToBoard);
        }
    } else if (!attractMode_ && titleAtFrameStart && page_ == WordGamePage::Title) {
        titleIdleTimer_ += seconds;
        if (titleIdleTimer_ >= AttractTitleIdleDuration) startAttract();
    }
    // A scene installed by resolveMunch() has already received the setup
    // dispatch. Do not also charge it for elapsed time that preceded the
    // munch callback within this same presentation frame.
    if (page_ == WordGamePage::LevelComplete && levelCompleteAtFrameStart) {
        if (!levelCompleteScene_.valid() || levelCompleteScene_.finished()) {
            finishLevelCompleteScene();
        } else {
            sceneTickAccumulator_ += seconds * OriginalCartoonTicksPerSecond;
            while (sceneTickAccumulator_ >= 1.0 && levelCompleteScene_.valid() &&
                   !levelCompleteScene_.finished()) {
                advanceLevelCompleteScene();
                ++sceneTicks_;
                for (const std::uint16_t event : levelCompleteScene_.callbackEvents()) {
                    playSceneEvent(event);
                }
                sceneTickAccumulator_ -= 1.0;
            }
            if (!levelCompleteScene_.valid() || levelCompleteScene_.finished()) {
                finishLevelCompleteScene();
            }
        }
    }

    // Poll-derived joystick events enter through the ordinary key routes only
    // after this frame's animation/scheduler work.
    updateJoystickInput(seconds);
}

void WordGame::updateAttractPlayer() {
    if (attractActionTimer_ > JobTimerEpsilon) return;

    const bool currentCellOwnedByEnemy =
        enemyOwnsAttractPlayerCell(playerRow_, playerColumn_);
    const bool acceptsInjectedKey = isActiveBoardPage(page_) &&
                                    !currentCellOwnedByEnemy && !moving_ && !munching_ &&
                                    !deathAnimating_;

    // Exact selector-5 order at image 0x0918e: reload first, inspect the live
    // signed current cell, then choose/clockwise-rotate a direction.
    attractActionTimer_ = static_cast<double>(random_.range(30) + 15) /
                          OriginalSceneTicksPerSecond;

    const int currentIndex = playerRow_ * BoardColumns + playerColumn_;
    const bool currentHasValue = currentIndex >= 0 && currentIndex < BoardCellCount &&
        !core_.eaten(static_cast<std::size_t>(currentIndex)) &&
        core_.board().cells[static_cast<std::size_t>(currentIndex)].signedSourceIndex != 0;
    if (currentHasValue) {
        const int signedValue =
            core_.board().cells[static_cast<std::size_t>(currentIndex)].signedSourceIndex;
        if (signedValue > 0 || (signedValue < 0 && random_.range(10) == 0)) {
            if (acceptsInjectedKey) beginMunch();
            return;
        }
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
            const int index = row * BoardColumns + column;
            const WordBoardCell& candidate =
                core_.board().cells[static_cast<std::size_t>(index)];
            if (!core_.eaten(static_cast<std::size_t>(index)) &&
                candidate.signedSourceIndex > 0) {
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

void WordGame::playAttractMusic() {
    if (!attractMode_ || !musicOn_ || speakerEffects_) return;
    const BlobView sound = assets_.gameArchive().find("GSND", 2);
    if (!sound) return;
    MeccSound score = decodeMeccGSound(sound, 16, MeccSoundProfile::WordMunchers);
    if (!score.valid) return;
    (void)attractOplPlayer_.startLoop(
        std::move(score.writes), score.durationMilliseconds);
}

void WordGame::keyDown(const UINT virtualKey) {
    if (exitPending_) return;
    if (configurationWriteError_) {
        // Printable keys and Space are completed by WM_CHAR; deferring them
        // here consumes exactly one physical input instead of dismissing the
        // alert and leaking the paired character into the restored page.
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        dismissConfigurationWriteError();
        return;
    }
    if (page_ == WordGamePage::StartupVersion) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        page_ = WordGamePage::StartupSplash;
        return;
    }
    if (page_ == WordGamePage::StartupSplash) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        page_ = WordGamePage::Title;
        menuSelection_ = 0;
        titleIdleTimer_ = 0.0;
        return;
    }
    if (attractMode_) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        stopAttract();
        return;
    }
    if (userPresentationActive()) return;
    if (page_ == WordGamePage::Title) titleIdleTimer_ = 0.0;
    if (cheatOpen_) {
        handleCheatKey(virtualKey);
        return;
    }
    if (page_ == WordGamePage::LevelComplete) {
        // The common dispatcher does not forward Escape in selector state 6.
        // Any ordinary key that it does forward calls the scene teardown
        // before the gameplay callback attempts to decode that key.
        if (virtualKey != VK_ESCAPE && virtualKey != VK_SHIFT &&
            virtualKey != VK_CONTROL && virtualKey != VK_MENU &&
            !virtualKeyDefersToCharacter(virtualKey)) {
            finishLevelCompleteScene();
        }
        return;
    }
    switch (page_) {
    case WordGamePage::StartupVersion:
    case WordGamePage::StartupSplash:
        // Both pages returned before this switch.
        break;
    case WordGamePage::Title:
        handleTitleKey(virtualKey);
        break;
    case WordGamePage::InstructionsQuestion:
        if (virtualKey == VK_ESCAPE) {
            page_ = WordGamePage::Title;
            menuSelection_ = 0;
        } else {
            munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
            // The shared wrapper at 0x122fd keeps Y/N as selection keys.
            // Only Enter (or pointer activation) accepts; Space is absent
            // from the nine-byte custom list at DS:2352.
            if (virtualKey == VK_RETURN) {
                if (menuSelection_ == 0) {
                    informationStartsGame_ = true;
                    informationGroup_ = 1;
                    informationPage_ = 0;
                    page_ = WordGamePage::Information;
                    playGameplaySound(13);
                } else {
                    startGameWithBoardPresentation();
                }
            }
        }
        break;
    case WordGamePage::Information:
        if (virtualKey == VK_ESCAPE) {
            informationStartsGame_ = false;
            informationGroup_ = 0;
            informationPage_ = 0;
            page_ = WordGamePage::Title;
            menuSelection_ = 0;
        } else if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            ++informationPage_;
            if (informationGroup_ == 1 && informationPage_ == 1) {
                playGameplaySound(14);
            }
            if (informationPage_ >=
                InformationPageCounts[static_cast<std::size_t>(informationGroup_)]) {
                informationPage_ = 0;
                informationGroup_ = 0;
                if (informationStartsGame_) {
                    informationStartsGame_ = false;
                    startGameWithBoardPresentation();
                } else {
                    page_ = WordGamePage::Title;
                    menuSelection_ = 0;
                }
            }
        }
        break;
    case WordGamePage::Options:
        if (virtualKey == VK_ESCAPE) {
            page_ = WordGamePage::Title;
            menuSelection_ = 0;
            break;
        }
        munchers::applyFixedMenuNavigation(virtualKey, 5, menuSelection_);
        if (virtualKey != VK_RETURN) break;
        switch (menuSelection_) {
        case 0:
            contentDraftDifficulty_ = wordDifficulty_;
            contentDraftSounds_ = selectedSounds_;
            contentDraftChanged_ = false;
            contentInsufficientMessage_ = false;
            page_ = WordGamePage::OptionsContent;
            menuSelection_ = 0;
            break;
        case 1:
            eraseDraft_ = hall_.entries();
            eraseSelection_ = 0;
            page_ = WordGamePage::OptionsEraseHall;
            break;
        case 2:
            passwordDraft_ = optionPassword_;
            hintDraft_ = optionHint_;
            passwordEditingHint_ = false;
            page_ = WordGamePage::OptionsPassword;
            break;
        case 3:
            joystickEnabled_ = !joystickEnabled_;
            saveSettings();
            break;
        case 4:
            beginJoystickCalibration();
            break;
        default: break;
        }
        break;
    case WordGamePage::OptionsContent:
        if (contentInsufficientMessage_) {
            // The shared pAccept waiter at image 0x1386E accepts only the
            // DS:14D4 table: Escape, Enter, or Space.  Dismissing the warning
            // returns to this same Set Content loop; it does not accept or
            // discard the invalid draft.
            if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN ||
                virtualKey == VK_SPACE) {
                contentInsufficientMessage_ = false;
            }
            break;
        }
        if (virtualKey == VK_ESCAPE) {
            // The original tracks whether either child editor accepted a
            // change.  An untouched Set Content page exits directly.  A
            // changed draft must build at least one target word; failure
            // opens the blocking message at DS:2836 and loops here.
            if (!contentDraftChanged_) {
                page_ = WordGamePage::Options;
                menuSelection_ = 0;
            } else if (commitContentDraft()) {
                page_ = WordGamePage::Options;
                menuSelection_ = 0;
                contentDraftChanged_ = false;
            } else {
                contentInsufficientMessage_ = true;
            }
            break;
        }
        munchers::applyFixedMenuNavigation(virtualKey, 3, menuSelection_);
        if (virtualKey != VK_RETURN) break;
        if (menuSelection_ == 0) {
            difficultyEditorOriginal_ = contentDraftDifficulty_;
            menuSelection_ = contentDraftDifficulty_;
            page_ = WordGamePage::OptionsDifficulty;
        } else if (menuSelection_ == 1) {
            vowelEditorOriginalSounds_ = contentDraftSounds_;
            vowelSelection_ = 0;
            vowelMessage_ = 0;
            page_ = WordGamePage::OptionsVowels;
        } else if (menuSelection_ == 2) {
            beginWordPreview();
        }
        break;
    case WordGamePage::OptionsDifficulty:
        if (virtualKey == VK_ESCAPE) {
            contentDraftDifficulty_ = difficultyEditorOriginal_;
            page_ = WordGamePage::OptionsContent;
            menuSelection_ = 0;
            break;
        }
        munchers::applyFixedMenuNavigation(virtualKey, 8, menuSelection_);
        if (virtualKey == VK_RETURN) {
            if (contentDraftDifficulty_ != menuSelection_) contentDraftChanged_ = true;
            contentDraftDifficulty_ = menuSelection_;
            page_ = WordGamePage::OptionsContent;
            menuSelection_ = 0;
        }
        break;
    case WordGamePage::OptionsVowels:
        if (vowelMessage_ != 0) {
            if (virtualKey == VK_SPACE || virtualKey == VK_RETURN || virtualKey == VK_ESCAPE) {
                vowelMessage_ = 0;
            }
            break;
        }
        if (virtualKey == VK_ESCAPE) {
            contentDraftSounds_ = vowelEditorOriginalSounds_;
            page_ = WordGamePage::OptionsContent;
            menuSelection_ = 1;
            break;
        }
        if (virtualKey == VK_F1) {
            page_ = WordGamePage::OptionsVowelsHelp;
            break;
        }
        if (virtualKey == VK_F2) {
            contentDraftSounds_.fill(true);
            break;
        }
        if (virtualKey == VK_F3) {
            contentDraftSounds_.fill(false);
            break;
        }
        if (virtualKey == VK_SPACE) {
            toggleVowelChoice(vowelSelection_);
            break;
        }
        if (virtualKey == VK_RETURN) {
            vowelMessage_ = vowelValidationMessage();
            if (vowelMessage_ == 0) {
                // 0x1472A returns AL=1 for every validated Enter, which sets
                // the parent Set Content changed byte even if the final bit
                // pattern happens to equal the entry snapshot.
                contentDraftChanged_ = true;
                page_ = WordGamePage::OptionsContent;
                menuSelection_ = 1;
            }
            break;
        }
        if (virtualKey == VK_HOME) {
            // Home/0xC7 is absent from DS:2998, so the generic widget
            // consumes it at 0x0F36D and selects its one-based first item.
            vowelSelection_ = 0;
        } else if (virtualKey == VK_END) {
            // End/0xCF likewise stays inside the generic widget and reaches
            // 0x0F378, which installs the fourteen-item terminal selection.
            vowelSelection_ = 13;
        } else if (virtualKey == VK_UP) {
            if (vowelSelection_ < 6) vowelSelection_ = (vowelSelection_ + 5) % 6;
            else if (vowelSelection_ < 9) vowelSelection_ = 6 + (vowelSelection_ - 6 + 2) % 3;
            else vowelSelection_ = 9 + (vowelSelection_ - 9 + 4) % 5;
        } else if (virtualKey == VK_DOWN) {
            if (vowelSelection_ < 6) vowelSelection_ = (vowelSelection_ + 1) % 6;
            else if (vowelSelection_ < 9) vowelSelection_ = 6 + (vowelSelection_ - 6 + 1) % 3;
            else vowelSelection_ = 9 + (vowelSelection_ - 9 + 1) % 5;
        } else if (virtualKey == VK_RIGHT) {
            const int current = vowelSelection_ + 1;
            vowelSelection_ = current <= 6 ? std::min(current + 6, 9) - 1
                : current <= 9 ? current + 3 - 1 : current - 9 - 1;
        } else if (virtualKey == VK_LEFT) {
            const int current = vowelSelection_ + 1;
            vowelSelection_ = current > 9 ? std::min(current - 3, 9) - 1
                : current > 6 ? current - 6 - 1 : std::min(current + 9, 14) - 1;
        }
        break;
    case WordGamePage::OptionsVowelsHelp:
        // 0x1499e calls the shared accepted-key waiter at 0x0bcc7. Its
        // DS:14D4 table is exactly Escape, Enter, Space, NUL.
        if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            page_ = WordGamePage::OptionsVowels;
        }
        break;
    case WordGamePage::OptionsPreview:
        if (virtualKey == VK_ESCAPE) {
            page_ = WordGamePage::OptionsContent;
            menuSelection_ = 2;
        } else if (virtualKey == VK_SPACE || virtualKey == VK_RETURN) {
            advanceWordPreview();
        }
        break;
    case WordGamePage::OptionsEraseHall:
        if (virtualKey == VK_ESCAPE) {
            eraseDraft_.clear();
            page_ = WordGamePage::Options;
            menuSelection_ = 1;
            break;
        }
        if (eraseDraft_.empty()) {
            if (hall_.entries().empty()) {
                // The byte-isomorphic branch calls the accepted-key wrapper
                // at image 0x0AF80, using DS:14D4.
                if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
                    page_ = WordGamePage::Options;
                    menuSelection_ = 1;
                }
            } else if (virtualKey == VK_RETURN) {
                (void)hall_.replace({});
                saveSettings();
                page_ = WordGamePage::Options;
                menuSelection_ = 1;
            }
            break;
        }
        if (munchers::applyFixedMenuNavigation(
                virtualKey, static_cast<int>(eraseDraft_.size()), eraseSelection_)) {
            // Navigation is selection-only; activation remains Space/Enter.
        } else if (virtualKey == VK_SPACE) {
            eraseDraft_.erase(eraseDraft_.begin() + eraseSelection_);
            if (!eraseDraft_.empty()) eraseSelection_ %= static_cast<int>(eraseDraft_.size());
            else eraseSelection_ = 0;
        } else if (virtualKey == VK_RETURN) {
            if (hall_.replace(eraseDraft_)) {
                saveSettings();
                eraseDraft_.clear();
                page_ = WordGamePage::Options;
                menuSelection_ = 1;
            }
        }
        break;
    case WordGamePage::OptionsPassword:
        if (virtualKey == VK_ESCAPE) {
            // The original routine returns its "accepted" result after the
            // password field has completed, even when Escape cancels the
            // subsequent hint field.  The caller therefore rewrites the
            // unchanged configuration on this path.  Escape from the first
            // field returns the non-accepted result and does not save.
            if (passwordEditingHint_) saveSettings();
            passwordDraft_.clear();
            hintDraft_.clear();
            page_ = WordGamePage::Options;
            menuSelection_ = 2;
        } else if (virtualKey == VK_LEFT) {
            std::string& field = passwordEditingHint_ ? hintDraft_ : passwordDraft_;
            if (!field.empty()) field.pop_back();
        } else if (virtualKey == VK_HOME) {
            (passwordEditingHint_ ? hintDraft_ : passwordDraft_).clear();
        } else if (virtualKey == VK_RETURN) {
            if (!passwordEditingHint_) {
                if (passwordDraft_.empty()) {
                    optionPassword_.clear();
                    optionHint_.clear();
                    saveSettings();
                    page_ = WordGamePage::Options;
                    menuSelection_ = 2;
                } else {
                    passwordEditingHint_ = true;
                }
            } else {
                optionPassword_ = passwordDraft_;
                optionHint_ = hintDraft_;
                saveSettings();
                page_ = WordGamePage::Options;
                menuSelection_ = 2;
            }
        }
        break;
    case WordGamePage::OptionsPasswordPrompt:
        if (passwordRejected_) {
            // 0x0fa41 reaches the 0x0bcc7 wrapper and its exact DS:14D4
            // {Escape, Enter, Space, NUL} table. Unsupported keys stay owned
            // by the blocking rejection overlay.
            if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
                passwordRejected_ = false;
            }
            break;
        }
        if (virtualKey == VK_ESCAPE) {
            passwordAttempt_.clear();
            page_ = WordGamePage::Title;
            menuSelection_ = 0;
        } else if (virtualKey == VK_LEFT) {
            if (!passwordAttempt_.empty()) passwordAttempt_.pop_back();
        } else if (virtualKey == VK_HOME) {
            passwordAttempt_.clear();
        } else if (virtualKey == VK_RETURN) {
            const bool matches = optionPassword_.size() == passwordAttempt_.size() &&
                std::equal(optionPassword_.begin(), optionPassword_.end(), passwordAttempt_.begin(),
                           [](const unsigned char left, const unsigned char right) {
                               return std::toupper(left) == std::toupper(right);
                           });
            passwordAttempt_.clear();
            if (matches) {
                passwordRejected_ = false;
                page_ = WordGamePage::Options;
                menuSelection_ = 0;
            } else {
                passwordRejected_ = true;
            }
        }
        break;
    case WordGamePage::OptionsJoystickCalibration:
        if (virtualKey == VK_ESCAPE) {
            page_ = WordGamePage::Options;
            menuSelection_ = 4;
        } else if (virtualKey == VK_SPACE || virtualKey == VK_RETURN) {
            acceptJoystickCalibration();
        }
        break;
    case WordGamePage::Hall:
        if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            // The original Hall painter clears the score used for its
            // name/score lookup after drawing the admitted row. Retain the
            // pair across native repaints, then consume it on Hall departure.
            hallHighlightName_.clear();
            hallHighlightScore_ = 0;
            if (postGameHall_) {
                page_ = WordGamePage::ReplayQuestion;
                menuSelection_ = 0;
            } else {
                page_ = WordGamePage::Title;
                menuSelection_ = 0;
            }
        }
        break;
    case WordGamePage::Playing:
    case WordGamePage::Paused:
    case WordGamePage::Feedback:
        handlePlayingKey(virtualKey);
        break;
    case WordGamePage::Attract:
    case WordGamePage::AttractLogo:
        break;
    case WordGamePage::QuitConfirm:
        if (virtualKey == VK_ESCAPE) {
            if (quitFromFeedback_) finishFeedback();
            else page_ = quitReturnPage_;
            quitFromFeedback_ = false;
        } else {
            munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
            if (virtualKey == VK_RETURN) {
                const bool fromFeedback = quitFromFeedback_;
                quitFromFeedback_ = false;
                if (menuSelection_ == 0) {
                    if (fromFeedback) {
                        // 0x0d43d/0x0d5dc call the ordinary terminal after
                        // a confirmed feedback Escape, including Hall
                        // admission for a qualifying score.
                        finishFeedback(true);
                    } else {
                        page_ = WordGamePage::Title;
                        menuSelection_ = 0;
    pointerCellQueue_.clear();
    moving_ = false;
    playerTerminalFrame_ = -1;
    munching_ = false;
                        enemies_.clear();
                        enemySlotCount_ = 0;
                        safeZoneJobCount_ = 0;
                        safeCells_.fill(false);
                        playerRecovering_ = false;
                        deathAnimating_ = false;
                        levelCompleteScene_ = SceneScript{};
                        sceneEffectPlayer_.stop();
                        // Confirmed in-game quit returns to Word's own title;
                        // the resident driver remains installed and receives
                        // the same all-channel stop used by board setup.
                        attractOplPlayer_.stopAllChannels();
                    }
                } else {
                    if (fromFeedback) finishFeedback();
                    else page_ = quitReturnPage_;
                }
            }
        }
        break;
    case WordGamePage::LevelComplete:
        // Handled before the page switch above.
        break;
    case WordGamePage::NameEntry:
        if (virtualKey == VK_ESCAPE) {
            submitHallName();
        } else if (virtualKey == VK_LEFT) {
            if (!nameInput_.empty()) nameInput_.pop_back();
            nameCursorBlinkTimer_ = 0.0;
            nameCursorVisible_ = true;
        } else if (virtualKey == VK_HOME) {
            nameInput_.clear();
            nameCursorBlinkTimer_ = 0.0;
            nameCursorVisible_ = true;
        } else if (virtualKey == VK_RETURN) {
            // DS:16BC stores a zero threshold, so empty Enter exits and the
            // caller substitutes "The Unknown Muncher".
            submitHallName();
        }
        break;
    case WordGamePage::ReplayQuestion:
        if (virtualKey == VK_ESCAPE) {
            postGameHall_ = false;
            page_ = WordGamePage::Title;
            menuSelection_ = 0;
            break;
        }
        munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
        if (virtualKey == VK_RETURN) {
            if (menuSelection_ == 0) {
                // Neither the primitive replay prompt nor the ordinary board
                // initializer reloads a PCX palette after the Hall.
                startGameWithBoardPresentation(1, true);
            } else {
                postGameHall_ = false;
                page_ = WordGamePage::Title;
                menuSelection_ = 0;
            }
        }
        break;
    }
}

void WordGame::beginExitDelay() {
    exitPending_ = true;
    exitDelayTimer_ = MainControllerCleanupDelay;
    titleIdleTimer_ = 0.0;
}

void WordGame::handleTitleKey(const UINT virtualKey) {
    if (virtualKey == VK_ESCAPE) {
        beginExitDelay();
        return;
    }
    munchers::applyFixedMenuNavigation(virtualKey, 5, menuSelection_);
    if (virtualKey != VK_RETURN) return;
    switch (menuSelection_) {
    case 0:
        page_ = WordGamePage::InstructionsQuestion;
        menuSelection_ = 1;
        break;
    case 1: postGameHall_ = false; page_ = WordGamePage::Hall; break;
    case 2:
        informationStartsGame_ = false;
        informationGroup_ = 0;
        informationPage_ = 0;
        page_ = WordGamePage::Information;
        break;
    case 3:
        menuSelection_ = 0;
        if (optionPassword_.empty()) {
            page_ = WordGamePage::Options;
        } else {
            passwordAttempt_.clear();
            passwordRejected_ = false;
            page_ = WordGamePage::OptionsPasswordPrompt;
        }
        break;
    case 4: beginExitDelay(); break;
    default: break;
    }
}

void WordGame::handlePlayingKey(const UINT virtualKey) {
    if (page_ == WordGamePage::Feedback) {
        if (deathAnimating_) return;
        if (virtualKey == VK_ESCAPE) {
            beginQuitConfirm(WordGamePage::Playing, true);
            return;
        }
        // DS:14D4 is {Escape, Enter, Space, NUL}; normal Enter and Space
        // therefore share the same strip-restoration path.
        if (virtualKey != VK_SPACE && virtualKey != VK_RETURN) return;
        finishFeedback();
        return;
    }
    if (page_ == WordGamePage::Paused) {
        if (virtualKey == VK_RETURN) {
            pointerCellQueue_.clear();
            page_ = WordGamePage::Playing;
        }
        if (virtualKey == VK_ESCAPE) beginQuitConfirm(WordGamePage::Paused);
        return;
    }
    if (virtualKey == VK_ESCAPE) {
        beginQuitConfirm(WordGamePage::Playing);
        return;
    }
    if (virtualKey == VK_RETURN) {
        // Word's byte-isomorphic Enter branch clears the shared input ring
        // before entering Time out, including while the player actor is busy.
        pointerCellQueue_.clear();
        page_ = WordGamePage::Paused;
        return;
    }
    switch (virtualKey) {
    case VK_UP: enqueuePlayerInput('I'); break;
    case VK_RIGHT: enqueuePlayerInput('K'); break;
    case VK_DOWN: enqueuePlayerInput('M'); break;
    case VK_LEFT: enqueuePlayerInput('J'); break;
    case VK_SPACE: enqueuePlayerInput(' '); break;
    default: break;
    }
}

void WordGame::character(const wchar_t characterValue) {
    if (exitPending_) return;
    if (configurationWriteError_) {
        dismissConfigurationWriteError();
        return;
    }
    if (page_ == WordGamePage::StartupVersion) {
        if (!belongsToDeferredBlockingKey(characterValue)) return;
        page_ = WordGamePage::StartupSplash;
        return;
    }
    if (attractMode_) {
        // Word's byte-isomorphic common dispatcher applies +/- before Demo
        // selector states 1..3 consume the same event and return to Title.
        const int previousRepeat = joystickRepeatTicks_;
        (void)adjustJoystickRepeat(characterValue);
        stopAttract();
        if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
        return;
    }
    if (userPresentationActive()) return;
    if (page_ == WordGamePage::StartupSplash) {
        if (!belongsToDeferredBlockingKey(characterValue)) return;
        page_ = WordGamePage::Title;
        menuSelection_ = 0;
        titleIdleTimer_ = 0.0;
        return;
    }
    if (page_ == WordGamePage::OptionsVowelsHelp) {
        // The blocking 0x0bcc7 waiter owns this event. Unsupported translated
        // characters are discarded here instead of reaching global +/-
        // joystick-repeat handling while the help page remains visible.
        return;
    }
    if (page_ == WordGamePage::OptionsContent && contentInsufficientMessage_) {
        // Printable events not present in DS:14D4 are consumed by the modal.
        // Space can arrive directly in headless callers as a character event.
        if (characterValue == L' ') contentInsufficientMessage_ = false;
        return;
    }
    if (page_ == WordGamePage::OptionsVowels && vowelMessage_ == 0) {
        // DS:2998 is the widget's returned-key table. It returns ASCII 4/6
        // to the custom switch at 0x14482..0x14579, where they share the
        // Left/Right graph with 0xCB/0xCD. ASCII 2/8 are not returned; the
        // generic widget consumes them through the same Down/Up branches as
        // 0xD0/0xC8. Other printable characters remain owned and inert.
        if (characterValue == L'2') keyDown(VK_DOWN);
        else if (characterValue == L'4') keyDown(VK_LEFT);
        else if (characterValue == L'6') keyDown(VK_RIGHT);
        else if (characterValue == L'8') keyDown(VK_UP);
        return;
    }
    if (page_ == WordGamePage::LevelComplete && !cheatOpen_) {
        // Word's isomorphic common dispatcher adjusts its 1..10 joystick
        // repeat word before selector state 6 forwards the same event to the
        // cartoon teardown callback. A repeat-control key therefore performs
        // both operations.
        const int previousRepeat = joystickRepeatTicks_;
        (void)adjustJoystickRepeat(characterValue);
        finishLevelCompleteScene();
        if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
        return;
    }
    if ((page_ == WordGamePage::OptionsPassword ||
         page_ == WordGamePage::OptionsPasswordPrompt) && !cheatOpen_) {
        if (page_ == WordGamePage::OptionsPasswordPrompt && passwordRejected_) {
            if (characterValue == L' ') passwordRejected_ = false;
            return;
        }
        std::string& field = page_ == WordGamePage::OptionsPasswordPrompt
            ? passwordAttempt_ : passwordEditingHint_ ? hintDraft_ : passwordDraft_;
        const bool passwordField = page_ == WordGamePage::OptionsPasswordPrompt ||
            !passwordEditingHint_;
        if (characterValue == L'\b') {
            if (!field.empty()) field.pop_back();
        }
        const std::size_t fieldLimit =
            page_ == WordGamePage::OptionsPasswordPrompt || !passwordEditingHint_ ? 10u : 50u;
        if (characterValue >= (passwordField ? 33 : 32) && characterValue <= 126 &&
            field.size() < fieldLimit) {
            field.push_back(static_cast<char>(characterValue));
        }
        return;
    }
    if (page_ == WordGamePage::NameEntry && !cheatOpen_) {
        if (characterValue == L'\b') {
            if (!nameInput_.empty()) nameInput_.pop_back();
        } else if (characterValue >= 32 && characterValue <= 126 && nameInput_.size() < 25) {
            nameInput_.push_back(static_cast<char>(characterValue));
        }
        nameCursorBlinkTimer_ = 0.0;
        nameCursorVisible_ = true;
        return;
    }
    if (page_ == WordGamePage::OptionsEraseHall && eraseDraft_.empty() &&
        hall_.entries().empty() && !cheatOpen_) {
        if (characterValue == L' ') {
            page_ = WordGamePage::Options;
            menuSelection_ = 1;
        }
        return;
    }
    if (!cheatOpen_) {
        bool commonMenuCharacter = false;
        switch (page_) {
        case WordGamePage::Title:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(characterValue, 5, menuSelection_);
            break;
        case WordGamePage::Options:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(characterValue, 5, menuSelection_);
            break;
        case WordGamePage::OptionsContent:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(characterValue, 3, menuSelection_);
            break;
        case WordGamePage::OptionsDifficulty:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(characterValue, 8, menuSelection_);
            break;
        case WordGamePage::OptionsEraseHall:
            if (!eraseDraft_.empty()) {
                commonMenuCharacter = munchers::applyNumberedMenuShortcut(
                    characterValue, static_cast<int>(eraseDraft_.size()), eraseSelection_);
            }
            break;
        case WordGamePage::InstructionsQuestion:
        case WordGamePage::ReplayQuestion:
        case WordGamePage::QuitConfirm:
            commonMenuCharacter = munchers::applyUnnumberedMenuCharacterNavigation(
                characterValue, 2, menuSelection_);
            break;
        default:
            break;
        }
        if (commonMenuCharacter) return;
    }
    if (acceptsCommonDispatcherInput()) {
        // Word's resident dispatcher has the same modal boundary: only live
        // selector states see this 1..10 control. '+' decreases the public-
        // tick word and '-' increases it when either joystick flag is set.
        const int previousRepeat = joystickRepeatTicks_;
        if (adjustJoystickRepeat(characterValue)) {
            if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
            return;
        }
    }
    if (page_ != WordGamePage::Playing || cheatOpen_ || deathAnimating_) return;
    switch (characterValue) {
    // Word carries the byte-identical dispatcher: 8/A/I are Up, 4/J is Left,
    // 6/K is Right, and 2/M/Z are Down. Only Space starts a chew.
    case L'8': case L'a': case L'A': case L'i': case L'I':
        enqueuePlayerInput('I');
        break;
    case L'6': case L'k': case L'K': enqueuePlayerInput('K'); break;
    case L'2': case L'm': case L'M': case L'z': case L'Z':
        enqueuePlayerInput('M');
        break;
    case L'4': case L'j': case L'J': enqueuePlayerInput('J'); break;
    default: break;
    }
}

bool WordGame::acceptsCommonDispatcherInput() const {
    return !configurationWriteError_ && !cheatOpen_ && !userPresentationActive() &&
           (attractMode_ || page_ == WordGamePage::Playing ||
            page_ == WordGamePage::Paused || page_ == WordGamePage::Feedback ||
            page_ == WordGamePage::LevelComplete);
}

void WordGame::beginMove(int row, int column, const int direction) {
    if (!isActiveBoardPage(page_) || moving_ || munching_ || playerRecovering_ ||
        deathAnimating_) return;
    row = std::clamp(row, 0, BoardRows - 1);
    column = std::clamp(column, 0, BoardColumns - 1);
    if (row == playerRow_ && column == playerColumn_) return;
    moveFromRow_ = playerRow_;
    moveFromColumn_ = playerColumn_;
    moveToRow_ = row;
    moveToColumn_ = column;
    moveDirection_ = std::clamp(direction, 0, 3);
    moveTimer_ = playerMoveAnimationDuration(moveDirection_);
    playerTerminalFrame_ = -1;
    moving_ = true;
}

double WordGame::playerMoveAnimationDuration(const int direction) {
    const bool vertical = direction == 0 || direction == 2;
    return static_cast<double>(vertical ? 5 : 6) / OriginalSceneTicksPerSecond;
}

int WordGame::playerMovementPhase() const {
    if (!moving_) return 0;
    const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
    const int stepCount = vertical ? 5 : 6;
    const double duration = playerMoveAnimationDuration(moveDirection_);
    const double elapsed = std::clamp(duration - moveTimer_, 0.0, duration);
    return std::clamp(static_cast<int>(
                          std::floor(elapsed * OriginalSceneTicksPerSecond + 1e-7)),
                      0, stepCount - 1);
}

int WordGame::playerMoveFrameAtPhase(const int direction, const int phase) {
    // The dirty-cell paint happens before the common animator changes the
    // record, so each new position shows the record retained from the prior
    // interval. The live rightward walk supplies the exact phase alignment.
    constexpr std::array<int, 6> FrameOffsets = {2, 2, 1, 0, 1, 2};
    return std::clamp(direction, 0, 3) * 3 +
           FrameOffsets[static_cast<std::size_t>(
               std::clamp(phase, 0, static_cast<int>(FrameOffsets.size()) - 1))];
}

int WordGame::munchFrameAtTick(const int elapsedTicks) {
    // Word's 0x09669/0x09F76 actor path is instruction-for-instruction the
    // same seven-transition chew dispatcher as Number's 0x08A31/0x093A9.
    constexpr std::array<int, 8> Frames = {12, 13, 14, 13, 12, 13, 14, 13};
    return Frames[static_cast<std::size_t>(
        std::clamp(elapsedTicks, 0, static_cast<int>(Frames.size()) - 1))];
}

bool WordGame::playerCanBeCaughtAtEnemyEndpoint() const {
    // The common predicate at Word image 0x09445 queries Muncher job 4 and
    // accepts only scheduler state 4. Native state 3 is moving, state 5 is
    // chewing, and state 6 is post-collision recovery.
    return !moving_ && !munching_ && !playerRecovering_ && !deathAnimating_;
}

void WordGame::updatePlayerMovementTick(const double tickSeconds) {
    if (!moving_) return;
    moveTimer_ -= tickSeconds;
    if (moveTimer_ > JobTimerEpsilon) return;

    moving_ = false;
    moveTimer_ = 0.0;
    playerRow_ = moveToRow_;
    playerColumn_ = moveToColumn_;
    const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
    playerTerminalFrame_ = moveDirection_ * 3 + (vertical ? 2 : 1);
    if (const int enemyType = enemyTypeAt(playerRow_, playerColumn_); enemyType >= 0) {
        beginTroggleCollision(enemyType, enemySlotAt(playerRow_, playerColumn_));
    } else {
        processPointerCellQueue();
    }
}

void WordGame::enqueuePlayerInput(const int inputByte) {
    if (pointerCellQueue_.size() >= 9) return;
    pointerCellQueue_.push_back(inputByte);
    processPointerCellQueue();
}

void WordGame::processPointerCellQueue() {
    if (pointerCellQueue_.empty() || !isActiveBoardPage(page_) || moving_ || munching_ ||
        playerRecovering_ || deathAnimating_) {
        return;
    }
    const int inputByte = pointerCellQueue_.front();
    if (inputByte > 0) {
        pointerCellQueue_.pop_front();
        switch (inputByte) {
        case 'I': beginMove(playerRow_ - 1, playerColumn_, 0); break;
        case 'K': beginMove(playerRow_, playerColumn_ + 1, 1); break;
        case 'M': beginMove(playerRow_ + 1, playerColumn_, 2); break;
        case 'J': beginMove(playerRow_, playerColumn_ - 1, 3); break;
        case ' ': beginMunch(); break;
        default: break;
        }
        return;
    }

    const int target = -inputByte;
    if (target >= BoardCellCount) {
        pointerCellQueue_.pop_front();
        processPointerCellQueue();
        return;
    }
    const int current = playerRow_ * BoardColumns + playerColumn_;
    if (target == current) {
        pointerCellQueue_.pop_front();
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
    if (row * BoardColumns + column == target) pointerCellQueue_.pop_front();
    beginMove(row, column, direction);
}

void WordGame::beginMunch() {
    if (!isActiveBoardPage(page_) || moving_ || munching_ || playerRecovering_ ||
        deathAnimating_) return;
    const int index = playerRow_ * BoardColumns + playerColumn_;
    if (core_.eaten(static_cast<std::size_t>(index))) return;
    const WordBoardCell& selected = core_.board().cells[static_cast<std::size_t>(index)];
    // Word image 0x0960f rejects the zero signed-source word before cue 7/8.
    // Keep that guard here as well as in the core so a rejected action cannot
    // emit a wrong-answer sound.
    if (selected.signedSourceIndex == 0) return;
    playGameplaySound(selected.signedSourceIndex > 0
                          ? 7 : 8);
    if (!core_.beginMunchCell(static_cast<std::size_t>(index))) return;
    munchCellIndex_ = index;
    munchTimer_ = MunchAnimationDuration;
    playerTerminalFrame_ = -1;
    munching_ = true;
}

void WordGame::resolveMunch() {
    if (!munching_ || munchCellIndex_ < 0 || munchCellIndex_ >= BoardCellCount) return;
    const int index = munchCellIndex_;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    lastResolution_ = core_.resolveMunchCell(static_cast<std::size_t>(index));
    if (lastResolution_.maximumScore) {
        if (attractMode_) {
            beginAttractHallTransition();
        } else {
            beginPostGame();
        }
        return;
    }
    if (lastResolution_.kind == WordMunchKind::Wrong || lastResolution_.gameOver) {
        feedbackEnemyType_ = -1;
        feedbackEnemySlot_ = -1;
        feedbackMessage_.clear();
        page_ = WordGamePage::Feedback;
        if (attractMode_) {
            attractPostFeedbackBoard_ = false;
            attractTransitionTimer_ = AttractFeedbackDuration;
        }
        return;
    }
    if (lastResolution_.boardComplete) handleCompletedBoard();
}

void WordGame::beginPostGame() {
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeCells_.fill(false);
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    moving_ = false;
    moveTimer_ = 0.0;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    pointerCellQueue_.clear();
    playerRecovering_ = false;
    deathAnimating_ = false;
    postGameHall_ = true;
    hallHighlightName_.clear();
    hallHighlightScore_ = 0;

    const int score = core_.scoreState().score();
    const int rank = hall_.insertionRank(static_cast<std::uint32_t>(score));
    pendingHallAdmission_ = rank >= 0;
    if (pendingHallAdmission_) {
        nameInput_.clear();
        nameCursorBlinkTimer_ = 0.0;
        nameCursorVisible_ = true;
        nameEntryMessageType_ = score >= MuncherScore::MaximumScore ? 3 : (rank == 0 ? 2 : 1);
        page_ = WordGamePage::NameEntry;
    } else {
        page_ = WordGamePage::Hall;
    }
}

void WordGame::submitHallName() {
    if (pendingHallAdmission_) {
        const std::uint32_t score = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(core_.scoreState().score()),
            static_cast<std::uint32_t>(MuncherScore::MaximumScore));
        std::string admittedName = nameInput_.empty() ? "The Unknown Muncher" : nameInput_;
        if (admittedName.size() > WordHall::NameLimit) admittedName.resize(WordHall::NameLimit);
        if (hall_.insert(score, admittedName)) {
            // Image 0x11c33 searches the final Hall from the beginning for
            // the just-admitted name/score pair.  Keeping the pair (rather
            // than only its insertion rank) also preserves duplicate-row
            // behavior and naturally stops highlighting an erased entry.
            hallHighlightName_ = std::move(admittedName);
            hallHighlightScore_ = score;
        }
    }
    pendingHallAdmission_ = false;
    nameInput_.clear();
    postGameHall_ = true;
    saveSettings();
    page_ = WordGamePage::Hall;
}

void WordGame::handleCompletedBoard() {
    if (attractMode_) {
        // Image 0x0ad76 skips stream 15 in demo mode and routes through the
        // unattended terminal controller instead of advancing the level.
        moving_ = false;
        munching_ = false;
        pointerCellQueue_.clear();
        beginAttractHallTransition();
        return;
    }
    // Word 0x0fe2d closes the just-completed board for both direct and
    // every-third-board routes before it loads the next target presentation.
    captureBoardPresentationSource();
    playGameplaySound(15);
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeCells_.fill(false);
    enemySlotCount_ = 0;
    safeZoneJobCount_ = 0;
    enemyWarning_ = false;
    playerRecovering_ = false;
    deathAnimating_ = false;
    deathSequenceTicks_ = 0;
    moving_ = false;
    munching_ = false;
    pointerCellQueue_.clear();
    gameplayTickAccumulator_ = 0.0;
    if (core_.cartoonPending()) {
        page_ = WordGamePage::LevelComplete;
        // Word's selected-scene loader at 0x114E0 submits GSND 0, then waits
        // 15 ms at 0x114EB before installing the PCXF/SCPT scene. Defer the
        // scene and its setup callbacks until that covered hold has elapsed.
        attractOplPlayer_.stopAllChannels();
        sceneEffectPlayer_.stop();
        pendingCartoonScene_ = core_.pendingCartoonScene();
        beginCartoonPresentation();
    } else if (core_.advanceBoard()) {
        initializeBoardRuntime();
        page_ = WordGamePage::Playing;
        beginBoardPresentation();
    } else {
        page_ = WordGamePage::Hall;
    }
}

bool WordGame::loadLevelCompleteScene(const int sceneIndex) {
    static constexpr std::array<std::uint16_t, 9> Scene0 = {6, 7, 5, 8, 4, 0, 1, 2, 3};
    static constexpr std::array<std::uint16_t, 5> Scene1 = {3, 4, 0, 1, 2};
    static constexpr std::array<std::uint16_t, 7> Scene2 = {6, 4, 3, 5, 1, 0, 2};
    static constexpr std::array<std::uint16_t, 5> Scene3 = {0, 3, 4, 1, 2};
    static constexpr std::array<std::uint16_t, 7> Scene4 = {0, 1, 2, 5, 3, 4, 6};
    static constexpr std::array<std::uint16_t, 6> Scene5 = {5, 2, 3, 0, 1, 4};

    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(SceneGraphicIds.size())) return false;
    sceneGraphicId_ = SceneGraphicIds[static_cast<std::size_t>(sceneIndex)];
    sceneAudioBank_ = sceneIndex + 1;
    sceneTicks_ = 0;
    sceneTickAccumulator_ = 0.0;
    sceneEffectPlayer_.stop();
    attractOplPlayer_.stopEffect();
    const BlobView script = assets_.gameArchive().find(
        "SCPT", static_cast<std::uint32_t>(sceneGraphicId_ - 1));

    bool loaded = false;
    switch (sceneIndex) {
    case 0: loaded = levelCompleteScene_.load(script, Scene0); break;
    case 1: loaded = levelCompleteScene_.load(script, Scene1); break;
    case 2: loaded = levelCompleteScene_.load(script, Scene2); break;
    case 3: loaded = levelCompleteScene_.load(script, Scene3); break;
    case 4: loaded = levelCompleteScene_.load(script, Scene4); break;
    case 5: loaded = levelCompleteScene_.load(script, Scene5); break;
    default: break;
    }
    if (!loaded) return false;

    // The production loader installs the jobs while the target page is
    // covered, clears that page to black, opens the Wipe, and only then allows
    // the first 10 Hz public scene callback. Keep the VM at tick zero here.
    resetLevelCompleteSurface();
    return levelCompleteScene_.valid();
}

void WordGame::resetLevelCompleteSurface() {
    sceneSurfacePixels_.assign(
        static_cast<std::size_t>(Renderer::Width) * Renderer::Height, Colors::Black);
}

void WordGame::advanceLevelCompleteScene() {
    levelCompleteScene_.tick();
    applyLevelCompletePaintEvents();
}

void WordGame::applyLevelCompletePaintEvents() {
    if (!levelCompleteScene_.valid()) return;
    if (sceneSurfacePixels_.size() !=
        static_cast<std::size_t>(Renderer::Width) * Renderer::Height) {
        resetLevelCompleteSurface();
    }

    Renderer surface(assets_.graphicsMode());
    surface.replacePixels(sceneSurfacePixels_);
    const bool cgaScene = assets_.graphicsMode() == GraphicsMode::Cga4;
    const Image* scenePalette = assets_.image(hallPaletteActive_ ? 6003u : 6009u);
    const bool remapSceneTitlePalette = !hallPaletteActive_;
    const bool remapSceneHallPalette = hallPaletteActive_;
    const auto floorCgaByte = [](const int x) {
        const int remainder = x % 4;
        return x - (remainder >= 0 ? remainder : remainder + 4);
    };
    const auto actorDestinationX = [&](const SceneActor& actor) {
        if (!cgaScene) return actor.x;
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(sceneGraphicId_), actor.frame);
        return floorCgaByte(actor.x) + (frame ? frame->x % 4 : 0);
    };
    const auto actorWidth = [&](const SceneActor& actor) {
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(sceneGraphicId_), actor.frame);
        if (!frame) return 0;
        int width = frame->width;
        if (cgaScene) {
            // The CGA PCXF driver copies complete packed four-pixel bytes.
            // BTMP rectangles retain inclusive source coordinates, so the
            // exposed width stops at the final complete source byte.
            width -= (frame->x + width) % 4;
        }
        return width;
    };
    const auto measuredCgaOffscreenRecord = [&](const SceneActor& actor) {
        // Word scene 4 starts record 4 at x=324. The packed CGA copy has no
        // addressable screen byte there, whereas the VGA rectangle clipper
        // saturates the record to x=319. Its next x=316 frame is visible.
        return cgaScene && sceneGraphicId_ == 2009 && actor.frame == 4 &&
               actorDestinationX(actor) >= Renderer::Width;
    };
    const auto clearActorRectangle = [&](const SceneActor& actor) {
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(sceneGraphicId_), actor.frame);
        if (!frame || measuredCgaOffscreenRecord(actor)) return;
        int x = actorDestinationX(actor);
        int y = actor.y;
        if (x >= Renderer::Width) x = Renderer::Width - 1;
        if (y >= Renderer::Height) y = Renderer::Height - 1;
        const int width = actorWidth(actor);
        if (width <= 0) return;
        surface.fillRect(x, y, width, frame->height, Colors::Black);
    };
    const auto redrawActiveActors = [&] {
        const auto& actors = levelCompleteScene_.actors();
        for (auto iterator = actors.rbegin(); iterator != actors.rend(); ++iterator) {
            if (!iterator->visible || iterator->ended ||
                measuredCgaOffscreenRecord(*iterator)) continue;
            drawSprite(surface, static_cast<std::uint32_t>(sceneGraphicId_),
                       iterator->frame, actorDestinationX(*iterator), iterator->y,
                       remapSceneTitlePalette, remapSceneHallPalette,
                       scenePalette, false, true, false,
                       cgaScene ? actorWidth(*iterator) : -1);
        }
    };

    // WM 0x06C3F first advances every SCPT object, then visits the changed
    // objects in construction order. A dirty rectangle is cleared to the
    // scene's black backing store and only still-active objects are repainted.
    // Pixels left by opcode 0E are therefore retained until a later dirty
    // rectangle crosses them; they are not permanent logical sprites.
    for (const ScenePaintEvent& event : levelCompleteScene_.paintEvents()) {
        if (!event.dirty) continue;
        const bool previousVisible = event.previous.visible && !event.previous.ended;
        const bool currentVisible = event.current.visible && !event.current.ended;
        if (!previousVisible && !currentVisible) continue;
        if (previousVisible) clearActorRectangle(event.previous);
        if (currentVisible) clearActorRectangle(event.current);
        redrawActiveActors();
    }
    sceneSurfacePixels_ = surface.pixels();
}

void WordGame::finishLevelCompleteScene() {
    captureBoardPresentationSource();
    sceneEffectPlayer_.stop();
    pendingCartoonScene_ = -1;
    // The helper at 0x11640 submits GSND 0 and waits 15 ms; outer teardown at
    // 0x0FFBE submits it again. Keep both resident calls without a chip reset.
    attractOplPlayer_.stopAllChannels();
    attractOplPlayer_.stopAllChannelsAfter(15);
    if (core_.cartoonPending()) (void)core_.finishCartoon();
    sceneTickAccumulator_ = 0.0;
    if (core_.advanceBoard()) {
        pointerCellQueue_.clear();
        moving_ = false;
        munching_ = false;
        initializeBoardRuntime();
        page_ = WordGamePage::Playing;
        beginBoardPresentation();
    } else {
        page_ = WordGamePage::Hall;
    }
}

void WordGame::playSceneEvent(const std::uint16_t event) {
    // The host callback adds three before indexing the active scene bank.
    if (!soundOn_ || event > 0xfcu || sceneAudioBank_ < 1) return;
    const std::uint8_t stream = static_cast<std::uint8_t>(event + 3);
    const BlobView sound = assets_.gameArchive().find(
        speakerEffects_ ? "PSND" : "ADLI", static_cast<std::uint32_t>(sceneAudioBank_));
    if (!sound) return;
    if (!speakerEffects_) {
        MeccSound decoded = decodeMeccGSound(sound, stream, MeccSoundProfile::WordMunchers);
        if (decoded.valid &&
            (attractOplPlayer_.playing() || attractOplPlayer_.startIdle())) {
            (void)attractOplPlayer_.playEffect(
                std::move(decoded.writes), decoded.durationMilliseconds);
        }
        return;
    }
    std::vector<std::uint8_t> wave = speakerEffects_
        ? renderMeccSpeakerSoundToWave(sound, stream, 40, MeccSoundProfile::WordMunchers)
        : renderMeccGSoundToWave(sound, stream, 300, MeccSoundProfile::WordMunchers);
    if (!wave.empty()) (void)sceneEffectPlayer_.play(std::move(wave));
}

void WordGame::playCartoonScore() {
    // Word's 0x0fee0 event A6 follows the same high-bit resident route as
    // Number: music-enabled AdLib stream 38, with no PC-speaker substitute.
    if (!musicOn_ || speakerEffects_ || sceneAudioBank_ < 1) return;
    const BlobView sound = assets_.gameArchive().find(
        "ADLI", static_cast<std::uint32_t>(sceneAudioBank_));
    if (!sound) return;
    MeccSound decoded = decodeMeccGSound(
        sound, 38, MeccSoundProfile::WordMunchers);
    if (!decoded.valid) return;
    (void)attractOplPlayer_.playScoreOnce(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

void WordGame::playGameplaySound(const std::uint8_t stream) {
    if (!soundOn_) return;
    const BlobView sound = assets_.gameArchive().find("GSND", 2);
    if (!sound) return;
    bool submitted = false;
    if (!speakerEffects_) {
        MeccSound decoded = decodeMeccGSound(sound, stream, MeccSoundProfile::WordMunchers);
        if (decoded.valid &&
            (attractOplPlayer_.playing() || attractOplPlayer_.startIdle())) {
            submitted = attractOplPlayer_.playEffect(
                std::move(decoded.writes), decoded.durationMilliseconds);
        }
    } else {
        std::vector<std::uint8_t> wave = speakerEffects_
            ? renderMeccSpeakerSoundToWave(sound, stream, 40, MeccSoundProfile::WordMunchers)
            : renderMeccGSoundToWave(sound, stream, 300, MeccSoundProfile::WordMunchers);
        submitted = !wave.empty() && sceneEffectPlayer_.play(std::move(wave));
    }
    if (submitted) {
        previousGameplaySound_ = lastGameplaySound_;
        lastGameplaySound_ = stream;
    }
}

void WordGame::playToggleFeedback(const bool enabled,
                                  const std::uint32_t startDelayMilliseconds) {
    const BlobView sound = assets_.gameArchive().find("GSND", 2);
    if (!sound) return;
    const std::uint8_t stream = enabled ? 2 : 1;
    if (speakerEffects_) {
        std::vector<std::uint8_t> wave = renderMeccSpeakerSoundToWave(
            sound, stream, 40, MeccSoundProfile::WordMunchers,
            startDelayMilliseconds);
        if (!wave.empty()) (void)sceneEffectPlayer_.play(std::move(wave));
        return;
    }
    MeccSound decoded = decodeMeccGSound(
        sound, stream, MeccSoundProfile::WordMunchers);
    if (!decoded.valid) return;
    for (OplWrite& write : decoded.writes) {
        write.milliseconds += startDelayMilliseconds;
    }
    decoded.durationMilliseconds += startDelayMilliseconds;
    if (!attractOplPlayer_.playing() && !attractOplPlayer_.startIdle()) return;
    (void)attractOplPlayer_.playEffect(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

bool WordGame::adjustJoystickRepeat(const wchar_t character) {
    if (!(joystickEnabled_ || joystickCalibrated_)) return false;
    const bool faster = character == L'+' || character == L'=';
    const bool slower = character == L'-' || character == L'_';
    if (!faster && !slower) return false;
    if (faster && joystickRepeatTicks_ > 1) --joystickRepeatTicks_;
    if (slower && joystickRepeatTicks_ < 10) ++joystickRepeatTicks_;
    return true;
}

void WordGame::playJoystickRepeatFeedback() {
    // Word's 0xDFB1 helper is byte-isomorphic to Number's direct PIT path.
    lastJoystickRepeatFeedbackHz_ = 1000 - 50 * joystickRepeatTicks_;
    std::vector<std::uint8_t> wave = renderPcSpeakerToneToWave(
        static_cast<std::uint16_t>(lastJoystickRepeatFeedbackHz_), 50);
    if (!wave.empty()) (void)sceneEffectPlayer_.play(std::move(wave));
}

int WordGame::difficultyIndexForPressure(const int pressure) {
    return std::clamp(pressure, 0, 11);
}

int WordGame::maximumEnemiesForPressure(const int pressure) {
    return EnemyLimits[static_cast<std::size_t>(difficultyIndexForPressure(pressure))];
}

int WordGame::maximumSafeZoneJobsForPressure(const int pressure) {
    return SafeZoneJobLimits[static_cast<std::size_t>(difficultyIndexForPressure(pressure))];
}

int WordGame::initialSafeZonesForPressure(const int pressure) {
    return InitialSafeZoneCounts[static_cast<std::size_t>(difficultyIndexForPressure(pressure))];
}

int WordGame::enemyWeightForPressure(const int pressure, const int enemyType) {
    if (enemyType < 0 || enemyType >= 5) return 0;
    return EnemyTypeWeights[static_cast<std::size_t>(difficultyIndexForPressure(pressure))]
                           [static_cast<std::size_t>(enemyType)];
}

double WordGame::enemySpawnDelay() {
    const int tier = difficultyIndexForPressure(core_.pressureTier());
    const int ticks = EnemyArrivalTicks[static_cast<std::size_t>(tier)] + random_.range(90);
    return static_cast<double>(ticks) / OriginalSceneTicksPerSecond;
}

double WordGame::enemyDwellDelay() {
    // The inclusive scheduler deadline makes a newly installed 30..119 value
    // eligible after one additional public tick.
    return static_cast<double>(31 + random_.range(90)) / OriginalSceneTicksPerSecond;
}

double WordGame::enemyMoveAnimationDuration(const int direction) const {
    const int tier = difficultyIndexForPressure(core_.pressureTier());
    const int callbacks = direction == 0 || direction == 2 ? 5 : 6;
    return static_cast<double>(callbacks * EnemyMoveTicks[static_cast<std::size_t>(tier)]) /
           OriginalSceneTicksPerSecond;
}

int WordGame::enemyMovementPhase(const Enemy& enemy) const {
    const bool vertical = enemy.direction == 0 || enemy.direction == 2;
    const int stepCount = vertical ? 5 : 6;
    const int tier = difficultyIndexForPressure(core_.pressureTier());
    const double interval =
        static_cast<double>(EnemyMoveTicks[static_cast<std::size_t>(tier)]) /
        OriginalSceneTicksPerSecond;
    // Entry state 3 owns exactly the five vertical or six horizontal move
    // callbacks.  The seeded DOS scheduler trace enters state 3 at call 277
    // with the Demo record at countdown 6, then reaches the dwell draw at
    // call 281 after that axis budget; an extra entry endpoint interval made
    // later selector-1/selector-5 ties consume the RNG in the wrong order.
    // Only a vertical exit has the separately recovered terminal callback.
    const bool hasTerminalCallback = enemy.exiting && vertical;
    const double total = enemyMoveAnimationDuration(enemy.direction) +
                         (hasTerminalCallback ? interval : 0.0);
    const double elapsed = std::max(0.0, total - std::max(0.0, enemy.animationTimer));
    const int storedPhase = static_cast<int>(
        std::floor(elapsed / interval + 1e-7));
    // State 3 advances before painting for both entry and ordinary movement.
    // Phase 0 is a logical setup state, not a visible actor position.
    return std::clamp(storedPhase + 1, 0, stepCount);
}

int WordGame::ordinaryEnemyFrame(const Enemy& enemy) {
    const int direction = std::clamp(enemy.direction, 0, 3);
    const int firstFrame = direction * 3;
    // Word retains the same actor-record writes as Number: entry holds the
    // first directional record, ordinary movement holds the third, and dwell
    // holds the middle. Record 15 is the exact down-dwell alias of record 7.
    if (enemy.entering) return firstFrame;
    if (enemy.moving) return firstFrame + 2;
    if (enemy.dwellFrame >= 0) return enemy.dwellFrame;
    return direction == 2 ? 15 : firstFrame + 1;
}

int WordGame::troggleBiteFrameAtTick(const int elapsedTicks) {
    // WM's state-5 record has the same tick-by-tick BTMP cycle as Number.
    // The lossless live collision confirms one visible pose change per
    // scheduler tick rather than progressively longer frame holds.
    if (elapsedTicks < 0) return 12;
    constexpr std::array<int, 6> BiteFrames = {12, 13, 14, 13, 12, 13};
    return BiteFrames[static_cast<std::size_t>(elapsedTicks) % BiteFrames.size()];
}

int WordGame::chooseEnemyType() {
    int choice = random_.range(100);
    const auto& weights = EnemyTypeWeights[
        static_cast<std::size_t>(difficultyIndexForPressure(core_.pressureTier()))];
    for (int type = 0; type < static_cast<int>(weights.size()); ++type) {
        if (choice < weights[static_cast<std::size_t>(type)]) return type;
        choice -= weights[static_cast<std::size_t>(type)];
    }
    return 0;
}

int WordGame::steeredEnemyDirection(const int enemyType, const int currentDirection,
                                    const int playerDistancePixels,
                                    const int directionTowardPlayer,
                                    const int randomRoll) {
    const int direction = ((currentDirection % 4) + 4) % 4;
    const int toward = ((directionTowardPlayer % 4) + 4) % 4;
    const auto turn = [](const int value, const int amount) {
        return (value + amount + 4) % 4;
    };
    switch (enemyType) {
    case 0: return direction;
    case 1:
    case 3:
        if (randomRoll == 0) return turn(direction, -1);
        if (randomRoll == 1) return turn(direction, 1);
        return direction;
    case 2:
        if (playerDistancePixels <= 96) return turn(toward, 2);
        if (randomRoll < 2) return turn(direction, 1);
        if (randomRoll < 4) return turn(direction, -1);
        if (randomRoll < 6) return turn(direction, 2);
        return direction;
    case 4:
        if (playerDistancePixels <= 96) return toward;
        if (playerDistancePixels <= 192 && randomRoll == 0) return toward;
        return direction;
    default: return direction;
    }
}

int WordGame::chooseSafeZoneCell() {
    for (;;) {
        const int row = 1 + random_.range(BoardRows - 2);
        const int column = 1 + random_.range(BoardColumns - 2);
        const int index = row * BoardColumns + column;
        if (!safeCells_[static_cast<std::size_t>(index)]) return index;
    }
}

void WordGame::initializeSafeZones() {
    safeCells_.fill(false);
    for (SafeZoneJob& job : safeZoneJobs_) job = {};
    safeZoneJobCount_ = maximumSafeZoneJobsForPressure(core_.pressureTier());
    const int initiallyActive = initialSafeZonesForPressure(core_.pressureTier());
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        const int ticks = 210 + random_.range(210);
        job.period = static_cast<double>(ticks) / OriginalSceneTicksPerSecond;
        // The seeded 0x8E74 live Demo establishes the board-start phase that
        // static record values alone cannot show: safe-zone records have
        // accrued one public scheduler tick by the time the later selector-5
        // Demo record begins its countdown.  At their first shared deadline,
        // DOS therefore spends calls 275/276 on the safe cell and call 277 on
        // the Demo reload.  Starting both clocks at their literal values made
        // native spend call 275 on Demo one dispatch too early and permanently
        // changed the safe cell and subsequent trajectory.  Reloads retain the
        // exact stored period; this offset applies only to board insertion.
        job.timer = std::max(
            0.0, job.period - 1.0 / OriginalSceneTicksPerSecond);
        if (index < initiallyActive) {
            job.active = true;
            job.cellIndex = chooseSafeZoneCell();
            safeCells_[static_cast<std::size_t>(job.cellIndex)] = true;
        }
    }
}

bool WordGame::updateSafeZones(const double seconds) {
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        job.timer -= seconds;
        while (job.timer <= JobTimerEpsilon) {
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
                    if (!applyEnemyCellEffect(enemy)) return false;
                    rearmEnemySlot(enemy.slot);
                    enemies_.erase(enemies_.begin() + static_cast<std::ptrdiff_t>(enemyIndex));
                    playGameplaySound(11);
                }
                if (playerRecovering_ && !enemyAt(playerRow_, playerColumn_)) {
                    playerRecovering_ = false;
                }
                playGameplaySound(4);
            }
            job.timer += job.period;
        }
    }
    return true;
}

void WordGame::initializeEnemySlots() {
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    enemySlotCount_ = maximumEnemiesForPressure(core_.pressureTier());
    for (int index = 0; index < enemySlotCount_; ++index) {
        EnemySlot& slot = enemySlots_[static_cast<std::size_t>(index)];
        slot.type = chooseEnemyType();
        slot.phase = EnemySlotPhase::Waiting;
    }
    for (int index = 0; index < enemySlotCount_; ++index) {
        enemySlots_[static_cast<std::size_t>(index)].timer = std::max(
            0.0, enemySpawnDelay() - EnemyArrivalJobPhaseAdvance);
    }
    enemyWarning_ = false;
}

bool WordGame::beginEnemyWarning(const int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return false;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    if (slot.phase != EnemySlotPhase::Waiting) return false;
    const int edge = random_.range(4);
    const int row = edge == 0 ? BoardRows - 1 : edge == 2 ? 0 : random_.range(BoardRows);
    const int column = edge == 1 ? 0 : edge == 3 ? BoardColumns - 1
                                                    : random_.range(BoardColumns);
    slot.edge = edge;
    slot.row = row;
    slot.column = column;
    slot.phase = EnemySlotPhase::Warning;
    slot.timer = TroggleWarningDuration;
    Enemy enemy;
    enemy.row = row;
    enemy.column = column;
    enemy.type = slot.type;
    enemy.direction = edge == 0 ? 0 : edge == 1 ? 1 : edge == 2 ? 2 : 3;
    enemy.fromRow = row + (edge == 0 ? 1 : edge == 2 ? -1 : 0);
    enemy.fromColumn = column + (edge == 1 ? -1 : edge == 3 ? 1 : 0);
    enemy.entering = true;
    enemy.slot = slotIndex;
    enemies_.push_back(enemy);
    enemyWarning_ = true;
    playGameplaySound(9);
    return true;
}

void WordGame::spawnPendingEnemy(const int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    if (slot.phase != EnemySlotPhase::Warning) return;
    const auto pending = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& enemy) {
        return enemy.slot == slotIndex && enemy.entering;
    });
    if (pending == enemies_.end()) return;
    pending->moving = true;
    // The warning callback changes this persistent selector-1 record directly
    // to state 3.  Its installed movement budget has no additional dormant
    // endpoint interval; dwell is selected by the axis terminal callback.
    pending->animationTimer = enemyMoveAnimationDuration(pending->direction);
    slot.phase = EnemySlotPhase::Active;
    slot.timer = 0.0;
    playGameplaySound(3);
}

void WordGame::rearmEnemySlot(const int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    slot.phase = EnemySlotPhase::Waiting;
    slot.timer = std::max(
        0.0, enemySpawnDelay() - EnemyArrivalJobPhaseAdvance);
}

void WordGame::beginEnemyMove(Enemy& enemy, const int row, const int column) {
    enemy.fromRow = enemy.row;
    enemy.fromColumn = enemy.column;
    if (row < enemy.row) enemy.direction = 0;
    else if (column > enemy.column) enemy.direction = 1;
    else if (row > enemy.row) enemy.direction = 2;
    else enemy.direction = 3;
    enemy.row = row;
    enemy.column = column;
    enemy.dwellFrame = -1;
    enemy.exiting = row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns;
    enemy.moving = true;
    enemy.animationTimer = enemyMoveAnimationDuration(enemy.direction);
    if (enemy.exiting && (enemy.direction == 0 || enemy.direction == 2)) {
        const int tier = difficultyIndexForPressure(core_.pressureTier());
        enemy.animationTimer +=
            static_cast<double>(EnemyMoveTicks[static_cast<std::size_t>(tier)]) /
            OriginalSceneTicksPerSecond;
    }
}

bool WordGame::safeAt(const int row, const int column) const {
    return row >= 0 && row < BoardRows && column >= 0 && column < BoardColumns &&
           safeCells_[static_cast<std::size_t>(row * BoardColumns + column)];
}

bool WordGame::moveEnemy(Enemy& enemy) {
    constexpr std::array<int, 4> RowDeltas = {-1, 0, 1, 0};
    constexpr std::array<int, 4> ColumnDeltas = {0, 1, 0, -1};
    if (isActiveBoardPage(page_) && enemy.row >= 0 && enemy.row < BoardRows &&
        enemy.column >= 0 && enemy.column < BoardColumns) {
        if (!applyEnemyCellEffect(enemy)) return false;
    }

    int playerPixelX = playerColumn_ * BoardCellWidth;
    int playerPixelY = playerRow_ * BoardCellHeight;
    if (moving_) {
        const int phase = playerMovementPhase();
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
    const int toward = horizontal >= vertical
        ? (enemyPixelX > playerPixelX ? 3 : 1)
        : (enemyPixelY > playerPixelY ? 0 : 2);
    const int distance = horizontal + vertical;
    int roll = 9;
    if (enemy.type == 1 || enemy.type == 3 || (enemy.type == 2 && distance > 96)) {
        roll = random_.range(10);
    } else if (enemy.type == 4 && distance > 96 && distance <= 192) {
        roll = random_.range(2);
    }
    enemy.direction = steeredEnemyDirection(enemy.type, enemy.direction, distance, toward, roll);
    enemy.collisionHidden = false;
    // Word's instruction-equivalent 0x09D3D -> 0x09C63 branch has the same
    // unbounded protected-cell retry as Number.
    for (;;) {
        const int row = enemy.row + RowDeltas[static_cast<std::size_t>(enemy.direction)];
        const int column = enemy.column + ColumnDeltas[static_cast<std::size_t>(enemy.direction)];
        if (row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns ||
            !safeAt(row, column)) {
            beginEnemyMove(enemy, row, column);
            return true;
        }
        enemy.direction = (enemy.direction + (random_.range(2) == 0 ? 3 : 1)) % 4;
    }
}

bool WordGame::applyEnemyCellEffect(const Enemy& enemy) {
    if (enemy.row < 0 || enemy.row >= BoardRows || enemy.column < 0 ||
        enemy.column >= BoardColumns || !core_.active() || core_.boardComplete()) return false;
    const std::size_t index = static_cast<std::size_t>(enemy.row * BoardColumns + enemy.column);
    const WordBoardCell savedCell = enemy.savedCellValid
        ? enemy.savedCell : core_.board().cells[index];
    const bool savedEaten = enemy.savedCellValid ? enemy.savedCellEaten : core_.eaten(index);
    if (enemy.type == 4) {
        (void)core_.restoreCell(index, savedCell, savedEaten);
    } else if ((enemy.type == 0 || enemy.type == 2) && savedEaten) {
        (void)core_.clearCellWithoutScore(index);
    } else if (enemy.type == 3) {
        (void)core_.clearCellWithoutScore(index);
    } else {
        (void)core_.regenerateCell(index);
    }
    if (core_.completeBoardIfEmpty()) {
        handleCompletedBoard();
        return false;
    }
    return true;
}

bool WordGame::updateEnemies(const double seconds, const int slotUpperBound,
                             const int slotLowerBound) {
    const int dispatchSlotCount = slotUpperBound >= 0
        ? std::min(enemySlotCount_, slotUpperBound)
        : enemySlotCount_;
    const int dispatchSlotBegin = std::clamp(slotLowerBound, 0, dispatchSlotCount);
    for (Enemy& mover : enemies_) {
        if (mover.slot < dispatchSlotBegin || mover.slot >= dispatchSlotCount) continue;
        if (!mover.moving || mover.animationTimer - seconds > JobTimerEpsilon || mover.exiting ||
            mover.row < 0 || mover.row >= BoardRows || mover.column < 0 ||
            mover.column >= BoardColumns) continue;
        for (Enemy& resident : enemies_) {
            if (&resident == &mover || resident.entering || resident.moving ||
                resident.overlapFrozen || resident.cannibalizing ||
                resident.row != mover.row || resident.column != mover.column) continue;
            const bool residentStillDwelling = resident.moveTimer - seconds > JobTimerEpsilon;
            if (!residentStillDwelling && resident.slot < mover.slot) continue;
            resident.collisionHidden = true;
            resident.overlapFrozen = true;
            resident.overlapRetired = true;
        }
    }

    // One persistent selector-1 record owns every phase of each Troggle job.
    // Preserve physical job-ID order across mixed waiting/warning/actor states.
    for (int slotIndex = dispatchSlotBegin; slotIndex < dispatchSlotCount; ++slotIndex) {
        EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
        if (slot.phase == EnemySlotPhase::Waiting) {
            slot.timer -= seconds;
            if (slot.timer <= JobTimerEpsilon && !beginEnemyWarning(slotIndex)) slot.timer = 1.0;
            continue;
        }
        if (slot.phase == EnemySlotPhase::Warning) {
            slot.timer -= seconds;
            if (slot.timer <= JobTimerEpsilon) spawnPendingEnemy(slotIndex);
            continue;
        }
        if (slot.phase != EnemySlotPhase::Active) continue;

        auto actor = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& enemy) {
            return enemy.slot == slotIndex;
        });
        if (actor == enemies_.end()) continue;
        Enemy& enemy = *actor;
        if (deathAnimating_ && feedbackEnemySlot_ >= 0 && enemy.slot == feedbackEnemySlot_) {
            continue;
        }
        if (enemy.overlapFrozen) {
            continue;
        }
        if (enemy.cannibalizing) {
            enemy.cannibalTimer -= seconds;
            if (enemy.cannibalTimer > JobTimerEpsilon) {
                continue;
            }
            enemy.cannibalizing = false;
            enemy.cannibalTimer = 0.0;
            enemy.dwellFrame = 15;
            const std::size_t cellIndex = static_cast<std::size_t>(
                enemy.row * BoardColumns + enemy.column);
            enemy.savedCell = core_.board().cells[cellIndex];
            enemy.savedCellEaten = core_.eaten(cellIndex);
            enemy.savedCellValid = true;
            enemy.moveTimer = enemyDwellDelay();
            const int survivorRow = enemy.row;
            const int survivorColumn = enemy.column;
            for (int victimSlot = 0; victimSlot < enemySlotCount_; ++victimSlot) {
                if (victimSlot == slotIndex) continue;
                const auto victim = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& candidate) {
                        return candidate.slot == victimSlot && candidate.overlapRetired &&
                               candidate.row == survivorRow &&
                               candidate.column == survivorColumn;
                    });
                if (victim == enemies_.end()) continue;
                enemies_.erase(victim);
                rearmEnemySlot(victimSlot);
                playGameplaySound(11);
            }
            continue;
        }
        if (enemy.entering && !enemy.moving) {
            continue;
        }
        if (enemy.moving) {
            enemy.animationTimer -= seconds;
            if (enemy.animationTimer > JobTimerEpsilon) {
                continue;
            }
            const bool overlap = std::any_of(enemies_.begin(), enemies_.end(),
                [&](const Enemy& candidate) {
                    return &candidate != &enemy && candidate.overlapRetired &&
                           candidate.row == enemy.row && candidate.column == enemy.column;
                });
            enemy.moving = false;
            enemy.animationTimer = 0.0;
            if (enemy.exiting || enemy.row < 0 || enemy.row >= BoardRows ||
                enemy.column < 0 || enemy.column >= BoardColumns) {
                enemies_.erase(actor);
                rearmEnemySlot(slotIndex);
                if (playerRecovering_ && !enemyAt(playerRow_, playerColumn_)) {
                    playerRecovering_ = false;
                }
                continue;
            }
            enemy.entering = false;
            const std::size_t cellIndex = static_cast<std::size_t>(
                enemy.row * BoardColumns + enemy.column);
            enemy.savedCell = core_.board().cells[cellIndex];
            enemy.savedCellEaten = core_.eaten(cellIndex);
            enemy.savedCellValid = true;
            enemy.dwellFrame = enemy.direction == 2 ? 15 : enemy.direction * 3 + 1;
            enemy.moveTimer = enemyDwellDelay();
            if (overlap) {
                enemy.cannibalizing = true;
                enemy.cannibalTimer = TroggleCannibalAnimationDuration;
            }
            if (playerCanBeCaughtAtEnemyEndpoint() && enemy.row == playerRow_ &&
                enemy.column == playerColumn_) {
                beginTroggleCollision(enemy.type, enemy.slot);
                continue;
            }
            if (playerRecovering_ && !enemyAt(playerRow_, playerColumn_)) {
                playerRecovering_ = false;
            }
            continue;
        }
        enemy.moveTimer -= seconds;
        if (enemy.moveTimer <= JobTimerEpsilon) {
            if (!moveEnemy(enemy)) return false;
            if (!enemy.moving) enemy.moveTimer = enemyDwellDelay();
        }
    }
    enemyWarning_ = std::any_of(enemySlots_.begin(), enemySlots_.begin() + enemySlotCount_,
        [](const EnemySlot& slot) { return slot.phase == EnemySlotPhase::Warning; });
    return true;
}

void WordGame::beginTroggleCollision(const int enemyType, const int enemySlot) {
    if (deathAnimating_ || playerRecovering_ || enemyType < 0) return;
    playGameplaySound(12);
    moving_ = false;
    moveTimer_ = 0.0;
    playerTerminalFrame_ = -1;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    pointerCellQueue_.clear();
    lastResolution_ = {};
    feedbackEnemyType_ = enemyType;
    feedbackEnemySlot_ = enemySlot;
    feedbackMessage_.clear();
    deathAnimating_ = true;
    deathSequenceTicks_ = 0;
    playerRecovering_ = false;
    bool selectedActor = false;
    for (Enemy& enemy : enemies_) {
        if (enemy.row != playerRow_ || enemy.column != playerColumn_) continue;
        const bool selected = !selectedActor && (enemySlot < 0 || enemy.slot == enemySlot);
        if (selected) {
            selectedActor = true;
            enemy.collisionHidden = false;
            enemy.overlapFrozen = false;
            enemy.overlapRetired = false;
            feedbackEnemySlot_ = enemy.slot;
        } else {
            enemy.collisionHidden = true;
            enemy.overlapFrozen = true;
            enemy.overlapRetired = true;
        }
    }
    page_ = WordGamePage::Feedback;
    if (feedbackEnemySlot_ >= 0) {
        const int selectedSlot = feedbackEnemySlot_;
        deathResidentSurfacePixels_.clear();
        feedbackEnemySlot_ = enemySlotCount_ + 1;
        enqueuePresentationFrame(capturePresentationFrame());
        feedbackEnemySlot_ = selectedSlot;
    }
}

void WordGame::updateDeathAnimation(const double seconds) {
    constexpr double tickSeconds = 1.0 / OriginalSceneTicksPerSecond;
    gameplayTickAccumulator_ += seconds * OriginalSceneTicksPerSecond;
    while (gameplayTickAccumulator_ >= 1.0 && deathAnimating_) {
        gameplayTickAccumulator_ -= 1.0;
        const int biteTickBeforeDispatch = deathSequenceTicks_;
        const bool firstBiteCallback = biteTickBeforeDispatch == 0;
        const std::vector<std::uint32_t> beforeFirstBitePixels =
            firstBiteCallback ? capturePresentationFrame()
                              : std::vector<std::uint32_t>{};
        if (!updateSafeZones(tickSeconds)) continue;
        const bool hasPhysicalBiter =
            feedbackEnemySlot_ >= 0 && feedbackEnemySlot_ < enemySlotCount_;
        const int biterSlot = hasPhysicalBiter
            ? feedbackEnemySlot_ : enemySlotCount_;
        if (deathAnimating_ &&
            !updateEnemies(tickSeconds, biterSlot)) continue;
        const bool abortAfterBiter =
            biteTickBeforeDispatch >= TroggleEatAnimationTicks - 2;
        const auto laterMovingPaintAdvances = [&](const Enemy& enemy) {
            if (enemy.slot <= biterSlot || enemy.slot >= enemySlotCount_ ||
                !enemy.moving) return false;
            Enemy advanced = enemy;
            advanced.animationTimer -= tickSeconds;
            return enemyMovementPhase(advanced) != enemyMovementPhase(enemy);
        };
        const bool laterMovingCallbackDue =
            hasPhysicalBiter && !abortAfterBiter &&
            std::any_of(enemies_.begin(), enemies_.end(),
                        laterMovingPaintAdvances);
        const bool laterEntryPhaseOneCallback =
            laterMovingCallbackDue &&
            std::any_of(enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                return enemy.entering && laterMovingPaintAdvances(enemy) &&
                       enemyMovementPhase(enemy) == 1;
            });
        ++deathSequenceTicks_;
        const std::vector<std::uint32_t> afterBiterPixels =
            laterMovingCallbackDue &&
                    deathSequenceTicks_ < TroggleEatAnimationTicks
                ? capturePresentationFrame()
                : std::vector<std::uint32_t>{};
        if (deathAnimating_ && hasPhysicalBiter && !abortAfterBiter &&
            !updateEnemies(tickSeconds, -1, biterSlot + 1)) continue;
        bool deferredDemoAtBiteTerminal = false;
        if (attractMode_ && deathAnimating_) {
            attractActionTimer_ -= tickSeconds;
            // Word's selector-5 record is byte-identical to Number's and is
            // appended after the collision actor. If both become due at the
            // terminal boundary, the bite removes the Muncher first. Carry a
            // due selector across the final pre-terminal interval; after the
            // terminal it performs only its mandatory reload draw.
            deferredDemoAtBiteTerminal =
                attractActionTimer_ <= JobTimerEpsilon &&
                biteTickBeforeDispatch >= TroggleEatAnimationTicks - 2;
            if (!deferredDemoAtBiteTerminal) updateAttractPlayer();
        }
        const std::vector<std::uint32_t> afterAllCallbacksPixels =
            deathSequenceTicks_ < TroggleEatAnimationTicks
                ? capturePresentationFrame()
                : std::vector<std::uint32_t>{};
        if (!afterBiterPixels.empty() &&
            afterBiterPixels != afterAllCallbacksPixels) {
            enqueuePresentationFrame(afterBiterPixels);
        }
        const bool retainAfterBiterUntilNextCallback =
            laterEntryPhaseOneCallback && !afterBiterPixels.empty() &&
            afterBiterPixels != afterAllCallbacksPixels;
        if (firstBiteCallback && !beforeFirstBitePixels.empty()) {
            const std::vector<std::uint32_t>& advancedBitePixels =
                afterAllCallbacksPixels;
            deathSequenceTicks_ = 0;
            const std::vector<std::uint32_t> priorBitePixels =
                capturePresentationFrame();
            deathSequenceTicks_ = 1;
            std::vector<std::uint32_t> residentBitePixels = beforeFirstBitePixels;
            for (std::size_t pixel = 0; pixel < residentBitePixels.size(); ++pixel) {
                if (advancedBitePixels[pixel] != priorBitePixels[pixel]) {
                    residentBitePixels[pixel] = advancedBitePixels[pixel];
                }
            }
            deathResidentSurfacePixels_ = std::move(residentBitePixels);
        } else if (deathSequenceTicks_ < TroggleEatAnimationTicks) {
            deathResidentSurfacePixels_ = retainAfterBiterUntilNextCallback
                ? afterBiterPixels : afterAllCallbacksPixels;
        }
        if (deathSequenceTicks_ < TroggleEatAnimationTicks) continue;
        deathAnimating_ = false;
        deathResidentSurfacePixels_.clear();
        const auto biter = std::find_if(enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
            return enemy.slot == feedbackEnemySlot_;
        });
        if (biter != enemies_.end()) {
            const std::size_t cellIndex = static_cast<std::size_t>(
                biter->row * BoardColumns + biter->column);
            biter->savedCell = core_.board().cells[cellIndex];
            biter->savedCellEaten = core_.eaten(cellIndex);
            biter->savedCellValid = true;
            biter->dwellFrame = 15;
            biter->moveTimer = enemyDwellDelay();
        }
        lastResolution_.gameOver = core_.loseMuncher();
        playGameplaySound(10);
        feedbackMessage_ = std::string(FailureExclamations[
            static_cast<std::size_t>(random_.range(static_cast<int>(FailureExclamations.size())))]);
        // 0x0a9e8 enters the common feedback painter before scanning extra
        // state-6 actors on the collision square. Demo and final-life paths
        // leave those records to the imminent terminal transition.
        if (!attractMode_ && !lastResolution_.gameOver) {
            for (std::size_t index = 0; index < enemies_.size();) {
                const Enemy& enemy = enemies_[index];
                if (!enemy.collisionHidden || enemy.row != playerRow_ ||
                    enemy.column != playerColumn_) {
                    ++index;
                    continue;
                }
                const int slot = enemy.slot;
                enemies_.erase(enemies_.begin() + static_cast<std::ptrdiff_t>(index));
                rearmEnemySlot(slot);
                playGameplaySound(11);
            }
        }
        if (attractMode_) {
            if (attractActionTimer_ <= JobTimerEpsilon) {
                attractActionTimer_ =
                    static_cast<double>(random_.range(30) + 15) /
                    OriginalSceneTicksPerSecond;
            }
            attractPostFeedbackBoard_ = false;
            attractTransitionTimer_ = AttractFeedbackDuration;
        }
    }
}

bool WordGame::enemyAt(const int row, const int column) const {
    return std::any_of(enemies_.begin(), enemies_.end(), [=](const Enemy& enemy) {
        return !enemy.entering && enemy.row == row && enemy.column == column;
    });
}

bool WordGame::enemyOwnsAttractPlayerCell(const int row, const int column) const {
    // Word's selector-5/job-ownership split is byte-isomorphic to Number's.
    // Entry phase 3 takes the Muncher scheduler cell, while the independent
    // signed answer array remains readable for choosing the discarded key.
    return std::any_of(enemies_.begin(), enemies_.end(), [=, this](const Enemy& enemy) {
        const bool ownsLiveCell = !enemy.entering ||
            (enemy.moving && enemyMovementPhase(enemy) >= 3);
        return ownsLiveCell && enemy.row == row && enemy.column == column;
    });
}

int WordGame::enemyTypeAt(const int row, const int column) const {
    const auto enemy = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& current) {
        return !current.entering && !current.collisionHidden &&
               current.row == row && current.column == column;
    });
    return enemy == enemies_.end() ? -1 : enemy->type;
}

int WordGame::enemySlotAt(const int row, const int column) const {
    const auto enemy = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& current) {
        return !current.entering && !current.collisionHidden &&
               current.row == row && current.column == column;
    });
    return enemy == enemies_.end() ? -1 : enemy->slot;
}

bool WordGame::enemyIntersectsCell(const Enemy& enemy, const int row, const int column) {
    if (enemy.row == row && enemy.column == column) return true;
    return (enemy.moving || enemy.entering) &&
           enemy.fromRow == row && enemy.fromColumn == column;
}

void WordGame::finishFeedback(const bool forcePostGame) {
    const bool postGame = forcePostGame || lastResolution_.gameOver ||
                          lastResolution_.maximumScore || !core_.active();
    playerRecovering_ = !postGame && feedbackEnemyType_ >= 0 && core_.active() &&
                        enemyAt(playerRow_, playerColumn_);
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    feedbackMessage_.clear();
    quitFromFeedback_ = false;
    if (postGame) beginPostGame();
    else page_ = WordGamePage::Playing;
    lastResolution_ = {};
}

void WordGame::beginQuitConfirm(const WordGamePage returnPage, const bool fromFeedback) {
    // Word's selector-state 4/5 Escape path reaches GSND 0 at 0x0DF87 before
    // opening the prompt. A cancelled prompt leaves that cue retired.
    attractOplPlayer_.stopAllChannels();
    sceneEffectPlayer_.stop();
    pointerCellQueue_.clear();
    quitReturnPage_ = returnPage;
    quitFromFeedback_ = fromFeedback;
    menuSelection_ = 1;
    page_ = WordGamePage::QuitConfirm;
}

void WordGame::pointerMove(const int x, const int y) {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (cheatOpen_ && x >= 50 && x <= 270 && y >= 82 && y < 128) {
        cheatSelection_ = std::clamp((y - 82) / 22, 0, 1);
    }
}

bool WordGame::pointerPress(const bool secondary) {
    if (exitPending_) return true;
    if (configurationWriteError_) return true;
    if (userPresentationActive()) return false;
    if (!secondary) return false;
    if (attractMode_) {
        stopAttract();
        return true;
    }
    if (page_ == WordGamePage::Playing || page_ == WordGamePage::Paused) {
        keyDown(VK_RETURN);
        return true;
    }
    if (page_ == WordGamePage::LevelComplete) {
        keyDown(VK_PROCESSKEY);
        return true;
    }
    return false;
}

void WordGame::pointerButton(const int x, const int y, const bool secondary) {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (page_ == WordGamePage::StartupVersion || page_ == WordGamePage::StartupSplash) {
        keyDown(VK_RETURN);
        return;
    }
    // Word's accepted-key waiter at image 0x0BF6A has the same DOS mouse
    // contract as Number's: event type 2 becomes Space, while type 13 keeps
    // code 0xFD and is not in the {Escape, Enter, Space} table.
    const bool acceptedKeyWaiter =
        page_ == WordGamePage::Information ||
        (page_ == WordGamePage::OptionsContent && contentInsufficientMessage_) ||
        (page_ == WordGamePage::OptionsVowels && vowelMessage_ != 0) ||
        page_ == WordGamePage::OptionsVowelsHelp ||
        page_ == WordGamePage::OptionsPreview ||
        (page_ == WordGamePage::OptionsEraseHall && eraseDraft_.empty() &&
         hall_.entries().empty()) ||
        (page_ == WordGamePage::OptionsPasswordPrompt && passwordRejected_) ||
        page_ == WordGamePage::OptionsJoystickCalibration ||
        page_ == WordGamePage::Hall ||
        page_ == WordGamePage::Feedback;
    if (secondary) {
        // The custom vowel loop handles left press/release but has no
        // right-release/0xFD branch. Fixed list widgets convert 0xFD to Enter.
        if (page_ == WordGamePage::OptionsVowels ||
            (!cheatOpen_ && acceptedKeyWaiter)) return;
        keyDown(VK_RETURN);
        return;
    }
    if (attractMode_) {
        stopAttract();
        return;
    }
    if (page_ == WordGamePage::Title) titleIdleTimer_ = 0.0;
    if (cheatOpen_) {
        pointerMove(x, y);
        if (cheatSelection_ == 0 && y >= 82 && y < 104) {
            cheatLevel_ = std::clamp(cheatLevel_ + (x < 160 ? -1 : 1), 1, 20);
        } else {
            handleCheatKey(VK_RETURN);
        }
        return;
    }
    if (acceptedKeyWaiter) {
        keyDown(VK_SPACE);
        return;
    }
    const auto selectOrActivate = [this](const int index) {
        if (index < 0) return;
        if (menuSelection_ == index) keyDown(VK_RETURN);
        else menuSelection_ = index;
    };
    const auto numberedItemAt = [this, x, y](const int left, const int firstBaseline,
                                             const auto& labels) {
        const GemFont& font = assets_.largeFont();
        for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
            const int baseline = firstBaseline + index * 11;
            const std::string display = " " + std::to_string(index + 1) + ". " +
                std::string(labels[static_cast<std::size_t>(index)]) + " ";
            if (x >= left && x <= left + textWidth(font, display) &&
                y >= baseline - 1 && y <= baseline + 8) {
                return index;
            }
        }
        return -1;
    };
    if (page_ == WordGamePage::Title) {
        selectOrActivate(numberedItemAt(49, 91, TitleLabels));
        return;
    }
    if (page_ == WordGamePage::Options) {
        std::array<std::string_view, OptionsLabels.size()> labels = OptionsLabels;
        labels[3] = joystickEnabled_ ? "Turn Joystick OFF" : "Turn Joystick ON ";
        selectOrActivate(numberedItemAt(50, 80, labels));
        return;
    }
    if (page_ == WordGamePage::OptionsContent) {
        selectOrActivate(numberedItemAt(50, 96, ContentLabels));
        return;
    }
    if (page_ == WordGamePage::OptionsDifficulty) {
        selectOrActivate(numberedItemAt(50, 64, DifficultyLabels));
        return;
    }
    if (page_ == WordGamePage::OptionsVowels && vowelMessage_ == 0) {
        // The custom loop at 0x145D7 waits for left release, hit-tests the
        // fourteen inclusive DS:1D22 rectangles, selects the hit item, and
        // immediately toggles its raw selection bit.
        for (int index = 0; index < static_cast<int>(VowelChoiceGeometry.size()); ++index) {
            const WordVowelChoiceGeometry& item =
                VowelChoiceGeometry[static_cast<std::size_t>(index)];
            if (x >= item.left && x <= item.right && y >= item.top && y <= item.bottom) {
                vowelSelection_ = index;
                toggleVowelChoice(index);
                return;
            }
        }
        return;
    }
    if (page_ == WordGamePage::OptionsEraseHall && !eraseDraft_.empty()) {
        std::vector<std::string_view> labels;
        labels.reserve(eraseDraft_.size());
        for (const WordHallScoreEntry& entry : eraseDraft_) labels.push_back(entry.name);
        const int item = numberedItemAt(40, 38, labels);
        if (item >= 0) {
            if (eraseSelection_ == item) keyDown(VK_SPACE);
            else eraseSelection_ = item;
        }
        return;
    }
    if (page_ == WordGamePage::InstructionsQuestion || page_ == WordGamePage::QuitConfirm ||
        page_ == WordGamePage::ReplayQuestion) {
        const int baseline = page_ == WordGamePage::QuitConfirm ? 110 : 105;
        if (y >= baseline - 1 && y <= baseline + 8) {
            if (x >= 115 && x <= 155) selectOrActivate(0);
            if (x >= 165 && x <= 197) selectOrActivate(1);
        }
        return;
    }
    if (page_ == WordGamePage::Playing && !deathAnimating_ &&
        x >= BoardLeft && x < BoardRight &&
        y >= BoardTop && y < BoardBottom) {
        const int row = (y - BoardTop) / BoardCellHeight;
        const int column = (x - BoardLeft) / BoardCellWidth;
        enqueuePlayerInput(-(row * BoardColumns + column));
        return;
    }
}

void WordGame::setJoystickState(const bool connected, const std::uint32_t x,
                                const std::uint32_t y, const std::uint32_t buttons) {
    if (!connected) {
        joystickConnected_ = false;
        joystickX_ = 0;
        joystickY_ = 0;
        joystickButtons_ = 0;
        joystickSampleCount_ = 0;
        joystickSampleIndex_ = 0;
        return;
    }
    if (!joystickConnected_) {
        joystickSampleCount_ = 0;
        joystickSampleIndex_ = 0;
    }
    joystickConnected_ = true;
    joystickX_ = x;
    joystickY_ = y;
    joystickButtons_ = buttons & 0x03u;
    joystickSampleX_[joystickSampleIndex_] = x;
    joystickSampleY_[joystickSampleIndex_] = y;
    joystickSampleIndex_ = (joystickSampleIndex_ + 1) % joystickSampleX_.size();
    joystickSampleCount_ = std::min(joystickSampleCount_ + 1, joystickSampleX_.size());
}

void WordGame::beginJoystickCalibration() {
    page_ = WordGamePage::OptionsJoystickCalibration;
    menuSelection_ = 4;
    bool detected = joystickConnected_ && joystickSampleCount_ != 0;
    for (std::size_t index = 0; detected && index < joystickSampleCount_; ++index) {
        detected = joystickSampleX_[index] != 0 && joystickSampleY_[index] != 0;
    }
    joystickCalibrationStep_ = detected ? 0 : -1;
    if (!detected) return;

    std::uint64_t sumX = 0;
    std::uint64_t sumY = 0;
    for (std::size_t index = 0; index < joystickSampleCount_; ++index) {
        sumX += joystickSampleX_[index];
        sumY += joystickSampleY_[index];
    }
    const std::uint32_t divisor = static_cast<std::uint32_t>(
        std::max<std::size_t>(1, joystickSampleCount_));
    joystickCalibrationCenterX_ = static_cast<std::uint32_t>(sumX / divisor);
    joystickCalibrationCenterY_ = static_cast<std::uint32_t>(sumY / divisor);
    joystickDraftLeftThreshold_ = joystickCalibrationCenterX_ - joystickCalibrationCenterX_ / 4;
    joystickDraftRightThreshold_ = joystickCalibrationCenterX_ + joystickCalibrationCenterX_ / 4;
    joystickDraftUpThreshold_ = joystickCalibrationCenterY_ - joystickCalibrationCenterY_ / 4;
    joystickDraftDownThreshold_ = joystickCalibrationCenterY_ + joystickCalibrationCenterY_ / 4;
}

void WordGame::acceptJoystickCalibration() {
    if (joystickCalibrationStep_ < 0) {
        if (joystickConnected_) beginJoystickCalibration();
        return;
    }
    switch (joystickCalibrationStep_) {
    case 0:
        joystickCalibrationCenterX_ = joystickX_;
        joystickCalibrationCenterY_ = joystickY_;
        break;
    case 1:
        joystickDraftLeftThreshold_ = joystickX_ <= joystickCalibrationCenterX_
            ? joystickCalibrationCenterX_ - (joystickCalibrationCenterX_ - joystickX_) / 2
            : joystickCalibrationCenterX_;
        break;
    case 2:
        joystickDraftRightThreshold_ = joystickX_ >= joystickCalibrationCenterX_
            ? joystickCalibrationCenterX_ + (joystickX_ - joystickCalibrationCenterX_) / 2
            : joystickCalibrationCenterX_;
        break;
    case 3:
        joystickDraftUpThreshold_ = joystickY_ <= joystickCalibrationCenterY_
            ? joystickCalibrationCenterY_ - (joystickCalibrationCenterY_ - joystickY_) / 2
            : joystickCalibrationCenterY_;
        break;
    case 4:
        joystickDraftDownThreshold_ = joystickY_ >= joystickCalibrationCenterY_
            ? joystickCalibrationCenterY_ + (joystickY_ - joystickCalibrationCenterY_) / 2
            : joystickCalibrationCenterY_;
        break;
    case 5:
        joystickLeftThreshold_ = joystickDraftLeftThreshold_;
        joystickRightThreshold_ = joystickDraftRightThreshold_;
        joystickUpThreshold_ = joystickDraftUpThreshold_;
        joystickDownThreshold_ = joystickDraftDownThreshold_;
        joystickCalibrated_ = true;
        saveSettings();
        page_ = WordGamePage::Options;
        menuSelection_ = 4;
        return;
    default:
        return;
    }
    ++joystickCalibrationStep_;
}

UINT WordGame::joystickDirectionKey() const {
    unsigned mask = 0;
    if (joystickY_ < joystickUpThreshold_) mask |= 0x01;
    if (joystickX_ > joystickRightThreshold_) mask |= 0x02;
    if (joystickY_ > joystickDownThreshold_) mask |= 0x04;
    if (joystickX_ < joystickLeftThreshold_) mask |= 0x08;
    switch (mask) {
    case 0x01: return VK_UP;
    case 0x02: return VK_RIGHT;
    case 0x04: return VK_DOWN;
    case 0x08: return VK_LEFT;
    default: return 0;
    }
}

void WordGame::updateJoystickInput(const double seconds) {
    if (seconds > 0.0) {
        joystickClockFraction_ += seconds * OriginalSceneTicksPerSecond;
        const double elapsedTicks = std::floor(joystickClockFraction_);
        joystickClockTicks_ += static_cast<std::uint64_t>(elapsedTicks);
        joystickClockFraction_ -= elapsedTicks;
    }

    const bool active = joystickEnabled_ && joystickConnected_ && joystickCalibrated_ &&
                        page_ != WordGamePage::OptionsJoystickCalibration;
    const std::uint32_t buttons = joystickButtons_ & 0x03u;
    if (!active) {
        joystickPreviousButtons_ = buttons;
        return;
    }

    const std::uint32_t pressed = buttons & ~joystickPreviousButtons_;
    const std::uint32_t released = joystickPreviousButtons_ & ~buttons;
    joystickPreviousButtons_ = buttons;
    if (released != 0) {
        return;
    }
    if ((buttons & 0x01u) != 0) {
        if ((pressed & 0x01u) != 0) keyDown(VK_SPACE);
        return;
    }
    if ((buttons & 0x02u) != 0) {
        if ((pressed & 0x02u) != 0) keyDown(VK_RETURN);
        return;
    }

    // Word's DS:24AE gate is isomorphic to Number's DS:17FE: buttons remain
    // live during a player movement job, but directional sampling/repeat
    // dispatch stays disabled until the movement terminal restores the gate;
    // the independent public clock above continues to advance.
    if (moving_) return;

    const std::uint64_t repeat = static_cast<std::uint64_t>(joystickRepeatTicks_);
    for (int poll = 0; poll < 2; ++poll) {
        if (joystickNextRepeatTicks_ < repeat ||
            joystickNextRepeatTicks_ - repeat > joystickCachedClockTicks_) {
            joystickNextRepeatTicks_ = joystickCachedClockTicks_ + repeat;
        }
        if (joystickCachedClockTicks_ >= joystickNextRepeatTicks_) {
            joystickCachedClockTicks_ = joystickClockTicks_;
            joystickNextRepeatTicks_ = joystickCachedClockTicks_ + repeat;
            const UINT pending = joystickPendingDirection_;
            joystickPendingDirection_ = 0;
            if (pending != 0) keyDown(pending);
            if (moving_) break;
            continue;
        }

        joystickCachedClockTicks_ = joystickClockTicks_;
        if (joystickCachedClockTicks_ <= repeat / 2) continue;
        const UINT direction = joystickDirectionKey();
        if (joystickPendingDirection_ != 0 &&
            direction == joystickLastSampledDirection_) continue;
        joystickPendingDirection_ = direction;
        joystickLastSampledDirection_ = direction;
    }
}

void WordGame::toggleCheatMenu() {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (attractMode_) stopAttract();
    if (page_ == WordGamePage::StartupVersion || page_ == WordGamePage::StartupSplash) {
        page_ = WordGamePage::Title;
        menuSelection_ = 0;
        titleIdleTimer_ = 0.0;
    }
    cheatOpen_ = !cheatOpen_;
    cheatSelection_ = 0;
    cheatLevel_ = core_.active() ? core_.level() : 1;
}

void WordGame::toggleSound() {
    if (!acceptsCommonDispatcherInput()) return;
    soundOn_ = !soundOn_;
    // Word's common driver is byte-isomorphic here: stream 1 acknowledges
    // disabled sound and stream 2 acknowledges enabled sound, bypassing the
    // changed flag and replacing only logical channel 0.
    playToggleFeedback(soundOn_);
    saveSettings();
}

void WordGame::toggleMusic() {
    if (!acceptsCommonDispatcherInput()) return;
    musicOn_ = !musicOn_;
    if (!musicOn_) {
        attractOplPlayer_.stopAllChannels();
        sceneEffectPlayer_.stop();
        // Entry 0 stops all driver channels; stream 1 follows after the
        // resident handler's literal 50 ms wait.
        playToggleFeedback(false, 50);
    } else if (attractMode_ &&
               (page_ == WordGamePage::Attract ||
                page_ == WordGamePage::Feedback) &&
               !speakerEffects_) {
        // Word's byte-isomorphic resident path at 0x0dea6 resumes event 0x90
        // only for a nonzero current device in selector state 2: board,
        // feedback, or board-to-Hall Wipe. Hall/Hall-to-logo are state 3 and
        // logo/logo-to-board are state 1.
        playAttractMusic();
    }
    if (musicOn_) playToggleFeedback(true);
    saveSettings();
    if (configurationWriteError_) {
        // The original Alt+M resumes the selector-state controller only after
        // its synchronous write-error alert has consumed a later key.
        configurationWriteErrorPostDismissContinuation_ = true;
    }
}

void WordGame::toggleSpeaker() {
    if (!acceptsCommonDispatcherInput()) return;
    speakerEffects_ = !speakerEffects_;
    if (speakerEffects_) {
        if (musicOn_) {
            attractOplPlayer_.stopAllChannels();
            sceneEffectPlayer_.stop();
        }
    } else if (musicOn_ && attractMode_ &&
               (page_ == WordGamePage::Attract ||
                page_ == WordGamePage::Feedback)) {
        playAttractMusic();
    }
    saveSettings();
    if (configurationWriteError_) {
        // Preserve Alt+P's post-save controller fallthrough without allowing
        // the native synthetic event to acknowledge the alert immediately.
        configurationWriteErrorPostDismissContinuation_ = true;
    }
}

void WordGame::handleCheatKey(const UINT virtualKey) {
    if (virtualKey == VK_ESCAPE) {
        cheatOpen_ = false;
        return;
    }
    if (virtualKey == VK_UP || virtualKey == VK_DOWN) cheatSelection_ = 1 - cheatSelection_;
    if (cheatSelection_ == 0 && (virtualKey == VK_LEFT || virtualKey == VK_RIGHT)) {
        cheatLevel_ = std::clamp(cheatLevel_ + (virtualKey == VK_LEFT ? -1 : 1), 1, 20);
    }
    if (virtualKey != VK_RETURN && virtualKey != VK_SPACE) return;
    if (cheatSelection_ == 0) startGame(cheatLevel_);
    cheatOpen_ = false;
}

std::vector<std::uint32_t> WordGame::capturePresentationFrame() {
    Renderer snapshot(assets_.graphicsMode());
    render(snapshot);
    return snapshot.pixels();
}

const WordGame::Enemy* WordGame::terminalResidentEnemyForPlayerCallback() const {
    if (!attractMode_ || !moving_) return nullptr;
    const bool verticalPlayerMove = moveDirection_ == 0 || moveDirection_ == 2;
    const int finalPlayerPhase = verticalPlayerMove ? 4 : 5;
    if (playerMovementPhase() != finalPlayerPhase) return nullptr;
    const auto found = std::find_if(
        enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
            if (!enemy.moving || enemy.entering || enemy.exiting) return false;
            const bool verticalEnemyMove =
                enemy.direction == 0 || enemy.direction == 2;
            return enemyMovementPhase(enemy) == (verticalEnemyMove ? 5 : 6);
        });
    return found == enemies_.end() ? nullptr : &*found;
}

std::vector<std::uint32_t> WordGame::capturePlayerCallbackFrame() {
    // Word and Number share the resident actor painter: isolate the player
    // layer with all Troggles hidden, then lay those pixels over the complete
    // pre-Troggle surface instead of recomposing the whole board.
    std::vector<std::uint32_t> composite = capturePresentationFrame();
    std::vector<bool> enemyHidden;
    enemyHidden.reserve(enemies_.size());
    for (Enemy& enemy : enemies_) {
        enemyHidden.push_back(enemy.collisionHidden);
        enemy.collisionHidden = true;
    }
    const bool playerWasRecovering = playerRecovering_;
    const std::vector<std::uint32_t> playerVisible = capturePresentationFrame();
    playerRecovering_ = true;
    const std::vector<std::uint32_t> actorsHidden = capturePresentationFrame();
    playerRecovering_ = playerWasRecovering;
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        enemies_[index].collisionHidden = enemyHidden[index];
    }
    const Enemy* terminalResidentEnemy =
        terminalResidentEnemyForPlayerCallback();
    for (std::size_t pixel = 0; pixel < composite.size(); ++pixel) {
        const int x = static_cast<int>(pixel % Renderer::Width);
        const int y = static_cast<int>(pixel / Renderer::Width);
        const bool inResidentEnemySourceCell =
            terminalResidentEnemy != nullptr &&
            x >= BoardLeft + terminalResidentEnemy->fromColumn * BoardCellWidth &&
            x <= BoardLeft + (terminalResidentEnemy->fromColumn + 1) * BoardCellWidth &&
            y >= BoardTop + terminalResidentEnemy->fromRow * BoardCellHeight &&
            y < BoardTop + (terminalResidentEnemy->fromRow + 1) * BoardCellHeight;
        if (inResidentEnemySourceCell &&
            (composite[pixel] & 0xffffffu) == Colors::BoardBlue &&
            ((playerVisible[pixel] & 0xffffffu) == Colors::White ||
             (playerVisible[pixel] & 0xffffffu) == Colors::Magenta)) {
            // The state-4 player callback repairs cell foreground after its
            // actor paint. DOS run 351 catches that callback while a Smarty's
            // prior selector-1 sweep is resident: "sleep" and the x=260 rule
            // appear over blue pixels, but the later actor pose remains
            // otherwise untouched. Preserve only foreground writes over the
            // blue sweep; do not erase current actor pixels or synthesize a
            // second logical Troggle state.
            composite[pixel] = playerVisible[pixel];
        }
        if (playerVisible[pixel] != actorsHidden[pixel]) {
            composite[pixel] = playerVisible[pixel];
        }
    }
    return composite;
}

void WordGame::copyBoardCellPixels(std::vector<std::uint32_t>& destination,
                                   const std::vector<std::uint32_t>& source,
                                   const int row, const int column) {
    const std::size_t expected =
        static_cast<std::size_t>(Renderer::Width * Renderer::Height);
    if (destination.size() != expected || source.size() != expected ||
        row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns) {
        return;
    }
    const int left = BoardLeft + column * BoardCellWidth;
    const int top = BoardTop + row * BoardCellHeight;
    for (int y = top; y < top + BoardCellHeight; ++y) {
        const std::size_t first = static_cast<std::size_t>(
            y * Renderer::Width + left);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(first),
                    BoardCellWidth,
                    destination.begin() + static_cast<std::ptrdiff_t>(first));
    }
}

void WordGame::enqueuePresentationFrame(const std::vector<std::uint32_t>& pixels,
                                        const int repeatCount) {
    if (pixels.size() != static_cast<std::size_t>(Renderer::Width * Renderer::Height) ||
        repeatCount <= 0) return;
    constexpr std::size_t MaximumQueuedFrames = 8;
    for (int repeat = 0; repeat < repeatCount; ++repeat) {
        if (presentationFrames_.size() == MaximumQueuedFrames) {
            presentationFrames_.pop_front();
        }
        presentationFrames_.push_back(pixels);
    }
}

void WordGame::discardPresentationFrames() {
    presentationFrames_.clear();
    deathResidentSurfacePixels_.clear();
    attractPlayerPhaseZeroHoldPixels_.clear();
    attractMunchSafeZoneHoldPixels_.clear();
    attractSafeZoneEnemyHoldOldPixels_.clear();
    attractSafeZoneEnemyHoldNewPixels_.clear();
    attractSafeZoneEnemyHoldTicks_ = 0;
    attractPlayerTerminalEnemyHoldPixels_.clear();
    attractPlayerTerminalEnemyHoldTicks_ = 0;
    attractPlayerCallbackHoldPixels_.clear();
    attractPlayerCallbackHoldTicks_ = 0;
    cannibalPlayerResidentSurfacePixels_.clear();
    cannibalPlayerPresentationHoldPixels_.clear();
    cannibalPlayerRetainedPenultimatePaint_ = false;
}

void WordGame::renderPresentation(Renderer& renderer) {
    if (!presentationFrames_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(presentationFrames_.front());
        presentationFrames_.pop_front();
        return;
    }
    if (attractPlayerCallbackHoldTicks_ > 0 && isActiveBoardPage(page_) &&
        attractPlayerCallbackHoldPixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerCallbackHoldPixels_);
        return;
    }
    if (deathAnimating_ &&
        deathResidentSurfacePixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(deathResidentSurfacePixels_);
        return;
    }
    if (!attractPlayerPhaseZeroHoldPixels_.empty() && moving_ &&
        playerMovementPhase() == 0) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerPhaseZeroHoldPixels_);
        return;
    }
    attractPlayerPhaseZeroHoldPixels_.clear();
    if (!attractMunchSafeZoneHoldPixels_.empty() && munching_) {
        const double elapsed = MunchAnimationDuration - std::max(0.0, munchTimer_);
        const int elapsedTicks = std::max(
            0, static_cast<int>(std::floor(
                   elapsed * OriginalSceneTicksPerSecond + 1e-7)));
        if (elapsedTicks == 0) {
            renderer.setGraphicsMode(assets_.graphicsMode());
            renderer.replacePixels(attractMunchSafeZoneHoldPixels_);
            return;
        }
    }
    attractMunchSafeZoneHoldPixels_.clear();
    if (attractSafeZoneEnemyHoldTicks_ > 0 && isActiveBoardPage(page_) &&
        attractSafeZoneEnemyHoldOldPixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height) &&
        attractSafeZoneEnemyHoldNewPixels_.size() ==
            attractSafeZoneEnemyHoldOldPixels_.size()) {
        render(renderer);
        std::vector<std::uint32_t> pixels = renderer.pixels();
        for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
            if (attractSafeZoneEnemyHoldOldPixels_[pixel] !=
                attractSafeZoneEnemyHoldNewPixels_[pixel]) {
                pixels[pixel] = attractSafeZoneEnemyHoldOldPixels_[pixel];
            }
        }
        renderer.replacePixels(pixels);
        return;
    }
    if (attractSafeZoneEnemyHoldTicks_ <= 0 || !isActiveBoardPage(page_)) {
        attractSafeZoneEnemyHoldOldPixels_.clear();
        attractSafeZoneEnemyHoldNewPixels_.clear();
        attractSafeZoneEnemyHoldTicks_ = 0;
    }
    if (attractPlayerTerminalEnemyHoldTicks_ > 0 &&
        isActiveBoardPage(page_) &&
        attractPlayerTerminalEnemyHoldPixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerTerminalEnemyHoldPixels_);
        return;
    }
    if (attractPlayerTerminalEnemyHoldTicks_ <= 0 ||
        !isActiveBoardPage(page_)) {
        attractPlayerTerminalEnemyHoldPixels_.clear();
        attractPlayerTerminalEnemyHoldTicks_ = 0;
    }
    if (!cannibalPlayerPresentationHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(cannibalPlayerPresentationHoldPixels_);
        return;
    }
    render(renderer);
}

void WordGame::render(Renderer& renderer) {
    renderer.setGraphicsMode(assets_.graphicsMode());
    if (configurationWriteError_ &&
        configurationWriteErrorBackgroundPixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        // The original stores the initiating pixels before drawing its alert.
        // Keeping the actual frame also preserves editor text and transactional
        // collections that the caller is free to clear after saveSettings().
        renderer.replacePixels(configurationWriteErrorBackgroundPixels_);
    } else switch (page_) {
    case WordGamePage::StartupVersion: renderStartupVersion(renderer); break;
    case WordGamePage::StartupSplash: renderStartupSplash(renderer); break;
    case WordGamePage::Title: renderTitle(renderer); break;
    case WordGamePage::InstructionsQuestion: renderInstructionsQuestion(renderer); break;
    case WordGamePage::Information: renderInformation(renderer); break;
    case WordGamePage::Options: renderOptions(renderer); break;
    case WordGamePage::OptionsContent: renderOptionsContent(renderer); break;
    case WordGamePage::OptionsDifficulty: renderOptionsDifficulty(renderer); break;
    case WordGamePage::OptionsVowels: renderOptionsVowels(renderer); break;
    case WordGamePage::OptionsVowelsHelp: renderOptionsVowelsHelp(renderer); break;
    case WordGamePage::OptionsPreview: renderOptionsPreview(renderer); break;
    case WordGamePage::OptionsEraseHall: renderOptionsEraseHall(renderer); break;
    case WordGamePage::OptionsPassword: renderOptionsPassword(renderer); break;
    case WordGamePage::OptionsPasswordPrompt: renderOptionsPasswordPrompt(renderer); break;
    case WordGamePage::OptionsJoystickCalibration:
        renderOptionsJoystickCalibration(renderer);
        break;
    case WordGamePage::Hall: renderHall(renderer); break;
    case WordGamePage::Playing:
    case WordGamePage::Attract:
    case WordGamePage::Paused:
    case WordGamePage::Feedback: renderBoard(renderer); break;
    case WordGamePage::AttractLogo: renderAttractLogo(renderer); break;
    case WordGamePage::QuitConfirm: renderBoard(renderer); renderQuitConfirm(renderer); break;
    case WordGamePage::LevelComplete: renderLevelComplete(renderer); break;
    case WordGamePage::NameEntry: renderBoard(renderer); renderNameEntry(renderer); break;
    case WordGamePage::ReplayQuestion: renderReplayQuestion(renderer); break;
    }
    if (!configurationWriteError_) {
        if (attractMode_ &&
            attractHallTransitionPhase_ == AttractHallTransitionPhase::Wipe) {
            renderAttractHallWipe(renderer);
        }
        if (attractInterstitialTransition_ !=
                AttractInterstitialTransition::None) {
            renderAttractInterstitialTransition(renderer);
        }
        if (cheatOpen_) renderCheatMenu(renderer);
    }
    if (configurationWriteError_) renderConfigurationWriteError(renderer);
}

void WordGame::renderConfigurationWriteError(Renderer& renderer) {
    // Common alert at image 0x07E21.  Its saved rectangle is inclusive
    // (60,65)..(260,135); visible text uses x=68 and baselines 74,84,94,119.
    // DS:1F90 supplies the deliberately empty fourth row between 94 and 119.
    renderer.fillRect(60, 65, 201, 71, Colors::Black);
    const GemFont& font = assets_.largeFont();
    // The common routine selects original logical color 3. DS:0464 maps that
    // to VGA index 15 (white) and CGA index 1 (light cyan).
    const std::uint32_t textColor = renderer.graphicsMode() == GraphicsMode::Cga4
        ? Colors::Cyan : Colors::White;
    renderer.outlineRect(60, 65, 201, 71, textColor);
    renderer.drawText(font, 68, 74, "Configuration file", textColor);
    renderer.drawText(font, 68, 84, "cannot be written, it", textColor);
    renderer.drawText(font, 68, 94, "is write protected", textColor);
    renderer.drawText(font, 68, 119, "    Press any key.", textColor);
}

void WordGame::renderStartupVersion(Renderer& renderer) {
    renderer.clear(Colors::White);
    const GemFont& font = assets_.largeFont();
    const BlobView product = loadEmbeddedResource(IDR_WM_PRODUCT_PF);

    // PRODUCT.PF's 350-byte record supplies version words at +8/+10,
    // copyright at +0x16, and the product name at +0x135. These are the exact
    // fields consumed by the common startup painter at image 0x0DAA0.
    std::string productName = "Word Munchers";
    std::string copyright = "Copyright 1990,1991, MECC";
    int versionMajor = 1;
    int versionMinor = 1;
    const auto field = [&product](const std::size_t offset) {
        if (!product || offset >= product.size) return std::string{};
        const std::uint8_t* begin = product.data + offset;
        const std::uint8_t* end = product.data + product.size;
        const std::uint8_t* terminator = std::find(begin, end, static_cast<std::uint8_t>(0));
        return std::string(reinterpret_cast<const char*>(begin),
                           reinterpret_cast<const char*>(terminator));
    };
    if (product && product.size >= 0x143) {
        versionMajor = readInformationU16(product.data + 8);
        versionMinor = readInformationU16(product.data + 10);
        if (std::string value = field(0x16); !value.empty()) copyright = std::move(value);
        if (std::string value = field(0x135); !value.empty()) productName = std::move(value);
    }

    renderer.drawCenteredText(font, 159, 35, productName, Colors::Black);
    renderer.drawCenteredText(font, 159, 47,
                              "Version " + std::to_string(versionMajor) + "." +
                                  std::to_string(versionMinor),
                              Colors::Black);
    if (const Image* logo = assets_.startupLogo()) renderer.drawImage(*logo, 0, 75, false);
    renderer.drawCenteredText(font, 159, 185, copyright, Colors::Black);
}

void WordGame::renderStartupSplash(Renderer& renderer) {
    renderAttractLogo(renderer);
}

void WordGame::renderTitle(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (const Image* title = assets_.image(6009)) renderer.drawImage(*title, 0, 0, false, true);
    for (int index = 0; index < static_cast<int>(TitleLabels.size()); ++index) {
        const int y = TitleRows[static_cast<std::size_t>(index)];
        if (menuSelection_ == index) {
            renderer.drawText(assets_.largeFont(), 57, y,
                              std::to_string(index + 1) + ".", Colors::White);
            const std::string_view label = TitleLabels[static_cast<std::size_t>(index)];
            renderer.fillRect(71, y - 1, renderer.textWidth(assets_.largeFont(), label) + 21,
                              10, Colors::White);
            renderer.drawText(assets_.largeFont(), 81, y, label, Colors::Black);
        } else {
            renderer.drawText(assets_.largeFont(), 57, y,
                              std::to_string(index + 1) + ". " +
                                  std::string(TitleLabels[static_cast<std::size_t>(index)]),
                              Colors::White);
        }
    }
    renderer.drawText(assets_.largeFont(), 10, 170,
                      "Use Arrows to move, then press Enter.", Colors::White);
}

void WordGame::renderAttractLogo(Renderer& renderer) {
    renderer.clear(0xfbffdb);
    if (const Image* logo = assets_.image(6005)) renderer.drawImage(*logo, 0, 0, false, true);
    renderer.drawCenteredText(assets_.largeFont(), 159, 185,
                               "Press a key for Muncher Menu", Colors::Black);
}

void WordGame::renderAttractHallWipe(Renderer& renderer) {
    // Word 0x11993 and Number 0x1220d both call the same type-1 PCX effect,
    // and their logical 6051/6053 transition resources are byte-identical
    // (6050/6052 in CGA). Preserve the measured shared stepped aperture while
    // composing Word-owned pages.
    // This shared effect does not paint Word's white Hall background.  Both
    // seeded DOS clean-route occurrences open into the resident navy board
    // surface, reach one complete navy presentation, and only then paint the
    // Hall; Number's independently audited path preserves the same surface.
    // The renderer maps navy to the corresponding CGA entry when needed.
    const std::uint32_t hallBackground = Colors::BoardBlue;
    if (attractHallWipeVariant_ == AttractHallWipeVariant::Clean) {
        switch (attractHallWipeFrame_) {
        case 0:
            renderer.fillRect(0, 192, 11, 3, Colors::Cyan);
            renderer.fillRect(0, 195, 11, 4, Colors::Cyan);
            renderer.fillRect(310, 195, 10, 4, Colors::Cyan);
            renderer.fillRect(0, 199, Renderer::Width, 1, Colors::Cyan);
            break;
        case 1:
        case 2:
            renderer.clear(Colors::Cyan);
            break;
        case 3:
            renderer.clear(Colors::Cyan);
            renderer.fillRect(160, 61, 11, 3, hallBackground);
            renderer.fillRect(150, 64, 21, 10, hallBackground);
            renderer.fillRect(150, 74, 31, 4, hallBackground);
            renderer.fillRect(140, 78, 41, 10, hallBackground);
            renderer.fillRect(140, 88, 51, 1, hallBackground);
            renderer.fillRect(0, 89, Renderer::Width,
                              Renderer::Height - 89, hallBackground);
            break;
        case 4:
        case 5:
            renderer.clear(hallBackground);
            break;
        default:
            renderHall(renderer);
            break;
        }
        return;
    }

    switch (attractHallWipeFrame_) {
    case 0: {
        struct Band { int top; int bottom; int left; int right; };
        static constexpr std::array<Band, 14> survivingBands = {{
            {0, 70, 0, 319}, {71, 74, 11, 319}, {75, 84, 11, 309},
            {85, 87, 21, 309}, {88, 97, 21, 299}, {98, 100, 31, 299},
            {101, 110, 31, 289}, {111, 114, 41, 289},
            {115, 124, 41, 279}, {125, 127, 51, 279},
            {128, 137, 51, 269}, {138, 140, 61, 269},
            {141, 144, 61, 259}, {145, 145, 154, 259},
        }};
        for (const Band& band : survivingBands) {
            const int height = band.bottom - band.top + 1;
            if (band.left > 0) {
                renderer.fillRect(0, band.top, band.left, height, Colors::Cyan);
            }
            if (band.right < Renderer::Width - 1) {
                renderer.fillRect(band.right + 1, band.top,
                                  Renderer::Width - band.right - 1, height,
                                  Colors::Cyan);
            }
        }
        renderer.fillRect(0, 146, Renderer::Width,
                          Renderer::Height - 146, Colors::Cyan);
        break;
    }
    case 1:
        renderer.clear(Colors::Cyan);
        break;
    case 2:
        renderer.clear(Colors::Cyan);
        renderer.fillRect(160, 170, 11, 3, hallBackground);
        renderer.fillRect(150, 173, 21, 10, hallBackground);
        renderer.fillRect(150, 183, 31, 4, hallBackground);
        renderer.fillRect(140, 187, 41, 10, hallBackground);
        renderer.fillRect(140, 197, 51, 3, hallBackground);
        break;
    case 3:
        renderer.clear(Colors::Cyan);
        renderer.fillRect(110, 0, 101, 9, hallBackground);
        renderer.fillRect(110, 9, 111, 3, hallBackground);
        renderer.fillRect(100, 12, 121, 10, hallBackground);
        renderer.fillRect(100, 22, 131, 3, hallBackground);
        renderer.fillRect(90, 25, 141, 10, hallBackground);
        renderer.fillRect(90, 35, 151, 4, hallBackground);
        renderer.fillRect(0, 39, Renderer::Width,
                          Renderer::Height - 39, hallBackground);
        break;
    case 4:
        renderer.clear(hallBackground);
        break;
    default:
        renderHall(renderer);
        break;
    }
}

void WordGame::renderAttractInterstitialTransition(Renderer& renderer) {
    struct Band { int top; int bottom; int left; int right; };
    const auto coverOutside = [&](const auto& survivingBands, const int coveredTop) {
        for (const Band& band : survivingBands) {
            const int height = band.bottom - band.top + 1;
            if (band.left > 0) {
                renderer.fillRect(0, band.top, band.left, height, Colors::Cyan);
            }
            if (band.right < Renderer::Width - 1) {
                renderer.fillRect(band.right + 1, band.top,
                                  Renderer::Width - band.right - 1, height,
                                  Colors::Cyan);
            }
        }
        renderer.fillRect(0, coveredTop, Renderer::Width,
                          Renderer::Height - coveredTop, Colors::Cyan);
    };

    if (attractInterstitialTransition_ ==
        AttractInterstitialTransition::HallToLogo) {
        switch (attractInterstitialFrame_) {
        case 0: {
            static constexpr std::array<Band, 15> survivingBands = {{
                {0, 27, 0, 319}, {28, 31, 11, 319}, {32, 49, 11, 309},
                {50, 52, 21, 309}, {53, 62, 21, 299}, {63, 66, 31, 299},
                {67, 76, 31, 289}, {77, 79, 41, 289}, {80, 89, 41, 279},
                {90, 92, 51, 279}, {93, 102, 51, 269},
                {103, 106, 61, 269}, {107, 116, 61, 259},
                {117, 119, 71, 259}, {120, 128, 71, 249},
            }};
            coverOutside(survivingBands, 129);
            break;
        }
        case 1:
            renderer.clear(Colors::Cyan);
            break;
        case 2:
            renderer.clear(Colors::Cyan);
            renderer.fillRect(160, 133, 11, 3, Colors::Black);
            renderer.fillRect(150, 136, 21, 10, Colors::Black);
            renderer.fillRect(150, 146, 31, 3, Colors::Black);
            renderer.fillRect(140, 149, 41, 10, Colors::Black);
            renderer.fillRect(140, 159, 51, 4, Colors::Black);
            renderer.fillRect(130, 163, 61, 10, Colors::Black);
            renderer.fillRect(130, 173, 71, 3, Colors::Black);
            renderer.fillRect(120, 176, 81, 10, Colors::Black);
            renderer.fillRect(120, 186, 91, 3, Colors::Black);
            renderer.fillRect(110, 189, 101, 10, Colors::Black);
            renderer.fillRect(110, 199, 111, 1, Colors::Black);
            break;
        case 3:
            renderer.clear(Colors::Cyan);
            renderer.fillRect(90, 0, 151, 1, Colors::Black);
            renderer.fillRect(80, 1, 161, 11, Colors::Black);
            renderer.fillRect(80, 12, 171, 4, Colors::Black);
            renderer.fillRect(70, 16, 181, 5, Colors::Black);
            renderer.fillRect(0, 21, Renderer::Width,
                              Renderer::Height - 21, Colors::Black);
            break;
        case 4:
            renderer.clear(Colors::Black);
            break;
        default:
            // DOS begins painting the logo during this callback, but the only
            // intervening source state is an incomplete scanout (runs 608 and
            // 665). Hold the last complete black page until the complete logo
            // is ready. The former cream footer-only page was native-only and
            // produced a visible one-refresh flash.
            renderer.clear(Colors::Black);
            break;
        }
        return;
    }

    if (userPresentationActive() && attractInterstitialFrame_ == 0) {
        renderer.replacePixels(boardPresentationSourcePixels_);
    }

    switch (attractInterstitialFrame_) {
    case 0: {
        if (attractInterstitialTransition_ ==
            AttractInterstitialTransition::UserToCartoon) {
            // Completed logical close state reconstructed from the first
            // lossless Word cartoon capture. DOS can expose one torn refresh
            // while drawing it; native presents this complete state atomically.
            static constexpr std::array<Band, 15> cartoonBands = {{
                {0, 45, 0, 319}, {46, 49, 11, 319}, {50, 58, 11, 309},
                {59, 62, 21, 309}, {63, 72, 21, 299}, {73, 75, 31, 299},
                {76, 85, 31, 289}, {86, 89, 41, 289}, {90, 99, 41, 279},
                {100, 102, 51, 279}, {103, 112, 51, 269},
                {113, 115, 61, 269}, {116, 125, 61, 259},
                {126, 129, 71, 259}, {130, 133, 71, 249},
            }};
            coverOutside(cartoonBands, 134);
        } else {
            static constexpr std::array<Band, 9> boardBands = {{
                {0, 104, 0, 319}, {105, 108, 11, 319},
                {109, 118, 11, 309}, {119, 121, 21, 309},
                {122, 131, 21, 299}, {132, 134, 31, 299},
                {135, 144, 31, 289}, {145, 148, 41, 289},
                {149, 158, 41, 279},
            }};
            coverOutside(boardBands, 159);
        }
        break;
    }
    case 1:
    case 2:
        renderer.clear(Colors::Cyan);
        break;
    case 3:
        if (attractInterstitialTransition_ ==
            AttractInterstitialTransition::UserToCartoon) {
            // Type-1 opens upward from the bottom onto the still-black target
            // page. The scene's first retained-painter tick is deliberately
            // not underneath this aperture.
            renderer.clear(Colors::Cyan);
            renderer.fillRect(160, 56, 11, 4, Colors::Black);
            renderer.fillRect(150, 60, 21, 10, Colors::Black);
            renderer.fillRect(150, 70, 31, 3, Colors::Black);
            renderer.fillRect(140, 73, 41, 10, Colors::Black);
            renderer.fillRect(140, 83, 51, 4, Colors::Black);
            renderer.fillRect(130, 87, 61, 2, Colors::Black);
            renderer.fillRect(0, 89, Renderer::Width,
                              Renderer::Height - 89, Colors::Black);
            break;
        }
        renderer.clear(Colors::Cyan);
        renderer.fillRect(140, 0, 41, 2, Colors::BoardBlue);
        renderer.fillRect(140, 2, 51, 4, Colors::BoardBlue);
        renderer.fillRect(130, 6, 61, 10, Colors::BoardBlue);
        renderer.fillRect(130, 16, 71, 3, Colors::BoardBlue);
        renderer.fillRect(120, 19, 81, 10, Colors::BoardBlue);
        renderer.fillRect(120, 29, 91, 3, Colors::BoardBlue);
        renderer.fillRect(110, 32, 101, 10, Colors::BoardBlue);
        renderer.fillRect(110, 42, 111, 4, Colors::BoardBlue);
        renderer.fillRect(100, 46, 121, 5, Colors::BoardBlue);
        renderer.fillRect(0, 51, Renderer::Width,
                          Renderer::Height - 51, Colors::BoardBlue);
        break;
    case 4:
        if (attractInterstitialTransition_ ==
            AttractInterstitialTransition::UserToCartoon) break;
        [[fallthrough]];
    case 5:
    case 6:
        renderer.clear(Colors::BoardBlue);
        break;
    default:
        renderAttractBoardPaint(renderer, attractInterstitialFrame_ - 7);
        break;
    }
}

void WordGame::renderAttractBoardPaint(Renderer& renderer, const int frame) {
    if (frame > 0) {
        renderBoard(renderer);
        renderer.fillRect(0, 3, Renderer::Width, 13, Colors::BoardBlue);
        return;
    }

    // The shared painter has reached only the grid prefix at this sample. Word
    // retains its own live word records rather than copied Number-frame pixels.
    renderer.clear(Colors::BoardBlue);
    renderer.horizontalLine(92, 234, 2, Colors::White);
    renderer.horizontalLine(92, 234, 16, Colors::White);
    renderer.horizontalLine(18, 310, 24, Colors::Magenta);
    renderer.verticalLine(310, 24, 178, Colors::Magenta);
    renderer.verticalLine(18, 28, 178, Colors::Magenta);
    renderer.horizontalLine(18, 310, 178, Colors::Magenta);
    renderer.verticalLine(68, 34, 56, Colors::Magenta);
    renderer.verticalLine(20, 36, 56, Colors::Magenta);
    renderer.verticalLine(116, 41, 56, Colors::Magenta);
    renderer.verticalLine(164, 49, 56, Colors::Magenta);
    renderer.horizontalLine(20, 164, 56, Colors::Magenta);
    renderer.verticalLine(68, 79, 86, Colors::Magenta);
    renderer.verticalLine(20, 81, 86, Colors::Magenta);
    renderer.horizontalLine(20, 68, 86, Colors::Magenta);

    const GemFont& font = assets_.largeFont();
    for (int column = 0; column < 2; ++column) {
        const int index = column;
        if (core_.eaten(static_cast<std::size_t>(index))) continue;
        renderer.drawCenteredText(
            font, BoardLeft + column * BoardCellWidth + BoardCellWidth / 2,
            BoardTop + 12,
            core_.board().cells[static_cast<std::size_t>(index)].record.text(),
            Colors::White);
    }
    renderer.fillRect(BoardLeft + BoardCellWidth + 1, BoardTop + 12,
                      BoardCellWidth - 2, 1, Colors::BoardBlue);
}

void WordGame::renderInstructionsQuestion(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 60, 85, "Do you want instructions?", Colors::White);
    const auto choice = [&](const int x, const std::string_view label, const bool selected) {
        if (selected) renderer.fillRect(x - 2, 104, renderer.textWidth(font, label) + 5, 10,
                                        Colors::White);
        renderer.drawText(font, x, 105, label, selected ? Colors::Black : Colors::White);
    };
    choice(115, " Yes ", menuSelection_ == 0);
    choice(165, " No ", menuSelection_ == 1);
}

void WordGame::renderInformation(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);
    const GemFont& font = assets_.largeFont();
    const BlobView data = assets_.gameArchive().find("DATA", 1);
    const std::uint8_t* page = informationPage(data, informationGroup_, informationPage_);
    if (page) {
        for (int lineIndex = 0; lineIndex < InformationLineCount; ++lineIndex) {
            const char* line = reinterpret_cast<const char*>(
                page + static_cast<std::size_t>(lineIndex) * InformationLineBytes);
            std::size_t length = 0;
            while (length < InformationLineBytes && line[length] != '\0') ++length;
            if (length != 0) {
                renderer.drawText(font, 12, 15 + lineIndex * 10,
                                  std::string_view(line, length), Colors::White);
            }
        }
        for (int spriteIndex = 0; spriteIndex < InformationSpriteCount; ++spriteIndex) {
            const std::uint8_t* placement =
                page + InformationTextBytes + static_cast<std::size_t>(spriteIndex) * 4;
            const int y = readInformationI16(placement);
            const int x = readInformationI16(placement + 2);
            if (x >= 0 && y >= 0) {
                drawSprite(renderer,
                           InformationSpriteSheets[static_cast<std::size_t>(spriteIndex)],
                           spriteIndex == 0 ? 7 : 15,
                           x + 4, y + 1, spriteIndex != 0);
            }
        }
    }
    renderer.drawText(font, 50, 191, "Press Space Bar to continue.", Colors::White);
}

void WordGame::renderOptions(Renderer& renderer) {
    drawOptionsFrame(renderer, "Options");
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Choose an option:", Colors::White);
    std::vector<std::string_view> labels(OptionsLabels.begin(), OptionsLabels.end());
    labels[3] = joystickEnabled_ ? "Turn Joystick OFF" : "Turn Joystick ON ";
    drawNumberedMenu(renderer, labels, 80, 50);
    renderer.drawText(font, 0, 167, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Main Menu", Colors::White);
}

void WordGame::renderOptionsContent(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Content");
    const GemFont& font = assets_.largeFont();
    const std::string_view difficulty =
        DifficultyLabels[static_cast<std::size_t>(contentDraftDifficulty_)];
    renderer.drawText(font, 5, 32, "Current Difficulty: " + std::string(difficulty),
                      Colors::White);
    renderer.drawText(font, 5, 41,
                      "Vowel sounds selected:  " +
                          std::to_string(selectedSoundCount(contentDraftSounds_)),
                      Colors::White);
    renderer.drawText(font, 5, 50,
                      "Target words available: " +
                          std::to_string(targetWordCount(contentDraftSounds_,
                                                         contentDraftDifficulty_)),
                      Colors::White);
    renderer.drawText(font, 5, 86, "Choose an option:", Colors::White);
    drawNumberedMenu(renderer,
                     std::vector<std::string_view>(ContentLabels.begin(), ContentLabels.end()),
                     104, 50);
    renderer.drawText(font, 0, 167, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Options Menu", Colors::White);

    if (contentInsufficientMessage_) {
        // pDisplayText(0xC3, ...) at image 0x0BB4B counts the leading and
        // trailing newlines, uses a nine-pixel line advance, vertically
        // centers the resulting 82-pixel strip, and centers every padded
        // 38-column source line.  Its saved-screen frame spans the full VGA
        // width and is restored when the accepted-key waiter returns.
        constexpr int outerTop = 58;
        constexpr int outerBottom = 140;
        constexpr int firstBaseline = 63;
        constexpr int lineHeight = 9;
        constexpr std::array<std::string_view, 8> lines = {
            "",
            "The current content settings provide  ",
            "insufficient target words for the     ",
            "Muncher.                              ",
            "You must select a different difficulty",
            "level or select from available vowel  ",
            "sounds.                               ",
            "",
        };
        renderer.fillRect(0, outerTop, Renderer::Width,
                          outerBottom - outerTop + 1, Colors::Black);
        renderer.outlineRect(0, outerTop, Renderer::Width,
                             outerBottom - outerTop + 1, Colors::Cyan);
        renderer.outlineRect(2, outerTop + 2, Renderer::Width - 4,
                             outerBottom - outerTop - 3, Colors::Cyan);
        for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
            renderer.drawCenteredText(font, 159, firstBaseline + index * lineHeight,
                                      lines[static_cast<std::size_t>(index)],
                                      Colors::White);
        }
        // Flag 1 routes through the shared accepted-key presenter. It replaces
        // the page-specific footer with the centered continuation prompt while
        // retaining the existing divider pair at y=163/165.
        renderer.fillRect(0, 166, Renderer::Width, Renderer::Height - 166,
                          Colors::Black);
        renderer.drawCenteredText(font, 159, 176,
                                  "Press Space Bar to continue.", Colors::White);
    }
}

void WordGame::renderOptionsDifficulty(Renderer& renderer) {
    drawOptionsFrame(renderer, "Select Word Difficulty", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32,
                      "Current Difficulty: " +
                          std::string(DifficultyLabels[static_cast<std::size_t>(
                              contentDraftDifficulty_)]),
                      Colors::White);
    renderer.drawText(font, 5, 50, "Choose a level:", Colors::White);
    drawNumberedMenu(renderer,
                     std::vector<std::string_view>(DifficultyLabels.begin(),
                                                   DifficultyLabels.end()),
                     68, 50);
    renderer.drawText(font, 0, 176, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void WordGame::renderOptionsVowels(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Vowel Sounds");
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32,
                      "Current Difficulty: " +
                          std::string(DifficultyLabels[static_cast<std::size_t>(
                              contentDraftDifficulty_)]),
                      Colors::White);
    renderer.drawText(font, 26, 56, "Group 1", Colors::White);
    renderer.drawText(font, 106, 56, "Group 2", Colors::White);
    renderer.drawText(font, 196, 56, "Group 3", Colors::White);

    for (int choice = 0; choice < static_cast<int>(VowelChoiceGeometry.size()); ++choice) {
        const WordVowelChoiceGeometry& geometry =
            VowelChoiceGeometry[static_cast<std::size_t>(choice)];
        drawVowelStateBox(renderer, geometry.top, geometry.left,
                          geometry.bottom, geometry.right, vowelChoiceState(choice));

        const std::string label =
            " " + std::string(VowelChoiceLabels[static_cast<std::size_t>(choice)]) + " ";
        // The generic blocking message is painted after the editor releases
        // its widget focus; the surviving rows above the modal therefore do
        // not retain a native-only white focus backing.
        const bool focused = vowelMessage_ == 0 && vowelSelection_ == choice;
        if (focused) {
            renderer.fillRect(geometry.textX - 2, geometry.textY - 3,
                              renderer.textWidth(font, label) + 5, font.height() + 5,
                              Colors::White);
        }
        renderer.drawText(font, geometry.textX, geometry.textY, label,
                          focused ? Colors::Black : Colors::White);

        // The Group-1 labels use the original font driver's overstrike
        // marks for their long-vowel spellings. Those zero-width marks sit
        // above the otherwise byte-identical ASCII labels.
        const std::uint32_t mark = focused ? Colors::Black : Colors::White;
        if (choice >= 0 && choice < 5) {
            renderer.horizontalLine(53, 59, geometry.textY - 1, mark);
            renderer.horizontalLine(69, 75, geometry.textY - 1, mark);
            renderer.fillRect(67, geometry.textY - 2, 2, 1, mark);
            renderer.fillRect(76, geometry.textY - 2, 2, 1, mark);
        } else if (choice == 5) {
            renderer.horizontalLine(54, 66, geometry.textY - 1, mark);
            renderer.horizontalLine(81, 87, geometry.textY - 1, mark);
            renderer.fillRect(78, geometry.textY - 2, 3, 1, mark);
            renderer.fillRect(88, geometry.textY - 2, 3, 1, mark);
        }
    }

    renderer.drawText(font, 0, 167,
                      "Use Arrows to move, Space Bar to select.", Colors::White);
    renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
    renderer.drawText(font, 0, 185,
                      "Escape: Cancel changes         F1: Help", Colors::White);

    if (vowelMessage_ != 0) {
        const int top = vowelMessage_ == 1 ? 76 : 67;
        const int bottom = vowelMessage_ == 1 ? 122 : 131;
        renderer.fillRect(0, top, Renderer::Width, bottom - top + 1, Colors::Black);
        renderer.horizontalLine(0, 319, top, Colors::Cyan);
        renderer.horizontalLine(2, 317, top + 2, Colors::Cyan);
        renderer.horizontalLine(2, 317, bottom - 2, Colors::Cyan);
        renderer.horizontalLine(0, 319, bottom, Colors::Cyan);
        renderer.verticalLine(0, top, bottom, Colors::Cyan);
        renderer.verticalLine(319, top, bottom, Colors::Cyan);
        renderer.verticalLine(2, top + 2, bottom - 2, Colors::Cyan);
        renderer.verticalLine(317, top + 2, bottom - 2, Colors::Cyan);
        if (vowelMessage_ == 1) {
            constexpr std::array<std::string_view, 4> lines = {
                "",
                "You have selected no vowel sounds.   ",
                "Please select at least two.          ",
                "",
            };
            for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
                renderer.drawCenteredText(font, 159, 81 + index * 9,
                                          lines[static_cast<std::size_t>(index)],
                                          Colors::White);
            }
        } else {
            const std::string group = vowelMessage_ == 2 ? "Group 2" : "Group 3";
            const std::array<std::string, 6> lines = {
                "",
                "You have selected only one vowel  ",
                "sound from " + group + ". If you wish to",
                "use any vowels from " + group + ", you  ",
                "must select at least two.         ",
                "",
            };
            for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
                renderer.drawCenteredText(font, 159, 72 + index * 9,
                                          lines[static_cast<std::size_t>(index)],
                                          Colors::White);
            }
        }
        renderer.fillRect(0, 166, Renderer::Width, Renderer::Height - 166,
                          Colors::Black);
        renderer.drawCenteredText(font, 159, 176,
                                  "Press Space Bar to continue.", Colors::White);
    }
}

void WordGame::renderOptionsVowelsHelp(Renderer& renderer) {
    drawOptionsFrame(renderer, "Select Vowel Sounds Help");
    const GemFont& font = assets_.largeFont();
    const GemFont& smallFont = assets_.smallFont();
    const std::array<std::string_view, 8> lines = {
        "You may use this option to select the",
        "vowel sounds to use with Word Munchers.",
        "At least one vowel must be selected.",
        "If you wish to select certain vowel",
        "sounds in Groups 2 or 3, then you must",
        "select a minimum of two vowel sounds in",
        "each group.  See the User's Guide for",
        "details."
    };
    constexpr int lineHeight = 9; // BGI textheight(BIT8X8)+1
    for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
        renderer.drawText(font, 0, 33 + index * lineHeight,
                          lines[static_cast<std::size_t>(index)], Colors::White);
    }
    renderer.drawText(smallFont, 0, 114,
                      "    = Available               = Not Available",
                      Colors::White);
    renderer.drawText(smallFont, 0, 132,
                      "    = Selected, Available      = Selected, Not Available",
                      Colors::White);
    renderer.drawText(smallFont, 0, 150,
                      "F2/F3 = Select All/De-select All", Colors::White);
    drawVowelStateBox(renderer, 110, 2, 121, 12, 0);
    drawVowelStateBox(renderer, 110, 150, 121, 161, 2);
    drawVowelStateBox(renderer, 128, 1, 139, 12, 1);
    drawVowelStateBox(renderer, 128, 150, 139, 161, 3);
    renderer.drawCenteredText(font, 159, 176,
                              "Press Space Bar to continue.", Colors::White);
}

void WordGame::renderOptionsPreview(Renderer& renderer) {
    drawOptionsFrame(renderer, "Preview Words", 171);
    const GemFont& font = assets_.largeFont();
    const GemFont& previewFont = assets_.smallFont();
    renderer.drawText(font, 5, 32,
                      "Current Difficulty: " +
                          std::string(DifficultyLabels[static_cast<std::size_t>(
                              contentDraftDifficulty_)]),
                      Colors::White);
    if (previewSound_ >= 0 && previewSound_ < 20) {
        constexpr int LineHeight = 9; // b3c:0751 returns text height plus one.
        constexpr int GridTop = 32 + 3 * LineHeight;
        constexpr int GridLeft = 12;
        const int columnStep = 8 * renderer.textWidth(previewFont, "W");
        std::string previewLabel(
            wordSoundLabels()[static_cast<std::size_t>(previewSound_)]);
        previewLabel.erase(std::remove(previewLabel.begin(), previewLabel.end(), '/'),
                           previewLabel.end());
        constexpr int PreviewLabelY = 45;
        constexpr int PreviewAccentY = 44;
        const int previewLabelWidth = renderer.textWidth(previewFont, previewLabel);
        const int previewLabelX = 159 - previewLabelWidth / 2;
        renderer.drawText(previewFont, previewLabelX, PreviewLabelY,
                          previewLabel, Colors::White);
        const bool singleBreve = previewSound_ < 10 && (previewSound_ & 1) != 0;
        renderer.horizontalLine(previewLabelX - (singleBreve ? 4 : 0),
                                previewLabelX + previewLabelWidth +
                                    (singleBreve ? 3 : 0),
                                55, Colors::White);
        if (previewSound_ >= 0 && previewSound_ < 12) {
            const int markedWidth = previewSound_ >= 10 ? 13 : 6;
            if ((previewSound_ & 1) == 0) {
                renderer.horizontalLine(previewLabelX - 1,
                                        previewLabelX + markedWidth - 2,
                                        PreviewAccentY, Colors::White);
            } else if (previewSound_ < 10) {
                renderer.fillRect(previewLabelX - 2, PreviewAccentY - 1,
                                  2, 1, Colors::White);
                renderer.fillRect(previewLabelX + 5, PreviewAccentY - 1,
                                  2, 1, Colors::White);
                renderer.horizontalLine(previewLabelX, previewLabelX + 4,
                                        PreviewAccentY, Colors::White);
            } else {
                renderer.fillRect(previewLabelX - 1, PreviewAccentY - 1, 2, 1,
                                  Colors::White);
                renderer.fillRect(previewLabelX + markedWidth - 3,
                                  PreviewAccentY - 1, 2, 1, Colors::White);
                renderer.horizontalLine(previewLabelX + 1,
                                        previewLabelX + markedWidth - 4,
                                        PreviewAccentY, Colors::White);
            }
        }
        if (const WordSection* section = words_.section(static_cast<std::size_t>(previewSound_))) {
            const int count = targetAvailabilityCount(previewSound_, contentDraftDifficulty_);
            constexpr int WordsPerPage = 60;
            const int end = std::min(count, previewWordOffset_ + WordsPerPage);
            for (int index = previewWordOffset_; index < end; ++index) {
                const int displayIndex = index - previewWordOffset_;
                const int column = displayIndex / 10;
                const int row = displayIndex % 10;
                // The section's record 0 is the exemplar already used in the
                // centered "as in" label. The original grid starts at +0x0E,
                // which is record 1, and displays exactly `count` words.
                renderer.drawText(previewFont, GridLeft + column * columnStep,
                                  GridTop + row * LineHeight,
                                  section->records[static_cast<std::size_t>(index + 1)].text(),
                                  Colors::White);
            }
        }
    }
    renderer.drawText(font, 0, 176, "Press Space Bar to see more.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Set Content Menu", Colors::White);
}

void WordGame::renderOptionsEraseHall(Renderer& renderer) {
    drawOptionsFrame(renderer, "Erase Hall of Fame");
    const GemFont& font = assets_.largeFont();
    if (eraseDraft_.empty()) {
        const bool originallyEmpty = hall_.entries().empty();
        renderer.drawText(font, 0, originallyEmpty ? 90 : 85,
                          originallyEmpty ? "There are currently no entries in the"
                                          : "There are no more entries in the",
                          Colors::White);
        renderer.drawText(font, 0, originallyEmpty ? 99 : 94,
                          "Hall of Fame.", Colors::White);
        if (!originallyEmpty) {
            renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
            renderer.drawText(font, 0, 185, "Escape: Cancel changes", Colors::White);
        } else {
            renderer.drawCenteredText(font, 159, 176,
                                      "Press Space Bar to continue.", Colors::White);
        }
        return;
    }
    for (int index = 0; index < static_cast<int>(eraseDraft_.size()); ++index) {
        const int y = 38 + index * 11;
        const WordHallScoreEntry& entry = eraseDraft_[static_cast<std::size_t>(index)];
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, 64 - renderer.textWidth(font, prefix), y,
                          prefix, Colors::White);
        if (eraseSelection_ == index) {
            renderer.fillRect(62, y - 1,
                              renderer.textWidth(font, entry.name) + 21, 10,
                              Colors::White);
        }
        const std::uint32_t color = eraseSelection_ == index ? Colors::Black : Colors::White;
        renderer.drawText(font, 72, y, entry.name, color);
    }
    renderer.drawText(font, 0, 167, "Use arrows to move, Space Bar to delete.", Colors::White);
    renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Cancel changes", Colors::White);
}

void WordGame::renderOptionsPassword(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Password");
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Enter a new password:", Colors::White);
    renderer.outlineRect(110, 50, 99, 20, Colors::White);
    renderer.drawText(font, 115, 56, passwordDraft_, Colors::White);
    if (!passwordEditingHint_) {
        renderer.fillRect(115 + renderer.textWidth(font, passwordDraft_), 61, 9, 3,
                          Colors::White);
    }
    renderer.drawText(font, 5, 77, "Enter a hint:", Colors::White);
    renderer.outlineRect(1, 95, 317, 20, Colors::White);
    renderer.drawText(font, 6, 101, hintDraft_, Colors::White);
    if (passwordEditingHint_) {
        renderer.fillRect(6 + renderer.textWidth(font, hintDraft_), 106, 9, 3,
                          Colors::White);
    }
    renderer.drawText(font, 0, 167, "Type entry, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Options Menu", Colors::White);
}

void WordGame::renderOptionsPasswordPrompt(Renderer& renderer) {
    drawOptionsFrame(renderer, "Options", passwordRejected_ ? 162 : 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Enter the password:", Colors::White);
    renderer.outlineRect(110, 80, 99, 20, Colors::White);
    renderer.drawText(font, 115, 86, passwordAttempt_, Colors::White);
    renderer.fillRect(115 + renderer.textWidth(font, passwordAttempt_), 91, 9, 3,
                      Colors::White);
    if (!optionHint_.empty()) {
        renderer.drawCenteredText(assets_.smallFont(), 159, 120,
                                  optionHint_, Colors::White);
    }
    if (passwordRejected_) {
        constexpr int top = 67;
        constexpr int bottom = 131;
        renderer.fillRect(0, top, 320, bottom - top + 1, Colors::Black);
        renderer.horizontalLine(0, 319, top, Colors::Cyan);
        renderer.horizontalLine(2, 317, top + 2, Colors::Cyan);
        renderer.horizontalLine(2, 317, bottom - 2, Colors::Cyan);
        renderer.horizontalLine(0, 319, bottom, Colors::Cyan);
        renderer.verticalLine(0, top, bottom, Colors::Cyan);
        renderer.verticalLine(319, top, bottom, Colors::Cyan);
        renderer.verticalLine(2, top + 2, bottom - 2, Colors::Cyan);
        renderer.verticalLine(317, top + 2, bottom - 2, Colors::Cyan);
        renderer.drawCenteredText(font, 159, 81, "That password is not correct.", Colors::White);
        renderer.drawCenteredText(font, 159, 99, "Please try again.", Colors::White);
        renderer.fillRect(0, 166, Renderer::Width, Renderer::Height - 166,
                          Colors::Black);
        renderer.drawCenteredText(font, 159, 176,
                                  "Press Space Bar to continue.", Colors::White);
    }
    if (!passwordRejected_) {
        renderer.drawText(font, 0, 176, "Type entry, then press Enter.", Colors::White);
        renderer.drawText(font, 0, 185, "Escape: Main Menu", Colors::White);
    }
}

void WordGame::renderOptionsJoystickCalibration(Renderer& renderer) {
    drawOptionsFrame(renderer, "Calibrate Joystick", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 0, 176, "Press Space Bar to continue.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);

    constexpr int top = 81;
    constexpr int bottom = 118;
    renderer.fillRect(0, top, 320, bottom - top + 1, Colors::Black);
    renderer.horizontalLine(0, 319, top, Colors::Cyan);
    renderer.horizontalLine(2, 317, top + 2, Colors::Cyan);
    renderer.horizontalLine(2, 317, bottom - 2, Colors::Cyan);
    renderer.horizontalLine(0, 319, bottom, Colors::Cyan);
    renderer.verticalLine(0, top, bottom, Colors::Cyan);
    renderer.verticalLine(319, top, bottom, Colors::Cyan);
    renderer.verticalLine(2, top + 2, bottom - 2, Colors::Cyan);
    renderer.verticalLine(317, top + 2, bottom - 2, Colors::Cyan);
    const std::string_view prompt = joystickCalibrationStep_ < 0
        ? std::string_view("Attach joystick to computer.")
        : JoystickCalibrationPrompts[static_cast<std::size_t>(
              std::clamp(joystickCalibrationStep_, 0,
                         static_cast<int>(JoystickCalibrationPrompts.size()) - 1))];
    renderer.drawCenteredText(font, 159, 95, prompt, Colors::White);
}

void WordGame::drawOptionsFrame(Renderer& renderer, const std::string_view title,
                                const int footerDividerTop) {
    renderer.clear(Colors::Black);
    renderer.drawCenteredText(assets_.largeFont(), 159, 5, title, Colors::White);
    renderer.horizontalLine(2, 317, 17, Colors::Cyan);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.horizontalLine(0, 319, footerDividerTop, Colors::Cyan);
    renderer.horizontalLine(2, 317, footerDividerTop + 2, Colors::Cyan);
}

void WordGame::drawNumberedMenu(Renderer& renderer,
                                const std::vector<std::string_view>& labels,
                                const int firstBaseline, const int left) {
    const GemFont& font = assets_.largeFont();
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const int y = firstBaseline + index * 11;
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, left + 8, y, prefix, Colors::White);
        if (menuSelection_ == index) {
            renderer.fillRect(left + 22, y - 1,
                              renderer.textWidth(font, labels[static_cast<std::size_t>(index)]) + 21,
                              10, Colors::White);
        }
        renderer.drawText(font, left + 32, y,
                          labels[static_cast<std::size_t>(index)],
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
}

int WordGame::selectedSoundCount(const std::array<bool, 20>& sounds) const {
    return static_cast<int>(std::count(sounds.begin(), sounds.end(), true));
}

int WordGame::targetWordCount(const std::array<bool, 20>& sounds,
                              const int difficulty) const {
    if (difficulty < 0 || difficulty >= 8) return 0;
    int count = 0;
    for (std::size_t sound = 0; sound < sounds.size(); ++sound) {
        if (!sounds[sound]) continue;
        count += targetAvailabilityCount(static_cast<int>(sound), difficulty);
    }
    return count;
}

bool WordGame::commitContentDraft() {
    if (targetWordCount(contentDraftSounds_, contentDraftDifficulty_) <= 0) return false;
    if (!core_.configure(contentDraftSounds_, contentDraftDifficulty_)) return false;
    wordDifficulty_ = contentDraftDifficulty_;
    selectedSounds_ = contentDraftSounds_;
    saveSettings();
    return true;
}

std::wstring WordGame::settingsPath() const {
    if (!settingsPathOverride_.empty()) return settingsPathOverride_;
    PWSTR localApplicationData = nullptr;
    std::wstring result = L"word-munchers-settings.txt";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                                       &localApplicationData))) {
        std::filesystem::path directory(localApplicationData);
        CoTaskMemFree(localApplicationData);
        directory /= L"NumberMunchersNative";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        result = (directory / L"word-settings.txt").wstring();
    }
    return result;
}

void WordGame::loadSettings() {
    if (!settingsPersistenceEnabled_) return;
    std::ifstream input{std::filesystem::path(settingsPath())};
    std::string signature;
    int version = 0;
    int difficulty = 0;
    int sound = 0;
    int music = 0;
    int joystick = 0;
    int speaker = 0;
    int calibrated = 0;
    std::uint64_t left = 0;
    std::uint64_t right = 0;
    std::uint64_t up = 0;
    std::uint64_t down = 0;
    std::string password;
    std::string hint;
    if (!(input >> signature >> version) ||
        signature != "WORD_MUNCHERS_NATIVE_SETTINGS" || version != 1 ||
        !(input >> difficulty >> sound >> music >> joystick >> speaker >> calibrated >>
          left >> right >> up >> down >> std::quoted(password) >> std::quoted(hint))) {
        return;
    }
    const auto binaryFlag = [](const int value) { return value == 0 || value == 1; };
    if (difficulty < 0 || difficulty >= 8 || !binaryFlag(sound) || !binaryFlag(music) ||
        !binaryFlag(joystick) || !binaryFlag(speaker) || !binaryFlag(calibrated) ||
        left > std::numeric_limits<std::uint32_t>::max() ||
        right > std::numeric_limits<std::uint32_t>::max() ||
        up > std::numeric_limits<std::uint32_t>::max() ||
        down > std::numeric_limits<std::uint32_t>::max() ||
        (calibrated != 0 && (left >= right || up >= down)) ||
        password.size() > 10 || hint.size() > 50) {
        return;
    }

    std::array<bool, 20> sounds{};
    for (bool& selected : sounds) {
        int stored = 0;
        if (!(input >> stored) || !binaryFlag(stored)) return;
        selected = stored != 0;
    }
    if (targetWordCount(sounds, difficulty) <= 0) return;

    std::size_t hallCount = 0;
    if (!(input >> hallCount) || hallCount > WordHall::Capacity) return;
    std::vector<WordHallScoreEntry> entries;
    entries.reserve(hallCount);
    for (std::size_t index = 0; index < hallCount; ++index) {
        std::string name;
        std::uint64_t score = 0;
        if (!(input >> std::quoted(name) >> score) || name.empty() ||
            name.size() > WordHall::NameLimit || score > MuncherScore::MaximumScore) {
            return;
        }
        entries.push_back({std::move(name), static_cast<std::uint32_t>(score)});
    }
    input >> std::ws;
    if (!input.eof()) return;
    WordHall candidateHall;
    if (!candidateHall.replace(entries)) return;

    wordDifficulty_ = difficulty;
    selectedSounds_ = sounds;
    contentDraftDifficulty_ = difficulty;
    contentDraftSounds_ = sounds;
    soundOn_ = sound != 0;
    musicOn_ = music != 0;
    joystickEnabled_ = joystick != 0;
    speakerEffects_ = speaker != 0;
    joystickCalibrated_ = calibrated != 0;
    joystickLeftThreshold_ = static_cast<std::uint32_t>(left);
    joystickRightThreshold_ = static_cast<std::uint32_t>(right);
    joystickUpThreshold_ = static_cast<std::uint32_t>(up);
    joystickDownThreshold_ = static_cast<std::uint32_t>(down);
    optionPassword_ = std::move(password);
    optionHint_ = std::move(hint);
    (void)core_.configure(selectedSounds_, wordDifficulty_);
    (void)hall_.replace(std::move(entries));
}

void WordGame::beginConfigurationWriteError() {
    configurationWriteErrorBackgroundPage_ = page_;
    Renderer background(assets_.graphicsMode());
    render(background);
    configurationWriteErrorBackgroundPixels_ = background.pixels();
    configurationWriteError_ = true;
}

void WordGame::dismissConfigurationWriteError() {
    const bool continueController = configurationWriteErrorPostDismissContinuation_;
    configurationWriteError_ = false;
    configurationWriteErrorPostDismissContinuation_ = false;
    configurationWriteErrorBackgroundPixels_.clear();
    if (continueController) keyDown(VK_PROCESSKEY);
}

void WordGame::saveSettings() {
    if (!settingsPersistenceEnabled_) return;
    std::ofstream output(std::filesystem::path(settingsPath()), std::ios::trunc);
    if (!output) {
        beginConfigurationWriteError();
        return;
    }
    output << "WORD_MUNCHERS_NATIVE_SETTINGS 1\n";
    output << wordDifficulty_ << ' ' << (soundOn_ ? 1 : 0) << ' '
           << (musicOn_ ? 1 : 0) << ' ' << (joystickEnabled_ ? 1 : 0) << ' '
           << (speakerEffects_ ? 1 : 0) << ' ' << (joystickCalibrated_ ? 1 : 0) << ' '
           << joystickLeftThreshold_ << ' ' << joystickRightThreshold_ << ' '
           << joystickUpThreshold_ << ' ' << joystickDownThreshold_ << ' '
           << std::quoted(optionPassword_) << ' ' << std::quoted(optionHint_) << '\n';
    for (const bool selected : selectedSounds_) output << (selected ? 1 : 0) << ' ';
    output << '\n' << hall_.entries().size() << '\n';
    for (const WordHallScoreEntry& entry : hall_.entries()) {
        output << std::quoted(entry.name) << ' ' << entry.score << '\n';
    }
    output.flush();
    if (!output) {
        beginConfigurationWriteError();
    }
}

void WordGame::toggleVowelChoice(const int choice) {
    if (choice < 0 || choice >= static_cast<int>(VowelChoiceLabels.size())) return;
    if (choice < 6) {
        const std::size_t first = static_cast<std::size_t>(choice * 2);
        const bool selected = contentDraftSounds_[first] && contentDraftSounds_[first + 1];
        contentDraftSounds_[first] = !selected;
        contentDraftSounds_[first + 1] = !selected;
    } else {
        const std::size_t sound = static_cast<std::size_t>(choice + 6);
        contentDraftSounds_[sound] = !contentDraftSounds_[sound];
    }
}

std::uint8_t WordGame::vowelChoiceState(const int choice) const {
    if (choice < 0 || choice >= static_cast<int>(VowelChoiceLabels.size())) return 0x02u;
    bool selected = false;
    if (choice < 6) {
        const std::size_t first = static_cast<std::size_t>(choice * 2);
        selected = contentDraftSounds_[first] && contentDraftSounds_[first + 1];
    } else {
        selected = contentDraftSounds_[static_cast<std::size_t>(choice + 6)];
    }

    // 13F9B sets raw state bit 1 from DS:26F6 row visible-choice+6. A zero
    // target count means "Not Available" but does not block Space/F2
    // selection, permitting the original raw states 2 and 3.
    const bool available =
        targetAvailabilityCount(choice + 6, std::clamp(contentDraftDifficulty_, 0, 7)) > 0;
    return static_cast<std::uint8_t>((selected ? 0x01u : 0x00u) |
                                     (available ? 0x00u : 0x02u));
}

int WordGame::vowelValidationMessage() const {
    // 13D65 does not count raw selection bits. It walks the fourteen visible
    // state bytes at DS:6432 and increments only when a byte is exactly 1.
    // A selected-but-unavailable choice is state 3 and is therefore ignored
    // by the all/group validation calls at 146B1, 146D7, and 14705.
    int selectedAvailable = 0;
    int groupTwo = 0;
    int groupThree = 0;
    for (int choice = 0; choice < static_cast<int>(VowelChoiceLabels.size()); ++choice) {
        if (vowelChoiceState(choice) != 0x01u) continue;
        ++selectedAvailable;
        if (choice >= 6 && choice <= 8) ++groupTwo;
        if (choice >= 9) ++groupThree;
    }
    if (selectedAvailable == 0) return 1;
    if (groupTwo == 1) return 2;
    return groupThree == 1 ? 3 : 0;
}

void WordGame::beginWordPreview() {
    previewSound_ = -1;
    previewWordOffset_ = 0;
    for (int sound = 0; sound < 20; ++sound) {
        if (contentDraftSounds_[static_cast<std::size_t>(sound)] &&
            targetAvailabilityCount(sound, contentDraftDifficulty_) > 0) {
            previewSound_ = sound;
            break;
        }
    }
    if (previewSound_ >= 0) {
        page_ = WordGamePage::OptionsPreview;
    } else {
        // Set Content checks the target total at 0x13801. Its zero branch at
        // 0x1381A displays the same DS:2836 flag-1 warning used by a failed
        // commit and then returns to the Set Content loop.
        contentInsufficientMessage_ = true;
    }
}

void WordGame::advanceWordPreview() {
    if (previewSound_ < 0 || previewSound_ >= 20) {
        page_ = WordGamePage::OptionsContent;
        menuSelection_ = 2;
        return;
    }
    constexpr int WordsPerPage = 60;
    const int count = targetAvailabilityCount(previewSound_, contentDraftDifficulty_);
    if (previewWordOffset_ + WordsPerPage < count) {
        previewWordOffset_ += WordsPerPage;
        return;
    }
    for (int sound = previewSound_ + 1; sound < 20; ++sound) {
        if (contentDraftSounds_[static_cast<std::size_t>(sound)] &&
            targetAvailabilityCount(sound, contentDraftDifficulty_) > 0) {
            previewSound_ = sound;
            previewWordOffset_ = 0;
            return;
        }
    }
    page_ = WordGamePage::OptionsContent;
    menuSelection_ = 2;
}

void WordGame::renderHall(Renderer& renderer) {
    const bool cga = renderer.graphicsMode() == GraphicsMode::Cga4;
    const std::uint32_t normalText = cga ? Colors::White : Colors::Black;
    renderer.clear(cga ? Colors::Black : Colors::White);
    if (const Image* hall = assets_.image(6003)) {
        renderer.drawImage(*hall, 0, 0, false, false, true);
    }
    const GemFont& largeFont = assets_.largeFont();
    const GemFont& smallFont = assets_.smallFont();

    // The caller installs a y=30 clamp in the flags-0x140 formatter.  Its
    // five-pixel inset places line zero at 35; Hall of Fame is line three.
    renderer.drawCenteredText(largeFont, 159, 35, "WORD MUNCHERS", normalText);
    renderer.drawCenteredText(largeFont, 159, 62, "Hall of Fame", normalText);
    if (hall_.entries().empty()) {
        // The empty-list source has two leading line feeds.  Its y=97 clamp
        // and the common formatter's five-pixel inset put these at 120/129.
        renderer.drawCenteredText(largeFont, 159, 120, "There are no entries", normalText);
        renderer.drawCenteredText(largeFont, 159, 129, "in the Hall of Fame.", normalText);
    } else {
        int highlighted = -1;
        if (!hallHighlightName_.empty()) {
            const auto found = std::find_if(
                hall_.entries().begin(), hall_.entries().end(), [this](const auto& entry) {
                    return entry.name == hallHighlightName_ && entry.score == hallHighlightScore_;
                });
            if (found != hall_.entries().end()) {
                highlighted = static_cast<int>(found - hall_.entries().begin());
            }
        }
        int y = 97;
        for (std::size_t index = 0;
             index < std::min<std::size_t>(10, hall_.entries().size()); ++index) {
            const WordHallScoreEntry& entry = hall_.entries()[index];
            const std::string rank = std::to_string(index + 1);
            const std::string label = rank + ". " + entry.name;
            const std::string score = entry.score >= MuncherScore::MaximumScore
                                          ? "Perfect"
                                          : std::to_string(entry.score);
            const int labelX = 57 - renderer.textWidth(smallFont, rank);
            const int scoreX = 262 - renderer.textWidth(smallFont, score);
            if (static_cast<int>(index) == highlighted) {
                // The original switches bit 0x20 off, making both outtext
                // calls opaque. VGA uses white-on-black; CGA uses
                // white-on-magenta. Each call paints its own text extent.
                const std::uint32_t background = cga ? Colors::Magenta : Colors::Black;
                renderer.fillRect(labelX, y, renderer.textWidth(smallFont, label),
                                  smallFont.height(), background);
                renderer.fillRect(scoreX, y, renderer.textWidth(smallFont, score),
                                  smallFont.height(), background);
                renderer.drawText(smallFont, labelX, y, label, Colors::White);
                renderer.drawText(smallFont, scoreX, y, score, Colors::White);
            } else {
                renderer.drawText(smallFont, labelX, y, label, normalText);
                renderer.drawText(smallFont, scoreX, y, score, normalText);
            }
            y += 9;
        }
    }
    if (!attractMode_) {
        // The original source includes a leading blank.  Centering that full
        // string produces the same visible x=51 start for the nonblank text.
        renderer.drawText(largeFont, 51, 185, "Press Space Bar to continue.", normalText);
    } else {
        renderer.drawCenteredText(largeFont, 159, 185,
                                  "Press a key for Muncher Menu", normalText);
    }
}

void WordGame::drawSprite(Renderer& renderer, const std::uint32_t sheetId, const int frame,
                          const int x, const int y, const bool remapTitlePalette,
                          const bool remapHallPalette,
                          const Image* indexedPaletteSource,
                          const bool transparentBlack,
                          const bool saturateSceneClip,
                          const bool clipToBoardInterior,
                          const int sourceWidthLimit) {
    const auto source = assets_.spriteFrame(sheetId, frame);
    // A BTMP record names the PCXF sheet that owns its rectangle. Word
    // cartoons 2003 and 2005 move their final frames to continuation sheets
    // 3003 and 3005, so the logical animation ID is not always the source
    // image ID.
    const std::uint32_t sourceSheetId =
        source && source->sheetId != 0 ? source->sheetId : sheetId;
    const Image* sheet = assets_.image(sourceSheetId);
    if (!sheet) return;
    int sourceX = 12 + (frame % 4) * 48;
    int sourceY = 17 + ((frame / 4) % 3) * 40;
    int sourceWidth = std::min(38, sheet->width - sourceX);
    int sourceHeight = std::min(38, sheet->height - sourceY);
    if (source) {
        sourceX = source->x;
        sourceY = source->y;
        sourceWidth = source->width;
        sourceHeight = source->height;
    }
    if (sourceWidthLimit >= 0) sourceWidth = std::min(sourceWidth, sourceWidthLimit);
    if (sourceWidth <= 0) return;
    int destinationX = x;
    int destinationY = y;
    if (saturateSceneClip) {
        // The MECC rectangle clipper saturates a wholly positive-overflowing
        // rectangle at its right/bottom endpoint. The corresponding first
        // source column/row remains visible at x=319 or y=199.
        if (destinationX >= Renderer::Width) destinationX = Renderer::Width - 1;
        if (destinationY >= Renderer::Height) destinationY = Renderer::Height - 1;
    }
    if (clipToBoardInterior) {
        // Edge arrivals are clipped by the board's interior viewport.  In
        // particular, wm_011 shows a top-edge Reggie progressively revealed
        // below y=27 without disturbing the y=26 grid rule or the header.
        constexpr int ClipLeft = BoardLeft + 1;
        constexpr int ClipTop = BoardTop + 1;
        // The arrival viewport preserves the outer x=310 outline but includes
        // the x=308 grid rule and x=309 gutter. Number nm_001 exposes this
        // right-edge asymmetry directly.
        constexpr int ClipRight = BoardRight + 2;
        // The shared DOS arrival viewport also extends through y=177 while
        // preserving the y=178 outer outline at the bottom edge.
        constexpr int ClipBottom = BoardBottom + 2;
        if (destinationX < ClipLeft) {
            const int clipped = ClipLeft - destinationX;
            sourceX += clipped;
            sourceWidth -= clipped;
            destinationX = ClipLeft;
        }
        if (destinationY < ClipTop) {
            const int clipped = ClipTop - destinationY;
            sourceY += clipped;
            sourceHeight -= clipped;
            destinationY = ClipTop;
        }
        sourceWidth = std::min(sourceWidth, ClipRight - destinationX);
        sourceHeight = std::min(sourceHeight, ClipBottom - destinationY);
        if (sourceWidth <= 0 || sourceHeight <= 0) return;
    }
    renderer.drawImageRegion(*sheet, sourceX, sourceY, sourceWidth, sourceHeight,
                             destinationX, destinationY, sourceWidth, sourceHeight,
                             transparentBlack,
                             sheetId == 1006,
                             remapTitlePalette, remapHallPalette,
                             sheetId >= 1007 && sheetId <= 1011 &&
                                 !remapTitlePalette && !remapHallPalette,
                             indexedPaletteSource);
}

void WordGame::renderBoard(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    // Word 0x110ae paints the left header at (0,6), or Demo at (30,6).
    if (attractMode_) {
        renderer.drawText(font, 30, 6, "Demo", Colors::White);
    } else {
        renderer.drawText(font, 0, 6, "Level: " + std::to_string(core_.level()), Colors::White);
    }
    const int targetSound = core_.board().targetSound;
    if (targetSound >= 0 && targetSound < static_cast<int>(wordSoundLabels().size())) {
        // The question owner first blits BTMP 6001[targetSound] into the
        // inclusive (260,0)..(306,22) header rectangle. Black record pixels
        // are transparent, leaving only the 20 exact exemplar pictures.
        // The board inherits the yellow/olive DAC installed by the shared
        // Hall/gameplay palette; PCXF 6001's local green ramp is therefore
        // not the color that reaches the display. wm_011 measures all four
        // ramp entries on the tree exemplar at their resident-DAC values.
        drawSprite(renderer, 6001, targetSound, 260, 0, false, true,
                   assets_.image(6003));
        // Word 0x10cf5 selects DS:2026[targetSound]; helper 0x0d2cb centers
        // the label over x=20..308 at x=164. The switch at 0x10d81 then
        // applies the driver's macron/breve overstrike to sounds 0..11;
        // sound 5 alone moves the underlying label to baseline 7.
        const std::string_view label =
            wordSoundLabels()[static_cast<std::size_t>(targetSound)];
        renderer.drawCenteredText(font, 164, targetSound == 5 ? 7 : 6,
                                  label, Colors::White);
        if (targetSound < 12) {
            const int labelX = 164 - renderer.textWidth(font, label) / 2;
            const int markX = labelX + renderer.textWidth(font, "/") + 1;
            constexpr int AccentY = 6;
            if ((targetSound & 1) == 0) {
                // Helper 0x10B49 uses inclusive x..x+6 and x..x+14
                // macrons for the one- and two-character sound spellings.
                const int macronWidth = targetSound >= 10 ? 15 : 7;
                renderer.horizontalLine(markX - 1, markX + macronWidth - 2,
                                        AccentY, Colors::White);
            } else if (targetSound < 10) {
                renderer.fillRect(markX - 2, AccentY - 1, 2, 1, Colors::White);
                renderer.fillRect(markX + 5, AccentY - 1, 2, 1, Colors::White);
                renderer.horizontalLine(markX, markX + 4, AccentY, Colors::White);
            } else {
                constexpr int markedWidth = 13;
                renderer.fillRect(markX - 1, AccentY - 1, 2, 1, Colors::White);
                renderer.fillRect(markX + markedWidth - 3, AccentY - 1,
                                  2, 1, Colors::White);
                renderer.horizontalLine(markX + 1, markX + markedWidth - 4,
                                        AccentY, Colors::White);
            }
        }
    }
    renderer.horizontalLine(92, 234, 2, Colors::White);
    renderer.horizontalLine(92, 234, 16, Colors::White);
    const auto drawCellForeground = [&](const bool repaintPayloadBackgrounds) {
        renderer.outlineRect(18, 24, 293, 155, Colors::Magenta);
        for (int row = 0; row <= BoardRows; ++row) {
            renderer.horizontalLine(BoardLeft, BoardRight,
                                    BoardTop + row * BoardCellHeight, Colors::Magenta);
        }
        for (int column = 0; column <= BoardColumns; ++column) {
            renderer.verticalLine(BoardLeft + column * BoardCellWidth,
                                  BoardTop, BoardBottom, Colors::Magenta);
        }
        for (int row = 0; row < BoardRows; ++row) {
            for (int column = 0; column < BoardColumns; ++column) {
                const int index = row * BoardColumns + column;
                const int x = BoardLeft + column * BoardCellWidth;
                const int y = BoardTop + row * BoardCellHeight;
                if (!core_.eaten(static_cast<std::size_t>(index))) {
                    const std::string label =
                        core_.board().cells[static_cast<std::size_t>(index)].record.text();
                    if (repaintPayloadBackgrounds) {
                        // The DOS dirty-cell text primitive is opaque. Its
                        // blue character box cuts through an actor before the
                        // white glyph pixels are painted back over it.
                        const int labelWidth = renderer.textWidth(font, label);
                        renderer.fillRect(x + (BoardCellWidth - labelWidth) / 2,
                                          y + 12, labelWidth, font.height(),
                                          Colors::BoardBlue);
                    }
                    renderer.drawCenteredText(font, x + BoardCellWidth / 2, y + 12,
                                              label, Colors::White);
                }
                if (safeCells_[static_cast<std::size_t>(index)]) {
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
            }
        }
    };
    drawCellForeground(false);
    if (enemyWarning_) {
        renderer.outlineRect(0, 62, 16, 75, Colors::White);
        constexpr std::string_view warning = "Troggle!";
        for (std::size_t index = 0; index < warning.size(); ++index) {
            renderer.drawText(font, 5, 67 + static_cast<int>(index) * 8 + (index >= 5 ? 1 : 0),
                              warning.substr(index, 1), Colors::White);
        }
    }
    if (!attractMode_) {
        // Word 0x10e84 paints the score; 0x10fe9 caps the reserve row at
        // three frame-17 Munchers at x=148+44*n, y=179.
        renderer.drawText(font, 0, 187, "Score:", Colors::White);
        renderer.outlineRect(50, 184, 65, 13, Colors::White);
        renderer.drawText(font, 56, 187, std::to_string(core_.scoreState().score()), Colors::White);
        for (int life = 0; life < core_.scoreState().visibleReserves(); ++life) {
            drawSprite(renderer, 1006, 17, 148 + life * 44, 179);
        }
    } else {
        // WM 0x10F13 is the Demo branch of the score/footer painter. It
        // centers DS:220F at y=187; the live wm_009 board exposed that this
        // prompt is present even though Score and reserve icons are omitted.
        renderer.drawCenteredText(font, 160, 187,
                                  "Press a key for Muncher Menu", Colors::White);
    }

    int playerFrame = playerTerminalFrame_ >= 0 ? playerTerminalFrame_ : 7;
    double playerX = BoardLeft + playerColumn_ * BoardCellWidth + 4;
    double playerY = BoardTop + playerRow_ * BoardCellHeight + 1;
    if (moving_) {
        const int positionPhase = playerMovementPhase();
        const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
        const double positionProgress = static_cast<double>(positionPhase) / (vertical ? 5.0 : 6.0);
        playerX = BoardLeft + moveFromColumn_ * BoardCellWidth + 4 +
            (moveToColumn_ - moveFromColumn_) * BoardCellWidth * positionProgress;
        playerY = BoardTop + moveFromRow_ * BoardCellHeight + 1 +
            (moveToRow_ - moveFromRow_) * BoardCellHeight * positionProgress;
        playerFrame = playerMoveFrameAtPhase(moveDirection_, positionPhase);
    }
    if (munching_) {
        const double elapsed = MunchAnimationDuration - std::max(0.0, munchTimer_);
        const int elapsedTicks = std::clamp(
            static_cast<int>(std::floor(elapsed * OriginalSceneTicksPerSecond + 1e-7)),
            0, MunchAnimationTicks - 1);
        playerFrame = munchFrameAtTick(elapsedTicks);
    }
    if (page_ == WordGamePage::Feedback && feedbackEnemyType_ < 0 &&
        lastResolution_.kind == WordMunchKind::Wrong) {
        // A wrong chew retains its last visible open-mouth record through both
        // the 150-tick explanation and the clean pre-Wipe board hold.  Seeded
        // DOS run 651 proves the former at row 0; run 595 separately proves
        // that a correct board completion retains closed record 13 instead.
        playerFrame = 12;
    }
    const bool hidePlayer = (page_ == WordGamePage::Feedback && feedbackEnemyType_ >= 0) ||
                            playerRecovering_;
    if (!hidePlayer) {
        // Live first-board captures establish the same 0x414100 output for
        // Word and Number at the one occupied index-111 player pixel. The
        // retained Hall flag still models the original page lifecycle, but
        // this player-sheet slot itself does not change at that boundary.
        const bool useHallPalette =
            hallPaletteActive_ ||
            (attractMode_ && attractPostFeedbackBoard_ &&
             attractHallWipeVariant_ == AttractHallWipeVariant::Clean);
        drawSprite(renderer, 1006, playerFrame,
                   static_cast<int>(std::lround(playerX)), static_cast<int>(std::lround(playerY)),
                   false, useHallPalette);
    }

    // The player record is followed by its dirty-cell foreground repair.
    // Later Troggle jobs paint over those rules and labels; the live Reggie
    // approach exposes that ordering at every crossed cell boundary.
    drawCellForeground(true);
    for (const Enemy& enemy : enemies_) {
        if (enemy.collisionHidden) continue;
        if (attractPostFeedbackBoard_ &&
            attractHallWipeVariant_ == AttractHallWipeVariant::Collision &&
            attractHallTransitionPhase_ !=
                AttractHallTransitionPhase::CollisionTransient &&
            enemy.row == playerRow_ && enemy.column == playerColumn_ &&
            (feedbackEnemySlot_ < 0 || enemy.slot == feedbackEnemySlot_)) {
            continue;
        }
        int enemyFrame = ordinaryEnemyFrame(enemy);
        int movementPhase = 0;
        double enemyX = BoardLeft + enemy.column * BoardCellWidth + 4;
        double enemyY = BoardTop + enemy.row * BoardCellHeight + 1;
        if (enemy.entering && !enemy.moving) {
            enemyX = BoardLeft + enemy.fromColumn * BoardCellWidth + 4;
            enemyY = BoardTop + enemy.fromRow * BoardCellHeight + 1;
        }
        if (enemy.moving) {
            const bool vertical = enemy.direction == 0 || enemy.direction == 2;
            const int stepCount = vertical ? 5 : 6;
            const int phase = enemyMovementPhase(enemy);
            movementPhase = phase;
            const double positionProgress =
                static_cast<double>(phase) / static_cast<double>(stepCount);
            enemyX = BoardLeft + enemy.fromColumn * BoardCellWidth + 4 +
                (enemy.column - enemy.fromColumn) * BoardCellWidth * positionProgress;
            enemyY = BoardTop + enemy.fromRow * BoardCellHeight + 1 +
                (enemy.row - enemy.fromRow) * BoardCellHeight * positionProgress;
            if (enemy.entering && movementPhase >= 2) {
                // The actor job becomes paintable on its third interpolation
                // callback.  Its entry-owned pose writes then run middle,
                // forward, middle, back, middle before the dwell takes over
                // at the endpoint.  The complete top-edge Reggie in wm_011
                // exposes phases 2..5; the left-edge Reggie in Number
                // nm_000 independently exposes the horizontal phase 6 alias.
                constexpr std::array<int, 5> EntryPoseCycle = {1, 2, 1, 0, 1};
                enemyFrame = enemy.direction * 3 + EntryPoseCycle[
                    static_cast<std::size_t>(movementPhase - 2)];
            } else if (!enemy.entering && movementPhase > 0) {
                constexpr std::array<int, 4> PoseCycle = {2, 1, 0, 1};
                enemyFrame = enemy.direction * 3 + PoseCycle[
                    static_cast<std::size_t>(movementPhase - 1) % PoseCycle.size()];
            }
        }
        if (enemy.cannibalizing) {
            const double elapsed = TroggleCannibalAnimationDuration -
                                   std::max(0.0, enemy.cannibalTimer);
            const int elapsedTicks = std::max(
                0, static_cast<int>(std::floor(
                       elapsed * OriginalSceneTicksPerSecond + 1e-7)));
            enemyFrame = troggleBiteFrameAtTick(elapsedTicks);
        }
        if (page_ == WordGamePage::Feedback && feedbackEnemyType_ >= 0 &&
            enemy.row == playerRow_ && enemy.column == playerColumn_ &&
            (feedbackEnemySlot_ < 0 || enemy.slot == feedbackEnemySlot_)) {
            if (deathAnimating_) {
                enemyFrame = troggleBiteFrameAtTick(deathSequenceTicks_);
            } else if (!attractPostFeedbackBoard_) {
                // Shared state-5 feedback holds bite record 12 only while the
                // text is present. The collision-transient board returns to
                // the survivor's terminal dwell record before the later Word
                // transition hides that actor.
                enemyFrame = 12;
            }
        }
        const int roundedEnemyX = static_cast<int>(std::lround(enemyX));
        const int roundedEnemyY = static_cast<int>(std::lround(enemyY));
        if (enemy.entering && (!enemy.moving || movementPhase < 2)) {
            // The warning owns no visible actor, and the first two entry
            // callbacks remain wholly suppressed before the clipped reveal.
            continue;
        }
        if (enemy.entering) {
            const int left = std::max(roundedEnemyX, BoardLeft + 1);
            const int top = std::max(roundedEnemyY, BoardTop + 1);
            const int right = std::min(roundedEnemyX + 41, BoardRight + 2);
            const int bottom = std::min(roundedEnemyY + 29, BoardBottom + 2);
            if (left < right && top < bottom) {
                renderer.fillRect(left, top, right - left, bottom - top,
                                  Colors::BoardBlue);
            }
            const bool concurrentPlayerFinalRightEntry =
                enemy.fromColumn >= BoardColumns && movementPhase == 6 && moving_ &&
                playerMovementPhase() == 4;
            if (enemy.fromColumn >= BoardColumns &&
                !concurrentPlayerFinalRightEntry) {
                // A right entry normally retains the overwritten x=308 rule
                // through phase 6; the idle level-9 entrant on native page
                // 632/source run 736 proves that hold. In the seeded level-6
                // overlap, however, the earlier player movement callback owns
                // the resident page when the entrant reaches phase 6 and the
                // player reaches phase 4. Source run 263 restores all 29 rule
                // pixels at x=308/y=117..145,
                // while every other pixel is identical to native page 221.
                // Preserve that callback-local surface without changing either
                // logical job, deadline, or PRNG ownership.
                renderer.fillRect(BoardRight, roundedEnemyY, 1, 29,
                                  Colors::BoardBlue);
            }
            if (enemy.fromRow >= BoardRows) {
                renderer.fillRect(roundedEnemyX, BoardBottom, 41, 1,
                                  Colors::BoardBlue);
            }
        } else if (enemy.exiting) {
            const int fromX = BoardLeft + enemy.fromColumn * BoardCellWidth + 4;
            const int fromY = BoardTop + enemy.fromRow * BoardCellHeight + 1;
            const int left = std::max(std::min(fromX, roundedEnemyX), BoardLeft + 1);
            const int top = std::max(std::min(fromY, roundedEnemyY), BoardTop + 1);
            const int right = std::min(std::max(fromX, roundedEnemyX) + 41,
                                       BoardRight + 2);
            const int bottom = std::min(std::max(fromY, roundedEnemyY) + 29,
                                        BoardBottom + 2);
            if (left < right && top < bottom) {
                renderer.fillRect(left, top, right - left, bottom - top,
                                  Colors::BoardBlue);
            }
        } else if (enemy.moving) {
            const int fromX = BoardLeft + enemy.fromColumn * BoardCellWidth + 4;
            const int fromY = BoardTop + enemy.fromRow * BoardCellHeight + 1;
            renderer.fillRect(std::min(fromX, roundedEnemyX),
                              std::min(fromY, roundedEnemyY),
                              std::abs(roundedEnemyX - fromX) + 41,
                              std::abs(roundedEnemyY - fromY) + 29,
                              Colors::BoardBlue);
        } else {
            renderer.fillRect(roundedEnemyX, roundedEnemyY, 41, 29,
                              Colors::BoardBlue);
        }
        drawSprite(renderer, static_cast<std::uint32_t>(1007 + enemy.type), enemyFrame,
                   roundedEnemyX, roundedEnemyY, false, false, nullptr, true,
                   false, enemy.entering || enemy.exiting);
    }

    if (page_ == WordGamePage::Paused) {
        renderer.fillRect(0, 62, 16, 75, Colors::BoardBlue);
        renderer.outlineRect(0, 62, 16, 75, Colors::White);
        constexpr std::string_view message = "Time out";
        for (std::size_t index = 0; index < message.size(); ++index) {
            renderer.drawText(font, 5, 67 + static_cast<int>(index) * 8,
                              message.substr(index, 1), Colors::White);
        }
    }
    if (page_ == WordGamePage::Feedback && !deathAnimating_ &&
        !(attractMode_ && attractPostFeedbackBoard_)) {
        renderFeedback(renderer);
    }
}

void WordGame::renderQuitConfirm(Renderer& renderer) {
    renderer.fillRect(0, 67, 320, 65, Colors::BoardBlue);
    renderer.horizontalLine(0, 319, 67, Colors::Cyan);
    renderer.horizontalLine(2, 317, 69, Colors::Cyan);
    renderer.horizontalLine(2, 317, 129, Colors::Cyan);
    renderer.horizontalLine(0, 319, 131, Colors::Cyan);
    renderer.verticalLine(0, 67, 131, Colors::Cyan);
    renderer.verticalLine(319, 67, 131, Colors::Cyan);
    renderer.verticalLine(2, 69, 129, Colors::Cyan);
    renderer.verticalLine(317, 69, 129, Colors::Cyan);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 90, "Do you really want to quit?", Colors::White);
    const auto choice = [&](const int x, const std::string_view label, const bool selected) {
        if (selected) renderer.fillRect(x - 2, 109, renderer.textWidth(font, label) + 5, 10,
                                        Colors::White);
        renderer.drawText(font, x, 110, label, selected ? Colors::Black : Colors::White);
    };
    choice(115, " Yes ", menuSelection_ == 0);
    choice(165, " No ", menuSelection_ == 1);
}

void WordGame::renderFeedback(Renderer& renderer) {
    constexpr int feedbackTop = BoardTop + 2 * BoardCellHeight;
    // Word image 0x0d2e1 saves (21,87)..(307,115), exactly one grid-row
    // interior. Preserve the x=20/x=308 sides and y=86/y=116 rules.
    renderer.fillRect(BoardLeft + 1, feedbackTop + 1,
                      BoardRight - BoardLeft - 1, BoardCellHeight - 1,
                      Colors::BoardBlue);
    renderer.horizontalLine(BoardLeft, BoardRight, feedbackTop, Colors::Magenta);
    renderer.horizontalLine(BoardLeft, BoardRight, feedbackTop + BoardCellHeight,
                            Colors::Magenta);
    const GemFont& font = assets_.largeFont();
    // Both Word feedback painters move their two Demo lines from y=88 to
    // y=93 and suppress the user continuation footer (0x0d320/0x0d4aa).
    const int firstBaseline = attractMode_ ? feedbackTop + 7 : feedbackTop + 2;
    if (feedbackEnemyType_ >= 0) {
        renderer.drawCenteredText(font, 164, firstBaseline,
                                  feedbackMessage_ + "! You were eaten by a", Colors::White);
        const int type = std::clamp(feedbackEnemyType_, 0,
                                    static_cast<int>(EnemySpecies.size()) - 1);
        renderer.drawCenteredText(
            font, 168, firstBaseline + 10,
            "Trogglus " + std::string(EnemySpecies[static_cast<std::size_t>(type)]) + ".",
            Colors::White);
    } else if (lastResolution_.kind == WordMunchKind::Wrong) {
        const int index = playerRow_ * BoardColumns + playerColumn_;
        const std::string word = core_.board().cells[static_cast<std::size_t>(index)].record.text();
        renderer.drawCenteredText(font, 164, firstBaseline,
                                  "The word \"" + word + "\" does not have the", Colors::White);
        const int target = core_.board().targetSound;
        std::string sound;
        if (target >= 0 && target < 20) {
            if (const WordSection* section = words_.section(static_cast<std::size_t>(target));
                section && !section->records.empty()) {
                sound = section->records.front().text();
            }
        }
        renderer.drawCenteredText(font, 164, firstBaseline + 10,
                                  "same vowel sound as \"" + sound + ".\"", Colors::White);
    }
    if (!attractMode_) {
        const bool collision = feedbackEnemyType_ >= 0;
        if (collision) {
            renderer.drawCenteredText(font, 168, feedbackTop + 22,
                                      "Press Space Bar to continue.", Colors::White);
        } else {
            renderer.drawCenteredText(font, 168, feedbackTop + 22,
                                      "Press Space Bar to continue.", Colors::White);
        }
    }
}

void WordGame::renderNameEntry(Renderer& renderer) {
    renderer.fillRect(0, 45, 320, 110, Colors::BoardBlue);
    renderer.horizontalLine(0, 319, 45, Colors::Cyan);
    renderer.horizontalLine(2, 317, 47, Colors::Cyan);
    renderer.horizontalLine(2, 317, 152, Colors::Cyan);
    renderer.horizontalLine(0, 319, 154, Colors::Cyan);
    renderer.verticalLine(0, 45, 154, Colors::Cyan);
    renderer.verticalLine(319, 45, 154, Colors::Cyan);
    renderer.verticalLine(2, 47, 152, Colors::Cyan);
    renderer.verticalLine(317, 47, 152, Colors::Cyan);

    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 59, "Congratulations!", Colors::White);
    if (nameEntryMessageType_ == 3) {
        renderer.drawCenteredText(font, 159, 68, "A Word Munchers Hall of Fame", Colors::White);
        renderer.drawCenteredText(font, 159, 77, "perfect score!!!", Colors::White);
    } else if (nameEntryMessageType_ == 2) {
        renderer.drawCenteredText(font, 159, 68, "You have the Word Munchers", Colors::White);
        renderer.drawCenteredText(font, 159, 77, "Hall of Fame best score!", Colors::White);
    } else {
        renderer.drawCenteredText(font, 159, 68, "You made it into", Colors::White);
        renderer.drawCenteredText(font, 159, 77, "the Word Munchers Hall of Fame!", Colors::White);
    }
    renderer.outlineRect(50, 96, 219, 20, Colors::White);
    renderer.drawText(font, 55, 101, nameInput_, Colors::White);
    if (nameCursorVisible_) {
        renderer.fillRect(55 + renderer.textWidth(font, nameInput_), 107, 9, 3, Colors::White);
    }
    renderer.drawCenteredText(font, 159, 131,
                              "Type your name, and press Enter.", Colors::White);
}

void WordGame::renderReplayQuestion(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);

    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 55, 85, "Do you want to play again?", Colors::White);
    const auto choice = [&](const int x, const std::string_view label, const bool selected) {
        if (selected) {
            renderer.fillRect(x - 2, 104, renderer.textWidth(font, label) + 5, 10,
                              Colors::White);
        }
        renderer.drawText(font, x, 105, label, selected ? Colors::Black : Colors::White);
    };
    choice(115, " Yes ", menuSelection_ == 0);
    choice(165, " No ", menuSelection_ == 1);
}

void WordGame::renderLevelComplete(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (!levelCompleteScene_.valid()) return;
    if (sceneSurfacePixels_.size() == renderer.pixels().size()) {
        renderer.replacePixels(sceneSurfacePixels_);
    }
}

void WordGame::renderCheatMenu(Renderer& renderer) {
    renderer.fillRect(40, 50, 240, 100, Colors::Black);
    renderer.outlineRect(40, 50, 240, 100, Colors::Cyan);
    renderer.outlineRect(42, 52, 236, 96, Colors::Magenta);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 60, "CHEAT / LEVEL SELECT", Colors::Yellow);
    const std::array<std::string, 2> items = {
        "Start level < " + std::to_string(cheatLevel_) + " >", "Close"
    };
    for (int index = 0; index < 2; ++index) {
        const int y = 84 + index * 22;
        const std::string& text = items[static_cast<std::size_t>(index)];
        const int x = (320 - renderer.textWidth(font, text)) / 2;
        if (cheatSelection_ == index) {
            renderer.fillRect(x - 4, y - 2, renderer.textWidth(font, text) + 8, 12,
                              Colors::White);
        }
        renderer.drawText(font, x, y, text,
                          cheatSelection_ == index ? Colors::Black : Colors::White);
    }
}
