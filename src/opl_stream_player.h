#pragma once

#include "dro_audio.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Streams replaceable effect writes, optionally with a looping score, through
// one continuously clocked YM3812. This preserves ordinary driver state between
// cues and the channel-1..8 music/channel-0 effect relationship in Demo.
class OplStreamPlayer {
public:
    OplStreamPlayer();
    ~OplStreamPlayer();

    OplStreamPlayer(const OplStreamPlayer&) = delete;
    OplStreamPlayer& operator=(const OplStreamPlayer&) = delete;
    OplStreamPlayer(OplStreamPlayer&&) = delete;
    OplStreamPlayer& operator=(OplStreamPlayer&&) = delete;

    // Opens one continuously clocked chip without installing a score. This is
    // the original ordinary-gameplay state: replaceable channel-0 cues share
    // LFO/noise/register history across otherwise silent intervals.
    bool startIdle();
    bool startLoop(std::vector<OplWrite> writes, std::uint32_t durationMilliseconds);
    // Installs a non-repeating background score on channels 1-8 without
    // resetting the live chip or its independently scheduled channel-0 cue.
    bool playScoreOnce(std::vector<OplWrite> writes,
                       std::uint32_t durationMilliseconds);
    bool playEffect(std::vector<OplWrite> writes, std::uint32_t durationMilliseconds);
    // Executes the resident GSND-0 channel-retirement contract without
    // resetting or closing the underlying YM3812.
    void stopAllChannels();
    // Queues that same resident retirement after a host-side blocking delay.
    // Cartoon teardown uses this for its second GSND-0 call after 15 ms.
    void stopAllChannelsAfter(std::uint32_t delayMilliseconds);
    void stopEffect();
    void stop();

    [[nodiscard]] bool playing() const;
    [[nodiscard]] bool looping() const; // true only when a score loop is installed
    [[nodiscard]] bool effectPlaying() const;
    [[nodiscard]] std::size_t loopPlayCount() const;
    [[nodiscard]] std::size_t effectPlayCount() const;
    [[nodiscard]] std::size_t scoreWriteCount() const;
#ifdef NUMBER_MUNCHERS_TESTING
    [[nodiscard]] std::vector<std::int16_t> renderTestSamples(std::size_t count);
    [[nodiscard]] std::uint8_t registerValueForTest(std::uint8_t reg) const;
    [[nodiscard]] std::size_t allChannelStopCountForTest() const;
    [[nodiscard]] double bufferedMillisecondsForTest() const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
