#pragma once

#include "assets.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

struct SceneActor {
    int thread{};
    int frame{};
    int x{};
    int y{};
    int layer{};
    bool visible{};
    bool ended{};
};

struct ScenePaintEvent {
    SceneActor previous;
    SceneActor current;
    bool dirty{};
    bool detached{};
};

// Bounds-checked native interpreter for the compact MECC SCPT command stream.
// Rendering stays in Game; this class owns only deterministic scene state.
class SceneScript {
public:
    using ExtendedCallback =
        std::function<bool(std::uint16_t value, SceneActor& actor, bool firstVisit)>;

    bool load(BlobView script, std::span<const std::uint16_t> threadOrder);
    void setExtendedCallback(ExtendedCallback callback) {
        extendedCallback_ = std::move(callback);
    }
    void tick();

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] bool finished() const;
    [[nodiscard]] std::uint16_t signal() const { return signal_; }
    [[nodiscard]] const std::vector<SceneActor>& actors() const { return publicActors_; }
    [[nodiscard]] const std::vector<SceneActor>& bakedActors() const { return bakedActors_; }
    [[nodiscard]] const std::vector<ScenePaintEvent>& paintEvents() const {
        return paintEvents_;
    }
    [[nodiscard]] const std::vector<std::uint16_t>& callbackEvents() const {
        return callbackEvents_;
    }

private:
    struct ThreadState {
        SceneActor actor;
        SceneActor previousActor;
        std::size_t position{};
        bool dirty{};
        bool detached{};
        int waitTicks{};
        int movementStep{};
        int movementSteps{};
        int movementStartX{};
        int movementStartY{};
        int movementTargetX{};
        int movementTargetY{};
        std::int32_t movementFixedX{};
        std::int32_t movementFixedY{};
        std::int32_t movementIncrementX{};
        std::int32_t movementIncrementY{};
        std::size_t loopPosition{static_cast<std::size_t>(-1)};
        std::uint16_t loopRemaining{};
    };

    [[nodiscard]] bool commandAvailable(std::size_t position, std::size_t length) const;
    [[nodiscard]] std::uint16_t word(std::size_t position) const;
    void advanceMovement(ThreadState& thread);
    bool executeThread(ThreadState& thread);
    void publishActors();

    BlobView script_{};
    std::vector<ThreadState> threads_;
    std::vector<SceneActor> publicActors_;
    std::vector<SceneActor> bakedActors_;
    std::vector<ScenePaintEvent> paintEvents_;
    std::vector<std::uint16_t> callbackEvents_;
    ExtendedCallback extendedCallback_;
    std::uint16_t signal_{};
    bool valid_{};
};
