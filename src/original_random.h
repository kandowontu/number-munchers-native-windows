#pragma once

#include <cstdint>

// Borland's 32-bit linear generator as used by both Munchers executables.
// The games seed only the low 16 bits and consume the high 15 bits.
struct OriginalRandom {
    explicit OriginalRandom(std::uint32_t seedValue = 1) { seed(seedValue); }

    void seed(std::uint32_t value) {
        state = value & 0xffffu;
        calls = 0;
    }

    [[nodiscard]] std::uint16_t next() {
        state = state * 0x015a4e35u + 1u;
        ++calls;
        return static_cast<std::uint16_t>((state >> 16) & 0x7fffu);
    }

    [[nodiscard]] int range(int upperExclusive) {
        return upperExclusive > 0 ? static_cast<int>(next() % upperExclusive) : 0;
    }

    std::uint32_t state{1};
    std::uint64_t calls{};
};
