#pragma once

#include "original_random.h"
#include "scene_script.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

// Native form of the five Super Munchers opcode-FE host handlers installed
// by the controller switch at image 0x13905. The callback owns only the small
// amount of controller state that the original keeps in DS globals.
class SuperSceneCallbacks {
public:
    using FrameRight = std::function<int(const SceneActor&)>;

    SuperSceneCallbacks(int missionIndex,
                        int difficulty,
                        OriginalRandom& random,
                        FrameRight frameRight = {});

    bool invoke(std::uint16_t value, SceneActor& actor, bool firstVisit);

    [[nodiscard]] int variant() const { return variant_; }
    [[nodiscard]] const std::array<std::uint8_t, 4>& sequence() const {
        return sequence_;
    }
    [[nodiscard]] std::size_t sequencePosition() const { return sequencePosition_; }

private:
    int missionIndex_{};
    int difficulty_{};
    OriginalRandom& random_;
    FrameRight frameRight_;
    int variant_{};
    int frameBase_{};
    std::array<std::uint8_t, 4> sequence_{};
    std::size_t sequencePosition_{};
    std::array<bool, 16> expertToggle_{};
};
