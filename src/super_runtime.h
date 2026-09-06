#pragma once

#include "assets.h"
#include "audio_player.h"
#include "original_random.h"
#include "opl_stream_player.h"
#include "render.h"
#include "scene_script.h"
#include "super_config.h"
#include "super_content.h"
#include "super_game.h"
#include "super_scene_callbacks.h"

#include <windows.h>

#include <cstdint>
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

struct SuperGameTestAccess;

enum class SuperGamePage {
    StartupVersion,
    StartupSplash,
    Title,
    InstructionsQuestion,
    GameSelect,
    GameDifficultySelect,
    HallSelect,
    Hall,
    Information,
    Options,
    OptionsPasswordPrompt,
    OptionsSetPassword,
    OptionsEraseHallSelect,
    OptionsEraseEntries,
    OptionsEraseConfirm,
    OptionsCalibration,
    OptionsContent,
    OptionsQuickSet,
    OptionsGames,
    OptionsRules,
    OptionsRuleWords,
    OptionsDifficulty,
    OptionsPreview,
    Playing,
    Paused,
    QuitConfirm,
    Feedback,
    LevelComplete,
    MissionIntro,
    MissionQuestion,
    MissionResult,
    NameEntry,
    ReplayQuestion,
};

// Native presentation/input owner for Super Munchers. Board selection,
// scoring, lives, and the transformation lifecycle stay in SuperGameCore;
// this class owns the recovered actor cadence and visible DOS pages.
class SuperGame {
public:
    static constexpr int BoardRows = 5;
    static constexpr int BoardColumns = 6;
    static constexpr int BoardCellCount = BoardRows * BoardColumns;
    static constexpr int BoardLeft = 3;
    static constexpr int BoardTop = 26;
    static constexpr int BoardCellWidth = 52;
    static constexpr int BoardCellHeight = 30;
    static constexpr int BoardRight = BoardLeft + BoardColumns * BoardCellWidth;
    static constexpr int BoardBottom = BoardTop + BoardRows * BoardCellHeight;
    static constexpr int MunchAnimationTicks = 7;
    static constexpr int TransformationAnimationTicks = 12;
    static constexpr int TitleIdleTicks = 450;
    static constexpr double SchedulerTicksPerSecond =
        1'193'182.0 / (static_cast<double>(0x0555) * 0x1e);
    static constexpr double MunchAnimationDuration =
        static_cast<double>(MunchAnimationTicks) / SchedulerTicksPerSecond;

    explicit SuperGame(GraphicsMode graphicsMode, std::uint16_t randomSeed,
                       bool settingsPersistence = true);

    void update(double seconds);
    void render(Renderer& renderer);
    void renderPresentation(Renderer& renderer) { render(renderer); }
    void discardPresentationFrames() {}
    void keyDown(UINT virtualKey);
    void character(wchar_t character);
    void pointerMove(int x, int y);
    [[nodiscard]] bool pointerPress(bool secondary);
    void pointerButton(int x, int y, bool secondary);
    void setJoystickState(bool connected, std::uint32_t x, std::uint32_t y,
                          std::uint32_t buttons);
    void toggleCheatMenu();
    void toggleSound();
    void toggleMusic();
    void toggleSpeaker();
    [[nodiscard]] bool acceptsCommonDispatcherInput() const;

    [[nodiscard]] bool shouldQuit() const { return shouldQuit_; }
    [[nodiscard]] SuperGamePage page() const { return page_; }
    [[nodiscard]] int menuSelection() const { return menuSelection_; }
    [[nodiscard]] int playerRow() const { return playerRow_; }
    [[nodiscard]] int playerColumn() const { return playerColumn_; }
    [[nodiscard]] bool moving() const { return moving_; }
    [[nodiscard]] bool munching() const { return munching_; }
    [[nodiscard]] bool cheatOpen() const { return cheatOpen_; }
    [[nodiscard]] std::size_t enemyCount() const { return enemies_.size(); }
    [[nodiscard]] int visiblePlayerFrame() const;
    [[nodiscard]] int visibleTransformationFrame() const;
    [[nodiscard]] const SuperGameCore& core() const { return core_; }

private:
    struct Enemy {
        int row{};
        int column{};
        int fromRow{};
        int fromColumn{};
        int type{};
        int direction{2};
        int slot{-1};
        SuperBoardCell savedCell{};
        double timer{};
        double animationTimer{};
        double cannibalTimer{};
        bool savedCellValid{};
        bool savedCellEaten{};
        bool moving{};
        bool entering{};
        bool exiting{};
        bool biting{};
        bool cannibalizing{};
        bool overlapFrozen{};
        bool overlapRetired{};
    };

    enum class EnemySlotPhase { Disabled, Waiting, Warning, Active };
    struct EnemySlot {
        int type{};
        int row{};
        int column{};
        int edge{};
        double timer{};
        EnemySlotPhase phase{EnemySlotPhase::Disabled};
    };

