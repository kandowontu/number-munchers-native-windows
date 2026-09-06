#include "opl_stream_player.h"

#include "ymfm_opl.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t AdLibClock = 3'579'545;
constexpr std::size_t BufferCount = 12;
// YM3812's native rate is about 49.7 kHz.  The former 256-sample headers made
// WinMM wake an ordinary worker every ~5.1 ms and left only ~20.6 ms queued.
// A briefly busy desktop could exhaust that queue and expose audible gaps.
// Twelve 512-sample headers retain the ~10.3 ms refill/cue granularity while
// raising the complete device queue to ~123.6 ms.  Eight headers (~82.4 ms)
// still exposed gaps on the user's busy desktop, where the app and other
// accelerated windows can occasionally occupy a scheduling interval longer
// than an ordinary multimedia queue.
constexpr std::size_t SamplesPerBuffer = 512;
constexpr std::array<std::uint8_t, 9> OperatorOffsets = {
    0, 1, 2, 8, 9, 10, 16, 17, 18
};

class Ym3812Interface final : public ymfm::ymfm_interface {};

#ifndef NUMBER_MUNCHERS_TESTING
// Resolve MMCSS dynamically so the self-contained executable keeps the same
// system-DLL import boundary on Windows versions where AVRT is unavailable.
// The Audio task class raises only this sleeping refill worker and is less
// aggressive than the Pro Audio class, which could compete with the UI.
class AudioThreadScheduling final {
public:
    AudioThreadScheduling() {
        module_ = LoadLibraryW(L"avrt.dll");
        if (module_) {
            const auto setCharacteristics = reinterpret_cast<SetCharacteristics>(
                GetProcAddress(module_, "AvSetMmThreadCharacteristicsW"));
            setPriority_ = reinterpret_cast<SetPriority>(
                GetProcAddress(module_, "AvSetMmThreadPriority"));
            revert_ = reinterpret_cast<Revert>(
                GetProcAddress(module_, "AvRevertMmThreadCharacteristics"));
            if (setCharacteristics && revert_) {
                DWORD taskIndex = 0;
                task_ = setCharacteristics(L"Audio", &taskIndex);
                if (task_ && setPriority_) {
                    // AVRT_PRIORITY_HIGH is the stable numeric value 1.
                    (void)setPriority_(task_, 1);
                }
            }
        }
        if (!task_) {
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        }
    }

    ~AudioThreadScheduling() {
        if (task_ && revert_) (void)revert_(task_);
        if (module_) FreeLibrary(module_);
    }

    AudioThreadScheduling(const AudioThreadScheduling&) = delete;
    AudioThreadScheduling& operator=(const AudioThreadScheduling&) = delete;

private:
    using SetCharacteristics = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
    using SetPriority = BOOL (WINAPI*)(HANDLE, int);
    using Revert = BOOL (WINAPI*)(HANDLE);

    HMODULE module_{};
    HANDLE task_{};
    SetPriority setPriority_{};
    Revert revert_{};
};
#endif

struct ScheduledWrite {
    std::uint64_t sample{};
    std::uint8_t reg{};
    std::uint8_t value{};
};

struct ScheduledSound {
    std::uint64_t durationSamples{};
    std::vector<ScheduledWrite> writes;
};

std::optional<ScheduledSound> scheduleSound(
    const std::vector<OplWrite>& writes,
    const std::uint32_t durationMilliseconds,
    const std::uint32_t sampleRate) {
    if (durationMilliseconds == 0 || sampleRate == 0) return std::nullopt;
    ScheduledSound result;
    result.durationSamples =
        static_cast<std::uint64_t>(durationMilliseconds) * sampleRate / 1000;
    if (result.durationSamples == 0) return std::nullopt;
    result.writes.reserve(writes.size());
    std::uint32_t previousMilliseconds = 0;
    for (const OplWrite& write : writes) {
        if (write.milliseconds < previousMilliseconds ||
            write.milliseconds > durationMilliseconds) return std::nullopt;
        previousMilliseconds = write.milliseconds;
        result.writes.push_back({
            static_cast<std::uint64_t>(write.milliseconds) * sampleRate / 1000,
            write.reg,
            write.value,
        });
    }
    return result;
}

} // namespace

