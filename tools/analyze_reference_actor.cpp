#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: analyze_reference_actor RAW_RGB START_SECONDS FPS\n";
        return 2;
    }

    constexpr int width = 144;
    constexpr int height = 75;
    constexpr std::size_t frameBytes = static_cast<std::size_t>(width * height * 3);
    const double start = std::stod(argv[2]);
    const double fps = std::stod(argv[3]);
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "could not open " << argv[1] << '\n';
        return 3;
    }

    std::vector<std::uint8_t> frame(frameBytes);
    std::uint64_t index = 0;
    int previousCell = -2;
    int previousBucketX = -1;
    int previousBucketY = -1;
    int previousCountBucket = -1;
    while (input.read(reinterpret_cast<char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size()))) {
        std::uint64_t sumX = 0;
        std::uint64_t sumY = 0;
        int count = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>((y * width + x) * 3);
                const int red = frame[pixel];
                const int green = frame[pixel + 1];
                const int blue = frame[pixel + 2];
                if (red >= 100 && green >= 90 && blue <= 20) {
                    sumX += static_cast<std::uint64_t>(x);
                    sumY += static_cast<std::uint64_t>(y);
                    ++count;
                }
            }
        }

        int cell = -1;
        int bucketX = -1;
        int bucketY = -1;
        if (count > 0) {
            const double centerX = static_cast<double>(sumX) / count;
            const double centerY = static_cast<double>(sumY) / count;
            const int column = std::clamp(static_cast<int>(centerX / 24.0), 0, 5);
            const int row = std::clamp(static_cast<int>(centerY / 15.0), 0, 4);
            cell = row * 6 + column;
            bucketX = static_cast<int>(std::lround(centerX * 2.0));
            bucketY = static_cast<int>(std::lround(centerY * 2.0));
        }
        const int countBucket = count / 4;
        if (cell != previousCell || bucketX != previousBucketX || bucketY != previousBucketY ||
            countBucket != previousCountBucket) {
            std::cout << std::fixed << std::setprecision(5)
                      << (start + static_cast<double>(index) / fps)
                      << " frame=" << index << " cell=" << cell
                      << " xy2=" << bucketX << ',' << bucketY
                      << " yellow=" << count << '\n';
            previousCell = cell;
            previousBucketX = bucketX;
            previousBucketY = bucketY;
            previousCountBucket = countBucket;
        }
        ++index;
    }
    return input.eof() ? 0 : 4;
}