    struct SafeZoneJob {
        bool active{};
        int cellIndex{-1};
        double period{};
        double timer{};
    };

    struct HallEntry {
        std::string name;
        std::uint32_t score{};

        bool operator==(const HallEntry&) const = default;
    };

    void activateTitleSelection();
    void activateGameSelection();
    void beginPlaySelection();
    void prepareGameChoices();
    void requestStartGame(int gameIndex, int level = 1);
    void startGame(int gameIndex, int level = 1);
    void startDemo();
    void updateSchedulerTick();
    void updateDemo();
    void returnFromDemo();
    void playAttractMusic();
    void initializePlayer();
    void beginMove(int row, int column, int direction);
    void processInputQueue();
    void beginMunch();
    void resolveMunch();
    void finishFeedback();
    void advanceBoard();
    void initializeHalls();
    void beginPostGame();
    void submitHallName();
    [[nodiscard]] bool startMission(int missionIndex);
    void updateMission(double seconds);
    void advanceMissionTick();
    void applyMissionPaintEvents();
    void finishMission();
    void prepareMissionQuestion();
    void completeMission(bool success);
    void updateMissionCatchState();
    void playMissionSound(std::uint16_t event);
    void initializeEnemies();
    void initializeSafeZones();
    [[nodiscard]] int chooseSafeZoneCell();
    [[nodiscard]] bool updateSafeZones(double seconds);
    [[nodiscard]] bool safeAt(int row, int column) const;
    [[nodiscard]] static bool enemyIntersectsCell(const Enemy& enemy,
                                                  int row, int column);
    void handleCompletedBoard();
    void updateEnemies(double seconds);
    void beginEnemyWarning(int slot);
    void spawnEnemy(int slot);
    void rearmEnemy(int slot);
    void beginEnemyMove(Enemy& enemy, int row, int column);
    void chooseEnemyMove(Enemy& enemy);
    [[nodiscard]] bool applyEnemyTrail(const Enemy& enemy);
    void collideWithEnemy(int slot);
    void finishEnemyCollision();
    [[nodiscard]] int enemyAtPlayer() const;
    [[nodiscard]] int chooseEnemyType();
    [[nodiscard]] double enemySpawnDelay();
    [[nodiscard]] double enemyDwellDelay();
    [[nodiscard]] double enemyMoveDuration(int direction) const;
    [[nodiscard]] int enemyMovePhase(const Enemy& enemy) const;
    [[nodiscard]] int enemyFrame(const Enemy& enemy) const;
    [[nodiscard]] static double movementDuration(int direction);
    [[nodiscard]] int movementPhase() const;
    [[nodiscard]] static int movementFrame(int direction, int phase);
    [[nodiscard]] static int munchFrame(int elapsedTicks);
    [[nodiscard]] int cellAtPoint(int x, int y) const;

    void renderStartup(Renderer& renderer);
    void renderStartupVersion(Renderer& renderer);
    void renderTitle(Renderer& renderer);
    void renderInstructionsQuestion(Renderer& renderer);
    void renderGameSelect(Renderer& renderer);
    void renderGameDifficultySelect(Renderer& renderer);
    void renderHallSelect(Renderer& renderer);
    void renderHall(Renderer& renderer);
    void renderInformation(Renderer& renderer);
    void renderOptions(Renderer& renderer);
    void renderOptionsPassword(Renderer& renderer, bool prompt);
    void renderOptionsEraseHallSelect(Renderer& renderer);
    void renderOptionsEraseEntries(Renderer& renderer);
    void renderOptionsEraseConfirm(Renderer& renderer);
    void renderOptionsCalibration(Renderer& renderer);
    void renderOptionsContent(Renderer& renderer);
    void renderOptionsQuickSet(Renderer& renderer);
    void renderOptionsGames(Renderer& renderer);
    void renderOptionsRules(Renderer& renderer);
    void renderOptionsRuleWords(Renderer& renderer);
    void renderOptionsDifficulty(Renderer& renderer);
    void renderOptionsPreview(Renderer& renderer);
    void renderBoard(Renderer& renderer);
    void renderPause(Renderer& renderer);
    void renderQuitConfirm(Renderer& renderer);
    void renderNameEntry(Renderer& renderer);
    void renderReplayQuestion(Renderer& renderer);
    void renderMissionIntro(Renderer& renderer);
    void renderMissionQuestion(Renderer& renderer);
    void renderMissionResult(Renderer& renderer);
    void renderCheat(Renderer& renderer);
    void drawSprite(Renderer& renderer, std::uint32_t sheetId, int frame,
                    int x, int y, bool transparentBlack = true);
    void drawCellText(Renderer& renderer, int index,
                      std::string_view text, std::uint32_t color);
    void playGameplaySound(std::uint8_t stream);
    void playToggleFeedback(bool enabled,
                            std::uint32_t startDelayMilliseconds = 0);
    [[nodiscard]] std::wstring settingsPath() const;
    void loadSettings();
    void saveSettings() const;

