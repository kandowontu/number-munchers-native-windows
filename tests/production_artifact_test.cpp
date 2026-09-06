#include "../src/resource_ids.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct ExpectedResource {
    int id;
    std::size_t size;
    std::uint64_t hash;
    const char* name;
};

// Keep the same stable byte fingerprint used by the other preservation gates.
std::uint64_t byteHash(const std::uint8_t* data, const std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
bool readStructure(const std::vector<std::uint8_t>& bytes, const std::size_t offset, T& value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}

bool isAllowedSystemImport(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    static const std::set<std::string> allowed = {
        "gdi32.dll", "kernel32.dll", "ole32.dll", "shell32.dll",
        "user32.dll", "winmm.dll",
    };
    return allowed.contains(name) ||
           (name.starts_with("api-ms-win-crt-") && name.ends_with(".dll"));
}

bool importsOnlyWindowsComponents(const std::filesystem::path& executable) {
    std::ifstream input(executable, std::ios::binary);
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad() || bytes.empty()) return false;

    IMAGE_DOS_HEADER dos{};
    if (!readStructure(bytes, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0) {
        std::cerr << "Production artifact is not a valid DOS/PE image\n";
        return false;
    }
    const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
    DWORD signature{};
    IMAGE_FILE_HEADER fileHeader{};
    if (!readStructure(bytes, ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
        !readStructure(bytes, ntOffset + sizeof(signature), fileHeader)) {
        std::cerr << "Production artifact has an invalid PE header\n";
        return false;
    }

    const std::size_t optionalOffset = ntOffset + sizeof(signature) + sizeof(fileHeader);
    WORD magic{};
    if (!readStructure(bytes, optionalOffset, magic)) return false;
    IMAGE_DATA_DIRECTORY imports{};
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_OPTIONAL_HEADER64 optional{};
        if (!readStructure(bytes, optionalOffset, optional)) return false;
        imports = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        IMAGE_OPTIONAL_HEADER32 optional{};
        if (!readStructure(bytes, optionalOffset, optional)) return false;
        imports = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    } else {
        std::cerr << "Production artifact uses an unknown PE optional header\n";
        return false;
    }

    const std::size_t sectionsOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> sections;
    sections.reserve(fileHeader.NumberOfSections);
    for (WORD index = 0; index < fileHeader.NumberOfSections; ++index) {
        IMAGE_SECTION_HEADER section{};
        if (!readStructure(bytes, sectionsOffset +
                                      static_cast<std::size_t>(index) * sizeof(section),
                           section)) {
            return false;
        }
        sections.push_back(section);
    }
    const auto fileOffsetForRva = [&sections, &bytes](const DWORD rva) -> std::size_t {
        for (const IMAGE_SECTION_HEADER& section : sections) {
            const std::uint64_t start = section.VirtualAddress;
            const std::uint64_t span =
                std::max(section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva >= start && static_cast<std::uint64_t>(rva) < start + span) {
                const std::uint64_t offset = section.PointerToRawData +
                    (static_cast<std::uint64_t>(rva) - start);
                return offset < bytes.size() ? static_cast<std::size_t>(offset) : bytes.size();
            }
        }
        return bytes.size();
    };

    std::size_t descriptorOffset = fileOffsetForRva(imports.VirtualAddress);
    if (imports.VirtualAddress == 0 || descriptorOffset == bytes.size()) {
        std::cerr << "Production artifact has no readable import directory\n";
        return false;
    }
    bool sawImport = false;
    for (;;) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!readStructure(bytes, descriptorOffset, descriptor)) return false;
        if (descriptor.Name == 0 && descriptor.FirstThunk == 0) break;
        const std::size_t nameOffset = fileOffsetForRva(descriptor.Name);
        if (nameOffset == bytes.size()) return false;
        const auto terminator = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(nameOffset),
                                          bytes.end(), static_cast<std::uint8_t>(0));
        if (terminator == bytes.end()) return false;
        const std::string name(reinterpret_cast<const char*>(bytes.data() + nameOffset),
                               static_cast<std::size_t>(terminator -
                                                        (bytes.begin() + nameOffset)));
        sawImport = true;
        if (!isAllowedSystemImport(name)) {
            std::cerr << "Production artifact imports non-system dependency: " << name << '\n';
            return false;
        }
        descriptorOffset += sizeof(descriptor);
    }
    return sawImport;
}

