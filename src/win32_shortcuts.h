#pragma once

#include <windows.h>

// Bit 30 is the documented previous-key-state flag on WM_KEYDOWN and
// WM_SYSKEYDOWN. Toggle-style shell shortcuts act only on the physical edge;
// repeat messages are consumed without applying the toggle again.
[[nodiscard]] constexpr bool isInitialWin32KeyPress(const LPARAM lParam) noexcept {
    constexpr LPARAM PreviousKeyState = static_cast<LPARAM>(1) << 30;
    return (lParam & PreviousKeyState) == 0;
}