struct OplStreamPlayer::Impl {
    Ym3812Interface interface;
    ymfm::ym3812 chip{interface};
    const std::uint32_t sampleRate{chip.sample_rate(AdLibClock)};

    std::atomic<bool> playing{};
    std::atomic<bool> looping{};
    std::atomic<bool> effectPlaying{};
    std::atomic<std::size_t> loopPlayCount{};
    std::atomic<std::size_t> effectPlayCount{};
    std::atomic<std::size_t> allChannelStopCount{};
    std::atomic<std::size_t> scoreWriteCount{};

    ScheduledSound score;
    std::size_t scoreWriteIndex{};
    std::uint64_t scoreStartSample{};
    std::uint64_t generatedSamples{};
    bool scoreRepeats{};
    bool scoreActive{};
    std::array<std::uint8_t, 256> registers{};

    std::mutex stateMutex;
    std::optional<ScheduledSound> pendingScore;
    bool pendingScoreRepeats{};
    std::optional<ScheduledSound> pendingEffect;
    ScheduledSound effect;
    std::size_t effectWriteIndex{};
    std::uint64_t effectStartSample{};
    bool effectActive{};
    bool pendingKeyOff{};
    bool pendingStopAll{};
    std::vector<std::uint64_t> pendingDelayedStopSamples;
    std::vector<std::uint64_t> scheduledStopSamples;

#ifndef NUMBER_MUNCHERS_TESTING
    HWAVEOUT handle{};
    std::array<std::array<std::int16_t, SamplesPerBuffer>, BufferCount> buffers{};
    std::array<WAVEHDR, BufferCount> headers{};
    std::jthread worker;
    std::mutex completionMutex;
    std::condition_variable completion;
    std::atomic<bool> stopping{};
    std::deque<std::size_t> completedQueue;
#endif

    void writeRegister(const std::uint8_t reg, const std::uint8_t value) {
        registers[reg] = value;
        chip.write_address(reg);
        chip.write_data(value);
    }

    void applyDriverStop() {
        score = {};
        scoreWriteIndex = 0;
        scoreRepeats = false;
        scoreActive = false;
        effect = {};
        effectWriteIndex = 0;
        effectActive = false;
        pendingKeyOff = false;
        // GSND 0 dispatches B3 00..08. The handler at 0x0EE1 retires each
        // physical channel with these four writes in this exact order.
        for (std::uint8_t channel = 0; channel < OperatorOffsets.size(); ++channel) {
            const std::uint8_t operatorOffset = OperatorOffsets[channel];
            writeRegister(static_cast<std::uint8_t>(0xc0 + channel), 0x00);
            writeRegister(static_cast<std::uint8_t>(0x43 + operatorOffset), 0x3f);
            writeRegister(static_cast<std::uint8_t>(0x83 + operatorOffset), 0xff);
            writeRegister(static_cast<std::uint8_t>(0xb0 + channel), 0x00);
        }
        // C3 at 0x1169 clears rhythm mode while preserving the two depth bits
        // already held in the resident driver's BD shadow.
        writeRegister(0xbd, static_cast<std::uint8_t>(registers[0xbd] & 0xc0u));
        looping.store(false, std::memory_order_release);
        effectPlaying.store(false, std::memory_order_release);
        scoreWriteCount.store(0, std::memory_order_release);
        allChannelStopCount.fetch_add(1, std::memory_order_relaxed);
    }

    void installPendingStops() {
        bool stopNow = false;
        std::vector<std::uint64_t> delayed;
        {
            std::lock_guard lock(stateMutex);
            stopNow = pendingStopAll;
            pendingStopAll = false;
            delayed.swap(pendingDelayedStopSamples);
        }
        if (stopNow) applyDriverStop();
        for (const std::uint64_t delay : delayed) {
            scheduledStopSamples.push_back(generatedSamples + delay);
        }
        std::sort(scheduledStopSamples.begin(), scheduledStopSamples.end());
    }

