#include "scene_script.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

std::int16_t signedWord(const std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

} // namespace

bool SceneScript::commandAvailable(const std::size_t position, const std::size_t length) const {
    return script_ && position <= script_.size && length <= script_.size - position;
}

std::uint16_t SceneScript::word(const std::size_t position) const {
    return static_cast<std::uint16_t>(script_.data[position]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(script_.data[position + 1]) << 8);
}

bool SceneScript::load(const BlobView script,
                       const std::span<const std::uint16_t> threadOrder) {
    script_ = script;
    threads_.clear();
    publicActors_.clear();
    bakedActors_.clear();
    paintEvents_.clear();
    callbackEvents_.clear();
    signal_ = 0;
    valid_ = false;
    if (!script || script.size < 2) return false;

    const std::uint16_t threadCount = word(0);
    const std::size_t tableSize = 2u + static_cast<std::size_t>(threadCount) * 4u;
    if (threadCount == 0 || !commandAvailable(0, tableSize)) return false;

    for (const std::uint16_t threadIndex : threadOrder) {
        if (threadIndex >= threadCount) return false;
        const std::size_t tablePosition = 2u + static_cast<std::size_t>(threadIndex) * 4u;
        const std::uint32_t offset = static_cast<std::uint32_t>(word(tablePosition)) |
                                     (static_cast<std::uint32_t>(word(tablePosition + 2)) << 16);
        if (offset < tableSize || !commandAvailable(offset, 2)) return false;
        ThreadState state;
        state.actor.thread = threadIndex;
        state.position = offset;
        threads_.push_back(state);
    }
    if (threads_.empty()) return false;
    valid_ = true;
    publishActors();
    return true;
}

void SceneScript::advanceMovement(ThreadState& thread) {
    if (thread.movementStep >= thread.movementSteps || thread.movementSteps <= 0) return;
    ++thread.movementStep;
    // 0x06a58-0x06bf6 uses signed 16.16 accumulators. Each increment is the
    // truncated signed quotient (delta << 16) / duration; the visible word is
    // the high half after adding 0x8000. Preserve the 32-bit wrap and its
    // round-half-toward-positive-infinity behavior, including negative moves.
    thread.movementFixedX = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(thread.movementFixedX) +
        static_cast<std::uint32_t>(thread.movementIncrementX));
    thread.movementFixedY = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(thread.movementFixedY) +
        static_cast<std::uint32_t>(thread.movementIncrementY));
    const auto roundedWord = [](const std::int32_t fixed) {
        const std::uint32_t rounded = static_cast<std::uint32_t>(fixed) + 0x8000u;
        return static_cast<int>(static_cast<std::int16_t>(rounded >> 16));
    };
    thread.actor.x = roundedWord(thread.movementFixedX);
    thread.actor.y = roundedWord(thread.movementFixedY);
    thread.dirty = true;
}