    GraphicsMode graphicsMode_{GraphicsMode::Vga256};
    GameAssets assets_;
    SuperContent content_;
    SuperConfig config_;
    SuperBoardSettings boardSettings_{};
    OriginalRandom random_;
    SuperGameCore core_;
    OplStreamPlayer oplPlayer_;
    WavePlayer effectPlayer_;
    WavePlayer missionSoundPlayer_;
    SceneScript missionScene_;
    std::unique_ptr<SuperSceneCallbacks> missionCallbacks_;
    std::vector<std::uint32_t> missionSurfacePixels_;

    SuperGamePage page_{SuperGamePage::StartupVersion};
    SuperGamePage quitReturnPage_{SuperGamePage::Title};
    int menuSelection_{};
    int selectedGame_{};
    int pendingGame_{};
    int pendingLevel_{1};
    int difficultyMode_{};
    int quickSet_{};
    int difficultyErrorTopic_{-1};
    int ruleTopic_{};
    int ruleOffset_{};
    int previewTopic_{};
    int previewOffset_{};
    int informationPage_{};
    int playerRow_{2};
    int playerColumn_{2};
    int moveFromRow_{};
    int moveFromColumn_{};
    int moveToRow_{};
    int moveToColumn_{};
    int moveDirection_{1};
    int playerTerminalFrame_{7};
    int munchCellIndex_{-1};
    std::deque<int> inputQueue_;
    std::vector<int> gameChoices_;
    std::array<std::vector<HallEntry>, SuperConfig::HallCount> halls_{};
    std::array<EnemySlot, 3> enemySlots_{};
    std::array<SafeZoneJob, 2> safeZoneJobs_{};
    std::array<bool, BoardCellCount> safeCells_{};
    std::vector<Enemy> enemies_;
    SuperMunchResolution lastResolution_{};
    std::string feedbackMessage_;
    std::string nameEntry_;
    std::string password_;
    std::string passwordHint_;
    std::string passwordEntry_;
    std::string passwordDraft_;
    std::vector<std::string> missionChoices_;
    std::vector<HallEntry> eraseDraft_;
    double startupVersionTickAccumulator_{};
    double schedulerTickAccumulator_{};
    double moveTimer_{};
    double munchTimer_{};
    double feedbackTimer_{};
    double levelCompleteTimer_{};
    double missionTickAccumulator_{};
    double transformationDrainTimer_{};
    double transformationAnimationTimer_{};
    double collisionTimer_{};
    double titleIdleTimer_{
        static_cast<double>(TitleIdleTicks) / SchedulerTicksPerSecond};
    int demoActionTicks_{};
    bool moving_{};
    bool munching_{};
    bool transforming_{};
    bool collisionActive_{};
    bool demoMode_{};
    bool cheatOpen_{};
    bool informationFromPlay_{};
    bool difficultyChosenForPlay_{};
    bool soundOn_{true};
    bool musicOn_{true};
    bool speakerEffects_{};
    bool answerVisionOption_{true};
    bool joystickEnabled_{};
    bool passwordError_{};
    bool passwordEditingHint_{};
    bool shouldQuit_{};
    bool settingsPersistenceEnabled_{true};
    std::wstring settingsPathOverride_;
    bool joystickConnected_{};
    std::uint32_t joystickButtons_{};
    std::uint32_t joystickX_{0x8000u};
    std::uint32_t joystickY_{0x8000u};
    std::uint32_t joystickLeftThreshold_{0x3000u};
    std::uint32_t joystickRightThreshold_{0xc000u};
    std::uint32_t joystickUpThreshold_{0x3000u};
    std::uint32_t joystickDownThreshold_{0xc000u};
    std::uint32_t calibrationMinX_{0xffffffffu};
    std::uint32_t calibrationMaxX_{};
    std::uint32_t calibrationMinY_{0xffffffffu};
    std::uint32_t calibrationMaxY_{};
    bool joystickCalibrated_{};
    bool calibrationActive_{};
    int enemySlotCount_{};
    int safeZoneJobCount_{};
    int collisionSlot_{-1};
    int feedbackEnemySlot_{-1};
    int eraseHallIndex_{-1};
    int missionIndex_{-1};
    int missionProgress_{};
    int levelsSinceMission_{};
    int missionCorrectSelection_{};
    int missionPlayerX_{136};
    int missionCaughtNumbers_{};
    int missionCatchFailureReason_{};
    int missionGraphicId_{};
    bool enemyWarning_{};
    bool playerRecovering_{};
    bool hallPostGame_{};
    bool missionSucceeded_{};
    bool missionCatchFailed_{};
    std::array<bool, 16> missionFallingInZone_{};

    friend struct SuperGameTestAccess;
};