    void applyScheduledStops() {
        while (!scheduledStopSamples.empty() &&
               scheduledStopSamples.front() <= generatedSamples) {
            {
                // A driver-wide stop reached after its host delay also retires
                // any event submitted during that interval.
                std::lock_guard lock(stateMutex);
                pendingScore.reset();
                pendingScoreRepeats = false;
                pendingEffect.reset();
                pendingKeyOff = false;
            }
            applyDriverStop();
            scheduledStopSamples.erase(scheduledStopSamples.begin());
        }
    }

    void installPendingEffect() {
        std::lock_guard lock(stateMutex);
        if (pendingKeyOff) {
            writeRegister(0xb0, 0x00);
            pendingKeyOff = false;
            effectActive = false;
            effectPlaying.store(false, std::memory_order_release);
        }
        if (!pendingEffect) return;
        // Replacing the driver's logical channel 0 first releases its old
        // carrier, then dispatches the new stream's time-zero writes.
        writeRegister(0xb0, 0x00);
        effect = std::move(*pendingEffect);
        pendingEffect.reset();
        effectWriteIndex = 0;
        effectStartSample = generatedSamples;
        effectActive = true;
        effectPlaying.store(true, std::memory_order_release);
    }

    void installPendingScore() {
        std::lock_guard lock(stateMutex);
        if (!pendingScore) return;
        score = std::move(*pendingScore);
        pendingScore.reset();
        scoreWriteIndex = 0;
        scoreStartSample = generatedSamples;
        scoreRepeats = pendingScoreRepeats;
        pendingScoreRepeats = false;
        scoreActive = true;
    }

    void applyScoreWrites() {
        if (!scoreActive || generatedSamples < scoreStartSample) return;
        const std::uint64_t elapsed = generatedSamples - scoreStartSample;
        const std::uint64_t position = scoreRepeats
            ? elapsed % score.durationSamples
            : std::min(elapsed, score.durationSamples);
        if (scoreRepeats && elapsed != 0 && position == 0) {
            while (scoreWriteIndex < score.writes.size() &&
                   score.writes[scoreWriteIndex].sample <= score.durationSamples) {
                const ScheduledWrite& write = score.writes[scoreWriteIndex++];
                writeRegister(write.reg, write.value);
            }
            scoreWriteIndex = 0;
        }
        while (scoreWriteIndex < score.writes.size() &&
               score.writes[scoreWriteIndex].sample <= position) {
            const ScheduledWrite& write = score.writes[scoreWriteIndex++];
            writeRegister(write.reg, write.value);
        }
        if (!scoreRepeats && elapsed >= score.durationSamples) scoreActive = false;
    }

    void applyEffectWrites() {
        if (!effectActive) return;
        const std::uint64_t position = generatedSamples - effectStartSample;
        while (effectWriteIndex < effect.writes.size() &&
               effect.writes[effectWriteIndex].sample <= position) {
            const ScheduledWrite& write = effect.writes[effectWriteIndex++];
            writeRegister(write.reg, write.value);
        }
        if (position >= effect.durationSamples) {
            while (effectWriteIndex < effect.writes.size() &&
                   effect.writes[effectWriteIndex].sample <= effect.durationSamples) {
                const ScheduledWrite& write = effect.writes[effectWriteIndex++];
                writeRegister(write.reg, write.value);
            }
            effectActive = false;
            effectPlaying.store(false, std::memory_order_release);
        }
    }

