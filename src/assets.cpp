#include "assets.h"

#include "resource_ids.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

} // namespace

BlobView loadEmbeddedResource(int resourceId) {
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) {
        return {};
    }
    const HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) {
        return {};
    }
    const auto* data = static_cast<const std::uint8_t*>(LockResource(loaded));
    return {data, static_cast<std::size_t>(SizeofResource(module, resource))};
}

ResArchive::ResArchive(BlobView blob) : blob_(blob) {
    if (blob.size < 12) {
        return;
    }
    const std::uint16_t dataOffset = read16(blob.data);
    const std::uint16_t typeCount = static_cast<std::uint16_t>(read16(blob.data + 2) + 1);
    if (dataOffset > blob.size || 4u + static_cast<std::size_t>(typeCount) * 8u > blob.size) {
        return;
    }
    for (std::uint16_t typeIndex = 0; typeIndex < typeCount; ++typeIndex) {
        const std::size_t descriptorOffset = 4u + static_cast<std::size_t>(typeIndex) * 8u;
        const auto* descriptor = blob.data + descriptorOffset;
        const std::uint16_t entryCount = static_cast<std::uint16_t>(read16(descriptor + 4) + 1);
        const std::uint16_t tableOffset = read16(descriptor + 6);
        if (static_cast<std::size_t>(tableOffset) + static_cast<std::size_t>(entryCount) * 16u > blob.size) {
            entries_.clear();
            return;
        }
        for (std::uint16_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
            const auto* record = blob.data + tableOffset + static_cast<std::size_t>(entryIndex) * 16u;
            Entry entry{};
            std::memcpy(entry.tag, descriptor, 4);
            entry.id = read32(record);
            entry.offset = read32(record + 4);
            entry.size = read32(record + 8);
            if (entry.offset < dataOffset || static_cast<std::size_t>(entry.offset) + entry.size > blob.size) {
                entries_.clear();
                return;
            }
            entries_.push_back(entry);
        }
    }
    valid_ = true;
}

BlobView ResArchive::find(std::string_view tag, std::uint32_t id) const {
    if (tag.size() != 4) {
        return {};
    }
    for (const Entry& entry : entries_) {
        if (entry.id == id && std::memcmp(entry.tag, tag.data(), 4) == 0) {
            return {blob_.data + entry.offset, entry.size};
        }
    }
    return {};
}

