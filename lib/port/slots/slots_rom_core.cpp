#include "slots_rom_core.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace SlotsRom {
namespace {

constexpr uint32_t kOverlayBase = 0x021E4E40;
constexpr uint32_t kOverlayId = 77;
constexpr uint32_t kOddsTable = 0x021EA284; // 4 types x 3 bets x 7 u16 weights

uint32_t R32(const std::vector<uint8_t> &d, size_t o) {
    return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24;
}

uint16_t R16(const std::vector<uint8_t> &d, size_t o) {
    return d[o] | d[o + 1] << 8;
}

struct RomFile {
    std::string path;
    uint32_t start, end;
};

bool ReadAt(std::ifstream &rom, uint32_t offset, std::vector<uint8_t> &out, uint32_t size) {
    out.resize(size);
    rom.clear();
    rom.seekg(offset);
    rom.read((char *)out.data(), size);
    return (bool)rom;
}

// The file name table's folders and the allocation table's ranges
bool ListFiles(std::ifstream &rom, const std::vector<uint8_t> &header, std::vector<RomFile> &files, std::vector<uint8_t> &fat) {
    const uint32_t fntOffset = R32(header, 0x40), fntSize = R32(header, 0x44);
    const uint32_t fatOffset = R32(header, 0x48), fatSize = R32(header, 0x4C);
    if (fntSize < 8 || fntSize > (1 << 20) || fatSize > (1 << 20)) {
        return false;
    }
    std::vector<uint8_t> fnt;
    if (!ReadAt(rom, fntOffset, fnt, fntSize) || !ReadAt(rom, fatOffset, fat, fatSize)) {
        return false;
    }
    std::vector<std::pair<uint16_t, std::string>> folders = { { 0xF000, "" } };
    while (!folders.empty()) {
        auto [id, prefix] = folders.back();
        folders.pop_back();
        const size_t entry = (id & 0xFFF) * 8;
        if (entry + 8 > fnt.size()) {
            return false;
        }
        size_t at = R32(fnt, entry);
        uint16_t fileId = R16(fnt, entry + 4);
        while (at < fnt.size() && fnt[at] != 0) {
            const uint8_t length = fnt[at] & 0x7F;
            const bool folder = (fnt[at] & 0x80) != 0;
            if (at + 1 + length + (folder ? 2 : 0) > fnt.size()) {
                return false;
            }
            const std::string name((const char *)&fnt[at + 1], length);
            at += 1 + length;
            if (folder) {
                folders.push_back({ R16(fnt, at), prefix + name + "/" });
                at += 2;
            } else {
                if ((size_t)fileId * 8 + 8 > fat.size()) {
                    return false;
                }
                files.push_back({ prefix + name, R32(fat, fileId * 8), R32(fat, fileId * 8 + 4) });
                fileId++;
            }
        }
    }
    return true;
}

// The DS's backward LZ (BLZ), used for overlays
bool DecompressBLZ(const std::vector<uint8_t> &in, std::vector<uint8_t> &out) {
    if (in.size() < 8) {
        return false;
    }
    const uint32_t hdr = R32(in, in.size() - 8);
    const uint32_t encLen = hdr & 0xFFFFFF, hdrLen = hdr >> 24, incLen = R32(in, in.size() - 4);
    if (hdrLen < 8 || hdrLen > encLen || encLen > in.size() || incLen > (1 << 24)) {
        return false;
    }
    const size_t outSize = in.size() + incLen;
    out.assign(outSize, 0);
    std::copy(in.begin(), in.end(), out.begin());
    size_t src = in.size() - hdrLen, dst = outSize;
    const size_t end = in.size() - encLen;
    while (src > end) {
        uint8_t flags = in[--src];
        for (int i = 0; i < 8 && src > end; i++, flags <<= 1) {
            if (flags & 0x80) {
                if (src < end + 2) {
                    return false;
                }
                const uint8_t b1 = in[--src], b2 = in[--src];
                const int len = (b1 >> 4) + 3;
                const size_t disp = (((b1 & 0xF) << 8) | b2) + 3;
                for (int k = 0; k < len; k++) {
                    if (dst == 0 || dst + disp - 1 >= outSize) {
                        return false;
                    }
                    out[dst - 1] = out[dst - 1 + disp];
                    dst--;
                }
            } else {
                if (dst == 0) {
                    return false;
                }
                out[--dst] = in[--src];
            }
        }
    }
    return true;
}

bool WriteFile(const fs::path &path, const uint8_t *data, size_t size) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out.write((const char *)data, size);
    return (bool)out;
}