bool SceneScript::executeThread(ThreadState& thread) {
    constexpr int MaxImmediateCommands = 4096;
    for (int commands = 0; commands < MaxImmediateCommands; ++commands) {
        if (!commandAvailable(thread.position, 2)) return false;
        const std::uint8_t length = script_.data[thread.position];
        const std::uint8_t opcode = script_.data[thread.position + 1];
        if (length < 2 || !commandAvailable(thread.position, length)) return false;
        const std::size_t payload = thread.position + 2;
        const auto requireLength = [length](const std::uint8_t expected) {
            return length == expected;
        };

        switch (opcode) {
        case 0x00:
            return false;
        case 0x01: // wait N scene ticks
            if (!requireLength(4)) return false;
            thread.position += length;
            thread.waitTicks = word(payload);
            if (thread.waitTicks != 0) return true;
            break;
        case 0x02: // move relative over N ticks
        case 0x03: { // move to absolute coordinates over N ticks
            if (!requireLength(8)) return false;
            const int duration = word(payload);
            const int x = signedWord(word(payload + 2));
            const int y = signedWord(word(payload + 4));
            thread.position += length;
            thread.movementStartX = thread.actor.x;
            thread.movementStartY = thread.actor.y;
            thread.movementTargetX = opcode == 0x03 ? x : thread.actor.x + x;
            thread.movementTargetY = opcode == 0x03 ? y : thread.actor.y + y;
            thread.movementSteps = duration;
            thread.movementStep = 0;
            if (duration == 0) {
                thread.actor.x = thread.movementTargetX;
                thread.actor.y = thread.movementTargetY;
                break;
            }
            thread.movementFixedX = static_cast<std::int32_t>(thread.actor.x * 65'536);
            thread.movementFixedY = static_cast<std::int32_t>(thread.actor.y * 65'536);
            thread.movementIncrementX = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(thread.movementTargetX - thread.actor.x) * 65'536) /
                duration);
            thread.movementIncrementY = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(thread.movementTargetY - thread.actor.y) * 65'536) /
                duration);
            advanceMovement(thread);
            thread.waitTicks = duration;
            return true;
        }
        case 0x04: // select an absolute bitmap frame
            if (!requireLength(4)) return false;
            thread.actor.frame = word(payload);
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x05: // next bitmap frame
            if (!requireLength(2)) return false;
            ++thread.actor.frame;
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x06: // previous bitmap frame
            if (!requireLength(2)) return false;
            --thread.actor.frame;
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x07: { // counted backward branch
            if (!requireLength(8)) return false;
            if (thread.loopPosition != thread.position || thread.loopRemaining == 0) {
                thread.loopPosition = thread.position;
                thread.loopRemaining = word(payload);
            } else {
                --thread.loopRemaining;
            }
            const std::uint16_t rewind = word(payload + 4);
            if (thread.loopRemaining != 0) {
                if (rewind > thread.position) return false;
                thread.position -= rewind;
            } else {
                thread.position += length;
                thread.loopPosition = static_cast<std::size_t>(-1);
            }
            break;
        }
        case 0x08: // erase/hide actor
            if (!requireLength(2)) return false;
            thread.actor.visible = false;
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x09: // show actor
            if (!requireLength(2)) return false;
            thread.actor.visible = true;
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x0a: // publish synchronization signal
            if (!requireLength(4)) return false;
            signal_ = word(payload);
            thread.position += length;
            break;
        case 0x0b: // wait for synchronization signal
            if (!requireLength(4)) return false;
            if (signal_ != word(payload)) return true;
            thread.position += length;
            break;
        case 0x0c: // actor attribute used only by the generic MECC callback layer
            if (!requireLength(4)) return false;
            thread.actor.layer = word(payload);
            thread.dirty = true;
            thread.position += length;
            break;
        case 0x0d: // host callback/event
            if (!requireLength(4)) return false;
            callbackEvents_.push_back(word(payload));
            thread.position += length;
            break;
        case 0x0e:
            if (!requireLength(2)) return false;
            // 0x06da3 clears both the visible member and saved-background
            // pointer after the current pixels have become part of the scene.
            // Several scripts reuse this same actor to place more background
            // pieces, so retaining only its latest state loses earlier tiles.
            if (thread.actor.visible) {
                SceneActor baked = thread.actor;
                baked.ended = false;
                bakedActors_.push_back(baked);
            }
            // The original deliberately clears both the live visibility word
            // and the painter's just-snapshotted previous visibility word.
            // Its pixels therefore stay in the retained framebuffer without
            // generating an erase rectangle on this callback.
            thread.previousActor.visible = false;
            thread.actor.visible = false;
            thread.dirty = true;
            thread.detached = true;
            thread.position += length;
            break;
        case 0xfe: // optional game-specific actor callback
            if (!requireLength(4)) return false;
            // The shared dispatcher at Number 0x06DBF / Super 0x07304 calls
            // the installed host hook with the payload, live actor, and a
            // first-visit flag. A null hook is deliberately a no-op that
            // advances. The supplied Super handlers all return true, but the
            // boolean remains part of the recovered VM contract.
            if (extendedCallback_ &&
                !extendedCallback_(word(payload), thread.actor, true)) {
                return true;
            }
            thread.dirty = true;
            thread.position += length;
            break;
        case 0xff:
            if (!requireLength(2)) return false;
            // The interpreter only marks the object for same-callback list
            // removal. Unlike a hide command, it does not set the actor's
            // dirty word, so the retained framebuffer is not restored merely
            // because the object is unlinked.
            thread.actor.ended = true;
            return true;
        default:
            return false;
        }
    }
    return false;
}

void SceneScript::tick() {
    if (!valid_) return;
    callbackEvents_.clear();
    paintEvents_.clear();
    for (ThreadState& thread : threads_) {
        thread.previousActor = thread.actor;
        thread.dirty = false;
        thread.detached = false;
        if (thread.actor.ended) {
            paintEvents_.push_back({thread.previousActor, thread.actor, false, false});
            continue;
        }
        if (thread.waitTicks > 0) {
            advanceMovement(thread);
            --thread.waitTicks;
            if (thread.waitTicks > 0) {
                paintEvents_.push_back(
                    {thread.previousActor, thread.actor, thread.dirty, thread.detached});
                continue;
            }
        }
        if (!executeThread(thread)) {
            valid_ = false;
            break;
        }
        paintEvents_.push_back(
            {thread.previousActor, thread.actor, thread.dirty, thread.detached});
    }
    // Preserve construction-order cardinality even if a malformed thread
    // invalidated the stream early. Valid scripts always take the first path.
    while (paintEvents_.size() < threads_.size()) {
        const ThreadState& thread = threads_[paintEvents_.size()];
        paintEvents_.push_back({thread.actor, thread.actor, false, false});
    }
    publishActors();
}

bool SceneScript::finished() const {
    return valid_ && !threads_.empty() &&
           std::all_of(threads_.begin(), threads_.end(),
                       [](const ThreadState& thread) { return thread.actor.ended; });
}

void SceneScript::publishActors() {
    publicActors_.clear();
    publicActors_.reserve(threads_.size());
    for (const ThreadState& thread : threads_) publicActors_.push_back(thread.actor);
}
