#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct BlobView {
    const std::uint8_t* data{};
    std::size_t size{};

    [[nodiscard]] std::span<const std::uint8_t> span() const { return {data, size}; }
    [[nodiscard]] explicit operator bool() const { return data != nullptr && size != 0; }
};

BlobView loadEmbeddedResource(int resourceId);

class ResArchive {
public:
    explicit ResArchive(BlobView blob = {});
    [[nodiscard]] BlobView find(std::string_view tag, std::uint32_t id) const;
    [[nodiscard]] bool valid() const { return valid_; }

private:
    struct Entry {
        char tag[4]{};
        std::uint32_t id{};
        std::uint32_t offset{};
        std::uint32_t size{};
    };

    BlobView blob_{};
    std::vector<Entry> entries_;
    bool valid_{};
};

struct Image {
    int width{};
    int height{};
    std::vector<std::uint32_t> pixels;
    // Preserve the source indexes as well as their decoded RGB values.  Most
    // pages use the PCX's own palette, but DOS sprites and cartoons are often
    // blitted after a different full-page PCX has installed the active VGA
    // DAC.  Retaining indexes lets those paths reproduce that palette state
    // without reverse-mapping duplicate RGB entries.
    std::vector<std::uint8_t> indices;
    // Some eight-bit PCX files omit a trailer and are decoded through a
    // resource-supplied compatibility map for their default RGB view, while
    // the DOS presenter later reuses the original byte indexes with a
    // resident 256-entry DAC.  Retain both representations.
    std::vector<std::uint8_t> sourceIndices;
    std::array<std::uint32_t, 256> palette{};

    [[nodiscard]] explicit operator bool() const {
        return width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width * height);
    }
};

struct SpriteFrame {
    int x{};
    int y{};
    int width{};
    int height{};
    std::uint32_t sheetId{};
};

enum class GraphicsMode {
    Vga256,
    Cga4,
};

enum class GameAssetSet {
    NumberMunchers,
    WordMunchers,
    SuperMunchers,
};

std::optional<Image> decodePcx(BlobView blob, BlobView paletteIndexMap = {});

class GemFont {
public:
    explicit GemFont(BlobView blob = {});

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] int glyphWidth(unsigned char character) const;
    [[nodiscard]] bool pixel(unsigned char character, int x, int y) const;

private:
    BlobView blob_{};
    int minimum_{};
    int maximum_{};
    int stride_{};
    int height_{};
    std::size_t bitmapOffset_{};
    std::vector<std::uint16_t> offsets_;
    bool valid_{};
};

class GameAssets {
public:
    explicit GameAssets(GraphicsMode graphicsMode = GraphicsMode::Vga256,
                        GameAssetSet assetSet = GameAssetSet::NumberMunchers);

    [[nodiscard]] const Image* image(std::uint32_t id);
    [[nodiscard]] const Image* startupLogo();
    [[nodiscard]] std::optional<SpriteFrame> spriteFrame(std::uint32_t id, int frame) const;
    [[nodiscard]] GraphicsMode graphicsMode() const { return graphicsMode_; }
    [[nodiscard]] GameAssetSet assetSet() const { return assetSet_; }
    [[nodiscard]] const GemFont& largeFont() const { return largeFont_; }
    [[nodiscard]] const GemFont& smallFont() const { return smallFont_; }
    [[nodiscard]] const ResArchive& gameArchive() const { return gameArchive_; }

private:
    [[nodiscard]] std::uint32_t resolvedGraphicsId(std::uint32_t id) const;

    GraphicsMode graphicsMode_{GraphicsMode::Vga256};
    GameAssetSet assetSet_{GameAssetSet::NumberMunchers};
    ResArchive graphicsArchive_;
    ResArchive gameArchive_;
    GemFont largeFont_;
    GemFont smallFont_;
    BlobView paletteIndexMap_;
    std::map<std::uint32_t, Image> images_;
    std::map<std::uint32_t, bool> missingImages_;
};
