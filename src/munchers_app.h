#pragma once

#include "assets.h"
#include "game.h"
#include "render.h"
#include "super_runtime.h"
#include "word_runtime.h"

#include <windows.h>

#include <cstdint>
#include <memory>

enum class MunchersAppMode {
    Launcher,
    NumberMunchers,
    WordMunchers,
    SuperMunchers,
};

// One-window owner for the requested two-game launcher. A game's own terminal
// Quit/Escape request is consumed here and returns to Launcher; only the
// launcher's Exit item ends the process.
class MunchersApp {
public:
    explicit MunchersApp(GraphicsMode graphicsMode = GraphicsMode::Vga256);

    void update(double seconds);
    void render(Renderer& renderer);
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
    [[nodiscard]] bool handleAltShortcut(UINT virtualKey);
    [[nodiscard]] bool acceptsCommonDispatcherInput() const;

    [[nodiscard]] bool shouldQuit() const { return shouldQuit_; }
    [[nodiscard]] MunchersAppMode mode() const { return mode_; }
    [[nodiscard]] int launcherSelection() const { return launcherSelection_; }
    [[nodiscard]] const Game* numberGame() const { return numberGame_.get(); }
    [[nodiscard]] const WordGame* wordGame() const { return wordGame_.get(); }
    [[nodiscard]] const SuperGame* superGame() const { return superGame_.get(); }

private:
    void launchNumberMunchers();
    void launchWordMunchers();
    void launchSuperMunchers();
    void returnToLauncher();
    void consumeGameQuit();
    void activateLauncherSelection();
    void discardGamePresentationFrames();
    void renderLauncher(Renderer& renderer);

    GraphicsMode graphicsMode_{GraphicsMode::Vga256};
    GameAssets launcherAssets_;
    MunchersAppMode mode_{MunchersAppMode::Launcher};
    int launcherSelection_{};
    bool shouldQuit_{};
    bool suppressLeftRelease_{};
    bool suppressRightRelease_{};
    std::unique_ptr<Game> numberGame_;
    std::unique_ptr<WordGame> wordGame_;
    std::unique_ptr<SuperGame> superGame_;
};
