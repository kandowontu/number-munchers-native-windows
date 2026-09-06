#include "audio_player.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace {

std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

bool idEquals(const std::uint8_t* bytes, const char (&id)[5]) {
    return std::memcmp(bytes, id, 4) == 0;
}

struct ParsedWave {
    WAVEFORMATEX format{};
    std::size_t dataOffset{};
    std::uint32_t dataLength{};
};

bool parseWave(const std::vector<std::uint8_t>& bytes, ParsedWave& parsed) {
    if (bytes.size() < 12 || !idEquals(bytes.data(), "RIFF") ||
        !idEquals(bytes.data() + 8, "WAVE")) {
        return false;
    }
    const std::uint64_t riffEnd = static_cast<std::uint64_t>(read32(bytes.data() + 4)) + 8;
    if (riffEnd > bytes.size()) return false;

    bool haveFormat = false;
    bool haveData = false;
    std::size_t position = 12;
    while (position + 8 <= riffEnd) {
        const std::uint32_t chunkLength = read32(bytes.data() + position + 4);
        const std::size_t payload = position + 8;
        if (payload > riffEnd || chunkLength > riffEnd - payload) return false;
        if (idEquals(bytes.data() + position, "fmt ")) {
            if (chunkLength < 16) return false;
            parsed.format.wFormatTag = read16(bytes.data() + payload);
            parsed.format.nChannels = read16(bytes.data() + payload + 2);
            parsed.format.nSamplesPerSec = read32(bytes.data() + payload + 4);
            parsed.format.nAvgBytesPerSec = read32(bytes.data() + payload + 8);
            parsed.format.nBlockAlign = read16(bytes.data() + payload + 12);
            parsed.format.wBitsPerSample = read16(bytes.data() + payload + 14);
            parsed.format.cbSize = 0;
            haveFormat = true;
        } else if (idEquals(bytes.data() + position, "data")) {
            if (chunkLength == 0 || chunkLength > std::numeric_limits<DWORD>::max()) return false;
            parsed.dataOffset = payload;
            parsed.dataLength = chunkLength;
            haveData = true;
        }
        const std::uint64_t next = static_cast<std::uint64_t>(payload) + chunkLength +
                                   (chunkLength & 1u);
        if (next > riffEnd) return false;
        position = static_cast<std::size_t>(next);
    }

    if (!haveFormat || !haveData || parsed.format.wFormatTag != WAVE_FORMAT_PCM ||
        parsed.format.nChannels == 0 || parsed.format.nSamplesPerSec == 0 ||
        parsed.format.nBlockAlign == 0 ||
        (parsed.format.wBitsPerSample != 8 && parsed.format.wBitsPerSample != 16)) {
        return false;
    }
    const std::uint32_t expectedBlockAlign =
        static_cast<std::uint32_t>(parsed.format.nChannels) * parsed.format.wBitsPerSample / 8;
    return expectedBlockAlign == parsed.format.nBlockAlign &&
           parsed.dataLength % parsed.format.nBlockAlign == 0;
}

} // namespace

WavePlayer::~WavePlayer() {
    stop();
}

bool WavePlayer::play(std::vector<std::uint8_t> wave, const bool loop) {
    return play(std::make_shared<const std::vector<std::uint8_t>>(std::move(wave)), loop);
}

bool WavePlayer::play(std::shared_ptr<const std::vector<std::uint8_t>> wave, const bool loop) {
    stop();
    if (!wave) return false;
    ParsedWave parsed;
    if (!parseWave(*wave, parsed)) return false;

    wave_ = std::move(wave);
    looping_ = loop;
#ifdef NUMBER_MUNCHERS_TESTING
    // Console regression tests validate lifecycle and mixing-slot ownership
    // without opening an audio device or any visible application window.
    playing_ = true;
    ++playCount_;
    return true;
#else
    MMRESULT result = waveOutOpen(&handle_, WAVE_MAPPER, &parsed.format,
                                  0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        wave_.reset();
        looping_ = false;
        return false;
    }

    header_ = {};
    header_.lpData = reinterpret_cast<LPSTR>(
        const_cast<std::uint8_t*>(wave_->data() + parsed.dataOffset));
    header_.dwBufferLength = parsed.dataLength;
    if (loop) {
        header_.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
        header_.dwLoops = std::numeric_limits<DWORD>::max();
    }
    result = waveOutPrepareHeader(handle_, &header_, sizeof(header_));
    if (result != MMSYSERR_NOERROR) {
        waveOutClose(handle_);
        handle_ = nullptr;
        wave_.reset();
        looping_ = false;
        return false;
    }
    prepared_ = true;
    result = waveOutWrite(handle_, &header_, sizeof(header_));
    if (result != MMSYSERR_NOERROR) {
        stop();
        return false;
    }
    playing_ = true;
    ++playCount_;
    return true;
#endif
}

void WavePlayer::stop() {
#ifndef NUMBER_MUNCHERS_TESTING
    if (handle_) {
        waveOutReset(handle_);
        if (prepared_) waveOutUnprepareHeader(handle_, &header_, sizeof(header_));
        waveOutClose(handle_);
    }
#endif
    handle_ = nullptr;
    header_ = {};
    prepared_ = false;
    playing_ = false;
    looping_ = false;
    wave_.reset();
}