bool OddsRowsSumTo1000(const std::vector<uint8_t> &ov) {
    const size_t at = kOddsTable - kOverlayBase;
    if (ov.size() < at + 4 * 42) {
        return false;
    }
    for (int row = 0; row < 12; row++) {
        int sum = 0;
        for (int i = 0; i < 7; i++) {
            sum += R16(ov, at + row * 14 + i * 2);
        }
        if (sum != 1000) {
            return false;
        }
    }
    return true;
}

} // namespace

bool CheckOverlay(const std::string &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> ov((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ov.empty() || !OddsRowsSumTo1000(ov)) {
        error = "The slot machine's tables aren't what the code expects";
        return false;
    }
    return true;
}

bool Extract(const std::string &romPath, const std::string &outDir, std::string &game, std::string &error,
    const std::function<void(const std::string &)> &progress) {
    std::ifstream rom(romPath, std::ios::binary);
    std::vector<uint8_t> header;
    if (!rom || !ReadAt(rom, 0, header, 0x200)) {
        error = "That file can't be read.";
        return false;
    }
    const std::string code((const char *)&header[0xC], 4);
    if (code == "IPKJ") {
        game = "HeartGold";
    } else if (code == "IPGJ") {
        game = "SoulSilver";
    } else {
        error = "That isn't Pokemon HeartGold or SoulSilver (Japan) (game code " + code + "). The Game Corner's slot "
            "machine isn't in the other versions' files.";
        return false;
    }

    progress("Reading the ROM's file system...");
    std::vector<RomFile> files;
    std::vector<uint8_t> fat;
    if (!ListFiles(rom, header, files, fat)) {
        error = "Its file system can't be read";
        return false;
    }

    const fs::path tmp = fs::path(outDir + ".tmp");
    std::error_code ec;
    fs::remove_all(tmp, ec);

    // The graphics
    for (const char *name : { "a/1/2/7", "a/1/5/5" }) {
        const RomFile *file = nullptr;
        for (const RomFile &candidate : files) {
            if (candidate.path == name) {
                file = &candidate;
            }
        }
        std::vector<uint8_t> data;
        if (file == nullptr || file->end <= file->start || !ReadAt(rom, file->start, data, file->end - file->start)
            || !WriteFile(tmp / name, data.data(), data.size())) {
            error = std::string("Couldn't extract ") + name + ".";
            fs::remove_all(tmp, ec);
            return false;
        }
        progress(std::string("Extracted ") + name);
    }

    // Overlay 77: the ARM9 overlay table's entry (32 bytes each), its file, decompressed
    const uint32_t tableOffset = R32(header, 0x50), tableSize = R32(header, 0x54);
    std::vector<uint8_t> table, packed, overlay;
    bool ok = tableSize >= (kOverlayId + 1) * 32 && ReadAt(rom, tableOffset, table, tableSize);
    if (ok) {
        const size_t e = kOverlayId * 32;
        const uint32_t ramAddress = R32(table, e + 4), fileId = R32(table, e + 24), flags = R32(table, e + 28);
        ok = ramAddress == kOverlayBase && (size_t)fileId * 8 + 8 <= fat.size();
        if (ok) {
            const uint32_t start = R32(fat, fileId * 8), end = R32(fat, fileId * 8 + 4);
            ok = end > start && ReadAt(rom, start, packed, end - start);
            if (ok) {
                overlay = packed;
                if (flags & 0x01000000) {
                    ok = DecompressBLZ(packed, overlay);
                }
            }
        }
    }
    if (!ok || !WriteFile(tmp / "ov77.bin", overlay.data(), overlay.size())) {
        error = "Couldn't read the slot machine's code from that ROM.";
        fs::remove_all(tmp, ec);
        return false;
    }
    if (!OddsRowsSumTo1000(overlay)) {
        error = "That ROM's slot machine isn't the one this port reads (its odds don't add up).";
        fs::remove_all(tmp, ec);
        return false;
    }
    progress("Extracted the slot machine's code");

    std::ofstream info(tmp / "rom.txt");
    info << "game=" << game << "\n";
    info.close();
    fs::remove_all(outDir, ec);
    fs::rename(tmp, outDir, ec);
    if (ec) {
        error = "Couldn't move the files into place: " + ec.message();
        fs::remove_all(tmp, ec);
        return false;
    }
    return true;
}

} // namespace SlotsRom
