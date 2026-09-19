#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct XInputFrame {
    bool connected{};
    std::uint16_t buttons{};
    std::int16_t thumbX{};
    std::int16_t thumbY{};
};

struct XInputActions {
    bool connected{};
    std::array<unsigned int, 4> virtualKeys{};
    std::size_t count{};
};

class XInputMapper {
public:
    XInputActions update(const XInputFrame& frame, double elapsedSeconds);

private:
    unsigned int heldDirection_{};
    std::uint16_t previousButtons_{};
    double repeatCountdown_{};
};

class XInputController {
public:
    XInputController();
    ~XInputController();

    XInputController(const XInputController&) = delete;
    XInputController& operator=(const XInputController&) = delete;

    XInputActions poll(double elapsedSeconds);

private:
    void* library_{};
    void* getState_{};
    XInputMapper mapper_{};
};