    void generate(std::span<std::int16_t> output) {
        installPendingStops();
        installPendingScore();
        installPendingEffect();
        for (std::int16_t& sample : output) {
            applyScheduledStops();
            applyScoreWrites();
            applyEffectWrites();
            ymfm::ym3812::output_data generated;
            chip.generate(&generated);
            generated.clamp16();
            sample = static_cast<std::int16_t>(generated.data[0]);
            ++generatedSamples;
        }
    }

#ifndef NUMBER_MUNCHERS_TESTING
    static void CALLBACK waveCallback(HWAVEOUT, const UINT message,
                                      const DWORD_PTR instance, const DWORD_PTR parameter,
                                      DWORD_PTR) {
        if (message != WOM_DONE || instance == 0) return;
        auto* self = reinterpret_cast<Impl*>(instance);
        if (self->stopping.load(std::memory_order_acquire)) return;
        const auto* completed = reinterpret_cast<const WAVEHDR*>(parameter);
        for (std::size_t index = 0; index < BufferCount; ++index) {
            if (completed == &self->headers[index]) {
                {
                    // Preserve callback chronology. A bitmask can reorder a
                    // delayed batch that wraps from header 7 to header 0,
                    // submitting adjacent PCM blocks in the wrong order.
                    std::lock_guard lock(self->completionMutex);
                    self->completedQueue.push_back(index);
                }
                self->completion.notify_one();
                break;
            }
        }
    }

    bool refill(const std::size_t index) {
        if (stopping.load(std::memory_order_acquire)) return false;
        generate(buffers[index]);
        return waveOutWrite(handle, &headers[index], sizeof(WAVEHDR)) == MMSYSERR_NOERROR;
    }

    void run() {
        // The waveform callback only signals this worker; all synthesis and
        // waveOutWrite calls intentionally stay off the driver callback. MMCSS
        // gives the refill worker bounded Audio-class service without raising
        // the game's UI thread or another process.
        AudioThreadScheduling scheduling;
        for (std::size_t index = 0; index < BufferCount; ++index) {
            if (!refill(index)) {
                stopping.store(true, std::memory_order_release);
                playing.store(false, std::memory_order_release);
                return;
            }
        }
        while (!stopping.load(std::memory_order_acquire)) {
            std::deque<std::size_t> finished;
            std::unique_lock lock(completionMutex);
            completion.wait(lock, [this] {
                return stopping.load(std::memory_order_acquire) ||
                       !completedQueue.empty();
            });
            if (stopping.load(std::memory_order_acquire)) break;
            finished.swap(completedQueue);
            lock.unlock();
            for (const std::size_t index : finished) {
                if (!refill(index)) {
                    stopping.store(true, std::memory_order_release);
                    playing.store(false, std::memory_order_release);
                    break;
                }
            }
        }
    }

