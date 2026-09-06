#pragma once

#include "assets.h"

#include <windows.h>

#include <cstdint>
#include <string_view>
#include <vector>

class Renderer {
public:
    static constexpr int Width = 320;
    static constexpr int Height = 200;

    explicit Renderer(GraphicsMode graphicsMode = GraphicsMode::Vga256);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void setGraphicsMode(GraphicsMode graphicsMode) { graphicsMode_ = graphicsMode; }
    [[nodiscard]] GraphicsMode graphicsMode() const { return graphicsMode_; }

    void clear(std::uint32_t color);
    void pixel(int x, int y, std::uint32_t color);
    void fillRect(int x, int y, int width, int height, std::uint32_t color);
    void outlineRect(int x, int y, int width, int height, std::uint32_t color);
    void horizontalLine(int x1, int x2, int y, std::uint32_t color);
    void verticalLine(int x, int y1, int y2, std::uint32_t color);

    void drawImage(const Image& image, int x, int y, bool transparentBlack = false,
                   bool remapTitlePalette = false, bool remapHallPalette = false);
    void drawImageRegion(const Image& image,
                         int sourceX,
                         int sourceY,
                         int sourceWidth,
                         int sourceHeight,
                         int destinationX,
                         int destinationY,
                         int destinationWidth,
                         int destinationHeight,
                         bool transparentBlack = false,
                         bool remapMuncherPalette = false,
                         bool remapTitlePalette = false,
                         bool remapHallPalette = false,
                         bool remapGameplayTrogglePalette = false,
                         const Image* indexedPaletteSource = nullptr,
                         bool remapNumberScenePalette = false,
                         int numberScenePaletteSheet = 0,
                         int transparentPaletteIndex = -1,
                         int numberScenePaletteFrame = -1,
                         bool initialNumberScenePaint = false);

    [[nodiscard]] int textWidth(const GemFont& font, std::string_view text, int scale = 1) const;
    void drawText(const GemFont& font,
                  int x,
                  int y,
                  std::string_view text,
                  std::uint32_t color,
                  int scale = 1);
    void drawCenteredText(const GemFont& font,
                          int centerX,
                          int y,
                          std::string_view text,
                          std::uint32_t color,
                          int scale = 1);

    void paint(HDC deviceContext, const RECT& clientRectangle) const;
    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const { return pixels_; }
    void replacePixels(const std::vector<std::uint32_t>& pixels);

private:
    [[nodiscard]] std::uint32_t mappedPrimitiveColor(std::uint32_t color) const;
    bool ensureBackBuffer(HDC deviceContext, int width, int height) const;

    GraphicsMode graphicsMode_{GraphicsMode::Vga256};
    std::vector<std::uint32_t> pixels_;
    mutable HDC backBufferContext_{};
    mutable HBITMAP backBufferBitmap_{};
    mutable HGDIOBJ originalBackBufferBitmap_{};
    mutable int backBufferWidth_{};
    mutable int backBufferHeight_{};
};

namespace Colors {
constexpr std::uint32_t Black = 0x000000;
constexpr std::uint32_t White = 0xffffff;
constexpr std::uint32_t BoardBlue = 0x000079;
constexpr std::uint32_t BrightBlue = 0x0000ff;
constexpr std::uint32_t Magenta = 0xff55ff;
constexpr std::uint32_t Yellow = 0xffff00;
constexpr std::uint32_t Cyan = 0x55ffff;
constexpr std::uint32_t Green = 0x00ff00;
constexpr std::uint32_t Red = 0xff3030;
constexpr std::uint32_t Gray = 0x808080;
} // namespace Colors
