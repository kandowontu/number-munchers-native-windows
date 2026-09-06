#include "munchers_app.h"
#include "render.h"
#include "win32_shortcuts.h"

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

namespace {

constexpr wchar_t WindowClassName[] = L"NumberMunchersNativeWindow";
constexpr wchar_t WindowTitle[] = L"Munchers";

struct Application {
    MunchersApp game;
    Renderer renderer;
    bool fullscreen{};
    WINDOWPLACEMENT windowPlacement{};
    std::chrono::steady_clock::time_point lastUpdate = std::chrono::steady_clock::now();

    explicit Application(const GraphicsMode graphicsMode)
        : game(graphicsMode), renderer(graphicsMode) {
        windowPlacement.length = sizeof(windowPlacement);
    }
};

GraphicsMode requestedGraphicsMode() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) return GraphicsMode::Vga256;
    GraphicsMode result = GraphicsMode::Vga256;
    for (int index = 1; index < argumentCount; ++index) {
        if (_wcsicmp(arguments[index], L"--cga") == 0 ||
            _wcsicmp(arguments[index], L"-cga") == 0 ||
            _wcsicmp(arguments[index], L"/cga") == 0) {
            result = GraphicsMode::Cga4;
        }
    }
    LocalFree(arguments);
    return result;
}

Application* applicationFromWindow(HWND window) {
    return reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

bool gamePointFromClient(HWND window, LPARAM lParam, int& gameX, int& gameY) {
    RECT client{};
    GetClientRect(window, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    double scale = std::min(static_cast<double>(clientWidth) / Renderer::Width,
                            static_cast<double>(clientHeight) / Renderer::Height);
    if (scale >= 1.0) scale = std::floor(scale);
    const int destinationWidth = std::max(1, static_cast<int>(Renderer::Width * scale));
    const int destinationHeight = std::max(1, static_cast<int>(Renderer::Height * scale));
    const int destinationX = (clientWidth - destinationWidth) / 2;
    const int destinationY = (clientHeight - destinationHeight) / 2;
    const int clientX = GET_X_LPARAM(lParam);
    const int clientY = GET_Y_LPARAM(lParam);
    if (clientX < destinationX || clientX >= destinationX + destinationWidth ||
        clientY < destinationY || clientY >= destinationY + destinationHeight) {
        return false;
    }
    gameX = (clientX - destinationX) * Renderer::Width / destinationWidth;
    gameY = (clientY - destinationY) * Renderer::Height / destinationHeight;
    return true;
}

void toggleFullscreen(HWND window, Application& application) {
    if (!application.fullscreen) {
        application.windowPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window, &application.windowPlacement);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        SetWindowLongPtrW(window, GWL_STYLE,
                          GetWindowLongPtrW(window, GWL_STYLE) & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
        SetWindowPos(window, HWND_TOP,
                     monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.top,
                     monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                     monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        application.fullscreen = true;
    } else {
        SetWindowLongPtrW(window, GWL_STYLE,
                          GetWindowLongPtrW(window, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window, &application.windowPlacement);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        application.fullscreen = false;
    }
    // Do not leave the newly exposed client area to a later timer paint. The
    // class has no erase brush, and this synchronously presents one complete
    // offscreen frame after either style transition.
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* application = applicationFromWindow(window);
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE:
        SetTimer(window, 1, 16, nullptr);
        return 0;
    case WM_TIMER:
        if (application) {
            JOYINFOEX joystick{};
            joystick.dwSize = sizeof(joystick);
            joystick.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS;
            const bool joystickConnected =
                joyGetPosEx(JOYSTICKID1, &joystick) == JOYERR_NOERROR;
            application->game.setJoystickState(
                joystickConnected,
                joystickConnected ? joystick.dwXpos : 0,
                joystickConnected ? joystick.dwYpos : 0,
                joystickConnected ? joystick.dwButtons : 0);
            const auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - application->lastUpdate).count();
            application->lastUpdate = now;
            elapsed = std::max(0.0, elapsed);
            application->game.update(elapsed);
            if (application->game.shouldQuit()) {
                DestroyWindow(window);
                return 0;
            }
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        if (!application) return 0;
        if (wParam == VK_RETURN && (lParam & (1L << 29))) {
            if (isInitialWin32KeyPress(lParam)) {
                toggleFullscreen(window, *application);
            }
            return 0;
        }
        if (wParam == VK_F1 && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            if (isInitialWin32KeyPress(lParam)) {
                application->game.toggleCheatMenu();
            }
            return 0;
        }
        if ((lParam & (1L << 29)) && (wParam == 'S' || wParam == 'M' || wParam == 'P')) {
            (void)application->game.handleAltShortcut(static_cast<UINT>(wParam));
            return 0;
        }
        application->game.keyDown(static_cast<UINT>(wParam));
        return 0;
    case WM_SYSCHAR:
        return 0;
    case WM_CHAR:
        if (application) application->game.character(static_cast<wchar_t>(wParam));
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(window);
        if (application) (void)application->game.pointerPress(false);
        return 0;
    case WM_RBUTTONDOWN:
        SetCapture(window);
        if (application) (void)application->game.pointerPress(true);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (application) {
            int gameX = 0;
            int gameY = 0;
            const bool inGame = gamePointFromClient(window, lParam, gameX, gameY);
            if (message == WM_MOUSEMOVE) {
                if (inGame) application->game.pointerMove(gameX, gameY);
            } else {
                // MunchersApp owns physical press/release pairing so a press
                // consumed by a game remains consumed if its release arrives
                // after that game has returned to the launcher. Fixed DOS
                // widgets still accept right release independent of position.
                application->game.pointerButton(
                    inGame ? gameX : 0, inGame ? gameY : 0,
                    message == WM_RBUTTONUP);
            }
        }
        if ((message == WM_LBUTTONUP || message == WM_RBUTTONUP) && GetCapture() == window) {
            ReleaseCapture();
        }
        return 0;
    case WM_PAINT:
        if (application) {
            PAINTSTRUCT paint{};
            HDC deviceContext = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            application->game.render(application->renderer);
            application->renderer.paint(deviceContext, client);
            EndPaint(window, &paint);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        // Renderer::paint fills a private client-sized bitmap and commits it
        // with one BitBlt. A separate class/background erase would expose a
        // black frame between the erase and that atomic commit.
        return 1;
    case WM_SIZE:
        if (application && wParam != SIZE_MINIMIZED) {
            // Resize/fullscreen transitions expose new pixels immediately.
            // Paint them synchronously without requesting WM_ERASEBKGND.
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 336;
        limits->ptMinTrackSize.y = 239;
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    if (const HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiAwareness = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        const FARPROC procedure = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        SetDpiAwareness setDpiAwareness = nullptr;
        static_assert(sizeof(procedure) == sizeof(setDpiAwareness));
        std::memcpy(&setDpiAwareness, &procedure, sizeof(procedure));
        if (setDpiAwareness) {
            setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    // WM_SIZE performs an explicit no-erase redraw. Avoid the class-level
    // H/V redraw flags and background brush, both of which can schedule a
    // visible erase before the persistent backbuffer reaches the window.
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WindowClassName;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }

    auto application = std::make_unique<Application>(requestedGraphicsMode());
    RECT desiredClient{0, 0, 960, 600};
    AdjustWindowRectEx(&desiredClient, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND window = CreateWindowExW(
        0,
        WindowClassName,
        WindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desiredClient.right - desiredClient.left,
        desiredClient.bottom - desiredClient.top,
        nullptr,
        nullptr,
        instance,
        application.get());
    if (!window) {
        return 2;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
