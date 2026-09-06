#pragma once

#include <windows.h>
#include <mmsystem.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Owns one WinMM waveform stream and its backing memory. Separate instances
// are mixed by the Windows audio engine, which lets the original channel-1..8
// demo score continue while channel-0 gameplay cues are replaced independently.
class WavePlayer {
public:
    WavePlayer() = default;
    ~WavePlayer();

    WavePlayer(const WavePlayer&) = delete;
    WavePlayer& operator=(const WavePlayer&) = delete;
    WavePlayer(WavePlayer&&) = delete;
    WavePlayer& operator=(WavePlayer&&) = delete;

    bool play(std::vector<std::uint8_t> wave, bool loop = false);
    bool play(std::shared_ptr<const std::vector<std::uint8_t>> wave, bool loop = false);
    void stop();

    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] bool looping() const { return playing_ && looping_; }
    [[nodiscard]] std::size_t playCount() const { return playCount_; }
    [[nodiscard]] std::size_t bufferSize() const { return wave_ ? wave_->size() : 0; }
#ifdef NUMBER_MUNCHERS_TESTING
    [[nodiscard]] std::uint64_t bufferHash() const {
        std::uint64_t hash = 1469598103934665603ull;
        if (!wave_) return hash;
        for (const std::uint8_t byte : *wave_) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }
#endif

private:
    HWAVEOUT handle_{};
    WAVEHDR header_{};
    bool prepared_{};
    bool playing_{};
    bool looping_{};
    std::size_t playCount_{};
    std::shared_ptr<const std::vector<std::uint8_t>> wave_;
};
