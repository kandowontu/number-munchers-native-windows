#include "../src/scene_script.h"
#include "../src/super_scene_callbacks.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::uint16_t read16(const BlobView blob) {
    return blob.size >= 2 ? static_cast<std::uint16_t>(blob.data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(blob.data[1]) << 8) : 0;
}

void hash16(std::uint64_t& hash, const std::uint16_t value) {
    for (int shift = 0; shift < 16; shift += 8) {
        hash ^= static_cast<std::uint8_t>(value >> shift);
        hash *= 1099511628211ull;
    }
}

bool callbacksMatchExecutable() {
    {
        OriginalRandom actualRandom(0x1234u);
        OriginalRandom reference(0x1234u);
        SuperSceneCallbacks callback(0, 3, actualRandom);
        const int variant = reference.range(4);
        SceneActor actor;
        actor.thread = 5;
        const int expectedFirst = variant * 6 + 4 + reference.range(3);
        if (!callback.invoke(3, actor, true) || actor.frame != expectedFirst ||
            !callback.invoke(4, actor, true) || actor.frame != variant * 6 + 7 ||
            callback.variant() != variant || actualRandom.state != reference.state ||
            actualRandom.calls != reference.calls) return false;
    }
    {
        OriginalRandom actualRandom(0x2345u);
        OriginalRandom reference(0x2345u);
        SuperSceneCallbacks callback(1, 3, actualRandom);
        if (callback.variant() != reference.range(4) ||
            actualRandom.state != reference.state || actualRandom.calls != 1) return false;
    }
    {
        OriginalRandom actualRandom(0x3456u);
        OriginalRandom reference(0x3456u);
        std::array<std::uint8_t, 4> sequence{};
        for (std::size_t index = 0; index < sequence.size(); ++index) {
            int candidate = 0;
            do {
                candidate = reference.range(10);
            } while (std::find(sequence.begin(), sequence.begin() + index, candidate) !=
                     sequence.begin() + index);
            sequence[index] = static_cast<std::uint8_t>(candidate);
        }
        const int variant = reference.range(4);
        SuperSceneCallbacks callback(2, 3, actualRandom);
        if (callback.sequence() != sequence || callback.variant() != variant ||
            actualRandom.state != reference.state || actualRandom.calls != reference.calls) {
            return false;
        }
        SceneActor actor;
        actor.thread = 2;
        for (std::size_t index = 0; index < sequence.size(); ++index) {
            if (!callback.invoke(0, actor, true) ||
                actor.frame != static_cast<int>(sequence[index]) + 5) return false;
        }
    }
    {
        OriginalRandom actualRandom(0x4567u);
        OriginalRandom reference(0x4567u);
        SuperSceneCallbacks callback(3, 5, actualRandom);
        SceneActor actor;
        actor.thread = 6;
        actor.x = 40;
        actor.y = 90;
        const int expectedX = reference.range(24) * 8;
        const int expectedFrame = reference.range(4) < 1
            ? reference.range(3) + 17
            : reference.range(5) + 12;
        if (!callback.invoke(1, actor, true) || actor.x != expectedX || actor.y != -12 ||
            actor.frame != expectedFrame || actualRandom.state != reference.state ||
            actualRandom.calls != reference.calls) return false;
    }
    {
        OriginalRandom random(0x5678u);
        SuperSceneCallbacks callback(3, 6, random,
            [](const SceneActor& actor) { return actor.x + 20; });
        SceneActor left;
        left.thread = 7;
        left.x = 70;
        left.frame = 4;
        if (!callback.invoke(2, left, true) || left.frame != 4 ||
            !callback.invoke(2, left, true) || left.frame != 7) return false;
        SceneActor right;
        right.thread = 8;
        right.x = 80;
        right.frame = 4;
        if (!callback.invoke(2, right, true) || right.frame != 4 ||
            !callback.invoke(2, right, true) || right.frame != 9 || random.calls != 0) {
            return false;
        }
    }
    {
        OriginalRandom actualRandom(0x6789u);
        OriginalRandom reference(0x6789u);
        SuperSceneCallbacks callback(4, 3, actualRandom);
        SceneActor actor;
        actor.thread = 9;
        const int expected = reference.range(6) + 61;
        if (!callback.invoke(0, actor, true) || actor.frame != expected ||
            actualRandom.state != reference.state || actualRandom.calls != 1) return false;
    }
    return true;
}

} // namespace

