#include "super_scene_callbacks.h"

#include <algorithm>
#include <utility>

SuperSceneCallbacks::SuperSceneCallbacks(const int missionIndex,
                                         const int difficulty,
                                         OriginalRandom& random,
                                         FrameRight frameRight)
    : missionIndex_(missionIndex),
      difficulty_(difficulty),
      random_(random),
      frameRight_(std::move(frameRight)) {
    switch (missionIndex_) {
    case 0:
    case 1:
        // 0x128D0 and 0x129EC consume one rand()%4 before constructing actors.
        variant_ = random_.range(4);
        break;
    case 2:
        // 0x12A4F-0x12A98 draws four distinct digits without replacement.
        for (std::size_t index = 0; index < sequence_.size(); ++index) {
            int candidate = 0;
            do {
                candidate = random_.range(10);
            } while (std::find(sequence_.begin(), sequence_.begin() + index,
                               candidate) != sequence_.begin() + index);
            sequence_[index] = static_cast<std::uint8_t>(candidate);
        }
        variant_ = random_.range(4);
        break;
    default:
        break;
    }
}

bool SuperSceneCallbacks::invoke(const std::uint16_t value,
                                 SceneActor& actor,
                                 const bool firstVisit) {
    if (!firstVisit) return true;

    switch (missionIndex_) {
    case 0:
        // 0x139EB: payload 3 chooses one of three frames in the selected
        // six-frame group. Payload 4 selects that group's fourth frame.
        if (value == 3) {
            frameBase_ = variant_ * 6 + 4;
            actor.frame = frameBase_ + random_.range(3);
        } else {
            actor.frame = frameBase_ + 3;
        }
        break;
    case 1:
        // This mission installs no FE callback and its selected threads do
        // not contain FE. Preserve the null-host behavior if called manually.
        break;
    case 2:
        // 0x13B16: the four callback visits consume the pre-shuffled digit
        // list and select frames 5..14.
        if (sequencePosition_ < sequence_.size()) {
            actor.frame = static_cast<int>(sequence_[sequencePosition_++]) + 5;
        }
        break;
    case 3:
        if (difficulty_ == 6) {
            // 0x13982: expert mode toggles a byte on each actor. Every second
            // visit selects frame 7/9 from the actor's right edge at x=96.
            const std::size_t slot = static_cast<std::size_t>(actor.thread) %
                                     expertToggle_.size();
            expertToggle_[slot] = !expertToggle_[slot];
            if (!expertToggle_[slot]) {
                const int right = frameRight_ ? frameRight_(actor) : actor.x;
                actor.frame = right > 96 ? 9 : 7;
            }
        } else {
            // 0x13A49: ordinary mode chooses a top-edge x position, chooses
            // a 12..19 sprite with the source's 1-in-4 branch, then teleports.
            const int targetX = random_.range(24) * 8;
            if (random_.range(4) < 1) {
                actor.frame = random_.range(3) + 17;
            } else {
                actor.frame = random_.range(5) + 12;
            }
            actor.x = targetX;
            actor.y = -12;
        }
        break;
    case 4:
        // 0x13AE9: choose one of frames 61..66.
        actor.frame = random_.range(6) + 61;
        break;
    default:
        break;
    }
    return true;
}
