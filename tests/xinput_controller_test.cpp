#include "../src/xinput_controller.h"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <iostream>

namespace {

bool hasKey(const XInputActions& actions, const unsigned int key) {
    return std::find(actions.virtualKeys.begin(),
                     actions.virtualKeys.begin() + actions.count, key) !=
           actions.virtualKeys.begin() + actions.count;
}

bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

} // namespace

int main() {
    bool passed = true;
    XInputMapper mapper;

    passed &= require(mapper.update({true, 0, 1000, -1000}, 0.016).count == 0,
                      "left-stick dead zone generated input");
    passed &= require(hasKey(mapper.update(
                          {true, XINPUT_GAMEPAD_DPAD_LEFT, 32767, 0}, 0.016), VK_LEFT),
                      "D-pad did not take priority over the left stick");
    passed &= require(mapper.update(
                          {true, XINPUT_GAMEPAD_DPAD_LEFT, 32767, 0}, 0.10).count == 0,
                      "held direction repeated before the initial delay");
    passed &= require(hasKey(mapper.update(
                          {true, XINPUT_GAMEPAD_DPAD_LEFT, 32767, 0}, 0.20), VK_LEFT),
                      "held direction did not repeat");

    mapper.update({false, 0, 0, 0}, 0.016);
    XInputActions buttons = mapper.update(
        {true, static_cast<std::uint16_t>(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_BACK |
                                         XINPUT_GAMEPAD_START), 0, 0}, 0.016);
    passed &= require(hasKey(buttons, VK_SPACE), "A was not mapped to Space");
    passed &= require(hasKey(buttons, VK_ESCAPE), "Back/Select was not mapped to Escape");
    passed &= require(hasKey(buttons, VK_RETURN), "Start was not mapped to Enter");
    passed &= require(mapper.update(
                          {true, static_cast<std::uint16_t>(XINPUT_GAMEPAD_A |
                                                           XINPUT_GAMEPAD_BACK |
                                                           XINPUT_GAMEPAD_START), 0, 0},
                          0.016).count == 0,
                      "held action buttons generated duplicate presses");

    mapper.update({false, 0, 0, 0}, 0.016);
    XInputActions alternateButtons = mapper.update(
        {true, static_cast<std::uint16_t>(XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_B |
                                         XINPUT_GAMEPAD_Y), 0, 0}, 0.016);
    passed &= require(hasKey(alternateButtons, VK_SPACE), "X was not mapped to Space");
    passed &= require(hasKey(alternateButtons, VK_ESCAPE), "B was not mapped to Escape");
    passed &= require(hasKey(alternateButtons, VK_RETURN), "Y was not mapped to Enter");

    mapper.update({false, 0, 0, 0}, 0.016);
    passed &= require(hasKey(mapper.update({true, 0, 0, 20000}, 0.016), VK_UP),
                      "positive stick Y was not mapped to Up");
    passed &= require(hasKey(mapper.update({true, 0, -22000, -10000}, 0.016), VK_LEFT),
                      "dominant stick axis was not mapped to a cardinal direction");
    return passed ? 0 : 1;
}
