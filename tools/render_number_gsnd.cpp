#include "../src/assets.h"
#include "../src/mecc_sound.h"
#include "../src/resource_ids.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

bool parseByte(const std::string_view text, std::uint8_t& value) {
    unsigned parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed > 0xff) return false;
    value = static_cast<std::uint8_t>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: NumberMunchersAudioProbe <GSND-index> <output.wav>\n";
        return 2;
    }

    std::uint8_t soundIndex = 0;
    if (!parseByte(argv[1], soundIndex)) {
        std::cerr << "invalid GSND index\n";
        return 2;
    }

    const ResArchive archive(loadEmbeddedResource(IDR_NM_RES));
    const BlobView gsnd = archive.find("GSND", 12);
    if (!gsnd) {
        std::cerr << "embedded Number Munchers GSND 12 is unavailable\n";
        return 3;
    }

    const std::uint32_t releaseTail = soundIndex == 16 ? 0u : 300u;
    const std::vector<std::uint8_t> wave = renderMeccGSoundToWave(
        gsnd, soundIndex, releaseTail, MeccSoundProfile::NumberMunchers);
    if (wave.empty()) {
        std::cerr << "GSND stream failed to decode\n";
        return 4;
    }

    const std::filesystem::path outputPath(argv[2]);
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(wave.data()),
                                 static_cast<std::streamsize>(wave.size()))) {
        std::cerr << "could not write output waveform\n";
        return 5;
    }
    std::cout << outputPath.string() << ' ' << wave.size() << " bytes\n";
    return 0;
}
