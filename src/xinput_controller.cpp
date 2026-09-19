#include "xinput_controller.h"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr double InitialRepeatDelay = 0.28;
constexpr double RepeatInterval = 0.10;

unsigned int directionForFrame(const XInputFrame& frame) {
    const bool left = (frame.buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    const bool right = (frame.buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    const bool up = (frame.buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    const bool down = (frame.buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    if (left != right) return left ? VK_LEFT : VK_RIGHT;
    if (up != down) return up ? VK_UP : VK_DOWN;

    const int x = frame.thumbX;
    const int y = frame.thumbY;
    const int absoluteX = std::abs(x);
    const int absoluteY = std::abs(y);
    if (absoluteX < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE &&
        absoluteY < XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
        return 0;
    }
    if (absoluteX >= absoluteY) return x < 0 ? VK_LEFT : VK_RIGHT;
    return y < 0 ? VK_DOWN : VK_UP;
}

void appendKey(XInputActions& actions, const unsigned int key) {
    if (actions.count < actions.virtualKeys.size()) {
        actions.virtualKeys[actions.count++] = key;
    }
}

} // namespace

XInputActions XInputMapper::update(const XInputFrame& frame, const double elapsedSeconds) {
    XInputActions actions{};
    actions.connected = frame.connected;
    if (!frame.connected) {
        heldDirection_ = 0;
        previousButtons_ = 0;
        repeatCountdown_ = 0.0;
        return actions;
    }

    const std::uint16_t pressed = frame.buttons & ~previousButtons_;
    previousButtons_ = frame.buttons;
    if ((pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X)) != 0) {
        appendKey(actions, VK_SPACE);
    }
    if ((pressed & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_B)) != 0) {
        appendKey(actions, VK_ESCAPE);
    }
    if ((pressed & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_Y)) != 0) {
        appendKey(actions, VK_RETURN);
    }

    const unsigned int direction = directionForFrame(frame);
    if (direction == 0) {
        heldDirection_ = 0;
        repeatCountdown_ = 0.0;
    } else if (direction != heldDirection_) {
        appendKey(actions, direction);
        heldDirection_ = direction;
        repeatCountdown_ = InitialRepeatDelay;
    } else {
        repeatCountdown_ -= std::max(0.0, elapsedSeconds);
        if (repeatCountdown_ <= 0.0) {
            appendKey(actions, direction);
            do {
                repeatCountdown_ += RepeatInterval;
            } while (repeatCountdown_ <= 0.0);
        }
    }
    return actions;
}

XInputController::XInputController() {
    constexpr const wchar_t* candidates[] = {
        L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"
    };
    for (const wchar_t* candidate : candidates) {
        const HMODULE library = LoadLibraryW(candidate);
        if (!library) continue;
        const FARPROC procedure = GetProcAddress(library, "XInputGetState");
        if (procedure) {
            library_ = library;
            static_assert(sizeof(procedure) == sizeof(getState_));
            std::memcpy(&getState_, &procedure, sizeof(procedure));
            break;
        }
        FreeLibrary(library);
    }
}

XInputController::~XInputController() {
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
}

XInputActions XInputController::poll(const double elapsedSeconds) {
    XInputFrame frame{};
    if (getState_) {
        using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        GetState getState = nullptr;
        static_assert(sizeof(getState) == sizeof(getState_));
        std::memcpy(&getState, &getState_, sizeof(getState));
        for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
            XINPUT_STATE state{};
            if (getState(user, &state) != ERROR_SUCCESS) continue;
            frame.connected = true;
            frame.buttons = state.Gamepad.wButtons;
            frame.thumbX = state.Gamepad.sThumbLX;
            frame.thumbY = state.Gamepad.sThumbLY;
            break;
        }
    }
    return mapper_.update(frame, elapsedSeconds);
}
