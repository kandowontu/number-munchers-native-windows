#include "../src/resource_ids.h"
#include "../src/mecc_sound.h"
#include "../src/scene_script.h"
#include "../src/word_config.h"
#include "../src/word_content.h"
#include "../src/word_game.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

std::uint64_t fnv1a(const BlobView blob) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < blob.size; ++index) {
        hash ^= blob.data[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

void hashByte(std::uint64_t& hash, const std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
}

void hash32(std::uint64_t& hash, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

bool embeddedWordResourcesMatch() {
    struct ExpectedResource {
        int id;
        std::size_t size;
        std::uint64_t fnv;
        const char* name;
    };
    constexpr std::array<ExpectedResource, 9> expected{{
        {IDR_WM_WLIST, 13'474, 0x3277179d8e2823deull, "WLIST.BIN"},
        {IDR_WM_RES, 30'890, 0xa9972305a2c873f2ull, "WM.RES"},
        {IDR_WMCGA_RES, 213'837, 0xf399756efb39d988ull, "MCGA.RES"},
        {IDR_WCGA_RES, 85'637, 0x08ffd37c1310a282ull, "CGA.RES"},
        {IDR_BIT8X8_GFT, 2'646, 0x474c60db8d02dbd8ull, "BIT8X8.GFT"},
        {IDR_BIT5X8_GFT, 1'158, 0xc67251db775223b3ull, "BIT5X8.GFT"},
        {IDR_WM_CFG, 636, 0x604e4b90156720cbull, "WM.CFG"},
        {IDR_WM_PRODUCT_PF, 350, 0xcca6a750823aeaadull, "PRODUCT.PF"},
        {IDR_LOGO_256, 2'117, 0x73299f350a8d9c6cull, "LOGO.256"},
    }};
    bool matched = true;
    for (const ExpectedResource& item : expected) {
        const BlobView blob = loadEmbeddedResource(item.id);
        const std::uint64_t actual = fnv1a(blob);
        if (blob.size != item.size || actual != item.fnv) {
            std::cerr << item.name << " embedded size=" << blob.size
                      << " fnv=0x" << std::hex << actual << std::dec << '\n';
            matched = false;
        }
    }
    return matched;
}

bool wordAssetModeMatches(const GraphicsMode mode, const std::uint64_t expectedHash) {
    struct Sheet {
        std::uint32_t logicalId;
        int vgaWidth;
        int vgaHeight;
        int cgaWidth;
        int cgaHeight;
    };
    constexpr std::array<Sheet, 20> sheets{{
        {1006, 211, 179, 213, 182}, {1007, 209, 187, 210, 186},
        {1008, 207, 186, 202, 185}, {1009, 211, 187, 208, 185},
        {1010, 160, 187, 160, 185}, {1011, 210, 189, 204, 186},
        {2001, 320, 200, 320, 200}, {2003, 320, 200, 320, 200},
        {2005, 320, 200, 320, 200}, {2007, 320, 200, 320, 200},
        {2009, 320, 200, 320, 200}, {2011, 320, 200, 320, 200},
        {3003, 320, 200, 320, 200}, {3005, 320, 177, 320, 178},
        {6001, 213, 163, 205, 162}, {6003, 320, 200, 320, 200},
        {6005, 320, 200, 320, 200}, {6009, 320, 200, 320, 200},
        {6051, 320, 200, 320, 200}, {6053, 320, 200, 320, 200},
    }};
    constexpr std::array<std::pair<std::uint32_t, int>, 18> animations{{
        {1006, 19}, {1007, 16}, {1008, 16}, {1009, 16}, {1010, 16}, {1011, 16},
        {2001, 15}, {2003, 17}, {2005, 22}, {2007, 11}, {2009, 15}, {2011, 21},
        {6001, 20}, {6003, 1}, {6005, 1}, {6009, 1}, {6051, 1}, {6053, 1},
    }};

    GameAssets assets(mode, GameAssetSet::WordMunchers);
    if (assets.assetSet() != GameAssetSet::WordMunchers ||
        assets.graphicsMode() != mode || !assets.gameArchive().valid() ||
        !assets.largeFont().valid() || !assets.smallFont().valid()) {
        std::cerr << "Word asset archive/font initialization failed\n";
        return false;
    }

    std::uint64_t hash = 1469598103934665603ull;
    int decodedSheets = 0;
    for (const Sheet& expected : sheets) {
        const Image* image = assets.image(expected.logicalId);
        const int width = mode == GraphicsMode::Vga256 ? expected.vgaWidth : expected.cgaWidth;
        const int height = mode == GraphicsMode::Vga256 ? expected.vgaHeight : expected.cgaHeight;
        if (!image || image->width != width || image->height != height) {
            std::cerr << "Word sheet " << expected.logicalId << " decoded as "
                      << (image ? std::to_string(image->width) + "x" +
                                      std::to_string(image->height) : "missing") << '\n';
            return false;
        }
        ++decodedSheets;
        hash32(hash, expected.logicalId);
        hash32(hash, static_cast<std::uint32_t>(image->width));
        hash32(hash, static_cast<std::uint32_t>(image->height));
        for (const std::uint32_t pixel : image->pixels) {
            hash32(hash, pixel);
            if (mode == GraphicsMode::Cga4 && pixel != 0x000000u && pixel != 0x55ffffu &&
                pixel != 0xff55ffu && pixel != 0xffffffu) {
                std::cerr << "Word CGA sheet contains a non-hardware-palette color\n";
                return false;
            }
        }
    }

    int decodedFrames = 0;
    for (const auto& [logicalId, frameCount] : animations) {
        hash32(hash, logicalId);
        hash32(hash, static_cast<std::uint32_t>(frameCount));
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const std::optional<SpriteFrame> frame = assets.spriteFrame(logicalId, frameIndex);
            if (!frame || frame->x < 0 || frame->y < 0 || frame->width <= 0 ||
                frame->height <= 0) {
                std::cerr << "Word animation " << logicalId << " frame " << frameIndex
                          << " did not resolve\n";
                return false;
            }
            const Image* source = assets.image(frame->sheetId);
            // The original archives deliberately store the two 605x
            // interstitial records as 0,0,319,199 in the otherwise YXYX
            // table, yielding a transposed 200x320 descriptor over a
            // 320x200 sheet. Preserve that source record; these pages are
            // selected as whole PCXF images rather than sprite crops.
            const bool transposedInterstitial =
                (logicalId == 6051 || logicalId == 6053) && frameIndex == 0 &&
                frame->x == 0 && frame->y == 0 && frame->width == 200 &&
                frame->height == 320 && source && source->width == 320 &&
                source->height == 200;
            // CGA's 6000 table also retains two source rectangles whose
            // right edge exceeds the archived 205-pixel PCX by one/four
            // pixels. This is source data, not a native normalization.
            const bool archivedCgaOverflow =
                mode == GraphicsMode::Cga4 && logicalId == 6001 && source &&
                source->width == 205 && source->height == 162 &&
                ((frameIndex == 12 && frame->x == 159 && frame->width == 47) ||
                 (frameIndex == 16 && frame->x == 162 && frame->width == 47));
            if (!source || (!transposedInterstitial && !archivedCgaOverflow &&
                            (frame->x + frame->width > source->width ||
                             frame->y + frame->height > source->height))) {
                std::cerr << "Word animation " << logicalId << " frame " << frameIndex
                          << " exceeds source sheet " << frame->sheetId << '\n';
                return false;
            }
            ++decodedFrames;
            hash32(hash, static_cast<std::uint32_t>(frame->x));
            hash32(hash, static_cast<std::uint32_t>(frame->y));
            hash32(hash, static_cast<std::uint32_t>(frame->width));
            hash32(hash, static_cast<std::uint32_t>(frame->height));
            hash32(hash, frame->sheetId);
        }
        if (assets.spriteFrame(logicalId, frameCount)) {
            std::cerr << "Word animation " << logicalId << " accepted an extra frame\n";
            return false;
        }
    }

    const Image* logo = assets.startupLogo();
    if (!logo) {
        std::cerr << "Word startup logo did not decode\n";
        return false;
    }
    hash32(hash, static_cast<std::uint32_t>(logo->width));
    hash32(hash, static_cast<std::uint32_t>(logo->height));
    for (const std::uint32_t pixel : logo->pixels) hash32(hash, pixel);

    if (decodedSheets != 20 || decodedFrames != 225 || hash != expectedHash) {
        std::cerr << (mode == GraphicsMode::Vga256 ? "VGA" : "CGA")
                  << " Word asset snapshot sheets=" << decodedSheets
                  << " frames=" << decodedFrames << " hash=0x" << std::hex << hash
                  << std::dec << '\n';
        return false;
    }
    return true;
}

bool wordAssetsMatch() {
    return embeddedWordResourcesMatch() &&
           wordAssetModeMatches(GraphicsMode::Vga256, 0x0586883564bda4a5ull) &&
           wordAssetModeMatches(GraphicsMode::Cga4, 0x473a005451f3cb0cull);
}

bool wordScenesMatch() {
    struct SceneFixture {
        std::uint32_t scriptId;
        std::uint32_t graphicId;
        std::uint32_t audioBankId;
        std::vector<std::uint16_t> threads;
        std::size_t scriptSize;
        std::size_t adliSize;
        std::size_t psndSize;
        int frameCount;
        int expectedEventOccurrences;
        int expectedUniqueEvents;
        int expectedTerminalTick;
        int expectedBakedCount;
        std::uint64_t expectedTimelineHash;
        std::uint64_t expectedAudioHash;
    };
    // Selector 0x114dd reads the sound bank byte from DS:228c and dispatches
    // to 0x116f0..0x1184e. Each setup loads the SCPT below, selects the odd
    // VGA logical graphics ID, and passes one recovered DS thread-order table.
    const std::array fixtures = {
        SceneFixture{2000, 2001, 1, {6, 7, 5, 8, 4, 0, 1, 2, 3},
                     406, 1307, 1166, 15, 2, 2, 102, 5,
                     0xb7149f23389c711cull, 0x44f5089577b226b0ull},
        SceneFixture{2002, 2003, 2, {3, 4, 0, 1, 2},
                     416, 1458, 1100, 17, 9, 9, 142, 3,
                     0xcb193a534abc16b0ull, 0x4c3d256761fa07beull},
        SceneFixture{2004, 2005, 3, {6, 4, 3, 5, 1, 0, 2},
                     518, 1291, 1068, 22, 12, 6, 133, 5,
                     0x5b3c131e43ff1cc1ull, 0x1eab910f11ccc1ffull},
        SceneFixture{2006, 2007, 4, {0, 3, 4, 1, 2},
                     332, 1164, 1082, 11, 7, 3, 106, 2,
                     0xad060b02d054e261ull, 0x949ba26a364e3224ull},
        SceneFixture{2008, 2009, 5, {0, 1, 2, 5, 3, 4, 6},
                     478, 1570, 1168, 15, 4, 4, 110, 5,
                     0x3fe87bce9f8c8960ull, 0x064bf84f87655de8ull},
        SceneFixture{2010, 2011, 6, {5, 2, 3, 0, 1, 4},
                     1328, 2099, 1168, 21, 12, 4, 251, 7,
                     0x9bc9f6d0f4e0645bull, 0xbf431e7cb260f5f7ull},
    };
    static constexpr std::array<std::uint32_t, 6> ExpectedScoreDurations = {
        11'792, 16'048, 15'622, 12'039, 14'043, 28'032,
    };
    static constexpr std::array<std::size_t, 6> ExpectedScoreWriteCounts = {
        544, 488, 287, 183, 587, 1'240,
    };
    static constexpr std::array<std::uint64_t, 6> ExpectedScoreHashes = {
        0x72e0ab09089fa021ull, 0xd6c5c96bc730d4dcull,
        0xa5560bea60d78119ull, 0x3a804757eac7d440ull,
        0x233041229c488dd2ull, 0x4a3f72d537a0df01ull,
    };
    std::array<std::uint32_t, 6> scoreDurations{};
    std::array<std::size_t, 6> scoreWriteCounts{};
    std::array<std::uint64_t, 6> scoreHashes{};

    GameAssets vga(GraphicsMode::Vga256, GameAssetSet::WordMunchers);
    GameAssets cga(GraphicsMode::Cga4, GameAssetSet::WordMunchers);
    const ResArchive& archive = vga.gameArchive();
    bool matched = true;
    for (std::size_t fixtureIndex = 0; fixtureIndex < fixtures.size(); ++fixtureIndex) {
        const SceneFixture& fixture = fixtures[fixtureIndex];
        const BlobView script = archive.find("SCPT", fixture.scriptId);
        const BlobView adli = archive.find("ADLI", fixture.audioBankId);
        const BlobView psnd = archive.find("PSND", fixture.audioBankId);
        if (script.size != fixture.scriptSize || adli.size != fixture.adliSize ||
            psnd.size != fixture.psndSize) {
            std::cerr << "Word scene " << fixture.scriptId
                      << " resource mapping/size regressed\n";
            matched = false;
            continue;
        }

        const MeccSound score = decodeMeccGSound(
            adli, 38, MeccSoundProfile::WordMunchers);
        if (!score.valid || score.durationMilliseconds == 0 ||
            score.writes.size() < 2 || score.writes[0].reg != 0xbd ||
            score.writes[0].value != 0x80 || score.writes[1].reg != 0xbd ||
            score.writes[1].value != 0xc0 || !score.speakerWrites.empty()) {
            std::cerr << "Word scene bank " << fixture.audioBankId
                      << " stream 38 did not decode as its AdLib conductor\n";
            matched = false;
        } else {
            scoreDurations[fixtureIndex] = score.durationMilliseconds;
            scoreWriteCounts[fixtureIndex] = score.writes.size();
            std::uint64_t scoreHash = 1469598103934665603ull;
            hash32(scoreHash, score.durationMilliseconds);
            hash32(scoreHash, static_cast<std::uint32_t>(score.writes.size()));
            for (const OplWrite& write : score.writes) {
                hash32(scoreHash, write.milliseconds);
                hashByte(scoreHash, write.reg);
                hashByte(scoreHash, write.value);
            }
            scoreHashes[fixtureIndex] = scoreHash;
        }

        const std::uint16_t scriptThreadCount = static_cast<std::uint16_t>(script.data[0]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(script.data[1]) << 8);
        std::vector<std::uint16_t> sortedThreads = fixture.threads;
        std::sort(sortedThreads.begin(), sortedThreads.end());
        bool completePermutation = sortedThreads.size() == scriptThreadCount;
        for (std::size_t index = 0; index < sortedThreads.size(); ++index) {
            completePermutation = completePermutation && sortedThreads[index] == index;
        }
        if (!completePermutation) {
            std::cerr << "Word scene " << fixture.scriptId
                      << " thread order is not the recovered full permutation\n";
            matched = false;
            continue;
        }

        SceneScript scene;
        if (!scene.load(script, fixture.threads)) {
            std::cerr << "Word SCPT " << fixture.scriptId << " failed structural load\n";
            matched = false;
            continue;
        }

        int ticks = 0;
        int eventOccurrences = 0;
        std::set<std::uint16_t> uniqueEvents;
        std::set<int> visibleFrames;
        std::uint64_t timelineHash = 1469598103934665603ull;
        std::uint64_t audioHash = 1469598103934665603ull;
        const auto hashSound = [](std::uint64_t& hash, const MeccSound& sound) {
            hash32(hash, sound.durationMilliseconds);
            hash32(hash, static_cast<std::uint32_t>(sound.writes.size()));
            for (const OplWrite& write : sound.writes) {
                hash32(hash, write.milliseconds);
                hashByte(hash, write.reg);
                hashByte(hash, write.value);
            }
            hash32(hash, static_cast<std::uint32_t>(sound.speakerWrites.size()));
            for (const MeccSound::SpeakerWrite& write : sound.speakerWrites) {
                hash32(hash, write.milliseconds);
                hash32(hash, write.frequencyHz);
            }
        };
        while (scene.valid() && !scene.finished() && ticks < 20000) {
            scene.tick();
            ++ticks;
            hash32(timelineHash, static_cast<std::uint32_t>(ticks));
            hash32(timelineHash, scene.signal());
            hash32(timelineHash, static_cast<std::uint32_t>(scene.actors().size()));
            hash32(timelineHash, static_cast<std::uint32_t>(scene.bakedActors().size()));
            hash32(timelineHash, static_cast<std::uint32_t>(scene.callbackEvents().size()));

            for (const std::uint16_t event : scene.callbackEvents()) {
                ++eventOccurrences;
                hash32(timelineHash, event);
                const std::uint16_t stream = event + 3;
                const bool firstOccurrence = uniqueEvents.insert(event).second;
                if (stream > 0xff) {
                    std::cerr << "Word scene " << fixture.scriptId << " event " << event
                              << " exceeds the biased stream table\n";
                    matched = false;
                } else if (firstOccurrence) {
                    const MeccSound decodedAdli = decodeMeccGSound(
                        adli, static_cast<std::uint8_t>(stream),
                        MeccSoundProfile::WordMunchers);
                    const MeccSound decodedPsnd = decodeMeccGSound(
                        psnd, static_cast<std::uint8_t>(stream),
                        MeccSoundProfile::WordMunchers);
                    if (!decodedAdli.valid || !decodedPsnd.valid) {
                        std::cerr << "Word scene " << fixture.scriptId << " event " << event
                                  << " did not decode as ADLI/PSND stream " << stream << '\n';
                        matched = false;
                    } else {
                        hash32(audioHash, event);
                        hashSound(audioHash, decodedAdli);
                        hashSound(audioHash, decodedPsnd);
                    }
                }
            }

            const auto validateActor = [&](const SceneActor& actor, const bool baked) {
                if (actor.visible && !actor.ended) visibleFrames.insert(actor.frame);
                hash32(timelineHash, static_cast<std::uint32_t>(actor.thread));
                hash32(timelineHash, static_cast<std::uint32_t>(actor.frame));
                hash32(timelineHash, static_cast<std::uint32_t>(actor.x));
                hash32(timelineHash, static_cast<std::uint32_t>(actor.y));
                hash32(timelineHash, static_cast<std::uint32_t>(actor.layer));
                hashByte(timelineHash, actor.visible ? 1 : 0);
                hashByte(timelineHash, actor.ended ? 1 : 0);
                if (actor.frame < 0 || actor.frame >= fixture.frameCount ||
                    !vga.spriteFrame(fixture.graphicId, actor.frame) ||
                    !cga.spriteFrame(fixture.graphicId, actor.frame) ||
                    (baked && (!actor.visible || actor.ended))) {
                    std::cerr << "Word scene " << fixture.scriptId
                              << " produced invalid " << (baked ? "baked " : "")
                              << "actor frame/state\n";
                    matched = false;
                }
            };
            for (const SceneActor& actor : scene.actors()) validateActor(actor, false);
            for (const SceneActor& actor : scene.bakedActors()) validateActor(actor, true);
        }

        const bool terminalMatches = scene.valid() && scene.finished() &&
            scene.signal() == 99 && ticks < 20000;
        bool continuationFramesMatch = true;
        if (fixture.graphicId == 2003) {
            for (const int frame : {13, 14, 15, 16}) {
                continuationFramesMatch = continuationFramesMatch && visibleFrames.contains(frame);
            }
        } else if (fixture.graphicId == 2005) {
            for (const int frame : {20, 21}) {
                continuationFramesMatch = continuationFramesMatch && visibleFrames.contains(frame);
            }
        }
        const bool snapshotMatches =
            eventOccurrences == fixture.expectedEventOccurrences &&
            static_cast<int>(uniqueEvents.size()) == fixture.expectedUniqueEvents &&
            ticks == fixture.expectedTerminalTick &&
            static_cast<int>(scene.bakedActors().size()) == fixture.expectedBakedCount &&
            timelineHash == fixture.expectedTimelineHash &&
            audioHash == fixture.expectedAudioHash && continuationFramesMatch;
        if (!terminalMatches || !snapshotMatches) {
            std::cerr << "Word SCPT " << fixture.scriptId
                      << " snapshot ticks=" << ticks
                      << " events=" << eventOccurrences
                      << " unique=" << uniqueEvents.size()
                      << " baked=" << scene.bakedActors().size()
                      << " signal=" << scene.signal()
                      << " timeline=0x" << std::hex << timelineHash
                      << " audio=0x" << audioHash << std::dec << " callbacks={";
            bool first = true;
            for (const std::uint16_t event : uniqueEvents) {
                if (!first) std::cerr << ',';
                std::cerr << event;
                first = false;
            }
            std::cerr << "}\n";
            matched = false;
        }

        // Stream 15 in bank 5 is the scene path that first exposes Word's
        // executable-specific patch key 0x1b. Exercise both complete native
        // renderers so a decoder-only success cannot mask an unusable wave.
        if (fixture.audioBankId == 5) {
            const auto adlibWave = renderMeccGSoundToWave(
                adli, 15, 50, MeccSoundProfile::WordMunchers);
            const auto speakerWave = renderMeccSpeakerSoundToWave(
                psnd, 15, 50, MeccSoundProfile::WordMunchers);
            if (adlibWave.size() <= 44 || speakerWave.size() <= 44) {
                std::cerr << "Word scene bank 5 stream 15 did not render to audio\n";
                matched = false;
            }
        }
    }
    if (scoreDurations != ExpectedScoreDurations ||
        scoreWriteCounts != ExpectedScoreWriteCounts ||
        scoreHashes != ExpectedScoreHashes) {
        std::cerr << "Word cartoon scores durations=";
        for (const auto value : scoreDurations) std::cerr << ' ' << value;
        std::cerr << " writes=";
        for (const auto value : scoreWriteCounts) std::cerr << ' ' << value;
        std::cerr << " hashes=" << std::hex;
        for (const auto value : scoreHashes) std::cerr << " 0x" << value;
        std::cerr << std::dec << '\n';
        matched = false;
    }
    return matched;
}

bool structureMatches(const WordList& words) {
    constexpr std::array<int, 21> counts{
        181, 181, 171, 167, 153, 181, 175, 119, 24, 181, 132,
        26, 79, 77, 30, 68, 34, 32, 104, 82, 6};
    constexpr std::array<const char*, 21> exemplars{
        "cake", "hat", "tree", "bell", "kite", "fish", "boat", "fox",
        "mule", "duck", "moon", "book", "mouse", "haunt", "oil", "car",
        "chair", "deer", "bird", "corn", "mule"};
    if (!words.valid() || words.sections().size() != counts.size()) return false;

    int total = 0;
    int metadataRecords = 0;
    std::array<int, 256> metadataHistogram{};
    for (std::size_t sectionIndex = 0; sectionIndex < counts.size(); ++sectionIndex) {
        const WordSection& section = words.sections()[sectionIndex];
        if (section.records.size() != static_cast<std::size_t>(counts[sectionIndex]) ||
            section.records.front().text() != exemplars[sectionIndex] ||
            section.difficultyCounts.back() + 1u != section.records.size() ||
            !std::is_sorted(section.difficultyCounts.begin(), section.difficultyCounts.end())) {
            return false;
        }
        total += static_cast<int>(section.records.size());
        for (const WordRecord& record : section.records) {
            const auto nul = std::find(record.bytes.begin(), record.bytes.end(), std::uint8_t{});
            if (record.text().size() !=
                static_cast<std::size_t>(nul - record.bytes.begin())) return false;
            bool hasMetadata = false;
            for (auto value = nul + 1; value != record.bytes.end(); ++value) {
                if (*value != 0) {
                    ++metadataHistogram[*value];
                    hasMetadata = true;
                }
            }
            if (hasMetadata) ++metadataRecords;
        }
    }
    return total == 2203 && metadataRecords == 54 && metadataHistogram[0x20] == 4 &&
           metadataHistogram[0x53] == 9 && metadataHistogram[0x54] == 49 &&
           metadataHistogram[0x70] == 1;
}

bool wordConfigMatches() {
    const BlobView blob = loadEmbeddedResource(IDR_WM_CFG);
    const WordConfig config(blob);
    if (!config.valid() || !config.usable() || config.version() != 1 ||
        config.applicationName() != "Word Munchers" || config.difficulty() != 0 ||
        config.joystickFlags() != 0 || config.joystickEnabled() ||
        !config.password().empty() || !config.hint().empty() || config.savedState() != 0 ||
        config.hallCount() != 7 || config.hallCapacity() != 10 ||
        config.hallNameLimit() != 25) {
        return false;
    }

    std::array<std::uint8_t, WordConfig::SoundCount> expectedSounds{};
    expectedSounds[2] = 1;
    expectedSounds[3] = 1;
    if (config.selectedSounds() != expectedSounds) return false;

    const WordJoystickCalibration& joystick = config.joystickCalibration();
    if (joystick.verticalLow != 0x81b6 || joystick.verticalHigh != 0x809b ||
        joystick.horizontalHigh != 0x8098 || joystick.horizontalLow != 0x81b3) {
        return false;
    }

    constexpr std::array<std::uint32_t, 10> scores{
        14'660, 2'440, 950, 835, 515, 230, 50, 175, 55, 55};
    constexpr std::array<const char*, 10> names{
        "The Unknown Muncher", "jennine oct.1", "jennine oct.1",
        "The Unknown Muncher", "jennine", "The Unknown Muncher",
        "The Unknown Muncher", "The masked muncher", "L,HIGH", "L,HIGH"};
    for (std::size_t index = 0; index < scores.size(); ++index) {
        if (config.hallEntries()[index].score != scores[index] ||
            config.hallEntries()[index].name() != names[index]) return false;
    }
    const std::vector<WordHallEntry> active = config.activeHallEntries();
    if (active.size() != 7 || active.back().score != 50 ||
        active.back().name() != "The Unknown Muncher") return false;

    // Empty Borland ShortStrings retain inactive payload bytes. They must not
    // become a password/hint and must survive a lossless native round trip.
    if (config.serialized()[0x3d] != 0 || config.serialized()[0x3e] != '2' ||
        config.serialized()[0x13d] != 0 || config.serialized()[0x13e] != '2' ||
        !std::equal(config.serialized().begin(), config.serialized().end(), blob.data)) {
        return false;
    }

    // The supplied lengths are both zero, so use a synthetic nonempty image
    // to distinguish the executable's +0x03D hint and +0x13D password fields.
    auto populatedStrings = config.serialized();
    populatedStrings[0x3d] = 50;
    std::fill_n(populatedStrings.begin() + 0x3e, 50, static_cast<std::uint8_t>('H'));
    populatedStrings[0x13d] = 10;
    std::fill_n(populatedStrings.begin() + 0x13e, 10, static_cast<std::uint8_t>('P'));
    const WordConfig populated({populatedStrings.data(), populatedStrings.size()});
    if (!populated.usable() || populated.hint() != std::string(50, 'H') ||
        populated.password() != std::string(10, 'P')) return false;

    const auto rejected = [&config](const std::size_t offset, const std::uint8_t value,
                                    const bool expectFormat, const bool expectContent,
                                    const bool expectHall) {
        auto bytes = config.serialized();
        bytes[offset] = value;
        const WordConfig changed({bytes.data(), bytes.size()});
        return changed.valid() == expectFormat &&
               changed.contentValid() == expectContent && changed.hallValid() == expectHall;
    };
    if (!rejected(0, 'X', false, false, false) ||
        !rejected(4, 2, false, false, false) ||
        !rejected(0x13d, 11, false, false, false) ||
        !rejected(0x26, 8, true, false, true) ||
        !rejected(0x28, 3, true, false, true) ||
        !rejected(0x14a, 11, true, true, false) ||
        !rejected(0x14c, 9, true, true, false) ||
        !rejected(0x14e, 24, true, true, false)) {
        return false;
    }
    auto unterminatedApplication = config.serialized();
    std::fill_n(unterminatedApplication.begin() + 6, 32, static_cast<std::uint8_t>('A'));
    if (WordConfig({unterminatedApplication.data(), unterminatedApplication.size()}).valid() ||
        WordConfig({blob.data, blob.size - 1}).valid() || WordConfig({}).valid()) {
        return false;
    }
    auto unterminatedHallName = config.serialized();
    std::fill_n(unterminatedHallName.begin() + 0x150, 26, static_cast<std::uint8_t>('A'));
    const WordConfig unsafeHall({unterminatedHallName.data(), unterminatedHallName.size()});
    return unsafeHall.valid() && unsafeHall.contentValid() && !unsafeHall.hallValid();
}

bool tuplesMatch(const WordList& words) {
    {
        OriginalRandom random(1);
        WordBoardGenerator generator(words, random);
        std::array<bool, 20> selected{};
        selected[0] = selected[1] = true;
        if (!generator.configure(selected, 0) || random.calls != 0 ||
            generator.tuples() != std::vector<WordSoundTuple>{{0, 1, -1}, {1, 0, -1}}) {
            return false;
        }
    }
    {
        OriginalRandom random(1);
        WordBoardGenerator generator(words, random);
        std::array<bool, 20> selected{};
        selected[12] = selected[13] = selected[14] = true;
        if (!generator.configure(selected, 7) ||
            generator.tuples() != std::vector<WordSoundTuple>{
                {12, 13, 14}, {13, 14, 12}, {14, 12, 13}}) {
            return false;
        }
    }
    {
        OriginalRandom random(0x574f5244u);
        WordBoardGenerator generator(words, random);
        std::array<bool, 20> selected{};
        selected.fill(true);
        const std::vector<WordSoundTuple> expected = {
            {0,1,-1}, {1,0,-1}, {2,3,-1}, {3,2,-1}, {4,5,-1}, {5,4,-1},
            {6,7,-1}, {7,6,-1}, {9,8,-1}, {10,11,-1}, {11,10,-1},
            {12,13,14}, {13,14,12}, {14,12,13},
            {15,17,18}, {16,19,17}, {17,19,18}, {18,17,16}, {19,17,18},
        };
        if (!generator.configure(selected, 0) || generator.tuples() != expected ||
            random.calls != 13 || random.state != 0xea11e0d9u) return false;
    }
    {
        OriginalRandom random(0x574f5244u);
        WordBoardGenerator generator(words, random);
        std::array<bool, 20> selected{};
        selected.fill(true);
        if (!generator.configure(selected, 2) || generator.tuples().size() != 20 ||
            std::none_of(generator.tuples().begin(), generator.tuples().end(),
                         [](const WordSoundTuple& tuple) { return tuple.target == 8; })) return false;
    }
    {
        OriginalRandom random(1);
        WordBoardGenerator generator(words, random);
        std::array<bool, 20> selected{};
        selected[0] = true;
        if (generator.configure(selected, 0)) return false;
        selected[12] = true;
        if (generator.configure(selected, 0)) return false;
    }
    return true;
}

bool fallbackAndBoardRulesMatch(const WordList& words) {
    OriginalRandom random(0x4d554c45u);
    WordBoardGenerator generator(words, random);
    std::array<bool, 20> selected{};
    selected[0] = selected[1] = selected[9] = true;
    if (!generator.configure(selected, 0)) return false;

    const std::set<std::string> fallback{"you", "use", "huge", "cute", "mule"};
    bool sawShortUTarget = false;
    for (int boardIndex = 0; boardIndex < 12; ++boardIndex) {
        const WordBoard board = generator.nextBoard(false);
        if (board.targetSound < 0 || board.correctCount < 2 || board.targetPoolCount < 1) return false;
        for (const WordBoardCell& cell : board.cells) {
            if (cell.correct != (cell.signedSourceIndex > 0) || cell.signedSourceIndex == 0 ||
                cell.record.text().empty()) return false;
        }
        if (board.targetSound == 9) {
            sawShortUTarget = true;
            for (const WordBoardCell& cell : board.cells) {
                if (!cell.correct && !fallback.contains(cell.record.text())) return false;
            }
        }
    }
    return sawShortUTarget;
}

bool deterministicBoardMatches(const WordList& words) {
    OriginalRandom random(0x1234u);
    WordBoardGenerator generator(words, random);
    std::array<bool, 20> selected{};
    selected[0] = selected[1] = true;
    if (!generator.configure(selected, 0)) return false;
    const WordBoard board = generator.nextBoard(false);

    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&hash](const std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    append(static_cast<std::uint8_t>(board.targetSound));
    append(static_cast<std::uint8_t>(board.tupleIndex));
    append(static_cast<std::uint8_t>(board.correctCount));
    append(static_cast<std::uint8_t>(board.targetPoolCount));
    for (const WordBoardCell& cell : board.cells) {
        for (const std::uint8_t value : cell.record.bytes) append(value);
        append(cell.correct ? 1 : 0);
        const std::uint16_t source = static_cast<std::uint16_t>(cell.signedSourceIndex);
        append(static_cast<std::uint8_t>(source));
        append(static_cast<std::uint8_t>(source >> 8));
    }
    if (hash != 0xf13ec2ff889dad0full || random.calls != 244) {
        std::cerr << "Word board snapshot hash=0x" << std::hex << hash << std::dec
                  << " calls=" << random.calls << '\n';
        return false;
    }
    return true;
}

bool wordGameCoreMatches(const WordList& words) {
    constexpr std::array<std::string_view, 20> expectedLabels = {
        "/a/ as in cake", "/a/ as in hat", "/e/ as in tree", "/e/ as in bell",
        "/i/ as in kite", "/i/ as in fish", "/o/ as in boat", "/o/ as in fox",
        "/u/ as in mule", "/u/ as in duck", "/oo/ as in moon", "/oo/ as in book",
        "/ou/ as in mouse", "/au/ as in haunt", "/oi/ as in oil", "/ar/ as in car",
        "/air/ as in chair", "/eer/ as in deer", "/ir/ as in bird", "/or/ as in corn",
    };
    if (wordSoundLabels() != expectedLabels) return false;

    OriginalRandom configuredRandom(0x574d4346u);
    WordGameCore configuredCore(words, configuredRandom);
    const WordConfig suppliedConfig(loadEmbeddedResource(IDR_WM_CFG));
    if (!configuredCore.configure(suppliedConfig) || !configuredCore.start() ||
        configuredCore.board().targetSound < 2 || configuredCore.board().targetSound > 3 ||
        configuredCore.correctRemaining() < 2) return false;

    WordHall hall;
    if (!hall.load(suppliedConfig) || hall.entries().size() != 7 ||
        hall.entries().front() != WordHallScoreEntry{"The Unknown Muncher", 14'660} ||
        hall.entries().back() != WordHallScoreEntry{"The Unknown Muncher", 50} ||
        hall.insertionRank(49) != -1 || hall.insertionRank(14'660) != 1 ||
        !hall.insert(14'660, "stable tie") || hall.entries()[1].name != "stable tie" ||
        !hall.insert(1'000'001, std::string(40, 'P')) ||
        hall.entries().front().score != 1'000'000 || hall.entries().front().name.size() != 25 ||
        !hall.insert(50, "") || hall.entries().size() != 10 ||
        hall.entries().back().name != "The Unknown Muncher" ||
        hall.insertionRank(50) != -1 || hall.insert(50, "floor tie") ||
        hall.insertionRank(51) < 0 || !hall.insert(51, "strictly higher") ||
        hall.entries().size() != 10 || hall.entries().back().score != 50) {
        return false;
    }
    auto invalidConfigBytes = suppliedConfig.serialized();
    invalidConfigBytes[0x26] = 8;
    const WordConfig invalidConfig({invalidConfigBytes.data(), invalidConfigBytes.size()});
    if (configuredCore.configure(invalidConfig) || configuredCore.active()) return false;
    invalidConfigBytes = suppliedConfig.serialized();
    invalidConfigBytes[0x14c] = 9;
    const WordConfig invalidHallConfig({invalidConfigBytes.data(), invalidConfigBytes.size()});
    if (hall.load(invalidHallConfig) || !hall.entries().empty()) return false;

    constexpr std::array<int, 16> expectedPoints = {
        5, 5, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70,
    };
    for (int level = 1; level <= static_cast<int>(expectedPoints.size()); ++level) {
        if (MuncherScore::pointsForLevel(level) !=
            expectedPoints[static_cast<std::size_t>(level - 1)]) return false;
    }
    if (MuncherScore::pointsForLevel(17) != 75 ||
        MuncherScore::pointsForLevel(99) != 75) return false;

    MuncherScore score;
    score.restore(995, 2);
    MuncherScoreAward award = score.awardPoints(5);
    if (!award.bonusMuncher || award.maximumReached || score.score() != 1'000 ||
        score.reserves() != 3) return false;
    score.restore(9'995, 3);
    award = score.awardPoints(5);
    if (!award.bonusMuncher || score.score() != 10'000 || score.reserves() != 4 ||
        score.visibleReserves() != 3) return false;
    score.restore(999'950, 7);
    award = score.awardPoints(75);
    if (award.bonusMuncher || !award.maximumReached ||
        score.score() != MuncherScore::MaximumScore || score.reserves() != 7) return false;
    score.reset();
    if (score.loseMuncher() || score.loseMuncher() || score.loseMuncher() ||
        !score.loseMuncher() || score.reserves() != -1 || score.visibleReserves() != 0) {
        return false;
    }

    OriginalRandom random(0x1234u);
    WordGameCore core(words, random);
    std::array<bool, 20> selected{};
    selected[0] = selected[1] = true;
    if (!core.configure(selected, 0) || !core.start() || !core.active() ||
        core.level() != 1 || core.pressureTier() != 0 ||
        core.correctRemaining() != core.board().correctCount || random.calls != 244 ||
        core.scoreState().score() != 0 || core.scoreState().reserves() != 3) {
        return false;
    }

    const auto correct = std::find_if(core.board().cells.begin(), core.board().cells.end(),
                                      [](const WordBoardCell& cell) { return cell.correct; });
    const auto wrong = std::find_if(core.board().cells.begin(), core.board().cells.end(),
                                    [](const WordBoardCell& cell) { return !cell.correct; });
    if (correct == core.board().cells.end() || wrong == core.board().cells.end()) return false;
    const std::size_t correctIndex = static_cast<std::size_t>(correct - core.board().cells.begin());
    const std::size_t wrongIndex = static_cast<std::size_t>(wrong - core.board().cells.begin());
    const WordBoardCell wrongCell = *wrong;
    const int initialCorrect = core.correctRemaining();

    // Image 0x0960f rejects a zero signed-source board record even when its
    // auxiliary eaten flag is clear.  This also protects synthetic/restored
    // core state from turning a blank into a wrong answer.
    if (!core.restoreCell(wrongIndex, {}, false) || core.beginMunchCell(wrongIndex) ||
        !core.restoreCell(wrongIndex, wrongCell, false)) return false;

    // The DOS initializer clears the live board word immediately but keeps
    // the remaining-answer count intact until the seventh chew callback.
    if (!core.beginMunchCell(correctIndex) || !core.eaten(correctIndex) ||
        core.correctRemaining() != initialCorrect ||
        core.beginMunchCell(wrongIndex)) return false;
    const WordMunchResolution correctResult = core.resolveMunchCell(correctIndex);
    if (correctResult.kind != WordMunchKind::Correct || correctResult.points != 5 ||
        correctResult.bonusMuncher || correctResult.maximumScore ||
        core.scoreState().score() != 5 || core.correctRemaining() != initialCorrect - 1 ||
        !core.eaten(correctIndex) ||
        core.munchCell(correctIndex).kind != WordMunchKind::Invalid) return false;

    const WordMunchResolution wrongResult = core.munchCell(wrongIndex);
    if (wrongResult.kind != WordMunchKind::Wrong || wrongResult.gameOver ||
        core.scoreState().reserves() != 2 || !core.eaten(wrongIndex)) return false;

    std::vector<std::size_t> remainingCorrect;
    for (std::size_t index = 0; index < core.board().cells.size(); ++index) {
        if (core.board().cells[index].correct && !core.eaten(index)) {
            remainingCorrect.push_back(index);
        }
    }
    if (remainingCorrect.empty()) return false;
    for (std::size_t index = 0; index + 1 < remainingCorrect.size(); ++index) {
        if (core.munchCell(remainingCorrect[index]).kind != WordMunchKind::Correct) return false;
    }
    const std::uint64_t callsBeforeLevelOneCompletion = random.calls;
    const WordMunchResolution levelOneCompletion = core.munchCell(remainingCorrect.back());
    if (!levelOneCompletion.boardComplete || levelOneCompletion.cartoonScene != -1 ||
        random.calls != callsBeforeLevelOneCompletion + 5 || core.cartoonPending() ||
        !core.boardComplete() || core.correctRemaining() != 0 ||
        core.scoreState().score() != initialCorrect * 5 ||
        !core.advanceBoard() || core.level() != 2 || core.pressureTier() != 1 ||
        core.boardComplete() || core.correctRemaining() < 2 || !core.active()) {
        return false;
    }

    // Complete levels 2 through 18. A zero cursor causes five full-range
    // swaps on every completion (even levels without cartoons); divisible-by-
    // three levels block board advance until teardown moves the cursor. Level
    // 18 selects special scene 5 but still consumes the ordinary cursor slot.
    for (int completedLevel = 2; completedLevel <= 18; ++completedLevel) {
        std::vector<std::size_t> correctCells;
        for (std::size_t index = 0; index < core.board().cells.size(); ++index) {
            if (core.board().cells[index].correct) correctCells.push_back(index);
        }
        if (correctCells.empty()) return false;
        for (std::size_t index = 0; index + 1 < correctCells.size(); ++index) {
            if (core.munchCell(correctCells[index]).kind != WordMunchKind::Correct) return false;
        }
        const int cursorBefore = core.cartoonOrderIndex();
        const std::uint64_t callsBefore = random.calls;
        const WordMunchResolution completion = core.munchCell(correctCells.back());
        const bool cartoonDue = completedLevel % 3 == 0;
        if (!completion.boardComplete ||
            random.calls != callsBefore + (cursorBefore == 0 ? 5u : 0u) ||
            core.cartoonPending() != cartoonDue) return false;
        if (cartoonDue) {
            const int expectedScene = completedLevel == 18
                ? 5
                : core.cartoonOrder()[static_cast<std::size_t>(cursorBefore)];
            if (completion.cartoonScene != expectedScene ||
                core.pendingCartoonScene() != expectedScene || core.advanceBoard() ||
                !core.finishCartoon() ||
                core.cartoonOrderIndex() != (cursorBefore + 1) % 5 ||
                core.finishCartoon()) return false;
        } else if (completion.cartoonScene != -1) {
            return false;
        }
        if (!core.advanceBoard() || core.level() != completedLevel + 1 ||
            core.pressureTier() != std::min(11, completedLevel)) return false;
    }

    // The score routine at 0x08a47 calls the scored terminal from 0x08aac
    // before the chew caller reaches its later board-empty/cartoon checks.
    // Make the million-point award also be level 18's final correct word: the
    // terminal must win without the special cartoon or its five shuffle draws.
    OriginalRandom maximumRandom(0x4d4158u);
    WordGameCore maximumCore(words, maximumRandom);
    if (!maximumCore.configure(selected, 0) ||
        !maximumCore.startAtLevel(18, false)) return false;
    std::vector<std::size_t> maximumCorrect;
    for (std::size_t index = 0; index < maximumCore.board().cells.size(); ++index) {
        if (maximumCore.board().cells[index].correct) maximumCorrect.push_back(index);
    }
    if (maximumCorrect.empty()) return false;
    for (std::size_t index = 0; index + 1 < maximumCorrect.size(); ++index) {
        if (maximumCore.munchCell(maximumCorrect[index]).kind != WordMunchKind::Correct) {
            return false;
        }
    }
    auto& maximumScore = const_cast<MuncherScore&>(maximumCore.scoreState());
    maximumScore.restore(
        MuncherScore::MaximumScore - MuncherScore::pointsForLevel(18), 7);
    const std::uint64_t callsBeforeMaximum = maximumRandom.calls;
    const WordMunchResolution maximumResult =
        maximumCore.munchCell(maximumCorrect.back());
    if (maximumResult.kind != WordMunchKind::Correct || maximumResult.points != 75 ||
        maximumResult.bonusMuncher || !maximumResult.maximumScore ||
        maximumResult.boardComplete || maximumResult.cartoonScene != -1 ||
        maximumCore.scoreState().score() != MuncherScore::MaximumScore ||
        maximumCore.scoreState().reserves() != 7 || maximumCore.correctRemaining() != 0 ||
        !maximumCore.maximumScoreReached() || maximumCore.active() ||
        maximumCore.boardComplete() || maximumCore.cartoonPending() ||
        maximumCore.pendingCartoonScene() != -1 ||
        maximumRandom.calls != callsBeforeMaximum || maximumCore.advanceBoard()) {
        return false;
    }

    // Demo's board-empty branch at 0x0ad76 bypasses the only call to the
    // cartoon selector (main-screen action 0x120f9 -> 0x0fd74).  Completing
    // the unattended board therefore spends no five-draw scene shuffle.
    OriginalRandom demoRandom(0x4445u);
    WordGameCore demoCore(words, demoRandom);
    if (!demoCore.configure(selected, 0) || !demoCore.startAtLevel(3, true)) return false;
    std::vector<std::size_t> demoCorrect;
    for (std::size_t index = 0; index < demoCore.board().cells.size(); ++index) {
        if (demoCore.board().cells[index].correct) demoCorrect.push_back(index);
    }
    if (demoCorrect.size() < 2) return false;
    for (std::size_t index = 0; index + 1 < demoCorrect.size(); ++index) {
        if (demoCore.munchCell(demoCorrect[index]).kind != WordMunchKind::Correct) return false;
    }
    const std::uint64_t callsBeforeDemoCompletion = demoRandom.calls;
    const WordMunchResolution demoCompletion = demoCore.munchCell(demoCorrect.back());
    if (!demoCompletion.boardComplete || demoCompletion.cartoonScene != -1 ||
        demoCore.cartoonPending() || demoRandom.calls != callsBeforeDemoCompletion) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    const BlobView blob = loadEmbeddedResource(IDR_WM_WLIST);
    if (!blob || blob.size != 13474 || fnv1a(blob) != 0x3277179d8e2823deull) {
        std::cerr << "Embedded WLIST.BIN provenance mismatch\n";
        return 1;
    }

    const WordList words(blob);
    if (!wordConfigMatches()) {
        std::cerr << "Recovered Word Munchers configuration layout regressed\n";
        return 1;
    }
    if (!structureMatches(words)) {
        std::cerr << "Word-list structure or lossless record metadata regressed\n";
        return 1;
    }
    if (WordList({blob.data, blob.size - 1}).valid() || WordList({}).valid()) {
        std::cerr << "Malformed word-list input was accepted\n";
        return 1;
    }
    if (!tuplesMatch(words)) {
        std::cerr << "Recovered selected-sound tuple construction regressed\n";
        return 1;
    }
    if (!fallbackAndBoardRulesMatch(words)) {
        std::cerr << "Recovered Word Munchers board/fallback rules regressed\n";
        return 1;
    }
    if (!deterministicBoardMatches(words)) {
        std::cerr << "Recovered Word Munchers deterministic board snapshot regressed\n";
        return 1;
    }
    if (!wordGameCoreMatches(words)) {
        std::cerr << "Recovered shared scoring/Word game core regressed\n";
        return 1;
    }
    if (!wordAssetsMatch()) {
        std::cerr << "Embedded Word Munchers asset bridge regressed\n";
        return 1;
    }
    if (!wordScenesMatch()) {
        std::cerr << "Recovered Word Munchers scripted scenes regressed\n";
        return 1;
    }

    std::cout << "Word Munchers static content, configuration, asset, scene, and core tests passed\n";
    return 0;
}
