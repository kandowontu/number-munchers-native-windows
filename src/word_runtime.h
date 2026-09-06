#pragma once

#include "assets.h"
#include "audio_player.h"
#include "opl_stream_player.h"
#include "original_random.h"
#include "render.h"
#include "scene_script.h"
#include "word_config.h"
#include "word_content.h"
#include "word_game.h"

#include <windows.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct WordGameTestAccess;

enum class WordGamePage {
    StartupVersion,
    StartupSplash,
    Title,
    InstructionsQuestion,
    Information,
    Options,
    OptionsContent,
    OptionsDifficulty,
    OptionsVowels,
    OptionsVowelsHelp,
    OptionsPreview,
    OptionsEraseHall,
    OptionsPassword,
    OptionsPasswordPrompt,
    OptionsJoystickCalibration,
    Hall,
    Playing,
    Attract,
    AttractLogo,
    Paused,
    QuitConfirm,
    Feedback,
    LevelComplete,
    NameEntry,
    ReplayQuestion,
};

// Native presentation/input owner for Word Munchers. It deliberately consumes
// WordGameCore instead of duplicating the recovered board/scoring rules.
class WordGame {
public:
    static constexpr int BoardRows = 5;
    static constexpr int BoardColumns = 6;
    static constexpr int BoardCellCount = BoardRows * BoardColumns;
    static constexpr int BoardLeft = 20;
    static constexpr int BoardTop = 26;
    static constexpr int BoardCellWidth = 48;
    static constexpr int BoardCellHeight = 30;
    static constexpr int BoardRight = BoardLeft + BoardColumns * BoardCellWidth;
    static constexpr int BoardBottom = BoardTop + BoardRows * BoardCellHeight;
    static constexpr int MunchAnimationTicks = 7;
    static constexpr double MunchAnimationDuration =
        static_cast<double>(MunchAnimationTicks) /
        (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    // The shared player actor moves once per public scheduler tick: six
    // horizontal callbacks or five vertical callbacks per board cell.
    static constexpr double MoveAnimationDuration =
        6.0 / (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    static constexpr double TroggleWarningDuration =
        90.0 / (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    static constexpr int TroggleEatAnimationTicks = 21;

    explicit WordGame(GraphicsMode graphicsMode, std::uint16_t randomSeed);

    void update(double seconds);
    void render(Renderer& renderer);
    void renderPresentation(Renderer& renderer);
    void discardPresentationFrames();
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
    [[nodiscard]] WordGamePage page() const { return page_; }
    [[nodiscard]] int menuSelection() const { return menuSelection_; }
    [[nodiscard]] int playerRow() const { return playerRow_; }
    [[nodiscard]] int playerColumn() const { return playerColumn_; }
    [[nodiscard]] bool moving() const { return moving_; }
    [[nodiscard]] bool munching() const { return munching_; }
    [[nodiscard]] bool cheatOpen() const { return cheatOpen_; }
    [[nodiscard]] std::size_t enemyCount() const { return enemies_.size(); }
    [[nodiscard]] int enemySlotCount() const { return enemySlotCount_; }
    [[nodiscard]] int safeZoneJobCount() const { return safeZoneJobCount_; }
    [[nodiscard]] bool enemyWarning() const { return enemyWarning_; }
    [[nodiscard]] bool deathAnimating() const { return deathAnimating_; }
    [[nodiscard]] int deathSequenceTicks() const { return deathSequenceTicks_; }
    [[nodiscard]] bool playerRecovering() const { return playerRecovering_; }
    [[nodiscard]] bool sceneActive() const {
        return page_ == WordGamePage::LevelComplete && levelCompleteScene_.valid();
    }
    [[nodiscard]] int sceneGraphicId() const { return sceneGraphicId_; }
    [[nodiscard]] int sceneTicks() const { return sceneTicks_; }
    [[nodiscard]] std::size_t sceneAudioPlayCount() const {
        return attractOplPlayer_.effectPlayCount() + sceneEffectPlayer_.playCount();
    }
    [[nodiscard]] const WordGameCore& core() const { return core_; }

private:
    struct Enemy {
        int row{};
        int column{};
        int type{};
        double moveTimer{};
        int fromRow{};
        int fromColumn{};
        int direction{2};
        // Shared actor semantics: the stationary BTMP record is separate
        // from the heading retained for the next movement callback.
        int dwellFrame{-1};
        bool moving{};
        double animationTimer{};
        bool entering{};
        bool exiting{};
        bool collisionHidden{};
        bool overlapFrozen{};
        bool overlapRetired{};
        bool cannibalizing{};
        double cannibalTimer{};
        WordBoardCell savedCell;
        bool savedCellEaten{true};
        bool savedCellValid{};
        int slot{-1};
    };

    enum class EnemySlotPhase {
        Disabled,
        Waiting,
        Warning,
        Active,
    };

    struct EnemySlot {
        int type{};
        EnemySlotPhase phase{EnemySlotPhase::Disabled};
        double timer{};
        int edge{};
        int row{};
        int column{};
    };

    struct SafeZoneJob {
        bool active{};
        int cellIndex{-1};
        double period{};
        double timer{};
    };

    enum class AttractHallTransitionPhase {
        None,
        CollisionTransient,
        BoardHold,
        Wipe,
    };

    enum class AttractHallWipeVariant {
        Collision,
        Clean,
    };

    enum class AttractInterstitialTransition {
        None,
        HallToLogo,
        LogoToBoard,
        UserToBoard,
        UserToCartoon,
    };

    void startGame(int level = 1, bool preserveHallPalette = false);
    void startGameWithBoardPresentation(int level = 1,
                                        bool preserveHallPalette = false);
    void startGameSession(int level, bool demo);
    void startAttract();
    void stopAttract();
    void beginAttractHallTransition();
    void beginAttractHall();
    void updateAttractHallTransition(double seconds);
    void beginAttractInterstitialTransition(AttractInterstitialTransition transition);
    void updateAttractInterstitialTransition(double seconds);
    void startNextAttractBoard();
    void captureBoardPresentationSource();
    void beginBoardPresentation();
    void beginCartoonPresentation();
    [[nodiscard]] bool userPresentationActive() const;
    void updateAttractPlayer();
    void playAttractMusic();
    void initializeBoardRuntime();
    void initializePlayer();
    void beginMove(int row, int column, int direction);
    void updatePlayerMovementTick(double tickSeconds);
    [[nodiscard]] static double playerMoveAnimationDuration(int direction);
    [[nodiscard]] int playerMovementPhase() const;
    [[nodiscard]] static int playerMoveFrameAtPhase(int direction, int phase);
    [[nodiscard]] static int munchFrameAtTick(int elapsedTicks);
    [[nodiscard]] bool playerCanBeCaughtAtEnemyEndpoint() const;
    void enqueuePlayerInput(int inputByte);
    void processPointerCellQueue();
    void beginMunch();
    void resolveMunch();
    void handleCompletedBoard();
    void beginTroggleCollision(int enemyType, int enemySlot);
    void updateDeathAnimation(double seconds);
    void beginPostGame();
    void submitHallName();
    bool updateEnemies(double seconds, int slotUpperBound = -1,
                       int slotLowerBound = 0);
    bool updateSafeZones(double seconds);
    void initializeEnemySlots();
    void initializeSafeZones();
    [[nodiscard]] int chooseSafeZoneCell();
    [[nodiscard]] bool beginEnemyWarning(int slotIndex);
    void spawnPendingEnemy(int slotIndex);
    void rearmEnemySlot(int slotIndex);
    bool moveEnemy(Enemy& enemy);
    void beginEnemyMove(Enemy& enemy, int row, int column);
    bool applyEnemyCellEffect(const Enemy& enemy);
    [[nodiscard]] double enemySpawnDelay();
    [[nodiscard]] double enemyDwellDelay();
    [[nodiscard]] double enemyMoveAnimationDuration(int direction) const;
    [[nodiscard]] int enemyMovementPhase(const Enemy& enemy) const;
    [[nodiscard]] static int ordinaryEnemyFrame(const Enemy& enemy);
    [[nodiscard]] static int troggleBiteFrameAtTick(int elapsedTicks);
    [[nodiscard]] int chooseEnemyType();
    [[nodiscard]] static int difficultyIndexForPressure(int pressure);
    [[nodiscard]] static int maximumEnemiesForPressure(int pressure);
    [[nodiscard]] static int maximumSafeZoneJobsForPressure(int pressure);
    [[nodiscard]] static int initialSafeZonesForPressure(int pressure);
    [[nodiscard]] static int enemyWeightForPressure(int pressure, int enemyType);
    [[nodiscard]] static int steeredEnemyDirection(int enemyType, int currentDirection,
                                                   int playerDistancePixels,
                                                   int directionTowardPlayer,
                                                   int randomRoll);
    [[nodiscard]] bool enemyAt(int row, int column) const;
    [[nodiscard]] bool enemyOwnsAttractPlayerCell(int row, int column) const;
    [[nodiscard]] int enemyTypeAt(int row, int column) const;
    [[nodiscard]] int enemySlotAt(int row, int column) const;
    [[nodiscard]] static bool enemyIntersectsCell(const Enemy& enemy, int row, int column);
    [[nodiscard]] bool safeAt(int row, int column) const;
    [[nodiscard]] std::vector<std::uint32_t> capturePresentationFrame();
    [[nodiscard]] const Enemy* terminalResidentEnemyForPlayerCallback() const;
    [[nodiscard]] std::vector<std::uint32_t> capturePlayerCallbackFrame();
    static void copyBoardCellPixels(std::vector<std::uint32_t>& destination,
                                    const std::vector<std::uint32_t>& source,
                                    int row, int column);
    void enqueuePresentationFrame(const std::vector<std::uint32_t>& pixels,
                                  int repeatCount = 1);
    [[nodiscard]] bool loadLevelCompleteScene(int sceneIndex);
    void advanceLevelCompleteScene();
    void resetLevelCompleteSurface();
    void applyLevelCompletePaintEvents();
    void finishLevelCompleteScene();
    void playSceneEvent(std::uint16_t event);
    void playCartoonScore();
    void playGameplaySound(std::uint8_t stream);
    [[nodiscard]] bool adjustJoystickRepeat(wchar_t character);
    void playJoystickRepeatFeedback();
    void playToggleFeedback(bool enabled, std::uint32_t startDelayMilliseconds = 0);
    void finishFeedback(bool forcePostGame = false);
    void beginQuitConfirm(WordGamePage returnPage, bool fromFeedback = false);
    void beginExitDelay();
    void handleTitleKey(UINT virtualKey);
    void handlePlayingKey(UINT virtualKey);
    void handleCheatKey(UINT virtualKey);

    void renderStartupVersion(Renderer& renderer);
    void renderStartupSplash(Renderer& renderer);
    void renderTitle(Renderer& renderer);
    void renderAttractLogo(Renderer& renderer);
    void renderAttractHallWipe(Renderer& renderer);
    void renderAttractInterstitialTransition(Renderer& renderer);
    void renderAttractBoardPaint(Renderer& renderer, int frame);
    void renderInstructionsQuestion(Renderer& renderer);
    void renderInformation(Renderer& renderer);
    void renderOptions(Renderer& renderer);
    void renderOptionsContent(Renderer& renderer);
    void renderOptionsDifficulty(Renderer& renderer);
    void renderOptionsVowels(Renderer& renderer);
    void renderOptionsVowelsHelp(Renderer& renderer);
    void renderOptionsPreview(Renderer& renderer);
    void renderOptionsEraseHall(Renderer& renderer);
    void renderOptionsPassword(Renderer& renderer);
    void renderOptionsPasswordPrompt(Renderer& renderer);
    void renderOptionsJoystickCalibration(Renderer& renderer);
    void renderHall(Renderer& renderer);
    void renderBoard(Renderer& renderer);
    void renderQuitConfirm(Renderer& renderer);
    void renderFeedback(Renderer& renderer);
    void renderLevelComplete(Renderer& renderer);
    void renderNameEntry(Renderer& renderer);
    void renderReplayQuestion(Renderer& renderer);
    void renderCheatMenu(Renderer& renderer);
    void drawOptionsFrame(Renderer& renderer, std::string_view title,
                          int footerDividerTop = 162);
    void drawNumberedMenu(Renderer& renderer, const std::vector<std::string_view>& labels,
                          int firstBaseline, int left);
    [[nodiscard]] int selectedSoundCount(const std::array<bool, 20>& sounds) const;
    [[nodiscard]] int targetWordCount(const std::array<bool, 20>& sounds,
                                      int difficulty) const;
    [[nodiscard]] bool commitContentDraft();
    void toggleVowelChoice(int choice);
    [[nodiscard]] std::uint8_t vowelChoiceState(int choice) const;
    [[nodiscard]] int vowelValidationMessage() const;
    void beginWordPreview();
    void advanceWordPreview();
    void beginJoystickCalibration();
    void acceptJoystickCalibration();
    void updateJoystickInput(double seconds);
    [[nodiscard]] UINT joystickDirectionKey() const;
    void loadSettings();
    void saveSettings();
    [[nodiscard]] std::wstring settingsPath() const;
    void beginConfigurationWriteError();
    void dismissConfigurationWriteError();
    void renderConfigurationWriteError(Renderer& renderer);
    void drawSprite(Renderer& renderer, std::uint32_t sheetId, int frame, int x, int y,
                    bool remapTitlePalette = false,
                    bool remapHallPalette = false,
                    const Image* indexedPaletteSource = nullptr,
                    bool transparentBlack = true,
                    bool saturateSceneClip = false,
                    bool clipToBoardInterior = false,
                    int sourceWidthLimit = -1);

    GameAssets assets_;
    OriginalRandom random_;
    WordList words_;
    WordConfig config_;
    WordGameCore core_;
    WordHall hall_;

    WordGamePage page_{WordGamePage::StartupVersion};
    WordGamePage quitReturnPage_{WordGamePage::Playing};
    bool quitFromFeedback_{};
    int menuSelection_{};
    bool shouldQuit_{};
    bool exitPending_{};
    double exitDelayTimer_{};
    double startupVersionTickAccumulator_{};
    bool soundOn_{true};
    bool musicOn_{true};
    bool speakerEffects_{};
    bool informationStartsGame_{};
    int informationGroup_{};
    int informationPage_{};
    int wordDifficulty_{};
    std::array<bool, 20> selectedSounds_{};
    int contentDraftDifficulty_{};
    std::array<bool, 20> contentDraftSounds_{};
    bool contentDraftChanged_{};
    bool contentInsufficientMessage_{};
    bool configurationWriteError_{};
    bool configurationWriteErrorPostDismissContinuation_{};
    WordGamePage configurationWriteErrorBackgroundPage_{WordGamePage::Title};
    std::vector<std::uint32_t> configurationWriteErrorBackgroundPixels_;
    int difficultyEditorOriginal_{};
    std::array<bool, 20> vowelEditorOriginalSounds_{};
    int vowelSelection_{};
    int vowelMessage_{};
    int previewSound_{};
    int previewWordOffset_{};
    std::vector<WordHallScoreEntry> eraseDraft_;
    int eraseSelection_{};
    std::string optionPassword_;
    std::string optionHint_;
    std::string passwordDraft_;
    std::string hintDraft_;
    std::string passwordAttempt_;
    bool passwordEditingHint_{};
    bool passwordRejected_{};
    bool joystickEnabled_{};
    bool joystickCalibrated_{};

    int playerRow_{2};
    int playerColumn_{2};
    bool moving_{};
    double moveTimer_{};
    // Retained framebuffer pose painted by the terminal movement callback;
    // the following standing callback replaces it with frame 7.
    int playerTerminalFrame_{-1};
    int moveFromRow_{};
    int moveFromColumn_{};
    int moveToRow_{};
    int moveToColumn_{};
    int moveDirection_{};
    // Shared nine-byte pSeqKey/pSeqmouse FIFO: positive I/J/K/M/Space bytes
    // are keyboard actions and non-positive bytes are negated cell targets.
    std::deque<int> pointerCellQueue_;

    bool munching_{};
    double munchTimer_{};
    int munchCellIndex_{-1};
    WordMunchResolution lastResolution_;
    double feedbackTimer_{};
    double gameplayTickAccumulator_{};
    std::uint64_t boardGenerationSerial_{};
    std::deque<std::vector<std::uint32_t>> presentationFrames_;
    std::vector<std::uint32_t> deathResidentSurfacePixels_;
    std::vector<std::uint32_t> attractPlayerPhaseZeroHoldPixels_;
    // A selector-6 safe removal immediately before selector 5 starts a chew
    // is still offscreen on the first state-5 player callback page. Preserve
    // the old outline only for chew tick 0; the next callback exposes removal.
    std::vector<std::uint32_t> attractMunchSafeZoneHoldPixels_;
    // With an idle player and active Troggle painters, selector-6 changes stay
    // on the nonresident page for three public ticks. Keep the before/after
    // surfaces as a pixel delta so current actor poses can continue advancing.
    std::vector<std::uint32_t> attractSafeZoneEnemyHoldOldPixels_;
    std::vector<std::uint32_t> attractSafeZoneEnemyHoldNewPixels_;
    int attractSafeZoneEnemyHoldTicks_{};
    // When state 4 clears a directional terminal before later selector-1
    // callbacks, DOS can present the current Troggles over the old player
    // delta for one public tick.
    std::vector<std::uint32_t> attractPlayerTerminalEnemyHoldPixels_;
    int attractPlayerTerminalEnemyHoldTicks_{};
    // A final player-movement callback can repair the source-cell foreground
    // over a resident final-phase Troggle. That callback surface remains the
    // displayed page until the next public scheduler dispatch.
    std::vector<std::uint32_t> attractPlayerCallbackHoldPixels_;
    int attractPlayerCallbackHoldTicks_{};
    std::vector<std::uint32_t> cannibalPlayerResidentSurfacePixels_;
    std::vector<std::uint32_t> cannibalPlayerPresentationHoldPixels_;
    bool cannibalPlayerRetainedPenultimatePaint_{};
    double titleIdleTimer_{};
    double attractActionTimer_{};
    double attractTransitionTimer_{};
    bool attractMode_{};
    // The shared DOS page painter leaves the Hall value installed in the
    // darkest occupied Muncher palette slot through primitive replay prompts,
    // replayed user boards, the logo, and later Demo boards.
    bool hallPaletteActive_{};
    bool attractPostFeedbackBoard_{};
    AttractHallTransitionPhase attractHallTransitionPhase_{
        AttractHallTransitionPhase::None};
    AttractHallWipeVariant attractHallWipeVariant_{
        AttractHallWipeVariant::Clean};
    int attractHallWipeFrame_{};
    AttractInterstitialTransition attractInterstitialTransition_{
        AttractInterstitialTransition::None};
    int attractInterstitialFrame_{};
    std::vector<std::uint32_t> boardPresentationSourcePixels_;

    std::vector<Enemy> enemies_;
    std::array<EnemySlot, 3> enemySlots_{};
    int enemySlotCount_{};
    std::array<SafeZoneJob, 2> safeZoneJobs_{};
    int safeZoneJobCount_{};
    std::array<bool, BoardCellCount> safeCells_{};
    bool enemyWarning_{};
    bool deathAnimating_{};
    int deathSequenceTicks_{};
    bool playerRecovering_{};
    int feedbackEnemyType_{-1};
    int feedbackEnemySlot_{-1};
    std::string feedbackMessage_;
    bool postGameHall_{};
    bool pendingHallAdmission_{};
    std::string hallHighlightName_;
    std::uint32_t hallHighlightScore_{};
    int nameEntryMessageType_{1};
    std::string nameInput_;
    double nameCursorBlinkTimer_{};
    bool nameCursorVisible_{true};

    SceneScript levelCompleteScene_;
    int sceneGraphicId_{};
    int sceneAudioBank_{};
    int pendingCartoonScene_{-1};
    int sceneTicks_{};
    double sceneTickAccumulator_{};
    std::vector<std::uint32_t> sceneSurfacePixels_;

    bool cheatOpen_{};
    int cheatSelection_{};
    int cheatLevel_{1};

    bool joystickConnected_{};
    std::uint32_t joystickX_{};
    std::uint32_t joystickY_{};
    std::uint32_t joystickButtons_{};
    std::uint32_t joystickPreviousButtons_{};
    std::uint32_t joystickLeftThreshold_{16'384};
    std::uint32_t joystickRightThreshold_{49'152};
    std::uint32_t joystickUpThreshold_{16'384};
    std::uint32_t joystickDownThreshold_{49'152};
    std::array<std::uint32_t, 4> joystickSampleX_{};
    std::array<std::uint32_t, 4> joystickSampleY_{};
    std::size_t joystickSampleCount_{};
    std::size_t joystickSampleIndex_{};
    int joystickCalibrationStep_{-1};
    int joystickRepeatTicks_{4};
    int lastJoystickRepeatFeedbackHz_{};
    std::uint32_t joystickCalibrationCenterX_{};
    std::uint32_t joystickCalibrationCenterY_{};
    std::uint32_t joystickDraftLeftThreshold_{};
    std::uint32_t joystickDraftRightThreshold_{};
    std::uint32_t joystickDraftUpThreshold_{};
    std::uint32_t joystickDraftDownThreshold_{};
    double joystickClockFraction_{};
    std::uint64_t joystickClockTicks_{};
    std::uint64_t joystickCachedClockTicks_{};
    std::uint64_t joystickNextRepeatTicks_{};
    UINT joystickPendingDirection_{};
    UINT joystickLastSampledDirection_{};
    bool settingsPersistenceEnabled_{true};
    std::wstring settingsPathOverride_;
    int lastGameplaySound_{-1};
    int previousGameplaySound_{-1};
    // Original AdLib gameplay/scene cues keep one YM3812 alive across events;
    // during Demo that same owner adds score channels 1-8 around channel 0.
    OplStreamPlayer attractOplPlayer_;
    WavePlayer sceneEffectPlayer_;

    friend struct WordGameTestAccess;
};