std::optional<Image> decodePcx(BlobView blob, const BlobView paletteIndexMap) {
    if (blob.size < 128 || blob.data[0] != 10 || blob.data[2] != 1) {
        return std::nullopt;
    }
    const int bits = blob.data[3];
    const int xMinimum = read16(blob.data + 4);
    const int yMinimum = read16(blob.data + 6);
    const int xMaximum = read16(blob.data + 8);
    const int yMaximum = read16(blob.data + 10);
    const int width = xMaximum - xMinimum + 1;
    const int height = yMaximum - yMinimum + 1;
    const int planes = blob.data[65];
    const int bytesPerLine = read16(blob.data + 66);
    if (width <= 0 || height <= 0 || planes <= 0 || bytesPerLine <= 0 ||
        width > 4096 || height > 4096) {
        return std::nullopt;
    }

    const std::size_t expected = static_cast<std::size_t>(bytesPerLine) * planes * height;
    std::vector<std::uint8_t> decoded;
    decoded.reserve(expected);
    std::size_t position = 128;
    while (decoded.size() < expected && position < blob.size) {
        std::uint8_t value = blob.data[position++];
        int count = 1;
        if ((value & 0xc0u) == 0xc0u) {
            count = value & 0x3fu;
            if (position >= blob.size) {
                return std::nullopt;
            }
            value = blob.data[position++];
        }
        const std::size_t remaining = expected - decoded.size();
        decoded.insert(decoded.end(), std::min<std::size_t>(count, remaining), value);
    }
    if (decoded.size() != expected) {
        return std::nullopt;
    }

    const bool hasVgaPalette = bits == 8 && planes == 1 && blob.size >= 769 &&
                               blob.data[blob.size - 769] == 12;
    const bool remapHeaderPalette = bits == 8 && planes == 1 && !hasVgaPalette &&
                                    paletteIndexMap.size == 256;
    if (remapHeaderPalette) {
        for (const std::uint8_t index : paletteIndexMap.span()) {
            if (index >= 16) return std::nullopt;
        }
    }

    std::uint32_t palette[256]{};
    if (bits == 2 && planes == 1) {
        // CGA mode 1 is the original high-intensity palette-1 display:
        // black, light cyan, light magenta, and white. The archived two-bit
        // PCX files retain their pixel indexes but use placeholder red-only
        // header entries, so interpreting those RGB triples would not
        // reproduce the colors that CGA hardware actually displayed.
        palette[0] = 0x000000;
        palette[1] = 0x55ffff;
        palette[2] = 0xff55ff;
        palette[3] = 0xffffff;
    } else if (hasVgaPalette) {
        const auto* colors = blob.data + blob.size - 768;
        for (int index = 0; index < 256; ++index) {
            palette[index] = (static_cast<std::uint32_t>(colors[index * 3]) << 16) |
                             (static_cast<std::uint32_t>(colors[index * 3 + 1]) << 8) |
                             static_cast<std::uint32_t>(colors[index * 3 + 2]);
        }
    } else {
        for (int index = 0; index < 16; ++index) {
            palette[index] = (static_cast<std::uint32_t>(blob.data[16 + index * 3]) << 16) |
                             (static_cast<std::uint32_t>(blob.data[17 + index * 3]) << 8) |
                             static_cast<std::uint32_t>(blob.data[18 + index * 3]);
        }
    }

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height);
    image.indices.resize(static_cast<std::size_t>(width) * height);
    image.sourceIndices.resize(static_cast<std::size_t>(width) * height);
    std::copy(std::begin(palette), std::end(palette), image.palette.begin());
    for (int y = 0; y < height; ++y) {
        const std::size_t rowBase = static_cast<std::size_t>(y) * bytesPerLine * planes;
        for (int x = 0; x < width; ++x) {
            int index = 0;
            if (bits == 8 && planes == 1) {
                index = decoded[rowBase + x];
            } else if ((bits == 2 || bits == 4) && planes == 1) {
                const int pixelsPerByte = 8 / bits;
                const int shift = 8 - bits * ((x % pixelsPerByte) + 1);
                index = (decoded[rowBase + x / pixelsPerByte] >> shift) & ((1 << bits) - 1);
            } else if (bits == 1 && planes <= 4) {
                for (int plane = 0; plane < planes; ++plane) {
                    const auto value = decoded[rowBase + static_cast<std::size_t>(plane) * bytesPerLine + x / 8];
                    index |= ((value >> (7 - x % 8)) & 1) << plane;
                }
            } else {
                return std::nullopt;
            }
            const std::size_t destination = static_cast<std::size_t>(y) * width + x;
            image.sourceIndices[destination] = static_cast<std::uint8_t>(index);
            if (remapHeaderPalette) index = paletteIndexMap.data[index];
            image.indices[destination] = static_cast<std::uint8_t>(index);
            image.pixels[destination] = palette[index];
        }
    }
    return image;
}

GemFont::GemFont(BlobView blob) : blob_(blob) {
    if (blob.size < 84) {
        return;
    }
    minimum_ = read16(blob.data + 36);
    maximum_ = read16(blob.data + 38);
    const std::size_t offsetTable = read32(blob.data + 72);
    bitmapOffset_ = read32(blob.data + 76);
    stride_ = read16(blob.data + 80);
    height_ = read16(blob.data + 82);
    const int glyphCount = maximum_ - minimum_ + 1;
    if (minimum_ < 0 || maximum_ > 255 || glyphCount <= 0 || stride_ <= 0 || height_ <= 0 ||
        offsetTable + static_cast<std::size_t>(glyphCount + 1) * 2 > blob.size ||
        bitmapOffset_ + static_cast<std::size_t>(stride_) * height_ > blob.size) {
        return;
    }
    offsets_.resize(static_cast<std::size_t>(glyphCount + 1));
    for (int index = 0; index <= glyphCount; ++index) {
        offsets_[index] = read16(blob.data + offsetTable + static_cast<std::size_t>(index) * 2);
    }
    valid_ = true;
}

int GemFont::glyphWidth(unsigned char character) const {
    if (!valid_ || character < minimum_ || character > maximum_) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(character - minimum_);
    return offsets_[index + 1] - offsets_[index];
}

