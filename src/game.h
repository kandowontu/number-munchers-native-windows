#pragma once

#include "audio_player.h"
#include "assets.h"
#include "muncher_score.h"
#include "opl_stream_player.h"
#include "original_random.h"
#include "render.h"
#include "scene_script.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

struct GameTestAccess;

class Game {
public:
    explicit Game(GraphicsMode graphicsMode = GraphicsMode::Vga256);

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

private:
    enum class Page {
        StartupVersion,
        StartupSplash,
        Title,
        InstructionsQuestion,
        ModeSelect,
        Information,
        Options,
        OptionsDifficulty,
        OptionsContent,
        OptionsContentRange,
        OptionsContentOtherNumber,
        OptionsContentOperations,
        OptionsContentHelp,
        OptionsContentValidation,
        OptionsEraseHall,
        OptionsEraseEntries,
        OptionsEraseAllConfirm,
        OptionsPassword,
        OptionsPasswordPrompt,
        OptionsJoystickCalibration,
        HallSelect,
        Hall,
        Playing,
        Paused,
        QuitConfirm,
        Feedback,
        LevelComplete,
        NameEntry,
        ReplayQuestion,
        Attract,
    };

    enum class Mode {
        Multiples,
        Factors,
        Primes,
        Equality,
        Inequality,
        Challenge,
        Count,
    };

    enum class FeedbackKind {
        None,
        WrongAnswer,
        EatenByTroggle,
    };

    enum class ContentValidationKind {
        None,
        NoGames,
        RangeOrder,
        ValueRange,
        NoOperations,
    };

    enum class HallContext {
        Browse,
        PostGame,
        Attract,
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
        HallToSplash,
        SplashToBoard,
        UserToBoard,
        UserToCartoon,
    };

    struct Cell {
        std::string label;
        bool correct{};
        bool eaten{};
        bool safe{};
        double safeTimer{};
    };

    struct Enemy {
        int row{};
        int column{};
        int type{};
        double moveTimer{};
        int fromRow{};
        int fromColumn{};
        int direction{2};
        // The DOS actor record stores its painted BTMP index independently
        // from the direction used by the next steering callback. Bite
        // terminals force record 15 without changing that logical heading.
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
        Cell savedCell{};
        bool savedCellValid{};
        int slot{-1};
    };

    struct PresentationPlayerState {
        bool moving{};
        double moveTimer{};
        int row{};
        int column{};
        int terminalFrame{-1};
        int direction{};
        int fromRow{};
        int fromColumn{};
        int toRow{};
        int toColumn{};
        int movementPhase{-1};
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

    struct ScoreEntry {
        int score{};
        int level{};
        std::string name;
    };

    struct ContentSettings {
        bool use{true};
        int minimum{1};
        int maximum{50};
        bool randomSequence{true};
        int maxMultiplier{50};
        std::array<bool, 4> operations{true, true, true, true};

        bool operator==(const ContentSettings&) const = default;
    };

