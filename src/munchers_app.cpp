#include "munchers_app.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <string_view>

namespace {

constexpr std::array<std::string_view, 4> LauncherItems = {
    "Number Munchers", "Word Munchers", "Super Munchers", "Exit"
};
constexpr std::array<int, 4> LauncherRows = {66, 91, 116, 141};
constexpr double MaximumControllerUpdateSlice = 0.1;

} // namespace

MunchersApp::MunchersApp(const GraphicsMode graphicsMode)
    : graphicsMode_(graphicsMode), launcherAssets_(graphicsMode) {}

void MunchersApp::launchNumberMunchers() {
    wordGame_.reset();
    superGame_.reset();
    numberGame_ = std::make_unique<Game>(graphicsMode_);
    mode_ = MunchersAppMode::NumberMunchers;
}

void MunchersApp::launchWordMunchers() {
    numberGame_.reset();
    superGame_.reset();
    wordGame_ = std::make_unique<WordGame>(
        graphicsMode_, static_cast<std::uint16_t>(std::time(nullptr)));
    mode_ = MunchersAppMode::WordMunchers;
}

void MunchersApp::launchSuperMunchers() {
    numberGame_.reset();
    wordGame_.reset();
    superGame_ = std::make_unique<SuperGame>(
        graphicsMode_, static_cast<std::uint16_t>(std::time(nullptr)));
    mode_ = MunchersAppMode::SuperMunchers;
}

void MunchersApp::returnToLauncher() {
    numberGame_.reset();
    wordGame_.reset();
    superGame_.reset();
    mode_ = MunchersAppMode::Launcher;
}

void MunchersApp::consumeGameQuit() {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_ && numberGame_->shouldQuit()) {
        launcherSelection_ = 0;
        returnToLauncher();
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_ && wordGame_->shouldQuit()) {
        launcherSelection_ = 1;
        returnToLauncher();
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_ && superGame_->shouldQuit()) {
        launcherSelection_ = 2;
        returnToLauncher();
    }
}

void MunchersApp::activateLauncherSelection() {
    switch (launcherSelection_) {
    case 0: launchNumberMunchers(); break;
    case 1: launchWordMunchers(); break;
    case 2: launchSuperMunchers(); break;
    case 3: shouldQuit_ = true; break;
    default: break;
    }
}

void MunchersApp::discardGamePresentationFrames() {
    if (numberGame_) numberGame_->discardPresentationFrames();
    if (wordGame_) wordGame_->discardPresentationFrames();
    if (superGame_) superGame_->discardPresentationFrames();
}

void MunchersApp::update(const double seconds) {
    double remaining = std::max(0.0, seconds);
    if (remaining == 0.0) {
        if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
            numberGame_->update(0.0);
        } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
            wordGame_->update(0.0);
        } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
            superGame_->update(0.0);
        }
        consumeGameQuit();
        return;
    }

    // WM_TIMER messages can coalesce while Windows is busy. Preserve all
    // steady-clock time, but present it to each recovered controller in the
    // already-gated 100 ms maximum slices so synchronous page boundaries are
    // visited in order rather than skipping elapsed time wholesale.
    while (remaining > 0.0 && mode_ != MunchersAppMode::Launcher) {
        const double slice = std::min(remaining, MaximumControllerUpdateSlice);
        if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
            numberGame_->update(slice);
        } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
            wordGame_->update(slice);
        } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
            superGame_->update(slice);
        }
        remaining -= slice;
        consumeGameQuit();
    }
}

void MunchersApp::keyDown(const UINT virtualKey) {
    if (mode_ == MunchersAppMode::Launcher) {
        if (virtualKey == VK_ESCAPE) {
            shouldQuit_ = true;
            return;
        }
        if (virtualKey == VK_UP) launcherSelection_ = (launcherSelection_ + 3) % 4;
        if (virtualKey == VK_DOWN) launcherSelection_ = (launcherSelection_ + 1) % 4;
        if (virtualKey == 'N') launcherSelection_ = 0;
        if (virtualKey == 'W') launcherSelection_ = 1;
        if (virtualKey == 'S') launcherSelection_ = 2;
        if (virtualKey == 'E' || virtualKey == 'Q') launcherSelection_ = 3;
        if (virtualKey == VK_RETURN) activateLauncherSelection();
        return;
    }
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        discardGamePresentationFrames();
        numberGame_->keyDown(virtualKey);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        discardGamePresentationFrames();
        wordGame_->keyDown(virtualKey);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        discardGamePresentationFrames();
        superGame_->keyDown(virtualKey);
    }
    consumeGameQuit();
}

void MunchersApp::character(const wchar_t characterValue) {
    discardGamePresentationFrames();
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->character(characterValue);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->character(characterValue);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->character(characterValue);
    }
    consumeGameQuit();
}

void MunchersApp::pointerMove(const int x, const int y) {
    if (mode_ == MunchersAppMode::Launcher) {
        if (x >= 68 && x <= 252) {
            for (int index = 0; index < 4; ++index) {
                const int row = LauncherRows[static_cast<std::size_t>(index)];
                if (y >= row - 5 && y <= row + 13) launcherSelection_ = index;
            }
        }
        return;
    }
    discardGamePresentationFrames();
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->pointerMove(x, y);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->pointerMove(x, y);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->pointerMove(x, y);
    }
}

bool MunchersApp::pointerPress(const bool secondary) {
    discardGamePresentationFrames();
    bool consumed = false;
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        consumed = numberGame_->pointerPress(secondary);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        consumed = wordGame_->pointerPress(secondary);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        consumed = superGame_->pointerPress(secondary);
    }
    if (secondary) suppressRightRelease_ = consumed;
    else suppressLeftRelease_ = consumed;
    return consumed;
}