bool GemFont::pixel(unsigned char character, int x, int y) const {
    if (!valid_ || character < minimum_ || character > maximum_ || y < 0 || y >= height_) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(character - minimum_);
    const int start = offsets_[index];
    const int width = offsets_[index + 1] - start;
    if (x < 0 || x >= width) {
        return false;
    }
    const int sourceX = start + x;
    const std::size_t byteOffset = bitmapOffset_ + static_cast<std::size_t>(y) * stride_ + sourceX / 8;
    return (blob_.data[byteOffset] & (0x80u >> (sourceX & 7))) != 0;
}

GameAssets::GameAssets(const GraphicsMode graphicsMode, const GameAssetSet assetSet)
    : graphicsMode_(graphicsMode),
      assetSet_(assetSet),
      graphicsArchive_(loadEmbeddedResource(
          assetSet == GameAssetSet::WordMunchers
              ? (graphicsMode == GraphicsMode::Cga4 ? IDR_WCGA_RES : IDR_WMCGA_RES)
              : assetSet == GameAssetSet::SuperMunchers
                  ? (graphicsMode == GraphicsMode::Cga4 ? IDR_SCGA_RES : IDR_SMCGA_RES)
                  : (graphicsMode == GraphicsMode::Cga4 ? IDR_NCGA_RES : IDR_NMCGA_RES))),
      gameArchive_(loadEmbeddedResource(
          assetSet == GameAssetSet::WordMunchers ? IDR_WM_RES
              : assetSet == GameAssetSet::SuperMunchers ? IDR_SM_RES : IDR_NM_RES)),
      largeFont_(loadEmbeddedResource(
          assetSet == GameAssetSet::WordMunchers ? IDR_BIT8X8_GFT
              : assetSet == GameAssetSet::SuperMunchers ? IDR_SM_BIT8X8_GFT
              : IDR_MECC8X8_GFT)),
      smallFont_(loadEmbeddedResource(
          assetSet == GameAssetSet::WordMunchers ? IDR_BIT5X8_GFT
              : assetSet == GameAssetSet::SuperMunchers ? IDR_SM_MIX5X8_GFT
              : IDR_MECC5X8_GFT)) {
    if (assetSet_ == GameAssetSet::SuperMunchers && graphicsMode_ == GraphicsMode::Vga256) {
        // Super's SMCGA PCX files intentionally omit the 256-color trailer.
        // EGAT:1 is the original executable's exact byte-to-header-palette map.
        paletteIndexMap_ = gameArchive_.find("EGAT", 1);
    }
}

std::uint32_t GameAssets::resolvedGraphicsId(const std::uint32_t id) const {
    if (graphicsMode_ != GraphicsMode::Cga4) return id;

    // The DOS source selects parallel odd/even VGA/CGA resource IDs. Keep
    // the native game state expressed in the VGA logical IDs and resolve the
    // device-specific archive entry only at the asset boundary.
    if (id >= 1006 && id <= 1011) return id - 6;
    if (id == 1013 || id == 1021) return id - 1;
    if (assetSet_ == GameAssetSet::WordMunchers) {
        if (id >= 2001 && id <= 2011 && (id & 1u) != 0) return id - 1;
        if (id >= 3003 && id <= 3005 && (id & 1u) != 0) return id - 1;
        if (id == 6001) return 6000;
    } else if (assetSet_ == GameAssetSet::SuperMunchers) {
        if (id >= 2023 && id <= 2031 && (id & 1u) != 0) return id - 1;
        if (id >= 3023 && id <= 3031 && (id & 1u) != 0) return id - 1;
    } else {
        if (id >= 2013 && id <= 2021 && (id & 1u) != 0) return id - 1;
        if (id >= 3013 && id <= 3021 && (id & 1u) != 0) return id - 1;
    }
    switch (id) {
    case 6003: return 6002;
    case 6005: return 6004;
    case 6009: return 6008;
    case 6011: return 6010;
    case 6051: return 6050;
    case 6053: return 6052;
    default: return id;
    }
}

const Image* GameAssets::image(std::uint32_t id) {
    id = resolvedGraphicsId(id);
    if (const auto found = images_.find(id); found != images_.end()) {
        return &found->second;
    }
    if (missingImages_[id]) {
        return nullptr;
    }
    const BlobView payload = graphicsArchive_.find("PCXF", id);
    if (const auto decoded = decodePcx(payload, paletteIndexMap_)) {
        const auto [iterator, inserted] = images_.emplace(id, std::move(*decoded));
        (void)inserted;
        return &iterator->second;
    }
    missingImages_[id] = true;
    return nullptr;
}