    static constexpr int BoardRows = 5;
    static constexpr int BoardColumns = 6;
    static constexpr int BoardCellCount = BoardRows * BoardColumns;
    static constexpr int BoardLeft = 20;
    static constexpr int BoardTop = 26;
    static constexpr int BoardCellWidth = 48;
    static constexpr int BoardCellHeight = 30;
    static constexpr int BoardRight = BoardLeft + BoardColumns * BoardCellWidth;
    static constexpr int BoardBottom = BoardTop + BoardRows * BoardCellHeight;
    static constexpr int MaximumScore = MuncherScore::MaximumScore;
    // 0x08A31 installs the chew job at one public scheduler tick and 0x093A9
    // resolves it when the transition counter reaches seven.
    static constexpr int MunchAnimationTicks = 7;
    static constexpr double MunchAnimationDuration =
        static_cast<double>(MunchAnimationTicks) /
        (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    // DS:0674 installs the player movement job at one public scheduler tick.
    // 0x0871A reaches a cell in six horizontal or five vertical callbacks.
    static constexpr double MoveAnimationDuration =
        6.0 / (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    static constexpr double TroggleWarningDuration =
        90.0 / (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));
    // The collision job starts with a one-tick deadline, then reschedules at
    // 2, 3, 4, 5, and 6 ticks before its terminal callback: 21 ticks total.
    static constexpr int TroggleEatAnimationTicks = 21;
    static constexpr double TroggleEatAnimationDuration =
        static_cast<double>(TroggleEatAnimationTicks) /
        (1'193'182.0 / (static_cast<double>(0x0555) * 0x1e));

    void enterPage(Page page);
    void enterModeSelect();
    void enterHall(HallContext context);
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
    void startGameWithBoardPresentation(Mode mode, int startingLevel = 1,
                                        bool preserveHallPalette = false);
    void updateAttractPlayer(double seconds);
    void beginExitDelay();
    void handleMenuKey(UINT virtualKey);
    void handlePlayKey(UINT virtualKey);
    void finishFeedback(bool forcePostGame = false);
    void beginQuitConfirm(Page returnPage, bool fromFeedback = false);
    void handleCheatKey(UINT virtualKey);
    void startGame(Mode mode, int startingLevel = 1,
                   bool preserveHallPalette = false);
    void beginContentEdit();
    void beginJoystickCalibration();
    void acceptJoystickCalibration();
    void updateJoystickInput(double seconds);
    [[nodiscard]] UINT joystickDirectionKey() const;
    void applyDifficultyPreset(int difficulty);
    void resetContentProgression();
    void openContentRange();
    void openContentOther();
    void acceptContentNumber();
    void showContentValidation(ContentValidationKind kind);
    void dismissContentValidation();
    [[nodiscard]] bool contentFieldValid(int row, int column) const;
    [[nodiscard]] int contentFieldCount(int row) const;
    [[nodiscard]] std::vector<Mode> enabledPlayModes() const;
    void moveContentSelection(int rowDelta, int columnDelta);
    void generateBoard();
    void prepareMode();
    [[nodiscard]] Cell generatedCell(bool correct);
    void beginMove(int row, int column, int direction);
    void updatePlayerMovementTick(double tickSeconds);
    [[nodiscard]] static double playerMoveAnimationDuration(int direction);
    [[nodiscard]] int playerMovementPhase() const;
    [[nodiscard]] static int playerMoveFrameAtPhase(int direction, int phase);
    [[nodiscard]] static int munchFrameAtTick(int elapsedTicks);
    [[nodiscard]] bool playerCanBeCaughtAtEnemyEndpoint() const;
    void enqueuePlayerInput(int inputByte);
    void processPointerCellQueue();
    void munch();
    void resolveMunch();
    void loseLife(int enemyType = -1, int enemySlot = -1);
    void completeLevel();
    void finishLevelComplete();
    void prepareCartoonOrderForCompletedLevel(int completedLevel);
    void advanceCartoonOrder();
    bool loadLevelCompleteScene(int sceneIndex);
    void advanceLevelCompleteScene();
    void resetLevelCompleteSurface();
    void applyLevelCompletePaintEvents();
    bool updateEnemies(double seconds, int slotUpperBound = -1,
                       int slotLowerBound = 0);
    bool updateSafeZones(double seconds);
    void initializeSafeZones();
    [[nodiscard]] int chooseSafeZoneCell();
    void initializeEnemySlots();
    void promoteEnemyPaintSlot(int slot);
    bool beginEnemyWarning(int slot);
    void spawnPendingEnemy(int slot);
    void rearmEnemySlot(int slot);
    void spawnEnemy();
    bool moveEnemy(Enemy& enemy);
    void beginEnemyMove(Enemy& enemy, int row, int column);
    [[nodiscard]] double enemySpawnDelay();
    [[nodiscard]] double enemyDwellDelay();
    [[nodiscard]] double enemyMoveAnimationDuration(int direction) const;
    [[nodiscard]] int enemyMovementPhase(const Enemy& enemy) const;
    [[nodiscard]] static int ordinaryEnemyFrame(const Enemy& enemy);
    [[nodiscard]] static int troggleBiteFrameAtTick(int elapsedTicks);
    [[nodiscard]] static int difficultyIndexForLevel(int level);
    [[nodiscard]] static int maximumEnemiesForLevel(int level);
    [[nodiscard]] static int maximumSafeZoneJobsForLevel(int level);
    [[nodiscard]] static int initialSafeZonesForLevel(int level);
    [[nodiscard]] static int enemyWeightForLevel(int level, int enemyType);
    [[nodiscard]] int chooseEnemyType();
    [[nodiscard]] static int steeredEnemyDirection(int enemyType, int currentDirection,
                                                   int playerDistancePixels,
                                                   int directionTowardPlayer, int randomRoll);
    bool applyEnemyCellEffect(const Enemy& enemy);
    [[nodiscard]] bool addScore(int points);
    void finishScoredGame();
    [[nodiscard]] static int scoreValueForLevel(int level);
    [[nodiscard]] bool enemyAt(int row, int column) const;
    [[nodiscard]] bool enemyOwnsAttractPlayerCell(int row, int column) const;
    [[nodiscard]] int enemyTypeAt(int row, int column) const;
    [[nodiscard]] int enemySlotAt(int row, int column) const;
    [[nodiscard]] static bool enemyIntersectsCell(const Enemy& enemy, int row, int column);
    [[nodiscard]] std::string wrongAnswerMessage(const Cell& current) const;
    [[nodiscard]] Cell& cell(int row, int column) { return cells_[row * BoardColumns + column]; }
    [[nodiscard]] const Cell& cell(int row, int column) const { return cells_[row * BoardColumns + column]; }
    [[nodiscard]] int randomInt(int minimum, int maximum);
    [[nodiscard]] std::vector<std::uint32_t> capturePresentationFrame();
    [[nodiscard]] std::vector<std::uint32_t> capturePlayerCallbackFrame();
    [[nodiscard]] std::vector<std::uint32_t>
        captureDepartingEnemyPlayerLookaheadFrame(bool playerCallbackPaint = false);
    static void copyBoardCellPixels(std::vector<std::uint32_t>& destination,
                                    const std::vector<std::uint32_t>& source,
                                    int row, int column);
    static void copyPixelRectangle(std::vector<std::uint32_t>& destination,
                                   const std::vector<std::uint32_t>& source,
                                   int left, int top, int right, int bottom);
    void enqueuePresentationFrame(const std::vector<std::uint32_t>& pixels,
                                  int repeatCount = 1,
                                  int auditOrigin = 0);
    void beginEnemyPresentationLag(int slot, int lagTicks,
                                   int firstRecordPresentations = 2,
                                   bool actorMayDisappear = false);
    void advanceEnemyPresentationLag();

    void renderTitle(Renderer& renderer);
    void renderStartupVersion(Renderer& renderer);
    void renderStartupSplash(Renderer& renderer);
    void renderInstructionsQuestion(Renderer& renderer);
    void renderModeSelect(Renderer& renderer, bool hallMode);
    void renderAttractHallWipe(Renderer& renderer);
    void renderAttractInterstitialTransition(Renderer& renderer);
    void renderAttractBoardPaint(Renderer& renderer, int frame);
    void renderInformation(Renderer& renderer);
    void renderOptions(Renderer& renderer);
    void renderOptionsDifficulty(Renderer& renderer);
    void renderOptionsContent(Renderer& renderer);
    void renderOptionsContentRange(Renderer& renderer);
    void renderOptionsContentOtherNumber(Renderer& renderer);
    void renderOptionsContentOperations(Renderer& renderer);
    void renderOptionsContentHelp(Renderer& renderer);
    void renderOptionsContentValidation(Renderer& renderer);
    void renderOptionsEraseHall(Renderer& renderer);
    void renderOptionsEraseEntries(Renderer& renderer);
    void renderOptionsEraseAllConfirm(Renderer& renderer);
    void renderOptionsPassword(Renderer& renderer);
    void renderOptionsPasswordPrompt(Renderer& renderer);
    void renderOptionsJoystickCalibration(Renderer& renderer);
    void drawOptionsFrame(Renderer& renderer, std::string_view title, int bottomLineY);
    void renderHall(Renderer& renderer);
    void renderBoard(Renderer& renderer);
    void renderQuitConfirm(Renderer& renderer);
    void renderFeedback(Renderer& renderer);
    void renderNameEntry(Renderer& renderer);
    void renderReplayQuestion(Renderer& renderer);
    void renderLevelComplete(Renderer& renderer);
    void renderCheatMenu(Renderer& renderer);
    void drawMenuItem(Renderer& renderer, int y, std::string_view text, bool selected, int x = 74);
    void drawSprite(Renderer& renderer, std::uint32_t sheetId, int frame, int x, int y,
                    bool remapTitlePalette = false, bool remapHallPalette = false,
                    bool transparentBlack = true, bool saturateSceneClip = false,
                    bool remapNumberScenePalette = false,
                    bool provideNumberScenePaletteContext = false,
                    int transparentPaletteIndex = -1,
                    bool initialNumberScenePaint = false,
                    bool clipToBoardInterior = false,
                    int sourceWidthLimit = -1);

    void playTone(int frequency, int milliseconds);
    void playNotes(std::initializer_list<std::pair<int, int>> notes, bool music = false);
    [[nodiscard]] bool adjustJoystickRepeat(wchar_t character);
    void playJoystickRepeatFeedback();
    void playToggleFeedback(bool enabled, std::uint32_t startDelayMilliseconds = 0);
    void playGameplaySound(std::uint8_t soundIndex);
    [[nodiscard]] bool playOriginalSound(std::uint8_t soundIndex, bool music = false);
    void playOriginalAdli(std::uint8_t bankId, std::uint8_t soundIndex);
    void playCartoonScore();
    void loadSettings();
    void saveSettings();
    void loadScores();
    void saveScores();
    void beginConfigurationWriteError();
    void dismissConfigurationWriteError();
    void renderConfigurationWriteError(Renderer& renderer);
    void submitScore();
    [[nodiscard]] bool qualifiesForHallOfFame() const;
    [[nodiscard]] std::wstring scorePath() const;
    [[nodiscard]] std::wstring settingsPath() const;

    GameAssets assets_;
    OriginalRandom random_;
    Page page_{Page::StartupVersion};
    Page quitReturnPage_{Page::Playing};
    bool quitFromFeedback_{};
    Page informationReturnPage_{Page::Title};
    bool shouldQuit_{};
    bool exitPending_{};
    double exitDelayTimer_{};
    double startupVersionTickAccumulator_{};
    int menuSelection_{};
    int informationPage_{};
    int difficulty_{10};
    bool soundOn_{true};
    bool musicOn_{true};
    bool speakerEffects_{};
    bool joystickOn_{};
    bool joystickConnected_{};
    bool joystickCalibrated_{};
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
    // The DOS poller keeps a public-clock snapshot/deadline and a pending
    // sampled key; it does not dispatch immediately when an axis changes.
    double joystickClockFraction_{};
    std::uint64_t joystickClockTicks_{};
    std::uint64_t joystickCachedClockTicks_{};
    std::uint64_t joystickNextRepeatTicks_{};
    UINT joystickPendingDirection_{};
    UINT joystickLastSampledDirection_{};
    bool passwordEditingHint_{};
    std::string optionPassword_;
    std::string optionHint_;
    std::string passwordDraft_;
    std::string hintDraft_;
    std::string passwordAttempt_;
    bool passwordRejected_{};
    bool configurationWriteError_{};
    bool configurationWriteErrorPostDismissContinuation_{};
    Page configurationWriteErrorBackgroundPage_{Page::Title};
    std::vector<std::uint32_t> configurationWriteErrorBackgroundPixels_;
    std::array<ContentSettings, static_cast<std::size_t>(Mode::Count)> contentSettings_{};
    std::array<ContentSettings, static_cast<std::size_t>(Mode::Count)> contentDraft_{};
    std::array<int, static_cast<std::size_t>(Mode::Count)> contentSequenceNext_{};
    std::array<int, static_cast<std::size_t>(Mode::Count)> contentPreviousRandom_{};
    int primeMaximumIndex_{-1};
    int primeMaximumIndexLimit_{};
    ContentSettings contentDialogBackup_{};
    int contentRow_{};
    int contentColumn_{};
    int contentInputStage_{};
    int contentRangeCandidateMinimum_{};
    bool contentNumberConfirm_{};
    int contentOperationSelection_{};
    int contentHelpPage_{};
    ContentValidationKind contentValidationKind_{ContentValidationKind::None};
    Page contentValidationReturnPage_{Page::OptionsContent};
    std::string contentNumericInput_;
    int eraseMode_{};
    int eraseSelection_{};
    std::vector<ScoreEntry> eraseDraft_;

    Mode mode_{Mode::Multiples};
    Mode activeBoardMode_{Mode::Multiples};
    std::array<Cell, BoardCellCount> cells_{};
    std::vector<Enemy> enemies_;
    int playerRow_{2};
    int playerColumn_{2};
    int target_{};
    int relation_{};
    int level_{1};
    int score_{};
    // Total Munchers remaining, including the one active on the board. The
    // footer draws only the reserves, so a fresh four-Muncher game shows 3.
    int lives_{4};
    int correctRemaining_{};
    std::array<EnemySlot, 3> enemySlots_{};
    int enemySlotCount_{};
    // DS:5A84 is initialized to the one-based Troggle job IDs and the
    // original moves a job to the end whenever that actor begins an entry,
    // ordinary move, or collision. The board redraw traverses this list.
    std::array<int, 3> enemyPaintOrder_{{0, 1, 2}};
    std::array<SafeZoneJob, 2> safeZoneJobs_{};
    int safeZoneJobCount_{};
    double transitionTimer_{};
    double titleIdleTimer_{};
    double attractActionTimer_{};
    int attractBoardIndex_{};
    int attractDifficultyIndex_{};
    bool attractMode_{};
    // Full-page Hall PCX loads replace the darkest occupied Muncher palette
    // slot. Primitive replay prompts and board painters leave it installed.
    bool hallPaletteActive_{};
    AttractHallTransitionPhase attractHallTransitionPhase_{
        AttractHallTransitionPhase::None};
    AttractHallWipeVariant attractHallWipeVariant_{
        AttractHallWipeVariant::Clean};
    int attractHallWipeFrame_{};
    AttractInterstitialTransition attractInterstitialTransition_{
        AttractInterstitialTransition::None};
    int attractInterstitialFrame_{};
    std::vector<std::uint32_t> boardPresentationSourcePixels_;
    // Argument 7 is used for both title-menu browsing and user post-game
    // Halls; only the caller after its key wait differs. Demo state 3 uses the
    // nonblocking any-key Hall.
    HallContext hallContext_{HallContext::Browse};
    // The DOS Hall painter searches the active list for the retained
    // post-admission name/score pair and paints the first match opaque.
    std::string hallHighlightName_;
    int hallHighlightScore_{};
    bool enemyWarning_{};
    bool playerRecovering_{};
    int cutsceneImage_{2013};
    int cutsceneSoundBank_{11};
    int pendingCartoonScene_{-1};
    // DS:5f7a is shuffled with five full-range swaps whenever the original
    // cartoon cursor at DS:118e is zero. The cursor advances only after a
    // cartoon closes, so the completions of levels 1, 2, and 3 each shuffle.
    std::array<int, 5> cartoonOrder_{0, 1, 2, 3, 4};
    int cartoonOrderIndex_{};
    SceneScript levelCompleteScene_;
    int levelCompleteSceneTicks_{};
    double sceneTickAccumulator_{};
    std::vector<std::uint32_t> sceneSurfacePixels_;
    double gameplayTickAccumulator_{};
    std::uint64_t boardGenerationSerial_{};
    // Complete callback-intermediate framebuffers awaiting the Win32 host.
    // Logical scheduler state continues independently of this bounded queue.
    std::deque<std::vector<std::uint32_t>> presentationFrames_;
    // Mirrored diagnostic ownership for parity traces. It never affects
    // rendering and remains zero for ordinary untagged callers.
    std::deque<int> presentationFrameAuditOrigins_;
    int lastPresentationAuditOrigin_{};
    // Complete logical surfaces immediately after each due Troggle slot. The
    // outer presenter uses these only when multiple actor records paint on the
    // same public tick; ordinary gameplay rendering remains state-based.
    bool captureEnemyCallbackPresentationFrames_{};
    std::vector<int> enemyCallbackPresentationSlots_;
    std::vector<std::vector<std::uint32_t>>
        enemyCallbackPresentationFrames_;
#if defined(NUMBER_MUNCHERS_TESTING)
    // Last public-tick actor surfaces used by the headless autonomous audit to
    // distinguish player-, player-dirty-, and post-Troggle callback pages.
    std::vector<std::uint32_t> auditBeforePlayerPixels_;
    std::vector<std::uint32_t> auditAfterPlayerPixels_;
    std::vector<std::uint32_t> auditAfterPlayerCallbackPixels_;
    std::vector<std::uint32_t> auditAfterTrogglesPixels_;
#endif
    // The DOS dirty-cell presenter can leave a newly entering Troggle's actor
    // paint behind its logical scheduler record while an earlier actor is
    // about to start moving. Retain the actor records independently so each
    // later complete page combines the current board with the older entrant.
    // A slot -1 history record marks an actor that has logically completed an
    // exit while its older painted records are still reaching the presenter.
    int enemyPresentationLagSlot_{-1};
    int enemyPresentationLagTicks_{};
    int enemyPresentationLagFirstPresentationsRemaining_{};
    bool enemyPresentationLagPresentFirstLogicalPage_{};
    bool enemyPresentationLagActorMayDisappear_{};
    bool enemyPresentationLagFullSurface_{};
    std::deque<Enemy> enemyPresentationLagHistory_;
    std::deque<PresentationPlayerState> enemyPresentationLagPlayerHistory_;
    std::deque<PresentationPlayerState> enemyPresentationLagPendingPlayers_;
    PresentationPlayerState enemyPresentationLagDisplayedPlayer_{};
    PresentationPlayerState enemyPresentationLagLastHistoricalPlayer_{};
    int enemyPresentationLagLastEnemyPhase_{};
    std::vector<std::uint32_t> enemyPresentationLagPixels_;
    // The captured Prime-board bottom exit releases more historical player
    // pages than one host frame can consume. Preserve the following live
    // player callbacks behind that backlog until the walk reaches standing.
    bool attractPrimeExitPlayerCatchup_{};
    // Most recently delivered host page. Dirty-cell holds begin from the
    // resident page, not from a freshly recomposed logical framebuffer.
    std::vector<std::uint32_t> lastPresentationPixels_;
    // State-5 callbacks leave this complete surface resident between public
    // scheduler dispatches instead of recomposing the newer logical state.
    std::vector<std::uint32_t> deathResidentSurfacePixels_;
    std::vector<std::uint32_t> attractPlayerPhaseZeroHoldPixels_;
    std::vector<std::uint32_t> attractSafeZoneWarningHoldPixels_;
    // Fourth-board safe selector retained behind Reggie's phase-3/4 paints.
    std::vector<std::uint32_t> attractFourthPrimeSafeHoldPixels_;
    // When the earlier player record and a newly entering Troggle both paint
    // on successive callbacks, DOS presents the player's dirty-cell page
    // while retaining the later Troggle paint only on the working surface.
    std::vector<std::uint32_t> attractPlayerEntryResidentSurfacePixels_;
    std::vector<std::uint32_t> attractPlayerEntryPresentationHoldPixels_;
    // When the Muncher enters the source cell of a departing in-board
    // Troggle, the later Troggle callback advances over the immediately prior
    // player paint. Retain that callback-local page between public ticks.
    std::vector<std::uint32_t> attractPlayerDepartingEnemyHoldPixels_;
    // The Factors Demo contains two intervals where player callbacks become
    // resident only after simultaneous Helper callbacks. The stage records
    // which delayed callback group is being drained; the held page prevents a
    // newer clean logical render from skipping that resident state.
    int attractFactorsConcurrentPhase_{};
    std::vector<std::uint32_t> attractFactorsConcurrentHoldPixels_;
    // Chew callbacks use one dirty player cell rather than the two-cell
    // movement path. Track that overlap so an entry terminal can retain the
    // preterminal chew record without applying stale movement coordinates.
    bool attractPlayerEntryMunchOverlap_{};
    bool attractMunchEntryTerminalHold_{};
    int attractMunchResidentEntrySlot_{-1};
    // A player walk can begin while an entry is on its final actor records.
    // The player callback paints against the current pre-Troggle surface;
    // keep the physical slot until that walk's terminal callback drains.
    int attractPlayerMovingEntrySlot_{-1};
    std::vector<std::uint32_t> attractPlayerMovingEntryHoldPixels_;
    bool attractPlayerMovingEntryTerminalHold_{};
    // Source frames 1260-1274 contain a left-moving Demo Muncher whose job
    // precedes a concurrently left-moving Smarty. Preserve the player-first
    // callback pages independently of the later clean logical actor surface.
    int attractPlayerConcurrentEnemySlot_{-1};
    std::vector<std::uint32_t> attractPlayerConcurrentEnemyHoldPixels_;
    bool attractPlayerConcurrentEnemyTerminalHold_{};
    // The initial Factors Demo starts its wrong-answer chew from the queued
    // player callback while a Reggie movement job owns another dirty cell.
    // DOS keeps the last delivered Reggie page resident and applies only the
    // seven Muncher-cell paints through feedback and the clean Hall wipe.
    std::vector<std::uint32_t> attractMunchConcurrentEnemyPixels_;
    // On the captured fourth board, Reggie's downward move reaches its terminal
    // resident page immediately before Demo installs a chew. Keep that
    // actor-terminal page visible until the third hidden chew callback releases
    // the now-live Muncher records.
    std::vector<std::uint32_t> attractMunchEnemyTerminalHoldPixels_;
    // When the measured Worker (slot 0) and Helper (slot 2) move on one
    // dispatcher tick, only the earlier slot's complete page reaches the
    // display until slot 0 completes. The final later-slot page remains
    // resident through the Demo player's phase-zero installation.
    std::vector<std::uint32_t> attractConcurrentEnemyHoldPixels_;
    bool attractConcurrentEnemyAwaitPlayerCallback_{};
    // A later Troggle slot can install its warning on the same public tick as
    // the earlier player terminal. Preserve the pre-warning player page until
    // the following scheduler callback reaches the presenter.
    std::vector<std::uint32_t> attractPlayerWarningHoldPixels_;
    // An entry that reaches the standing Demo Muncher while an earlier entry
    // is still live retires the logical warning before the DOS dirty painter
    // erases it. Keep that resident warning while the actor list repaints, and
    // hand the surface to the collision callback if the entry catches the
    // Muncher. This is distinct from a newly installed warning above.
    std::vector<std::uint32_t> attractOverlapEntryWarningPixels_;
    // An edge-entry terminal can install state-5 cannibalism after painting
    // its ordinary endpoint. That endpoint remains resident until the first
    // bite callback on the following public tick.
    std::vector<std::uint32_t> cannibalEntryTerminalHoldPixels_;
    // The board painter owns a retained page while a moving Muncher and a
    // state-5 Troggle callback dirty disjoint cells. Keep that working page
    // separate from the stale callback pages queued for host presentation.
    std::vector<std::uint32_t> cannibalPlayerResidentSurfacePixels_;
    std::vector<std::uint32_t> cannibalPlayerPresentationHoldPixels_;
    bool cannibalPlayerRetainedPenultimatePaint_{};
    bool attractInitialCannibalPlayerTerminalHold_{};
    bool invincible_{};
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
    // The DOS input ring holds nine usable bytes. pSeqmouse stores a negated
    // 0..29 board-cell index; pSeqKey stores normalized I/J/K/M/Space bytes in
    // the same FIFO. Cell zero remains distinct from an empty queue through
    // the ring indices/count rather than through its byte value.
    std::deque<int> pointerCellQueue_;
    bool munching_{};
    double munchTimer_{};
    int munchCellIndex_{-1};
    // pMunch snapshots the signed board record before clearing the live cell.
    // Retain the richer native cell so terminal scoring/feedback cannot be
    // changed by independently scheduled board jobs during the seven ticks.
    Cell munchSavedCell_{};
    FeedbackKind feedbackKind_{FeedbackKind::None};
    std::string feedbackMessage_;
    int feedbackEnemyType_{-1};
    int feedbackEnemySlot_{-1};
    bool deathAnimating_{};
    double deathTimer_{};
    int deathSequenceTicks_{};
    bool attractPostFeedbackBoard_{};
    bool lifeLossPending_{};
    std::string nameInput_;

    bool cheatOpen_{};
    int cheatSelection_{};
    int cheatLevel_{1};

    std::array<std::vector<ScoreEntry>, static_cast<std::size_t>(Mode::Count)> scores_;
    bool scorePersistenceEnabled_{true};
    bool settingsPersistenceEnabled_{true};
    std::wstring scorePathOverride_;
    std::wstring settingsPathOverride_;
    int lastGameplaySound_{-1};
    int previousGameplaySound_{-1};
    // PC-speaker PCM and native-only tones retain waveform owners. Original
    // AdLib gameplay/scene cues keep one YM3812 clock running across events;
    // during Demo that same chip adds score channels 1-8 around channel 0.
    WavePlayer musicPlayer_;
    WavePlayer effectPlayer_;
    OplStreamPlayer attractOplPlayer_;

    friend struct GameTestAccess;
};