    bool openDevice() {
        if (sampleRate == 0) return false;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign = sizeof(std::int16_t);
        format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
        const MMRESULT opened = waveOutOpen(
            &handle, WAVE_MAPPER, &format,
            reinterpret_cast<DWORD_PTR>(&Impl::waveCallback),
            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (opened != MMSYSERR_NOERROR) return false;

        std::size_t prepared = 0;
        for (; prepared < BufferCount; ++prepared) {
            WAVEHDR& header = headers[prepared];
            header = {};
            header.lpData = reinterpret_cast<LPSTR>(buffers[prepared].data());
            header.dwBufferLength = static_cast<DWORD>(
                buffers[prepared].size() * sizeof(std::int16_t));
            if (waveOutPrepareHeader(handle, &header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                break;
            }
        }
        if (prepared != BufferCount) {
            for (std::size_t index = 0; index < prepared; ++index) {
                waveOutUnprepareHeader(handle, &headers[index], sizeof(WAVEHDR));
            }
            waveOutClose(handle);
            handle = nullptr;
            return false;
        }
        stopping.store(false, std::memory_order_release);
        {
            std::lock_guard lock(completionMutex);
            completedQueue.clear();
        }
        return true;
    }

    void closeDevice() {
        stopping.store(true, std::memory_order_release);
        if (handle) waveOutReset(handle);
        completion.notify_all();
        if (worker.joinable()) worker.join();
        if (handle) {
            for (WAVEHDR& header : headers) {
                waveOutUnprepareHeader(handle, &header, sizeof(WAVEHDR));
            }
            waveOutClose(handle);
        }
        handle = nullptr;
        headers = {};
        {
            std::lock_guard lock(completionMutex);
            completedQueue.clear();
        }
    }
#endif

    bool start(std::vector<OplWrite> writes, const std::uint32_t durationMilliseconds,
               const bool hasScoreLoop) {
        stop();
        const std::optional<ScheduledSound> scheduled =
            scheduleSound(writes, durationMilliseconds, sampleRate);
        if (!scheduled) return false;
        score = *scheduled;
        scoreWriteIndex = 0;
        scoreStartSample = 0;
        generatedSamples = 0;
        scoreRepeats = hasScoreLoop;
        scoreActive = true;
        chip.reset();
        registers.fill(0);
        scoreWriteCount.store(score.writes.size(), std::memory_order_release);
#ifndef NUMBER_MUNCHERS_TESTING
        if (!openDevice()) {
            score = {};
            scoreWriteCount.store(0, std::memory_order_release);
            return false;
        }
#endif
        playing.store(true, std::memory_order_release);
        looping.store(hasScoreLoop, std::memory_order_release);
        if (hasScoreLoop) loopPlayCount.fetch_add(1, std::memory_order_relaxed);
#ifndef NUMBER_MUNCHERS_TESTING
        worker = std::jthread([this] { run(); });
#endif
        return true;
    }

    bool playScore(std::vector<OplWrite> writes,
                   const std::uint32_t durationMilliseconds,
                   const bool repeats) {
        if (!playing.load(std::memory_order_acquire)) {
            return start(std::move(writes), durationMilliseconds, repeats);
        }
        const std::optional<ScheduledSound> scheduled =
            scheduleSound(writes, durationMilliseconds, sampleRate);
        if (!scheduled) return false;
        {
            std::lock_guard lock(stateMutex);
            pendingScore = *scheduled;
            pendingScoreRepeats = repeats;
        }
        scoreWriteCount.store(scheduled->writes.size(), std::memory_order_release);
        looping.store(repeats, std::memory_order_release);
        if (repeats) loopPlayCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void stopDriverChannels(const std::uint32_t delayMilliseconds = 0) {
        if (!playing.load(std::memory_order_acquire)) {
            looping.store(false, std::memory_order_release);
            effectPlaying.store(false, std::memory_order_release);
            scoreWriteCount.store(0, std::memory_order_release);
            return;
        }
        if (delayMilliseconds != 0) {
            std::lock_guard lock(stateMutex);
            pendingDelayedStopSamples.push_back(
                static_cast<std::uint64_t>(delayMilliseconds) * sampleRate / 1'000u);
            return;
        }
        {
            std::lock_guard lock(stateMutex);
            pendingScore.reset();
            pendingScoreRepeats = false;
            pendingEffect.reset();
            pendingKeyOff = false;
            pendingStopAll = true;
        }
        looping.store(false, std::memory_order_release);
        effectPlaying.store(false, std::memory_order_release);
        scoreWriteCount.store(0, std::memory_order_release);
    }

    bool play(std::vector<OplWrite> writes, const std::uint32_t durationMilliseconds) {
        if (!playing.load(std::memory_order_acquire)) return false;
        const std::optional<ScheduledSound> scheduled =
            scheduleSound(writes, durationMilliseconds, sampleRate);
        if (!scheduled) return false;
        {
            std::lock_guard lock(stateMutex);
            pendingEffect = *scheduled;
            pendingKeyOff = false;
        }
#ifdef NUMBER_MUNCHERS_TESTING
        effect = *scheduled;
        effectActive = true;
#endif
        effectPlaying.store(true, std::memory_order_release);
        effectPlayCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void stopCurrentEffect() {
        {
            std::lock_guard lock(stateMutex);
            pendingEffect.reset();
            pendingKeyOff = playing.load(std::memory_order_acquire);
#ifdef NUMBER_MUNCHERS_TESTING
            effectActive = false;
#endif
        }
        effectPlaying.store(false, std::memory_order_release);
    }

    void stop() {
#ifndef NUMBER_MUNCHERS_TESTING
        closeDevice();
#endif
        playing.store(false, std::memory_order_release);
        looping.store(false, std::memory_order_release);
        effectPlaying.store(false, std::memory_order_release);
        {
            std::lock_guard lock(stateMutex);
            pendingScore.reset();
            pendingScoreRepeats = false;
            pendingEffect.reset();
            pendingStopAll = false;
            pendingDelayedStopSamples.clear();
            effect = {};
            effectActive = false;
            pendingKeyOff = false;
        }
        score = {};
        scoreWriteIndex = 0;
        scoreStartSample = 0;
        generatedSamples = 0;
        scoreRepeats = false;
        scoreActive = false;
        scheduledStopSamples.clear();
        scoreWriteCount.store(0, std::memory_order_release);
    }
};

OplStreamPlayer::OplStreamPlayer() : impl_(std::make_unique<Impl>()) {}
OplStreamPlayer::~OplStreamPlayer() {
    // Unlike an internal GSND-0 dispatch, destroying a game at the collection
    // launcher is a real native ownership boundary.  Close waveOut and wake
    // the worker before std::jthread's destructor tries to join it.
    if (impl_) impl_->stop();
}

bool OplStreamPlayer::startIdle() {
    if (playing()) return true;
    // A silent one-second schedule keeps the existing modulo-based clock path
    // active without installing or reporting a music loop.
    return impl_->start({}, 1'000, false);
}

bool OplStreamPlayer::startLoop(std::vector<OplWrite> writes,
                                const std::uint32_t durationMilliseconds) {
    return impl_->playScore(std::move(writes), durationMilliseconds, true);
}

bool OplStreamPlayer::playScoreOnce(std::vector<OplWrite> writes,
                                    const std::uint32_t durationMilliseconds) {
    return impl_->playScore(std::move(writes), durationMilliseconds, false);
}

bool OplStreamPlayer::playEffect(std::vector<OplWrite> writes,
                                 const std::uint32_t durationMilliseconds) {
    return impl_->play(std::move(writes), durationMilliseconds);
}

void OplStreamPlayer::stopAllChannels() { impl_->stopDriverChannels(); }
void OplStreamPlayer::stopAllChannelsAfter(const std::uint32_t delayMilliseconds) {
    impl_->stopDriverChannels(delayMilliseconds);
}
void OplStreamPlayer::stopEffect() { impl_->stopCurrentEffect(); }
void OplStreamPlayer::stop() { impl_->stop(); }
bool OplStreamPlayer::playing() const { return impl_->playing.load(std::memory_order_acquire); }
bool OplStreamPlayer::looping() const { return impl_->looping.load(std::memory_order_acquire); }
bool OplStreamPlayer::effectPlaying() const {
    return impl_->effectPlaying.load(std::memory_order_acquire);
}
std::size_t OplStreamPlayer::loopPlayCount() const {
    return impl_->loopPlayCount.load(std::memory_order_relaxed);
}
std::size_t OplStreamPlayer::effectPlayCount() const {
    return impl_->effectPlayCount.load(std::memory_order_relaxed);
}
std::size_t OplStreamPlayer::scoreWriteCount() const {
    return impl_->scoreWriteCount.load(std::memory_order_acquire);
}
#ifdef NUMBER_MUNCHERS_TESTING
std::vector<std::int16_t> OplStreamPlayer::renderTestSamples(const std::size_t count) {
    if (!playing()) return {};
    std::vector<std::int16_t> samples(count);
    impl_->generate(samples);
    return samples;
}
std::uint8_t OplStreamPlayer::registerValueForTest(const std::uint8_t reg) const {
    return impl_->registers[reg];
}
std::size_t OplStreamPlayer::allChannelStopCountForTest() const {
    return impl_->allChannelStopCount.load(std::memory_order_relaxed);
}
double OplStreamPlayer::bufferedMillisecondsForTest() const {
    return static_cast<double>(BufferCount * SamplesPerBuffer) * 1'000.0 /
           static_cast<double>(impl_->sampleRate);
}
#endif