bool embeddedResourcesMatch(const std::filesystem::path& executable) {
    constexpr std::array<ExpectedResource, 31> expected{{
        {IDR_NM_RES, 28'181, 0x533f2fe372ed2f9aull, "NM.RES"},
        {IDR_NMCGA_RES, 266'147, 0x015c4cfc4cff1df3ull, "NMCGA.RES"},
        {IDR_NCGA_RES, 100'120, 0x347c51b2a4cc1f1eull, "NCGA.RES"},
        {IDR_MECC8X8_GFT, 2'646, 0xaf98002d185a9753ull, "MECC8X8.GFT"},
        {IDR_MECC5X8_GFT, 1'198, 0x0ac2400144b1a970ull, "MECC5X8.GFT"},
        {IDR_LOGO_256, 2'117, 0x73299f350a8d9c6cull, "LOGO.256"},
        {IDR_PRODUCT_PF, 350, 0xa44f1a711309de5cull, "Number PRODUCT.PF"},
        {IDR_NM_CFG, 2'188, 0x2165ab3c9a24fc22ull, "NM.CFG"},
        {IDR_LOGO_004, 1'562, 0xd42e6ba5e7b4f8bbull, "LOGO.004"},
        {IDR_WM_WLIST, 13'474, 0x3277179d8e2823deull, "WLIST.BIN"},
        {IDR_WM_RES, 30'890, 0xa9972305a2c873f2ull, "WM.RES"},
        {IDR_WMCGA_RES, 213'837, 0xf399756efb39d988ull, "Word MCGA.RES"},
        {IDR_WCGA_RES, 85'637, 0x08ffd37c1310a282ull, "Word CGA.RES"},
        {IDR_BIT8X8_GFT, 2'646, 0x474c60db8d02dbd8ull, "BIT8X8.GFT"},
        {IDR_BIT5X8_GFT, 1'158, 0xc67251db775223b3ull, "BIT5X8.GFT"},
        {IDR_WM_CFG, 636, 0x604e4b90156720cbull, "WM.CFG"},
        {IDR_WM_PRODUCT_PF, 350, 0xcca6a750823aeaadull, "Word PRODUCT.PF"},
        {IDR_SM_RES, 108'202, 0xe35ed62793692b76ull, "SM.RES"},
        {IDR_SMCGA_RES, 245'571, 0xd27d3f3dd3142cc1ull, "SMCGA.RES"},
        {IDR_SCGA_RES, 101'331, 0xe8c1985ab3fa2d79ull, "SCGA.RES"},
        {IDR_SM_BIT8X8_GFT, 2'654, 0xb5bd69b1bf2d7be9ull, "Super BIT8X8.GFT"},
        {IDR_SM_MIX5X8_GFT, 1'158, 0x342ba440d8d956a4ull, "MIX5X8.GFT"},
        {IDR_SM_LOGO_256, 2'112, 0x47a762e4780a1d67ull, "Super LOGO.256"},
        {IDR_SM_LOGO_004, 1'562, 0xd42e6ba5e7b4f8bbull, "Super LOGO.004"},
        {IDR_SM_CFG, 2'292, 0x1532acbb41b4c38cull, "SM.CFG"},
        {IDR_SM_PRODUCT_PF, 350, 0xa08fa4677dab782dull, "Super PRODUCT.PF"},
        {IDR_SFX_CORRECT_DRO, 372, 0xad1464732dae8633ull, "correct DRO"},
        {IDR_SFX_WRONG_DRO, 312, 0xb95161102b2675dfull, "wrong DRO"},
        {IDR_SFX_TROGGLE_COLLISION_DRO, 452, 0xffe4aceefdae7fb5ull,
         "collision DRO"},
        {IDR_SFX_BOARD_START_DRO, 258, 0xe4e9f7ea376dabb4ull, "board-start DRO"},
        {IDR_SFX_LEVEL_ADVANCE_DRO, 406, 0x1d4681b97586dfcbull,
         "level-advance DRO"},
    }};

    const HMODULE module = LoadLibraryExW(
        executable.c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        std::cerr << "Could not load isolated production artifact as resource data\n";
        return false;
    }
    bool matched = true;
    for (const ExpectedResource& item : expected) {
        const HRSRC resource =
            FindResourceW(module, MAKEINTRESOURCEW(item.id), RT_RCDATA);
        const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
        const DWORD size = resource ? SizeofResource(module, resource) : 0;
        const auto* bytes = loaded
            ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
        const std::uint64_t hash = bytes ? byteHash(bytes, size) : 0;
        if (!bytes || size != item.size || hash != item.hash) {
            std::cerr << item.name << " production resource mismatch: size=" << size
                      << " hash=0x" << std::hex << hash << std::dec << '\n';
            matched = false;
        }
    }
    FreeLibrary(module);
    return matched;
}

} // namespace

int wmain(const int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ProductionArtifactTests <production-exe>\n";
        return 2;
    }
    const std::filesystem::path source(argv[1]);
    if (!std::filesystem::is_regular_file(source)) {
        std::cerr << "Production executable does not exist\n";
        return 3;
    }

    const std::filesystem::path isolatedDirectory =
        std::filesystem::temp_directory_path() /
        (L"NumberMunchersArtifactTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    const std::filesystem::path isolatedExecutable =
        isolatedDirectory / L"Number Munchers.exe";
    std::error_code error;
    std::filesystem::create_directory(isolatedDirectory, error);
    if (error) {
        std::cerr << "Could not create isolated artifact directory\n";
        return 4;
    }
    std::filesystem::copy_file(source, isolatedExecutable,
                               std::filesystem::copy_options::none, error);
    if (error) {
        std::filesystem::remove(isolatedDirectory, error);
        std::cerr << "Could not copy production artifact into isolation\n";
        return 5;
    }

    // LoadLibraryEx uses data/image-resource flags only; no executable entry
    // point, GUI, audio device, or gameplay code is invoked by this test.
    const bool resourcesMatch = embeddedResourcesMatch(isolatedExecutable);
    const bool importsMatch = importsOnlyWindowsComponents(isolatedExecutable);

    std::filesystem::remove(isolatedExecutable, error);
    error.clear();
    std::filesystem::remove(isolatedDirectory, error);
    if (!resourcesMatch || !importsMatch) return 1;
    std::cout << "Production EXE is isolated, resource-complete, and system-DLL-only\n";
    return 0;
}
