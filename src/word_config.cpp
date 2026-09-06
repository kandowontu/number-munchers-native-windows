#include "word_config.h"

#include <algorithm>
#include <cstring>

namespace {

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::string terminatedString(const std::uint8_t* data, const std::size_t size,
                             bool& terminated) {
    const auto* end = std::find(data, data + size, std::uint8_t{});
    terminated = end != data + size;
    return {reinterpret_cast<const char*>(data), static_cast<std::size_t>(end - data)};
}

std::string shortString(const std::uint8_t* data, const std::size_t capacity,
                        bool& lengthValid) {
    const std::size_t length = data[0];
    lengthValid = length <= capacity;
    if (!lengthValid) return {};
    return {reinterpret_cast<const char*>(data + 1), length};
}

} // namespace

std::string WordHallEntry::name() const {
    const auto end = std::find(nameBytes.begin(), nameBytes.end(), std::uint8_t{});
    return {reinterpret_cast<const char*>(nameBytes.data()),
            static_cast<std::size_t>(end - nameBytes.begin())};
}

WordConfig::WordConfig(const BlobView blob) {
    if (!blob || blob.size != SerializedSize) return;
    std::copy_n(blob.data, bytes_.size(), bytes_.begin());

    if (std::memcmp(bytes_.data(), "MECC", 4) != 0) return;
    version_ = read16(bytes_.data() + 4);
    // Image 0x0CE56 accepts MECC configuration versions 0 and 1.
    if (version_ > 1) return;

    bool applicationTerminated = false;
    applicationName_ = terminatedString(bytes_.data() + 6, 32, applicationTerminated);
    if (!applicationTerminated) return;

    difficulty_ = read16(bytes_.data() + 0x26);
    std::copy_n(bytes_.data() + 0x28, selectedSounds_.size(), selectedSounds_.begin());
    joystickFlags_ = bytes_[0x3c];

    bool hintLengthValid = false;
    hint_ = shortString(bytes_.data() + 0x3d, 255, hintLengthValid);
    bool passwordLengthValid = false;
    password_ = shortString(bytes_.data() + 0x13d, 10, passwordLengthValid);
    if (!passwordLengthValid || !hintLengthValid) return;

    // The persisted word order follows the structure, while the DOS runtime
    // loads +0x76 before +0x74 to obtain horizontal low/high thresholds.
    joystickCalibration_.verticalLow = read16(bytes_.data() + 0x70);
    joystickCalibration_.verticalHigh = read16(bytes_.data() + 0x72);
    joystickCalibration_.horizontalHigh = read16(bytes_.data() + 0x74);
    joystickCalibration_.horizontalLow = read16(bytes_.data() + 0x76);
    savedState_ = read16(bytes_.data() + 0x148);

    hallCount_ = read16(bytes_.data() + 0x14a);
    hallCapacity_ = read16(bytes_.data() + 0x14c);
    hallNameLimit_ = read16(bytes_.data() + 0x14e);
    bool allNamesTerminated = true;
    for (std::size_t index = 0; index < hallEntries_.size(); ++index) {
        const std::size_t offset = 0x150 + index * 30;
        WordHallEntry& entry = hallEntries_[index];
        std::copy_n(bytes_.data() + offset, entry.nameBytes.size(), entry.nameBytes.begin());
        entry.score = read32(bytes_.data() + offset + entry.nameBytes.size());
        if (std::find(entry.nameBytes.begin(), entry.nameBytes.end(), std::uint8_t{}) ==
            entry.nameBytes.end()) {
            allNamesTerminated = false;
        }
    }

    valid_ = true;
    // 0x0CF31 rejects a sound-state byte >= 3. The native guard also bounds
    // the eight-choice difficulty before it can index WLIST count tables.
    contentValid_ = difficulty_ < 8 &&
        std::all_of(selectedSounds_.begin(), selectedSounds_.end(),
                    [](const std::uint8_t value) { return value < 3; });
    // 0x0CF52 requires count <= 10, capacity 10, and name limit 25. Requiring
    // NULs prevents a corrupt fixed record from escaping its 26-byte field.
    hallValid_ = hallCount_ <= HallCapacity && hallCapacity_ == HallCapacity &&
                 hallNameLimit_ == 25 && allNamesTerminated;
}

std::vector<WordHallEntry> WordConfig::activeHallEntries() const {
    if (!hallValid_) return {};
    return {hallEntries_.begin(), hallEntries_.begin() + hallCount_};
}