int main() {
    if (!callbacksMatchExecutable()) {
        std::cerr << "Super opcode-FE mission callback behavior drifted\n";
        return 1;
    }
    GameAssets assets(GraphicsMode::Vga256, GameAssetSet::SuperMunchers);
    struct Expected {
        int ticks;
        std::size_t callbackCount;
        std::uint64_t callbackHash;
        std::size_t extendedCount;
        std::uint64_t extendedHash;
        std::uint64_t actorHash;
    };
    constexpr std::array<Expected, 5> expected = {{
        {59, 0, 0x14650fb0739d0383ull, 17, 0xbf570f7a66e1f65cull,
         0x83b04049d48e63daull},
        {104, 0, 0x14650fb0739d0383ull, 0, 0x14650fb0739d0383ull,
         0xcc036536f9fa99cfull},
        {180, 0, 0x14650fb0739d0383ull, 4, 0xf9f4938cb3358b53ull,
         0x24eb06c8a04494bbull},
        {321, 0, 0x14650fb0739d0383ull, 16, 0xf51913228f3e10acull,
         0x29d93fa43a6b1a74ull},
        {185, 23, 0x0d6430d9c314b072ull, 1, 0x69b429c0459f68e3ull,
         0x1ba49dd50149589bull},
    }};
    constexpr std::array<std::array<std::uint16_t, 10>, 5> threadOrders = {{
        {0, 1, 2, 3, 4, 5, 6},
        {1, 0, 5, 6},
        {3, 4, 5, 6, 0, 1, 2},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
        {4, 7, 3, 5, 6, 8, 0, 1, 2, 9},
    }};
    constexpr std::array<std::size_t, 5> orderSizes = {7, 4, 7, 10, 10};
    constexpr std::array<std::uint16_t, 5> rawThreadCounts = {7, 7, 7, 10, 10};
    constexpr std::array<std::uint32_t, 5> scriptIds = {2022, 2024, 2026, 2028, 2030};
    for (std::size_t scriptIndex = 0; scriptIndex < scriptIds.size(); ++scriptIndex) {
        const std::uint32_t id = scriptIds[scriptIndex];
        const BlobView blob = assets.gameArchive().find("SCPT", id);
        const std::uint16_t threadCount = read16(blob);
        if (threadCount != rawThreadCounts[scriptIndex]) {
            std::cerr << "Super SCPT " << id << " raw thread-table count drifted\n";
            return 1;
        }
        SceneScript script;
        std::uint64_t extendedHash = 1469598103934665603ull;
        std::size_t extendedCount = 0;
        script.setExtendedCallback([&](const std::uint16_t value,
                                       SceneActor& actor,
                                       const bool firstVisit) {
            hash16(extendedHash, static_cast<std::uint16_t>(actor.thread));
            hash16(extendedHash, value);
            hash16(extendedHash, static_cast<std::uint16_t>(firstVisit));
            ++extendedCount;
            return true;
        });
        const std::span<const std::uint16_t> order(
            threadOrders[scriptIndex].data(), orderSizes[scriptIndex]);
        if (!script.load(blob, order)) {
            std::cerr << "Super SCPT " << id << " rejected structurally\n";
            return 1;
        }
        std::uint64_t callbackHash = 1469598103934665603ull;
        std::uint64_t actorHash = 1469598103934665603ull;
        std::size_t callbackCount = 0;
        int ticks = 0;
        while (script.valid() && !script.finished() && ticks < 20'000) {
            script.tick();
            ++ticks;
            for (const std::uint16_t event : script.callbackEvents()) {
                hash16(callbackHash, event);
                ++callbackCount;
            }
            for (const SceneActor& actor : script.actors()) {
                for (const std::uint16_t value : {
                         static_cast<std::uint16_t>(actor.thread),
                         static_cast<std::uint16_t>(actor.frame),
                         static_cast<std::uint16_t>(actor.x),
                         static_cast<std::uint16_t>(actor.y),
                         static_cast<std::uint16_t>(actor.visible),
                         static_cast<std::uint16_t>(actor.ended)}) {
                    hash16(actorHash, value);
                }
            }
        }
        if (!script.valid()) {
            std::cerr << "Super SCPT " << id << " reached an unsupported command\n";
            return 2;
        }
        const Expected& want = expected[scriptIndex];
        if (!script.finished() || script.signal() != 99 || ticks != want.ticks ||
            callbackCount != want.callbackCount || callbackHash != want.callbackHash ||
            extendedCount != want.extendedCount || extendedHash != want.extendedHash ||
            actorHash != want.actorHash) {
            std::cerr << "Super SCPT " << id << " drifted from its static timeline: ticks="
                      << ticks << " signal=" << script.signal() << " callbacks="
                      << callbackCount << " callbackHash=0x" << std::hex << callbackHash
                      << " extended=" << std::dec << extendedCount << " extendedHash=0x"
                      << std::hex << extendedHash << " actorHash=0x" << actorHash << '\n';
            return 3;
        }
    }
    std::cout << "Super SCPT structure, thread order, callbacks, and timelines passed\n";
    return 0;
}
