#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: analyze_reference_enemies RAW_RGB START_SECONDS FPS\n";
        return 2;
    }
    constexpr int width = 144;
    constexpr int height = 75;
    constexpr int columns = 6;
    constexpr int rows = 5;
    constexpr std::size_t frameBytes = static_cast<std::size_t>(width * height * 3);
    const double start = std::stod(argv[2]);
    const double fps = std::stod(argv[3]);
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 3;

    std::vector<std::uint8_t> frame(frameBytes);
    std::string previous;
    std::uint64_t index = 0;
    while (input.read(reinterpret_cast<char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size()))) {
        std::array<int, rows * columns> redCounts{};
        std::array<int, rows * columns> cyanCounts{};
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>((y * width + x) * 3);
                const int red = frame[pixel];
                const int green = frame[pixel + 1];
                const int blue = frame[pixel + 2];
                const int cell = (y / 15) * columns + x / 24;
                if (red >= 100 && green < 90 && blue < 70) ++redCounts[cell];
                if (red < 220 && green >= 80 && blue >= 120) ++cyanCounts[cell];
            }
        }

        std::ostringstream signature;
        for (int cell = 0; cell < rows * columns; ++cell) {
            if (redCounts[cell] >= 5) signature << " R" << cell << ':' << redCounts[cell];
            if (cyanCounts[cell] >= 5) signature << " C" << cell << ':' << cyanCounts[cell];
        }
        if (signature.str() != previous) {
            std::cout << std::fixed << std::setprecision(5)
                      << (start + static_cast<double>(index) / fps)
                      << " frame=" << index << signature.str() << '\n';
            previous = signature.str();
        }
        ++index;
    }
    return input.eof() ? 0 : 4;
}