const Image* GameAssets::startupLogo() {
    constexpr std::uint32_t cacheId = 0xffff6001u;
    if (const auto found = images_.find(cacheId); found != images_.end()) {
        return &found->second;
    }
    const bool super = assetSet_ == GameAssetSet::SuperMunchers;
    const BlobView payload = loadEmbeddedResource(graphicsMode_ == GraphicsMode::Cga4
        ? (super ? IDR_SM_LOGO_004 : IDR_LOGO_004)
        : (super ? IDR_SM_LOGO_256 : IDR_LOGO_256));
    if (auto decoded = decodePcx(payload)) {
        if (graphicsMode_ == GraphicsMode::Vga256) {
            for (std::uint32_t& color : decoded->pixels) {
                if ((color & 0xffffffu) == 0xff0000u) color = 0xffffffu;
                else if ((color & 0xffffffu) == 0x04b400u) color = 0xb6ae00u;
            }
        }
        const auto [iterator, inserted] = images_.emplace(cacheId, std::move(*decoded));
        (void)inserted;
        return &iterator->second;
    }
    return nullptr;
}

std::optional<SpriteFrame> GameAssets::spriteFrame(std::uint32_t id, int frame) const {
    if (frame < 0) return std::nullopt;
    const std::uint32_t logicalId = id;
    id = resolvedGraphicsId(id);
    const BlobView table = graphicsArchive_.find("BTMP", id);
    constexpr std::size_t recordSize = 12;
    const std::size_t offset = static_cast<std::size_t>(frame) * recordSize;
    if (offset + recordSize > table.size) return std::nullopt;
    const auto* record = table.data + offset;
    // Cutscene BTMP tables can switch to a continuation PCXF sheet (for
    // example 2013 frame 22 lives in sheet 3013). Keep the referenced ID.
    const std::uint32_t sourceSheetId = read32(record);
    if (sourceSheetId == 0) return std::nullopt;
    const int top = read16(record + 4);
    const int left = read16(record + 6);
    const int bottom = read16(record + 8);
    const int right = read16(record + 10);
    if (assetSet_ == GameAssetSet::SuperMunchers && logicalId == 1013) {
        // Super's transformed-player tables retain several source-tool
        // overhang records whose lower edge was copied from the preceding
        // row. The DOS presenter supplies the actor row height separately;
        // treating these four words as an ordinary normalized rectangle drops
        // right-facing and chew poses completely. Preserve the recorded wide
        // horizontal extent (the renderer clips transparent sheet overhang)
        // while restoring that fixed source-row height.
        if (graphicsMode_ == GraphicsMode::Vga256) {
            if (frame == 3 || frame == 4) {
                return SpriteFrame{left, top, right - left + 1, 30, sourceSheetId};
            }
            if (frame == 5) {
                return SpriteFrame{left, top, right - left + 1, 30, sourceSheetId};
            }
            if (frame == 12 || frame == 14) {
                return SpriteFrame{left, top, right - left + 1, 30, sourceSheetId};
            }
            if (frame >= 16 && frame <= 18) {
                return SpriteFrame{left, top, 28, 27, sourceSheetId};
            }
        } else {
            if (frame == 3 || frame == 4) {
                return SpriteFrame{left, 97, right - left + 1, 30, sourceSheetId};
            }
            if (frame >= 16 && frame <= 18) {
                return SpriteFrame{left, top, 28, 26, sourceSheetId};
            }
        }
    }
    if (bottom < top || right < left) {
        // The final three Muncher records are the small life-counter poses.
        // Their table entries preserve only the source origin; the sheet uses
        // 40-pixel columns and each usable tile is 36x39.
        if (logicalId == 1006 && frame >= 16 && frame <= 18 && bottom == 0 && right == 0) {
            return SpriteFrame{left, top, 36, 39, sourceSheetId};
        }
        // Troggle tables store only the sheet origin and leave the final pair
        // zeroed. Their tile grid is 48x40 with a 45-pixel-wide sprite; the
        // three arrival/departure spheres use the full 40-pixel row and the
        // normal/special poses use 30 pixels.
        if (logicalId >= 1007 && logicalId <= 1011 && bottom == 0 && right == 0) {
            return SpriteFrame{left, top, 45, frame < 3 ? 40 : 30, sourceSheetId};
        }
        return std::nullopt;
    }
    return SpriteFrame{left, top, right - left + 1, bottom - top + 1, sourceSheetId};
}