void MunchersApp::pointerButton(const int x, const int y, const bool secondary) {
    bool& suppressRelease = secondary ? suppressRightRelease_ : suppressLeftRelease_;
    if (suppressRelease) {
        suppressRelease = false;
        return;
    }
    discardGamePresentationFrames();
    if (mode_ == MunchersAppMode::Launcher) {
        if (secondary) {
            activateLauncherSelection();
            return;
        }
        if (x >= 68 && x <= 252) {
            for (int index = 0; index < 4; ++index) {
                const int row = LauncherRows[static_cast<std::size_t>(index)];
                if (y >= row - 5 && y <= row + 13) {
                    launcherSelection_ = index;
                    activateLauncherSelection();
                    return;
                }
            }
        }
        return;
    }
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->pointerButton(x, y, secondary);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->pointerButton(x, y, secondary);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->pointerButton(x, y, secondary);
    }
    consumeGameQuit();
}

void MunchersApp::setJoystickState(const bool connected, const std::uint32_t x,
                                   const std::uint32_t y, const std::uint32_t buttons) {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->setJoystickState(connected, x, y, buttons);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->setJoystickState(connected, x, y, buttons);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->setJoystickState(connected, x, y, buttons);
    }
}

void MunchersApp::toggleCheatMenu() {
    discardGamePresentationFrames();
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->toggleCheatMenu();
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->toggleCheatMenu();
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->toggleCheatMenu();
    }
}

void MunchersApp::toggleSound() {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) numberGame_->toggleSound();
    if (mode_ == MunchersAppMode::WordMunchers && wordGame_) wordGame_->toggleSound();
    if (mode_ == MunchersAppMode::SuperMunchers && superGame_) superGame_->toggleSound();
}

void MunchersApp::toggleMusic() {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) numberGame_->toggleMusic();
    if (mode_ == MunchersAppMode::WordMunchers && wordGame_) wordGame_->toggleMusic();
    if (mode_ == MunchersAppMode::SuperMunchers && superGame_) superGame_->toggleMusic();
}

void MunchersApp::toggleSpeaker() {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) numberGame_->toggleSpeaker();
    if (mode_ == MunchersAppMode::WordMunchers && wordGame_) wordGame_->toggleSpeaker();
    if (mode_ == MunchersAppMode::SuperMunchers && superGame_) superGame_->toggleSpeaker();
}

bool MunchersApp::handleAltShortcut(const UINT virtualKey) {
    if (virtualKey != 'S' && virtualKey != 'M' && virtualKey != 'P') return false;

    if (!acceptsCommonDispatcherInput()) {
        // The DOS resident dispatcher is suspended while a synchronous UI
        // routine owns input. Forward one non-character event so an any-event
        // startup gate can still acknowledge Alt+key; modal widgets ignore it.
        keyDown(VK_PROCESSKEY);
        return true;
    }

    if (virtualKey == 'S') {
        // Alt+S returns directly from the resident dispatcher.
        toggleSound();
        return true;
    }
    if (virtualKey == 'M') toggleMusic();
    else toggleSpeaker();

    // Alt+M and Alt+P rejoin the selector-100 state switch. The synthetic
    // non-character event preserves Demo exit and cartoon skip without a
    // separate WM_SYSCHAR letter leaking into the successor page. A failed
    // save owns a blocking alert first; each runtime defers this continuation
    // until that alert consumes a later physical acknowledgement.
    if (!acceptsCommonDispatcherInput()) return true;
    keyDown(VK_PROCESSKEY);
    return true;
}

bool MunchersApp::acceptsCommonDispatcherInput() const {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        return numberGame_->acceptsCommonDispatcherInput();
    }
    if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        return wordGame_->acceptsCommonDispatcherInput();
    }
    if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        return superGame_->acceptsCommonDispatcherInput();
    }
    return false;
}

void MunchersApp::render(Renderer& renderer) {
    if (mode_ == MunchersAppMode::NumberMunchers && numberGame_) {
        numberGame_->renderPresentation(renderer);
    } else if (mode_ == MunchersAppMode::WordMunchers && wordGame_) {
        wordGame_->renderPresentation(renderer);
    } else if (mode_ == MunchersAppMode::SuperMunchers && superGame_) {
        superGame_->renderPresentation(renderer);
    } else {
        renderLauncher(renderer);
    }
}

void MunchersApp::renderLauncher(Renderer& renderer) {
    renderer.setGraphicsMode(graphicsMode_);
    renderer.clear(Colors::Black);
    const GemFont& font = launcherAssets_.largeFont();
    renderer.outlineRect(7, 7, 306, 186, Colors::Magenta);
    renderer.outlineRect(9, 9, 302, 182, Colors::Cyan);
    renderer.drawCenteredText(font, 159, 25, "MUNCHERS", Colors::Yellow);
    renderer.drawCenteredText(font, 159, 42, "Choose a game", Colors::White);
    for (int index = 0; index < 4; ++index) {
        const std::string_view label = LauncherItems[static_cast<std::size_t>(index)];
        const int y = LauncherRows[static_cast<std::size_t>(index)];
        const int width = renderer.textWidth(font, label);
        const int x = (Renderer::Width - width) / 2;
        if (launcherSelection_ == index) {
            renderer.fillRect(x - 12, y - 4, width + 24, 16, Colors::White);
        }
        renderer.drawText(font, x, y, label,
                          launcherSelection_ == index ? Colors::Black : Colors::White);
    }
    renderer.drawCenteredText(font, 159, 169,
                              "Alt+Enter: Full Screen", Colors::White);
    renderer.drawCenteredText(font, 159, 181,
                              "Ctrl+Alt+F1: Level Select", Colors::White);
}
