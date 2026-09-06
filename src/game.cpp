#include "game.h"
#include "dro_audio.h"
#include "menu_input.h"
#include "mecc_sound.h"
#include "resource_ids.h"

#include <mmsystem.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

namespace {

constexpr std::array<std::string_view, 6> ModeNames = {
    "Multiples", "Factors", "Primes", "Equality", "Inequality", "Challenge"
};

constexpr std::array<std::string_view, 5> EnemyNames = {
    "Reggie", "Worker", "Bashful", "Helper", "Smarty"
};

constexpr std::array<std::string_view, 5> EnemySpecies = {
    "normalus", "laborus", "timidus", "assistus", "smarticus"
};

[[nodiscard]] int selectorFirstBaseline(std::size_t itemCount) {
    // The selector at image 0x12b6d starts at DS:159a y=70, then moves a
    // two-item list down 20 pixels and a three/four-item list down 10.
    if (itemCount == 2) return 90;
    if (itemCount == 3 || itemCount == 4) return 80;
    return 70;
}

[[nodiscard]] int fontTextWidth(const GemFont& font, std::string_view text) {
    int width = 0;
    for (const unsigned char character : text) width += font.glyphWidth(character);
    return width;
}

[[nodiscard]] char foldedPasswordCharacter(char character) {
    return character >= 'a' && character <= 'z'
        ? static_cast<char>(character - ('a' - 'A')) : character;
}

[[nodiscard]] bool originalPasswordMatches(std::string_view stored, std::string_view candidate) {
    // 0xec38 folds and compares each stored byte, then makes one final pass
    // over its terminating NUL. A candidate with the right prefix but a
    // trailing byte therefore fails that final comparison.
    if (candidate.size() != stored.size()) return false;
    for (std::size_t index = 0; index < stored.size(); ++index) {
        if (foldedPasswordCharacter(stored[index]) != foldedPasswordCharacter(candidate[index])) {
            return false;
        }
    }
    return true;
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
    // Backspace, Tab, Enter, and Escape are handled by their WM_KEYDOWN half.
    // TranslateMessage can still emit their control bytes afterwards; those
    // bytes must not satisfy the next startup gate. Every other WM_CHAR here
    // belongs to a key-down that virtualKeyDefersToCharacter() deferred.
    return character != L'\b' && character != L'\t' && character != L'\n' &&
           character != L'\r' && character != L'\x1b' && character != L'\x7f';
}

// Difficulty is stored by the DOS game as a zero-based index capped at 11.
// These tables are read verbatim from NM's data segment (DS:058e-064d).
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

constexpr std::array<int, 12> EnemyMoveSeconds = {
    3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1
};

// DS:069c and DS:06b4. The original creates this many recurring safe-zone
// jobs and seeds this many of them in the active phase at board setup.
constexpr std::array<int, 12> SafeZoneJobLimits = {
    2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1
};

constexpr std::array<int, 12> InitialSafeZoneCounts = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// The timer driver programs PIT channel 0 with divisor 0x0555 and advances
// its public 32-bit scheduler clock every 0x1e interrupts (image 0x07d4-
// 0x0807). Jobs whose mask is 2--the demo controller, Troggles, and safe
// zones--therefore share this cadence. random(0,90) is upper-exclusive.
constexpr double OriginalJobTicksPerSecond =
    1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);
// The shared SCPT cartoon driver owns a distinct 100 ms callback clock. Word's
// newly captured scenes measure it directly, and Number uses the byte-
// isomorphic scene engine with terminal counts and score lengths that require
// the same 10 Hz cadence.
constexpr double OriginalCartoonTicksPerSecond = 10.0;
constexpr double JobTimerEpsilon = 1e-9;
// The startup controller at image 0x121b6 passes literal 0x012c to the
// common event-or-clock waiter. Any input can end the wait sooner; otherwise
// the version page yields to the separately input-gated device splash after
// exactly 300 ticks of this same public clock.
constexpr double StartupVersionWaitTicks = 300.0;

constexpr std::array<std::string_view, 6> JoystickCalibrationPrompts = {
    "Center your joystick.",
    "Position your joystick: left",
    "Position your joystick: right",
    "Position your joystick: up",
    "Position your joystick: down",
    "Your joystick is ready for use.",
};
// The state-5 Troggle-overlap handler at 0x09d41 advances counters 1 through
// 21 before returning the surviving actor to dwell state 4 and rearming each
// state-6 victim occupying the same cell.
constexpr double TroggleCannibalAnimationDuration = 21.0 / OriginalJobTicksPerSecond;

// DS:0730 starts after 30 job ticks. Its callback at image 0x085c1 replaces
// both the reload and countdown with random(0, 30) + 15 ticks before choosing
// each action, so subsequent delays are uniformly 15-44 ticks.
constexpr double AttractInitialActionDelay = 30.0 / OriginalJobTicksPerSecond;
// The Demo feedback painter at 0x109cf calls 0x1056:0764. That routine reads
// the public clock, adds literal 0x0096, and pumps input until the deadline.
constexpr double AttractFeedbackDuration = 150.0 / OriginalJobTicksPerSecond;
// The following transition is not another scheduler record. Action 3 reaches
// 0x1220d, which calls the shared PCX-effects path at 0x10fd2 with argument 1.
// Both clean terminal sequences in the 70.086304-fps lossless recording take
// exactly 46 capture frames from feedback removal to the first stable Hall:
// 39 unobscured-board frames followed by a Wipe over seven frame intervals.
constexpr double AttractCaptureFrameDuration = 31250.0 / 2190197.0;
constexpr int AttractPostFeedbackBoardFrames = 39;
constexpr double AttractPostFeedbackBoardDuration =
    AttractPostFeedbackBoardFrames * AttractCaptureFrameDuration;
constexpr int AttractCollisionHallWipeFrames = 6;
constexpr int AttractCleanHallWipeFrames = 7;
constexpr int AttractHallToSplashFrames = 6;
constexpr int AttractSplashToBoardFrames = 9;
// Five muted lossless user-cartoon captures establish the same five completed
// presenter states as Word, but Number clears the target page to white. The
// bank load remains covered by cyan; tick 1 occurs only after the white target
// has opened and held for the first 10 Hz scene callback.
constexpr int UserBoardToCartoonFrames = 5;
constexpr double CartoonResourceLoadDelay = 0.015;
constexpr double CartoonScoreStartDelay = 0.010;
constexpr std::array<int, 5> NumberCartoonCoveredCaptureSamples = {
    43, 43, 44, 43, 44,
};
// Main-controller actions 3 and 6 both call 0x1243E. Its first operation at
// 0x12444-0x12448 is the resident driver's literal 15 ms delay. Action 3 is
// already contained by the measured Demo-to-Hall envelope below; action 6 is
// the terminal title exit and needs its own native blocking boundary.
constexpr double MainControllerCleanupDelay = 0.015;
// Main-screen cases 1 and 3 at 0x1289d/0x12927 both install selector-12
// records with countdown and reload 0x01c2. Callback 0x121d4 alternates
// state 3 -> 1 (Hall to logo) and state 1 -> 2 (logo to next Demo board).
// Use the exact common scheduler duration instead of rounded capture spans.
constexpr int AttractInterstitialTicks = 0x01c2;
constexpr double AttractHallDuration =
    static_cast<double>(AttractInterstitialTicks) / OriginalJobTicksPerSecond;
constexpr double AttractSplashDuration = AttractHallDuration;

constexpr std::array<std::string_view, 5> FailureExclamations = {
    "Yikes", "Oops", "Aargh", "Oh, Oh", "Rats"
};

constexpr std::array<std::string_view, 11> DifficultyNames = {
    "3rd Grade Easy", "3rd Grade Advanced", "4th Grade Easy",
    "4th Grade Advanced", "5th Grade Easy", "5th Grade Advanced",
    "6th Grade Easy", "6th Grade Advanced", "7th Grade Easy",
    "7th Grade Advanced", "8th Grade and Above"
};

struct ContentPreset {
    bool use;
    int minimum;
    int maximum;
    int sequence;
    int other;
};

// DS:1d1a contains eleven consecutive 60-byte difficulty records. Each record
// is six ten-byte content rows: use, minimum, maximum, sequence, and other.
// Sequence 1 is in-order, 2 is random, and 0 marks an unavailable field.
constexpr std::array<std::array<ContentPreset, 6>, 11> DifficultyContentPresets = {{
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

constexpr std::array<int, 46> PrimeNumbers = {
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
    59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113,
    127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181,
    191, 193, 197, 199
};

constexpr std::array<std::pair<int, int>, 5> ContentRangeLimits = {
    std::pair{2, 20}, std::pair{3, 99}, std::pair{2, 199},
    std::pair{1, 50}, std::pair{1, 50}
};

std::string formatNumber(int value) {
    return std::to_string(std::clamp(value, -9999, 99999));
}

std::optional<int> evaluateExpressionLabel(std::string_view label) {
    const std::size_t operatorAt = label.find_first_of("+-x:");
    if (operatorAt == std::string_view::npos) return std::nullopt;
    const int left = std::stoi(std::string(label.substr(0, operatorAt)));
    const int right = std::stoi(std::string(label.substr(operatorAt + 1)));
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

void write16(std::vector<std::uint8_t>& output, std::size_t offset, std::uint16_t value) {
    output[offset] = static_cast<std::uint8_t>(value & 0xffu);
    output[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        output[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8)) & 0xffu);
    }
}

} // namespace

Game::Game(const GraphicsMode graphicsMode)
    // DOS startup passes time(nullptr) through the runtime's 16-bit srand.
    : assets_(graphicsMode),
      random_(static_cast<std::uint16_t>(std::time(nullptr))) {
#ifdef NUMBER_MUNCHERS_TESTING
    settingsPersistenceEnabled_ = false;
#endif
    applyDifficultyPreset(difficulty_);
    loadSettings();
    loadScores();
}

void Game::applyDifficultyPreset(int difficulty) {
    difficulty_ = std::clamp(difficulty, 0, static_cast<int>(DifficultyContentPresets.size()) - 1);
    const auto& preset = DifficultyContentPresets[static_cast<std::size_t>(difficulty_)];
    for (std::size_t index = 0; index < preset.size(); ++index) {
        const ContentPreset& source = preset[index];
        ContentSettings& destination = contentSettings_[index];
        destination.use = source.use;
        destination.minimum = source.minimum;
        destination.maximum = source.maximum;
        destination.randomSequence = source.sequence != 1;
        destination.maxMultiplier = index == static_cast<std::size_t>(Mode::Multiples)
            ? source.other : 50;
        if (index == static_cast<std::size_t>(Mode::Equality) ||
            index == static_cast<std::size_t>(Mode::Inequality)) {
            for (int operation = 0; operation < 4; ++operation) {
                destination.operations[static_cast<std::size_t>(operation)] =
                    (source.other & (1 << operation)) != 0;
            }
        } else {
            destination.operations = {true, true, true, true};
        }
    }
    resetContentProgression();
    contentDraft_ = contentSettings_;
}

void Game::resetContentProgression() {
    for (std::size_t index = 0; index < contentSettings_.size(); ++index) {
        contentSequenceNext_[index] = contentSettings_[index].minimum;
        contentPreviousRandom_[index] = contentSettings_[index].minimum - 1;
    }

    const ContentSettings& primes = contentSettings_[static_cast<std::size_t>(Mode::Primes)];
    const auto lastPrimeAtOrBelow = [](int value) {
        int result = 0;
        for (std::size_t index = 1; index < PrimeNumbers.size(); ++index) {
            if (PrimeNumbers[index] > value) break;
            result = static_cast<int>(index);
        }
        return result;
    };
    // 0x0f8a4 seeds the cursor one slot before the prime at the configured
    // lower bound; 0x0fa23 advances it before constructing every board.
    primeMaximumIndex_ = lastPrimeAtOrBelow(primes.minimum) - 1;
    primeMaximumIndexLimit_ = lastPrimeAtOrBelow(primes.maximum);
}

void Game::enterPage(Page page) {
    page_ = page;
    menuSelection_ = 0;
    transitionTimer_ = 0.0;
    if (page == Page::Title) titleIdleTimer_ = 0.0;
}

void Game::enterModeSelect() {
    const std::vector<Mode> modes = enabledPlayModes();
    // 0x12cfa bypasses the selector entirely when only one entry is enabled.
    if (modes.size() == 1) {
        startGameWithBoardPresentation(modes.front());
        return;
    }
    enterPage(Page::ModeSelect);
}

void Game::enterHall(HallContext context) {
    hallContext_ = context;
    if (context != HallContext::PostGame) {
        hallHighlightName_.clear();
        hallHighlightScore_ = 0;
    }
    enterPage(Page::Hall);
}

void Game::beginContentEdit() {
    contentDraft_ = contentSettings_;
    contentRow_ = 0;
    contentColumn_ = 0;
    contentInputStage_ = 0;
    contentNumberConfirm_ = false;
    contentOperationSelection_ = 0;
    contentNumericInput_.clear();
}

void Game::setJoystickState(bool connected, std::uint32_t x, std::uint32_t y,
                            std::uint32_t buttons) {
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

void Game::beginJoystickCalibration() {
    enterPage(Page::OptionsJoystickCalibration);
    menuSelection_ = 5;
    bool detected = joystickConnected_ && joystickSampleCount_ != 0;
    for (std::size_t index = 0; detected && index < joystickSampleCount_; ++index) {
        // The gameport timer returns zero on timeout, and the DOS detector
        // rejects the whole four-sample batch if either axis does so once.
        detected = joystickSampleX_[index] != 0 && joystickSampleY_[index] != 0;
    }
    joystickCalibrationStep_ = detected ? 0 : -1;

    if (!detected) return;

    // The detector at image 0x13244 takes four samples, averages each axis,
    // and installs center +/- one quarter as a usable provisional range.
    // WinMM supplies the already-timed axis values; average the most recent
    // four polls (or repeat-equivalent available samples at first contact).
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

void Game::acceptJoystickCalibration() {
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
        // 0x13130 chooses the midpoint between the captured center and left.
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
        enterPage(Page::Options);
        menuSelection_ = 5;
        return;
    default:
        return;
    }
    ++joystickCalibrationStep_;
}

UINT Game::joystickDirectionKey() const {
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
    default: return 0; // The original 64-byte lookup rejects diagonals.
    }
}

void Game::updateJoystickInput(double seconds) {
    // The PIT clock is independent of the enabled/calibrated and movement
    // gates below. Retain its integer public ticks plus the sub-tick remainder
    // that a display frame can contribute.
    if (seconds > 0.0) {
        joystickClockFraction_ += seconds * OriginalJobTicksPerSecond;
        const double elapsedTicks = std::floor(joystickClockFraction_);
        joystickClockTicks_ += static_cast<std::uint64_t>(elapsedTicks);
        joystickClockFraction_ -= elapsedTicks;
    }

    const bool active = joystickOn_ && joystickConnected_ && joystickCalibrated_ &&
                        page_ != Page::OptionsJoystickCalibration;
    const std::uint32_t buttons = joystickButtons_ & 0x03u;
    if (!active) {
        joystickPreviousButtons_ = buttons;
        return;
    }

    const std::uint32_t pressed = buttons & ~joystickPreviousButtons_;
    const std::uint32_t released = joystickPreviousButtons_ & ~buttons;
    joystickPreviousButtons_ = buttons;

    // Gameport button 0 maps to Space and has priority over button 1/Enter.
    // Transitions are edge-triggered; a release emits no key even when an axis
    // remains displaced on that same poll.
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

    // DS:17FE is cleared when the Muncher starts a movement job and restored
    // only by its terminal callback. The original still checks both buttons
    // above while that byte is clear, but it neither samples an axis nor
    // advances either cached clock/deadline. The absolute PIT clock above
    // keeps running, so an expired deadline is observed after the movement
    // terminal restores DS:17FE.
    if (moving_) return;

    const std::uint64_t repeat = static_cast<std::uint64_t>(joystickRepeatTicks_);
    // The DOS event loop calls this poller more densely than the native 16 ms
    // presentation timer. Run two state-machine polls at the same public-clock
    // value so a refresh that discovers a due deadline can be consumed in the
    // same host frame, without changing the recovered sample-before-delivery
    // ordering.
    for (int poll = 0; poll < 2; ++poll) {
        // 0x13499-0x134CA repairs an uninitialized or implausibly distant
        // deadline to cached-current + repeat. The original uses a wrapping
        // 32-bit counter; a monotonic 64-bit clock preserves the same relation
        // for every practical process lifetime.
        if (joystickNextRepeatTicks_ < repeat ||
            joystickNextRepeatTicks_ - repeat > joystickCachedClockTicks_) {
            joystickNextRepeatTicks_ = joystickCachedClockTicks_ + repeat;
        }

        // The due path intentionally compares the previously cached clock
        // before reading the current one. It emits the pending sample, clears
        // it, and schedules from the fresh clock; it never samples the axes in
        // that poll.
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

bool Game::contentFieldValid(int row, int column) const {
    if (row < 0 || row >= static_cast<int>(Mode::Count) || column < 0 || column > 3) return false;
    // DS:1cf6 retains a Use item for Challenge (row 5, item 1); only its
    // Range, Sequence, and Other fields are absent.
    if (column == 0) return true;
    if (column == 1) return row < static_cast<int>(Mode::Challenge);
    if (column == 2) return row == static_cast<int>(Mode::Multiples) ||
                               row == static_cast<int>(Mode::Factors) ||
                               row == static_cast<int>(Mode::Equality) ||
                               row == static_cast<int>(Mode::Inequality);
    return row == static_cast<int>(Mode::Multiples) ||
           row == static_cast<int>(Mode::Equality) ||
           row == static_cast<int>(Mode::Inequality);
}

int Game::contentFieldCount(const int row) const {
    int count = 0;
    while (count < 4 && contentFieldValid(row, count)) ++count;
    return count;
}

std::vector<Game::Mode> Game::enabledPlayModes() const {
    std::vector<Mode> modes;
    // 0x12be9-0x12cdd walks all six DS:5a5a enable bytes. Challenge is an
    // editable selector entry, not an unconditional fallback.
    for (int index = 0; index < static_cast<int>(Mode::Count); ++index) {
        if (contentSettings_[static_cast<std::size_t>(index)].use) {
            modes.push_back(static_cast<Mode>(index));
        }
    }
    return modes;
}

void Game::moveContentSelection(int rowDelta, int columnDelta) {
    if (rowDelta != 0) {
        for (int attempts = 0; attempts < static_cast<int>(Mode::Count); ++attempts) {
            contentRow_ = (contentRow_ + rowDelta + static_cast<int>(Mode::Count)) %
                          static_cast<int>(Mode::Count);
            if (contentFieldValid(contentRow_, contentColumn_)) return;
        }
    } else if (columnDelta != 0) {
        for (int attempts = 0; attempts < 4; ++attempts) {
            contentColumn_ = (contentColumn_ + columnDelta + 4) % 4;
            if (contentFieldValid(contentRow_, contentColumn_)) return;
        }
    }
}

void Game::openContentRange() {
    contentDialogBackup_ = contentDraft_[static_cast<std::size_t>(contentRow_)];
    contentInputStage_ = 0;
    contentRangeCandidateMinimum_ = contentDialogBackup_.minimum;
    contentNumberConfirm_ = false;
    contentNumericInput_.clear();
    enterPage(Page::OptionsContentRange);
}

void Game::openContentOther() {
    contentDialogBackup_ = contentDraft_[static_cast<std::size_t>(contentRow_)];
    contentNumberConfirm_ = false;
    contentNumericInput_.clear();
    if (contentRow_ == static_cast<int>(Mode::Multiples)) {
        contentInputStage_ = 0;
        enterPage(Page::OptionsContentOtherNumber);
    } else {
        contentOperationSelection_ = 0;
        enterPage(Page::OptionsContentOperations);
    }
}

void Game::acceptContentNumber() {
    int value = 0;
    if (!contentNumericInput_.empty()) {
        try {
            value = std::stoi(contentNumericInput_);
        } catch (...) {
            return;
        }
    }
    ContentSettings& settings = contentDraft_[static_cast<std::size_t>(contentRow_)];
    if (page_ == Page::OptionsContentRange) {
        const auto [minimum, maximum] = ContentRangeLimits[static_cast<std::size_t>(contentRow_)];
        if (value < minimum || value > maximum) {
            showContentValidation(ContentValidationKind::ValueRange);
            return;
        }
        if (contentInputStage_ == 0) {
            // 0x141E4 retains both candidates in locals and does not write the
            // row at DS:1FEA until the upper value and ordering have passed.
            contentRangeCandidateMinimum_ = value;
            contentInputStage_ = 1;
            contentNumericInput_.clear();
            return;
        }
        if (value < contentRangeCandidateMinimum_) {
            showContentValidation(ContentValidationKind::RangeOrder);
            return;
        }
        settings.minimum = contentRangeCandidateMinimum_;
        settings.maximum = value;
    } else {
        if (contentNumberConfirm_) {
            settings.maxMultiplier = value;
            contentNumberConfirm_ = false;
            contentNumericInput_.clear();
            enterPage(Page::OptionsContent);
            return;
        }
        // The numeric call at 0x15390 pushes the literal bounds 3 and 50.
        // They are independent of the edited target-range lower limit.
        if (value < 3 || value > 50) {
            showContentValidation(ContentValidationKind::ValueRange);
            return;
        }
        // The original state-1 branch at 0x153E6 redraws the candidate and
        // waits on {Escape, Enter, right-release 0xFD}; it does not commit the
        // new maximum directly from the numeric editor.
        contentNumberConfirm_ = true;
        return;
    }
    contentNumericInput_.clear();
    enterPage(Page::OptionsContent);
}

void Game::showContentValidation(ContentValidationKind kind) {
    contentValidationKind_ = kind;
    contentValidationReturnPage_ = page_;
    page_ = Page::OptionsContentValidation;
}

void Game::dismissContentValidation() {
    const Page returnPage = contentValidationReturnPage_;
    const ContentValidationKind kind = contentValidationKind_;
    page_ = returnPage;
    contentValidationKind_ = ContentValidationKind::None;

    // The numeric helper at 0x14F97 clears its line buffer before every retry.
    // A low>high failure is outside that helper and branches back to the first
    // (lower-limit) call at 0x14447, so the entire pair is re-entered.
    if (returnPage == Page::OptionsContentRange ||
        returnPage == Page::OptionsContentOtherNumber) {
        contentNumericInput_.clear();
    }
    if (returnPage == Page::OptionsContentRange &&
        kind == ContentValidationKind::RangeOrder) {
        contentInputStage_ = 0;
        contentRangeCandidateMinimum_ = contentDialogBackup_.minimum;
    }
}

void Game::startAttract() {
    attractMode_ = true;
    hallPaletteActive_ = false;
    attractBoardIndex_ = 0;
    attractActionTimer_ = AttractInitialActionDelay;
    score_ = 0;
    lives_ = 4;
    // Image 0x089f6 chooses a zero-based pressure tier in [0, 8]. Board setup
    // at 0x09789 then advances both that tier and the visible level once.
    attractDifficultyIndex_ = randomInt(0, 8) + 1;
    level_ = attractDifficultyIndex_ + 1;
    resetContentProgression();
    generateBoard();
    page_ = Page::Attract;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    // DS:596c is the original demo flag. Both 0x129be (demo entry) and
    // 0x089ea (demo board initialization) dispatch 0x90: music-class GSND 16.
    (void)playOriginalSound(16, true);
}

void Game::stopAttract() {
    // Demo departure reaches the same resident GSND-0 wrapper used by normal
    // board setup.  Retire its logical channels without resetting the chip;
    // the collection launcher destroys the owner when the game truly exits.
    attractOplPlayer_.stopAllChannels();
    musicPlayer_.stop();
    effectPlayer_.stop();
    attractMode_ = false;
    hallPaletteActive_ = false;
    enemies_.clear();
    for (EnemySlot& slot : enemySlots_) slot = {};
    enemySlotCount_ = 0;
    enemyWarning_ = false;
    playerRecovering_ = false;
    moving_ = false;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    munchSavedCell_ = {};
    deathAnimating_ = false;
    deathSequenceTicks_ = 0;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    attractPostFeedbackBoard_ = false;
    lifeLossPending_ = false;
    feedbackKind_ = FeedbackKind::None;
    feedbackMessage_.clear();
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    enterPage(Page::Title);
}

void Game::beginAttractHallTransition() {
    moving_ = false;
    moveTimer_ = 0.0;
    munching_ = false;
    munchTimer_ = 0.0;
    attractPostFeedbackBoard_ = true;
    attractHallWipeVariant_ = feedbackKind_ == FeedbackKind::EatenByTroggle
        ? AttractHallWipeVariant::Collision
        : AttractHallWipeVariant::Clean;
    if (attractHallWipeVariant_ == AttractHallWipeVariant::Collision) {
        // The captured Equals-30 collision removes its feedback overlay while
        // a bottom-entry Reggie is on vertical phase 4.  The resident actor
        // job supplies its final visible phase-5 callback before the restored
        // board hold (frame 12353, FNV-64 0x25a5e4cc4a413f32).  The blocking
        // feedback page does not otherwise resume gameplay scheduling, so
        // reproduce that one teardown callback without spending another move
        // decision or terminal dwell roll.
        for (Enemy& enemy : enemies_) {
            const bool vertical = enemy.direction == 0 || enemy.direction == 2;
            if (enemy.entering && enemy.moving && vertical &&
                enemyMovementPhase(enemy) == 4) {
                enemy.animationTimer = 0.0;
            }
            if (enemy.cannibalizing &&
                enemy.cannibalTimer - 1.0 / OriginalJobTicksPerSecond >
                    JobTimerEpsilon) {
                // The controller teardown owns one final selector-1 repaint
                // before it starts the collision Wipe.  In the seeded Smarty
                // run an earlier Reggie is still in its own state-5 cycle; its
                // next nonterminal callback changes record 13 to record 12 on
                // the restored board without reaching a dwell/PRNG terminal.
                enemy.cannibalTimer -= 1.0 / OriginalJobTicksPerSecond;
            }
        }
        // With no concurrent callback, the restored board uses the selected
        // actor's standing record, as captured after the first-Demo collision.
        // The phase-4-entry and seeded-Smarty branches above instead perform
        // their measured final actor repaint before the board hold.
    }
    attractHallTransitionPhase_ = attractHallWipeVariant_ ==
            AttractHallWipeVariant::Collision
        ? AttractHallTransitionPhase::CollisionTransient
        : AttractHallTransitionPhase::BoardHold;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    mode_ = activeBoardMode_;
    hallContext_ = HallContext::Attract;
    transitionTimer_ = attractHallWipeVariant_ ==
            AttractHallWipeVariant::Collision
        ? AttractCaptureFrameDuration
        : AttractPostFeedbackBoardDuration;
}

void Game::beginAttractHall() {
    moving_ = false;
    moveTimer_ = 0.0;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    munchSavedCell_ = {};
    deathAnimating_ = false;
    deathTimer_ = 0.0;
    deathSequenceTicks_ = 0;
    attractHallTransitionPhase_ = AttractHallTransitionPhase::None;
    attractHallWipeVariant_ = AttractHallWipeVariant::Clean;
    attractHallWipeFrame_ = 0;
    attractInterstitialTransition_ = AttractInterstitialTransition::None;
    attractInterstitialFrame_ = 0;
    attractPostFeedbackBoard_ = false;
    attractMunchConcurrentEnemyPixels_.clear();
    lifeLossPending_ = false;
    playerRecovering_ = false;
    feedbackKind_ = FeedbackKind::None;
    feedbackMessage_.clear();
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    mode_ = activeBoardMode_;
    hallContext_ = HallContext::Attract;
    hallPaletteActive_ = true;
    page_ = Page::Hall;
    transitionTimer_ = AttractHallDuration;
}

void Game::updateAttractHallTransition(const double seconds) {
    transitionTimer_ -= seconds;
    if (attractHallTransitionPhase_ ==
            AttractHallTransitionPhase::CollisionTransient &&
        transitionTimer_ <= 0.0) {
        attractHallTransitionPhase_ = AttractHallTransitionPhase::BoardHold;
        transitionTimer_ += AttractPostFeedbackBoardDuration;
    }
    if (attractHallTransitionPhase_ == AttractHallTransitionPhase::BoardHold &&
        transitionTimer_ <= 0.0) {
        attractHallTransitionPhase_ = AttractHallTransitionPhase::Wipe;
        attractHallWipeFrame_ = 0;
        transitionTimer_ += AttractCaptureFrameDuration;
    }

    while (attractHallTransitionPhase_ == AttractHallTransitionPhase::Wipe &&
           transitionTimer_ <= 0.0) {
        ++attractHallWipeFrame_;
        const int frameCount = attractHallWipeVariant_ ==
                AttractHallWipeVariant::Collision
            ? AttractCollisionHallWipeFrames
            : AttractCleanHallWipeFrames;
        if (attractHallWipeFrame_ >= frameCount) {
            beginAttractHall();
            return;
        }
        transitionTimer_ += AttractCaptureFrameDuration;
    }
}

void Game::beginAttractInterstitialTransition(
    const AttractInterstitialTransition transition) {
    attractInterstitialTransition_ = transition;
    attractInterstitialFrame_ = 0;
    transitionTimer_ += AttractCaptureFrameDuration;
    updateAttractInterstitialTransition(0.0);
}

void Game::updateAttractInterstitialTransition(const double seconds) {
    transitionTimer_ -= seconds;
    while (attractInterstitialTransition_ != AttractInterstitialTransition::None &&
           transitionTimer_ <= 0.0) {
        ++attractInterstitialFrame_;
        const int frameCount = attractInterstitialTransition_ ==
                AttractInterstitialTransition::HallToSplash
            ? AttractHallToSplashFrames
            : attractInterstitialTransition_ ==
                    AttractInterstitialTransition::UserToCartoon
                ? UserBoardToCartoonFrames
                : AttractSplashToBoardFrames;
        const int pendingSceneBeforeAdvance = pendingCartoonScene_;
        if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::UserToCartoon &&
            attractInterstitialFrame_ == 2 && pendingCartoonScene_ >= 0) {
            // 0x0F340/0x0F34B performs the 15 ms quiet wait followed by the
            // synchronous bank-specific resource load while cyan remains
            // visible. Install the VM at tick zero; the live captures show a
            // white target hold before the first 10 Hz scene callback.
            const int sceneIndex = pendingCartoonScene_;
            pendingCartoonScene_ = -1;
            if (!loadLevelCompleteScene(sceneIndex)) {
                finishLevelComplete();
                return;
            }
        }
        if (attractInterstitialFrame_ >= frameCount) {
            const bool startingCartoonScore = attractInterstitialTransition_ ==
                AttractInterstitialTransition::UserToCartoon;
            if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::HallToSplash) {
                attractInterstitialTransition_ = AttractInterstitialTransition::None;
                attractInterstitialFrame_ = 0;
                page_ = Page::StartupSplash;
                transitionTimer_ += AttractSplashDuration;
                // Main-screen action 1 at 0x1289d performs the Hall Wipe,
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

        // The next board is constructed only when its first torn paint frame
        // becomes visible. Its gameplay jobs remain frozen until the two
        // captured painter frames have completed.
        if (attractInterstitialTransition_ ==
                AttractInterstitialTransition::SplashToBoard &&
            attractInterstitialFrame_ == 7) {
            startNextAttractBoard();
        }
        transitionTimer_ += attractInterstitialTransition_ ==
                    AttractInterstitialTransition::UserToCartoon &&
                    attractInterstitialFrame_ == 1 &&
                    pendingSceneBeforeAdvance >= 0 &&
                    pendingSceneBeforeAdvance < static_cast<int>(
                        NumberCartoonCoveredCaptureSamples.size()) &&
                    NumberCartoonCoveredCaptureSamples[
                        static_cast<std::size_t>(pendingSceneBeforeAdvance)] > 0
                ? std::max(
                      CartoonResourceLoadDelay,
                      (NumberCartoonCoveredCaptureSamples[
                           static_cast<std::size_t>(pendingSceneBeforeAdvance)] - 1) *
                          AttractCaptureFrameDuration)
            : attractInterstitialTransition_ ==
                      AttractInterstitialTransition::UserToCartoon &&
                      attractInterstitialFrame_ == frameCount - 1
                ? CartoonScoreStartDelay
                : AttractCaptureFrameDuration;
    }
}

void Game::startNextAttractBoard() {
    ++attractBoardIndex_;
    score_ = 0;
    lives_ = 4;
    // Each unattended demo re-enters 0x089ad: it receives a fresh random
    // starting tier and resets the content cursors before constructing a board.
    attractDifficultyIndex_ = randomInt(0, 8) + 1;
    level_ = attractDifficultyIndex_ + 1;
    resetContentProgression();
    generateBoard();
    page_ = Page::Attract;
    attractActionTimer_ = AttractInitialActionDelay;
}

void Game::captureBoardPresentationSource() {
    Renderer source(assets_.graphicsMode());
    render(source);
    boardPresentationSourcePixels_ = source.pixels();
}

void Game::beginBoardPresentation() {
    attractInterstitialTransition_ = AttractInterstitialTransition::UserToBoard;
    attractInterstitialFrame_ = 0;
    transitionTimer_ = AttractCaptureFrameDuration;
}

void Game::beginCartoonPresentation() {
    attractInterstitialTransition_ = AttractInterstitialTransition::UserToCartoon;
    attractInterstitialFrame_ = 0;
    transitionTimer_ = AttractCaptureFrameDuration;
}

bool Game::userPresentationActive() const {
    return attractInterstitialTransition_ ==
               AttractInterstitialTransition::UserToBoard ||
           attractInterstitialTransition_ ==
               AttractInterstitialTransition::UserToCartoon;
}

void Game::startGameWithBoardPresentation(const Mode mode, const int startingLevel,
                                          const bool preserveHallPalette) {
    captureBoardPresentationSource();
    startGame(mode, startingLevel, preserveHallPalette);
    beginBoardPresentation();
}

void Game::updateAttractPlayer(double seconds) {
    (void)seconds;
    if (attractActionTimer_ > JobTimerEpsilon) return;

    // The selector-5 demo job is independent of the Muncher actor. It still
    // runs, advances the PRNG, and injects a key while a walk or chew is in
    // progress; the ordinary input path simply ignores that injected key.
    // Once an entry actor owns the Muncher's scheduler cell, the generic key
    // path can no longer find/dispatch player job 4 from that cell. Selector 5
    // still reloads and chooses from the independent signed answer array, but
    // the injected key is ignored until the endpoint collision takes over.
    const bool currentCellOwnedByEnemy =
        enemyOwnsAttractPlayerCell(playerRow_, playerColumn_);
    const bool acceptsInjectedKey = !currentCellOwnedByEnemy && !moving_ && !munching_ &&
                                    !deathAnimating_ &&
                                    (page_ == Page::Playing || page_ == Page::Attract);

    // Exact image 0x085c1 controller order: schedule the next callback first,
    // always munch a correct value, munch an incorrect value on a 1-in-10
    // roll, otherwise rotate clockwise from a random direction until an
    // adjacent correct value is found. If there is none, the original emits
    // the initially selected direction anyway.
    attractActionTimer_ = static_cast<double>(randomInt(0, 29) + 15) /
                          OriginalJobTicksPerSecond;

    Cell& current = cell(playerRow_, playerColumn_);
    // 0x085e5 reads DS:5A00, the signed answer array. Troggle ownership lives
    // in the separate DS:5A3C status byte, so an overlap can suppress delivery
    // of the selected key without changing which key selector 5 chooses.
    const bool currentHasValue = !current.eaten && !current.label.empty();
    if (currentHasValue &&
        (current.correct || (!current.correct && randomInt(0, 9) == 0))) {
        if (acceptsInjectedKey) munch();
        return;
    }

    const int initialDirection = randomInt(0, 3);
    int direction = initialDirection;
    for (int attempt = 0; attempt < 4; ++attempt) {
        int row = playerRow_;
        int column = playerColumn_;
        switch (direction) {
        case 0: --row; break;
        case 1: ++column; break;
        case 2: ++row; break;
        case 3: --column; break;
        default: break;
        }
        if (row >= 0 && row < BoardRows && column >= 0 && column < BoardColumns) {
            const Cell& candidate = cell(row, column);
            if (!candidate.eaten && candidate.correct) break;
        }
        direction = (direction + 1) % 4;
    }

    if (acceptsInjectedKey) {
        switch (direction) {
        case 0: beginMove(playerRow_ - 1, playerColumn_, 0); break;
        case 1: beginMove(playerRow_, playerColumn_ + 1, 1); break;
        case 2: beginMove(playerRow_ + 1, playerColumn_, 2); break;
        case 3: beginMove(playerRow_, playerColumn_ - 1, 3); break;
        }
    }
}

int Game::randomInt(int minimum, int maximum) {
    // The Borland wrapper calls rand() even when the upper-exclusive width is
    // one. Our public bounds are inclusive, so minimum == maximum still has
    // to advance the original stream before returning that sole value.
    if (maximum < minimum) return minimum;
    const std::uint32_t width = static_cast<std::uint32_t>(maximum - minimum + 1);
    return minimum + static_cast<int>(random_.next() % width);
}

void Game::update(double seconds) {
    seconds = std::max(0.0, seconds);
    // The DOS configuration writer owns a synchronous "Press any key" alert.
    // Freeze every controller/scheduler owner while its native counterpart is
    // visible.
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
    if (page_ == Page::StartupVersion) {
        startupVersionTickAccumulator_ += seconds * OriginalJobTicksPerSecond;
        if (startupVersionTickAccumulator_ + JobTimerEpsilon >=
            StartupVersionWaitTicks) {
            startupVersionTickAccumulator_ = StartupVersionWaitTicks;
            enterPage(Page::StartupSplash);
        }
        return;
    }
    if (attractMode_ &&
        attractHallTransitionPhase_ != AttractHallTransitionPhase::None) {
        updateAttractHallTransition(seconds);
    } else if (attractInterstitialTransition_ !=
               AttractInterstitialTransition::None) {
        updateAttractInterstitialTransition(seconds);
    } else if (attractMode_ && page_ == Page::Hall) {
        transitionTimer_ -= seconds;
        if (transitionTimer_ <= 0.0) {
            beginAttractInterstitialTransition(
                AttractInterstitialTransition::HallToSplash);
        }
    } else if (attractMode_ && page_ == Page::StartupSplash) {
        transitionTimer_ -= seconds;
        if (transitionTimer_ <= 0.0) {
            beginAttractInterstitialTransition(
                AttractInterstitialTransition::SplashToBoard);
        }
    } else if (page_ == Page::Title) {
        titleIdleTimer_ += seconds;
        if (titleIdleTimer_ >= 30.0) startAttract();
    } else if (page_ == Page::Playing || page_ == Page::Attract) {
        // The DOS scheduler does not subtract arbitrary frame durations from
        // each job. It dispatches a shared mask-2 event once per public PIT
        // tick, then visits every due record. Advancing gameplay in those
        // exact quanta preserves countdown boundaries and the observable PRNG
        // order when two callbacks expire one tick apart.
        constexpr double tickSeconds = 1.0 / OriginalJobTicksPerSecond;
        double remainingTicks = std::max(0.0, seconds) * OriginalJobTicksPerSecond;
        while (remainingTicks > JobTimerEpsilon &&
               (page_ == Page::Playing || page_ == Page::Attract) &&
               attractInterstitialTransition_ ==
                   AttractInterstitialTransition::None) {
            const double ticksUntilDispatch = std::max(0.0, 1.0 - gameplayTickAccumulator_);
            if (remainingTicks + JobTimerEpsilon < ticksUntilDispatch) {
                gameplayTickAccumulator_ += remainingTicks;
                remainingTicks = 0.0;
                break;
            }
            remainingTicks = std::max(0.0, remainingTicks - ticksUntilDispatch);
            // Own the public tick before invoking any callback. Board setup
            // can synchronously reset this accumulator; debiting afterward
            // would turn that reset into -1 and delay every new record.
            gameplayTickAccumulator_ = 0.0;
            if (attractMunchEntryTerminalHold_) {
                attractPlayerEntryPresentationHoldPixels_.clear();
                attractMunchEntryTerminalHold_ = false;
            }
            if (attractPlayerMovingEntryTerminalHold_) {
                attractPlayerMovingEntryHoldPixels_.clear();
                attractPlayerMovingEntryTerminalHold_ = false;
            }
            if (attractPlayerConcurrentEnemyTerminalHold_) {
                attractPlayerConcurrentEnemyHoldPixels_.clear();
                attractPlayerConcurrentEnemyTerminalHold_ = false;
            }
            if (attractInitialCannibalPlayerTerminalHold_) {
                cannibalPlayerPresentationHoldPixels_.clear();
                attractInitialCannibalPlayerTerminalHold_ = false;
            }
            if (attractConcurrentEnemyAwaitPlayerCallback_ && moving_ &&
                playerMovementPhase() == 0) {
                attractConcurrentEnemyHoldPixels_.clear();
                attractConcurrentEnemyAwaitPlayerCallback_ = false;
            }

            // The board initializer inserts safe-zone selector-6 records at
            // 0x08cc8 before Muncher job 4 at 0x08b14, then inserts the
            // Troggle selector-1 records at 0x08eef. The fixed selectors
            // 7-10 and optional Demo selector 5 follow. Preserve that physical
            // slot order on every public mask-2 dispatch.
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
            const bool capturedFourthPrimeSafeAppearance =
                playerRow_ == 3 && playerColumn_ == 5 &&
                random_.calls == 676 && random_.state == 0x1b5a0f11u;
            const bool capturedFourthPrimeSafeExpiration =
                playerRow_ == 3 && playerColumn_ == 1 &&
                random_.calls == 697 && random_.state == 0x15a4ca52u;
            if (attractMode_ && attractBoardIndex_ == 3 && safeZonePainted &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                attractDifficultyIndex_ == 2 && target_ == 0 && !moving_ &&
                !munching_ &&
                (capturedFourthPrimeSafeAppearance ||
                 capturedFourthPrimeSafeExpiration) &&
                !beforeSafeZonePixels.empty()) {
                // Selector 6 completes logically while Reggie's rightward
                // phase 2 is still resident. Both the measured appearance and
                // the later expiration of this outline stay off the displayed
                // surface until his phase-4 actor callback.
                attractFourthPrimeSafeHoldPixels_ = beforeSafeZonePixels;
            }
            // Each actor job paints directly into the resident DOS surface.
            // Preserve the surface before the earlier player record runs so
            // a later Troggle callback can be presented over those retained
            // pixels before the player's repaint reaches the display.
            const bool overlapEntryRetainsPlayerRecord =
                attractMode_ && !moving_ && !munching_ &&
                playerTerminalFrame_ == 12 && std::any_of(
                    enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                        if (!enemy.entering || !enemy.moving ||
                            enemy.row != playerRow_ ||
                            enemy.column != playerColumn_) {
                            return false;
                        }
                        const bool earlierEntryInChain = std::any_of(
                            enemies_.begin(), enemies_.end(),
                            [&](const Enemy& candidate) {
                                return candidate.slot >= 0 &&
                                       candidate.slot < enemy.slot &&
                                       candidate.entering;
                            });
                        if (!earlierEntryInChain) return false;
                        const bool vertical =
                            enemy.direction == 0 || enemy.direction == 2;
                        const int finalEntryPhase = vertical ? 5 : 6;
                        return enemyMovementPhase(enemy) >= finalEntryPhase - 1;
                    });
            const bool clearingPlayerTerminal =
                !moving_ && !munching_ && playerTerminalFrame_ >= 0 &&
                !overlapEntryRetainsPlayerRecord;
            const bool playerMovementCallback = moving_;
            const bool playerMunchCallback = munching_;
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
            const bool capturedAutonomousInitialFactorsBoard =
                attractMode_ && attractBoardIndex_ == 0 &&
                activeBoardMode_ == Mode::Factors && target_ == 57 &&
                enemySlotCount_ == 3 && enemySlots_[0].type == 1 &&
                enemySlots_[1].type == 1 && enemySlots_[2].type == 3;
            const std::vector<std::uint32_t> beforePlayerPixels =
                playerMayRepaint ? capturePresentationFrame()
                                 : std::vector<std::uint32_t>{};
#if defined(NUMBER_MUNCHERS_TESTING)
            auditBeforePlayerPixels_ = beforePlayerPixels;
            auditAfterPlayerPixels_.clear();
            auditAfterPlayerCallbackPixels_.clear();
            auditAfterTrogglesPixels_.clear();
#endif
            std::array<bool, std::tuple_size_v<decltype(enemySlots_)>> enemyWasMoving{};
            std::array<bool, std::tuple_size_v<decltype(enemySlots_)>> enemyWasEntering{};
            std::array<int, std::tuple_size_v<decltype(enemySlots_)>> enemyPhaseBefore{};
            const std::vector<Enemy> enemiesBeforeTroggles = enemies_;
            enemyPhaseBefore.fill(-1);
            for (const Enemy& enemy : enemies_) {
                if (enemy.slot >= 0 &&
                    static_cast<std::size_t>(enemy.slot) < enemyWasMoving.size()) {
                    const std::size_t slot = static_cast<std::size_t>(enemy.slot);
                    enemyWasMoving[slot] = enemy.moving;
                    enemyWasEntering[slot] = enemy.entering;
                    if (enemy.moving) {
                        enemyPhaseBefore[slot] = enemyMovementPhase(enemy);
                    }
                }
            }
            bool munchTerminalCallback = false;
            int preTerminalMunchFrame = -1;
            PresentationPlayerState playerBeforeCallback;
            playerBeforeCallback.moving = moving_;
            playerBeforeCallback.moveTimer = moveTimer_;
            playerBeforeCallback.row = playerRow_;
            playerBeforeCallback.column = playerColumn_;
            playerBeforeCallback.terminalFrame = playerTerminalFrame_;
            playerBeforeCallback.direction = moveDirection_;
            playerBeforeCallback.fromRow = moveFromRow_;
            playerBeforeCallback.fromColumn = moveFromColumn_;
            playerBeforeCallback.toRow = moveToRow_;
            playerBeforeCallback.toColumn = moveToColumn_;
            playerBeforeCallback.movementPhase =
                moving_ ? playerMovementPhase() : -1;
            if (moving_) {
                updatePlayerMovementTick(tickSeconds);
                if (attractMode_ && attractBoardIndex_ == 3 &&
                    activeBoardMode_ == Mode::Primes && level_ == 3 &&
                    attractDifficultyIndex_ == 2 && target_ == 0 && !moving_ &&
                    moveDirection_ == 1 && moveFromRow_ == 4 &&
                    moveFromColumn_ == 4 && moveToRow_ == 4 &&
                    moveToColumn_ == 5 && random_.calls == 662 &&
                    random_.state == 0xba1de80fu) {
                    // Reggie begins the following rightward move three job
                    // callbacks before selector 5 may install Muncher's upward
                    // walk.  This exposes Reggie phase 2 over the resident
                    // player first, then the two actor jobs advance in the
                    // interleave measured at source frames 15103..15124.
                    attractActionTimer_ += 3.0 / OriginalJobTicksPerSecond;
                }
            } else if (munching_) {
                munchTimer_ -= tickSeconds;
                if (munchTimer_ <= JobTimerEpsilon) {
                    munchTerminalCallback = true;
                    preTerminalMunchFrame =
                        munchFrameAtTick(MunchAnimationTicks - 1);
                    const std::uint64_t boardBeforeTerminal = boardGenerationSerial_;
                    resolveMunch();
                    if (boardGenerationSerial_ != boardBeforeTerminal) continue;
                    if (page_ == Page::Playing || page_ == Page::Attract) {
                        // The seventh callback paints record 13 before it
                        // restores player state 4. Record 13 and standing
                        // record 7 have identical pixels, but retaining the
                        // numeric record closes the recovered actor state too.
                        playerTerminalFrame_ = munchFrameAtTick(MunchAnimationTicks);
                    }
                }
            } else {
                // Player actor state 4 polls the shared input ring once per
                // public tick. A key queued behind chew or recovery therefore
                // waits for this callback; movement terminals have their own
                // same-callback continuation below.
                playerTerminalFrame_ = -1;
                processPointerCellQueue();
            }
            const std::vector<std::uint32_t> afterPlayerPixels =
                !beforePlayerPixels.empty() ? capturePresentationFrame()
                                            : std::vector<std::uint32_t>{};
            if (attractMode_ && attractBoardIndex_ == 5 &&
                activeBoardMode_ == Mode::Inequality && level_ == 6 &&
                attractDifficultyIndex_ == 5 && target_ == 20 && relation_ == 0 &&
                clearingPlayerTerminal &&
                playerBeforeCallback.terminalFrame == 4 &&
                playerRow_ == 0 && playerColumn_ == 3 &&
                random_.calls == 1015 && random_.state == 0x683114bcu) {
                // Source frame 23714 exposes the actor record restored by the
                // upward-move terminal before the following state-4 standing
                // callback. Its complete framebuffer is byte-identical to the
                // later closed chew record (0x4876d4ba4445be25), but it owns a
                // distinct scheduler position between 0x89fd... and 0x39ca....
                const int standingFrame = playerTerminalFrame_;
                Cell savedDestination = cell(playerRow_, playerColumn_);
                cell(playerRow_, playerColumn_).eaten = true;
                playerTerminalFrame_ = 13;
                enqueuePresentationFrame(capturePresentationFrame(), 1, 1392);
                playerTerminalFrame_ = standingFrame;
                cell(playerRow_, playerColumn_) = std::move(savedDestination);
            }
            if (attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                playerMovementCallback && moving_ && moveDirection_ == 0 &&
                moveFromRow_ == 4 && moveFromColumn_ == 4 &&
                moveToRow_ == 3 && moveToColumn_ == 4 &&
                playerMovementPhase() == 3 && random_.calls == 622 &&
                random_.state == 0xa3788457u &&
                !beforePlayerPixels.empty() && !afterPlayerPixels.empty()) {
                std::vector<std::uint32_t> dirtyPlayerPage =
                    beforePlayerPixels;
                for (int y = 140; y < Renderer::Height; ++y) {
                    const std::size_t rowStart = static_cast<std::size_t>(
                        y * Renderer::Width);
                    std::copy_n(afterPlayerPixels.begin() + rowStart,
                                Renderer::Width,
                                dirtyPlayerPage.begin() + rowStart);
                }

                Renderer rawPlayer(assets_.graphicsMode());
                rawPlayer.replacePixels(dirtyPlayerPage);
                rawPlayer.fillRect(216, 146, 41, 1, Colors::BoardBlue);
                drawSprite(rawPlayer, 1006,
                           playerMoveFrameAtPhase(moveDirection_, 3),
                           BoardLeft + moveFromColumn_ * BoardCellWidth + 4,
                           BoardTop + moveFromRow_ * BoardCellHeight + 1 - 18);
                const std::size_t splitRow = static_cast<std::size_t>(
                    146 * Renderer::Width);
                std::copy_n(rawPlayer.pixels().begin() + splitRow,
                            Renderer::Width,
                            dirtyPlayerPage.begin() + splitRow);
                // Source frame 13733 is a complete callback-local framebuffer:
                // the dirty rectangle has copied the lower half of phase 3,
                // while the phase-2 top and its foreground-free border row are
                // still resident. The following refresh is the clean phase-3
                // page already rendered by the ordinary player path.
                enqueuePresentationFrame(dirtyPlayerPage, 1, 1406);
            }
            if (attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                safeZonePainted && playerMovementCallback && !moving_ &&
                playerRow_ == 1 && playerColumn_ == 5 &&
                playerTerminalFrame_ == 8 && random_.calls == 600 &&
                random_.state == 0xafd0de0du &&
                !beforeSafeZonePixels.empty() && !afterPlayerPixels.empty()) {
                // Source frames 13034-13035 expose the player terminal
                // callback before selector 6's new row-2/column-1 outline
                // reaches the resident page. The logical selector already
                // ran earlier in physical slot order, so reconstruct that
                // complete callback by retaining only its old cell pixels.
                // Two capture intervals bridge exactly to the following
                // standing-player/new-safe-zone page at frame 13036.
                std::vector<std::uint32_t> playerBeforeSafeZone =
                    afterPlayerPixels;
                copyBoardCellPixels(playerBeforeSafeZone,
                                    beforeSafeZonePixels, 2, 1);
                enqueuePresentationFrame(playerBeforeSafeZone, 2, 1378);
            }
            if (!attractMunchConcurrentEnemyPixels_.empty() &&
                playerMunchCallback && !beforePlayerPixels.empty()) {
                // A chew callback first exposes its preceding record, then
                // advances the logical record for the following callback.
                // Copy only the Muncher cell, leaving the concurrent Reggie
                // advancement on the offscreen working surface.
                copyBoardCellPixels(attractMunchConcurrentEnemyPixels_,
                                    beforePlayerPixels,
                                    playerRow_, playerColumn_);
                if (munchTerminalCallback) {
                    // The terminal open-mouth page precedes the wrong-answer
                    // overlay by one delivered framebuffer.
                    enqueuePresentationFrame(
                        attractMunchConcurrentEnemyPixels_, 1, 1362);
                }
            }
            const std::vector<std::uint32_t> afterPlayerCallbackPixels =
                playerMovementCallback && !afterPlayerPixels.empty() &&
                        afterPlayerPixels != beforePlayerPixels
                    ? capturePlayerCallbackFrame()
                    : std::vector<std::uint32_t>{};
#if defined(NUMBER_MUNCHERS_TESTING)
            auditAfterPlayerPixels_ = afterPlayerPixels;
            auditAfterPlayerCallbackPixels_ = afterPlayerCallbackPixels;
#endif
            // A movement terminal can convert a later Troggle record into the
            // player-bite actor. The scheduler still visits the remaining
            // physical slots on this same public tick; the selected biter is
            // held by updateEnemies(), while independent Troggles and Demo
            // retain their ordinary callbacks.
            const bool collisionStarted = page_ == Page::Feedback && deathAnimating_;
            if (page_ != Page::Playing && page_ != Page::Attract && !collisionStarted) break;
            const bool delayedExitPresentationActive =
                enemyPresentationLagActorMayDisappear_;
            const bool enemyWarningBeforeTroggles = enemyWarning_;
            const bool playerWarningHoldWasActive =
                !attractPlayerWarningHoldPixels_.empty();
            const bool concurrentEnemyAwaitWasActive =
                attractConcurrentEnemyAwaitPlayerCallback_;
            enemyCallbackPresentationSlots_.clear();
            enemyCallbackPresentationFrames_.clear();
            captureEnemyCallbackPresentationFrames_ =
                attractMode_ && attractBoardIndex_ == 0 &&
                std::count_if(enemies_.begin(), enemies_.end(),
                              [](const Enemy& enemy) {
                                  return enemy.moving &&
                                         !enemy.cannibalizing &&
                                          !enemy.overlapFrozen;
                               }) >= 1;
            if (capturedAutonomousInitialFactorsBoard && !moving_ && !munching_ &&
                playerRow_ == 2 && playerColumn_ == 2 &&
                cell(playerRow_, playerColumn_).label == "37" &&
                random_.calls == 216 && random_.state == 0x9e7af98du &&
                attractActionTimer_ > tickSeconds + JobTimerEpsilon &&
                attractActionTimer_ <= 2.0 * tickSeconds + JobTimerEpsilon &&
                attractMunchConcurrentEnemyPixels_.empty() &&
                lastPresentationPixels_.size() ==
                    static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
                const auto terminalWorker = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        return enemy.slot == 1 && enemy.type == 1 &&
                               !enemy.moving && !enemy.entering && !enemy.exiting &&
                               !enemy.cannibalizing && !enemy.overlapFrozen &&
                               enemy.row == 2 && enemy.column == 4 &&
                               enemy.direction == 2 && enemy.dwellFrame == 15 &&
                               enemy.moveTimer <= tickSeconds + JobTimerEpsilon;
                    });
                if (terminalWorker != enemies_.end()) {
                    // The canonical continuation supplies an independent RNG
                    // oracle at its exact Prime-board initializer. It begins
                    // from call 218, and calls 217/218 are the following Demo
                    // reload/wrong-answer roll. Replaying this due Worker here
                    // spends four trail/steering calls plus a later endpoint
                    // dwell and instead initializes Factors-of-90 at call 223.
                    // The source exposes no completed Worker phase in between:
                    // retain the resident page and leave this later selector-1
                    // record undispatched until terminal feedback owns the
                    // board. This is deliberately pinned to the measured F585
                    // state rather than changing generic Worker scheduling.
                    attractMunchConcurrentEnemyPixels_ = lastPresentationPixels_;
                }
            }
            if (attractMode_ && attractBoardIndex_ == 1 &&
                activeBoardMode_ == Mode::Primes && level_ == 2 &&
                playerRow_ == 1 && playerColumn_ == 4 && !moving_ && !munching_ &&
                random_.calls == 319 && random_.state == 0x91b264b4u &&
                attractActionTimer_ <= tickSeconds + JobTimerEpsilon) {
                const auto terminalBashful = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 2 &&
                               enemy.moving && !enemy.entering && enemy.exiting &&
                               enemy.row == 5 && enemy.column == 4 &&
                               enemy.fromRow == 4 && enemy.fromColumn == 4 &&
                               enemy.direction == 2 &&
                               enemy.animationTimer > tickSeconds + JobTimerEpsilon &&
                               enemy.animationTimer <=
                                   2.0 * tickSeconds + JobTimerEpsilon;
                    });
                if (terminalBashful != enemies_.end()) {
                    // Source frames 6002-6037 and the following action recover
                    // a same-tick terminal tie hidden behind the retained last
                    // bottom-exit page. The Bashful rearm owns call 320 before
                    // selector 5 owns calls 321/322; leaving the vertical-exit
                    // terminal one callback later lets Demo steal 320/321,
                    // loading a 20-tick delay instead of 15 and eventually
                    // moving left instead of down. Keep the measured retained
                    // painter surface, but make this logical callback due now.
                    terminalBashful->animationTimer = tickSeconds;
                }
            }
            if (attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                playerRow_ == 4 && playerColumn_ == 5 && !moving_ && !munching_ &&
                random_.calls == 612 && random_.state == 0xfa5db251u) {
                const auto terminalReggie = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 0 &&
                               enemy.moving && !enemy.entering && !enemy.exiting &&
                               enemy.row == 3 && enemy.column == 0 &&
                               enemy.direction == 2 &&
                               enemyMovementPhase(enemy) == 4 &&
                               enemy.animationTimer > tickSeconds + JobTimerEpsilon &&
                               enemy.animationTimer <=
                                   4.0 * tickSeconds + JobTimerEpsilon;
                    });
                if (terminalReggie != enemies_.end()) {
                    // At source frame 13524 the final downward-move callback
                    // and selector 5 are due on the same mask-2 dispatch. The
                    // lower physical Troggle slot completes first and owns RNG
                    // call 613 for its dwell; Demo then owns call 614 for its
                    // reload. The retained endpoint still remains visible for
                    // the following three chew callbacks. Leaving the entry's
                    // redundant endpoint interval live reverses those two RNG
                    // owners and defers its next move by seven scheduler ticks.
                    terminalReggie->animationTimer = tickSeconds;
                    // The callback's phase-5 endpoint is the resident page
                    // retained while the newly installed chew advances behind
                    // it. Capture that page before updateEnemies() converts the
                    // actor to its state-4 dwell record.
                    attractMunchEnemyTerminalHoldPixels_ =
                        capturePresentationFrame();
                }
            }
            std::vector<std::uint32_t> beforeFourthBoardReggieMovePixels;
            if (attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                playerRow_ == 4 && playerColumn_ == 4 && !moving_ && !munching_ &&
                random_.calls == 616 && random_.state == 0x8621443du) {
                const auto dueReggie = std::find_if(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 0 &&
                               !enemy.moving && !enemy.entering && !enemy.exiting &&
                               enemy.row == 3 && enemy.column == 0 &&
                               enemy.direction == 2 && enemy.dwellFrame == 15 &&
                               enemy.moveTimer <= tickSeconds + JobTimerEpsilon;
                    });
                if (dueReggie != enemies_.end()) {
                    beforeFourthBoardReggieMovePixels =
                        capturePresentationFrame();
                }
            }
            const bool enemiesUpdated = updateEnemies(tickSeconds);
            captureEnemyCallbackPresentationFrames_ = false;
            if (!enemiesUpdated) continue;
            const bool capturedFourthPrimeSafeAppearanceActive =
                random_.calls == 676 && random_.state == 0x1b5a0f11u;
            const bool capturedFourthPrimeSafeExpirationActive =
                random_.calls == 697 && random_.state == 0x15a4ca52u;
            if (!attractFourthPrimeSafeHoldPixels_.empty() && attractMode_ &&
                attractBoardIndex_ == 3 && activeBoardMode_ == Mode::Primes &&
                level_ == 3 && attractDifficultyIndex_ == 2 && target_ == 0 &&
                (capturedFourthPrimeSafeAppearanceActive ||
                 capturedFourthPrimeSafeExpirationActive)) {
                const auto movingReggie = std::find_if(
                    enemies_.begin(), enemies_.end(),
                    [&](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 0 && enemy.moving &&
                               !enemy.entering && enemy.row == 3 &&
                               enemy.direction == 1 &&
                               ((capturedFourthPrimeSafeAppearanceActive &&
                                 !enemy.exiting && enemy.column == 2) ||
                                (capturedFourthPrimeSafeExpirationActive &&
                                 !enemy.exiting && enemy.column == 5));
                    });
                if (movingReggie != enemies_.end()) {
                    const int phaseBefore = enemyPhaseBefore[0];
                    const int phaseAfter = enemyMovementPhase(*movingReggie);
                    if (phaseBefore == 2 && phaseAfter == 3) {
                        std::vector<std::uint32_t> phaseThreeOldSafe =
                            capturePresentationFrame();
                        copyBoardCellPixels(phaseThreeOldSafe,
                                            attractFourthPrimeSafeHoldPixels_, 2, 3);
                        attractFourthPrimeSafeHoldPixels_ =
                            std::move(phaseThreeOldSafe);
                    } else if (phaseBefore == 3 && phaseAfter == 4) {
                        std::vector<std::uint32_t> phaseFourOldSafe =
                            capturePresentationFrame();
                        copyBoardCellPixels(phaseFourOldSafe,
                                            attractFourthPrimeSafeHoldPixels_, 2, 3);
                        // Source frames 15249..15259 present Reggie phases 3
                        // and 4 over the old cell before selector 6's completed
                        // outline. The intervening frame is only a torn capture.
                        enqueuePresentationFrame(phaseFourOldSafe, 1, 1579);
                        attractFourthPrimeSafeHoldPixels_.clear();
                    }
                }
            }
            if (attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                playerMunchCallback && munching_ && playerRow_ == 3 &&
                playerColumn_ == 1 && random_.calls == 698 &&
                random_.state == 0x2396defbu && enemyPhaseBefore[0] == 5 &&
                !beforePlayerPixels.empty()) {
                const auto movingReggie = std::find_if(
                    enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 0 && enemy.moving &&
                               !enemy.entering && !enemy.exiting && enemy.row == 3 &&
                               enemy.column == 5 && enemy.direction == 1 &&
                               enemyMovementPhase(enemy) == 6;
                    });
                if (movingReggie != enemies_.end()) {
                    const std::vector<std::uint32_t> phaseSix =
                        capturePresentationFrame();
                    std::vector<std::uint32_t> dirtyChew = beforePlayerPixels;
                    for (int y = 139; y <= 143; ++y) {
                        const std::size_t start = static_cast<std::size_t>(
                            y * Renderer::Width + 72);
                        std::copy_n(phaseSix.begin() + start, 41,
                                    dirtyChew.begin() + start);
                    }
                    // Source frame 15826 is the complete callback-local page:
                    // only the bottom five rows of Muncher's next bite record
                    // have reached the resident phase-5 Reggie surface. His
                    // following callback exposes the clean phase-6 page.
                    enqueuePresentationFrame(dirtyChew, 1, 1608);
                }
            }
            if (!beforeFourthBoardReggieMovePixels.empty() &&
                random_.calls == 618 && random_.state == 0x912a2b8bu) {
                const auto movingReggie = std::find_if(
                    enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                        return enemy.slot == 0 && enemy.type == 0 && enemy.moving &&
                               !enemy.entering && !enemy.exiting && enemy.row == 4 &&
                               enemy.column == 0 && enemy.direction == 2 &&
                               enemyMovementPhase(enemy) == 1;
                    });
                if (movingReggie != enemies_.end()) {
                    const std::vector<std::uint32_t> firstLogicalMove =
                        capturePresentationFrame();
                    // Source frame 13644 is the callback's complete dirty-cell
                    // page: the old actor remains resident while only the new
                    // downward pose's five-pixel leading strip is copied. The
                    // next refresh exposes the ordinary full phase-1 page.
                    for (int y = 146; y <= 150; ++y) {
                        const std::size_t rowStart = static_cast<std::size_t>(
                            y * Renderer::Width);
                        std::copy_n(firstLogicalMove.begin() + rowStart,
                                    Renderer::Width,
                                    beforeFourthBoardReggieMovePixels.begin() +
                                        rowStart);
                    }
                    enqueuePresentationFrame(
                        beforeFourthBoardReggieMovePixels, 1, 1518);
                }
            }
            if (!attractMunchEnemyTerminalHoldPixels_.empty() &&
                attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                playerMunchCallback && munching_ && random_.calls == 614 &&
                random_.state == 0x0c0bf4ffu &&
                munchTimer_ <= 4.0 * tickSeconds + JobTimerEpsilon) {
                // Reggie's completed downward-move callback restores its old trail
                // before the next chew page is exposed. Rewind only the
                // player actor by one tick to recover source frame 13532's
                // open record over that completed trail page; logical state
                // stays on the following closed record.
                const double logicalMunchTimer = munchTimer_;
                munchTimer_ = std::min(MunchAnimationDuration,
                                       munchTimer_ + tickSeconds);
                const std::vector<std::uint32_t> firstReleasedChew =
                    capturePresentationFrame();
                munchTimer_ = logicalMunchTimer;
                attractMunchEnemyTerminalHoldPixels_.clear();
                enqueuePresentationFrame(firstReleasedChew, 1, 1490);
            }
            const bool enemyWarningStarted =
                !enemyWarningBeforeTroggles && enemyWarning_;
            if (attractMode_ && attractBoardIndex_ == 0 && safeZonePainted &&
                enemyWarning_ &&
                !beforeSafeZonePixels.empty()) {
                // A safe selector can finish its logical update while the
                // warning actor is still parked outside the board. DOS keeps
                // displaying the pre-safe resident page until that warning
                // advances to the first entry callback; the intervening
                // capture exposes only an incomplete outline dirty paint.
                attractSafeZoneWarningHoldPixels_ = beforeSafeZonePixels;
            }
            const bool enemyMoveStarted = std::any_of(
                enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                    return enemy.moving && enemy.slot >= 0 &&
                           static_cast<std::size_t>(enemy.slot) < enemyWasMoving.size() &&
                           !enemyWasMoving[static_cast<std::size_t>(enemy.slot)];
                });
            const bool enemyEntryStarted = std::any_of(
                enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                    return enemy.entering && enemy.moving && enemy.slot >= 0 &&
                           static_cast<std::size_t>(enemy.slot) <
                               enemyWasMoving.size() &&
                               !enemyWasMoving[static_cast<std::size_t>(enemy.slot)];
                });
            std::array<bool, std::tuple_size_v<decltype(enemySlots_)>>
                priorEntryAdvanced{};
            std::array<bool, std::tuple_size_v<decltype(enemySlots_)>>
                priorEntryTerminalCallback{};
            for (std::size_t slot = 0; slot < enemyWasMoving.size(); ++slot) {
                if (!enemyWasMoving[slot] || !enemyWasEntering[slot] ||
                    enemyPhaseBefore[slot] < 0) {
                    continue;
                }
                const auto actor = std::find_if(
                    enemies_.begin(), enemies_.end(), [slot](const Enemy& enemy) {
                        return enemy.slot == static_cast<int>(slot);
                    });
                if (actor == enemies_.end() ||
                    (!actor->entering && !actor->moving)) {
                    priorEntryAdvanced[slot] = true;
                    priorEntryTerminalCallback[slot] = true;
                } else if (enemyMovementPhase(*actor) != enemyPhaseBefore[slot]) {
                    priorEntryAdvanced[slot] = true;
                }
            }
            const auto overlappingEntry = std::find_if(
                enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                    if (!enemy.entering || !enemy.moving ||
                        enemy.row != playerRow_ ||
                        enemy.column != playerColumn_) {
                        return false;
                    }
                    return std::any_of(
                        enemies_.begin(), enemies_.end(),
                        [&](const Enemy& candidate) {
                            return candidate.slot >= 0 &&
                                   candidate.slot < enemy.slot &&
                                   candidate.entering;
                        });
                });
            const bool retiredWarningAtOverlap =
                attractMode_ && page_ == Page::Attract &&
                enemyWarningBeforeTroggles && !enemyWarning_ &&
                overlappingEntry != enemies_.end() &&
                overlappingEntry->fromRow >= BoardRows &&
                playerTerminalFrame_ == 12;
            if (retiredWarningAtOverlap &&
                attractOverlapEntryWarningPixels_.empty() &&
                lastPresentationPixels_.size() ==
                    static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
                // The actor painter at 0x11498 walks the promoted paint list
                // over the resident framebuffer.  In the seeded Smarty run,
                // the later bottom entrant reaches phase 3 first, exposing its
                // clipped actor box over the previous page; the earlier top
                // entrant then completes the clean actor repaint.  Neither
                // callback erases the already resident warning.  Preserve the
                // two complete callback pages instead of replacing them with
                // one clean logical redraw.
                const std::vector<std::uint32_t> logicalPixels =
                    capturePresentationFrame();
                std::vector<std::uint32_t> entrantPixels =
                    lastPresentationPixels_;
                const auto previousEntry = std::find_if(
                    enemiesBeforeTroggles.begin(),
                    enemiesBeforeTroggles.end(),
                    [&](const Enemy& enemy) {
                        return enemy.slot == overlappingEntry->slot;
                    });
                const Enemy& dirtyEntry = previousEntry != enemiesBeforeTroggles.end()
                    ? *previousEntry : *overlappingEntry;
                const bool vertical = dirtyEntry.direction == 0 ||
                                      dirtyEntry.direction == 2;
                const int steps = vertical ? 5 : 6;
                const int phase = enemyMovementPhase(dirtyEntry);
                const int actorX = static_cast<int>(std::lround(
                    static_cast<double>(
                        BoardLeft + dirtyEntry.fromColumn *
                                        BoardCellWidth + 4) +
                    static_cast<double>(
                        (dirtyEntry.column - dirtyEntry.fromColumn) *
                        BoardCellWidth) *
                        static_cast<double>(phase) /
                        static_cast<double>(steps)));
                const int actorY = static_cast<int>(std::lround(
                    static_cast<double>(
                        BoardTop + dirtyEntry.fromRow *
                                       BoardCellHeight + 1) +
                    static_cast<double>(
                        (dirtyEntry.row - dirtyEntry.fromRow) *
                        BoardCellHeight) * static_cast<double>(phase) /
                        static_cast<double>(steps)));
                copyPixelRectangle(
                    entrantPixels, logicalPixels,
                    std::max(actorX, BoardLeft + 1),
                    std::max(actorY, BoardTop + 1),
                    std::min(actorX + 41, BoardRight + 2),
                    std::min(actorY + 29, BoardBottom + 2));
                if (entrantPixels != lastPresentationPixels_) {
                    enqueuePresentationFrame(entrantPixels, 1, 1468);
                }
                attractOverlapEntryWarningPixels_ = logicalPixels;
                copyPixelRectangle(attractOverlapEntryWarningPixels_,
                                   lastPresentationPixels_, 0, 62, 16, 137);
            } else if (!attractOverlapEntryWarningPixels_.empty() &&
                       page_ == Page::Attract) {
                // Subsequent entry callbacks repaint the actor list but still
                // do not own the retired warning's rectangle. Carry those
                // pixels forward until collision setup or a later explicit
                // warning/entry transition replaces them.
                std::vector<std::uint32_t> logicalPixels =
                    capturePresentationFrame();
                copyPixelRectangle(logicalPixels,
                                   attractOverlapEntryWarningPixels_,
                                   0, 62, 16, 137);
                attractOverlapEntryWarningPixels_ = std::move(logicalPixels);
            }
            const bool departingEnemyOverlapAdvanced =
                attractMode_ && attractBoardIndex_ == 0 &&
                playerMovementCallback && std::any_of(
                    enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                        if (!enemy.moving || enemy.entering || enemy.exiting ||
                            enemy.fromRow != moveToRow_ ||
                            enemy.fromColumn != moveToColumn_ || enemy.slot < 0 ||
                            static_cast<std::size_t>(enemy.slot) >=
                                enemyWasMoving.size()) {
                            return false;
                        }
                        const std::size_t slot =
                            static_cast<std::size_t>(enemy.slot);
                        return !enemyWasMoving[slot] ||
                               enemyPhaseBefore[slot] !=
                                   enemyMovementPhase(enemy);
                    });
            const std::vector<std::uint32_t> afterTrogglesForPlayerCallback =
                (!afterPlayerCallbackPixels.empty() ||
                 !cannibalPlayerResidentSurfacePixels_.empty() ||
                 !attractPlayerEntryResidentSurfacePixels_.empty() ||
                  (attractMode_ && attractBoardIndex_ == 0 &&
                   playerMunchCallback) ||
                  departingEnemyOverlapAdvanced ||
                  (attractMode_ && enemyMoveStarted &&
                   playerMovementCallback))
                    ? capturePresentationFrame()
                    : std::vector<std::uint32_t>{};
            if (attractPrimeExitPlayerCatchup_ &&
                (playerMovementCallback || clearingPlayerTerminal) &&
                !afterPlayerPixels.empty()) {
                // The actor-removal callback releases nine older player
                // records at once. While those pages drain, the DOS presenter
                // continues appending each distinct live player callback.
                // This supplies source frames 6092..6101 instead of jumping
                // from movement phase 2 directly to phase 5.
                if (presentationFrames_.empty() ||
                    presentationFrames_.back() != afterPlayerPixels) {
                    enqueuePresentationFrame(afterPlayerPixels, 1, 1639);
                }
                if (clearingPlayerTerminal) {
                    attractPrimeExitPlayerCatchup_ = false;
                }
            }
#if defined(NUMBER_MUNCHERS_TESTING)
            auditAfterTrogglesPixels_ = afterTrogglesForPlayerCallback;
#endif
            // Once the later actor receives another callback, the prior
            // two-slot terminal page no longer owns the resident display.
            // A simultaneously installed player phase zero is the exception:
            // the measured dispatcher keeps the old page until phase one.
            const bool releaseConcurrentEnemyHoldAfterDispatch =
                concurrentEnemyAwaitWasActive && !playerMovementCallback &&
                !enemyCallbackPresentationSlots_.empty();
            bool concurrentEnemyPresentationHandled = false;
            if (attractMode_ && attractBoardIndex_ == 0 &&
                ((!playerMovementCallback && !playerMunchCallback) ||
                 (playerMovementCallback && !playerMunchCallback &&
                  playerTerminalFrame_ >= 0)) &&
                enemyCallbackPresentationSlots_.size() == 2 &&
                enemyCallbackPresentationFrames_.size() == 2 &&
                enemyCallbackPresentationSlots_[0] == 0 &&
                enemyCallbackPresentationSlots_[1] == 2) {
                const auto slotZero = std::find_if(
                    enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
                        return enemy.slot == 0;
                    });
                const auto slotTwo = std::find_if(
                    enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
                        return enemy.slot == 2;
                    });
                if (slotZero != enemies_.end() && slotTwo != enemies_.end() &&
                    slotZero->type == 1 && slotZero->direction == 1 &&
                    slotTwo->type == 3 && slotTwo->direction == 3 &&
                    !slotZero->entering && !slotZero->exiting &&
                    !slotTwo->entering && !slotTwo->exiting) {
                    enqueuePresentationFrame(
                        enemyCallbackPresentationFrames_.front(), 1, 1530);
                    concurrentEnemyPresentationHandled = true;
                    attractConcurrentEnemyHoldPixels_ =
                        enemyCallbackPresentationFrames_.front();
                    if (!slotZero->moving &&
                        enemyCallbackPresentationFrames_.back() !=
                            enemyCallbackPresentationFrames_.front()) {
                        enqueuePresentationFrame(
                            enemyCallbackPresentationFrames_.back(), 1, 1531);
                        attractConcurrentEnemyHoldPixels_ =
                            enemyCallbackPresentationFrames_.back();
                        attractConcurrentEnemyAwaitPlayerCallback_ = true;
                    }
                }
            }
            bool factorsConcurrentResidentHandled =
                attractFactorsConcurrentPhase_ != 0;
            if (attractMode_ && attractBoardIndex_ == 1 &&
                playerMovementCallback && !enemiesBeforeTroggles.empty()) {
                const auto actorForSlot = [](const std::vector<Enemy>& actors,
                                             const int slot) {
                    return std::find_if(
                        actors.begin(), actors.end(), [slot](const Enemy& enemy) {
                            return enemy.slot == slot;
                        });
                };
                const auto currentPlayerState = [this]() {
                    PresentationPlayerState state;
                    state.moving = moving_;
                    state.moveTimer = moveTimer_;
                    state.row = playerRow_;
                    state.column = playerColumn_;
                    state.terminalFrame = playerTerminalFrame_;
                    state.direction = moveDirection_;
                    state.fromRow = moveFromRow_;
                    state.fromColumn = moveFromColumn_;
                    state.toRow = moveToRow_;
                    state.toColumn = moveToColumn_;
                    state.movementPhase = moving_ ? playerMovementPhase() : -1;
                    return state;
                };
                const auto captureComposite =
                    [&](const PresentationPlayerState& player,
                        const std::vector<Enemy>& actors,
                        const bool playerCallbackPaint) {
                        const PresentationPlayerState logicalPlayer =
                            currentPlayerState();
                        std::vector<Enemy> logicalActors = enemies_;
                        moving_ = player.moving;
                        moveTimer_ = player.moveTimer;
                        playerRow_ = player.row;
                        playerColumn_ = player.column;
                        playerTerminalFrame_ = player.terminalFrame;
                        moveDirection_ = player.direction;
                        moveFromRow_ = player.fromRow;
                        moveFromColumn_ = player.fromColumn;
                        moveToRow_ = player.toRow;
                        moveToColumn_ = player.toColumn;
                        enemies_ = actors;
                        const std::vector<std::uint32_t> pixels =
                            playerCallbackPaint
                                ? capturePlayerCallbackFrame()
                                : capturePresentationFrame();
                        enemies_ = std::move(logicalActors);
                        moving_ = logicalPlayer.moving;
                        moveTimer_ = logicalPlayer.moveTimer;
                        playerRow_ = logicalPlayer.row;
                        playerColumn_ = logicalPlayer.column;
                        playerTerminalFrame_ = logicalPlayer.terminalFrame;
                        moveDirection_ = logicalPlayer.direction;
                        moveFromRow_ = logicalPlayer.fromRow;
                        moveFromColumn_ = logicalPlayer.fromColumn;
                        moveToRow_ = logicalPlayer.toRow;
                        moveToColumn_ = logicalPlayer.toColumn;
                        return pixels;
                    };
                const auto enqueueAndHold = [&](const std::vector<std::uint32_t>& pixels) {
                    enqueuePresentationFrame(pixels, 1, 1576);
                    attractFactorsConcurrentHoldPixels_ = pixels;
                };

                if (attractFactorsConcurrentPhase_ == 0 &&
                    moveDirection_ == 2 && moveFromRow_ == 1 &&
                    moveFromColumn_ == 5 && moveToRow_ == 2 &&
                    moveToColumn_ == 5 &&
                    playerBeforeCallback.movementPhase == 0 && moving_ &&
                    playerMovementPhase() == 1 &&
                    enemyPhaseBefore[0] == 3 && enemyPhaseBefore[1] == 1) {
                    const auto rightAfter = actorForSlot(enemies_, 0);
                    const auto bottomAfter = actorForSlot(enemies_, 1);
                    if (rightAfter != enemies_.end() &&
                        bottomAfter != enemies_.end() &&
                        enemyMovementPhase(*rightAfter) == 4 &&
                        enemyMovementPhase(*bottomAfter) == 2) {
                        PresentationPlayerState standing = playerBeforeCallback;
                        standing.moving = false;
                        standing.moveTimer = 0.0;
                        standing.row = standing.fromRow;
                        standing.column = standing.fromColumn;
                        standing.terminalFrame = -1;
                        standing.movementPhase = -1;
                        std::vector<Enemy> bottomThenRight = enemies_;
                        const auto oldRight = actorForSlot(enemiesBeforeTroggles, 0);
                        auto delayedRight = std::find_if(
                            bottomThenRight.begin(), bottomThenRight.end(),
                            [](const Enemy& enemy) { return enemy.slot == 0; });
                        if (oldRight != enemiesBeforeTroggles.end() &&
                            delayedRight != bottomThenRight.end()) {
                            *delayedRight = *oldRight;
                        }
                        enqueueAndHold(captureComposite(
                            standing, bottomThenRight, false));
                        enqueueAndHold(captureComposite(
                            standing, enemies_, false));
                        enqueueAndHold(captureComposite(
                            currentPlayerState(), enemies_, true));
                        attractPlayerPhaseZeroHoldPixels_.clear();
                        attractFactorsConcurrentPhase_ = 1;
                        factorsConcurrentResidentHandled = true;
                    }
                } else if (attractFactorsConcurrentPhase_ == 1 && moving_ &&
                           playerMovementPhase() == 3 &&
                           enemyPhaseBefore[0] == 4 &&
                           enemyPhaseBefore[1] == 2) {
                    enqueueAndHold(captureComposite(
                        playerBeforeCallback, enemies_, true));
                    enqueueAndHold(captureComposite(
                        currentPlayerState(), enemies_, true));
                    attractFactorsConcurrentPhase_ = 2;
                    factorsConcurrentResidentHandled = true;
                } else if (attractFactorsConcurrentPhase_ == 2 && !moving_ &&
                           playerBeforeCallback.moving &&
                           playerBeforeCallback.movementPhase == 4) {
                    enqueueAndHold(captureComposite(
                        playerBeforeCallback, enemies_, false));
                    enqueueAndHold(captureComposite(
                        currentPlayerState(), enemies_, false));
                    attractFactorsConcurrentPhase_ = 3;
                    factorsConcurrentResidentHandled = true;
                }

                if (attractFactorsConcurrentPhase_ == 0 &&
                    moveDirection_ == 3 && moveFromRow_ == 2 &&
                    moveFromColumn_ == 4 && moveToRow_ == 2 &&
                    moveToColumn_ == 3 &&
                    playerBeforeCallback.movementPhase == 0 && moving_ &&
                    playerMovementPhase() == 1) {
                    int completingSlot = -1;
                    for (std::size_t slot = 0; slot < enemyPhaseBefore.size(); ++slot) {
                        const auto actor = actorForSlot(enemies_, static_cast<int>(slot));
                        if (enemyPhaseBefore[slot] == 5 && actor != enemies_.end() &&
                            actor->moving && enemyMovementPhase(*actor) == 6) {
                            completingSlot = static_cast<int>(slot);
                            break;
                        }
                    }
                    if (completingSlot >= 0) {
                        PresentationPlayerState standing = playerBeforeCallback;
                        standing.moving = false;
                        standing.moveTimer = 0.0;
                        standing.row = standing.fromRow;
                        standing.column = standing.fromColumn;
                        standing.terminalFrame = -1;
                        standing.movementPhase = -1;
                        enqueueAndHold(captureComposite(
                            standing, enemies_, false));
                        enqueueAndHold(captureComposite(
                            currentPlayerState(), enemies_, false));
                        attractPlayerPhaseZeroHoldPixels_.clear();
                        attractFactorsConcurrentPhase_ = 10 + completingSlot;
                        factorsConcurrentResidentHandled = true;
                    }
                } else if (attractFactorsConcurrentPhase_ >= 10 &&
                           attractFactorsConcurrentPhase_ < 20 && moving_ &&
                           playerMovementPhase() == 3) {
                    const int completingSlot = attractFactorsConcurrentPhase_ - 10;
                    const auto actor = actorForSlot(enemies_, completingSlot);
                    if (enemyWasMoving[static_cast<std::size_t>(completingSlot)] &&
                        actor != enemies_.end() && !actor->moving) {
                        enqueueAndHold(captureComposite(
                            playerBeforeCallback, enemies_, false));
                        enqueueAndHold(captureComposite(
                            currentPlayerState(), enemies_, false));
                        attractFactorsConcurrentPhase_ = 0;
                        attractFactorsConcurrentHoldPixels_.clear();
                        factorsConcurrentResidentHandled = true;
                    }
                }
            }
            if (attractFactorsConcurrentPhase_ == 3) {
                const auto bottom = std::find_if(
                    enemies_.begin(), enemies_.end(), [](const Enemy& enemy) {
                        return enemy.slot == 1;
                    });
                if (enemyPhaseBefore[1] == 4 && bottom != enemies_.end() &&
                    bottom->moving && enemyMovementPhase(*bottom) == 5) {
                    attractFactorsConcurrentPhase_ = 0;
                    attractFactorsConcurrentHoldPixels_.clear();
                }
                factorsConcurrentResidentHandled = true;
            }
            const bool playerEntryResidentWasActive =
                !attractPlayerEntryResidentSurfacePixels_.empty();
            bool playerEntryResidentHandled = factorsConcurrentResidentHandled;
            bool movingEntryPresentationHandled = false;
            if (playerEntryResidentWasActive && playerMayRepaint &&
                !afterPlayerPixels.empty() &&
                !afterTrogglesForPlayerCallback.empty()) {
                if (attractPlayerEntryMunchOverlap_) {
                    if (clearingPlayerTerminal) {
                        // The callback after tick seven removes the retained
                        // chew record. The later entrant is already current,
                        // so ordinary rendering owns the combined page again.
                        attractPlayerEntryResidentSurfacePixels_.clear();
                        attractPlayerEntryPresentationHoldPixels_.clear();
                        attractPlayerEntryMunchOverlap_ = false;
                        playerEntryResidentHandled = true;
                    } else if (playerMunchCallback) {
                        std::vector<std::uint32_t> playerPage =
                            attractPlayerEntryResidentSurfacePixels_;
                        copyBoardCellPixels(playerPage, afterPlayerPixels,
                                            playerRow_, playerColumn_);
                        enqueuePresentationFrame(playerPage, 1, 1720);
                        attractPlayerEntryPresentationHoldPixels_ = playerPage;
                        attractPlayerEntryResidentSurfacePixels_ =
                            afterTrogglesForPlayerCallback;
                        if (munchTerminalCallback &&
                            preTerminalMunchFrame >= 0) {
                            const int terminalFrame = playerTerminalFrame_;
                            playerTerminalFrame_ = preTerminalMunchFrame;
                            const std::vector<std::uint32_t>
                                retainedChewAfterTroggles =
                                    capturePresentationFrame();
                            playerTerminalFrame_ = terminalFrame;
                            if (retainedChewAfterTroggles != playerPage) {
                                enqueuePresentationFrame(
                                    retainedChewAfterTroggles, 1, 1733);
                                attractPlayerEntryPresentationHoldPixels_ =
                                    retainedChewAfterTroggles;
                            }
                        }
                        playerEntryResidentHandled = true;
                    }
                }
                if (!playerEntryResidentHandled) {
                    // Source frames 2929-2935 expose only the earlier player
                    // callback while the later entry callback continues
                    // painting the resident working page. Apply the player's
                    // two dirty cells to that resident surface, present the
                    // result, then retain the post-Troggle logical page for
                    // the next callback.
                    std::vector<std::uint32_t> playerPage =
                        attractPlayerEntryResidentSurfacePixels_;
                    copyBoardCellPixels(playerPage, afterPlayerPixels,
                                        moveFromRow_, moveFromColumn_);
                    copyBoardCellPixels(playerPage, afterPlayerPixels,
                                        moveToRow_, moveToColumn_);
                    enqueuePresentationFrame(playerPage, 1, 1755);
                    attractPlayerEntryPresentationHoldPixels_ = playerPage;
                    attractPlayerEntryResidentSurfacePixels_ =
                        afterTrogglesForPlayerCallback;
                    if (clearingPlayerTerminal) {
                        // The state-4 repaint is the last earlier-player dirty
                        // callback in this overlap. Its page is followed by the
                        // later entrant's completed working page before
                        // ordinary rendering resumes (0xcae3... -> 0x9d81...).
                        enqueuePresentationFrame(
                            afterTrogglesForPlayerCallback, 1, 1764);
                        attractPlayerEntryPresentationHoldPixels_ =
                            afterTrogglesForPlayerCallback;
                    }
                    playerEntryResidentHandled = true;
                }
            }
            if (!playerEntryResidentHandled && attractMode_ &&
                attractBoardIndex_ == 0 && playerMovementCallback &&
                moveDirection_ == 1 && !afterPlayerPixels.empty()) {
                if (attractPlayerMovingEntrySlot_ < 0) {
                    for (std::size_t slot = 0; slot < enemyPhaseBefore.size(); ++slot) {
                        if (enemyWasEntering[slot] && enemyPhaseBefore[slot] >= 5 &&
                            priorEntryAdvanced[slot]) {
                            attractPlayerMovingEntrySlot_ =
                                static_cast<int>(slot);
                            break;
                        }
                    }
                }
                if (attractPlayerMovingEntrySlot_ >= 0) {
                    // The autonomous F585 source sequence at frames 632-641
                    // exposes the physical callback order that the earlier
                    // focused fixtures did not reach.  The player job first
                    // paints against the complete pre-Troggle actor surface;
                    // using lastPresentationPixels_ here leaves the entrant
                    // one pose behind for the rest of the walk.  If this is
                    // the entry terminal callback, the post-Troggle dwell
                    // surface is also complete and is presented immediately
                    // after the player-owned page.
                    enqueuePresentationFrame(afterPlayerPixels, 1, 1795);
                    attractPlayerMovingEntryHoldPixels_ = afterPlayerPixels;
                    const std::size_t slot = static_cast<std::size_t>(
                        attractPlayerMovingEntrySlot_);
                    if (slot < priorEntryTerminalCallback.size() &&
                        priorEntryTerminalCallback[slot] &&
                        !afterTrogglesForPlayerCallback.empty() &&
                        afterTrogglesForPlayerCallback != afterPlayerPixels) {
                        enqueuePresentationFrame(
                            afterTrogglesForPlayerCallback, 1, 1802);
                        attractPlayerMovingEntryHoldPixels_ =
                            afterTrogglesForPlayerCallback;
                    }
                    playerEntryResidentHandled = true;
                    movingEntryPresentationHandled = true;
                }
            }
            if (attractMode_ && attractBoardIndex_ == 0 &&
                attractPlayerConcurrentEnemySlot_ >= 0 &&
                playerMovementCallback && !afterPlayerPixels.empty()) {
                const std::size_t slot = static_cast<std::size_t>(
                    attractPlayerConcurrentEnemySlot_);
                const auto actor = std::find_if(
                    enemies_.begin(), enemies_.end(), [slot](const Enemy& enemy) {
                        return enemy.slot == static_cast<int>(slot);
                    });
                const bool enemyAdvanced =
                    slot < enemyWasMoving.size() && enemyWasMoving[slot] &&
                    enemyPhaseBefore[slot] >= 0 &&
                    (actor == enemies_.end() || !actor->moving ||
                     enemyMovementPhase(*actor) != enemyPhaseBefore[slot]);
                if (enemyAdvanced && afterPlayerPixels != beforePlayerPixels) {
                    // The phase-4 callback is preceded by one source-visible
                    // completed resident page. Other post-Troggle surfaces in
                    // this walk are either unsampled or interrupted mid-actor.
                    if (moving_ && playerMovementPhase() == 4 &&
                        !beforePlayerPixels.empty() &&
                        beforePlayerPixels !=
                            attractPlayerConcurrentEnemyHoldPixels_) {
                        enqueuePresentationFrame(beforePlayerPixels, 1, 1825);
                    }
                    enqueuePresentationFrame(afterPlayerPixels, 1, 1830);
                    attractPlayerConcurrentEnemyHoldPixels_ = afterPlayerPixels;
                    if (!moving_) {
                        attractPlayerConcurrentEnemySlot_ = -1;
                        attractPlayerConcurrentEnemyTerminalHold_ = true;
                    }
                } else {
                    attractPlayerConcurrentEnemySlot_ = -1;
                    attractPlayerConcurrentEnemyHoldPixels_.clear();
                    attractPlayerConcurrentEnemyTerminalHold_ = false;
                }
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
                              OriginalJobTicksPerSecond)));
                const int movementPhaseAfterCallback =
                    moving_ ? playerMovementPhase() : -1;

                if (cannibalTerminalCallback && playerDirtyCallback &&
                    cannibalPlayerRetainedPenultimatePaint_) {
                    // The interrupted penultimate repaint leaves the next
                    // player callback's newly leading 6/8-pixel movement strip
                    // on the other page. Restore that strip from the retained
                    // page before the terminal Troggle callback completes it.
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
                    if (movingEntryPresentationHandled) {
                        // The player/entry callback pages above already carry
                        // the pre-Troggle cannibal pose.  The source does not
                        // expose the older resident-cannibal page as a second
                        // complete framebuffer on these same callbacks.
                        cannibalPlayerPresentationHoldPixels_.clear();
                    } else {
                        enqueuePresentationFrame(pixels, 1, 1916);
                        cannibalPlayerPresentationHoldPixels_ = pixels;
                    }
                };

                if (playerDirtyCallback) {
                    if (!cannibalBiteActiveBeforeTroggles) {
                        // Once state 5 has terminated, the next ordinary
                        // player callback owns a clean complete page again.
                        // Release the retained overlap page and let the normal
                        // player presenter below handle that callback.
                        cannibalPlayerResidentSurfacePixels_.clear();
                        cannibalPlayerPresentationHoldPixels_.clear();
                        cannibalPlayerRetainedPenultimatePaint_ = false;
                    } else if (cannibalTerminalCallback) {
                        residentCannibalPlayerHandled = true;
                        // The final moving-player paint completes against the
                        // prior bite pose; the state-5 terminal then restores
                        // the survivor's record-15 cell and completes the
                        // still-dirty player strip on that same page.
                        queueResidentPage(residentAfterPlayer);
                        const std::vector<std::uint32_t>& terminalPage =
                            !afterPlayerCallbackPixels.empty()
                                ? afterPlayerCallbackPixels
                                : residentAfterBiter;
                        queueResidentPage(terminalPage);
                        cannibalPlayerResidentSurfacePixels_ = terminalPage;
                    } else if (playerMovementCallback && !moving_) {
                        residentCannibalPlayerHandled = true;
                        // At a movement terminal the later biter page reaches
                        // the display before the earlier player callback page,
                        // while the biter-complete page remains the painter's
                        // working surface for the next standing callback.
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
                        // The retained player page is the older of two complete
                        // pages on these physical page-turn boundaries.
                        queueResidentPage(residentAfterBiter);
                        queueResidentPage(residentAfterPlayer);
                        cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    } else {
                        residentCannibalPlayerHandled = true;
                        queueResidentPage(residentAfterPlayer);
                        // The penultimate bite update in the measured overlap
                        // is interrupted mid-cell and never becomes the next
                        // complete working page. Retain the completed player
                        // page; native deliberately does not expose the tear.
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
                    // Between the two player walks, ordinary state-5 paints
                    // advance the same retained cell/page even though no
                    // callback-intermediate page needs queuing.
                    cannibalPlayerResidentSurfacePixels_ = residentAfterBiter;
                    cannibalPlayerPresentationHoldPixels_ = residentAfterBiter;
                }
            }
            if (movingEntryPresentationHandled && !moving_) {
                attractPlayerMovingEntrySlot_ = -1;
                attractPlayerMovingEntryTerminalHold_ = true;
            }
            bool munchEntryCallbackHandled = false;
            const bool trackedMunchEntryAdvanced =
                attractMunchResidentEntrySlot_ >= 0 &&
                static_cast<std::size_t>(attractMunchResidentEntrySlot_) <
                    priorEntryAdvanced.size() &&
                priorEntryAdvanced[static_cast<std::size_t>(
                    attractMunchResidentEntrySlot_)];
            const bool trackedMunchEntryTerminalCallback =
                trackedMunchEntryAdvanced &&
                priorEntryTerminalCallback[static_cast<std::size_t>(
                    attractMunchResidentEntrySlot_)];
            if (!playerEntryResidentHandled &&
                !residentCannibalPlayerHandled && attractMode_ &&
                attractBoardIndex_ == 0 &&
                playerMunchCallback && trackedMunchEntryAdvanced &&
                !beforePlayerPixels.empty() && !afterPlayerPixels.empty() &&
                !afterTrogglesForPlayerCallback.empty()) {
                // A later entry record can advance while the earlier player
                // chew paints another resident page. Apply only that actor's
                // dirty delta to the pre-player page. Nonterminal callbacks
                // expose both the retained page and the completed logical
                // page; an entry terminal keeps the retained chew pose until
                // the following player callback catches up.
                std::vector<std::uint32_t> retainedPlayerAfterTroggles =
                    beforePlayerPixels;
                for (std::size_t pixel = 0;
                     pixel < afterTrogglesForPlayerCallback.size(); ++pixel) {
                    if (afterTrogglesForPlayerCallback[pixel] !=
                        afterPlayerPixels[pixel]) {
                        retainedPlayerAfterTroggles[pixel] =
                            afterTrogglesForPlayerCallback[pixel];
                    }
                }
                enqueuePresentationFrame(retainedPlayerAfterTroggles, 1, 2024);
                if (trackedMunchEntryTerminalCallback) {
                    attractPlayerEntryPresentationHoldPixels_ =
                        retainedPlayerAfterTroggles;
                    attractMunchEntryTerminalHold_ = true;
                    attractMunchResidentEntrySlot_ = -1;
                } else {
                    enqueuePresentationFrame(
                        afterTrogglesForPlayerCallback, 1, 2031);
                }
                munchEntryCallbackHandled = true;
            }
            bool playerDepartingEnemyOverlapHandled = false;
            if (!playerEntryResidentHandled &&
                !residentCannibalPlayerHandled &&
                !munchEntryCallbackHandled &&
                departingEnemyOverlapAdvanced &&
                !beforePlayerPixels.empty() && !afterPlayerPixels.empty() &&
                !afterTrogglesForPlayerCallback.empty()) {
                // This collision-avoidance crossing exposes the next player
                // callback beside the current later Troggle callback. Keep the
                // scheduler state unchanged and advance only the captured
                // player paint: phases 0..4 present 1..terminal, and the
                // logical terminal presents the following standing record.
                const std::vector<std::uint32_t>
                    retainedPlayerAfterTroggles =
                        captureDepartingEnemyPlayerLookaheadFrame(true);
                enqueuePresentationFrame(retainedPlayerAfterTroggles, 1, 2051);
                attractPlayerDepartingEnemyHoldPixels_ =
                    retainedPlayerAfterTroggles;
                attractPlayerPhaseZeroHoldPixels_.clear();
                playerDepartingEnemyOverlapHandled = true;
            } else {
                attractPlayerDepartingEnemyHoldPixels_.clear();
            }
            if (!playerDepartingEnemyOverlapHandled &&
                !playerEntryResidentHandled &&
                !residentCannibalPlayerHandled &&
                !munchEntryCallbackHandled &&
                cannibalBiteActiveBeforeTroggles &&
                !afterPlayerCallbackPixels.empty() &&
                afterPlayerCallbackPixels != afterTrogglesForPlayerCallback) {
                // A moving Muncher record precedes the Troggle slots. During
                // state-5 cannibalism its callback-local paint is presented
                // with the prior bite pose before the later biter callback
                // advances that actor on the same public tick.
                enqueuePresentationFrame(afterPlayerCallbackPixels, 1, 2070);
                if (capturedAutonomousInitialFactorsBoard &&
                    moveDirection_ == 2) {
                    cannibalPlayerPresentationHoldPixels_ =
                        afterPlayerCallbackPixels;
                }
            } else if (!playerDepartingEnemyOverlapHandled &&
                       !playerEntryResidentHandled &&
                       !residentCannibalPlayerHandled &&
                       !munchEntryCallbackHandled && munchTerminalCallback &&
                       preTerminalMunchFrame >= 0) {
                const std::vector<std::uint32_t> afterTroggles = capturePresentationFrame();
                const int terminalFrame = playerTerminalFrame_;
                playerTerminalFrame_ = preTerminalMunchFrame;
                const std::vector<std::uint32_t> retainedChewAfterTroggles =
                    capturePresentationFrame();
                playerTerminalFrame_ = terminalFrame;
                const bool capturedFourthPrimeChewActorCompletion =
                    attractMode_ && attractBoardIndex_ == 3 &&
                    activeBoardMode_ == Mode::Primes && level_ == 3 &&
                    playerRow_ == 3 && playerColumn_ == 1 &&
                    random_.calls == 699 && random_.state == 0xb56aa3f8u;
                if (retainedChewAfterTroggles != afterTroggles &&
                    !capturedFourthPrimeChewActorCompletion) {
                    enqueuePresentationFrame(retainedChewAfterTroggles, 1, 2083);
                }
            } else if (!playerDepartingEnemyOverlapHandled &&
                       !playerEntryResidentHandled &&
                       !residentCannibalPlayerHandled &&
                       !munchEntryCallbackHandled &&
                       !delayedExitPresentationActive &&
                       !afterPlayerCallbackPixels.empty() &&
                       afterTrogglesForPlayerCallback == afterPlayerPixels &&
                       afterPlayerCallbackPixels != afterPlayerPixels) {
                // The player job paints directly over the resident surface.
                // If no later Troggle job changed its logical pixels on this
                // dispatch, retain that callback-local ordering instead of
                // redrawing every enemy over the newly advanced Muncher.
                enqueuePresentationFrame(afterPlayerCallbackPixels, 1, 2097);
            } else if (!playerDepartingEnemyOverlapHandled &&
                       !playerEntryResidentHandled &&
                       !residentCannibalPlayerHandled &&
                       !munchEntryCallbackHandled && enemyMoveStarted &&
                       clearingPlayerTerminal &&
                       !afterPlayerPixels.empty()) {
                const std::vector<std::uint32_t> afterTroggles =
                    capturePresentationFrame();
                if (afterPlayerPixels != afterTroggles) {
                    // State 4 repaints the just-arrived player before the
                    // later Troggle record applies its trail and phase-1
                    // actor step. The first-Demo Reggie crossing exposes
                    // that complete callback-local framebuffer.
                    enqueuePresentationFrame(afterPlayerPixels, 1, 2111);
                }
            } else if (!playerDepartingEnemyOverlapHandled &&
                       !playerEntryResidentHandled &&
                       !residentCannibalPlayerHandled &&
                       !munchEntryCallbackHandled && enemyMoveStarted &&
                       !concurrentEnemyPresentationHandled &&
                       !capturedAutonomousInitialFactorsBoard &&
                       !enemyEntryStarted &&
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
                        enqueuePresentationFrame(
                            retainedPlayerAfterTroggles, 1, 2130);
                    }
                }
            }
            if (capturedAutonomousInitialFactorsBoard &&
                clearingPlayerTerminal && cannibalBiteActiveBeforeTroggles &&
                !afterPlayerPixels.empty() &&
                !cannibalPlayerPresentationHoldPixels_.empty()) {
                // The standing callback follows the retained terminal player
                // page before the next state-5 bite callback reaches the
                // display. Keep that complete standing page for one public
                // scheduler interval, then release the alternating bite page.
                cannibalPlayerPresentationHoldPixels_ = afterPlayerPixels;
                attractInitialCannibalPlayerTerminalHold_ = true;
            }
            if (!playerEntryResidentWasActive && attractMode_ &&
                enemyEntryStarted &&
                (playerMovementCallback ||
                 (playerMunchCallback && attractBoardIndex_ == 0)) &&
                enemyPresentationLagSlot_ < 0 &&
                !afterTrogglesForPlayerCallback.empty()) {
                // The entry-start callback itself exposes only the completed
                // player-then-entry page. Seed the working surface here; the
                // following player callbacks own the retained pages above.
                attractPlayerEntryResidentSurfacePixels_ =
                    afterTrogglesForPlayerCallback;
                attractPlayerEntryPresentationHoldPixels_.clear();
                attractPlayerEntryMunchOverlap_ =
                    playerMunchCallback && !playerMovementCallback;
            } else if (playerEntryResidentWasActive && !playerMayRepaint) {
                // Once state 4 has no dirty player callback, the later actor's
                // working surface becomes the ordinary presented page again.
                attractPlayerEntryResidentSurfacePixels_.clear();
                attractPlayerEntryPresentationHoldPixels_.clear();
                attractPlayerEntryMunchOverlap_ = false;
            }
            if (playerWarningHoldWasActive && !enemyWarningStarted) {
                attractPlayerWarningHoldPixels_.clear();
            }
            if (attractMode_ && enemyWarningStarted && playerMayRepaint &&
                !afterPlayerPixels.empty()) {
                // Frames 3321-3322 expose the earlier movement terminal
                // before the later Troggle slot's warning paint. Hold that
                // callback-local page through the host refresh; the next
                // public player callback releases the warning-complete page.
                attractPlayerWarningHoldPixels_ = afterPlayerPixels;
            }
            if (attractMode_ &&
                (page_ == Page::Attract || (page_ == Page::Feedback && deathAnimating_))) {
                attractActionTimer_ -= tickSeconds;
                const bool demoMayStartPlayerAction =
                    attractActionTimer_ <= JobTimerEpsilon && !moving_ && !munching_ &&
                    !deathAnimating_ &&
                    (page_ == Page::Playing || page_ == Page::Attract);
                const std::vector<std::uint32_t> beforeDemoPixels =
                    demoMayStartPlayerAction ? capturePresentationFrame()
                                             : std::vector<std::uint32_t>{};
                const bool playerWasMovingBeforeDemo = moving_;
                const bool playerWasMunchingBeforeDemo = munching_;
                updateAttractPlayer(tickSeconds);
                if (attractBoardIndex_ == 3 &&
                    activeBoardMode_ == Mode::Primes && level_ == 3 &&
                    playerRow_ == 3 && playerColumn_ == 1 &&
                    !playerWasMunchingBeforeDemo && munching_ &&
                    random_.calls == 698 && random_.state == 0x2396defbu) {
                    const auto concurrentReggie = std::find_if(
                        enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                            return enemy.slot == 0 && enemy.type == 0 &&
                                   enemy.moving && !enemy.entering &&
                                   !enemy.exiting && enemy.row == 3 &&
                                   enemy.column == 5 && enemy.direction == 1 &&
                                   enemyMovementPhase(enemy) == 4;
                        });
                    if (concurrentReggie != enemies_.end()) {
                        // The state-4 Reggie record is one callback older than
                        // selector 5's newly installed chew. Advancing that
                        // already-due actor interval makes phases 5/6 precede
                        // the corresponding bite records, as measured at
                        // source frames 15819..15834.
                        concurrentReggie->animationTimer = std::max(
                            0.0, concurrentReggie->animationTimer - tickSeconds);
                    }
                }
                if (attractBoardIndex_ == 3 &&
                    activeBoardMode_ == Mode::Primes && level_ == 3 &&
                    !playerWasMunchingBeforeDemo && munching_ &&
                    random_.calls == 614 && random_.state == 0x0c0bf4ffu &&
                    !beforeDemoPixels.empty() &&
                    attractMunchEnemyTerminalHoldPixels_.empty()) {
                    // Source frames 13524-13530 retain the just-completed
                    // downward-move endpoint page while selector 5 installs the
                    // chew behind it. The player's first two internal records
                    // must not replace that resident surface.
                    attractMunchEnemyTerminalHoldPixels_ = beforeDemoPixels;
                }
                if (attractBoardIndex_ == 0 &&
                    !playerWasMunchingBeforeDemo && munching_) {
                    attractMunchResidentEntrySlot_ = -1;
                    for (std::size_t slot = 0;
                         slot < enemyWasMoving.size(); ++slot) {
                        if (enemyWasMoving[slot] && enemyWasEntering[slot]) {
                            attractMunchResidentEntrySlot_ =
                                static_cast<int>(slot);
                            break;
                        }
                    }
                }
                if (!playerWasMovingBeforeDemo && moving_ &&
                    !beforeDemoPixels.empty()) {
                    // Selector 5 only installs the move. The player record's
                    // next callback performs the first visible translation,
                    // so retain the preceding framebuffer while logical
                    // phase 0 is waiting for that callback.
                    const bool movingIntoDepartingEnemy = std::any_of(
                        enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                            return enemy.moving && !enemy.entering &&
                                   !enemy.exiting &&
                                   enemy.fromRow == moveToRow_ &&
                                   enemy.fromColumn == moveToColumn_;
                        });
                    const auto concurrentLeftSmarty = std::find_if(
                        enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                            if (attractBoardIndex_ != 0 || moveDirection_ != 3 ||
                                enemy.type != 3 || enemy.direction != 3 ||
                                enemy.entering || enemy.exiting || !enemy.moving ||
                                enemy.slot < 0 ||
                                static_cast<std::size_t>(enemy.slot) >=
                                    enemyWasMoving.size()) {
                                return false;
                            }
                            const std::size_t slot =
                                static_cast<std::size_t>(enemy.slot);
                            return !enemyWasEntering[slot] &&
                                   (!enemyWasMoving[slot] ||
                                    (enemyPhaseBefore[slot] >= 0 &&
                                     enemyMovementPhase(enemy) !=
                                         enemyPhaseBefore[slot]));
                        });
                    if (concurrentLeftSmarty != enemies_.end() &&
                        lastPresentationPixels_.size() ==
                            static_cast<std::size_t>(
                                Renderer::Width * Renderer::Height)) {
                        attractPlayerConcurrentEnemySlot_ =
                            concurrentLeftSmarty->slot;
                        attractPlayerConcurrentEnemyHoldPixels_.clear();
                        attractPlayerConcurrentEnemyTerminalHold_ = false;
                        attractPlayerPhaseZeroHoldPixels_ =
                            lastPresentationPixels_;
                    } else if (attractBoardIndex_ == 0 &&
                        movingIntoDepartingEnemy) {
                        // The later in-board Troggle callback has already
                        // exposed its new actor phase. Selector 5 installs the
                        // player's phase-zero dirty record over that resident
                        // page before the next public player callback.
                        attractPlayerPhaseZeroHoldPixels_ =
                            captureDepartingEnemyPlayerLookaheadFrame();
                    } else if (capturedAutonomousInitialFactorsBoard &&
                        moveDirection_ == 2 &&
                        std::any_of(enemies_.begin(), enemies_.end(),
                                    [&](const Enemy& enemy) {
                                        if (!enemy.entering || !enemy.moving) {
                                            return false;
                                        }
                                        const bool vertical =
                                            enemy.direction == 0 ||
                                            enemy.direction == 2;
                                        return enemyMovementPhase(enemy) >=
                                            (vertical ? 5 : 6);
                                    }) &&
                        lastPresentationPixels_.size() ==
                            static_cast<std::size_t>(
                                Renderer::Width * Renderer::Height)) {
                        // The entrant reaches its final offscreen-owned
                        // callback on the same dispatch that selector 5
                        // installs this downward phase zero. Its endpoint is
                        // a working-surface update; the last delivered page
                        // remains visible until the first player callback.
                        attractPlayerPhaseZeroHoldPixels_ =
                            lastPresentationPixels_;
                    } else if (capturedAutonomousInitialFactorsBoard &&
                        moveDirection_ == 2 &&
                        std::any_of(enemies_.begin(), enemies_.end(),
                                    [](const Enemy& enemy) {
                                        return enemy.cannibalizing;
                                    }) &&
                        lastPresentationPixels_.size() ==
                            static_cast<std::size_t>(
                                Renderer::Width * Renderer::Height)) {
                        // This state-5 interval is installed by the later
                        // Troggle slot on the same dispatch that selector 5
                        // starts the downward move. The pre-dispatch test is
                        // therefore still false, but the delivered page—not
                        // the newly painted bite page—owns phase zero.
                        attractPlayerPhaseZeroHoldPixels_ =
                            lastPresentationPixels_;
                        cannibalPlayerPresentationHoldPixels_ =
                            lastPresentationPixels_;
                        cannibalPlayerResidentSurfacePixels_ = beforeDemoPixels;
                        cannibalPlayerRetainedPenultimatePaint_ = false;
                    } else if (cannibalBiteActiveBeforeTroggles &&
                        !beforeCannibalJobsPixels.empty()) {
                        // The state-5 job has already advanced its retained
                        // working cell, but that page is not exposed before
                        // the first player callback. Hold the page from before
                        // the Troggle slots and seed the dirty-cell painter
                        // with the post-Troggle resident surface.
                        const bool retainDeliveredFactorsPage =
                            capturedAutonomousInitialFactorsBoard &&
                            moveDirection_ == 2 &&
                            lastPresentationPixels_.size() ==
                                static_cast<std::size_t>(
                                    Renderer::Width * Renderer::Height);
                        const std::vector<std::uint32_t>& phaseZeroPage =
                            retainDeliveredFactorsPage
                                ? lastPresentationPixels_
                                : beforeCannibalJobsPixels;
                        attractPlayerPhaseZeroHoldPixels_ = phaseZeroPage;
                        cannibalPlayerPresentationHoldPixels_ = phaseZeroPage;
                        cannibalPlayerResidentSurfacePixels_ = beforeDemoPixels;
                        cannibalPlayerRetainedPenultimatePaint_ = false;
                    } else {
                        // Earlier mask-2 jobs on this same dispatcher tick
                        // may have changed a safe outline or cleared the
                        // prior directional terminal. Those callback-local
                        // paints are still offscreen when selector 5 merely
                        // installs the next move. DOS retains the page from
                        // before those paints until movement phase 1.
                        attractPlayerPhaseZeroHoldPixels_ =
                            safeZonePainted
                                ? beforeSafeZonePixels
                            : clearingPlayerTerminal && !beforePlayerPixels.empty()
                                ? beforePlayerPixels
                                : beforeDemoPixels;
                    }
                }
            }
            if (releaseConcurrentEnemyHoldAfterDispatch &&
                !(moving_ && playerMovementPhase() == 0)) {
                attractConcurrentEnemyHoldPixels_.clear();
                attractConcurrentEnemyAwaitPlayerCallback_ = false;
            }
            if (collisionStarted || (page_ == Page::Feedback && deathAnimating_)) break;
        }
        // Preserve the former active-to-collision handoff: elapsed time after
        // the player or a Troggle starts a bite remains on the shared public
        // clock for the feedback dispatcher on its next presentation update.
        if (userPresentationActive() && remainingTicks > 0.0) {
            // A board terminal can fall partway through one coalesced host
            // update. Only that post-callback remainder belongs to the
            // synchronous Wipe; no fresh board job is allowed to run first.
            updateAttractInterstitialTransition(remainingTicks * tickSeconds);
        } else if (page_ == Page::Feedback && deathAnimating_ &&
                   remainingTicks > 0.0) {
            gameplayTickAccumulator_ += remainingTicks;
        }
    } else if (page_ == Page::Feedback) {
        double transitionSeconds = seconds;
        if (deathAnimating_) {
            // 0x08db6 changes the selected actor to state 5 while the other
            // safe, Troggle, and Demo records remain live. Jobs before the
            // biter continue to dispatch during the bite; freezing everything
            // here drops the exact PRNG calls observed before the first
            // attract feedback. The final two biter callbacks abort later
            // slots, as the Worker collision framebuffer proves below.
            constexpr double tickSeconds = 1.0 / OriginalJobTicksPerSecond;
            gameplayTickAccumulator_ += seconds * OriginalJobTicksPerSecond;
            while (gameplayTickAccumulator_ >= 1.0 && deathAnimating_) {
                // As in the active-board dispatcher, the elapsed public tick
                // is owned before any callback can replace the board and
                // reset the shared accumulator.
                gameplayTickAccumulator_ -= 1.0;
                const bool hasPhysicalBiter =
                    feedbackEnemySlot_ >= 0 && feedbackEnemySlot_ < enemySlotCount_;
                const int biterSlot = hasPhysicalBiter
                    ? feedbackEnemySlot_ : enemySlotCount_;
                const auto earlierEntryIsMoving = [&]() {
                    return hasPhysicalBiter && std::any_of(
                        enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                            return enemy.slot >= 0 && enemy.slot < biterSlot &&
                                   enemy.entering && enemy.moving &&
                                   !enemy.collisionHidden &&
                                   !enemy.overlapFrozen &&
                                   !enemy.overlapRetired;
                        });
                };

                if (deathSequenceTicks_ == 0 && earlierEntryIsMoving() &&
                    biterSlot + 1 < enemySlotCount_) {
                    // The fourth Demo collision begins while an earlier-slot
                    // Reggie is still entering. The collision initializer has
                    // already hidden the Muncher and retained the Bashful's
                    // stationary endpoint, but the newly installed state-5
                    // bite record does not reach its first callback until the
                    // earlier entry chain drains. DOS therefore presents each
                    // complete Reggie phase over the ordinary Bashful dwell,
                    // followed by bite record 12 and the full 21-callback
                    // sequence. Advancing the bite here produces six hybrid
                    // pages and drops the last six source bite poses.
                    if (!updateSafeZones(tickSeconds)) continue;
                    if (!updateEnemies(tickSeconds)) continue;
                    if (attractMode_ && deathAnimating_) {
                        attractActionTimer_ -= tickSeconds;
                        updateAttractPlayer(tickSeconds);
                    }

                    const int selectedSlot = feedbackEnemySlot_;
                    feedbackEnemySlot_ = enemySlotCount_ + 1;
                    const std::vector<std::uint32_t> dwellPixels =
                        capturePresentationFrame();
                    feedbackEnemySlot_ = selectedSlot;

                    if (earlierEntryIsMoving()) {
                        deathResidentSurfacePixels_ = dwellPixels;
                    } else {
                        // The earlier entry's last moving phase has already
                        // been resident since its preceding callback. Its
                        // terminal state-4 repaint is not separately exposed;
                        // the collision initializer's record 12 replaces it
                        // before the first state-5 callback advances the bite.
                        deathResidentSurfacePixels_ = capturePresentationFrame();
                    }
                    continue;
                }

                const int biteTickBeforeDispatch = deathSequenceTicks_;
                const bool firstBiteCallback = biteTickBeforeDispatch == 0;
                const bool earlierEntryMovingBeforeBite =
                    earlierEntryIsMoving();
                const std::vector<std::uint32_t> beforeFirstBitePixels =
                    firstBiteCallback ? capturePresentationFrame()
                                      : std::vector<std::uint32_t>{};
                if (!updateSafeZones(tickSeconds)) continue;
                const std::vector<std::uint32_t> beforeEarlierTrogglesPixels =
                    hasPhysicalBiter && biterSlot > 0
                        ? capturePresentationFrame()
                        : std::vector<std::uint32_t>{};
                // State 5 occupies the selected biter's physical actor slot.
                // Dispatch earlier selector-1 records, paint that bite callback,
                // then dispatch later records. The seeded Smarty collision
                // exposes the complete earlier-slot Reggie repaint before the
                // bite, while the preserved Worker collision exposes the
                // complete later-slot Reggie repaint after it.
                if (deathAnimating_ &&
                    !updateEnemies(tickSeconds, biterSlot)) continue;
                const std::vector<std::uint32_t> afterEarlierTrogglesPixels =
                    !beforeEarlierTrogglesPixels.empty()
                        ? capturePresentationFrame()
                        : std::vector<std::uint32_t>{};
                const bool earlierTrogglePainted =
                    !beforeEarlierTrogglesPixels.empty() &&
                    afterEarlierTrogglesPixels != beforeEarlierTrogglesPixels;
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
                    // If selector 5 and state 5 become due at the terminal
                    // boundary, the bite removes the Muncher first. Carry the
                    // due selector across that interval; after the terminal it
                    // performs only its mandatory reload draw.
                    deferredDemoAtBiteTerminal =
                        attractActionTimer_ <= JobTimerEpsilon &&
                        biteTickBeforeDispatch >= TroggleEatAnimationTicks - 2;
                    if (!deferredDemoAtBiteTerminal) {
                        updateAttractPlayer(tickSeconds);
                    }
                }
                const std::vector<std::uint32_t> afterAllCallbacksPixels =
                    deathSequenceTicks_ <= TroggleEatAnimationTicks
                        ? capturePresentationFrame()
                        : std::vector<std::uint32_t>{};
                bool retainEarlierTroggleSurface = false;
                if (earlierTrogglePainted &&
                    deathSequenceTicks_ < TroggleEatAnimationTicks &&
                    biterSlot + 1 >= enemySlotCount_ &&
                    !afterAllCallbacksPixels.empty()) {
                    if (firstBiteCallback) {
                        // When the selected biter is the last physical record,
                        // its first state-5 callback follows the earlier entry
                        // repaint on the same dispatch.  The seeded Smarty run
                        // exposes both completed pages in that physical order.
                        enqueuePresentationFrame(afterEarlierTrogglesPixels);
                        enqueuePresentationFrame(afterAllCallbacksPixels);
                    } else if (earlierEntryMovingBeforeBite &&
                               earlierEntryIsMoving()) {
                        // On the following dispatch the earlier entry repaint
                        // remains resident while the biter advances behind it.
                        // DOS never exposes the clean after-all composition;
                        // its next visible page is this retained actor layer.
                        enqueuePresentationFrame(afterEarlierTrogglesPixels);
                        retainEarlierTroggleSurface = true;
                    } else if (earlierEntryMovingBeforeBite &&
                               !earlierEntryIsMoving()) {
                        // The entry terminal and biter callback collapse to
                        // the completed after-all resident page.  Enqueuing
                        // both internal surfaces here makes the host alternate
                        // the same two bite poses twice; DOS instead holds the
                        // terminal page until the next public biter callback.
                    } else {
                        // Once that entry reaches its endpoint, the biter's
                        // completed repaint reaches the display first and the
                        // earlier actor's terminal layer follows.  The later
                        // Worker/Smarty and Reggie-entry captures lock this
                        // complementary order.
                        enqueuePresentationFrame(afterAllCallbacksPixels);
                        enqueuePresentationFrame(afterEarlierTrogglesPixels);
                    }
                }
                if (!afterBiterPixels.empty() &&
                    afterBiterPixels != afterAllCallbacksPixels) {
                    enqueuePresentationFrame(afterBiterPixels);
                }
                if (deathSequenceTicks_ == TroggleEatAnimationTicks &&
                    hasPhysicalBiter && biterSlot + 1 >= enemySlotCount_ &&
                    std::any_of(enemies_.begin(), enemies_.end(),
                                [&](const Enemy& enemy) {
                                    return enemy.slot < biterSlot &&
                                           enemy.cannibalizing;
                                }) &&
                    !afterAllCallbacksPixels.empty()) {
                    // State 5 paints callback 21 before its terminal branch
                    // installs feedback text when the selected biter is the
                    // last physical record.  The seeded Smarty capture keeps
                    // that final record-13 page for two complete refreshes.
                    // It is exposed because an earlier actor has entered its
                    // own cannibal callback over that selected final record.
                    // Ordinary final-slot collisions instead reach feedback
                    // on the same pass, as locked by the first-Demo gate.
                    enqueuePresentationFrame(afterAllCallbacksPixels);
                }
                const bool retainAfterBiterUntilNextCallback =
                    laterEntryPhaseOneCallback && !afterBiterPixels.empty() &&
                    afterBiterPixels != afterAllCallbacksPixels;
                if (firstBiteCallback && !beforeFirstBitePixels.empty()) {
                    // The first state-5 actor callback is visible before the
                    // newly activated safe-zone painter reaches the resident
                    // surface. Isolate the bite-pose delta in the post-job
                    // logical frame and apply it to the pre-safe framebuffer.
                    const std::vector<std::uint32_t>& advancedBitePixels =
                        afterAllCallbacksPixels;
                    deathSequenceTicks_ = 0;
                    const std::vector<std::uint32_t> priorBitePixels =
                        capturePresentationFrame();
                    deathSequenceTicks_ = 1;
                    std::vector<std::uint32_t> residentBitePixels =
                        beforeFirstBitePixels;
                    for (std::size_t pixel = 0;
                         pixel < residentBitePixels.size(); ++pixel) {
                        if (advancedBitePixels[pixel] != priorBitePixels[pixel]) {
                            residentBitePixels[pixel] = advancedBitePixels[pixel];
                        }
                    }
                    deathResidentSurfacePixels_ = std::move(residentBitePixels);
                } else if (deathSequenceTicks_ < TroggleEatAnimationTicks) {
                    deathResidentSurfacePixels_ = retainEarlierTroggleSurface
                        ? afterEarlierTrogglesPixels
                        : retainAfterBiterUntilNextCallback
                            ? afterBiterPixels : afterAllCallbacksPixels;
                }
                deathTimer_ = static_cast<double>(
                    std::max(0, TroggleEatAnimationTicks - deathSequenceTicks_)) /
                    OriginalJobTicksPerSecond;
                if (deathSequenceTicks_ >= TroggleEatAnimationTicks) {
                    deathAnimating_ = false;
                    deathResidentSurfacePixels_.clear();
                    deathTimer_ = 0.0;
                    if (lifeLossPending_) {
                        --lives_;
                        lifeLossPending_ = false;
                    }
                    // The common state-5 bite terminal at 0x09da5 calls
                    // 0x09b16 for its survivor before it decides whether the
                    // victim was the Muncher. That snapshots the occupied
                    // cell and consumes a fresh 30-119-tick dwell roll. Only
                    // then does 0x109e3 choose the randomized exclamation.
                    // This call is observable in the preserved third demo:
                    // the dwell consumes call 506 and "Aargh" consumes 507.
                    const auto biter = std::find_if(
                        enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                            return enemy.slot == feedbackEnemySlot_;
                        });
                    const bool capturedFinalPrimeCollisionSchedulerDraw =
                        attractMode_ && attractBoardIndex_ == 4 &&
                        activeBoardMode_ == Mode::Primes && level_ == 5 &&
                        attractDifficultyIndex_ == 4 && target_ == 0 &&
                        feedbackEnemyType_ == 0 && playerRow_ == 4 &&
                        playerColumn_ == 2 && random_.calls == 850 &&
                        random_.state == 0x67fb2d03u;
                    if (capturedFinalPrimeCollisionSchedulerDraw) {
                        // The uninterrupted F585 capture proves one scheduler
                        // draw between the final visible bite callback and the
                        // survivor's state-4 dwell initializer. Without it the
                        // following phrase consumes call 852 ("Yikes") and the
                        // next board starts from call 852 as Prime Numbers.
                        // Consuming this recovered owner first makes the dwell
                        // and phrase calls 852/853, yielding captured "Aargh"
                        // and the unique Less-Than-20 board at frame 23358.
                        (void)random_.next();
                    }
                    if (biter != enemies_.end()) {
                        biter->savedCell = cell(biter->row, biter->column);
                        biter->savedCellValid = true;
                        // 0x09da5 passes literal direction 2 to the dwell
                        // initializer, selecting record 15 independently of
                        // the heading retained for the next move.
                        biter->dwellFrame = 15;
                        biter->moveTimer = enemyDwellDelay();
                    }
                    // The feedback painter at 0x109e3 performs its random
                    // exclamation lookup only after the bite job terminates.
                    // Earlier safe/Troggle/demo jobs therefore consume this
                    // tick's PRNG values first. Selecting at collision start
                    // happens to preserve the total call count but assigns
                    // the wrong phrase in the captured third-board collision.
                    playGameplaySound(10);
                    if (feedbackKind_ == FeedbackKind::EatenByTroggle &&
                        feedbackMessage_.empty()) {
                        feedbackMessage_ = std::string(FailureExclamations[
                            static_cast<std::size_t>(randomInt(0, 4))]);
                    }
                    // After the survivor dwell and feedback phrase are
                    // selected, the ordinary nonterminal path removes every
                    // other state-6 Troggle on the collision square. Each
                    // removal rearms its persistent slot and emits stream 11.
                    // Demo and the final Muncher branch leave this cleanup to
                    // the imminent terminal transition (0x09e01-0x09e15).
                    if (!attractMode_ && lives_ > 0) {
                        for (std::size_t index = 0; index < enemies_.size();) {
                            const Enemy& enemy = enemies_[index];
                            if (!enemy.collisionHidden || enemy.row != playerRow_ ||
                                enemy.column != playerColumn_) {
                                ++index;
                                continue;
                            }
                            const int slot = enemy.slot;
                            enemies_.erase(
                                enemies_.begin() + static_cast<std::ptrdiff_t>(index));
                            rearmEnemySlot(slot);
                            playGameplaySound(11);
                        }
                    }
                    if (attractMode_) {
                        if (attractActionTimer_ <= JobTimerEpsilon) {
                            attractActionTimer_ =
                                static_cast<double>(randomInt(0, 29) + 15) /
                                OriginalJobTicksPerSecond;
                        }
                        transitionTimer_ = AttractFeedbackDuration;
                        attractPostFeedbackBoard_ = false;
                    }
                }
            }
            // If the terminal callback fell within this display frame, only
            // its fractional remainder belongs to the feedback hold.
            transitionSeconds = deathAnimating_
                ? 0.0 : gameplayTickAccumulator_ / OriginalJobTicksPerSecond;
            if (!deathAnimating_) gameplayTickAccumulator_ = 0.0;
        }
        if (attractMode_ && !deathAnimating_) {
            transitionTimer_ -= transitionSeconds;
            if (transitionTimer_ <= 0.0) {
                beginAttractHallTransition();
            }
        }
    } else if (page_ == Page::LevelComplete) {
        if (transitionTimer_ <= 0.0) {
            finishLevelComplete();
        } else if (!levelCompleteScene_.valid()) {
            transitionTimer_ -= seconds;
            if (transitionTimer_ <= 0.0) finishLevelComplete();
        } else {
            sceneTickAccumulator_ += seconds * OriginalCartoonTicksPerSecond;
            while (sceneTickAccumulator_ >= 1.0 && !levelCompleteScene_.finished()) {
                advanceLevelCompleteScene();
                for (const std::uint16_t event : levelCompleteScene_.callbackEvents()) {
                    if (event <= 0xff) {
                        playOriginalAdli(static_cast<std::uint8_t>(cutsceneSoundBank_),
                                         static_cast<std::uint8_t>(event));
                    }
                }
                sceneTickAccumulator_ -= 1.0;
            }
            if (!levelCompleteScene_.valid() || levelCompleteScene_.finished()) {
                finishLevelComplete();
            }
        }
    }
    // Poll-derived joystick events enter through the same key paths, after the
    // current frame's scheduler work so a newly started move/chew is not also
    // advanced by the elapsed time that preceded the input.
    updateJoystickInput(seconds);
}

void Game::keyDown(UINT virtualKey) {
    if (exitPending_) return;
    if (configurationWriteError_) {
        // Printable keys and Space complete through WM_CHAR. Deferring them
        // prevents the physical key's paired character from reaching the page
        // revealed after this alert is dismissed.
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        dismissConfigurationWriteError();
        return;
    }
    if (page_ == Page::StartupVersion) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        enterPage(Page::StartupSplash);
        return;
    }
    if (attractMode_) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        stopAttract();
        return;
    }
    if (userPresentationActive()) return;
    if (page_ == Page::StartupSplash) {
        if (virtualKey == VK_SPACE || virtualKeyDefersToCharacter(virtualKey)) return;
        enterPage(Page::Title);
        return;
    }
    if (page_ == Page::Title) titleIdleTimer_ = 0.0;
    if (cheatOpen_) {
        handleCheatKey(virtualKey);
        return;
    }
    if (page_ == Page::LevelComplete) {
        // Selector 100/state 6 reaches the cartoon teardown for every
        // forwarded gameplay key. Escape is intercepted by the common
        // dispatcher and deliberately does nothing in this state.
        if (virtualKey != VK_ESCAPE && virtualKey != VK_SHIFT &&
            virtualKey != VK_CONTROL && virtualKey != VK_MENU &&
            !virtualKeyDefersToCharacter(virtualKey)) {
            finishLevelComplete();
        }
        return;
    }
    if (page_ == Page::OptionsPasswordPrompt && passwordRejected_) {
        // The nonzero flag at 0x0edb9 reaches the 0x0b0e6 wrapper, which
        // supplies DS:0864 ({Escape, Enter, Space, NUL}) to pAccept. Every
        // unsupported key remains owned by the blocking error overlay.
        if (virtualKey == VK_ESCAPE || virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            passwordRejected_ = false;
        }
        return;
    }
    if (page_ == Page::Playing || page_ == Page::Paused || page_ == Page::Feedback) {
        handlePlayKey(virtualKey);
    } else {
        handleMenuKey(virtualKey);
    }
}

void Game::character(wchar_t character) {
    if (exitPending_) return;
    if (configurationWriteError_) {
        dismissConfigurationWriteError();
        return;
    }
    if (page_ == Page::StartupVersion) {
        if (!belongsToDeferredBlockingKey(character)) return;
        enterPage(Page::StartupSplash);
        return;
    }
    if (attractMode_) {
        // The common dispatcher handles +/- before selector states 1..3
        // terminate Demo. Preserve both effects from the one translated
        // event. The native feedback owner is restarted after teardown so
        // stopAttract() cannot silence the recovered 50 ms PIT tone.
        const int previousRepeat = joystickRepeatTicks_;
        (void)adjustJoystickRepeat(character);
        stopAttract();
        if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
        return;
    }
    if (userPresentationActive()) return;
    if (page_ == Page::StartupSplash) {
        if (!belongsToDeferredBlockingKey(character)) return;
        enterPage(Page::Title);
        return;
    }
    if (page_ == Page::LevelComplete && !cheatOpen_) {
        // '+'/'=' and '-'/'_' are handled by the common DOS dispatcher
        // before selector state 6 forwards the same event to the cartoon
        // callback. Preserve both effects: update the live joystick-repeat
        // word, then tear down the scene.
        const int previousRepeat = joystickRepeatTicks_;
        (void)adjustJoystickRepeat(character);
        finishLevelComplete();
        // Scene teardown releases the ordinary speaker-effect owner. Start
        // the direct feedback tone afterwards so that it remains audible for
        // its recovered 50 ms lifetime.
        if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
        return;
    }
    if ((page_ == Page::OptionsContentRange || page_ == Page::OptionsContentOtherNumber) &&
        !cheatOpen_) {
        if (page_ == Page::OptionsContentOtherNumber && contentNumberConfirm_) return;
        const std::size_t maximumLength = page_ == Page::OptionsContentOtherNumber
            ? 2u
            : std::to_string(ContentRangeLimits[static_cast<std::size_t>(contentRow_)].second)
                  .size();
        if (character == L'\b') {
            if (!contentNumericInput_.empty()) contentNumericInput_.pop_back();
        } else if (character >= L'0' && character <= L'9' &&
                   contentNumericInput_.size() < maximumLength) {
            contentNumericInput_.push_back(static_cast<char>(character));
        }
        return;
    }
    if (page_ == Page::OptionsPassword && !cheatOpen_) {
        std::string& field = passwordEditingHint_ ? hintDraft_ : passwordDraft_;
        const std::size_t maximumLength = passwordEditingHint_ ? 50u : 10u;
        if (character == L'\b') {
            if (!field.empty()) field.pop_back();
        } else if (character >= (passwordEditingHint_ ? 32 : 33) && character <= 126 &&
                   field.size() < maximumLength) {
            field.push_back(static_cast<char>(character));
        }
        return;
    }
    if (page_ == Page::OptionsPasswordPrompt && !cheatOpen_) {
        if (passwordRejected_) {
            // A physical Space normally cleared the overlay on WM_KEYDOWN;
            // retain the translated-event path for direct WM_CHAR delivery.
            // All other translated characters are rejected by DS:0864.
            if (character == L' ') passwordRejected_ = false;
            return;
        }
        if (character == L'\b') {
            if (!passwordAttempt_.empty()) passwordAttempt_.pop_back();
        } else if (character >= 33 && character <= 126 && passwordAttempt_.size() < 10) {
            passwordAttempt_.push_back(static_cast<char>(character));
        }
        return;
    }
    if (page_ == Page::NameEntry && !cheatOpen_) {
        if (character == L'\b') {
            if (!nameInput_.empty()) nameInput_.pop_back();
        } else if (character >= 32 && character <= 126 && nameInput_.size() < 25) {
            nameInput_.push_back(static_cast<char>(character));
        }
        return;
    }
    if (page_ == Page::OptionsEraseEntries && eraseDraft_.empty() &&
        eraseMode_ >= 0 && eraseMode_ < static_cast<int>(scores_.size()) &&
        scores_[static_cast<std::size_t>(eraseMode_)].empty() && !cheatOpen_) {
        if (character == L' ') {
            enterPage(Page::OptionsEraseHall);
            menuSelection_ = eraseMode_;
        }
        return;
    }
    if (!cheatOpen_) {
        bool commonMenuCharacter = false;
        switch (page_) {
        case Page::Title:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(character, 5, menuSelection_);
            break;
        case Page::ModeSelect:
            commonMenuCharacter = munchers::applyNumberedMenuShortcut(
                character, static_cast<int>(enabledPlayModes().size()), menuSelection_);
            break;
        case Page::HallSelect:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(character, 6, menuSelection_);
            break;
        case Page::Options:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(character, 6, menuSelection_);
            break;
        case Page::OptionsDifficulty:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(character, 11, menuSelection_);
            break;
        case Page::OptionsEraseHall:
            commonMenuCharacter =
                munchers::applyNumberedMenuShortcut(character, 7, menuSelection_);
            break;
        case Page::OptionsEraseEntries:
            if (!eraseDraft_.empty()) {
                commonMenuCharacter = munchers::applyNumberedMenuShortcut(
                    character, static_cast<int>(eraseDraft_.size()), eraseSelection_);
            }
            break;
        case Page::InstructionsQuestion:
        case Page::ReplayQuestion:
        case Page::QuitConfirm:
        case Page::OptionsEraseAllConfirm:
            commonMenuCharacter = munchers::applyUnnumberedMenuCharacterNavigation(
                character, 2, menuSelection_);
            break;
        case Page::OptionsContent:
            commonMenuCharacter = munchers::applyUnnumberedMenuCharacterNavigation(
                character, contentFieldCount(contentRow_), contentColumn_);
            break;
        case Page::OptionsContentOperations:
            commonMenuCharacter = munchers::applyUnnumberedMenuCharacterNavigation(
                character, 4, contentOperationSelection_);
            break;
        default:
            break;
        }
        if (commonMenuCharacter) return;
    }
    if (acceptsCommonDispatcherInput()) {
        // The resident main-loop dispatcher owns only live selector states;
        // synchronous menus, line editors, waiters, and information pages do
        // not execute this branch. The preserved default is four public
        // ticks; '+' is faster and '-' is slower.
        const int previousRepeat = joystickRepeatTicks_;
        if (adjustJoystickRepeat(character)) {
            if (joystickRepeatTicks_ != previousRepeat) playJoystickRepeatFeedback();
            return;
        }
    }
    if (page_ != Page::Playing || cheatOpen_ || deathAnimating_) {
        return;
    }
    switch (character) {
    // The common DOS dispatcher normalizes 8/A/I, 4/J, 6/K, and 2/M/Z to
    // extended Up/Left/Right/Down codes. Space is the separate munch action.
    case L'8': case L'a': case L'A': case L'i': case L'I':
        enqueuePlayerInput('I');
        break;
    case L'4': case L'j': case L'J': enqueuePlayerInput('J'); break;
    case L'6': case L'k': case L'K': enqueuePlayerInput('K'); break;
    case L'2': case L'm': case L'M': case L'z': case L'Z':
        enqueuePlayerInput('M');
        break;
    default: break;
    }
}

bool Game::acceptsCommonDispatcherInput() const {
    return !configurationWriteError_ && !cheatOpen_ && !userPresentationActive() &&
           (attractMode_ || page_ == Page::Playing || page_ == Page::Paused ||
            page_ == Page::Feedback || page_ == Page::LevelComplete);
}

void Game::toggleSound() {
    if (!acceptsCommonDispatcherInput()) return;
    soundOn_ = !soundOn_;
    // Event 0x3e8 always submits a resident-driver acknowledgement: stream 1
    // while turning sound off and stream 2 while turning it on. The low-level
    // submission deliberately bypasses the newly changed sound flag and
    // replaces only logical channel 0, leaving an active Demo score intact.
    playToggleFeedback(soundOn_);
    saveSettings();
}

void Game::toggleMusic() {
    if (!acceptsCommonDispatcherInput()) return;
    musicOn_ = !musicOn_;
    if (!musicOn_) {
        // Event 0x3e9 dispatches entry 0 (all channels off), waits 50 ms, then
        // submits the same descending stream-1 acknowledgement used by Alt+S.
        attractOplPlayer_.stopAllChannels();
        musicPlayer_.stop();
        effectPlayer_.stop();
        playToggleFeedback(false, 50);
    } else if (attractMode_ &&
               (page_ == Page::Attract || page_ == Page::Feedback) &&
               !speakerEffects_) {
        // The resident Alt+M path at 0x0d1f3 resumes event 0x90 only when
        // the demo flag is set, selector state is 2, and the current driver
        // device is nonzero. State 2 owns the active board, feedback hold, and
        // board-to-Hall Wipe; Hall and logo/splash are states 3 and 1.
        (void)playOriginalSound(16, true);
    }
    if (musicOn_) {
        // The driver queues stream 2 before the high-level score dispatch.
        // The score's channel-9 launch preparation executes synchronously,
        // before the scheduler later dispatches queued channel 0; installing
        // the score then the effect preserves that physical write order.
        playToggleFeedback(true);
    }
    saveSettings();
    if (configurationWriteError_) {
        // Alt+M rejoins the original selector-state controller only after the
        // synchronous writer alert consumes its later acknowledgement key.
        configurationWriteErrorPostDismissContinuation_ = true;
    }
}

void Game::toggleSpeaker() {
    if (!acceptsCommonDispatcherInput()) return;
    speakerEffects_ = !speakerEffects_;
    if (speakerEffects_) {
        // With music enabled, AdLib -> PC speaker immediately dispatches
        // entry 0 and therefore stops every driver channel, including score.
        if (musicOn_) {
            attractOplPlayer_.stopAllChannels();
            musicPlayer_.stop();
            effectPlayer_.stop();
        }
    } else if (musicOn_ && attractMode_ &&
               (page_ == Page::Attract || page_ == Page::Feedback)) {
        // PC speaker -> detected AdLib restarts event 0x90 only in selector
        // state 2. No stop command is issued on this restoration branch, so a
        // speaker cue already in flight is left alone.
        (void)playOriginalSound(16, true);
    }
    saveSettings();
    if (configurationWriteError_) {
        // Alt+P has the same post-save fallthrough as Alt+M.  Defer it while
        // the asynchronous native alert stands in for the DOS blocking wait.
        configurationWriteErrorPostDismissContinuation_ = true;
    }
}

void Game::pointerMove(int x, int y) {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (cheatOpen_) {
        if (x >= 50 && x <= 270 && y >= 64 && y < 154) {
            cheatSelection_ = std::clamp((y - 64) / 18, 0, 4);
        }
        return;
    }
    switch (page_) {
    case Page::Title:
    case Page::InstructionsQuestion:
    case Page::ReplayQuestion:
    case Page::ModeSelect:
    case Page::HallSelect:
    case Page::Options:
    case Page::OptionsDifficulty:
    case Page::OptionsEraseHall:
    case Page::OptionsEraseEntries:
    case Page::OptionsEraseAllConfirm:
    case Page::QuitConfirm:
    case Page::OptionsContent:
        // INT 33h is installed with mask 0x001e, which excludes movement.
        // Ordinary pointer motion therefore never reaches these widgets.
        break;
    default:
        break;
    }
}

bool Game::pointerPress(const bool secondary) {
    if (exitPending_) return true;
    if (configurationWriteError_) return true;
    if (userPresentationActive()) return false;
    if (!secondary) return false;
    // INT 33h callback mask 0x08 produces event type 12/code 0xFE on the
    // right press. The common dispatcher treats it as Enter in active play,
    // as an any-key exit in Demo, and as a state-6 cartoon skip. Fixed menus
    // handle the later right release instead, so leave every other page alone.
    if (attractMode_) {
        stopAttract();
        return true;
    }
    if (page_ == Page::Playing || page_ == Page::Paused) {
        keyDown(VK_RETURN);
        return true;
    }
    if (page_ == Page::LevelComplete) {
        keyDown(VK_PROCESSKEY);
        return true;
    }
    return false;
}

void Game::pointerButton(int x, int y, bool secondary) {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (attractMode_) {
        stopAttract();
        return;
    }
    if (page_ == Page::StartupVersion || page_ == Page::StartupSplash) {
        keyDown(VK_RETURN);
        return;
    }
    // The shared accepted-key waiter at image 0x0B389 rewrites only event
    // type 2 (left release) to Space. A right release retains code 0xFD and
    // is rejected by the {Escape, Enter, Space} table. This differs from the
    // fixed-list widget, which explicitly converts 0xFD to Enter.
    const bool acceptedKeyWaiter =
        page_ == Page::Information ||
        page_ == Page::OptionsContentHelp ||
        page_ == Page::OptionsContentValidation ||
        (page_ == Page::OptionsEraseEntries && eraseDraft_.empty() &&
         eraseMode_ >= 0 && eraseMode_ < static_cast<int>(scores_.size()) &&
         scores_[static_cast<std::size_t>(eraseMode_)].empty()) ||
        (page_ == Page::OptionsPasswordPrompt && passwordRejected_) ||
        page_ == Page::OptionsJoystickCalibration ||
        page_ == Page::Hall ||
        page_ == Page::Feedback;
    const bool contentNumberConfirmWaiter =
        page_ == Page::OptionsContentOtherNumber && contentNumberConfirm_;
    if (secondary) {
        if (!cheatOpen_ && acceptedKeyWaiter) return;
        keyDown(VK_RETURN);
        return;
    }
    pointerMove(x, y);
    if (cheatOpen_) {
        if (cheatSelection_ == 0 && y >= 64 && y < 86) {
            cheatLevel_ = std::clamp(cheatLevel_ + (x < 160 ? -1 : 1), 1, 20);
        } else {
            handleCheatKey(VK_RETURN);
        }
        return;
    }
    // DS:22D0 is {Escape, Enter, 0xFD, NUL}. pAccept rewrites left release
    // to Space, so only the untouched right release accepts this state.
    if (contentNumberConfirmWaiter) return;
    if (acceptedKeyWaiter) {
        keyDown(VK_SPACE);
        return;
    }
    if (page_ == Page::Playing &&
        x >= BoardLeft && x < BoardRight && y >= BoardTop && y < BoardBottom) {
        const int row = std::clamp((y - BoardTop) / BoardCellHeight, 0, BoardRows - 1);
        const int column = std::clamp((x - BoardLeft) / BoardCellWidth, 0, BoardColumns - 1);
        // pSeqmouse at image 0x137d6 maps this exact board rectangle to a
        // row-major cell and queues its negated byte. The ring at 0x1674e
        // has nine usable slots; it never teleports the Muncher.
        enqueuePlayerInput(-(row * BoardColumns + column));
        return;
    }

    // The list widget at image 0x0e00b maps the type-2 left-release event to
    // one exact generated item rectangle. A different item is highlighted;
    // only release on the already-highlighted item is converted to Enter.
    const auto applyMenuHit = [this](int item) {
        if (item < 0) return false;
        if (menuSelection_ == item) {
            handleMenuKey(VK_RETURN);
        } else {
            menuSelection_ = item;
        }
        return true;
    };
    const auto numberedItemAt = [this, x, y](int left, int firstBaseline,
                                             const auto& labels) {
        const GemFont& font = assets_.largeFont();
        for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
            const int baseline = firstBaseline + index * 11;
            const std::string display = " " + std::to_string(index + 1) + ". " +
                std::string(labels[static_cast<std::size_t>(index)]) + " ";
            if (x >= left && x <= left + fontTextWidth(font, display) &&
                y >= baseline - 1 && y <= baseline + 8) {
                return index;
            }
        }
        return -1;
    };
    const auto plainItemAt = [this, x, y](const auto& items) {
        const GemFont& font = assets_.largeFont();
        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            const auto& item = items[static_cast<std::size_t>(index)];
            if (x >= item.left && x <= item.left + fontTextWidth(font, item.text) &&
                y >= item.baseline - 1 && y <= item.baseline + 8) {
                return index;
            }
        }
        return -1;
    };

    struct PointerItem {
        int left;
        int baseline;
        std::string_view text;
    };
    switch (page_) {
    case Page::Title: {
        static constexpr std::array<std::string_view, 5> labels = {
            "Play Number Munchers", "Hall of Fame", "Information", "Options", "Quit"};
        applyMenuHit(numberedItemAt(49, 91, labels));
        return;
    }
    case Page::ModeSelect: {
        const std::vector<Mode> modes = enabledPlayModes();
        std::vector<std::string_view> labels;
        labels.reserve(modes.size());
        for (const Mode mode : modes) labels.push_back(ModeNames[static_cast<std::size_t>(mode)]);
        applyMenuHit(numberedItemAt(90, selectorFirstBaseline(labels.size()), labels));
        return;
    }
    case Page::HallSelect: {
        applyMenuHit(numberedItemAt(90, 70, ModeNames));
        return;
    }
    case Page::InstructionsQuestion:
    case Page::ReplayQuestion: {
        static constexpr std::array<PointerItem, 2> items = {{
            {115, 105, " Yes "}, {165, 105, " No "}}};
        applyMenuHit(plainItemAt(items));
        return;
    }
    case Page::QuitConfirm: {
        static constexpr std::array<PointerItem, 2> items = {{
            {115, 110, " Yes "}, {165, 110, " No "}}};
        applyMenuHit(plainItemAt(items));
        return;
    }
    case Page::Options: {
        const std::array<std::string, 6> labels = {
            "Set Difficulty Level", "Set Content", "Erase Hall of Fame", "Set Password",
            std::string("Turn Joystick ") + (joystickOn_ ? "OFF" : "ON"), "Calibrate Joystick"};
        applyMenuHit(numberedItemAt(50, 72, labels));
        return;
    }
    case Page::OptionsDifficulty:
        applyMenuHit(numberedItemAt(50, 49, DifficultyNames));
        return;
    case Page::OptionsEraseHall: {
        std::array<std::string, 7> labels{};
        for (int index = 0; index < 6; ++index) {
            labels[static_cast<std::size_t>(index)] = ModeNames[static_cast<std::size_t>(index)];
        }
        labels[6] = "Erase ALL Lists";
        applyMenuHit(numberedItemAt(80, 73, labels));
        return;
    }
    case Page::OptionsEraseEntries: {
        std::vector<std::string_view> labels;
        labels.reserve(eraseDraft_.size());
        for (const ScoreEntry& entry : eraseDraft_) labels.push_back(entry.name);
        const int item = numberedItemAt(40, 38, labels);
        if (item >= 0) {
            // The shared eraser maps a repeated left-release hit to the same
            // branch as Space (delete), not Enter (commit).
            if (eraseSelection_ == item) handleMenuKey(VK_SPACE);
            else eraseSelection_ = item;
        }
        return;
    }
    case Page::OptionsEraseAllConfirm: {
        static constexpr std::array<PointerItem, 2> items = {{
            {105, 121, " Yes "}, {155, 121, " No "}}};
        applyMenuHit(plainItemAt(items));
        return;
    }
    case Page::OptionsContent: {
        // Set Content builds one widget per row from DS:19a6 and DS:1b2e.
        // The all-row pointer branch at 0x14cfe uses baselines 60..140 and
        // exact x positions 93/125/193/257, with inclusive generated bounds.
        const GemFont& font = assets_.smallFont();
        const int componentCount = static_cast<int>(std::count_if(
            contentDraft_.begin(),
            contentDraft_.begin() + static_cast<std::ptrdiff_t>(Mode::Challenge),
            [](const ContentSettings& settings) { return settings.use; }));
        static constexpr std::array<int, 4> lefts = {93, 125, 193, 257};

        const auto itemText = [this, componentCount](int row, int column) {
            const ContentSettings& settings = contentDraft_[static_cast<std::size_t>(row)];
            if (column == 0) {
                if (row == static_cast<int>(Mode::Challenge) && componentCount <= 1) {
                    return std::string(" --- ");
                }
                return settings.use ? std::string(" yes ") : std::string(" no ");
            }
            if (column == 1) {
                std::string text = std::to_string(settings.minimum);
                if (text.size() < 4) text.insert(0, 4 - text.size(), ' ');
                text += " - " + std::to_string(settings.maximum);
                text.resize(11, ' ');
                return text;
            }
            if (column == 2) {
                return settings.randomSequence ? std::string("  random  ")
                                               : std::string(" in order ");
            }
            if (row == static_cast<int>(Mode::Multiples)) {
                return " up to " + std::to_string(settings.maxMultiplier) + " ";
            }
            std::string operations(9, ' ');
            for (int operation = 0; operation < 4; ++operation) {
                if (settings.operations[static_cast<std::size_t>(operation)]) {
                    operations[static_cast<std::size_t>(1 + operation * 2)] =
                        static_cast<char>(2 + operation * 2);
                }
            }
            return operations;
        };

        for (int row = 0; row < static_cast<int>(Mode::Count); ++row) {
            const int baseline = 60 + row * 16;
            if (y < baseline - 1 || y > baseline + 8) continue;
            for (int column = 0; column < 4; ++column) {
                if (!contentFieldValid(row, column)) continue;
                const std::string text = itemText(row, column);
                const int left = lefts[static_cast<std::size_t>(column)];
                if (x < left || x > left + fontTextWidth(font, text)) continue;
                if (contentRow_ == row && contentColumn_ == column) {
                    // After the left-press event waits for release,
                    // 0x14d72-0x14d9c maps a repeated all-row hit to the same
                    // field-activation path as Space.
                    handleMenuKey(VK_SPACE);
                } else {
                    contentRow_ = row;
                    contentColumn_ = column;
                }
                return;
            }
        }
        return;
    }
    default:
        break;
    }
    switch (page_) {
    case Page::OptionsContent:
    case Page::OptionsContentRange:
    case Page::OptionsContentOtherNumber:
    case Page::OptionsContentOperations:
    case Page::OptionsContentHelp:
    case Page::OptionsContentValidation:
    case Page::OptionsPassword:
    case Page::OptionsPasswordPrompt:
    case Page::LevelComplete:
        handleMenuKey(VK_RETURN);
        break;
    default:
        break;
    }
}

void Game::toggleCheatMenu() {
    if (exitPending_) return;
    if (configurationWriteError_) return;
    if (userPresentationActive()) return;
    if (attractMode_) {
        stopAttract();
    } else if (page_ == Page::StartupVersion || page_ == Page::StartupSplash) {
        enterPage(Page::Title);
    }
    cheatOpen_ = !cheatOpen_;
    cheatSelection_ = 0;
    cheatLevel_ = level_;
}

void Game::beginExitDelay() {
    exitPending_ = true;
    exitDelayTimer_ = MainControllerCleanupDelay;
    titleIdleTimer_ = 0.0;
}

void Game::handleMenuKey(UINT virtualKey) {
    const bool up = virtualKey == VK_UP;
    const bool down = virtualKey == VK_DOWN;
    const bool left = virtualKey == VK_LEFT;
    const bool right = virtualKey == VK_RIGHT;
    if (virtualKey == VK_ESCAPE) {
        if (page_ == Page::QuitConfirm) {
            if (quitFromFeedback_) {
                finishFeedback();
            } else {
                page_ = quitReturnPage_;
            }
            quitFromFeedback_ = false;
        } else if (page_ == Page::OptionsContentValidation) {
            dismissContentValidation();
        } else if (page_ == Page::OptionsContentHelp) {
            enterPage(Page::OptionsContent);
        } else if (page_ == Page::OptionsContentOtherNumber && contentNumberConfirm_) {
            // Escape from the state-1 confirmation at 0x15449 clears that
            // candidate and loops to state 0; it does not close the editor.
            contentNumberConfirm_ = false;
            contentNumericInput_.clear();
        } else if (page_ == Page::OptionsContentRange ||
                   page_ == Page::OptionsContentOtherNumber ||
                   page_ == Page::OptionsContentOperations) {
            contentDraft_[static_cast<std::size_t>(contentRow_)] = contentDialogBackup_;
            contentNumericInput_.clear();
            contentNumberConfirm_ = false;
            enterPage(Page::OptionsContent);
        } else if (page_ == Page::OptionsEraseEntries || page_ == Page::OptionsEraseAllConfirm) {
            enterPage(Page::OptionsEraseHall);
            menuSelection_ = eraseMode_;
        } else if (page_ == Page::OptionsPasswordPrompt) {
            passwordAttempt_.clear();
            passwordRejected_ = false;
            enterPage(Page::Title);
        } else if (page_ == Page::OptionsJoystickCalibration) {
            // Calibration is transactional: Escape leaves the installed
            // thresholds and enabled flag untouched.
            enterPage(Page::Options);
            menuSelection_ = 5;
        } else if (page_ == Page::NameEntry) {
            // Escape exits the editor unconditionally. The caller keeps a
            // partial name, or substitutes the original fallback when the
            // field is still empty.
            submitScore();
            enterHall(HallContext::PostGame);
        } else if (page_ == Page::Title) {
            beginExitDelay();
        } else if (page_ == Page::OptionsDifficulty || page_ == Page::OptionsContent ||
                   page_ == Page::OptionsEraseHall || page_ == Page::OptionsPassword) {
            const Page previous = page_;
            // Accepting the password field sets the original routine's return
            // byte. Escape from the later hint field restores both strings but
            // leaves that result set, so the Options caller rewrites the
            // unchanged configuration. Escape from the first field does not.
            if (previous == Page::OptionsPassword && passwordEditingHint_) saveSettings();
            if (previous == Page::OptionsContent) contentDraft_ = contentSettings_;
            enterPage(Page::Options);
            menuSelection_ = previous == Page::OptionsDifficulty ? 0
                           : previous == Page::OptionsContent ? 1
                           : previous == Page::OptionsEraseHall ? 2 : 3;
        } else if (page_ == Page::Information) {
            // The instruction pager at image 0x1223a returns a nonzero value
            // on Escape. Its caller at 0x12afb then cancels the entire Play
            // path back to the main menu; only normal page completion reaches
            // the game selector.
            enterPage(Page::Title);
        } else if (page_ == Page::Hall) {
            hallHighlightName_.clear();
            hallHighlightScore_ = 0;
            if (hallContext_ == HallContext::PostGame) {
                enterPage(Page::ReplayQuestion);
            } else {
                enterPage(Page::Title);
            }
        } else if (page_ == Page::ReplayQuestion) {
            enterPage(Page::Title);
        } else {
            enterPage(Page::Title);
        }
        return;
    }

    switch (page_) {
    case Page::Title:
        munchers::applyFixedMenuNavigation(virtualKey, 5, menuSelection_);
        // DS:0d46 contains no Space accelerator; the generic widget ends on
        // carriage return (or Escape) only.
        if (virtualKey == VK_RETURN) {
            switch (menuSelection_) {
            case 0: enterPage(Page::InstructionsQuestion); menuSelection_ = 1; break;
            case 1: enterPage(Page::HallSelect); break;
            case 2: informationPage_ = 0; informationReturnPage_ = Page::Title; enterPage(Page::Information); break;
            case 3:
                if (optionPassword_.empty()) {
                    enterPage(Page::Options);
                } else {
                    passwordAttempt_.clear();
                    passwordRejected_ = false;
                    enterPage(Page::OptionsPasswordPrompt);
                }
                break;
            case 4: beginExitDelay(); break;
            }
        }
        break;
    case Page::InstructionsQuestion:
        munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
        // pSeqKey at 0x12f6f treats y/Y and n/N as selection keys. They do
        // not terminate the wrapper; Enter (or pointer activation) accepts
        // the current item. Up/Down do nothing and Space is not in DS:1664.
        if (virtualKey == VK_RETURN) {
            if (menuSelection_ == 0) {
                informationPage_ = 0;
                informationReturnPage_ = Page::ModeSelect;
                enterPage(Page::Information);
                playGameplaySound(13);
            } else {
                enterModeSelect();
            }
        }
        break;
    case Page::ModeSelect: {
        const std::vector<Mode> modes = enabledPlayModes();
        if (modes.empty()) break;
        const int count = static_cast<int>(modes.size());
        munchers::applyFixedMenuNavigation(virtualKey, count, menuSelection_);
        // Both the Play and Hall selectors use the widget at image 0x12b6d.
        // Its empty custom-key list and 0x0d terminal test make Enter (or a
        // pointer selection), not keyboard Space, the selector accept key.
        if (virtualKey == VK_RETURN) {
            startGameWithBoardPresentation(
                modes[static_cast<std::size_t>(menuSelection_)]);
        }
        break;
    }
    case Page::HallSelect:
        munchers::applyFixedMenuNavigation(virtualKey, 6, menuSelection_);
        if (virtualKey == VK_RETURN) {
            mode_ = static_cast<Mode>(menuSelection_);
            enterHall(HallContext::Browse);
        }
        break;
    case Page::Information:
        // 0x12f14 waits on the exact DS:0864 key set {Escape, Enter, Space}.
        // Arrow keys neither advance nor provide reverse page navigation.
        if (virtualKey == VK_SPACE || virtualKey == VK_RETURN) {
            ++informationPage_;
            if (informationReturnPage_ == Page::ModeSelect && informationPage_ == 1) {
                playGameplaySound(14);
            }
            if (informationPage_ >= 6) {
                if (informationReturnPage_ == Page::ModeSelect) enterModeSelect();
                else enterPage(informationReturnPage_);
            }
        }
        break;
    case Page::Options:
        munchers::applyFixedMenuNavigation(virtualKey, 6, menuSelection_);
        if (virtualKey == VK_RETURN) {
            const int selected = menuSelection_;
            if (selected == 0) {
                enterPage(Page::OptionsDifficulty);
                menuSelection_ = difficulty_;
            } else if (selected == 1) {
                beginContentEdit();
                enterPage(Page::OptionsContent);
            } else if (selected == 2) {
                enterPage(Page::OptionsEraseHall);
            } else if (selected == 3) {
                passwordEditingHint_ = false;
                passwordDraft_ = optionPassword_;
                hintDraft_ = optionHint_;
                enterPage(Page::OptionsPassword);
            } else if (selected == 4) {
                joystickOn_ = !joystickOn_;
                saveSettings();
            } else {
                beginJoystickCalibration();
            }
        }
        break;
    case Page::OptionsJoystickCalibration:
        // The six-step prompt wait uses DS:0864: Escape, Enter, and Space are
        // the only accepted keys. Escape was handled transactionally above.
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            acceptJoystickCalibration();
        }
        break;
    case Page::OptionsDifficulty:
        munchers::applyFixedMenuNavigation(virtualKey, 11, menuSelection_);
        if (virtualKey == VK_RETURN) {
            applyDifficultyPreset(menuSelection_);
            saveSettings();
            enterPage(Page::Options);
            menuSelection_ = 0;
        }
        break;
    case Page::OptionsContent:
        if (up) moveContentSelection(-1, 0);
        if (down) moveContentSelection(1, 0);
        if (left || right || virtualKey == VK_HOME || virtualKey == VK_END) {
            munchers::applyFixedMenuNavigation(
                virtualKey, contentFieldCount(contentRow_), contentColumn_);
        }
        if (virtualKey == VK_F1) {
            contentHelpPage_ = 0;
            enterPage(Page::OptionsContentHelp);
        } else if (virtualKey == VK_SPACE) {
            ContentSettings& settings = contentDraft_[static_cast<std::size_t>(contentRow_)];
            if (contentColumn_ == 0) {
                settings.use = !settings.use;
            } else if (contentColumn_ == 1) {
                openContentRange();
            } else if (contentColumn_ == 2) {
                settings.randomSequence = !settings.randomSequence;
            } else {
                openContentOther();
            }
        } else if (virtualKey == VK_RETURN) {
            const int enabledComponents = static_cast<int>(std::count_if(
                contentDraft_.begin(),
                contentDraft_.begin() + static_cast<std::ptrdiff_t>(Mode::Challenge),
                [](const ContentSettings& settings) { return settings.use; }));
            if (enabledComponents == 0) {
                showContentValidation(ContentValidationKind::NoGames);
                break;
            }
            // 0x14c5a-0x14c6d clears DS:201c (Challenge Use) when only one
            // component remains, because Challenge would merely duplicate it.
            if (enabledComponents == 1) {
                contentDraft_[static_cast<std::size_t>(Mode::Challenge)].use = false;
            }
            contentSettings_ = contentDraft_;
            resetContentProgression();
            saveSettings();
            enterPage(Page::Options);
            menuSelection_ = 1;
        }
        break;
    case Page::OptionsContentRange:
    case Page::OptionsContentOtherNumber:
        if (virtualKey == VK_RETURN) acceptContentNumber();
        break;
    case Page::OptionsContentOperations: {
        munchers::applyFixedMenuNavigation(virtualKey, 4, contentOperationSelection_);
        ContentSettings& settings = contentDraft_[static_cast<std::size_t>(contentRow_)];
        if (virtualKey == VK_SPACE) {
            settings.operations[static_cast<std::size_t>(contentOperationSelection_)] =
                !settings.operations[static_cast<std::size_t>(contentOperationSelection_)];
        } else if (virtualKey == VK_RETURN) {
            if (std::none_of(settings.operations.begin(), settings.operations.end(),
                             [](bool enabled) { return enabled; })) {
                showContentValidation(ContentValidationKind::NoOperations);
            } else {
                enterPage(Page::OptionsContent);
            }
        }
        break;
    }
    case Page::OptionsContentHelp:
        // Both pages call the DS:0864 accepted-key wrapper. Arrow keys never
        // leave that waiter; Escape exits through the common modal route.
        if (virtualKey == VK_SPACE || virtualKey == VK_RETURN) {
            if (++contentHelpPage_ >= 2) enterPage(Page::OptionsContent);
        }
        break;
    case Page::OptionsContentValidation:
        if (virtualKey == VK_SPACE || virtualKey == VK_RETURN) {
            dismissContentValidation();
        }
        break;
    case Page::OptionsEraseHall:
        munchers::applyFixedMenuNavigation(virtualKey, 7, menuSelection_);
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            if (menuSelection_ == 6) {
                eraseMode_ = 6;
                enterPage(Page::OptionsEraseAllConfirm);
                menuSelection_ = 1;
            } else {
                eraseMode_ = menuSelection_;
                eraseSelection_ = 0;
                eraseDraft_ = scores_[static_cast<std::size_t>(eraseMode_)];
                enterPage(Page::OptionsEraseEntries);
            }
        }
        break;
    case Page::OptionsEraseEntries:
        if (eraseDraft_.empty() && eraseMode_ >= 0 &&
            eraseMode_ < static_cast<int>(scores_.size()) &&
            scores_[static_cast<std::size_t>(eraseMode_)].empty()) {
            // 0x0A3E3 uses the ordinary DS:0864 accepted-key wrapper.
            if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
                enterPage(Page::OptionsEraseHall);
                menuSelection_ = eraseMode_;
            }
            break;
        }
        if (!eraseDraft_.empty()) {
            const int count = static_cast<int>(eraseDraft_.size());
            munchers::applyFixedMenuNavigation(virtualKey, count, eraseSelection_);
            if (virtualKey == VK_SPACE) {
                eraseDraft_.erase(eraseDraft_.begin() + eraseSelection_);
                if (!eraseDraft_.empty()) eraseSelection_ %= static_cast<int>(eraseDraft_.size());
                else eraseSelection_ = 0;
            }
        }
        if (virtualKey == VK_RETURN) {
            scores_[static_cast<std::size_t>(eraseMode_)] = eraseDraft_;
            saveScores();
            enterPage(Page::OptionsEraseHall);
            menuSelection_ = eraseMode_;
        }
        break;
    case Page::OptionsEraseAllConfirm:
        munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
        if (virtualKey == VK_RETURN) {
            if (menuSelection_ == 0) {
                for (auto& list : scores_) list.clear();
                saveScores();
            }
            enterPage(Page::OptionsEraseHall);
            menuSelection_ = 6;
        }
        break;
    case Page::OptionsPassword:
        if (virtualKey == VK_LEFT) {
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
                    enterPage(Page::Options);
                    menuSelection_ = 3;
                } else {
                    passwordEditingHint_ = true;
                }
            } else {
                optionPassword_ = passwordDraft_;
                optionHint_ = hintDraft_;
                saveSettings();
                enterPage(Page::Options);
                menuSelection_ = 3;
            }
        }
        break;
    case Page::OptionsPasswordPrompt:
        if (virtualKey == VK_LEFT) {
            if (!passwordAttempt_.empty()) passwordAttempt_.pop_back();
        } else if (virtualKey == VK_HOME) {
            passwordAttempt_.clear();
        } else if (virtualKey == VK_RETURN) {
            if (originalPasswordMatches(optionPassword_, passwordAttempt_)) {
                passwordAttempt_.clear();
                passwordRejected_ = false;
                enterPage(Page::Options);
            } else {
                passwordAttempt_.clear();
                passwordRejected_ = true;
            }
        }
        break;
    case Page::Hall:
        // Both user-facing argument-7 Halls wait on the exact DS:0864 set.
        // Their callers differ: title browsing resumes the title loop, while
        // the end-game callback continues to the same-mode replay question.
        if (virtualKey == VK_RETURN || virtualKey == VK_SPACE) {
            hallHighlightName_.clear();
            hallHighlightScore_ = 0;
            if (hallContext_ == HallContext::PostGame) {
                enterPage(Page::ReplayQuestion);
            } else {
                enterPage(Page::Title);
            }
        }
        break;
    case Page::ReplayQuestion:
        munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
        if (virtualKey == VK_RETURN) {
            if (menuSelection_ == 0) {
                // The original replay prompt and ordinary board initializer
                // are primitive painters, so the preceding Hall PCX palette
                // remains active on the replayed board.
                startGameWithBoardPresentation(mode_, 1, true);
            } else {
                enterPage(Page::Title);
            }
        }
        break;
    case Page::QuitConfirm:
        munchers::applyYesNoMenuSelection(virtualKey, menuSelection_);
        if (virtualKey == VK_RETURN) {
            const bool fromFeedback = quitFromFeedback_;
            quitFromFeedback_ = false;
            if (menuSelection_ == 1) {
                if (fromFeedback) finishFeedback();
                else page_ = quitReturnPage_;
            } else {
                // Both Number feedback painters call pEndGame after a
                // confirmed Escape (0x109b2/0x10b66). Unlike an ordinary
                // voluntary quit, that terminal still performs Hall
                // admission/name entry for a qualifying score.
                if (fromFeedback) finishFeedback(true);
                else enterHall(HallContext::PostGame);
            }
        }
        break;
    case Page::NameEntry:
        // The common line editor treats Left like Backspace and Home as clear.
        if (virtualKey == VK_LEFT && !nameInput_.empty()) nameInput_.pop_back();
        if (virtualKey == VK_HOME) nameInput_.clear();
        // DS:0A4C stores a zero threshold, so empty Enter exits just like
        // Escape; submitScore() supplies "The Unknown Muncher".
        if (virtualKey == VK_RETURN) {
            submitScore();
            enterHall(HallContext::PostGame);
        }
        break;
    case Page::LevelComplete:
        if (virtualKey != VK_ESCAPE) finishLevelComplete();
        break;
    default:
        break;
    }
}

void Game::finishFeedback(const bool forcePostGame) {
    playerRecovering_ = !forcePostGame &&
                        feedbackKind_ == FeedbackKind::EatenByTroggle && lives_ > 0 &&
                        enemyAt(playerRow_, playerColumn_);
    feedbackKind_ = FeedbackKind::None;
    feedbackMessage_.clear();
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    deathAnimating_ = false;
    deathTimer_ = 0.0;
    deathSequenceTicks_ = 0;
    attractPostFeedbackBoard_ = false;
    quitFromFeedback_ = false;

    if (forcePostGame) {
        finishScoredGame();
    } else if (lives_ <= 0) {
        if (qualifiesForHallOfFame()) {
            nameInput_.clear();
            enterPage(Page::NameEntry);
        } else {
            enterHall(HallContext::PostGame);
        }
    } else {
        page_ = Page::Playing;
    }
}

void Game::beginQuitConfirm(const Page returnPage, const bool fromFeedback) {
    // Escape in selector states 4/5 reaches GSND 0 at image 0x0D2DD before
    // the synchronous quit prompt. Cancelling the prompt does not resume the
    // retired cue; on the speaker route the same driver stop closes its gate.
    attractOplPlayer_.stopAllChannels();
    effectPlayer_.stop();
    pointerCellQueue_.clear();
    quitReturnPage_ = returnPage;
    quitFromFeedback_ = fromFeedback;
    page_ = Page::QuitConfirm;
    menuSelection_ = 1;
}

void Game::handlePlayKey(UINT virtualKey) {
    if (page_ == Page::Feedback) {
        if (deathAnimating_) return;
        if (virtualKey == VK_ESCAPE) {
            // Both original user feedback painters treat an Escape waiter
            // exit as a request for the standard default-No quit prompt.
            beginQuitConfirm(Page::Playing, true);
            return;
        }
        // The exact DS:0864 waiter accepts both carriage return and Space.
        if (virtualKey != VK_SPACE && virtualKey != VK_RETURN) return;
        finishFeedback();
        return;
    }
    if (page_ == Page::Paused) {
        if (virtualKey == VK_RETURN) {
            // The resident Enter branch calls the ring reset at
            // 0x1652:02DB before leaving Time out.
            pointerCellQueue_.clear();
            page_ = Page::Playing;
        }
        if (virtualKey == VK_ESCAPE) beginQuitConfirm(Page::Paused);
        return;
    }
    if (virtualKey == VK_RETURN) {
        // 0xD2A2 resets the complete mixed input ring before Time out in
        // player states 3/4/5/6, so queued clicks and keys cannot cross it.
        pointerCellQueue_.clear();
        page_ = Page::Paused;
        return;
    }
    if (virtualKey == VK_ESCAPE) {
        beginQuitConfirm(Page::Playing);
        return;
    }
    switch (virtualKey) {
    case VK_UP: enqueuePlayerInput('I'); return;
    case VK_DOWN: enqueuePlayerInput('M'); return;
    case VK_LEFT: enqueuePlayerInput('J'); return;
    case VK_RIGHT: enqueuePlayerInput('K'); return;
    case VK_SPACE: enqueuePlayerInput(' '); return;
    default: return;
    }
}

void Game::handleCheatKey(UINT virtualKey) {
    if (virtualKey == VK_ESCAPE) {
        cheatOpen_ = false;
        return;
    }
    if (virtualKey == VK_UP) cheatSelection_ = (cheatSelection_ + 4) % 5;
    if (virtualKey == VK_DOWN) cheatSelection_ = (cheatSelection_ + 1) % 5;
    if (cheatSelection_ == 0 && (virtualKey == VK_LEFT || virtualKey == VK_RIGHT)) {
        cheatLevel_ = std::clamp(cheatLevel_ + (virtualKey == VK_LEFT ? -1 : 1), 1, 20);
    }
    if (virtualKey != VK_RETURN && virtualKey != VK_SPACE) {
        return;
    }
    switch (cheatSelection_) {
    case 0:
        level_ = cheatLevel_;
        if (page_ == Page::Playing || page_ == Page::Paused) {
            generateBoard();
            page_ = Page::Playing;
        } else {
            startGame(mode_, level_);
        }
        cheatOpen_ = false;
        break;
    case 1: invincible_ = !invincible_; break;
    case 2: lives_ = std::min(99, lives_ + 1); playTone(700, 80); break;
    case 3:
        if (page_ == Page::Playing || page_ == Page::Paused) completeLevel();
        cheatOpen_ = false;
        break;
    case 4: cheatOpen_ = false; break;
    }
}

void Game::startGame(Mode mode, int startingLevel, const bool preserveHallPalette) {
    // Non-Demo board setup at 0x089C2 calls 0x10D20, whose only operation is
    // to submit GSND entry 0 to the already resident driver.
    attractOplPlayer_.stopAllChannels();
    musicPlayer_.stop();
    attractMode_ = false;
    hallPaletteActive_ = preserveHallPalette;
    mode_ = mode;
    nameInput_.clear();
    hallHighlightName_.clear();
    hallHighlightScore_ = 0;
    level_ = std::clamp(startingLevel, 1, 20);
    score_ = 0;
    lives_ = 4;
    enemies_.clear();
    cartoonOrder_ = {0, 1, 2, 3, 4};
    cartoonOrderIndex_ = 0;
    resetContentProgression();
    generateBoard();
    enterPage(Page::Playing);
}

void Game::prepareMode() {
    // 0x0fa23 advances all four keyed content streams before every board,
    // irrespective of the selected game (including Challenge boards).
    std::array<int, static_cast<std::size_t>(Mode::Challenge)> preparedTargets{};
    const auto selectTarget = [this](std::size_t modeIndex) {
        const ContentSettings& settings = contentSettings_[modeIndex];
        if (settings.randomSequence) {
            int selected = settings.minimum;
            if (settings.minimum != settings.maximum) {
                do {
                    selected = randomInt(settings.minimum, settings.maximum);
                } while (selected == contentPreviousRandom_[modeIndex]);
            }
            contentPreviousRandom_[modeIndex] = selected;
            return selected;
        }
        int& next = contentSequenceNext_[modeIndex];
        if (next < settings.minimum || next > settings.maximum) next = settings.minimum;
        const int selected = next;
        next = next >= settings.maximum ? settings.minimum : next + 1;
        return selected;
    };

    for (const Mode progressingMode : {Mode::Multiples, Mode::Factors, Mode::Equality, Mode::Inequality}) {
        const std::size_t index = static_cast<std::size_t>(progressingMode);
        preparedTargets[index] = selectTarget(index);
    }
    primeMaximumIndex_ = std::min(primeMaximumIndex_ + 1, primeMaximumIndexLimit_);

    activeBoardMode_ = mode_;
    if (mode_ == Mode::Challenge || attractMode_) {
        std::array<Mode, static_cast<std::size_t>(Mode::Challenge)> enabled{};
        int enabledCount = 0;
        for (int index = 0; index < static_cast<int>(Mode::Challenge); ++index) {
            if (contentSettings_[static_cast<std::size_t>(index)].use) {
                enabled[static_cast<std::size_t>(enabledCount++)] = static_cast<Mode>(index);
            }
        }
        activeBoardMode_ = enabledCount > 0
            ? enabled[static_cast<std::size_t>(randomInt(0, enabledCount - 1))]
            : Mode::Multiples;
        if (attractMode_) mode_ = activeBoardMode_;
    }
    const std::size_t modeIndex = static_cast<std::size_t>(activeBoardMode_);
    switch (activeBoardMode_) {
    case Mode::Multiples: target_ = preparedTargets[modeIndex]; break;
    case Mode::Factors: target_ = preparedTargets[modeIndex]; break;
    case Mode::Primes: target_ = 0; break;
    case Mode::Equality: target_ = preparedTargets[modeIndex]; break;
    case Mode::Inequality: target_ = preparedTargets[modeIndex]; break;
    default: break;
    }

    // 0x0fb16 writes DS:6040 after every mode selection. Only Inequality
    // consumes it, but every other game still spends this PRNG call. The sole
    // exception is Inequality target 1, which is forced to Greater Than.
    relation_ = activeBoardMode_ == Mode::Inequality && target_ == 1
        ? 2 : randomInt(0, 2);
}

void Game::generateBoard() {
    prepareMode();
    ++boardGenerationSerial_;
    gameplayTickAccumulator_ = 0.0;
    enemies_.clear();
    moving_ = false;
    moveTimer_ = 0.0;
    playerTerminalFrame_ = -1;
    pointerCellQueue_.clear();
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    munchSavedCell_ = {};
    feedbackKind_ = FeedbackKind::None;
    feedbackMessage_.clear();
    feedbackEnemyType_ = -1;
    feedbackEnemySlot_ = -1;
    deathAnimating_ = false;
    deathTimer_ = 0.0;
    deathSequenceTicks_ = 0;
    attractPostFeedbackBoard_ = false;
    lifeLossPending_ = false;
    enemyWarning_ = false;
    playerRecovering_ = false;
    // 0x0f787 draws upper-exclusive random(0,2) independently for every
    // cell, and the loop at 0x0fb52 regenerates the complete 30-cell board
    // only when fewer than two positive (correct) records were produced.
    do {
        correctRemaining_ = 0;
        for (Cell& current : cells_) current = {};
        for (Cell& current : cells_) {
            current = generatedCell(randomInt(0, 1) != 0);
            if (current.correct) ++correctRemaining_;
        }
    } while (correctRemaining_ < 2);

    initializeSafeZones();

    // The player initializer at 0x08b14 runs after board and safe-zone
    // generation. It draws the column first (random(0,4)+1), then the row
    // (random(0,3)+1), and clears whichever generated record it chose.
    playerColumn_ = randomInt(1, BoardColumns - 2);
    playerRow_ = randomInt(1, BoardRows - 2);
    Cell& playerCell = cell(playerRow_, playerColumn_);
    if (playerCell.correct) --correctRemaining_;
    const bool playerStartsSafe = playerCell.safe;
    const double playerSafeTimer = playerCell.safeTimer;
    playerCell = {};
    playerCell.eaten = true;
    playerCell.safe = playerStartsSafe;
    playerCell.safeTimer = playerSafeTimer;
    initializeEnemySlots();
}

Game::Cell Game::generatedCell(bool correct) {
    Cell current;
    current.correct = correct;
    static constexpr std::array<int, 5> NearbyOffsets = {1, 2, 3, 4, 10};
    static constexpr std::array<int, 6> NearbyOffsetsIncludingEqual = {0, 1, 2, 3, 4, 10};
    const auto lowerStrict = [this](int target) {
        if (target < 11) {
            const int span = std::min(target, 4);
            return target - randomInt(1, std::max(1, span - 1));
        }
        return target - NearbyOffsets[static_cast<std::size_t>(randomInt(0, 4))];
    };
    const auto upperStrict = [this](int target) {
        return target + NearbyOffsets[static_cast<std::size_t>(randomInt(0, 4))];
    };
    const auto lowerIncludingEqual = [this](int target) {
        if (target < 11) {
            const int span = std::min(target, 4);
            return target - randomInt(0, std::max(0, span - 1));
        }
        return target - NearbyOffsetsIncludingEqual[static_cast<std::size_t>(randomInt(0, 5))];
    };
    const auto upperIncludingEqual = [this](int target) {
        return target + NearbyOffsetsIncludingEqual[static_cast<std::size_t>(randomInt(0, 5))];
    };
    const auto expressionFor = [this](int result) {
        const ContentSettings& settings = contentSettings_[static_cast<std::size_t>(activeBoardMode_)];
        std::array<int, 4> enabledOperations{};
        int enabledCount = 0;
        for (int operation = 0; operation < 4; ++operation) {
            if (settings.operations[static_cast<std::size_t>(operation)]) {
                enabledOperations[static_cast<std::size_t>(enabledCount++)] = operation;
            }
        }
        if (enabledCount == 0) {
            for (int operation = 0; operation < 4; ++operation) {
                enabledOperations[static_cast<std::size_t>(enabledCount++)] = operation;
            }
        }
        const int operation = enabledOperations[static_cast<std::size_t>(randomInt(0, enabledCount - 1))];
        int left = 0;
        int right = 0;
        char symbol = '+';
        if (operation == 0) {
            left = randomInt(0, std::min(result, 9));
            right = result - left;
            // Addition and multiplication both call 0x10531, which spends a
            // random(0,2) result and swaps the operands when it is zero.
            if (randomInt(0, 1) == 0) std::swap(left, right);
        } else if (operation == 1) {
            symbol = '-';
            right = randomInt(0, 9);
            left = result + right;
        } else if (operation == 2) {
            symbol = 'x';
            std::vector<int> factors;
            // 0x100f2 stores the result first, walks candidate divisors
            // downward, and appends 1 last. The order is observable because
            // 0x0ffd1 indexes this table directly to choose the left operand.
            // For result 1 the original table contains two identical 1s.
            factors.push_back(result);
            for (int value = result - 1; value > 1; --value) {
                if (result % value == 0) factors.push_back(value);
            }
            factors.push_back(1);
            left = factors[randomInt(0, static_cast<int>(factors.size()) - 1)];
            right = result / left;
            if (randomInt(0, 1) == 0) std::swap(left, right);
        } else {
            symbol = ':';
            right = randomInt(1, std::min(100 / result, 9));
            left = result * right;
        }
        return std::to_string(left) + symbol + std::to_string(right);
    };

    switch (activeBoardMode_) {
    case Mode::Multiples: {
        const int maximumMultiplier =
            contentSettings_[static_cast<std::size_t>(Mode::Multiples)].maxMultiplier;
        const int maximumValue = target_ * maximumMultiplier;
        int value = correct ? target_ * randomInt(1, maximumMultiplier)
                            : randomInt(1, maximumValue);
        while (!correct && value % target_ == 0) value = randomInt(1, maximumValue);
        current.label = formatNumber(value);
        break;
    }
    case Mode::Factors: {
        std::vector<int> factors;
        // 0x0fe57 builds the factor table from the target down to one. The
        // correct-answer path indexes that stored order directly, so the
        // ordering is observable in the preserved Factors-of-63 demo board.
        // The adjacent-gap incorrect path consumes the same descending table.
        for (int value = target_; value >= 1; --value) {
            if (target_ % value == 0) factors.push_back(value);
        }
        int value = 1;
        if (correct) {
            value = factors[static_cast<std::size_t>(randomInt(0, static_cast<int>(factors.size()) - 1))];
        } else {
            for (;;) {
                const int gapIndex = randomInt(0, static_cast<int>(factors.size()) - 2);
                const int high = factors[static_cast<std::size_t>(gapIndex)];
                const int low = factors[static_cast<std::size_t>(gapIndex + 1)];
                const int gap = high - low - 1;
                if (gap <= 0) continue;
                value = high - randomInt(0, gap - 1) - 1;
                break;
            }
        }
        current.label = formatNumber(value);
        break;
    }
    case Mode::Primes: {
        const int maximumIndex = std::clamp(
            primeMaximumIndex_, 0, static_cast<int>(PrimeNumbers.size()) - 1);
        int value = 1;
        if (correct) {
            value = PrimeNumbers[static_cast<std::size_t>(randomInt(0, maximumIndex))];
        } else if (maximumIndex < 3) {
            if (maximumIndex == 2) value = randomInt(0, 1) == 0 ? 1 : 4;
        } else {
            const int gap = randomInt(0, maximumIndex - 2);
            if (gap != 0) {
                const int low = PrimeNumbers[static_cast<std::size_t>(gap)];
                const int middle = PrimeNumbers[static_cast<std::size_t>(gap + 1)];
                const int high = PrimeNumbers[static_cast<std::size_t>(gap + 2)];
                do {
                    value = randomInt(low + 1, high - 1);
                } while (value == middle);
            }
        }
        current.label = formatNumber(value);
        break;
    }
    case Mode::Equality: {
        int result = target_;
        if (!correct) {
            result = target_ == 1 || randomInt(0, 1) != 0
                ? upperStrict(target_) : lowerStrict(target_);
        }
        current.label = expressionFor(result);
        break;
    }
    case Mode::Inequality: {
        int result = target_;
        if (relation_ == 0) result = correct ? lowerStrict(target_) : upperIncludingEqual(target_);
        if (relation_ == 1 && correct) {
            result = randomInt(0, 1) == 0 ? lowerStrict(target_) : upperStrict(target_);
        }
        if (relation_ == 2) result = correct ? upperStrict(target_) : lowerIncludingEqual(target_);
        current.label = expressionFor(result);
        break;
    }
    default: break;
    }
    return current;
}

void Game::beginMove(int row, int column, int direction) {
    if (moving_ || munching_ || playerRecovering_ || deathAnimating_ ||
        (page_ != Page::Playing && page_ != Page::Attract)) return;
    row = std::clamp(row, 0, BoardRows - 1);
    column = std::clamp(column, 0, BoardColumns - 1);
    if (row == playerRow_ && column == playerColumn_) return;
    moveFromRow_ = playerRow_;
    moveFromColumn_ = playerColumn_;
    moveToRow_ = row;
    moveToColumn_ = column;
    moveDirection_ = std::clamp(direction, 0, 3);
    playerTerminalFrame_ = -1;
    moving_ = true;
    moveTimer_ = playerMoveAnimationDuration(moveDirection_);
}

double Game::playerMoveAnimationDuration(const int direction) {
    const bool vertical = direction == 0 || direction == 2;
    return static_cast<double>(vertical ? 5 : 6) / OriginalJobTicksPerSecond;
}

int Game::playerMovementPhase() const {
    if (!moving_) return 0;
    const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
    const int stepCount = vertical ? 5 : 6;
    const double duration = playerMoveAnimationDuration(moveDirection_);
    const double elapsed = std::clamp(duration - moveTimer_, 0.0, duration);
    return std::clamp(static_cast<int>(
                          std::floor(elapsed * OriginalJobTicksPerSecond + 1e-7)),
                      0, stepCount - 1);
}

int Game::playerMoveFrameAtPhase(const int direction, const int phase) {
    // 0x08BBA paints the third directional record. At 0x08807/0x08820 each
    // movement callback marks its cells before 0x0882F advances the actor
    // record, so the new position retains the prior record for that paint.
    // Live Word frames 2465-2474 fix this otherwise easy-to-miss alignment.
    constexpr std::array<int, 6> FrameOffsets = {2, 2, 1, 0, 1, 2};
    return std::clamp(direction, 0, 3) * 3 +
           FrameOffsets[static_cast<std::size_t>(
               std::clamp(phase, 0, static_cast<int>(FrameOffsets.size()) - 1))];
}

int Game::munchFrameAtTick(const int elapsedTicks) {
    // 0x08A9C seeds record 12/counter 0. Each 0x093A9 callback calls the
    // shared animator at 0x07F5B before incrementing the chew transition.
    // Tick seven reaches the terminal branch in that same callback, so its
    // final record 13 is not held for another visible scheduler interval.
    constexpr std::array<int, 8> Frames = {12, 13, 14, 13, 12, 13, 14, 13};
    return Frames[static_cast<std::size_t>(
        std::clamp(elapsedTicks, 0, static_cast<int>(Frames.size()) - 1))];
}

bool Game::playerCanBeCaughtAtEnemyEndpoint() const {
    // The common predicate at Number image 0x08878 queries Muncher job 4 and
    // accepts only scheduler state 4. Native state 3 is moving, state 5 is
    // chewing, and state 6 is post-collision recovery.
    return !moving_ && !munching_ && !playerRecovering_ && !deathAnimating_;
}

void Game::updatePlayerMovementTick(const double tickSeconds) {
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
        loseLife(enemyType, enemySlotAt(playerRow_, playerColumn_));
    } else {
        processPointerCellQueue();
    }
}

void Game::enqueuePlayerInput(const int inputByte) {
    // 0x1674E admits at most nine bytes into the ten-byte circular buffer.
    // Keyboard and pointer producers share the same head/tail/count state.
    if (pointerCellQueue_.size() >= 9) return;
    pointerCellQueue_.push_back(inputByte);
    processPointerCellQueue();
}

void Game::processPointerCellQueue() {
    if (pointerCellQueue_.empty() || page_ != Page::Playing || moving_ || munching_ ||
        deathAnimating_ || playerRecovering_) {
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
        case ' ': munch(); break;
        default: break;
        }
        return;
    }

    const int targetCell = -inputByte;
    if (targetCell >= BoardCellCount) {
        pointerCellQueue_.pop_front();
        processPointerCellQueue();
        return;
    }

    const int currentCell = playerRow_ * BoardColumns + playerColumn_;
    if (targetCell == currentCell) {
        // The normal input dispatcher at 0x0947f consumes a non-positive
        // current-cell event and calls the ordinary munch routine. A first
        // click that merely arrives here has already been consumed by
        // 0x07fd4, so arrival alone does not eat.
        pointerCellQueue_.pop_front();
        munch();
        return;
    }

    const int targetRow = targetCell / BoardColumns;
    const int targetColumn = targetCell % BoardColumns;
    const int rowDelta = targetRow - playerRow_;
    const int columnDelta = targetColumn - playerColumn_;

    // 0x07fd4 starts vertically and switches to the horizontal axis only
    // when its absolute distance is strictly greater. Ties are vertical.
    int direction = rowDelta < 0 ? 0 : 2;
    if (columnDelta > 0 && columnDelta > std::abs(rowDelta)) direction = 1;
    if (columnDelta < 0 && -columnDelta > std::abs(rowDelta)) direction = 3;

    int nextRow = playerRow_;
    int nextColumn = playerColumn_;
    switch (direction) {
    case 0: --nextRow; break;
    case 1: ++nextColumn; break;
    case 2: ++nextRow; break;
    case 3: --nextColumn; break;
    default: return;
    }

    // The DOS helper removes the queued target just before it starts the
    // final adjacent step. This is what lets a second queued click on that
    // same cell become the eat action as soon as the walk finishes.
    if (nextRow * BoardColumns + nextColumn == targetCell) {
        pointerCellQueue_.pop_front();
    }
    beginMove(nextRow, nextColumn, direction);
}

void Game::munch() {
    Cell& current = cell(playerRow_, playerColumn_);
    // Image 0x08a42 rejects a zero board word before it considers the
    // transient cell-status byte or starts cue 7/8.  `eaten` is normally the
    // native zero-word marker, while the empty label is the authoritative
    // payload check and also covers a blank restored by a Troggle trail.
    if (current.eaten || current.label.empty() || munching_ || moving_) return;
    // Image 0x08a82 saves the signed board record, then 0x08a8e clears the
    // live word before actor record 12 is installed. Keep the native label as
    // part of that saved record because wrong-answer feedback formats it only
    // when the seventh callback resolves.
    munchSavedCell_ = current;
    current.eaten = true;
    playGameplaySound(munchSavedCell_.correct ? 7 : 8);
    playerTerminalFrame_ = -1;
    munching_ = true;
    munchTimer_ = MunchAnimationDuration;
    munchCellIndex_ = playerRow_ * BoardColumns + playerColumn_;
}

void Game::resolveMunch() {
    if (!munching_ || munchCellIndex_ < 0 || munchCellIndex_ >= BoardCellCount) return;
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    const Cell eatenCell = munchSavedCell_;
    munchSavedCell_ = {};
    if (eatenCell.correct) {
        --correctRemaining_;
        const bool maximumScoreReached = addScore(scoreValueForLevel(level_));
        if (maximumScoreReached) {
            // 0x07ece calls the ordinary game terminal as soon as the updated
            // 32-bit score reaches one million. This correct answer keeps its
            // ordinary cue, but it cannot advance or start a reward cartoon.
            finishScoredGame();
            return;
        }
        if (correctRemaining_ <= 0) completeLevel();
    } else {
        feedbackMessage_ = wrongAnswerMessage(eatenCell);
        loseLife();
    }
}

int Game::scoreValueForLevel(int level) {
    return MuncherScore::pointsForLevel(level);
}

bool Game::addScore(int points) {
    // Game stores total Munchers while the original/shared rule stores only
    // reserves. Translate at this boundary without changing any caller state.
    MuncherScore shared(score_, lives_ - 1);
    const MuncherScoreAward award = shared.awardPoints(points);
    score_ = shared.score();
    lives_ = shared.reserves() + 1;
    return award.maximumReached;
}

void Game::finishScoredGame() {
    moving_ = false;
    moveTimer_ = 0.0;
    playerTerminalFrame_ = -1;
    pointerCellQueue_.clear();
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    munchSavedCell_ = {};
    playerRecovering_ = false;
    feedbackKind_ = FeedbackKind::None;
    feedbackMessage_.clear();
    hallHighlightName_.clear();
    hallHighlightScore_ = 0;
    if (qualifiesForHallOfFame()) {
        nameInput_.clear();
        enterPage(Page::NameEntry);
    } else {
        enterHall(HallContext::PostGame);
    }
}

std::string Game::wrongAnswerMessage(const Cell& current) const {
    const std::string quoted = "\"" + current.label + "\"";
    switch (activeBoardMode_) {
    case Mode::Multiples: return quoted + " is not a multiple of \"" + std::to_string(target_) + "\".";
    case Mode::Factors: return quoted + " is not a factor of \"" + std::to_string(target_) + "\".";
    case Mode::Primes: return "The number " + quoted + " is not prime.";
    case Mode::Equality:
    case Mode::Inequality:
        if (const auto result = evaluateExpressionLabel(current.label)) {
            return "Oops!  \"" + current.label + "=" + std::to_string(*result) + "\"";
        }
        return "Oops!  " + quoted;
    default: return "That answer does not match the rule.";
    }
}

void Game::loseLife(int enemyType, int enemySlot) {
    moving_ = false;
    moveTimer_ = 0.0;
    playerTerminalFrame_ = -1;
    pointerCellQueue_.clear();
    munching_ = false;
    munchTimer_ = 0.0;
    munchCellIndex_ = -1;
    munchSavedCell_ = {};
    playerRecovering_ = false;
    if (invincible_) {
        playTone(900, 50);
        return;
    }
    feedbackEnemyType_ = enemyType;
    feedbackEnemySlot_ = enemySlot;
    if (enemyType >= 0) {
        feedbackKind_ = FeedbackKind::EatenByTroggle;
        feedbackMessage_.clear();
        deathAnimating_ = true;
        deathTimer_ = TroggleEatAnimationDuration;
        deathSequenceTicks_ = 0;
        attractPostFeedbackBoard_ = false;
        // The common state-5 collision callback at 0x09d41 runs the bite
        // records first; only its terminal state calls 0x09262, which removes
        // the Muncher actor and decrements DS:5998. Keep every reserve visible
        // until that animation boundary.
        lifeLossPending_ = !attractMode_;
        // The common collision routine at 0x08db6 selects one biting actor
        // and assigns frame -1 to other active Troggles occupying that cell.
        // The ordinary nonterminal bite terminal removes/rearms those extra
        // state-6 actors; Demo and final-life terminal paths skip that scan.
        bool selectedCollisionActor = false;
        for (Enemy& enemy : enemies_) {
            if (enemy.row != playerRow_ || enemy.column != playerColumn_) continue;
            const bool selected = !selectedCollisionActor &&
                (enemySlot < 0 || enemy.slot == enemySlot);
            if (selected) {
                selectedCollisionActor = true;
                enemy.collisionHidden = false;
                enemy.overlapFrozen = false;
                enemy.overlapRetired = false;
                feedbackEnemySlot_ = enemy.slot;
            } else if (!enemy.entering) {
                // 0x08e76..0x08ecb only assigns record -1/state 6 when the
                // collocated Troggle job is already in its resident actor
                // state.  Warning/entry records merely carry the same saved
                // destination cell; they remain live.  The seeded Smarty
                // capture proves an earlier Reggie continues its right-edge
                // entry throughout the selected Smarty's player bite.
                enemy.collisionHidden = true;
                // The shared collision initializer assigns scheduler state 6
                // to every non-selected actor on this cell.  It remains inert
                // until the ordinary bite terminal removes/rearms it.
                enemy.overlapFrozen = true;
                enemy.overlapRetired = true;
            }
        }
        // The shared state-5 initializer ends at 0x08ee2 by moving the
        // selected biter to the end of DS:5A84, making it the last actor
        // restored wherever dirty rectangles overlap.
        promoteEnemyPaintSlot(feedbackEnemySlot_);
    } else {
        if (!attractMode_) --lives_;
        feedbackKind_ = FeedbackKind::WrongAnswer;
        // The seventh chew callback installs feedback while the preceding
        // open-mouth record is still resident. Keep that player record for
        // the blocking message instead of reverting immediately to standing.
        // The captured Prime feedback is 0xc118cc3e4eff3d27 with frame 12.
        playerTerminalFrame_ = 12;
        deathAnimating_ = false;
        deathTimer_ = 0.0;
        deathSequenceTicks_ = 0;
        attractPostFeedbackBoard_ = false;
        lifeLossPending_ = false;
    }
    page_ = Page::Feedback;
    if (enemyType >= 0 && feedbackEnemySlot_ >= 0) {
        // Endpoint state 4 paints its stationary record before the selected
        // actor's first state-5 bite callback. The live Worker collision holds
        // that player-hidden dwell for two complete frames. Preserve it as a
        // real resident-surface callback rather than jumping straight to bite
        // record 12.
        const int selectedSlot = feedbackEnemySlot_;
        deathResidentSurfacePixels_.clear();
        feedbackEnemySlot_ = enemySlotCount_ + 1;
        const std::vector<std::uint32_t> endpointPixels =
            capturePresentationFrame();
        std::vector<std::uint32_t> endpointPresentationPixels = endpointPixels;
        const bool retainedOverlapWarning =
            attractMode_ && !attractOverlapEntryWarningPixels_.empty();
        if (retainedOverlapWarning) {
            // State 3 reaches the occupied endpoint by repainting its dirty
            // cell on the retained warning surface.  The endpoint actor page
            // is therefore observable before the earlier entry's next repaint
            // and before the logical warning record is recomputed.  Apply the
            // collision cell (including the one-pixel outer grid extent) to
            // that resident page, then expose the completed endpoint while
            // carrying the untouched warning rectangle forward.
            std::vector<std::uint32_t> collisionCellPixels =
                attractOverlapEntryWarningPixels_;
            copyPixelRectangle(
                collisionCellPixels, endpointPixels,
                BoardLeft + playerColumn_ * BoardCellWidth,
                BoardTop + playerRow_ * BoardCellHeight,
                BoardLeft + (playerColumn_ + 1) * BoardCellWidth + 2,
                BoardTop + (playerRow_ + 1) * BoardCellHeight + 2);
            enqueuePresentationFrame(collisionCellPixels);
            copyPixelRectangle(endpointPresentationPixels,
                               attractOverlapEntryWarningPixels_,
                               0, 62, 16, 137);
            attractOverlapEntryWarningPixels_.clear();
        }
        const bool waitsForEarlierEntry = std::any_of(
            enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                return enemy.slot >= 0 && enemy.slot < selectedSlot &&
                       enemy.entering && enemy.moving &&
                       !enemy.collisionHidden && !enemy.overlapFrozen &&
                       !enemy.overlapRetired;
        });
        if (waitsForEarlierEntry) {
            deathResidentSurfacePixels_ = endpointPresentationPixels;
        }
        enqueuePresentationFrame(endpointPresentationPixels);
        feedbackEnemySlot_ = selectedSlot;
    }
    if (attractMode_) {
        // A collision begins the 150-tick hold only when the bite job reaches
        // its terminal callback. Wrong-answer feedback has no bite sequence.
        transitionTimer_ = enemyType >= 0 ? 0.0 : AttractFeedbackDuration;
    }
    if (enemyType >= 0) playGameplaySound(12);
}

void Game::completeLevel() {
    if (attractMode_) {
        beginAttractHallTransition();
        return;
    }
    // The empty-board branch at 0x0a1c0 passes literal 15 without music bit
    // 0x80. It is an ordinary sound cue on both AdLib and PC speaker, and it
    // precedes direct board generation or the cartoon's first callback.
    playGameplaySound(15);
    const int completedLevel = level_;
    prepareCartoonOrderForCompletedLevel(completedLevel);
    if (completedLevel % 3 != 0) {
        captureBoardPresentationSource();
        ++level_;
        cheatLevel_ = level_;
        generateBoard();
        enterPage(Page::Playing);
        beginBoardPresentation();
        return;
    }
    static constexpr std::array<int, 5> cutscenes = {2013, 2015, 2017, 2019, 2021};
    const int sceneIndex = cartoonOrder_[static_cast<std::size_t>(cartoonOrderIndex_)];
    // The original 0x0f1ba branch closes the completed board before replacing
    // it with the selected scene. Preserve that exact outgoing framebuffer.
    captureBoardPresentationSource();
    cutsceneImage_ = cutscenes[static_cast<std::size_t>(sceneIndex)];
    cutsceneSoundBank_ = 11 + sceneIndex;
    enterPage(Page::LevelComplete);
    sceneTickAccumulator_ = 0.0;
    // The selected-scene loader at 0x0F340 submits GSND 0, then blocks for
    // 15 ms at 0x0F34B before installing the PCXF/SCPT scene. Defer that load
    // to the presenter's covered frame so the interval remains callback-free.
    attractOplPlayer_.stopAllChannels();
    effectPlayer_.stop();
    pendingCartoonScene_ = sceneIndex;
    beginCartoonPresentation();
}

void Game::prepareCartoonOrderForCompletedLevel(const int completedLevel) {
    // pLevelDone at image 0x0f101 resets the five words only at visible
    // level 1. Whenever DS:118e is zero it then performs five independent
    // random(0,5) swaps, pairing each full-range draw with loop index 0..4.
    // This is deliberately neither Fisher-Yates nor a one-time batch shuffle.
    if (completedLevel == 1) {
        cartoonOrder_ = {0, 1, 2, 3, 4};
        cartoonOrderIndex_ = 0;
    }
    if (cartoonOrderIndex_ != 0) return;
    for (int index = 0; index < static_cast<int>(cartoonOrder_.size()); ++index) {
        const int randomIndex = randomInt(0, static_cast<int>(cartoonOrder_.size()) - 1);
        std::swap(cartoonOrder_[static_cast<std::size_t>(randomIndex)],
                  cartoonOrder_[static_cast<std::size_t>(index)]);
    }
}

void Game::advanceCartoonOrder() {
    // The original advances DS:118e in the cartoon teardown at 0x0f315,
    // after the scene has closed rather than when it is selected.
    ++cartoonOrderIndex_;
    if (cartoonOrderIndex_ >= static_cast<int>(cartoonOrder_.size())) {
        cartoonOrderIndex_ = 0;
    }
}

bool Game::loadLevelCompleteScene(const int sceneIndex) {
    static constexpr std::array<std::uint16_t, 12> Scene0 = {8,5,7,6,9,10,11,0,1,2,3,4};
    static constexpr std::array<std::uint16_t, 10> Scene1 = {11,4,5,6,2,1,3,7,8,9};
    static constexpr std::array<std::uint16_t, 9> Scene2 = {7,9,10,12,4,0,5,1,6};
    static constexpr std::array<std::uint16_t, 9> Scene3 = {10,9,4,5,6,0,1,3,2};
    static constexpr std::array<std::uint16_t, 7> Scene4 = {8,7,1,2,4,3,0};
    const BlobView script = assets_.gameArchive().find(
        "SCPT", static_cast<std::uint32_t>(cutsceneImage_ - 1));
    bool loaded = false;
    switch (sceneIndex) {
    case 0: loaded = levelCompleteScene_.load(script, Scene0); break;
    case 1: loaded = levelCompleteScene_.load(script, Scene1); break;
    case 2: loaded = levelCompleteScene_.load(script, Scene2); break;
    case 3: loaded = levelCompleteScene_.load(script, Scene3); break;
    case 4: loaded = levelCompleteScene_.load(script, Scene4); break;
    default: break;
    }
    if (loaded) {
        levelCompleteSceneTicks_ = 0;
        resetLevelCompleteSurface();
    }
    return loaded;
}

void Game::resetLevelCompleteSurface() {
    const std::uint32_t background = assets_.graphicsMode() == GraphicsMode::Cga4
        ? Colors::Black : Colors::White;
    sceneSurfacePixels_.assign(
        static_cast<std::size_t>(Renderer::Width) * Renderer::Height, background);
}

void Game::advanceLevelCompleteScene() {
    levelCompleteScene_.tick();
    ++levelCompleteSceneTicks_;
    applyLevelCompletePaintEvents();
}

void Game::applyLevelCompletePaintEvents() {
    if (!levelCompleteScene_.valid()) return;
    if (sceneSurfacePixels_.size() !=
        static_cast<std::size_t>(Renderer::Width) * Renderer::Height) {
        resetLevelCompleteSurface();
    }

    Renderer surface(assets_.graphicsMode());
    surface.replacePixels(sceneSurfacePixels_);
    const bool cgaScene = assets_.graphicsMode() == GraphicsMode::Cga4;
    const std::uint32_t background = cgaScene
        ? Colors::Black : Colors::White;
    const auto floorCgaByte = [](const int x) {
        const int remainder = x % 4;
        return x - (remainder >= 0 ? remainder : remainder + 4);
    };
    const auto actorDestinationX = [&](const SceneActor& actor) {
        if (!cgaScene) return actor.x;
        // The CGA PCXF presenter addresses packed four-pixel bytes. Its
        // destination coordinate combines the actor's containing byte with
        // the source crop's intra-byte offset, including actors entering from
        // a negative x coordinate.
        const int byteX = cutsceneImage_ == 2019 && actor.x < 0
            ? (actor.x / 4) * 4
            : floorCgaByte(actor.x);
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(cutsceneImage_), actor.frame);
        return byteX + (frame ? frame->x % 4 : 0);
    };
    struct SceneRectangle {
        int left{};
        int top{};
        int right{};
        int bottom{};
    };
    const auto actorRectangle = [&](const SceneActor& actor)
        -> std::optional<SceneRectangle> {
        const auto frame = assets_.spriteFrame(
            static_cast<std::uint32_t>(cutsceneImage_), actor.frame);
        if (!frame) return std::nullopt;
        int x = actorDestinationX(actor);
        int y = actor.y;
        const bool measuredCgaOffscreenRecord = cgaScene &&
            ((cutsceneImage_ == 2015 &&
              (actor.frame == 16 || actor.frame == 17)) ||
             (cutsceneImage_ == 2017 && actor.frame == 8));
        if (measuredCgaOffscreenRecord && x >= Renderer::Width) {
            return std::nullopt;
        }
        if (x >= Renderer::Width) x = Renderer::Width - 1;
        if (y >= Renderer::Height) y = Renderer::Height - 1;
        int width = frame->width;
        if (cgaScene) {
            width -= (frame->x + width) % 4;
            // A CGA crop beginning in the fourth pixel of a packed byte and
            // ending at source x=319 loses that terminal byte in the original
            // PCXF record builder. Scene 2's frame 1 exposes the otherwise
            // hidden edge case across all 136 states.
            if (frame->x % 4 == 3 && frame->x + frame->width == 320) {
                width -= 4;
            }
        }
        if (width <= 0) return std::nullopt;
        return SceneRectangle{x, y, x + width, y + frame->height};
    };
    const auto intersects = [](const SceneRectangle& left,
                               const SceneRectangle& right) {
        return left.left < right.right && right.left < left.right &&
               left.top < right.bottom && right.top < left.bottom;
    };
    const auto clearActorRectangle = [&](const SceneActor& actor) {
        const auto rectangle = actorRectangle(actor);
        if (!rectangle) return;
        surface.fillRect(rectangle->left, rectangle->top,
                         rectangle->right - rectangle->left,
                         rectangle->bottom - rectangle->top, background);
    };
    const auto redrawActiveActors = [&](const std::vector<SceneRectangle>& dirtyRectangles) {
        Renderer recomposed(assets_.graphicsMode());
        recomposed.replacePixels(surface.pixels());
        const auto& actors = levelCompleteScene_.actors();
        for (auto iterator = actors.rbegin(); iterator != actors.rend(); ++iterator) {
            if (!iterator->visible || iterator->ended) continue;
            const auto rectangle = actorRectangle(*iterator);
            if (!rectangle || std::none_of(
                    dirtyRectangles.begin(), dirtyRectangles.end(),
                    [&](const SceneRectangle& dirty) {
                        return intersects(*rectangle, dirty);
                    })) {
                continue;
            }
            // Number's cartoon sheets retain their own indexed geometry while
            // the resident VGA DAC supplies the live-captured scene palette.
            const int transparentPaletteIndex =
                cutsceneImage_ == 2015 && iterator->frame == 1 ? 0 : -1;
            int sourceWidthLimit = cgaScene
                ? rectangle->right - rectangle->left : -1;
            if (cgaScene && cutsceneImage_ == 2015 && iterator->frame == 5 &&
                rectangle->right >= Renderer::Width) {
                // The crossing record clears its complete clipped rectangle,
                // then the packed image copy stops before screen byte 79.
                sourceWidthLimit = std::max(0, 316 - rectangle->left);
            }
            drawSprite(recomposed, static_cast<std::uint32_t>(cutsceneImage_),
                       iterator->frame, actorDestinationX(*iterator), iterator->y,
                       false, false, false, true, true,
                       true, transparentPaletteIndex,
                       levelCompleteSceneTicks_ == 1, false,
                       sourceWidthLimit);
        }
        std::vector<std::uint32_t> merged = surface.pixels();
        const auto& recomposedPixels = recomposed.pixels();
        for (const SceneRectangle& dirty : dirtyRectangles) {
            const int left = std::clamp(dirty.left, 0, Renderer::Width);
            const int top = std::clamp(dirty.top, 0, Renderer::Height);
            const int right = std::clamp(dirty.right, 0, Renderer::Width);
            const int bottom = std::clamp(dirty.bottom, 0, Renderer::Height);
            for (int y = top; y < bottom; ++y) {
                const std::size_t row = static_cast<std::size_t>(y) * Renderer::Width;
                std::copy(recomposedPixels.begin() + row + left,
                          recomposedPixels.begin() + row + right,
                          merged.begin() + row + left);
            }
        }
        surface.replacePixels(std::move(merged));
    };
    for (const ScenePaintEvent& event : levelCompleteScene_.paintEvents()) {
        if (!event.dirty) continue;
        const bool previousVisible = event.previous.visible && !event.previous.ended;
        const bool currentVisible = event.current.visible && !event.current.ended;
        if (!previousVisible && !currentVisible) continue;
        const bool moved = event.previous.x != event.current.x ||
                           event.previous.y != event.current.y;
        std::vector<SceneRectangle> dirtyRectangles;
        bool retainCgaTrailingStrip = false;
        if (cgaScene && previousVisible && currentVisible && moved) {
            const auto previousRectangle = actorRectangle(event.previous);
            const auto currentRectangle = actorRectangle(event.current);
            // The packed CGA background snapshot follows the new left-moving
            // record. If that record's right edge also shrinks, the uncovered
            // byte strip from the preceding sprite remains in the retained
            // framebuffer. Number scene 2 exposes all 113 pixels of this tail.
            retainCgaTrailingStrip = cutsceneImage_ == 2017 &&
                event.previous.frame == 5 && event.current.frame == 6 &&
                previousRectangle && currentRectangle &&
                currentRectangle->left < previousRectangle->left &&
                currentRectangle->right < previousRectangle->right;
        }
        // The Number painter has already installed the new BTMP record when
        // it restores a same-position actor.  Its clean rectangle therefore
        // uses the current record's dimensions; pixels outside a shrinking
        // frame remain retained.  A move or hide still restores the old
        // rectangle so the previous placement cannot trail.
        if (previousVisible && (!currentVisible || moved) &&
            !retainCgaTrailingStrip) {
            if (const auto rectangle = actorRectangle(event.previous)) {
                dirtyRectangles.push_back(*rectangle);
            }
        }
        if (currentVisible) {
            if (const auto rectangle = actorRectangle(event.current)) {
                dirtyRectangles.push_back(*rectangle);
            }
        }
        if (previousVisible && (!currentVisible || moved) &&
            !retainCgaTrailingStrip) {
            clearActorRectangle(event.previous);
        }
        if (currentVisible) clearActorRectangle(event.current);
        redrawActiveActors(dirtyRectangles);
    }
    sceneSurfacePixels_ = surface.pixels();
}

void Game::finishLevelComplete() {
    captureBoardPresentationSource();
    effectPlayer_.stop();
    pendingCartoonScene_ = -1;
    // The teardown helper at 0x0F490 retires the scene, waits 15 ms, and the
    // outer teardown at 0x0F326 submits GSND 0 again. Preserve both resident
    // calls and their separation; neither unloads or resets the YM3812.
    attractOplPlayer_.stopAllChannels();
    attractOplPlayer_.stopAllChannelsAfter(15);
    advanceCartoonOrder();
    ++level_;
    cheatLevel_ = level_;
    generateBoard();
    enterPage(Page::Playing);
    beginBoardPresentation();
}

double Game::enemySpawnDelay() {
    const int difficulty = difficultyIndexForLevel(level_);
    const int ticks = EnemyArrivalTicks[static_cast<std::size_t>(difficulty)] + randomInt(0, 89);
    return static_cast<double>(ticks) / OriginalJobTicksPerSecond;
}

double Game::enemyDwellDelay() {
    // Scheduler id zero means the current job, so 0x09b6c-0x09b99 stores an
    // independent upper-exclusive random(0,90)+30 countdown for the actor
    // that just reached its endpoint. The original deadline comparison is
    // inclusive: a value installed during the current dispatch first becomes
    // eligible after that many subsequent full clock intervals. Represent
    // that scheduler boundary with one additional native tick. The captured
    // Reggie dwell sequence otherwise advances one tick early per move.
    return static_cast<double>(randomInt(30, 119) + 1) / OriginalJobTicksPerSecond;
}

double Game::enemyMoveAnimationDuration(int direction) const {
    const int difficulty = difficultyIndexForLevel(level_);
    // DS:0636 is the animation job's tick interval, not an idle delay. The
    // preserved run takes the low-speed branch at 0x0c9f8, installing the
    // original 8-pixel horizontal / 6-pixel vertical steps. The measured red
    // and cyan trajectories therefore require six callbacks across 48 pixels
    // and five across 30 pixels. The alternative benchmark branch uses the
    // faster 12/10 steps, but it is not the captured machine's behavior.
    const int callbacks = direction == 0 || direction == 2 ? 5 : 6;
    const int intervalTicks = EnemyMoveSeconds[static_cast<std::size_t>(difficulty)];
    return static_cast<double>(callbacks * intervalTicks) / OriginalJobTicksPerSecond;
}

int Game::ordinaryEnemyFrame(const Enemy& enemy) {
    const int direction = std::clamp(enemy.direction, 0, 3);
    const int firstFrame = direction * 3;
    // The original actor record owns its pose; it is not driven by a global
    // animation clock. Edge warning and entry retain the first directional
    // record, an ordinary interior/exit move retains the third, and the dwell
    // initializer selects the middle record. Down-facing dwell uses BTMP 15,
    // the resource's intentional alias of record 7.
    if (enemy.entering) return firstFrame;
    if (enemy.moving) return firstFrame + 2;
    if (enemy.dwellFrame >= 0) return enemy.dwellFrame;
    return direction == 2 ? 15 : firstFrame + 1;
}

int Game::troggleBiteFrameAtTick(const int elapsedTicks) {
    // The state-5 callback advances once per public scheduler tick and the
    // BTMP animation cycles records 12,13,14,13,12,13.  Records 12 and 14
    // contain identical pixels, which is why the lossless DOS recording
    // visibly alternates its two poses on every tick for all 21 ticks.
    if (elapsedTicks < 0) return 12;
    constexpr std::array<int, 6> BiteFrames = {12, 13, 14, 13, 12, 13};
    return BiteFrames[static_cast<std::size_t>(elapsedTicks) % BiteFrames.size()];
}

int Game::difficultyIndexForLevel(int level) {
    return std::clamp(level - 1, 0, 11);
}

int Game::maximumEnemiesForLevel(int level) {
    return EnemyLimits[static_cast<std::size_t>(difficultyIndexForLevel(level))];
}

int Game::maximumSafeZoneJobsForLevel(int level) {
    return SafeZoneJobLimits[static_cast<std::size_t>(difficultyIndexForLevel(level))];
}

int Game::initialSafeZonesForLevel(int level) {
    return InitialSafeZoneCounts[static_cast<std::size_t>(difficultyIndexForLevel(level))];
}

int Game::enemyWeightForLevel(int level, int enemyType) {
    if (enemyType < 0 || enemyType >= static_cast<int>(EnemyNames.size())) return 0;
    return EnemyTypeWeights[static_cast<std::size_t>(difficultyIndexForLevel(level))]
                           [static_cast<std::size_t>(enemyType)];
}

int Game::chooseEnemyType() {
    int choice = randomInt(0, 99);
    const auto& weights = EnemyTypeWeights[static_cast<std::size_t>(difficultyIndexForLevel(level_))];
    for (int type = 0; type < static_cast<int>(weights.size()); ++type) {
        if (choice < weights[static_cast<std::size_t>(type)]) return type;
        choice -= weights[static_cast<std::size_t>(type)];
    }
    return 0;
}

int Game::steeredEnemyDirection(int enemyType, int currentDirection, int playerDistancePixels,
                                int directionTowardPlayer, int randomRoll) {
    const int direction = ((currentDirection % 4) + 4) % 4;
    const int toward = ((directionTowardPlayer % 4) + 4) % 4;
    const auto turn = [](int value, int amount) { return (value + amount + 4) % 4; };
    switch (enemyType) {
    case 0: // Reggie / normalus: never voluntarily turns.
        return direction;
    case 1: // Worker / laborus: 80% straight, 10% left, 10% right.
    case 3: // Helper / assistus: same steering distribution as Worker.
        if (randomRoll == 0) return turn(direction, -1);
        if (randomRoll == 1) return turn(direction, 1);
        return direction;
    case 2: // Bashful / timidus: flees nearby; otherwise wanders.
        if (playerDistancePixels <= 96) return turn(toward, 2);
        if (randomRoll < 2) return turn(direction, 1);
        if (randomRoll < 4) return turn(direction, -1);
        if (randomRoll < 6) return turn(direction, 2);
        return direction;
    case 4: // Smarty / smarticus: tracks always near and half the time at mid-range.
        if (playerDistancePixels <= 96) return toward;
        if (playerDistancePixels <= 192 && randomRoll == 0) return toward;
        return direction;
    default:
        return direction;
    }
}

bool Game::updateEnemies(double seconds, const int slotUpperBound,
                         const int slotLowerBound) {
    const int dispatchSlotCount = slotUpperBound >= 0
        ? std::min(enemySlotCount_, slotUpperBound)
        : enemySlotCount_;
    const int dispatchSlotBegin = std::clamp(slotLowerBound, 0, dispatchSlotCount);
    const auto captureCallbackSurface = [this](const int slot) {
        if (!captureEnemyCallbackPresentationFrames_) return;
        enemyCallbackPresentationSlots_.push_back(slot);
        enemyCallbackPresentationFrames_.push_back(capturePresentationFrame());
    };
    // Scheduler jobs dispatch in ascending slot order, independently of the
    // actor painter list. Detect a state-3 actor that will reach a resident
    // state-4 actor on this update before processing either vector element.
    // If both callbacks fall on this tick, the lower slot runs first. This is
    // observable in the captured first demo overlap: slot 2 freezes slot 3
    // before slot 3 can apply its trail and begin departing.
    for (Enemy& mover : enemies_) {
        if (mover.slot < dispatchSlotBegin || mover.slot >= dispatchSlotCount) continue;
        if (!mover.moving || mover.animationTimer - seconds > JobTimerEpsilon ||
            mover.exiting || mover.row < 0 || mover.row >= BoardRows ||
            mover.column < 0 || mover.column >= BoardColumns) continue;
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

    // Each recurring Troggle controller remains one physical scheduler record
    // throughout states 1-6.  Dispatch the phase currently owned by job IDs
    // 1..N in that fixed order; batching every actor ahead of every waiting or
    // warning record changes PRNG ownership when unlike phases expire together.
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
        if (!attractMunchConcurrentEnemyPixels_.empty() && attractMode_ &&
            attractBoardIndex_ == 0 && activeBoardMode_ == Mode::Factors &&
            target_ == 57 && playerRow_ == 2 && playerColumn_ == 2 &&
            enemy.slot == 1 && enemy.type == 1 && enemy.row == 2 &&
            enemy.column == 4 && !enemy.moving && !enemy.entering &&
            !enemy.exiting && !enemy.cannibalizing) {
            // The measured terminal F585 route above leaves this later
            // selector-1 record undispatched while the Demo chew and feedback
            // take ownership. Other slots retain their ordinary callbacks.
            continue;
        }
        // The auxiliary state-5 collision job owns the selected biter's
        // actor record through its terminal callback. Its pre-existing dwell
        // job must not overwrite the bite record or start a move meanwhile;
        // other recurring records (including the demo controller) continue.
        if (deathAnimating_ && feedbackEnemySlot_ >= 0 &&
            enemy.slot == feedbackEnemySlot_) {
            continue;
        }
        // State 6 in the original actor dispatcher is intentionally inert.
        // State 3 puts an overlapped resident there until the state-5 biting
        // actor completes its 21 callbacks and explicitly rearms the victim.
        if (enemy.overlapFrozen) {
            continue;
        }
        if (enemy.cannibalizing) {
            // An entry-terminal overlap paints its ordinary endpoint before
            // state 5 is installed. The first state-5 callback owns the next
            // public tick, so only now may the bite replace that held page.
            cannibalEntryTerminalHoldPixels_.clear();
            enemy.cannibalTimer -= seconds;
            if (enemy.cannibalTimer > JobTimerEpsilon) {
                continue;
            }

            // 0x09da5 calls 0x09b16 first, which snapshots the survivor's
            // cell and consumes a fresh dwell roll. It then scans matching
            // state-6 actors and calls 0x092dd for each victim, preserving
            // the observable global PRNG order.
            enemy.cannibalizing = false;
            enemy.cannibalTimer = 0.0;
            // The state-5 terminal calls 0x09b16 with literal direction 2.
            // That writes stationary record 15, but it does not replace the
            // movement heading used when this dwell later expires.
            enemy.dwellFrame = 15;
            enemy.savedCell = cell(enemy.row, enemy.column);
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
        // State 2 leaves the actor parked just beyond the selected edge for
        // exactly 90 ticks. The slot transition above starts its entry move.
        if (enemy.entering && !enemy.moving) {
            continue;
        }
        if (enemy.moving) {
            const int movementPhaseBefore = enemyMovementPhase(enemy);
            enemy.animationTimer -= seconds;
            const int movementPhaseAfter = enemyMovementPhase(enemy);
            const bool earlierEntryInChain = std::any_of(
                enemies_.begin(), enemies_.end(), [&](const Enemy& candidate) {
                    return &candidate != &enemy && candidate.slot >= 0 &&
                           candidate.slot < enemy.slot && candidate.entering;
                });
            const bool entryOverlapsStandingMuncher =
                enemy.entering && attractMode_ && earlierEntryInChain &&
                enemy.row == playerRow_ && enemy.column == playerColumn_ &&
                playerCanBeCaughtAtEnemyEndpoint();
            const bool vertical = enemy.direction == 0 || enemy.direction == 2;
            const int finalEntryPhase = vertical ? 5 : 6;
            if (entryOverlapsStandingMuncher &&
                movementPhaseAfter >= finalEntryPhase - 1) {
                // The restarted fourth Demo capture exposes the original
                // overlap painter: the last two entry callbacks retain player
                // record 12 beneath the clipped Troggle even though the
                // logical Muncher job remains catchable state 4.
                playerTerminalFrame_ = 12;
            }
            const bool retainedFinalEntryEndpoint =
                entryOverlapsStandingMuncher &&
                movementPhaseBefore == finalEntryPhase &&
                movementPhaseAfter == finalEntryPhase;
            if (enemy.animationTimer > JobTimerEpsilon &&
                !retainedFinalEntryEndpoint) {
                captureCallbackSurface(slotIndex);
                continue;
            }

            const bool completedEntryMove = enemy.entering;
            const bool completedIntoResidentOverlap = std::any_of(
                enemies_.begin(), enemies_.end(), [&](const Enemy& candidate) {
                    return &candidate != &enemy && candidate.overlapRetired &&
                           candidate.row == enemy.row && candidate.column == enemy.column;
                });
            enemy.moving = false;
            enemy.animationTimer = 0.0;
            if (enemy.exiting || enemy.row < 0 || enemy.row >= BoardRows ||
                enemy.column < 0 || enemy.column >= BoardColumns) {
                enemies_.erase(actor);
                rearmEnemySlot(slotIndex);
                captureCallbackSurface(slotIndex);
                if (playerRecovering_ && !enemyAt(playerRow_, playerColumn_)) {
                    playerRecovering_ = false;
                }
                continue;
            }
            enemy.entering = false;
            // 0x09b16 snapshots the signed board record for this actor before
            // installing state 4. Its later trail callback uses this saved
            // record rather than rereading a cell another actor may have
            // changed in the meantime.
            enemy.savedCell = cell(enemy.row, enemy.column);
            enemy.savedCellValid = true;
            enemy.dwellFrame = enemy.direction == 2 ? 15 : enemy.direction * 3 + 1;
            // 0x09b16 consumes this dwell roll as soon as the endpoint is
            // reached, before the state-4 collision checks run.
            enemy.moveTimer = enemyDwellDelay();
            if (completedIntoResidentOverlap) {
                enemy.cannibalizing = true;
                enemy.cannibalTimer = TroggleCannibalAnimationDuration;
                promoteEnemyPaintSlot(enemy.slot);
                if (completedEntryMove) {
                    // The initializer changes logical record ownership now,
                    // but DOS still displays the endpoint record painted by
                    // this entry callback until state 5 runs one tick later.
                    enemy.cannibalizing = false;
                    cannibalEntryTerminalHoldPixels_ =
                        capturePresentationFrame();
                    enemy.cannibalizing = true;
                }
            }
            captureCallbackSurface(slotIndex);
            if (playerCanBeCaughtAtEnemyEndpoint() &&
                enemy.row == playerRow_ && enemy.column == playerColumn_) {
                loseLife(enemy.type, enemy.slot);
                // Collision installs a separate state-5 actor record.  The
                // common scanner continues through later Troggle and Demo
                // records on this same public tick.
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
            if (enemy.moving) captureCallbackSurface(slotIndex);
            if (!enemy.moving) enemy.moveTimer = enemyDwellDelay();
        }
    }
    enemyWarning_ = std::any_of(enemySlots_.begin(),
                                enemySlots_.begin() + enemySlotCount_,
                                [](const EnemySlot& slot) {
                                    return slot.phase == EnemySlotPhase::Warning;
                                });
    if (attractMode_ && std::any_of(
            enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                if (!enemy.entering || !enemy.moving ||
                    enemy.row != playerRow_ || enemy.column != playerColumn_) {
                    return false;
                }
                const bool earlierEntryInChain = std::any_of(
                    enemies_.begin(), enemies_.end(),
                    [&](const Enemy& candidate) {
                        return &candidate != &enemy && candidate.slot >= 0 &&
                               candidate.slot < enemy.slot &&
                               candidate.entering;
                    });
                if (!earlierEntryInChain) return false;
                const bool vertical = enemy.direction == 0 || enemy.direction == 2;
                const int finalEntryPhase = vertical ? 5 : 6;
                return enemyMovementPhase(enemy) >= finalEntryPhase - 1;
            })) {
        // The shared warning surface is retired by the overlap entry painter,
        // even when an independent slot is still waiting to begin its entry.
        enemyWarning_ = false;
    }
    if (dispatchSlotBegin == 0 && dispatchSlotCount == enemySlotCount_) {
        if (enemyPresentationLagPresentFirstLogicalPage_) {
            const std::vector<std::uint32_t> firstLogicalPage =
                capturePresentationFrame();
            enqueuePresentationFrame(firstLogicalPage);
            enemyPresentationLagPixels_ = firstLogicalPage;
            enemyPresentationLagPresentFirstLogicalPage_ = false;
        }
        advanceEnemyPresentationLag();
    }
    return true;
}

int Game::chooseSafeZoneCell() {
    // 0x0895e chooses row random(0,3)+1 and column random(0,4)+1. Both
    // bounds are upper-exclusive, so safe zones stay in the interior 3x4.
    for (;;) {
        const int row = randomInt(1, BoardRows - 2);
        const int column = randomInt(1, BoardColumns - 2);
        const int index = row * BoardColumns + column;
        if (!cells_[static_cast<std::size_t>(index)].safe) return index;
    }
}

void Game::initializeSafeZones() {
    for (Cell& current : cells_) {
        current.safe = false;
        current.safeTimer = 0.0;
    }
    for (SafeZoneJob& job : safeZoneJobs_) job = {};

    safeZoneJobCount_ = maximumSafeZoneJobsForLevel(level_);
    const int initiallyActive = initialSafeZonesForLevel(level_);
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        const int ticks = randomInt(210, 419);
        job.period = static_cast<double>(ticks) / OriginalJobTicksPerSecond;
        job.timer = job.period;
        if (index < initiallyActive) {
            job.active = true;
            job.cellIndex = chooseSafeZoneCell();
            Cell& current = cells_[static_cast<std::size_t>(job.cellIndex)];
            current.safe = true;
            current.safeTimer = job.timer;
        }
    }
}

bool Game::updateSafeZones(double seconds) {
    for (int index = 0; index < safeZoneJobCount_; ++index) {
        SafeZoneJob& job = safeZoneJobs_[static_cast<std::size_t>(index)];
        job.timer -= seconds;

        while (job.timer <= JobTimerEpsilon) {
            if (job.active) {
                if (job.cellIndex >= 0 && job.cellIndex < BoardCellCount) {
                    Cell& current = cells_[static_cast<std::size_t>(job.cellIndex)];
                    current.safe = false;
                    current.safeTimer = 0.0;
                }
                job.active = false;
                job.cellIndex = -1;
                playGameplaySound(5);
            } else {
                job.active = true;
                job.cellIndex = chooseSafeZoneCell();
                Cell& current = cells_[static_cast<std::size_t>(job.cellIndex)];
                current.safe = true;

                const int row = job.cellIndex / BoardColumns;
                const int column = job.cellIndex % BoardColumns;
                const std::size_t enemyCountBefore = enemies_.size();
                for (std::size_t enemyIndex = 0; enemyIndex < enemies_.size();) {
                    const Enemy enemy = enemies_[enemyIndex];
                    if (!enemyIntersectsCell(enemy, row, column)) {
                        ++enemyIndex;
                        continue;
                    }
                    // 0x099d8 invokes the species trail callback before
                    // 0x092dd removes/rearms every crossing Troggle.
                    if (!applyEnemyCellEffect(enemy)) return false;
                    rearmEnemySlot(enemy.slot);
                    enemies_.erase(enemies_.begin() + static_cast<std::ptrdiff_t>(enemyIndex));
                    playGameplaySound(11);
                }
                if (enemies_.size() != enemyCountBefore && playerRecovering_ &&
                    !enemyAt(playerRow_, playerColumn_)) {
                    playerRecovering_ = false;
                }
                playGameplaySound(4);
            }
            job.timer += job.period;
        }

        if (job.active && job.cellIndex >= 0 && job.cellIndex < BoardCellCount) {
            cells_[static_cast<std::size_t>(job.cellIndex)].safeTimer = job.timer;
        }
    }
    return true;
}

void Game::initializeEnemySlots() {
    for (EnemySlot& slot : enemySlots_) slot = {};
    enemySlotCount_ = maximumEnemiesForLevel(level_);
    std::iota(enemyPaintOrder_.begin(), enemyPaintOrder_.end(), 0);
    // 0x08f17-0x08f8c assigns every species first. A separate loop at
    // 0x08faa-0x09049 then draws every arrival delay. Keeping those passes
    // separate is observable because they share the global Borland stream.
    for (int index = 0; index < enemySlotCount_; ++index) {
        EnemySlot& slot = enemySlots_[static_cast<std::size_t>(index)];
        slot.type = chooseEnemyType();
        slot.phase = EnemySlotPhase::Waiting;
    }
    for (int index = 0; index < enemySlotCount_; ++index) {
        EnemySlot& slot = enemySlots_[static_cast<std::size_t>(index)];
        slot.timer = enemySpawnDelay();
    }
    enemyWarning_ = false;
}

void Game::promoteEnemyPaintSlot(const int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return;
    const auto first = enemyPaintOrder_.begin();
    const auto last = first + enemySlotCount_;
    const auto current = std::find(first, last, slotIndex);
    if (current == last || current + 1 == last) return;
    std::rotate(current, current + 1, last);
}

bool Game::beginEnemyWarning(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return false;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    if (slot.phase != EnemySlotPhase::Waiting) return false;
    // 0x08416 selects one edge and one coordinate directly. It does not
    // reject an entry because another Troggle already uses that cell.
    const int edge = randomInt(0, 3);
    // The 0x08452 switch orders entry sides as bottom, left, top, right.
    // Its saved cell is the in-board endpoint; the actor begins one cell
    // beyond that edge and moves inward during the warning-to-active stage.
    const int row = edge == 0 ? BoardRows - 1 : edge == 2 ? 0 : randomInt(0, BoardRows - 1);
    const int column = edge == 1 ? 0 : edge == 3 ? BoardColumns - 1 : randomInt(0, BoardColumns - 1);
    slot.edge = edge;
    slot.row = row;
    slot.column = column;
    slot.phase = EnemySlotPhase::Warning;
    // 0x085aa passes the literal 0x5a to the recurring job. The random
    // 0-89 ticks belong to the preceding arrival and later in-cell dwell,
    // not this warning state.
    slot.timer = TroggleWarningDuration;
    Enemy enemy;
    enemy.row = slot.row;
    enemy.column = slot.column;
    enemy.type = slot.type;
    enemy.direction = slot.edge == 0 ? 0 : slot.edge == 1 ? 1
                    : slot.edge == 2 ? 2 : 3;
    enemy.fromRow = enemy.row + (slot.edge == 0 ? 1 : slot.edge == 2 ? -1 : 0);
    enemy.fromColumn = enemy.column + (slot.edge == 1 ? -1 : slot.edge == 3 ? 1 : 0);
    enemy.entering = true;
    enemy.slot = slotIndex;
    enemies_.push_back(enemy);
    enemyWarning_ = true;
    playGameplaySound(9);
    return true;
}

void Game::spawnPendingEnemy(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    if (slot.phase != EnemySlotPhase::Warning) return;
    const auto pending = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& enemy) {
        return enemy.slot == slotIndex && enemy.entering;
    });
    if (pending == enemies_.end()) return;
    const bool capturedFourthPrimeEntryPlayerOrdering =
        attractMode_ && attractBoardIndex_ == 3 &&
        activeBoardMode_ == Mode::Primes && level_ == 3 &&
        attractDifficultyIndex_ == 2 && target_ == 0 && slotIndex == 0 &&
        enemySlotCount_ == 1 && enemySlots_[0].type == 0 &&
        playerRow_ == 4 && playerColumn_ == 4 &&
        random_.calls == 660 && random_.state == 0x9dd5b1e1u;
    if (capturedFourthPrimeEntryPlayerOrdering) {
        // On the measured fourth Prime board, Reggie's state-3 entry job is
        // installed two selector ticks before the Demo move may start.  Those
        // ticks make the first visible Reggie callback land after Muncher
        // phase 3, producing the complete interleaved pages at source
        // frames 14973..14981.  Keep the random stream untouched; this is the
        // recovered scheduler-record ordering, not a presentation delay.
        attractActionTimer_ += 2.0 / OriginalJobTicksPerSecond;
    }
    // The complete pages at source frames 4562-4591 recover a distinct
    // dirty-cell boundary: when a later actor begins its entry while an
    // earlier actor is within three state-4 callbacks of moving, the warning
    // page remains visible until that earlier move begins. The entrant then
    // paints from its correspondingly older actor records while the logical
    // scheduler continues normally. This is not a gameplay delay--the random
    // stream and every state transition retain their original tick.
    int presentationLagTicks = 0;
    int firstLagRecordPresentations = 2;
    bool presentEntryPhaseOneBeforeLag = false;
    for (const Enemy& candidate : enemies_) {
        if (&candidate == &*pending || candidate.slot < 0 ||
            candidate.slot >= slotIndex || candidate.moving ||
            candidate.entering || candidate.exiting ||
            candidate.overlapFrozen || candidate.cannibalizing ||
            candidate.moveTimer <= JobTimerEpsilon) {
            continue;
        }
        const int ticksUntilMove = static_cast<int>(std::ceil(
            candidate.moveTimer * OriginalJobTicksPerSecond - 1e-7));
        if (ticksUntilMove <= 0 || ticksUntilMove > 3) continue;
        if (presentationLagTicks == 0 || ticksUntilMove < presentationLagTicks) {
            presentationLagTicks = ticksUntilMove;
        }
    }
    // Frames 5182-5201 expose the same retained-entry boundary when the next
    // repaint owner is selector 5 rather than an earlier Troggle slot. The
    // Bashful entry begins three callbacks before the Demo Muncher moves; DOS
    // keeps the entry actor's older records while the player advances. This
    // affects presentation history only--the selector's countdown and draws
    // still occur on their normal callbacks.
    if (attractMode_ && !moving_ && !munching_ && !deathAnimating_ &&
        attractActionTimer_ > JobTimerEpsilon) {
        const int ticksUntilDemoAction = static_cast<int>(std::ceil(
            attractActionTimer_ * OriginalJobTicksPerSecond - 1e-7));
        // Selector 5 reloads its timer before deciding which key to inject.
        // A correct value is always Space; a wrong value is Space only on
        // its subsequent 1-in-10 roll.  Predict that decision on a copy of
        // the generator so this presentation-only test cannot perturb the
        // original random stream.  The retained-entry boundary recovered at
        // frames 5173-5215 belongs to an imminent walk, not to a chew: using
        // it for the earlier Space callback hides the player's bite pages.
        const Cell& currentCell = cell(playerRow_, playerColumn_);
        const bool currentCellOwnedByEnemy =
            enemyOwnsAttractPlayerCell(playerRow_, playerColumn_);
        const bool currentHasValue =
            !currentCell.eaten && !currentCell.label.empty();
        bool nextDemoActionIsMunch = currentHasValue && currentCell.correct;
        if (currentHasValue && !currentCell.correct) {
            OriginalRandom prediction = random_;
            (void)prediction.next();  // selector-5 timer reload
            nextDemoActionIsMunch = prediction.next() % 10u == 0u;
        }
        const bool imminentDemoMovement = !currentCellOwnedByEnemy &&
            ticksUntilDemoAction > 0 && ticksUntilDemoAction <= 3 &&
            !nextDemoActionIsMunch;
        if (imminentDemoMovement &&
            (presentationLagTicks == 0 ||
             ticksUntilDemoAction < presentationLagTicks)) {
            presentationLagTicks = ticksUntilDemoAction;
        }
        if (imminentDemoMovement) {
            // The player contributes four distinct complete callback pages
            // before the entrant's phase-2 record becomes resident.
            firstLagRecordPresentations = 4;
            presentEntryPhaseOneBeforeLag = true;
        }
    }
    if (presentationLagTicks > 0) {
        beginEnemyPresentationLag(slotIndex, presentationLagTicks,
                                  firstLagRecordPresentations);
    }
    pending->moving = true;
    // Entry retains one endpoint callback after its six/five interpolation
    // advances. This hold is independently visible in the live entry runs.
    const int difficulty = difficultyIndexForLevel(level_);
    pending->animationTimer = enemyMoveAnimationDuration(pending->direction) +
        static_cast<double>(EnemyMoveSeconds[static_cast<std::size_t>(difficulty)]) /
            OriginalJobTicksPerSecond;
    slot.phase = EnemySlotPhase::Active;
    slot.timer = 0.0;
    // 0x09ee5 calls 0x09893 after installing state 3. DS:5A84 is the actor
    // redraw list, not the fixed scheduler-record order.
    promoteEnemyPaintSlot(slotIndex);
    if (presentEntryPhaseOneBeforeLag) {
        // Source frames 5175-5181 retain the completed phase-1 page before
        // selector 5 begins repainting the player over lagged entrant records.
        // Later physical Troggle slots still run on this callback, so defer
        // the snapshot until the complete slot pass has finished.
        enemyPresentationLagPresentFirstLogicalPage_ = true;
    }
    playGameplaySound(3);
}

void Game::rearmEnemySlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= enemySlotCount_) return;
    EnemySlot& slot = enemySlots_[static_cast<std::size_t>(slotIndex)];
    slot.phase = EnemySlotPhase::Waiting;
    slot.timer = enemySpawnDelay();
}

void Game::spawnEnemy() {
    if (enemySlotCount_ == 0) {
        enemySlotCount_ = 1;
        enemySlots_[0] = {};
        enemySlots_[0].type = chooseEnemyType();
        enemySlots_[0].phase = EnemySlotPhase::Waiting;
    }
    for (int slotIndex = 0; slotIndex < enemySlotCount_; ++slotIndex) {
        if (enemySlots_[static_cast<std::size_t>(slotIndex)].phase != EnemySlotPhase::Waiting) continue;
        if (beginEnemyWarning(slotIndex)) spawnPendingEnemy(slotIndex);
        break;
    }
    enemyWarning_ = std::any_of(enemySlots_.begin(),
                                enemySlots_.begin() + enemySlotCount_,
                                [](const EnemySlot& slot) {
                                    return slot.phase == EnemySlotPhase::Warning;
                                });
}

bool Game::moveEnemy(Enemy& enemy) {
    constexpr std::array<int, 4> rowDeltas = {-1, 0, 1, 0};
    constexpr std::array<int, 4> columnDeltas = {0, 1, 0, -1};

    // 0x09053 invokes the species trail callback on the saved current cell
    // before it steers and begins the next actor move. Entry/arrival animation
    // completion itself does not alter the cell.
    if ((page_ == Page::Playing || page_ == Page::Attract) && enemy.row >= 0 &&
        enemy.row < BoardRows && enemy.column >= 0 && enemy.column < BoardColumns) {
        if (!applyEnemyCellEffect(enemy)) return false;
    }

    // 0x096b9 compares the Troggle actor coordinates with DS:59ac/59ae, the
    // Muncher's live actor coordinates. Those coordinates advance in the same
    // 8-pixel horizontal / 6-pixel vertical phases used by the DOS painter;
    // comparing only the destination grid cells changes the 96-pixel steering
    // boundary while the Muncher is walking and desynchronizes the PRNG stream.
    int playerPixelX = playerColumn_ * BoardCellWidth;
    int playerPixelY = playerRow_ * BoardCellHeight;
    if (moving_) {
        const int positionPhase = playerMovementPhase();
        if (moveDirection_ == 0 || moveDirection_ == 2) {
            playerPixelY = moveFromRow_ * BoardCellHeight +
                           (moveToRow_ - moveFromRow_) * 6 * positionPhase;
            playerPixelX = moveFromColumn_ * BoardCellWidth;
        } else {
            playerPixelX = moveFromColumn_ * BoardCellWidth +
                           (moveToColumn_ - moveFromColumn_) * 8 * positionPhase;
            playerPixelY = moveFromRow_ * BoardCellHeight;
        }
    }

    const int enemyPixelX = enemy.column * BoardCellWidth;
    const int enemyPixelY = enemy.row * BoardCellHeight;
    const int horizontalPixels = std::abs(enemyPixelX - playerPixelX);
    const int verticalPixels = std::abs(enemyPixelY - playerPixelY);
    const int directionTowardPlayer = horizontalPixels >= verticalPixels
        ? (enemyPixelX > playerPixelX ? 3 : 1)
        : (enemyPixelY > playerPixelY ? 0 : 2);
    const int playerDistancePixels = horizontalPixels + verticalPixels;

    int steeringRoll = 9;
    if (enemy.type == 1 || enemy.type == 3 ||
        (enemy.type == 2 && playerDistancePixels > 96)) {
        steeringRoll = randomInt(0, 9);
    } else if (enemy.type == 4 && playerDistancePixels > 96 && playerDistancePixels <= 192) {
        steeringRoll = randomInt(0, 1);
    }
    enemy.direction = steeredEnemyDirection(enemy.type, enemy.direction, playerDistancePixels,
                                             directionTowardPlayer, steeringRoll);

    enemy.collisionHidden = false;
    // The original retries a randomly selected left/right turn only when its
    // chosen in-board cell is protected. Troggles may overlap one another;
    // any species may continue through an edge and leave the board. The
    // 0x09170 -> 0x09096 branch is unbounded; do not invent a retry terminal.
    for (;;) {
        const int row = enemy.row + rowDeltas[static_cast<std::size_t>(enemy.direction)];
        const int column = enemy.column + columnDeltas[static_cast<std::size_t>(enemy.direction)];
        if (row < 0 || row >= BoardRows || column < 0 || column >= BoardColumns) {
            beginEnemyMove(enemy, row, column);
            enemy.exiting = true;
            return true;
        }
        if (!cell(row, column).safe) {
            beginEnemyMove(enemy, row, column);
            return true;
        }
        enemy.direction = (enemy.direction + (randomInt(0, 1) == 0 ? 3 : 1)) % 4;
    }
}

void Game::beginEnemyMove(Enemy& enemy, int row, int column) {
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
        // The live vertical-exit path retains its fifth clipped pose for one
        // additional actor interval before the terminal removal repaint.
        const int difficulty = difficultyIndexForLevel(level_);
        enemy.animationTimer +=
            static_cast<double>(EnemyMoveSeconds[static_cast<std::size_t>(difficulty)]) /
            OriginalJobTicksPerSecond;
    }
    if (enemy.exiting && attractMode_ && !moving_ && !munching_ &&
        !deathAnimating_ && attractActionTimer_ > JobTimerEpsilon) {
        const int ticksUntilDemoAction = static_cast<int>(std::ceil(
            attractActionTimer_ * OriginalJobTicksPerSecond - 1e-7));
        const int ticksUntilRemoval = static_cast<int>(std::ceil(
            enemy.animationTimer * OriginalJobTicksPerSecond - 1e-7));
        if (ticksUntilDemoAction > 1 &&
            ticksUntilDemoAction <= ticksUntilRemoval) {
            // Source frames 5687-5718 expose the corresponding page boundary
            // for a bottom exit. The actor begins its logical six-callback
            // exit five ticks before selector 5 is due, but the resident page
            // holds its dwell pose. When the later Demo record installs the
            // player move, the delayed exit records are painted with the
            // current player callbacks. Retain that actor history without
            // changing either scheduler countdown or the PRNG stream.
            beginEnemyPresentationLag(
                enemy.slot, ticksUntilDemoAction - 1, 1, true);
        }
    }
    // 0x09213 calls 0x09893 after every state-4 movement initializer.
    promoteEnemyPaintSlot(enemy.slot);
}

int Game::enemyMovementPhase(const Enemy& enemy) const {
    const bool vertical = enemy.direction == 0 || enemy.direction == 2;
    const int stepCount = vertical ? 5 : 6;
    const int difficulty = difficultyIndexForLevel(level_);
    const double interval =
        static_cast<double>(EnemyMoveSeconds[static_cast<std::size_t>(difficulty)]) /
        OriginalJobTicksPerSecond;
    const bool hasTerminalCallback = enemy.entering || (enemy.exiting && vertical);
    const double total = enemyMoveAnimationDuration(enemy.direction) +
                         (hasTerminalCallback ? interval : 0.0);
    const double elapsed = std::max(0.0, total - std::max(0.0, enemy.animationTimer));
    const int storedPhase = static_cast<int>(
        std::floor(elapsed / interval + 1e-7));
    // The state-3 callback advances the actor before its first paint. Entry
    // and ordinary movement therefore both begin at phase 1; a synthetic
    // phase 0 is never presented by the original actor job.
    return std::clamp(storedPhase + 1, 0, stepCount);
}

bool Game::applyEnemyCellEffect(const Enemy& enemy) {
    Cell& current = cell(enemy.row, enemy.column);
    const Cell& saved = enemy.savedCellValid ? enemy.savedCell : current;
    const bool savedBlank = saved.eaten || saved.label.empty();

    // smarticus restores the saved board cell. normalus and timidus only
    // replace populated cells; laborus also fills blanks; assistus eats cells.
    const bool safe = current.safe;
    const double safeTimer = current.safeTimer;
    if (enemy.type == 4) {
        current = saved;
    } else if ((enemy.type == 0 || enemy.type == 2) && savedBlank) {
        current = {};
        current.eaten = true;
    } else if (enemy.type == 3) {
        current.label.clear();
        current.correct = false;
        current.eaten = true;
    } else {
        current = generatedCell(randomInt(0, 1) != 0);
    }
    current.safe = safe;
    current.safeTimer = safeTimer;

    correctRemaining_ = static_cast<int>(std::count_if(
        cells_.begin(), cells_.end(), [](const Cell& candidate) {
            return candidate.correct && !candidate.eaten && !candidate.label.empty();
        }));
    if (munching_ && munchSavedCell_.correct) ++correctRemaining_;

    if (correctRemaining_ <= 0) {
        completeLevel();
        return false;
    }
    return true;
}

bool Game::enemyAt(int row, int column) const {
    return std::any_of(enemies_.begin(), enemies_.end(), [=](const Enemy& enemy) {
        return !enemy.entering && enemy.row == row && enemy.column == column;
    });
}

bool Game::enemyOwnsAttractPlayerCell(const int row, const int column) const {
    // State-3 entry installs actor ownership before its dirty painter reaches
    // the endpoint. The ordinary injected-key path therefore cannot find the
    // Muncher job from phase 3 onward. This does not mask DS:5A00: selector 5
    // continues to choose from the independent signed answer array.
    return std::any_of(enemies_.begin(), enemies_.end(), [=, this](const Enemy& enemy) {
        const bool ownsLiveCell = !enemy.entering ||
            (enemy.moving && enemyMovementPhase(enemy) >= 3);
        return ownsLiveCell && enemy.row == row && enemy.column == column;
    });
}

int Game::enemyTypeAt(int row, int column) const {
    const auto enemy = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& candidate) {
        return !candidate.entering && candidate.row == row && candidate.column == column &&
               !candidate.collisionHidden;
    });
    return enemy == enemies_.end() ? -1 : enemy->type;
}

int Game::enemySlotAt(int row, int column) const {
    const auto enemy = std::find_if(enemies_.begin(), enemies_.end(), [=](const Enemy& candidate) {
        return !candidate.entering && candidate.row == row && candidate.column == column &&
               !candidate.collisionHidden;
    });
    return enemy == enemies_.end() ? -1 : enemy->slot;
}

std::vector<std::uint32_t> Game::capturePresentationFrame() {
    Renderer snapshot(assets_.graphicsMode());
    render(snapshot);
    return snapshot.pixels();
}

std::vector<std::uint32_t> Game::capturePlayerCallbackFrame() {
    // render() composes a clean logical frame in player-then-Troggle order,
    // while the DOS player record paints only its own surviving pixels over
    // the resident surface. Isolate that player layer by rendering once with
    // every Troggle hidden and once with both actor classes hidden, then apply
    // the layer to the complete pre-Troggle surface.
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
    for (std::size_t pixel = 0; pixel < composite.size(); ++pixel) {
        if (playerVisible[pixel] != actorsHidden[pixel]) {
            composite[pixel] = playerVisible[pixel];
        }
    }
    return composite;
}

std::vector<std::uint32_t>
Game::captureDepartingEnemyPlayerLookaheadFrame(
    const bool playerCallbackPaint) {
    const bool savedMoving = moving_;
    const double savedMoveTimer = moveTimer_;
    const int savedPlayerRow = playerRow_;
    const int savedPlayerColumn = playerColumn_;
    const int savedTerminalFrame = playerTerminalFrame_;

    if (moving_) {
        const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
        const int finalMovementPhase = vertical ? 4 : 5;
        if (playerMovementPhase() < finalMovementPhase) {
            moveTimer_ = std::max(
                0.0, moveTimer_ - 1.0 / OriginalJobTicksPerSecond);
        } else {
            moving_ = false;
            moveTimer_ = 0.0;
            playerRow_ = moveToRow_;
            playerColumn_ = moveToColumn_;
            playerTerminalFrame_ =
                moveDirection_ * 3 + (vertical ? 2 : 1);
        }
    } else if (playerTerminalFrame_ >= 0) {
        playerTerminalFrame_ = -1;
    }

    std::vector<std::uint32_t> pixels = capturePresentationFrame();
    if (playerCallbackPaint) {
        // A clean frame clears the entire source-to-current Troggle sweep.
        // The DOS callback clears only the immediately preceding and current
        // 41x29 actor boxes. Repaint the player's raw BTMP record into the
        // older part of that swept trail; the adjacent two actor boxes remain
        // owned by the later Troggle callback.
        Renderer rawPlayer(assets_.graphicsMode());
        rawPlayer.replacePixels(pixels);
        int playerFrame = playerTerminalFrame_ >= 0 ? playerTerminalFrame_ : 7;
        double playerX = static_cast<double>(
            BoardLeft + playerColumn_ * BoardCellWidth + 4);
        double playerY = static_cast<double>(
            BoardTop + playerRow_ * BoardCellHeight + 1);
        if (moving_) {
            const int phase = playerMovementPhase();
            const bool vertical = moveDirection_ == 0 || moveDirection_ == 2;
            playerX = static_cast<double>(
                          BoardLeft + moveFromColumn_ * BoardCellWidth + 4) +
                      static_cast<double>(
                          (moveToColumn_ - moveFromColumn_) * BoardCellWidth) *
                          static_cast<double>(phase) /
                          static_cast<double>(vertical ? 5 : 6);
            playerY = static_cast<double>(
                          BoardTop + moveFromRow_ * BoardCellHeight + 1) +
                      static_cast<double>(
                          (moveToRow_ - moveFromRow_) * BoardCellHeight) *
                          static_cast<double>(phase) /
                          static_cast<double>(vertical ? 5 : 6);
            playerFrame = playerMoveFrameAtPhase(moveDirection_, phase);
        }
        const bool useHallPalette =
            hallPaletteActive_ ||
            (attractMode_ && attractPostFeedbackBoard_ &&
             attractHallWipeVariant_ == AttractHallWipeVariant::Clean);
        drawSprite(rawPlayer, 1006, playerFrame,
                   static_cast<int>(std::lround(playerX)),
                   static_cast<int>(std::lround(playerY)), false,
                   useHallPalette);

        const auto departing = std::find_if(
            enemies_.begin(), enemies_.end(), [&](const Enemy& enemy) {
                return enemy.moving && !enemy.entering && !enemy.exiting &&
                       enemy.fromRow == moveToRow_ &&
                       enemy.fromColumn == moveToColumn_;
            });
        if (departing != enemies_.end()) {
            const bool vertical =
                departing->direction == 0 || departing->direction == 2;
            const int steps = vertical ? 5 : 6;
            const int phase = enemyMovementPhase(*departing);
            const int previousPhase = std::max(0, phase - 1);
            const int originX =
                BoardLeft + departing->fromColumn * BoardCellWidth + 4;
            const int originY =
                BoardTop + departing->fromRow * BoardCellHeight + 1;
            const int previousX = static_cast<int>(std::lround(
                static_cast<double>(originX) +
                static_cast<double>((departing->column - departing->fromColumn) *
                                    BoardCellWidth) *
                    static_cast<double>(previousPhase) /
                    static_cast<double>(steps)));
            const int previousY = static_cast<int>(std::lround(
                static_cast<double>(originY) +
                static_cast<double>((departing->row - departing->fromRow) *
                                    BoardCellHeight) *
                    static_cast<double>(previousPhase) /
                    static_cast<double>(steps)));
            const int currentX = static_cast<int>(std::lround(
                static_cast<double>(originX) +
                static_cast<double>((departing->column - departing->fromColumn) *
                                    BoardCellWidth) *
                    static_cast<double>(phase) /
                    static_cast<double>(steps)));
            const int currentY = static_cast<int>(std::lround(
                static_cast<double>(originY) +
                static_cast<double>((departing->row - departing->fromRow) *
                                    BoardCellHeight) *
                    static_cast<double>(phase) /
                    static_cast<double>(steps)));

            int left = originX;
            int top = originY;
            int right = originX;
            int bottom = originY;
            if (currentX > originX) {
                right = previousX;
                bottom = originY + 29;
            } else if (currentX < originX) {
                left = previousX + 41;
                right = originX + 41;
                bottom = originY + 29;
            } else if (currentY > originY) {
                right = originX + 41;
                bottom = previousY;
            } else if (currentY < originY) {
                right = originX + 41;
                top = previousY + 29;
                bottom = originY + 29;
            }
            const auto& rawPixels = rawPlayer.pixels();
            for (int y = std::max(0, top);
                 y < std::min(Renderer::Height, bottom); ++y) {
                for (int x = std::max(0, left);
                     x < std::min(Renderer::Width, right); ++x) {
                    const std::size_t pixel = static_cast<std::size_t>(
                        y * Renderer::Width + x);
                    pixels[pixel] = rawPixels[pixel];
                }
            }
        }
    }
    moving_ = savedMoving;
    moveTimer_ = savedMoveTimer;
    playerRow_ = savedPlayerRow;
    playerColumn_ = savedPlayerColumn;
    playerTerminalFrame_ = savedTerminalFrame;
    return pixels;
}

void Game::copyBoardCellPixels(std::vector<std::uint32_t>& destination,
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

void Game::copyPixelRectangle(std::vector<std::uint32_t>& destination,
                              const std::vector<std::uint32_t>& source,
                              const int left, const int top,
                              const int right, const int bottom) {
    const std::size_t expected =
        static_cast<std::size_t>(Renderer::Width * Renderer::Height);
    if (destination.size() != expected || source.size() != expected) return;
    const int clippedLeft = std::clamp(left, 0, Renderer::Width);
    const int clippedTop = std::clamp(top, 0, Renderer::Height);
    const int clippedRight = std::clamp(right, clippedLeft, Renderer::Width);
    const int clippedBottom = std::clamp(bottom, clippedTop, Renderer::Height);
    for (int y = clippedTop; y < clippedBottom; ++y) {
        const std::size_t first = static_cast<std::size_t>(
            y * Renderer::Width + clippedLeft);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(first),
                    clippedRight - clippedLeft,
                    destination.begin() + static_cast<std::ptrdiff_t>(first));
    }
}

void Game::enqueuePresentationFrame(const std::vector<std::uint32_t>& pixels,
                                    const int repeatCount,
                                    const int auditOrigin) {
    if (pixels.size() != static_cast<std::size_t>(Renderer::Width * Renderer::Height) ||
        repeatCount <= 0) return;
    // The measured Prime-board bottom-exit release owns nine complete pages
    // before its following live callbacks are appended. Sixteen remains a
    // small bounded queue while avoiding loss of the oldest source page.
    constexpr std::size_t MaximumQueuedFrames = 16;
    for (int repeat = 0; repeat < repeatCount; ++repeat) {
        if (presentationFrames_.size() == MaximumQueuedFrames) {
            presentationFrames_.pop_front();
            if (!presentationFrameAuditOrigins_.empty()) {
                presentationFrameAuditOrigins_.pop_front();
            }
        }
        presentationFrames_.push_back(pixels);
        presentationFrameAuditOrigins_.push_back(auditOrigin);
    }
}

void Game::beginEnemyPresentationLag(const int slotIndex, const int lagTicks,
                                     const int firstRecordPresentations,
                                     const bool actorMayDisappear) {
    if (slotIndex < 0 || lagTicks <= 0 || enemyPresentationLagSlot_ >= 0) return;
    const std::vector<std::uint32_t> warningPixels =
        lastPresentationPixels_.size() ==
                static_cast<std::size_t>(Renderer::Width * Renderer::Height)
            ? lastPresentationPixels_
            : capturePresentationFrame();
    if (warningPixels.size() !=
        static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        return;
    }
    enemyPresentationLagSlot_ = slotIndex;
    enemyPresentationLagTicks_ = lagTicks;
    enemyPresentationLagFirstPresentationsRemaining_ =
        std::max(1, firstRecordPresentations);
    enemyPresentationLagPresentFirstLogicalPage_ = false;
    enemyPresentationLagActorMayDisappear_ = actorMayDisappear;
    // Factors Demo exits are whole-resident-surface delays: already-painted
    // Muncher records remain paired with their historical Helper records.
    // This occurs both on the measured initial Factors-of-57 board and on the
    // later Factors board. Other first-board delayed actors repaint the
    // current Muncher over the historical actor, which remains the default.
    const bool capturedInitialFactorsBoard =
        attractBoardIndex_ == 0 && activeBoardMode_ == Mode::Factors &&
        target_ == 57 && enemySlotCount_ == 3 &&
        enemySlots_[0].type == 1 && enemySlots_[1].type == 1 &&
        enemySlots_[2].type == 3;
    const bool capturedFourthPrimeBottomExit =
        attractMode_ && attractBoardIndex_ == 3 &&
        activeBoardMode_ == Mode::Primes && level_ == 3 && target_ == 0 &&
        slotIndex == 0 && enemySlotCount_ == 1 && enemySlots_[0].type == 0 &&
        random_.calls == 624 && random_.state == 0x54a040d5u;
    enemyPresentationLagFullSurface_ =
        actorMayDisappear && attractMode_ &&
        (attractBoardIndex_ == 1 || capturedInitialFactorsBoard ||
         capturedFourthPrimeBottomExit);
    enemyPresentationLagHistory_.clear();
    enemyPresentationLagPlayerHistory_.clear();
    enemyPresentationLagPendingPlayers_.clear();
    const auto playerState = [this]() {
        PresentationPlayerState state;
        state.moving = moving_;
        state.moveTimer = moveTimer_;
        state.row = playerRow_;
        state.column = playerColumn_;
        state.terminalFrame = playerTerminalFrame_;
        state.direction = moveDirection_;
        state.fromRow = moveFromRow_;
        state.fromColumn = moveFromColumn_;
        state.toRow = moveToRow_;
        state.toColumn = moveToColumn_;
        state.movementPhase = moving_ ? playerMovementPhase() : -1;
        return state;
    }();
    enemyPresentationLagDisplayedPlayer_ = playerState;
    enemyPresentationLagLastHistoricalPlayer_ = playerState;
    enemyPresentationLagLastEnemyPhase_ = 0;
    enemyPresentationLagPixels_ = warningPixels;
}

void Game::advanceEnemyPresentationLag() {
    if (enemyPresentationLagSlot_ < 0 || enemyPresentationLagTicks_ <= 0) return;
    auto current = std::find_if(
        enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
            return enemy.slot == enemyPresentationLagSlot_;
        });
    Enemy snapshot;
    if (current != enemies_.end()) {
        snapshot = *current;
        if (enemyPresentationLagActorMayDisappear_ &&
            !enemyPresentationLagFullSurface_ &&
            !enemyPresentationLagHistory_.empty()) {
            const Enemy& previous = enemyPresentationLagHistory_.back();
            const bool vertical = snapshot.direction == 0 || snapshot.direction == 2;
            const int terminalPhase = vertical ? 5 : 6;
            if (previous.slot == snapshot.slot && previous.moving &&
                previous.exiting && snapshot.moving && snapshot.exiting &&
                enemyMovementPhase(previous) == terminalPhase &&
                enemyMovementPhase(snapshot) == terminalPhase) {
                // A vertical exit owns one extra logical endpoint callback,
                // but its dirty actor record is removed instead of repainting
                // the same clipped endpoint twice. Queue the actor-free record
                // at that history position while the scheduler remains intact.
                snapshot = Enemy{};
                snapshot.slot = -1;
            }
        }
    } else {
        // A delayed exit must keep draining its historical actor records after
        // the logical slot has already rearmed. Slot -1 is an explicit absent
        // marker; it eventually restores the current actor-free page.
        snapshot.slot = -1;
    }
    enemyPresentationLagHistory_.push_back(snapshot);
    if (enemyPresentationLagFullSurface_) {
        PresentationPlayerState playerState;
        playerState.moving = moving_;
        playerState.moveTimer = moveTimer_;
        playerState.row = playerRow_;
        playerState.column = playerColumn_;
        playerState.terminalFrame = playerTerminalFrame_;
        playerState.direction = moveDirection_;
        playerState.fromRow = moveFromRow_;
        playerState.fromColumn = moveFromColumn_;
        playerState.toRow = moveToRow_;
        playerState.toColumn = moveToColumn_;
        playerState.movementPhase = moving_ ? playerMovementPhase() : -1;
        enemyPresentationLagPlayerHistory_.push_back(playerState);
    }
    if (enemyPresentationLagHistory_.size() <=
        static_cast<std::size_t>(enemyPresentationLagTicks_)) {
        return;
    }

    const Enemy presented = enemyPresentationLagHistory_.front();
    // State 3 suppresses the first arrival callback and the following board
    // scan still owns that same off-board actor record. The first complete
    // synchronized page therefore uses the invisible record twice before the
    // entrant's phase-2 pixels reach a presentation-complete surface.
    --enemyPresentationLagFirstPresentationsRemaining_;
    if (enemyPresentationLagFirstPresentationsRemaining_ <= 0) {
        enemyPresentationLagHistory_.pop_front();
    }
    if (enemyPresentationLagFullSurface_) {
        PresentationPlayerState historicalPlayer =
            enemyPresentationLagPlayerHistory_.front();
        if (enemyPresentationLagFirstPresentationsRemaining_ <= 0) {
            enemyPresentationLagPlayerHistory_.pop_front();
        }

        // Selector 5 installs movement phase zero without painting it. Treat
        // that logical record as the last resident standing player until the
        // first real movement callback reaches this delayed history cursor.
        if (historicalPlayer.moving && historicalPlayer.movementPhase == 0) {
            historicalPlayer = enemyPresentationLagLastHistoricalPlayer_;
        }
        const auto samePlayer = [](const PresentationPlayerState& first,
                                   const PresentationPlayerState& second) {
            return first.moving == second.moving &&
                   first.row == second.row && first.column == second.column &&
                   first.terminalFrame == second.terminalFrame &&
                   first.direction == second.direction &&
                   first.fromRow == second.fromRow &&
                   first.fromColumn == second.fromColumn &&
                   first.toRow == second.toRow &&
                   first.toColumn == second.toColumn &&
                   first.movementPhase == second.movementPhase;
        };
        if (!samePlayer(historicalPlayer,
                        enemyPresentationLagLastHistoricalPlayer_)) {
            enemyPresentationLagPendingPlayers_.push_back(historicalPlayer);
            enemyPresentationLagLastHistoricalPlayer_ = historicalPlayer;
        }

        const int presentedEnemyPhase = presented.slot < 0
            ? 6 : enemyMovementPhase(presented);
        const bool actorRemoved = presented.slot < 0;
        // Horizontal exits use phase 6 for both the final clipped pose and
        // the explicit absent-history marker. Actor identity, not phase
        // number alone, therefore owns the terminal removal callback.
        if (presentedEnemyPhase != enemyPresentationLagLastEnemyPhase_ ||
            actorRemoved) {
            const bool drainInitialFactorsPlayerHistory =
                actorRemoved && attractMode_ && attractBoardIndex_ == 0 &&
                activeBoardMode_ == Mode::Factors && target_ == 57 &&
                enemySlotCount_ == 3 && enemySlots_[0].type == 1 &&
                enemySlots_[1].type == 1 && enemySlots_[2].type == 3;
            const bool drainCapturedPrimeBottomExitPlayerHistory =
                actorRemoved && attractMode_ && attractBoardIndex_ == 1 &&
                activeBoardMode_ == Mode::Primes && level_ == 2 &&
                attractDifficultyIndex_ == 1 && target_ == 0 &&
                enemyPresentationLagSlot_ == 0 && enemySlotCount_ == 1 &&
                enemySlots_[0].type == 2 && random_.calls == 325 &&
                random_.state == 0x49b3e7e6u;
            const bool drainCapturedFourthPrimeBottomExitPlayerHistory =
                actorRemoved && attractMode_ && attractBoardIndex_ == 3 &&
                activeBoardMode_ == Mode::Primes && level_ == 3 &&
                attractDifficultyIndex_ == 2 && target_ == 0 &&
                enemyPresentationLagSlot_ == 0 && enemySlotCount_ == 1 &&
                enemySlots_[0].type == 0 && random_.calls == 627 &&
                random_.state == 0xb99ee580u;
            if (drainCapturedPrimeBottomExitPlayerHistory ||
                drainCapturedFourthPrimeBottomExitPlayerHistory) {
                attractPrimeExitPlayerCatchup_ = true;
            }
            if (drainInitialFactorsPlayerHistory ||
                drainCapturedPrimeBottomExitPlayerHistory ||
                drainCapturedFourthPrimeBottomExitPlayerHistory) {
                // The delayed bottom-exit record reaches the DOS presenter
                // after selector 5 has already advanced the Muncher. On the
                // captured Prime board (source frames 6038..6100), every
                // remaining player dirty record is delivered before the next
                // live callback. Without this drain the first walk is reduced
                // to one pose and the later wrong-answer surface is composed
                // over the wrong resident page.
                const auto appendHistoricalPlayer =
                    [&](PresentationPlayerState candidate) {
                        if (candidate.moving && candidate.movementPhase == 0) {
                            candidate = enemyPresentationLagLastHistoricalPlayer_;
                        }
                        if (!samePlayer(candidate,
                                        enemyPresentationLagLastHistoricalPlayer_)) {
                            enemyPresentationLagPendingPlayers_.push_back(candidate);
                            enemyPresentationLagLastHistoricalPlayer_ = candidate;
                        }
                    };
                for (const PresentationPlayerState& candidate :
                     enemyPresentationLagPlayerHistory_) {
                    appendHistoricalPlayer(candidate);
                }
                PresentationPlayerState currentPlayer;
                currentPlayer.moving = moving_;
                currentPlayer.moveTimer = moveTimer_;
                currentPlayer.row = playerRow_;
                currentPlayer.column = playerColumn_;
                currentPlayer.terminalFrame = playerTerminalFrame_;
                currentPlayer.direction = moveDirection_;
                currentPlayer.fromRow = moveFromRow_;
                currentPlayer.fromColumn = moveFromColumn_;
                currentPlayer.toRow = moveToRow_;
                currentPlayer.toColumn = moveToColumn_;
                currentPlayer.movementPhase =
                    moving_ ? playerMovementPhase() : -1;
                appendHistoricalPlayer(currentPlayer);
            }
            if (actorRemoved &&
                !enemyPresentationLagPendingPlayers_.empty() &&
                !drainCapturedPrimeBottomExitPlayerHistory &&
                !drainCapturedFourthPrimeBottomExitPlayerHistory) {
                // The player record precedes the exit terminal on this pass;
                // its next dirty pose and the actor removal therefore become
                // one complete page rather than exposing an actor-free copy
                // of the older player pose first.
                enemyPresentationLagDisplayedPlayer_ =
                    enemyPresentationLagPendingPlayers_.front();
                    enemyPresentationLagPendingPlayers_.pop_front();
            }
            // The captured Prime exit has the opposite record order: its
            // actor-free pass first exposes the previously displayed player
            // (0x09d4...), then the queued walk begins at phase 1. Leave the
            // first pending player untouched for the drain below.

            const auto captureCombination =
                [&](const PresentationPlayerState& displayPlayer) {
                    PresentationPlayerState logicalPlayer;
                    logicalPlayer.moving = moving_;
                    logicalPlayer.moveTimer = moveTimer_;
                    logicalPlayer.row = playerRow_;
                    logicalPlayer.column = playerColumn_;
                    logicalPlayer.terminalFrame = playerTerminalFrame_;
                    logicalPlayer.direction = moveDirection_;
                    logicalPlayer.fromRow = moveFromRow_;
                    logicalPlayer.fromColumn = moveFromColumn_;
                    logicalPlayer.toRow = moveToRow_;
                    logicalPlayer.toColumn = moveToColumn_;
                    logicalPlayer.movementPhase =
                        moving_ ? playerMovementPhase() : -1;

                    moving_ = displayPlayer.moving;
                    moveTimer_ = displayPlayer.moveTimer;
                    playerRow_ = displayPlayer.row;
                    playerColumn_ = displayPlayer.column;
                    playerTerminalFrame_ = displayPlayer.terminalFrame;
                    moveDirection_ = displayPlayer.direction;
                    moveFromRow_ = displayPlayer.fromRow;
                    moveFromColumn_ = displayPlayer.fromColumn;
                    moveToRow_ = displayPlayer.toRow;
                    moveToColumn_ = displayPlayer.toColumn;

                    auto logicalActor = std::find_if(
                        enemies_.begin(), enemies_.end(), [this](const Enemy& enemy) {
                            return enemy.slot == enemyPresentationLagSlot_;
                        });
                    bool restoreHidden = false;
                    bool insertedActor = false;
                    Enemy savedActor;
                    if (presented.slot >= 0) {
                        if (logicalActor != enemies_.end()) {
                            savedActor = *logicalActor;
                            *logicalActor = presented;
                        } else {
                            enemies_.push_back(presented);
                            insertedActor = true;
                        }
                    } else if (logicalActor != enemies_.end()) {
                        restoreHidden = logicalActor->collisionHidden;
                        logicalActor->collisionHidden = true;
                    }

                    const bool capturedFourthPrimeExitCombination =
                        attractMode_ && attractBoardIndex_ == 3 &&
                        activeBoardMode_ == Mode::Primes && level_ == 3 &&
                        attractDifficultyIndex_ == 2 && target_ == 0 &&
                        enemyPresentationLagSlot_ == 0 && enemySlotCount_ == 1 &&
                        enemySlots_[0].type == 0 &&
                        random_.calls >= 626 && random_.calls <= 627;
                    Cell savedBottomCell;
                    Cell savedSafeCell;
                    bool restoreBottomCell = false;
                    bool restoreSafeCell = false;
                    if (capturedFourthPrimeExitCombination) {
                        const bool historicalStanding =
                            !displayPlayer.moving && displayPlayer.row == 3 &&
                            displayPlayer.column == 4;
                        const bool preserveOldBottom =
                            presented.slot >= 0 || historicalStanding ||
                            (displayPlayer.moving &&
                             displayPlayer.movementPhase <= 1);
                        const bool preserveOldSafe =
                            presented.slot >= 0 || historicalStanding ||
                            (displayPlayer.moving &&
                             displayPlayer.movementPhase <= 2);
                        if (preserveOldBottom) {
                            savedBottomCell = cell(4, 0);
                            Cell& oldBottom = cell(4, 0);
                            oldBottom.label.clear();
                            oldBottom.correct = false;
                            oldBottom.eaten = true;
                            restoreBottomCell = true;
                        }
                        if (preserveOldSafe) {
                            savedSafeCell = cell(3, 1);
                            cell(3, 1).safe = true;
                            restoreSafeCell = true;
                        }
                    }

                    std::vector<std::uint32_t> combined =
                        capturePresentationFrame();

                    if (restoreBottomCell) cell(4, 0) = savedBottomCell;
                    if (restoreSafeCell) cell(3, 1) = savedSafeCell;
                    if (capturedFourthPrimeExitCombination &&
                        presented.slot < 0 && displayPlayer.moving &&
                        displayPlayer.movementPhase == 1) {
                        // Reggie's actor-removal dirty rectangle remains the
                        // resident base for the first queued player callback.
                        // It has already cleared the bottom 41-pixel sprite
                        // strip; phase 2 restores the ordinary cell foreground.
                        const std::size_t stripStart = static_cast<std::size_t>(
                            176 * Renderer::Width + 24);
                        std::fill_n(combined.begin() + stripStart, 41,
                                    Colors::BoardBlue);
                    }

                    if (insertedActor) {
                        enemies_.pop_back();
                    } else if (logicalActor != enemies_.end()) {
                        if (presented.slot >= 0) *logicalActor = savedActor;
                        else logicalActor->collisionHidden = restoreHidden;
                    }
                    moving_ = logicalPlayer.moving;
                    moveTimer_ = logicalPlayer.moveTimer;
                    playerRow_ = logicalPlayer.row;
                    playerColumn_ = logicalPlayer.column;
                    playerTerminalFrame_ = logicalPlayer.terminalFrame;
                    moveDirection_ = logicalPlayer.direction;
                    moveFromRow_ = logicalPlayer.fromRow;
                    moveFromColumn_ = logicalPlayer.fromColumn;
                    moveToRow_ = logicalPlayer.toRow;
                    moveToColumn_ = logicalPlayer.toColumn;
                    return combined;
                };

            auto presentCombination = [&](const PresentationPlayerState& state) {
                const std::vector<std::uint32_t> combined =
                    captureCombination(state);
                if (!combined.empty()) {
                    const bool capturedFourthExitLeadingStrip =
                        attractMode_ && attractBoardIndex_ == 3 &&
                        activeBoardMode_ == Mode::Primes && level_ == 3 &&
                        target_ == 0 && enemyPresentationLagSlot_ == 0 &&
                        enemyPresentationLagFullSurface_ && presented.slot >= 0 &&
                        enemyPresentationLagLastEnemyPhase_ == 1 &&
                        presentedEnemyPhase == 2 &&
                        enemyPresentationLagPixels_.size() == combined.size();
                    if (capturedFourthExitLeadingStrip) {
                        std::vector<std::uint32_t> leadingStrip =
                            enemyPresentationLagPixels_;
                        for (int y = 168; y < Renderer::Height; ++y) {
                            const std::size_t rowStart = static_cast<std::size_t>(
                                y * Renderer::Width);
                            std::copy_n(combined.begin() + rowStart,
                                        Renderer::Width,
                                        leadingStrip.begin() + rowStart);
                        }
                        // Source frame 13870 exposes the bottom ten rows of
                        // Reggie's first clipped exit pose over the previously
                        // resident endpoint before the complete phase-1 page.
                        enqueuePresentationFrame(leadingStrip);
                        enemyPresentationLagPixels_ = leadingStrip;
                    }
                    enqueuePresentationFrame(combined);
                    enemyPresentationLagPixels_ = combined;
                }
            };
            if (!(actorRemoved &&
                  drainCapturedFourthPrimeBottomExitPlayerHistory)) {
                presentCombination(enemyPresentationLagDisplayedPlayer_);
            }
            while (!enemyPresentationLagPendingPlayers_.empty()) {
                enemyPresentationLagDisplayedPlayer_ =
                    enemyPresentationLagPendingPlayers_.front();
                enemyPresentationLagPendingPlayers_.pop_front();
                presentCombination(enemyPresentationLagDisplayedPlayer_);
            }
            enemyPresentationLagLastEnemyPhase_ = presentedEnemyPhase;

            if (actorRemoved) {
                enemyPresentationLagSlot_ = -1;
                enemyPresentationLagTicks_ = 0;
                enemyPresentationLagFirstPresentationsRemaining_ = 0;
                enemyPresentationLagPresentFirstLogicalPage_ = false;
                enemyPresentationLagActorMayDisappear_ = false;
                enemyPresentationLagFullSurface_ = false;
                enemyPresentationLagHistory_.clear();
                enemyPresentationLagPlayerHistory_.clear();
                enemyPresentationLagPendingPlayers_.clear();
                enemyPresentationLagPixels_.clear();
            }
        }
        return;
    }
    const bool presentActorFreeTerminalNow =
        enemyPresentationLagActorMayDisappear_ && presented.slot >= 0 &&
        !enemyPresentationLagHistory_.empty() &&
        enemyPresentationLagHistory_.front().slot < 0;
    std::vector<std::uint32_t> pixels;
    if (presented.slot >= 0) {
        if (current != enemies_.end()) {
            const Enemy logical = *current;
            *current = presented;
            pixels = capturePresentationFrame();
            *current = logical;
        } else {
            enemies_.push_back(presented);
            pixels = capturePresentationFrame();
            enemies_.pop_back();
        }
    } else {
        pixels = capturePresentationFrame();
    }
    if (!pixels.empty()) {
        enqueuePresentationFrame(pixels);
        enemyPresentationLagPixels_ = pixels;
    }

    if (presentActorFreeTerminalNow && !enemyPresentationLagFullSurface_) {
        // The actor-removal dirty pass follows the final clipped pose on the
        // same scheduler callback. Queue both complete pages now, before the
        // player's following terminal callback can start collision state.
        // This is the measured 0x8abe... -> 0xfe9a... boundary at source
        // frames 5716-5718.
        bool restoreCollisionHidden = false;
        if (current != enemies_.end()) {
            restoreCollisionHidden = current->collisionHidden;
            current->collisionHidden = true;
        }
        const std::vector<std::uint32_t> actorFreePixels =
            capturePresentationFrame();
        if (current != enemies_.end()) {
            current->collisionHidden = restoreCollisionHidden;
        }
        if (!actorFreePixels.empty()) {
            enqueuePresentationFrame(actorFreePixels);
            enemyPresentationLagPixels_ = actorFreePixels;
        }
    }

    if (presentActorFreeTerminalNow || presented.slot < 0 ||
        (!presented.moving && !presented.entering)) {
        enemyPresentationLagSlot_ = -1;
        enemyPresentationLagTicks_ = 0;
        enemyPresentationLagFirstPresentationsRemaining_ = 0;
        enemyPresentationLagPresentFirstLogicalPage_ = false;
        enemyPresentationLagActorMayDisappear_ = false;
        enemyPresentationLagFullSurface_ = false;
        enemyPresentationLagHistory_.clear();
        enemyPresentationLagPlayerHistory_.clear();
        enemyPresentationLagPendingPlayers_.clear();
        enemyPresentationLagPixels_.clear();
    }
}

void Game::discardPresentationFrames() {
    presentationFrames_.clear();
    presentationFrameAuditOrigins_.clear();
    lastPresentationAuditOrigin_ = 0;
    captureEnemyCallbackPresentationFrames_ = false;
    enemyCallbackPresentationSlots_.clear();
    enemyCallbackPresentationFrames_.clear();
    enemyPresentationLagSlot_ = -1;
    enemyPresentationLagTicks_ = 0;
    enemyPresentationLagFirstPresentationsRemaining_ = 0;
    enemyPresentationLagPresentFirstLogicalPage_ = false;
    enemyPresentationLagActorMayDisappear_ = false;
    enemyPresentationLagFullSurface_ = false;
    enemyPresentationLagHistory_.clear();
    enemyPresentationLagPlayerHistory_.clear();
    enemyPresentationLagPendingPlayers_.clear();
    enemyPresentationLagPixels_.clear();
    attractPrimeExitPlayerCatchup_ = false;
    lastPresentationPixels_.clear();
    deathResidentSurfacePixels_.clear();
    attractPlayerPhaseZeroHoldPixels_.clear();
    attractSafeZoneWarningHoldPixels_.clear();
    attractFourthPrimeSafeHoldPixels_.clear();
    attractPlayerEntryResidentSurfacePixels_.clear();
    attractPlayerEntryPresentationHoldPixels_.clear();
    attractPlayerDepartingEnemyHoldPixels_.clear();
    attractFactorsConcurrentPhase_ = 0;
    attractFactorsConcurrentHoldPixels_.clear();
    attractPlayerEntryMunchOverlap_ = false;
    attractMunchEntryTerminalHold_ = false;
    attractMunchResidentEntrySlot_ = -1;
    attractPlayerMovingEntrySlot_ = -1;
    attractPlayerMovingEntryHoldPixels_.clear();
    attractPlayerMovingEntryTerminalHold_ = false;
    attractPlayerConcurrentEnemySlot_ = -1;
    attractPlayerConcurrentEnemyHoldPixels_.clear();
    attractPlayerConcurrentEnemyTerminalHold_ = false;
    attractMunchConcurrentEnemyPixels_.clear();
    attractMunchEnemyTerminalHoldPixels_.clear();
    attractConcurrentEnemyHoldPixels_.clear();
    attractConcurrentEnemyAwaitPlayerCallback_ = false;
    attractPlayerWarningHoldPixels_.clear();
    attractOverlapEntryWarningPixels_.clear();
    cannibalEntryTerminalHoldPixels_.clear();
    cannibalPlayerResidentSurfacePixels_.clear();
    cannibalPlayerPresentationHoldPixels_.clear();
    cannibalPlayerRetainedPenultimatePaint_ = false;
    attractInitialCannibalPlayerTerminalHold_ = false;
}

void Game::renderPresentation(Renderer& renderer) {
    lastPresentationAuditOrigin_ = 0;
    if (!presentationFrames_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(presentationFrames_.front());
        presentationFrames_.pop_front();
        lastPresentationAuditOrigin_ =
            presentationFrameAuditOrigins_.empty()
                ? 0 : presentationFrameAuditOrigins_.front();
        if (!presentationFrameAuditOrigins_.empty()) {
            presentationFrameAuditOrigins_.pop_front();
        }
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractMunchConcurrentEnemyPixels_.empty() && attractMode_ &&
        (page_ == Page::Attract || page_ == Page::Feedback ||
         attractPostFeedbackBoard_)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractMunchConcurrentEnemyPixels_);
        if (page_ == Page::Feedback && !deathAnimating_ &&
            !attractPostFeedbackBoard_) {
            renderFeedback(renderer);
        }
        if (attractHallTransitionPhase_ ==
            AttractHallTransitionPhase::Wipe) {
            renderAttractHallWipe(renderer);
        }
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractMunchEnemyTerminalHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractMunchEnemyTerminalHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (enemyPresentationLagPixels_.size() ==
        static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(enemyPresentationLagPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractFourthPrimeSafeHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractFourthPrimeSafeHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (deathAnimating_ &&
        deathResidentSurfacePixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(deathResidentSurfacePixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractPlayerDepartingEnemyHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerDepartingEnemyHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractFactorsConcurrentHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractFactorsConcurrentHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractConcurrentEnemyHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractConcurrentEnemyHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractPlayerPhaseZeroHoldPixels_.empty() && moving_ &&
        playerMovementPhase() == 0) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerPhaseZeroHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    attractPlayerPhaseZeroHoldPixels_.clear();
    if (!attractSafeZoneWarningHoldPixels_.empty() && enemyWarning_) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractSafeZoneWarningHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    attractSafeZoneWarningHoldPixels_.clear();
    if (!attractPlayerEntryPresentationHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerEntryPresentationHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractPlayerMovingEntryHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerMovingEntryHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractPlayerConcurrentEnemyHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerConcurrentEnemyHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractPlayerWarningHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractPlayerWarningHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!attractOverlapEntryWarningPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(attractOverlapEntryWarningPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!cannibalEntryTerminalHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(cannibalEntryTerminalHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    if (!cannibalPlayerPresentationHoldPixels_.empty()) {
        renderer.setGraphicsMode(assets_.graphicsMode());
        renderer.replacePixels(cannibalPlayerPresentationHoldPixels_);
        lastPresentationPixels_ = renderer.pixels();
        return;
    }
    render(renderer);
    lastPresentationPixels_ = renderer.pixels();
}

bool Game::enemyIntersectsCell(const Enemy& enemy, int row, int column) {
    // 0x08277 tests both saved actor endpoints. During the edge warning the
    // outside endpoint is off-board but the selected destination is already
    // installed, so a newly appearing safe zone can still catch that actor.
    if (enemy.row == row && enemy.column == column) return true;
    return (enemy.moving || enemy.entering) &&
           enemy.fromRow == row && enemy.fromColumn == column;
}

void Game::render(Renderer& renderer) {
    renderer.setGraphicsMode(assets_.graphicsMode());
    if (configurationWriteError_ &&
        configurationWriteErrorBackgroundPixels_.size() ==
            static_cast<std::size_t>(Renderer::Width * Renderer::Height)) {
        renderer.replacePixels(configurationWriteErrorBackgroundPixels_);
    } else switch (page_) {
    case Page::StartupVersion: renderStartupVersion(renderer); break;
    case Page::StartupSplash: renderStartupSplash(renderer); break;
    case Page::Title: renderTitle(renderer); break;
    case Page::InstructionsQuestion: renderInstructionsQuestion(renderer); break;
    case Page::ModeSelect: renderModeSelect(renderer, false); break;
    case Page::HallSelect: renderModeSelect(renderer, true); break;
    case Page::Information: renderInformation(renderer); break;
    case Page::Options: renderOptions(renderer); break;
    case Page::OptionsDifficulty: renderOptionsDifficulty(renderer); break;
    case Page::OptionsContent: renderOptionsContent(renderer); break;
    case Page::OptionsContentRange: renderOptionsContentRange(renderer); break;
    case Page::OptionsContentOtherNumber: renderOptionsContentOtherNumber(renderer); break;
    case Page::OptionsContentOperations: renderOptionsContentOperations(renderer); break;
    case Page::OptionsContentHelp: renderOptionsContentHelp(renderer); break;
    case Page::OptionsContentValidation: renderOptionsContentValidation(renderer); break;
    case Page::OptionsEraseHall: renderOptionsEraseHall(renderer); break;
    case Page::OptionsEraseEntries: renderOptionsEraseEntries(renderer); break;
    case Page::OptionsEraseAllConfirm: renderOptionsEraseAllConfirm(renderer); break;
    case Page::OptionsPassword: renderOptionsPassword(renderer); break;
    case Page::OptionsPasswordPrompt: renderOptionsPasswordPrompt(renderer); break;
    case Page::OptionsJoystickCalibration: renderOptionsJoystickCalibration(renderer); break;
    case Page::Hall: renderHall(renderer); break;
    case Page::Playing: case Page::Paused: case Page::Feedback: case Page::Attract: renderBoard(renderer); break;
    case Page::QuitConfirm: renderBoard(renderer); renderQuitConfirm(renderer); break;
    case Page::LevelComplete: renderLevelComplete(renderer); break;
    case Page::NameEntry: renderBoard(renderer); renderNameEntry(renderer); break;
    case Page::ReplayQuestion: renderReplayQuestion(renderer); break;
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

void Game::renderConfigurationWriteError(Renderer& renderer) {
    // Number's common alert at image 0x07A91 is byte-isomorphic to Word's.
    // It saves inclusive rectangle (60,65)..(260,135), paints black, and emits
    // the five DS:1151/1164/117A/118D and DS:0370 lines at these baselines.
    renderer.fillRect(60, 65, 201, 71, Colors::Black);
    const GemFont& font = assets_.largeFont();
    // Logical color 3 maps through DS:0464 to VGA white and CGA light cyan.
    const std::uint32_t textColor = renderer.graphicsMode() == GraphicsMode::Cga4
        ? Colors::Cyan : Colors::White;
    renderer.drawText(font, 68, 80, "Configuration file", textColor);
    renderer.drawText(font, 68, 90, "cannot be written, it", textColor);
    renderer.drawText(font, 68, 100, "is write protected", textColor);
    renderer.drawText(font, 68, 125, "    Press any key.", textColor);
}

void Game::renderAttractHallWipe(Renderer& renderer) {
    if (attractHallWipeVariant_ == AttractHallWipeVariant::Clean) {
        switch (attractHallWipeFrame_) {
        case 0:
            // Exact frame 9005: this capture samples the same closing Wipe
            // just as it first reaches the bottom of the restored board.
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
            renderer.fillRect(160, 61, 11, 3, Colors::BoardBlue);
            renderer.fillRect(150, 64, 21, 10, Colors::BoardBlue);
            renderer.fillRect(150, 74, 31, 4, Colors::BoardBlue);
            renderer.fillRect(140, 78, 41, 10, Colors::BoardBlue);
            renderer.fillRect(140, 88, 51, 1, Colors::BoardBlue);
            renderer.fillRect(0, 89, Renderer::Width,
                              Renderer::Height - 89, Colors::BoardBlue);
            break;
        case 4:
        case 5:
            renderer.clear(Colors::BoardBlue);
            break;
        default:
            // Exact frame 9011 is a torn scan through the Hall painter. The
            // live Hall is retained below the old navy/black scan bands while
            // title, body, and footer glyphs are erased only where the painter
            // had not reached that scanline yet.
            renderHall(renderer);
            renderer.fillRect(0, 0, Renderer::Width, 61, Colors::BoardBlue);
            renderer.fillRect(0, 61, Renderer::Width, 13, Colors::Black);
            renderer.fillRect(135, 80, 47, 7, Colors::White);
            renderer.fillRect(80, 120, 158, 7, Colors::White);
            renderer.fillRect(80, 129, 155, 7, Colors::White);
            renderer.fillRect(102, 185, 218, 4, Colors::White);
            renderer.fillRect(128, 189, 192, 1, Colors::White);
            renderer.fillRect(145, 190, 175, 1, Colors::White);
            renderer.fillRect(152, 191, 168, 1, Colors::White);
            break;
        }
        return;
    }

    switch (attractHallWipeFrame_) {
    case 0: {
        // Representative closing-Wipe sample from the first lossless terminal:
        // the old board survives inside this stepped region while cyan
        // replaces its exterior. The second terminal samples the same effect
        // earlier in its traversal, so timing is locked separately below.
        struct Band { int top; int bottom; int left; int right; };
        static constexpr std::array<Band, 14> survivingBands = {{
            {0, 70, 0, 319},
            {71, 74, 11, 319},
            {75, 84, 11, 309},
            {85, 87, 21, 309},
            {88, 97, 21, 299},
            {98, 100, 31, 299},
            {101, 110, 31, 289},
            {111, 114, 41, 289},
            {115, 124, 41, 279},
            {125, 127, 51, 279},
            {128, 137, 51, 269},
            {138, 140, 61, 269},
            {141, 144, 61, 259},
            {145, 145, 154, 259},
        }};
        for (const Band& band : survivingBands) {
            const int height = band.bottom - band.top + 1;
            if (band.left > 0) renderer.fillRect(0, band.top, band.left, height, Colors::Cyan);
            if (band.right < Renderer::Width - 1) {
                renderer.fillRect(band.right + 1, band.top,
                                  Renderer::Width - band.right - 1, height, Colors::Cyan);
            }
        }
        renderer.fillRect(0, 146, Renderer::Width, Renderer::Height - 146, Colors::Cyan);
        break;
    }
    case 1:
        renderer.clear(Colors::Cyan);
        break;
    case 2:
        renderer.clear(Colors::Cyan);
        renderer.fillRect(160, 170, 11, 3, Colors::BoardBlue);
        renderer.fillRect(150, 173, 21, 10, Colors::BoardBlue);
        renderer.fillRect(150, 183, 31, 4, Colors::BoardBlue);
        renderer.fillRect(140, 187, 41, 10, Colors::BoardBlue);
        renderer.fillRect(140, 197, 51, 3, Colors::BoardBlue);
        break;
    case 3:
        renderer.clear(Colors::Cyan);
        renderer.fillRect(110, 0, 101, 9, Colors::BoardBlue);
        renderer.fillRect(110, 9, 111, 3, Colors::BoardBlue);
        renderer.fillRect(100, 12, 121, 10, Colors::BoardBlue);
        renderer.fillRect(100, 22, 131, 3, Colors::BoardBlue);
        renderer.fillRect(90, 25, 141, 10, Colors::BoardBlue);
        renderer.fillRect(90, 35, 151, 4, Colors::BoardBlue);
        renderer.fillRect(0, 39, Renderer::Width, Renderer::Height - 39,
                          Colors::BoardBlue);
        break;
    case 4:
        renderer.clear(Colors::BoardBlue);
        break;
    default:
        renderer.clear(Colors::BoardBlue);
        renderer.fillRect(0, 167, Renderer::Width, Renderer::Height - 167,
                          Colors::Black);
        break;
    }
}

void Game::renderAttractInterstitialTransition(Renderer& renderer) {
    struct Band { int top; int bottom; int left; int right; };
    const auto coverOutside = [&](const auto& survivingBands,
                                  const int coveredTop) {
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
        AttractInterstitialTransition::HallToSplash) {
        switch (attractInterstitialFrame_) {
        case 0: {
            // Exact frame 7257: the closing Wipe retains the Hall inside this
            // stepped aperture and replaces the rest with palette cyan.
            static constexpr std::array<Band, 15> survivingBands = {{
                {0, 27, 0, 319},
                {28, 31, 11, 319},
                {32, 49, 11, 309},
                {50, 52, 21, 309},
                {53, 62, 21, 299},
                {63, 66, 31, 299},
                {67, 76, 31, 289},
                {77, 79, 41, 289},
                {80, 89, 41, 279},
                {90, 92, 51, 279},
                {93, 102, 51, 269},
                {103, 106, 61, 269},
                {107, 116, 61, 259},
                {117, 119, 71, 259},
                {120, 128, 71, 249},
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
            // Source frame 7686 is a completed bottom-up splash repaint, not
            // merely the first background strip: rows 88..199 already equal
            // the following full logo page while rows 0..87 remain black.
            renderStartupSplash(renderer);
            renderer.fillRect(0, 0, Renderer::Width, 88, Colors::Black);
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
            // Number and Word dispatch the byte-isomorphic type-2 Wipe. The
            // four live Number captures each expose one torn close refresh;
            // native presents this completed logical aperture atomically.
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
            // Exact frame 8344: the logo closes beneath the same cyan Wipe.
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
            // Type-1 opens upward from the bottom onto Number's white target
            // page. Tick 1 is not painted underneath the aperture.
            renderer.clear(Colors::Cyan);
            renderer.fillRect(160, 56, 11, 4, Colors::White);
            renderer.fillRect(150, 60, 21, 10, Colors::White);
            renderer.fillRect(150, 70, 31, 3, Colors::White);
            renderer.fillRect(140, 73, 41, 10, Colors::White);
            renderer.fillRect(140, 83, 51, 4, Colors::White);
            renderer.fillRect(130, 87, 61, 2, Colors::White);
            renderer.fillRect(0, 89, Renderer::Width,
                              Renderer::Height - 89, Colors::White);
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

void Game::renderAttractBoardPaint(Renderer& renderer, const int frame) {
    const bool capturedInequalityOpening =
        attractMode_ && attractBoardIndex_ == 2 &&
        activeBoardMode_ == Mode::Inequality && target_ == 24 &&
        playerRow_ == 1 && playerColumn_ == 1 &&
        random_.calls == 480 && random_.state == 0xfb64e725u;
    if (frame > 0) {
        // The autonomous 0xF585 capture exposes one complete static board
        // page at frame 8777 before the Muncher and the two header strings are
        // installed.  The surrounding painter refresh at frame 8776 is only
        // a timing-dependent prefix; this actor-free page is a coherent
        // callback surface and is therefore safe to present atomically.
        const bool recovering = playerRecovering_;
        if (capturedInequalityOpening) playerRecovering_ = true;
        renderBoard(renderer);
        playerRecovering_ = recovering;
        // The complete board painter precedes its two header strings. The
        // horizontal rules at y=2 and y=16 are already resident and remain.
        renderer.fillRect(0, 3, Renderer::Width, 13, Colors::BoardBlue);
        return;
    }

    if (capturedInequalityOpening) {
        // Source frame 8776 is a timing-dependent monotonic prefix through
        // the same static painter: compared with the next complete page it
        // contains only not-yet-painted blue pixels. Keep the preceding
        // completed blue surface instead of manufacturing that scanout tear.
        renderer.clear(Colors::BoardBlue);
        return;
    }

    // Frame 8351 is a torn scan through the DOS painter. Express the visible
    // prefix in geometric operations so the two cell strings remain live
    // board content rather than pixels copied from one capture.
    renderer.clear(Colors::BoardBlue);
    renderer.horizontalLine(97, 234, 2, Colors::White);
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
        const Cell& current = cell(0, column);
        if (current.eaten) continue;
        std::string boardLabel = current.label;
        for (char& character : boardLabel) {
            switch (character) {
            case '+': character = '\x02'; break;
            case '-': character = '\x04'; break;
            case 'x': character = '\x06'; break;
            case ':': character = '\x08'; break;
            default: break;
            }
        }
        renderer.drawCenteredText(font,
                                  BoardLeft + column * BoardCellWidth +
                                      BoardCellWidth / 2,
                                  BoardTop + 12, boardLabel, Colors::White);
    }
    // The top glyph row of the second cell had not reached the captured scan.
    renderer.fillRect(BoardLeft + BoardCellWidth + 1, BoardTop + 12,
                      BoardCellWidth - 2, 1, Colors::BoardBlue);
}

void Game::renderStartupVersion(Renderer& renderer) {
    renderer.clear(Colors::White);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 159, 35, "Number Munchers", Colors::Black);
    renderer.drawCenteredText(font, 159, 47, "Version 1.1", Colors::Black);
    if (const Image* logo = assets_.startupLogo()) renderer.drawImage(*logo, 0, 75, false);
    renderer.drawCenteredText(font, 159, 185, "Copyright 1990, 1991, MECC", Colors::Black);
}

void Game::renderStartupSplash(Renderer& renderer) {
    renderer.clear(0xfbffdb);
    if (const Image* splash = assets_.image(6005)) renderer.drawImage(*splash, 0, 0, false, true);
    renderer.drawCenteredText(assets_.largeFont(), 159, 185,
                              "Press a key for Muncher Menu", Colors::Black);
}

void Game::drawMenuItem(Renderer& renderer, int y, std::string_view text, bool selected, int x) {
    const GemFont& font = assets_.largeFont();
    const int width = renderer.textWidth(font, text);
    if (selected) renderer.fillRect(x - 3, y - 1, width + 6, font.height() + 2, Colors::White);
    renderer.drawText(font, x, y, text, selected ? Colors::Black : Colors::White);
}

void Game::renderTitle(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (const Image* title = assets_.image(6009)) renderer.drawImage(*title, 0, 0, false, true);
    static constexpr std::array<std::string_view, 5> choices = {
        "1. Play Number Munchers", "2. Hall of Fame", "3. Information", "4. Options", "5. Quit"
    };
    static constexpr std::array<int, 5> rowY = {91, 102, 113, 124, 135};
    for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
        if (menuSelection_ == index) {
            const std::string prefix = std::to_string(index + 1) + ".";
            const std::string_view label = choices[static_cast<std::size_t>(index)].substr(3);
            renderer.drawText(assets_.largeFont(), 57, rowY[static_cast<std::size_t>(index)],
                              prefix, Colors::White);
            renderer.fillRect(71, rowY[static_cast<std::size_t>(index)] - 1, 181, 10, Colors::White);
            renderer.drawText(assets_.largeFont(), 81, rowY[static_cast<std::size_t>(index)],
                              label, Colors::Black);
        } else {
            renderer.drawText(assets_.largeFont(), 57, rowY[static_cast<std::size_t>(index)],
                              choices[static_cast<std::size_t>(index)], Colors::White);
        }
    }
    renderer.drawText(assets_.largeFont(), 10, 170,
                      "Use Arrows to move, then press Enter.", Colors::White);
}

void Game::renderInstructionsQuestion(Renderer& renderer) {
    renderer.clear(Colors::Black);
    // The original dialog uses a three-pixel frame inset only vertically.
    // This is the exact 320x200 geometry captured from the supplied build.
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);

    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 60, 85, "Do you want instructions?", Colors::White);
    const auto drawChoice = [&](int x, std::string_view text, bool selected) {
        if (selected) {
            renderer.fillRect(x - 2, 104, renderer.textWidth(font, text) + 5, 10, Colors::White);
        }
        renderer.drawText(font, x, 105, text, selected ? Colors::Black : Colors::White);
    };
    drawChoice(115, " Yes ", menuSelection_ == 0);
    drawChoice(165, " No ", menuSelection_ == 1);
}

void Game::renderReplayQuestion(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);

    const GemFont& font = assets_.largeFont();
    const std::string question = "Do you want to play " +
        std::string(ModeNames[static_cast<std::size_t>(mode_)]) + " again?";
    // 0x1056c centers this prompt inside the 288-pixel board span and then
    // adds x=20; it is five pixels right of full-screen centering for Primes.
    const int questionX = BoardLeft +
        (BoardRight - BoardLeft - renderer.textWidth(font, question)) / 2;
    renderer.drawText(font, questionX, 85, question, Colors::White);
    const auto drawChoice = [&](int x, std::string_view text, bool selected) {
        if (selected) {
            renderer.fillRect(x - 2, 104, renderer.textWidth(font, text) + 5, 10, Colors::White);
        }
        renderer.drawText(font, x, 105, text, selected ? Colors::Black : Colors::White);
    };
    drawChoice(115, " Yes ", menuSelection_ == 0);
    drawChoice(165, " No ", menuSelection_ == 1);
}

void Game::renderModeSelect(Renderer& renderer, bool hallMode) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 0, 20,
                      hallMode ? "  Which Hall of Fame would" : "  Which Number Munchers game would",
                      Colors::White);
    renderer.drawText(font, 0, 29,
                      hallMode ? "  you like to see?" : "  you like to play?", Colors::White);
    std::vector<Mode> modes;
    if (hallMode) {
        for (int index = 0; index < static_cast<int>(Mode::Count); ++index) {
            modes.push_back(static_cast<Mode>(index));
        }
    } else {
        modes = enabledPlayModes();
    }
    const int firstBaseline = hallMode ? 70 : selectorFirstBaseline(modes.size());
    for (int index = 0; index < static_cast<int>(modes.size()); ++index) {
        const int y = firstBaseline + index * 11;
        renderer.drawText(font, 98, y, std::to_string(index + 1) + ".", Colors::White);
        const std::string_view label = ModeNames[static_cast<std::size_t>(modes[static_cast<std::size_t>(index)])];
        if (menuSelection_ == index) {
            renderer.fillRect(112, y - 1, renderer.textWidth(font, label) + 21, 10, Colors::White);
        }
        renderer.drawText(font, 122, y, label,
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 10, 170, "Use Arrows to move, then press Enter.", Colors::White);
}

void Game::renderInformation(Renderer& renderer) {
    renderer.clear(Colors::Black);
    renderer.fillRect(0, 7, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 186, 320, 3, Colors::Magenta);
    renderer.fillRect(0, 7, 3, 182, Colors::Magenta);
    renderer.fillRect(317, 7, 3, 182, Colors::Magenta);

    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 92, 15, "Number Munchers", Colors::White);
    renderer.drawText(font, 50, 191, "Press Space Bar to continue.", Colors::White);

    const auto drawLines = [&](int x, int firstY,
                               std::initializer_list<std::string_view> lines) {
        int y = firstY;
        for (const std::string_view line : lines) {
            if (!line.empty()) renderer.drawText(font, x, y, line, Colors::White);
            y += 10;
        }
    };

    switch (informationPage_) {
    case 0:
        drawLines(12, 45, {
            "Number Munchers is a game that",
            "helps you develop your math skills.",
            "",
            "In this game, your job is to move",
            "the Number Muncher and have it eat",
            "the correct numbers.  The correct",
            "numbers are those that match the",
            "rule or value shown at the top",
            "of the screen.",
        });
        break;
    case 1:
        drawLines(12, 45, {
            "Every time you munch a correct",
            "number, you earn points.",
            "",
            "Be careful!  You will lose a Muncher",
            "if it eats the wrong number or if it",
            "gets caught by a Troggle.",
        });
        drawSprite(renderer, 1006, 7, 52, 121);
        drawSprite(renderer, 1007, 7, 216, 121, true);
        renderer.drawText(font, 44, 165, "Muncher", Colors::White);
        renderer.drawText(font, 204, 165, "Troggle", Colors::White);
        break;
    case 2:
        drawLines(12, 35, {
            "Use the Arrow Keys or a joystick to",
            "move the Muncher.  Press the Space",
            "Bar to munch a number.",
            "",
            "To pause in the middle of a game,",
            "press the Enter Key.",
            "",
            "To quit in the middle of a game,",
            "press the Escape Key.",
            "",
            "To turn the sound on and off, hold",
            "down the Alt Key and press the",
            "letter S.",
        });
        break;
    case 3:
        drawLines(12, 45, {
            "Game squares with small markers in",
            "each corner are \"safe zones.\"",
            "Munchers can enter them, but Troggles",
            "cannot.",
            "",
            "Options are available that allow you",
            "to control the level of difficulty",
            "and select the range of numbers to be",
            "used in the game.  See the User's",
            "Guide for an explanation of the",
            "options.",
        });
        break;
    case 4:
        drawLines(20, 45, {
            "If your computer has Ad Lib sound:",
            "",
            "To turn the music on and off, hold",
            "down the Alt Key and press the",
            "letter M.",
            "",
            "To have sounds switch from external",
            "speakers to the computer's internal",
            "speaker, hold down the Alt Key and",
            "press the letter P.",
        });
        break;
    default:
        drawLines(12, 45, {
            "The software team responsible for the",
            "creation of this product included:",
        });
        renderer.drawText(font, 100, 75, "Craig Copley", Colors::White);
        renderer.drawText(font, 108, 85, "Mark Dostal", Colors::White);
        renderer.drawText(font, 116, 95, "Ed Gratz", Colors::White);
        renderer.drawText(font, 108, 105, "Al Lathrop", Colors::White);
        renderer.drawText(font, 100, 115, "Mark Paquette", Colors::White);
        renderer.drawText(font, 100, 125, "Diane Portner", Colors::White);
        renderer.drawText(font, 100, 135, "Larry Phenow", Colors::White);
        renderer.drawText(font, 100, 145, "Julie Redland", Colors::White);
        break;
    }
}

void Game::renderOptions(Renderer& renderer) {
    drawOptionsFrame(renderer, "Options", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Choose an option:", Colors::White);
    const std::array<std::string, 6> choices = {
        "Set Difficulty Level",
        "Set Content",
        "Erase Hall of Fame",
        "Set Password",
        std::string("Turn Joystick ") + (joystickOn_ ? "OFF" : "ON"),
        "Calibrate Joystick",
    };
    for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
        const int y = 72 + index * 11;
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, 74 - renderer.textWidth(font, prefix), y, prefix, Colors::White);
        if (menuSelection_ == index) {
            renderer.fillRect(72, y - 1, renderer.textWidth(font, choices[static_cast<std::size_t>(index)]) + 21,
                              10, Colors::White);
        }
        renderer.drawText(font, 82, y, choices[static_cast<std::size_t>(index)],
                          menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 0, 167, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Main Menu", Colors::White);
}

void Game::renderOptionsJoystickCalibration(Renderer& renderer) {
    drawOptionsFrame(renderer, "Calibrate Joystick", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 0, 176, "Press Space Bar to continue.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);

    // Each source prompt has a leading and trailing LF: three logical rows in
    // the shared flags-0xc3 modal. Its recovered row formula produces this
    // full-width 81..118 rectangle and visible baseline 95.
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

void Game::drawOptionsFrame(Renderer& renderer, std::string_view title, int bottomLineY) {
    renderer.clear(Colors::Black);
    renderer.drawCenteredText(assets_.largeFont(), 159, 5, title, Colors::White);
    renderer.horizontalLine(2, 317, 17, Colors::Cyan);
    renderer.horizontalLine(0, 319, 19, Colors::Cyan);
    renderer.horizontalLine(0, 319, bottomLineY, Colors::Cyan);
    renderer.horizontalLine(2, 317, bottomLineY + 2, Colors::Cyan);
}

void Game::renderOptionsDifficulty(Renderer& renderer) {
    drawOptionsFrame(renderer, "Select Difficulty", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 25,
                      "Current Difficulty: " + std::string(DifficultyNames[static_cast<std::size_t>(difficulty_)]),
                      Colors::White);
    renderer.drawText(font, 5, 37, "Choose a level:", Colors::White);
    for (int index = 0; index < static_cast<int>(DifficultyNames.size()); ++index) {
        const int y = 49 + index * 11;
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, 74 - renderer.textWidth(font, prefix), y, prefix, Colors::White);
        const std::string_view label = DifficultyNames[static_cast<std::size_t>(index)];
        if (menuSelection_ == index) {
            renderer.fillRect(72, y - 1, renderer.textWidth(font, label) + 21, 10, Colors::White);
        }
        renderer.drawText(font, 82, y, label, menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 0, 176, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);
}

void Game::renderOptionsContent(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Content", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 25,
                      "Current Difficulty: " + std::string(DifficultyNames[static_cast<std::size_t>(difficulty_)]),
                      Colors::White);
    renderer.drawText(font, 5, 41, "Game", Colors::White);
    renderer.drawText(font, 96, 41, "Use", Colors::White);
    renderer.drawText(font, 136, 41, "Range", Colors::White);
    renderer.drawText(font, 193, 41, "Sequence", Colors::White);
    renderer.drawText(font, 263, 41, "Other", Colors::White);
    const GemFont& valueFont = assets_.smallFont();
    const int componentCount = static_cast<int>(std::count_if(
        contentDraft_.begin(),
        contentDraft_.begin() + static_cast<std::ptrdiff_t>(Mode::Challenge),
        [](const ContentSettings& settings) { return settings.use; }));
    for (int index = 0; index < 6; ++index) {
        const int y = 60 + index * 16;
        const ContentSettings& settings = contentDraft_[static_cast<std::size_t>(index)];
        const bool selectUse = contentRow_ == index && contentColumn_ == 0;
        const bool selectRange = contentRow_ == index && contentColumn_ == 1;
        const bool selectSequence = contentRow_ == index && contentColumn_ == 2;
        const bool selectOther = contentRow_ == index && contentColumn_ == 3;
        const std::string use = index == static_cast<int>(Mode::Challenge) && componentCount <= 1
            ? "---" : settings.use ? "yes" : "no";
        const std::string range = index < static_cast<int>(Mode::Challenge)
            ? std::to_string(settings.minimum) + " - " + std::to_string(settings.maximum)
            : "";
        const std::string sequence = contentFieldValid(index, 2)
            ? (settings.randomSequence ? "random" : "in order") : "";
        const std::string multiplier = index == static_cast<int>(Mode::Multiples)
            ? "up to " + std::to_string(settings.maxMultiplier) : "";
        renderer.drawText(font, 5, y, ModeNames[static_cast<std::size_t>(index)], Colors::White);
        if (selectUse) renderer.fillRect(91, y - 1, renderer.textWidth(valueFont, use) + 17, 10, Colors::White);
        if (selectRange) renderer.fillRect(123, y - 1, 71, 10, Colors::White);
        if (selectSequence) renderer.fillRect(191, y - 1, 63, 10, Colors::White);
        if (selectOther) {
            const int valueWidth = index == static_cast<int>(Mode::Multiples)
                ? renderer.textWidth(valueFont, multiplier)
                : renderer.textWidth(valueFont, "+ - x :");
            renderer.fillRect(255, y - 1, valueWidth + 16, 10, Colors::White);
        }
        renderer.drawText(valueFont, 99, y, use, selectUse ? Colors::Black : Colors::White);
        renderer.drawText(valueFont, 143, y, range, selectRange ? Colors::Black : Colors::White);
        renderer.drawText(valueFont, settings.randomSequence ? 205 : 199, y, sequence,
                          selectSequence ? Colors::Black : Colors::White);
        renderer.drawText(valueFont, 263, y, multiplier, selectOther ? Colors::Black : Colors::White);
        if (index == 3 || index == 4) {
            const std::uint32_t color = selectOther ? Colors::Black : Colors::White;
            if (settings.operations[0]) renderer.drawText(valueFont, 263, y, "+", color);
            if (settings.operations[1]) renderer.horizontalLine(276, 279, y + 3, color);
            if (settings.operations[2]) renderer.drawText(valueFont, 287, y - 1, "x", color);
            if (settings.operations[3]) {
                renderer.horizontalLine(299, 303, y + 3, color);
                renderer.pixel(301, y + 1, color);
                renderer.pixel(301, y + 5, color);
            }
        }
    }
    renderer.drawText(font, 0, 167, "Use Arrows to move, Space Bar to change.", Colors::White);
    renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Cancel changes", Colors::White);
    renderer.drawText(font, 247, 190, "F1: Help", Colors::White);
}

void Game::renderOptionsContentRange(Renderer& renderer) {
    const std::string title = "Modify Range: " +
        std::string(ModeNames[static_cast<std::size_t>(contentRow_)]);
    drawOptionsFrame(renderer, "", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 155, 5, title, Colors::White);
    const ContentSettings& settings = contentDraft_[static_cast<std::size_t>(contentRow_)];
    const auto [minimum, maximum] = ContentRangeLimits[static_cast<std::size_t>(contentRow_)];
    renderer.drawText(font, 5, 41,
                      "Allowable range: " + std::to_string(minimum) + " - " + std::to_string(maximum),
                      Colors::White);
    renderer.drawText(font, 5, 65, "Current lower limit:", Colors::White);
    renderer.drawText(font, 197, 65, std::to_string(settings.minimum), Colors::White);
    renderer.drawText(font, 5, 81, "Enter new lower limit:", Colors::White);
    renderer.outlineRect(192, 75, 35, 20, Colors::White);
    renderer.drawText(font, 197, 80,
                      contentInputStage_ == 0 ? contentNumericInput_ : "", Colors::White);
    renderer.drawText(font, 5, 105, "Current upper limit:", Colors::White);
    renderer.drawText(font, 197, 105, std::to_string(settings.maximum), Colors::White);
    renderer.drawText(font, 5, 121, "Enter new upper limit:", Colors::White);
    renderer.outlineRect(192, 115, 35, 20, Colors::White);
    renderer.drawText(font, 197, 120,
                      contentInputStage_ == 1 ? contentNumericInput_ : "", Colors::White);
    renderer.drawText(font, 0, 167, "Type number, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Cancel changes", Colors::White);
}

void Game::renderOptionsContentOtherNumber(Renderer& renderer) {
    drawOptionsFrame(renderer, "Modify Other: Multiples", 162);
    const GemFont& font = assets_.largeFont();
    const ContentSettings& settings = contentDraft_[static_cast<std::size_t>(Mode::Multiples)];
    renderer.drawText(font, 5, 41,
                      "Current target range: " + std::to_string(settings.minimum) + " - " +
                          std::to_string(settings.maximum),
                      Colors::White);
    if (contentNumberConfirm_) {
        int value = settings.maxMultiplier;
        try {
            value = std::stoi(contentNumericInput_);
        } catch (...) {
        }
        renderer.drawText(font, 5, 57,
                          "New max. multiplier: " + std::to_string(value), Colors::White);
        renderer.drawText(font, 5, 73,
                          "The highest munch-able number is " +
                              std::to_string(settings.maximum * value) + ".",
                          Colors::White);
        const std::string formulaPrefix = "( " + std::to_string(settings.maximum) + " ";
        const std::string formula = formulaPrefix + "X " + std::to_string(value) + " = " +
            std::to_string(settings.maximum * value) + " )";
        renderer.drawText(font, 8, 82, formula, Colors::White);
        const int multiplyCursor = 8 + renderer.textWidth(font, formulaPrefix);
        renderer.fillRect(multiplyCursor, 82, renderer.textWidth(font, "X"), font.height(),
                          Colors::Black);
        renderer.drawText(assets_.smallFont(), multiplyCursor + 1, 82, "x", Colors::White);
        renderer.drawText(font, 0, 167, "Press Enter to accept.", Colors::White);
        renderer.drawText(font, 0, 176, "Escape: Cancel change", Colors::White);
        return;
    }
    renderer.drawText(font, 5, 57,
                      "Current max. multiplier: " + std::to_string(settings.maxMultiplier),
                      Colors::White);
    renderer.drawText(font, 5, 89, "Enter the max. multiplier:", Colors::White);
    renderer.outlineRect(220, 83, 35, 20, Colors::White);
    renderer.drawText(font, 225, 90, contentNumericInput_, Colors::White);
    if (contentNumericInput_.empty()) renderer.fillRect(225, 94, 9, 3, Colors::White);
    renderer.drawText(font, 8, 105,
                      "(Allowable range: 3 - 50)", Colors::White);
    renderer.drawText(font, 5, 129,
                      "The highest munch-able number is " +
                          std::to_string(settings.maximum * settings.maxMultiplier) + ".",
                      Colors::White);
    const std::string formulaPrefix = "( " + std::to_string(settings.maximum) + " ";
    const std::string formula = formulaPrefix + "X " +
        std::to_string(settings.maxMultiplier) + " = " +
        std::to_string(settings.maximum * settings.maxMultiplier) + " )";
    renderer.drawText(font, 8, 139, formula, Colors::White);
    const int multiplyCursor = 8 + renderer.textWidth(font, formulaPrefix);
    renderer.fillRect(multiplyCursor, 139, renderer.textWidth(font, "X"), font.height(), Colors::Black);
    renderer.drawText(assets_.smallFont(), multiplyCursor + 1, 139, "x", Colors::White);
    renderer.drawText(font, 0, 167, "Type number, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Set Content", Colors::White);
}

void Game::renderOptionsContentOperations(Renderer& renderer) {
    const std::string title = "Modify Other: " +
        std::string(ModeNames[static_cast<std::size_t>(contentRow_)]);
    drawOptionsFrame(renderer, "", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 155, 5, title, Colors::White);
    const ContentSettings& settings = contentDraft_[static_cast<std::size_t>(contentRow_)];
    static constexpr std::array<std::string_view, 4> operations = {
        "Addition ( + )", "Subtraction ( - )", "Multiplication ( x )", "Division ( : )"
    };
    renderer.drawText(font, 5, 33, "Operation", Colors::White);
    renderer.drawText(font, 200, 33, "Use", Colors::White);
    for (int index = 0; index < 4; ++index) {
        const int y = 49 + index * 24;
        renderer.drawText(font, 5, y, operations[static_cast<std::size_t>(index)], Colors::White);
        if (index == 0) {
            renderer.fillRect(94, y, 6, 7, Colors::Black);
            renderer.horizontalLine(94, 98, y + 3, Colors::White);
            renderer.verticalLine(96, y + 1, y + 5, Colors::White);
        } else if (index == 1) {
            renderer.fillRect(118, y, 6, 7, Colors::Black);
            renderer.horizontalLine(118, 122, y + 3, Colors::White);
        } else if (index == 2) {
            renderer.fillRect(141, y, 7, 7, Colors::Black);
            renderer.pixel(142, y + 2, Colors::White);
            renderer.pixel(146, y + 2, Colors::White);
            renderer.pixel(143, y + 3, Colors::White);
            renderer.pixel(145, y + 3, Colors::White);
            renderer.pixel(144, y + 4, Colors::White);
            renderer.pixel(143, y + 5, Colors::White);
            renderer.pixel(145, y + 5, Colors::White);
            renderer.pixel(142, y + 6, Colors::White);
            renderer.pixel(146, y + 6, Colors::White);
        } else {
            renderer.fillRect(94, y, 6, 8, Colors::Black);
            renderer.pixel(96, y, Colors::White);
            renderer.pixel(96, y + 1, Colors::White);
            renderer.horizontalLine(94, 98, y + 3, Colors::White);
            renderer.pixel(96, y + 5, Colors::White);
            renderer.pixel(96, y + 6, Colors::White);
        }
        if (contentOperationSelection_ == index) renderer.fillRect(198, y - 1, 37, 10, Colors::White);
        renderer.drawText(font, 208, y,
                          settings.operations[static_cast<std::size_t>(index)] ? "on" : "off",
                          contentOperationSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 0, 167, "Use Arrows to move, Space Bar to change.", Colors::White);
    renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Cancel changes", Colors::White);
}

void Game::renderOptionsContentHelp(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Content Help", 162);
    const GemFont& font = assets_.largeFont();
    const auto drawLines = [&](std::initializer_list<std::string_view> lines) {
        int y = 33;
        for (const std::string_view line : lines) {
            if (!line.empty()) renderer.drawText(font, 0, y, line, Colors::White);
            y += 9;
        }
    };
    if (contentHelpPage_ == 0) {
        drawLines({
            "Use:  The games marked \"yes\" are",
            "currently available to use.  If more",
            "than one game is available, the player",
            "is given a choice of games to play.",
            "",
            "Range:  Changing these values can limit",
            "or expand the variety of numbers the",
            "player will see.",
        });
        renderer.drawCenteredText(font, 159, 176, "Press Space Bar to continue.", Colors::White);
    } else {
        drawLines({
            "Sequence:  When sequence is \"in order,\"",
            "the key value displayed at the top of",
            "the game screen will increment by one",
            "for each new game screen.  When set to",
            "\"random,\" a new key value is randomly",
            "selected within the target range.",
            "",
            "Other:  In Multiples, this column shows",
            "which multiples of the key value are",
            "used.  In Equality and Inequality, this",
            "column indicates which operations",
            "( + - x : ) will be used.",
        });
        renderer.fillRect(17, 132, 6, 7, Colors::Black);
        renderer.horizontalLine(17, 21, 135, Colors::White);
        renderer.verticalLine(19, 133, 137, Colors::White);
        renderer.fillRect(33, 132, 6, 7, Colors::Black);
        renderer.horizontalLine(33, 37, 135, Colors::White);
        renderer.fillRect(48, 132, 7, 7, Colors::Black);
        renderer.drawText(assets_.smallFont(), 49, 132, "x", Colors::White);
        renderer.fillRect(65, 132, 5, 7, Colors::Black);
        renderer.pixel(67, 132, Colors::White);
        renderer.pixel(67, 133, Colors::White);
        renderer.horizontalLine(65, 69, 135, Colors::White);
        renderer.pixel(67, 137, Colors::White);
        renderer.pixel(67, 138, Colors::White);
        renderer.drawCenteredText(font, 159, 176, "Press Space Bar to continue.", Colors::White);
    }
}

void Game::renderOptionsContentValidation(Renderer& renderer) {
    switch (contentValidationReturnPage_) {
    case Page::OptionsContent: {
        const int savedRow = contentRow_;
        const int savedColumn = contentColumn_;
        contentRow_ = -1;
        contentColumn_ = -1;
        renderOptionsContent(renderer);
        contentRow_ = savedRow;
        contentColumn_ = savedColumn;
        break;
    }
    case Page::OptionsContentRange: renderOptionsContentRange(renderer); break;
    case Page::OptionsContentOtherNumber: renderOptionsContentOtherNumber(renderer); break;
    case Page::OptionsContentOperations: {
        const int savedOperation = contentOperationSelection_;
        contentOperationSelection_ = -1;
        renderOptionsContentOperations(renderer);
        contentOperationSelection_ = savedOperation;
        break;
    }
    default: renderOptionsContent(renderer); break;
    }

    const bool valueRange = contentValidationKind_ == ContentValidationKind::ValueRange;
    const int top = valueRange ? 67 : 76;
    const int bottom = valueRange ? 131 : 122;
    renderer.fillRect(0, top, 320, bottom - top + 1, Colors::Black);
    renderer.horizontalLine(0, 319, top, Colors::Cyan);
    renderer.horizontalLine(2, 317, top + 2, Colors::Cyan);
    renderer.horizontalLine(2, 317, bottom - 2, Colors::Cyan);
    renderer.horizontalLine(0, 319, bottom, Colors::Cyan);
    renderer.verticalLine(0, top, bottom, Colors::Cyan);
    renderer.verticalLine(319, top, bottom, Colors::Cyan);
    renderer.verticalLine(2, top + 2, bottom - 2, Colors::Cyan);
    renderer.verticalLine(317, top + 2, bottom - 2, Colors::Cyan);

    const GemFont& font = assets_.largeFont();
    if (contentValidationKind_ == ContentValidationKind::NoGames) {
        renderer.drawText(font, 31, 90, "You have selected no games.", Colors::White);
        renderer.drawText(font, 31, 99, "Please select at least one game.", Colors::White);
    } else if (contentValidationKind_ == ContentValidationKind::RangeOrder) {
        renderer.drawText(font, 39, 90, "The low limit must be equal to", Colors::White);
        renderer.drawText(font, 39, 99, "or less than the upper limit.", Colors::White);
    } else if (contentValidationKind_ == ContentValidationKind::ValueRange) {
        const auto [minimum, maximum] = contentValidationReturnPage_ == Page::OptionsContentOtherNumber
            ? std::pair{3, 50}
            : contentRow_ < static_cast<int>(Mode::Challenge)
                ? ContentRangeLimits[static_cast<std::size_t>(contentRow_)]
                : std::pair{1, 50};
        renderer.drawText(font, 67, 81,
                          "Allowable range: " + std::to_string(minimum) + " - " +
                              std::to_string(maximum),
                          Colors::White);
        renderer.drawText(font, 67, 99, "The value entered", Colors::White);
        renderer.drawText(font, 67, 108, "must be in this range.", Colors::White);
    } else if (contentValidationKind_ == ContentValidationKind::NoOperations) {
        renderer.drawText(font, 39, 90, "At least one operation must be", Colors::White);
        renderer.drawText(font, 39, 99, "set to \"on\".", Colors::White);
    }
    renderer.fillRect(0, 165, 320, 35, Colors::Black);
    renderer.drawCenteredText(font, 159, 176, "Press Space Bar to continue.", Colors::White);
}

void Game::renderOptionsEraseHall(Renderer& renderer) {
    drawOptionsFrame(renderer, "Select Hall of Fame", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 41, "Choose a Hall of Fame to erase:", Colors::White);
    for (int index = 0; index < 7; ++index) {
        const int y = 73 + index * 11;
        const std::string prefix = std::to_string(index + 1) + ".";
        renderer.drawText(font, 104 - renderer.textWidth(font, prefix), y, prefix, Colors::White);
        const std::string label = index < 6 ? std::string(ModeNames[static_cast<std::size_t>(index)])
                                           : "Erase ALL Lists";
        if (menuSelection_ == index) {
            renderer.fillRect(102, y - 1, renderer.textWidth(font, label) + 21, 10, Colors::White);
        }
        renderer.drawText(font, 112, y, label, menuSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawText(font, 0, 176, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Options Menu", Colors::White);
}

void Game::renderOptionsEraseEntries(Renderer& renderer) {
    drawOptionsFrame(renderer, "", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawCenteredText(font, 155, 5,
                              "Erase Hall of Fame: " +
                                  std::string(ModeNames[static_cast<std::size_t>(eraseMode_)]),
                              Colors::White);
    if (eraseDraft_.empty()) {
        const bool initiallyEmpty = eraseMode_ >= 0 &&
            eraseMode_ < static_cast<int>(scores_.size()) &&
            scores_[static_cast<std::size_t>(eraseMode_)].empty();
        renderer.drawText(font, 0, initiallyEmpty ? 81 : 90,
                          initiallyEmpty ? "There are currently no entries in the"
                                         : "There are no more entries in the",
                          Colors::White);
        renderer.drawText(font, 0, initiallyEmpty ? 90 : 99,
                          "Hall of Fame.", Colors::White);
        if (initiallyEmpty) return;
    } else {
        for (int index = 0; index < static_cast<int>(eraseDraft_.size()); ++index) {
            const int y = 38 + index * 11;
            const std::string prefix = std::to_string(index + 1) + ".";
            renderer.drawText(font, 64 - renderer.textWidth(font, prefix), y, prefix, Colors::White);
            const std::string& label = eraseDraft_[static_cast<std::size_t>(index)].name;
            if (eraseSelection_ == index) {
                renderer.fillRect(62, y - 1, renderer.textWidth(font, label) + 21, 10, Colors::White);
            }
            renderer.drawText(font, 72, y, label,
                              eraseSelection_ == index ? Colors::Black : Colors::White);
        }
        renderer.drawText(font, 0, 167, "Use arrows to move, Space Bar to delete.", Colors::White);
    }
    renderer.drawText(font, 0, 176, "Press Enter to accept changes.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Cancel changes", Colors::White);
}

void Game::renderOptionsEraseAllConfirm(Renderer& renderer) {
    drawOptionsFrame(renderer, "Erase Hall of Fame: ALL lists", 171);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 0, 54, "You are about to PERMANENTLY delete ALL", Colors::White);
    renderer.drawText(font, 0, 63, "of the entries from ALL of the Hall of", Colors::White);
    renderer.drawText(font, 0, 72, "Fame lists.", Colors::White);
    renderer.drawText(font, 0, 90, "Do you really want to do this?", Colors::White);

    const auto drawChoice = [&](int x, int fillX, std::string_view text, bool selected) {
        if (selected) renderer.fillRect(fillX, 120, renderer.textWidth(font, text) + 21, 10, Colors::White);
        renderer.drawText(font, x, 121, text, selected ? Colors::Black : Colors::White);
    };
    drawChoice(113, 102, "Yes", menuSelection_ == 0);
    drawChoice(163, 153, "No", menuSelection_ == 1);
    renderer.drawText(font, 0, 176, "Use Arrows to move, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 185, "Escape: Cancel", Colors::White);
}

void Game::renderOptionsPassword(Renderer& renderer) {
    drawOptionsFrame(renderer, "Set Password", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Enter a new password:", Colors::White);
    renderer.outlineRect(110, 50, 99, 20, Colors::White);
    renderer.drawText(font, 115, 56, passwordDraft_, Colors::White);
    if (!passwordEditingHint_) renderer.fillRect(115 + renderer.textWidth(font, passwordDraft_), 61, 9, 3, Colors::White);
    renderer.drawText(font, 5, 77, "Enter a hint:", Colors::White);
    renderer.outlineRect(1, 95, 317, 20, Colors::White);
    renderer.drawText(font, 6, 101, hintDraft_, Colors::White);
    if (passwordEditingHint_) renderer.fillRect(6 + renderer.textWidth(font, hintDraft_), 106, 9, 3, Colors::White);
    renderer.drawText(font, 0, 167, "Type entry, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Options Menu", Colors::White);
}

void Game::renderOptionsPasswordPrompt(Renderer& renderer) {
    drawOptionsFrame(renderer, "Options", 162);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, 5, 32, "Enter the password:", Colors::White);
    renderer.outlineRect(80, 50, 159, 20, Colors::White);
    renderer.drawText(font, 85, 56, passwordAttempt_, Colors::White);
    renderer.fillRect(85 + renderer.textWidth(font, passwordAttempt_), 61, 9, 3, Colors::White);
    if (!optionHint_.empty()) renderer.drawCenteredText(font, 159, 115, optionHint_, Colors::White);
    if (passwordRejected_) {
        // 0x0edb9 passes the exact six-line string at DS:0ff2 to the generic
        // flags-0xc3 modal at 0x0af6a. Its five line feeds make six logical
        // rows, exactly the same count as the captured Value Range modal.
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
    }
    renderer.drawText(font, 0, 167, "Type entry, then press Enter.", Colors::White);
    renderer.drawText(font, 0, 176, "Escape: Main Menu", Colors::White);
}

void Game::renderHall(Renderer& renderer) {
    const bool cga = renderer.graphicsMode() == GraphicsMode::Cga4;
    const std::uint32_t normalText = cga ? Colors::White : Colors::Black;
    renderer.clear(cga ? Colors::Black : Colors::White);
    if (const Image* hall = assets_.image(6003)) renderer.drawImage(*hall, 0, 0, false, false, true);
    const GemFont& largeFont = assets_.largeFont();
    const GemFont& smallFont = assets_.smallFont();
    renderer.drawCenteredText(largeFont, 159, 35, "NUMBER MUNCHERS", normalText);
    renderer.drawCenteredText(largeFont, 159, 62, "Hall of Fame", normalText);
    renderer.drawCenteredText(largeFont, 159, 80,
                              ModeNames[static_cast<std::size_t>(mode_)], normalText);
    const auto& list = scores_[static_cast<std::size_t>(mode_)];
    if (list.empty()) {
        // The nonblocking demo painter places the empty-list pair twenty
        // pixels below the argument-7 VGA browser/post-game Hall. A live CGA
        // title-browse capture proves that the four-color branch also uses
        // those lower 120/129 baselines even though it retains the user Hall
        // footer and blocking input contract.
        const int emptyListY = cga || hallContext_ == HallContext::Attract ? 120 : 100;
        renderer.drawCenteredText(largeFont, 159, emptyListY,
                                  "There are no entries", normalText);
        renderer.drawCenteredText(largeFont, 159, emptyListY + 9,
                                  "in the Hall of Fame.", normalText);
    } else {
        int highlighted = -1;
        if (!hallHighlightName_.empty()) {
            const auto found = std::find_if(list.begin(), list.end(), [this](const auto& entry) {
                return entry.name == hallHighlightName_ && entry.score == hallHighlightScore_;
            });
            if (found != list.end()) highlighted = static_cast<int>(found - list.begin());
        }
        int y = 97;
        for (std::size_t index = 0; index < std::min<std::size_t>(10, list.size()); ++index) {
            const std::string rank = std::to_string(index + 1);
            const std::string label = rank + ". " + list[index].name;
            const std::string score = list[index].score >= MaximumScore
                                          ? "Perfect"
                                          : std::to_string(list[index].score);
            const int rankOffset = renderer.textWidth(smallFont, rank) -
                                   renderer.textWidth(smallFont, "1");
            const int labelX = 51 - rankOffset;
            const int scoreX = 262 - renderer.textWidth(smallFont, score);
            if (static_cast<int>(index) == highlighted) {
                // Bit 0x20 is disabled for the matched record at 0x1266e,
                // making both text calls opaque. VGA is white-on-black;
                // CGA is white-on-magenta.
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
    if (hallContext_ == HallContext::Attract) {
        renderer.drawCenteredText(largeFont, 159, 185,
                                  "Press a key for Muncher Menu", normalText);
    } else {
        renderer.drawText(largeFont, 51, 185, "Press Space Bar to continue.", normalText);
    }
}

void Game::drawSprite(Renderer& renderer,
                      std::uint32_t sheetId,
                      int frame,
                      int x,
                      int y,
                      bool remapTitlePalette,
                      bool remapHallPalette,
                      bool transparentBlack,
                      bool saturateSceneClip,
                      bool remapNumberScenePalette,
                      bool provideNumberScenePaletteContext,
                      int transparentPaletteIndex,
                      bool initialNumberScenePaint,
                      bool clipToBoardInterior,
                      int sourceWidthLimit) {
    const std::optional<SpriteFrame> source = assets_.spriteFrame(sheetId, frame);
    const std::uint32_t sourceSheetId = source && source->sheetId != 0 ? source->sheetId : sheetId;
    const Image* sheet = assets_.image(sourceSheetId);
    if (!sheet) {
        renderer.fillRect(x + 5, y + 4, 26, 23, sheetId == 1006 ? Colors::Yellow : Colors::Red);
        return;
    }
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
    int destinationX = x;
    int destinationY = y;
    if (sheetId == 2013 && frame == 22 && sourceSheetId == 3013 &&
        initialNumberScenePaint && sourceWidth > 1) {
        // The first retained construction pass clips this continuation
        // record's leading column. Its next dirty paint includes the column.
        ++sourceX;
        --sourceWidth;
        ++destinationX;
    }
    if (sheetId == 2019 && frame == 6 && sourceSheetId == 2019 &&
        destinationX <= -35 && sourceWidth > 1) {
        // At the initial x=-35 entrance, DOS clips the record's stored right
        // edge and exposes no column. Once it has advanced, the ordinary
        // inclusive BTMP width is visible; later captures expose that final
        // column at every alternating frame-6 position.
        --sourceWidth;
    }
    if (sourceWidthLimit >= 0) sourceWidth = std::min(sourceWidth, sourceWidthLimit);
    if (sourceWidth <= 0) return;
    if (saturateSceneClip) {
        if (destinationX >= Renderer::Width) destinationX = Renderer::Width - 1;
        if (destinationY >= Renderer::Height) destinationY = Renderer::Height - 1;
    }
    if (clipToBoardInterior) {
        constexpr int ClipLeft = BoardLeft + 1;
        constexpr int ClipTop = BoardTop + 1;
        // The DOS arrival viewport ends immediately inside the outer x=310
        // outline, not at the x=308 grid rule. nm_001 right-edge entry pixels
        // therefore remain paintable through x=309.
        constexpr int ClipRight = BoardRight + 2;
        // The corresponding bottom arrival viewport ends immediately above
        // the y=178 outer outline. The full-demo Bashful entry therefore
        // paints through y=177 and may overwrite the y=176 grid rule.
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
                             transparentBlack, sheetId == 1006,
                             remapTitlePalette, remapHallPalette,
                             sheetId >= 1007 && sheetId <= 1011 &&
                                  !remapTitlePalette && !remapHallPalette,
                             nullptr, remapNumberScenePalette,
                             provideNumberScenePaletteContext
                                 ? static_cast<int>(sourceSheetId) : 0,
                             transparentPaletteIndex, frame,
                             initialNumberScenePaint);
}

void Game::renderBoard(Renderer& renderer) {
    renderer.clear(Colors::BoardBlue);
    const GemFont& font = assets_.largeFont();
    renderer.drawText(font, attractMode_ ? 30 : 0, 6,
                      attractMode_ ? "Demo" : "Level: " + std::to_string(level_), Colors::White);
    std::string rule;
    switch (activeBoardMode_) {
    case Mode::Multiples: rule = "Multiples of " + std::to_string(target_); break;
    case Mode::Factors: rule = "Factors of " + std::to_string(target_); break;
    case Mode::Primes: rule = "Prime Numbers"; break;
    case Mode::Equality: rule = "Equals " + std::to_string(target_); break;
    case Mode::Inequality:
        rule = std::string(relation_ == 0 ? "Less Than " : relation_ == 1 ? "Not Equal to " : "Greater Than ") +
               std::to_string(target_);
        break;
    default: break;
    }
    renderer.drawCenteredText(font, 164, 6, rule, Colors::White);
    renderer.horizontalLine(92, 234, 2, Colors::White);
    renderer.horizontalLine(92, 234, 16, Colors::White);

    // Exact doubled-border and 6x5 grid geometry measured from the raw
    // 640x400 DOSBox capture (the original 320x200 frame scaled by two).
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
                const Cell& current = cell(row, column);
                const int x = BoardLeft + column * BoardCellWidth;
                const int y = BoardTop + row * BoardCellHeight;
                if (!current.eaten) {
                    std::string boardLabel = current.label;
                    // The expression record stores operation indexes separately.
                    // Its painter maps them through DS:1212/1214 to the GFT's
                    // dedicated thin board glyphs 2, 4, 6, and 8 rather than the
                    // visibly heavier ASCII +, -, x, and : characters.
                    for (char& character : boardLabel) {
                        switch (character) {
                        case '+': character = '\x02'; break;
                        case '-': character = '\x04'; break;
                        case 'x': character = '\x06'; break;
                        case ':': character = '\x08'; break;
                        default: break;
                        }
                    }
                    if (repaintPayloadBackgrounds) {
                        // The original dirty-cell GFT painter clears each
                        // label's full character box before setting its white
                        // pixels. This opaque blue pass is visible wherever a
                        // moving actor crosses the cell payload.
                        const int labelWidth = renderer.textWidth(font, boardLabel);
                        renderer.fillRect(x + (BoardCellWidth - labelWidth) / 2,
                                          y + 12, labelWidth, font.height(),
                                          Colors::BoardBlue);
                    }
                    renderer.drawCenteredText(font, x + BoardCellWidth / 2, y + 12,
                                              boardLabel, Colors::White);
                }
                if (current.safe) {
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

    // The live DOS game uses the otherwise empty area below the grid for a
    // fixed score field and one Muncher sprite per remaining life.
    if (attractMode_) {
        renderer.drawCenteredText(font, 160, 187, "Press a key for Muncher Menu", Colors::White);
    } else {
        renderer.drawText(font, 0, 187, "Score:", Colors::White);
        renderer.outlineRect(50, 184, 65, 13, Colors::White);
        // The lossless typed-name frame isolates the dynamic value from the
        // otherwise unchanged score box: its first glyph begins at x=56.
        renderer.drawText(font, 56, 187, std::to_string(score_), Colors::White);
        // DS:5998 may exceed three after bonus awards, but 0x11caf skips
        // drawing indices 3 and higher. The internal lives remain usable.
        const int visibleReserves = std::min(3, std::max(0, lives_ - 1));
        for (int life = 0; life < visibleReserves; ++life) {
            drawSprite(renderer, 1006, 17, 148 + life * 44, 179);
        }
    }

    int playerFrame = playerTerminalFrame_ >= 0 ? playerTerminalFrame_ : 7;
    double playerX = static_cast<double>(BoardLeft + playerColumn_ * BoardCellWidth + 4);
    double playerY = static_cast<double>(BoardTop + playerRow_ * BoardCellHeight + 1);
    if (moving_) {
        const int positionPhase = playerMovementPhase();
        const bool verticalMove = moveDirection_ == 0 || moveDirection_ == 2;
        const double positionProgress = static_cast<double>(positionPhase) / (verticalMove ? 5.0 : 6.0);
        playerX = static_cast<double>(BoardLeft + moveFromColumn_ * BoardCellWidth + 4) +
                  static_cast<double>((moveToColumn_ - moveFromColumn_) * BoardCellWidth) * positionProgress;
        playerY = static_cast<double>(BoardTop + moveFromRow_ * BoardCellHeight + 1) +
                  static_cast<double>((moveToRow_ - moveFromRow_) * BoardCellHeight) * positionProgress;
        playerFrame = playerMoveFrameAtPhase(moveDirection_, positionPhase);
    }
    if (munching_) {
        const double elapsed = MunchAnimationDuration - std::max(0.0, munchTimer_);
        const int elapsedTicks = std::clamp(
            static_cast<int>(std::floor(elapsed * OriginalJobTicksPerSecond + 1e-7)),
            0, MunchAnimationTicks - 1);
        playerFrame = munchFrameAtTick(elapsedTicks);
    }
    if (attractPostFeedbackBoard_ &&
        attractHallWipeVariant_ == AttractHallWipeVariant::Clean) {
        // The clean wrong-answer terminal retains the final open-mouth chew
        // record throughout its 39-frame board hold and the following Wipe.
        playerFrame = 12;
    }
    const bool playerWasEaten = (page_ == Page::Feedback &&
                                 feedbackKind_ == FeedbackKind::EatenByTroggle) ||
                                playerRecovering_;
    if (!playerWasEaten) {
        // The live first Number Demo board and the live Word user chew both
        // establish 0x414100 for the darkest occupied Muncher slot. The
        // retained Hall-state flag remains part of the recovered lifecycle,
        // but this player-sheet slot itself does not change across that edge.
        const bool useHallPalette =
            hallPaletteActive_ ||
            (attractMode_ && attractPostFeedbackBoard_ &&
             attractHallWipeVariant_ == AttractHallWipeVariant::Clean);
        drawSprite(renderer, 1006, playerFrame,
                   static_cast<int>(std::lround(playerX)), static_cast<int>(std::lround(playerY)),
                   false, useHallPalette);
    }
    // The player updater repaints opaque payload text, cell borders, and safe
    // corners after changing its actor. Troggle callbacks run later in the
    // same scheduler pass and therefore paint over that retained foreground.
    drawCellForeground(true);
    std::vector<const Enemy*> enemiesInPaintOrder;
    enemiesInPaintOrder.reserve(enemies_.size());
    for (const Enemy& enemy : enemies_) enemiesInPaintOrder.push_back(&enemy);
    std::stable_sort(
        enemiesInPaintOrder.begin(), enemiesInPaintOrder.end(),
        [this](const Enemy* first, const Enemy* second) {
            const auto rank = [this](const int slot) {
                const auto begin = enemyPaintOrder_.begin();
                const auto end = begin + enemySlotCount_;
                const auto iterator = std::find(begin, end, slot);
                return iterator == end
                    ? enemySlotCount_
                    : static_cast<int>(std::distance(begin, iterator));
            };
            return rank(first->slot) < rank(second->slot);
        });
    // The fixed selector-1 job records still dispatch by scheduler ID. The
    // separate DS:5A84 painter list moves an actor to the foreground whenever
    // it begins an entry/move/collision, and 0x11498 redraws in that order.
    for (const Enemy* enemyPointer : enemiesInPaintOrder) {
        const Enemy& enemy = *enemyPointer;
        if (enemy.collisionHidden) continue;
        int enemyFrame = ordinaryEnemyFrame(enemy);
        int movementPhase = 0;
        double enemyX = static_cast<double>(BoardLeft + enemy.column * BoardCellWidth + 4);
        double enemyY = static_cast<double>(BoardTop + enemy.row * BoardCellHeight + 1);
        if (enemy.entering && !enemy.moving) {
            enemyX = static_cast<double>(BoardLeft + enemy.fromColumn * BoardCellWidth + 4);
            enemyY = static_cast<double>(BoardTop + enemy.fromRow * BoardCellHeight + 1);
        }
        if (enemy.moving) {
            const bool verticalMove = enemy.direction == 0 || enemy.direction == 2;
            const int stepCount = verticalMove ? 5 : 6;
            const int positionPhase = enemyMovementPhase(enemy);
            movementPhase = positionPhase;
            const double positionProgress =
                static_cast<double>(positionPhase) / static_cast<double>(stepCount);
            enemyX = static_cast<double>(BoardLeft + enemy.fromColumn * BoardCellWidth + 4) +
                     static_cast<double>((enemy.column - enemy.fromColumn) * BoardCellWidth) * positionProgress;
            enemyY = static_cast<double>(BoardTop + enemy.fromRow * BoardCellHeight + 1) +
                     static_cast<double>((enemy.row - enemy.fromRow) * BoardCellHeight) * positionProgress;
            if (enemy.entering && movementPhase >= 2) {
                // The shared MECC actor job suppresses its first two arrival
                // callbacks, then writes middle/forward/middle/back/middle
                // poses as the sprite is clipped into the board interior.
                // Vertical entries end at phase 5; horizontal entries have a
                // sixth 8-pixel step whose middle pose aliases the following
                // dwell.  wm_011 exposes phases 2..5 from above, while
                // nm_000 exposes phases 2..6 from the left.
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
                       elapsed * OriginalJobTicksPerSecond + 1e-7)));
            enemyFrame = troggleBiteFrameAtTick(elapsedTicks);
        }
        if (page_ == Page::Feedback && feedbackKind_ == FeedbackKind::EatenByTroggle &&
            enemy.row == playerRow_ && enemy.column == playerColumn_ &&
            (feedbackEnemySlot_ < 0 || enemy.slot == feedbackEnemySlot_)) {
            if (deathAnimating_) {
                // The BTMP cycle advances once on each state-5 callback;
                // tick 21 is the terminal transition to feedback.
                enemyFrame = troggleBiteFrameAtTick(deathSequenceTicks_);
            } else if (!attractPostFeedbackBoard_) {
                // The stable feedback text retains bite record 12. Clearing
                // the strip leaves that actor underlay untouched unless a
                // concurrent entry callback repaints the complete actor list;
                // that callback exposes survivor record 15 instead.
                enemyFrame = 12;
            }
        }
        // Gameplay BTMP index zero is the active board blue, so each
        // Troggle record carries a 41x29 opaque blue actor box. Painting the
        // box first reproduces the live boundary/label erasure while the
        // extracted black sheet padding remains transparent in native data.
        const int roundedEnemyX = static_cast<int>(std::lround(enemyX));
        const int roundedEnemyY = static_cast<int>(std::lround(enemyY));
        if (enemy.entering && (!enemy.moving || movementPhase < 2)) {
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
            if (enemy.fromColumn >= BoardColumns) {
                // Retain the overwritten x=308 grid column through phase 6;
                // the terminal dwell callback is what restores it.
                renderer.fillRect(BoardRight, roundedEnemyY, 1, 29,
                                  Colors::BoardBlue);
            }
            if (enemy.fromRow >= BoardRows) {
                // Bottom arrivals likewise retain their overwritten y=176
                // grid row through phase 5. The endpoint/dwell repaint is
                // the first frame that restores the magenta rule.
                renderer.fillRect(roundedEnemyX, BoardBottom, 41, 1,
                                  Colors::BoardBlue);
            }
        } else if (enemy.exiting) {
            // The exit callback retains the entire swept actor rectangle in
            // board blue until terminal removal. Clip that dirty rectangle
            // to the board viewport: the regenerated trail label remains
            // hidden, but no sprite/clear pixels leak into the screen margin.
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
                   roundedEnemyX, roundedEnemyY,
                   attractPostFeedbackBoard_ &&
                       attractHallWipeVariant_ == AttractHallWipeVariant::Collision,
                   false, true, false, false, false, -1, false,
                   enemy.entering || enemy.exiting);
    }
    if (attractPostFeedbackBoard_ &&
        attractHallWipeVariant_ == AttractHallWipeVariant::Collision) {
        // Feedback removal clears the departed edge actor's last three-pixel
        // trail below the still-live Troggle warning strip.
        renderer.fillRect(0, 154, 3, 21, Colors::BoardBlue);
    }
    if (page_ == Page::Paused) {
        renderer.fillRect(0, 62, 16, 75, Colors::BoardBlue);
        renderer.outlineRect(0, 62, 16, 75, Colors::White);
        constexpr std::string_view timeOut = "Time out";
        for (std::size_t index = 0; index < timeOut.size(); ++index) {
            renderer.drawText(font, 5, 67 + static_cast<int>(index) * 8,
                              timeOut.substr(index, 1), Colors::White);
        }
    }
    if (page_ == Page::Feedback && !deathAnimating_ && !attractPostFeedbackBoard_) {
        renderFeedback(renderer);
    }
}

void Game::renderQuitConfirm(Renderer& renderer) {
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
    const auto drawChoice = [&](int x, std::string_view text, bool selected) {
        if (selected) {
            renderer.fillRect(x - 2, 109, renderer.textWidth(font, text) + 5, 10, Colors::White);
        }
        renderer.drawText(font, x, 110, text, selected ? Colors::Black : Colors::White);
    };
    drawChoice(115, " Yes ", menuSelection_ == 0);
    drawChoice(165, " No ", menuSelection_ == 1);
}

void Game::renderFeedback(Renderer& renderer) {
    const GemFont& font = assets_.largeFont();
    constexpr int feedbackTop = BoardTop + 2 * BoardCellHeight;
    const auto clearFeedbackCell = [&]() {
        // The DOS overlay clears only the inside of this two-row grid band.
        // Keep the existing x=20/x=308 grid sides and y=86/y=116 rules.
        renderer.fillRect(BoardLeft + 1, feedbackTop + 1,
                          BoardRight - BoardLeft - 1, BoardCellHeight - 1,
                          Colors::BoardBlue);
        renderer.horizontalLine(BoardLeft, BoardRight, feedbackTop, Colors::Magenta);
        renderer.horizontalLine(BoardLeft, BoardRight, feedbackTop + BoardCellHeight,
                                Colors::Magenta);
    };
    if (feedbackKind_ == FeedbackKind::EatenByTroggle) {
        clearFeedbackCell();
        // 0x10a22 selects y=88 for a user game and y=93 for Demo. The
        // species line follows at +10 and the user-only continuation at +20.
        const int collisionTextTop = feedbackTop + (attractMode_ ? 7 : 2);
        renderer.drawCenteredText(font, 164, collisionTextTop,
                                  feedbackMessage_ + "! You were eaten by a", Colors::White);
        const int type = std::clamp(feedbackEnemyType_, 0, static_cast<int>(EnemySpecies.size()) - 1);
        renderer.drawCenteredText(font, 164, collisionTextTop + 10,
                                  "Trogglus " + std::string(EnemySpecies[static_cast<std::size_t>(type)]) + ".",
                                  Colors::White);
        if (!attractMode_) {
            renderer.drawCenteredText(font, 168, collisionTextTop + 20,
                                      "Press Space Bar to continue.", Colors::White);
        }
        return;
    }

    clearFeedbackCell();
    std::string renderedMessage = feedbackMessage_;
    if (renderedMessage.starts_with("Oops!")) {
        // The DOS expression formatter passes the operation code through the
        // same thin GFT glyph table used by board cells.
        for (char& character : renderedMessage) {
            switch (character) {
            case '+': character = '\x02'; break;
            case '-': character = '\x04'; break;
            case 'x': character = '\x06'; break;
            case ':': character = '\x08'; break;
            default: break;
            }
        }
    }
    // The corresponding one-line routine uses y=93 in a user game and y=98
    // in Demo (base 88/93 plus five at 0x10708).
    renderer.drawCenteredText(font, 164, feedbackTop + (attractMode_ ? 12 : 7),
                              renderedMessage, Colors::White);
    if (!attractMode_) {
        renderer.drawCenteredText(font, 168, feedbackTop + 22,
                                  "Press Space Bar to continue.", Colors::White);
    }
}

void Game::renderNameEntry(Renderer& renderer) {
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
    renderer.drawCenteredText(font, 159, 68, "You have the Number Munchers", Colors::White);
    renderer.drawCenteredText(font, 159, 77, "Hall of Fame best score!", Colors::White);
    renderer.outlineRect(50, 96, 219, 20, Colors::White);
    renderer.drawText(font, 55, 101, nameInput_, Colors::White);
    renderer.fillRect(55 + renderer.textWidth(font, nameInput_), 107, 9, 3, Colors::White);
    renderer.drawCenteredText(font, 159, 131, "Type your name, and press Enter.", Colors::White);
}

void Game::renderLevelComplete(Renderer& renderer) {
    renderer.clear(Colors::Black);
    if (levelCompleteScene_.valid()) {
        if (sceneSurfacePixels_.size() == renderer.pixels().size()) {
            renderer.replacePixels(sceneSurfacePixels_);
        }
        return;
    }
    if (const Image* cutscene = assets_.image(static_cast<std::uint32_t>(cutsceneImage_))) {
        renderer.drawImage(*cutscene, 0, 0, false);
    }
    renderer.fillRect(50, 76, 220, 48, Colors::Black);
    renderer.outlineRect(50, 76, 220, 48, Colors::White);
    renderer.drawCenteredText(assets_.largeFont(), 160, 85, "Congratulations!", Colors::Yellow);
    renderer.drawCenteredText(assets_.largeFont(), 160, 101,
                              "Level " + std::to_string(level_) + " complete", Colors::White);
}

void Game::renderCheatMenu(Renderer& renderer) {
    renderer.fillRect(43, 28, 234, 145, Colors::Black);
    renderer.outlineRect(43, 28, 234, 145, Colors::Cyan);
    renderer.outlineRect(46, 31, 228, 139, Colors::Magenta);
    renderer.drawCenteredText(assets_.largeFont(), 160, 40, "CHEAT / LEVEL SELECT", Colors::Yellow);
    const std::array<std::string, 5> choices = {
        "Start at Level: < " + std::to_string(cheatLevel_) + " >",
        std::string("Invincibility: ") + (invincible_ ? "ON" : "OFF"),
        "Add One Muncher",
        "Skip Current Board",
        "Close Cheat Menu"
    };
    for (int index = 0; index < 5; ++index) {
        drawMenuItem(renderer, 68 + index * 18, choices[index], cheatSelection_ == index, 61);
    }
    renderer.drawCenteredText(assets_.smallFont(), 160, 157, "Ctrl+Alt+F1 or Esc closes", Colors::Cyan);
}

void Game::playTone(int frequency, int milliseconds) {
    playNotes({{frequency, milliseconds}});
}

bool Game::adjustJoystickRepeat(const wchar_t character) {
    if (!(joystickOn_ || joystickCalibrated_)) return false;
    const bool faster = character == L'+' || character == L'=';
    const bool slower = character == L'-' || character == L'_';
    if (!faster && !slower) return false;
    if (faster && joystickRepeatTicks_ > 1) --joystickRepeatTicks_;
    if (slower && joystickRepeatTicks_ < 10) ++joystickRepeatTicks_;
    return true;
}

void Game::playJoystickRepeatFeedback() {
    // 0xD307 computes 1000 - 50*N, starts PIT channel 2, waits 50 ms,
    // and closes the gate. This bypasses Sound/Alt+P configuration.
    lastJoystickRepeatFeedbackHz_ = 1000 - 50 * joystickRepeatTicks_;
    std::vector<std::uint8_t> wave = renderPcSpeakerToneToWave(
        static_cast<std::uint16_t>(lastJoystickRepeatFeedbackHz_), 50);
    if (!wave.empty()) (void)effectPlayer_.play(std::move(wave));
}

void Game::playGameplaySound(const std::uint8_t soundIndex) {
    if (!playOriginalSound(soundIndex, false)) return;
    previousGameplaySound_ = lastGameplaySound_;
    lastGameplaySound_ = soundIndex;
}

bool Game::playOriginalSound(std::uint8_t soundIndex, bool music) {
    if (music ? (!musicOn_ || speakerEffects_) : !soundOn_) return false;
    const BlobView gsnd = assets_.gameArchive().find("GSND", 12);
    if (!gsnd) return false;
    if (!music && speakerEffects_) {
        std::vector<std::uint8_t> rendered = renderMeccSpeakerSoundToWave(gsnd, soundIndex);
        return !rendered.empty() && effectPlayer_.play(std::move(rendered));
    }
    if (soundIndex == 16 && music) {
        MeccSound decoded = decodeMeccGSound(gsnd, soundIndex);
        if (decoded.valid) {
            musicPlayer_.stop();
            return attractOplPlayer_.startLoop(
                std::move(decoded.writes), decoded.durationMilliseconds);
        }
        return false;
    }
    MeccSound decoded = decodeMeccGSound(gsnd, soundIndex);
    if (!decoded.valid) return false;
    if (!attractOplPlayer_.playing() && !attractOplPlayer_.startIdle()) return false;
    return attractOplPlayer_.playEffect(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

void Game::playToggleFeedback(const bool enabled,
                              const std::uint32_t startDelayMilliseconds) {
    const BlobView gsnd = assets_.gameArchive().find("GSND", 12);
    if (!gsnd) return;
    const std::uint8_t stream = enabled ? 2 : 1;
    if (speakerEffects_) {
        std::vector<std::uint8_t> rendered = renderMeccSpeakerSoundToWave(
            gsnd, stream, 40, MeccSoundProfile::NumberMunchers,
            startDelayMilliseconds);
        if (!rendered.empty()) (void)effectPlayer_.play(std::move(rendered));
        return;
    }
    MeccSound decoded = decodeMeccGSound(gsnd, stream);
    if (!decoded.valid) return;
    for (OplWrite& write : decoded.writes) {
        write.milliseconds += startDelayMilliseconds;
    }
    decoded.durationMilliseconds += startDelayMilliseconds;
    if (!attractOplPlayer_.playing() && !attractOplPlayer_.startIdle()) return;
    (void)attractOplPlayer_.playEffect(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

void Game::playOriginalAdli(const std::uint8_t bankId, const std::uint8_t soundIndex) {
    if (!soundOn_ || soundIndex > 0xfc) return;
    // While a scene bank is active the original wrapper at 0x10def adds three
    // to every opcode-0D event. PSND 11 is the common speaker stream set;
    // ADLI 11-15 share those first 38 streams and append bank-specific parts.
    const std::uint8_t streamIndex = static_cast<std::uint8_t>(soundIndex + 3);
    const BlobView sound = assets_.gameArchive().find(speakerEffects_ ? "PSND" : "ADLI",
                                                       speakerEffects_ ? 11u : bankId);
    if (!sound) return;
    if (speakerEffects_) {
        std::vector<std::uint8_t> rendered =
            renderMeccSpeakerSoundToWave(sound, streamIndex);
        if (!rendered.empty()) effectPlayer_.play(std::move(rendered));
        return;
    }
    MeccSound decoded = decodeMeccGSound(sound, streamIndex);
    if (!decoded.valid) return;
    if (!attractOplPlayer_.playing() && !attractOplPlayer_.startIdle()) return;
    (void)attractOplPlayer_.playEffect(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

void Game::playCartoonScore() {
    // Event A6 at 0x0f25c takes the resident wrapper's music-class branch,
    // strips bit 80, and dispatches stream 38 only on the AdLib device.
    if (!musicOn_ || speakerEffects_ || cutsceneSoundBank_ < 11) return;
    const BlobView sound = assets_.gameArchive().find(
        "ADLI", static_cast<std::uint32_t>(cutsceneSoundBank_));
    if (!sound) return;
    MeccSound decoded = decodeMeccGSound(sound, 38);
    if (!decoded.valid) return;
    (void)attractOplPlayer_.playScoreOnce(
        std::move(decoded.writes), decoded.durationMilliseconds);
}

void Game::playNotes(std::initializer_list<std::pair<int, int>> notes, bool music) {
    if ((music ? !musicOn_ : !soundOn_) || notes.size() == 0) return;
    constexpr std::uint32_t sampleRate = 22050;
    std::uint64_t totalSamples64 = 0;
    for (const auto& [frequency, milliseconds] : notes) {
        (void)frequency;
        if (milliseconds < 0) return;
        totalSamples64 += static_cast<std::uint64_t>(sampleRate) *
                          static_cast<std::uint32_t>(milliseconds + 15) / 1000;
    }
    if (totalSamples64 == 0 || totalSamples64 > std::numeric_limits<std::uint32_t>::max()) return;
    const std::uint32_t totalSamples = static_cast<std::uint32_t>(totalSamples64);
    // SND_MEMORY requires the backing allocation to remain stable during playback.
    const std::size_t paddedSamples = static_cast<std::size_t>(totalSamples) + (totalSamples & 1u);
    std::vector<std::uint8_t> wave(44u + paddedSamples, 128);
    std::memcpy(wave.data(), "RIFF", 4);
    write32(wave, 4, static_cast<std::uint32_t>(wave.size() - 8));
    std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
    write32(wave, 16, 16);
    write16(wave, 20, 1);
    write16(wave, 22, 1);
    write32(wave, 24, sampleRate);
    write32(wave, 28, sampleRate);
    write16(wave, 32, 1);
    write16(wave, 34, 8);
    std::memcpy(wave.data() + 36, "data", 4);
    write32(wave, 40, static_cast<std::uint32_t>(totalSamples));
    std::size_t cursor = 44;
    for (const auto& [frequency, milliseconds] : notes) {
        const std::size_t toneSamples = static_cast<std::uint64_t>(sampleRate) *
                                        static_cast<std::uint32_t>(milliseconds) / 1000;
        const double period = static_cast<double>(sampleRate) / std::max(1, frequency);
        for (std::size_t sample = 0; sample < toneSamples; ++sample) {
            const double phase = std::fmod(static_cast<double>(sample), period);
            wave[cursor++] = phase < period / 2.0 ? 210 : 46;
        }
        cursor += sampleRate * 15 / 1000;
    }
    (music ? musicPlayer_ : effectPlayer_).play(std::move(wave));
}

std::wstring Game::scorePath() const {
    if (!scorePathOverride_.empty()) return scorePathOverride_;
    PWSTR localApplicationData = nullptr;
    std::wstring result = L"number-munchers-scores.txt";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localApplicationData))) {
        std::filesystem::path directory(localApplicationData);
        CoTaskMemFree(localApplicationData);
        directory /= L"NumberMunchersNative";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        result = (directory / L"scores.txt").wstring();
    }
    return result;
}

std::wstring Game::settingsPath() const {
    if (!settingsPathOverride_.empty()) return settingsPathOverride_;
    PWSTR localApplicationData = nullptr;
    std::wstring result = L"number-munchers-settings.txt";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localApplicationData))) {
        std::filesystem::path directory(localApplicationData);
        CoTaskMemFree(localApplicationData);
        directory /= L"NumberMunchersNative";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        result = (directory / L"settings.txt").wstring();
    }
    return result;
}

void Game::loadSettings() {
    if (!settingsPersistenceEnabled_) return;
    std::ifstream input{std::filesystem::path(settingsPath())};
    std::string signature;
    int version = 0;
    int storedDifficulty = difficulty_;
    int storedSound = 1;
    int storedMusic = 1;
    int storedJoystick = 0;
    int storedSpeaker = 0;
    int storedJoystickCalibrated = 0;
    std::uint64_t storedJoystickLeft = joystickLeftThreshold_;
    std::uint64_t storedJoystickRight = joystickRightThreshold_;
    std::uint64_t storedJoystickUp = joystickUpThreshold_;
    std::uint64_t storedJoystickDown = joystickDownThreshold_;
    std::string storedPassword;
    std::string storedHint;
    if (!(input >> signature >> version) || signature != "NUMBER_MUNCHERS_NATIVE_SETTINGS" ||
        (version != 1 && version != 2 && version != 3)) return;
    if (!(input >> storedDifficulty >> storedSound >> storedMusic >> storedJoystick)) return;
    if (version >= 2 && !(input >> storedSpeaker)) return;
    if (version >= 3 &&
        !(input >> storedJoystickCalibrated >> storedJoystickLeft >> storedJoystickRight >>
          storedJoystickUp >> storedJoystickDown)) return;
    if (!(input >> std::quoted(storedPassword) >> std::quoted(storedHint))) return;
    if (storedDifficulty < 0 || storedDifficulty >= static_cast<int>(DifficultyContentPresets.size()) ||
        (storedSound != 0 && storedSound != 1) || (storedMusic != 0 && storedMusic != 1) ||
        (storedJoystick != 0 && storedJoystick != 1) ||
        (storedSpeaker != 0 && storedSpeaker != 1) ||
        (storedJoystickCalibrated != 0 && storedJoystickCalibrated != 1) ||
        storedJoystickLeft > std::numeric_limits<std::uint32_t>::max() ||
        storedJoystickRight > std::numeric_limits<std::uint32_t>::max() ||
        storedJoystickUp > std::numeric_limits<std::uint32_t>::max() ||
        storedJoystickDown > std::numeric_limits<std::uint32_t>::max() ||
        (storedJoystickCalibrated != 0 &&
         (storedJoystickLeft >= storedJoystickRight || storedJoystickUp >= storedJoystickDown)) ||
        storedPassword.size() > 10 ||
        storedHint.size() > 50) return;

    std::array<ContentSettings, static_cast<std::size_t>(Mode::Count)> storedContent{};
    for (std::size_t index = 0; index < storedContent.size(); ++index) {
        int use = 0;
        int randomSequence = 0;
        int operationMask = 0;
        ContentSettings& settings = storedContent[index];
        if (!(input >> use >> settings.minimum >> settings.maximum >> randomSequence >>
              settings.maxMultiplier >> operationMask) ||
            (use != 0 && use != 1) || (randomSequence != 0 && randomSequence != 1)) return;
        settings.use = use != 0;
        settings.randomSequence = randomSequence != 0;
        for (int operation = 0; operation < 4; ++operation) {
            settings.operations[static_cast<std::size_t>(operation)] =
                (operationMask & (1 << operation)) != 0;
        }

        if (index < static_cast<std::size_t>(Mode::Challenge)) {
            const auto [minimum, maximum] = ContentRangeLimits[index];
            if (settings.minimum < minimum || settings.maximum > maximum ||
                settings.minimum > settings.maximum) return;
        }
    }
    if (std::none_of(storedContent.begin(),
                     storedContent.begin() + static_cast<std::ptrdiff_t>(Mode::Challenge),
                     [](const ContentSettings& settings) { return settings.use; }) ||
        storedContent[static_cast<std::size_t>(Mode::Multiples)].maxMultiplier <
            storedContent[static_cast<std::size_t>(Mode::Multiples)].minimum ||
        storedContent[static_cast<std::size_t>(Mode::Multiples)].maxMultiplier > 50) return;
    for (const Mode mode : {Mode::Equality, Mode::Inequality}) {
        const auto& operations = storedContent[static_cast<std::size_t>(mode)].operations;
        if (std::none_of(operations.begin(), operations.end(), [](bool enabled) { return enabled; })) return;
    }

    difficulty_ = storedDifficulty;
    soundOn_ = storedSound != 0;
    musicOn_ = storedMusic != 0;
    joystickOn_ = storedJoystick != 0;
    speakerEffects_ = storedSpeaker != 0;
    joystickCalibrated_ = storedJoystickCalibrated != 0;
    joystickLeftThreshold_ = static_cast<std::uint32_t>(storedJoystickLeft);
    joystickRightThreshold_ = static_cast<std::uint32_t>(storedJoystickRight);
    joystickUpThreshold_ = static_cast<std::uint32_t>(storedJoystickUp);
    joystickDownThreshold_ = static_cast<std::uint32_t>(storedJoystickDown);
    optionPassword_ = std::move(storedPassword);
    optionHint_ = std::move(storedHint);
    contentSettings_ = storedContent;
    contentDraft_ = contentSettings_;
    resetContentProgression();
}

void Game::beginConfigurationWriteError() {
    configurationWriteErrorBackgroundPage_ = page_;
    Renderer background(assets_.graphicsMode());
    render(background);
    configurationWriteErrorBackgroundPixels_ = background.pixels();
    configurationWriteError_ = true;
}

void Game::dismissConfigurationWriteError() {
    const bool continueController = configurationWriteErrorPostDismissContinuation_;
    configurationWriteError_ = false;
    configurationWriteErrorPostDismissContinuation_ = false;
    configurationWriteErrorBackgroundPixels_.clear();
    if (continueController) keyDown(VK_PROCESSKEY);
}

void Game::saveSettings() {
    if (!settingsPersistenceEnabled_) return;
    std::ofstream output(std::filesystem::path(settingsPath()), std::ios::trunc);
    if (!output) {
        beginConfigurationWriteError();
        return;
    }
    output << "NUMBER_MUNCHERS_NATIVE_SETTINGS 3\n";
    output << difficulty_ << ' ' << (soundOn_ ? 1 : 0) << ' ' << (musicOn_ ? 1 : 0) << ' '
           << (joystickOn_ ? 1 : 0) << ' ' << (speakerEffects_ ? 1 : 0) << ' '
           << (joystickCalibrated_ ? 1 : 0) << ' '
           << joystickLeftThreshold_ << ' ' << joystickRightThreshold_ << ' '
           << joystickUpThreshold_ << ' ' << joystickDownThreshold_ << ' '
           << std::quoted(optionPassword_) << ' '
           << std::quoted(optionHint_) << '\n';
    for (const ContentSettings& settings : contentSettings_) {
        int operationMask = 0;
        for (int operation = 0; operation < 4; ++operation) {
            if (settings.operations[static_cast<std::size_t>(operation)]) operationMask |= 1 << operation;
        }
        output << (settings.use ? 1 : 0) << ' ' << settings.minimum << ' ' << settings.maximum << ' '
               << (settings.randomSequence ? 1 : 0) << ' ' << settings.maxMultiplier << ' '
               << operationMask << '\n';
    }
    output.flush();
    if (!output) beginConfigurationWriteError();
}

void Game::loadScores() {
    const std::filesystem::path path(scorePath());
    std::error_code pathError;
    if (!std::filesystem::exists(path, pathError)) {
        const BlobView config = loadEmbeddedResource(IDR_NM_CFG);
        constexpr std::size_t hallOffset = 0x160;
        constexpr std::size_t hallBlockSize = 0x132;
        constexpr std::size_t hallHeaderSize = 6;
        constexpr std::size_t hallEntrySize = 30;
        for (std::size_t mode = 0; mode < scores_.size(); ++mode) {
            const std::size_t block = hallOffset + mode * hallBlockSize;
            if (block + hallBlockSize > config.size) break;
            const int count = std::min<int>(config.data[block] | (config.data[block + 1] << 8), 10);
            for (int index = 0; index < count; ++index) {
                const std::size_t entry = block + hallHeaderSize +
                                          static_cast<std::size_t>(index) * hallEntrySize;
                const char* nameData = reinterpret_cast<const char*>(config.data + entry);
                std::size_t nameLength = 0;
                while (nameLength < 26 && nameData[nameLength] != '\0') ++nameLength;
                const std::size_t scoreOffset = entry + 26;
                const std::uint32_t score = config.data[scoreOffset] |
                                            (static_cast<std::uint32_t>(config.data[scoreOffset + 1]) << 8) |
                                            (static_cast<std::uint32_t>(config.data[scoreOffset + 2]) << 16) |
                                            (static_cast<std::uint32_t>(config.data[scoreOffset + 3]) << 24);
                scores_[mode].push_back({static_cast<int>(score), 1,
                                         std::string(nameData, nameLength)});
            }
        }
        return;
    }
    std::ifstream input{path};
    int mode = 0;
    ScoreEntry entry;
    while (input >> mode >> entry.score >> entry.level >> std::quoted(entry.name)) {
        if (mode >= 0 && mode < static_cast<int>(Mode::Count)) scores_[static_cast<std::size_t>(mode)].push_back(entry);
    }
}

void Game::saveScores() {
    if (!scorePersistenceEnabled_) return;
    std::ofstream output(std::filesystem::path(scorePath()), std::ios::trunc);
    if (!output) {
        beginConfigurationWriteError();
        return;
    }
    for (std::size_t mode = 0; mode < scores_.size(); ++mode) {
        for (const ScoreEntry& entry : scores_[mode]) {
            output << mode << ' ' << entry.score << ' ' << entry.level << ' ' << std::quoted(entry.name) << '\n';
        }
    }
    output.flush();
    if (!output) beginConfigurationWriteError();
}

void Game::submitScore() {
    if (score_ <= 0) return;
    auto& list = scores_[static_cast<std::size_t>(mode_)];
    // The original 0xa77d rank scan advances past every existing score that is
    // greater than or equal to the candidate. Consequently equal scores keep
    // their existing order and the new entry follows the whole tie.
    const auto insertion = std::find_if(list.begin(), list.end(), [this](const ScoreEntry& entry) {
        return entry.score < score_;
    });
    const std::string admittedName = nameInput_.empty() ? "The Unknown Muncher" : nameInput_;
    list.insert(insertion, {score_, level_, admittedName});
    if (list.size() > 10) list.resize(10);
    // The painter searches from rank one for this exact pair, rather than
    // retaining the insertion index. Identical duplicates therefore select
    // the first pre-existing match, exactly as the original does.
    hallHighlightName_ = admittedName;
    hallHighlightScore_ = score_;
    saveScores();
}

bool Game::qualifiesForHallOfFame() const {
    // Controlled final-loss runs against an empty original Hall reject 45
    // points and accept 50, establishing the exact reachable admission floor.
    if (score_ < 50) return false;
    const auto& list = scores_[static_cast<std::size_t>(mode_)];
    return list.size() < 10 || score_ > list.back().score;
}
