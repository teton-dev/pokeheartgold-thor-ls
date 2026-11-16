// ROM hacks of HeartGold and SoulSilver (Drayano's Sacred Gold and Storm Silver, Complete and Classic): their new
// data files load like the game's own. The few things they change in the game's code that the port has in C are
// read from the ROM here, and the game picker gets their names. See include/port_sdk/port_rom_hacks.h.
//
// Sacred Gold 1.05 changes, in arm9, besides its anti-piracy fix: the special marts' lists (the Goldenrod Department
// Store's floors, the Pokemon Centers' second counters...: _0210FA3C in src/scrcmd_mart.c), in place.
#include "port_sdk/port_rom_hacks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simulator/sim_carts.h>

// --- files ---------------------------------------------------------------------------------

static u8 *ReadFile(const char *path, u32 *sizeOut)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *data = size > 0 && size < 64 * 1024 * 1024 ? malloc((size_t)size) : NULL;
    if (data != NULL && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data != NULL) {
        *sizeOut = (u32)size;
    }
    return data;
}

static u32 Read32(const u8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

static BOOL FileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return TRUE;
    }
    return FALSE;
}

// A NARC member's bytes [offset, offset + size)
static BOOL ReadNarcBytes(const char *path, int member, u32 offset, u8 *out, u32 size)
{
    u32 narcSize = 0;
    u8 *narc = ReadFile(path, &narcSize);
    BOOL ok = FALSE;
    if (narc != NULL && narcSize > 0x1C && memcmp(narc, "NARC", 4) == 0 && memcmp(narc + 16, "BTAF", 4) == 0) {
        const u32 btafSize = Read32(narc + 20);
        const u32 count = narc[24] | (narc[25] << 8);
        const u32 btnf = 0x10 + btafSize;
        if ((u32)member < count && btnf + 8 <= narcSize) {
            const u32 start = Read32(narc + 0x1C + 8 * member);
            const u32 end = Read32(narc + 0x20 + 8 * member);
            const u32 gmif = btnf + Read32(narc + btnf + 4) + 8;
            if (end >= start + offset + size && gmif + end <= narcSize) {
                memcpy(out, narc + gmif + start + offset, size);
                ok = TRUE;
            }
        }
    }
    free(narc);
    return ok;
}

// --- arm9 ----------------------------------------------------------------------------------

// The arm9 as the DS runs it: its static part is BLZ-compressed (backwards LZ, decompressed in place at boot), the
// end of it given by the module parameters (found by their "nitrocode" magic)
static u8 *DecompressedArm9(const u8 *arm9, u32 size, u32 load, u32 *sizeOut)
{
    static const u8 kMagic[8] = { 0x21, 0x06, 0xC0, 0xDE, 0xDE, 0xC0, 0x06, 0x21 };
    u32 params = 0;
    for (u32 i = 0x1C; i + 8 <= size; i += 4) {
        if (memcmp(arm9 + i, kMagic, 8) == 0) {
            params = i - 0x1C;
            break;
        }
    }
    if (params == 0) {
        return NULL;
    }
    const u32 compressedEnd = Read32(arm9 + params + 0x14);
    if (compressedEnd == 0) { // not compressed
        u8 *copy = malloc(size);
        if (copy != NULL) {
            memcpy(copy, arm9, size);
            *sizeOut = size;
        }
        return copy;
    }
    const u32 end = compressedEnd - load;
    if (end < 8 || end > size) {
        return NULL;
    }
    const u32 header = Read32(arm9 + end - 8);
    const u32 extra = Read32(arm9 + end - 4);
    const u32 compressedLength = header & 0xFFFFFF;
    const u32 headerLength = header >> 24;
    if (compressedLength > end || headerLength > compressedLength || extra > 16 * 1024 * 1024) {
        return NULL;
    }
    const u32 start = end - compressedLength;
    const u32 outLength = start + compressedLength + extra + (size - end);
    u8 *out = calloc(outLength, 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, arm9, start);
    memcpy(out + start + compressedLength + extra, arm9 + end, size - end);
    u8 *region = out + start;
    memcpy(region, arm9 + start, compressedLength - headerLength);
    s64 in = compressedLength - headerLength;
    s64 o = compressedLength + extra;
    while (in > 0 && o > 0) {
        u8 flags = region[--in];
        for (int bit = 0; bit < 8 && in > 0 && o > 0; bit++, flags <<= 1) {
            if (flags & 0x80) {
                if (in < 2) {
                    in = 0;
                    break;
                }
                in -= 2;
                const u32 v = region[in] | (region[in + 1] << 8);
                const u32 count = (v >> 12) + 3;
                const u32 distance = (v & 0xFFF) + 3;
                for (u32 n = 0; n < count && o > 0; n++) {
                    o--;
                    region[o] = o + distance < compressedLength + extra ? region[o + distance] : 0;
                }
            } else {
                region[--o] = region[--in];
            }
        }
    }
    *sizeOut = outLength;
    return out;
}

// --- the special marts ---------------------------------------------------------------------

static BOOL sMartsLoaded;
static u16 *sMarts[ROM_HACKS_MART_COUNT];

static int ListLength(const u16 *list)
{
    int n = 0;
    while (list[n] != 0xFFFF) {
        n++;
    }
    return n;
}

// The ROM's lists, found by the pointer table: ROM_HACKS_MART_COUNT pointers into arm9, each list ending in 0xFFFF
// exactly where the game's own does (a hack that changes the lists in place keeps their lengths)
static void LoadMarts(const u16 *const *vanilla)
{
    u32 headerSize = 0, arm9Size = 0, size = 0;
    u8 *header = ReadFile("header.bin", &headerSize);
    u8 *arm9 = ReadFile("arm9.bin", &arm9Size);
    u8 *code = header != NULL && arm9 != NULL && headerSize >= 0x30
        ? DecompressedArm9(arm9, arm9Size, Read32(header + 0x28), &size) : NULL;
    const u32 load = header != NULL && headerSize >= 0x30 ? Read32(header + 0x28) : 0;
    int lengths[ROM_HACKS_MART_COUNT];
    for (int i = 0; i < ROM_HACKS_MART_COUNT; i++) {
        lengths[i] = ListLength(vanilla[i]);
    }
    int found = -1;
    for (u32 at = 0; code != NULL && at + ROM_HACKS_MART_COUNT * 4 <= size && found < 0; at += 4) {
        BOOL match = TRUE;
        for (int i = 0; i < ROM_HACKS_MART_COUNT && match; i++) {
            const u32 address = Read32(code + at + i * 4);
            const u32 offset = address - load;
            if (address < load || offset + (lengths[i] + 1) * 2 > size || (offset & 1)) {
                match = FALSE;
                break;
            }
            const u8 *list = code + offset;
            for (int n = 0; n <= lengths[i] && match; n++) {
                const u16 item = list[n * 2] | (list[n * 2 + 1] << 8);
                match = n == lengths[i] ? item == 0xFFFF : item != 0xFFFF && item != 0;
            }
        }
        if (match) {
            found = (int)at;
        }
    }
    int changed = 0;
    if (found >= 0) {
        for (int i = 0; i < ROM_HACKS_MART_COUNT; i++) {
            const u8 *list = code + (Read32(code + found + i * 4) - load);
            sMarts[i] = malloc((lengths[i] + 1) * sizeof(u16));
            for (int n = 0; n <= lengths[i]; n++) {
                sMarts[i][n] = list[n * 2] | (list[n * 2 + 1] << 8);
            }
            changed += memcmp(sMarts[i], vanilla[i], (lengths[i] + 1) * sizeof(u16)) != 0;
        }
        printf("ROM hacks: the special marts' lists read from arm9 (0x%08X), %d of %d differ from HeartGold's\n",
            (unsigned)(load + found), changed, ROM_HACKS_MART_COUNT);
    } else {
        printf("ROM hacks: the special marts' lists weren't found in arm9: the game's own are used\n");
    }
    free(code);
    free(arm9);
    free(header);
}

const u16 *RomHacks_MartList(int which, const u16 *const *vanilla)
{
    if (!sMartsLoaded) {
        sMartsLoaded = TRUE;
        LoadMarts(vanilla);
    }
    if (which < 0 || which >= ROM_HACKS_MART_COUNT) {
        return NULL;
    }
    return sMarts[which] != NULL ? sMarts[which] : vanilla[which];
}

// --- the game picker -----------------------------------------------------------------------

void RomHacks_DescribeRom(const char *dir, char *name, int nameSize)
{
    char path[1024];
    char code[4] = { 'I', 'P', 'K', 'E' };
    snprintf(path, sizeof(path), "%s/header.bin", dir);
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) {
            memcpy(code, "IPKE", 4);
        }
        fclose(f);
    }
    const BOOL soulSilver = code[2] == 'G';
    // Drayano's hacks add msgdata/msg.narc; Complete changes Pokemon (Venomoth is Bug/Psychic, Bug/Poison otherwise)
    snprintf(path, sizeof(path), "%s/msgdata/msg.narc", dir);
    if (FileExists(path)) {
        u8 types[2] = { 0, 0 };
        snprintf(path, sizeof(path), "%s/a/0/0/2", dir);
        const BOOL complete = ReadNarcBytes(path, 49, 6, types, 2) && types[1] == 14;
        snprintf(name, nameSize, "%s%s", soulSilver ? "Storm Silver" : "Sacred Gold", complete ? "" : " Classic");
        return;
    }
    snprintf(name, nameSize, "%s", soulSilver ? "Pokemon SoulSilver" : "Pokemon HeartGold");
}

// libntr hooks
void SIM_Prj_CartName(const char *dir, char *name, int nameSize)
{
    RomHacks_DescribeRom(dir, name, nameSize);
}

static BOOL Md5Is(const u8 md5[16], const char *hex)
{
    for (int i = 0; i < 16; i++) {
        unsigned byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1 || md5[i] != byte) {
            return FALSE;
        }
    }
    return TRUE;
}

const char *SIM_Prj_KnownRom(const u8 md5[16])
{
    // No-Intro's dumps, and the ones Drayano's patches were made from
    static const struct { const char *md5, *name; } kRoms[] = {
        { "258cea3a62ac0d6eb04b5a0fd764d788", "Pokemon HeartGold (USA)" },
        { "ae2a483d0a5e8130d39f44f41a86df57", "Pokemon HeartGold (USA)" },
        { "8a6c8888bed9e1dce952f840351b73f2", "Pokemon SoulSilver (USA)" },
        { "981691920015f00bfde4894a7d737470", "Pokemon SoulSilver (USA)" },
        { "545aa550bafeebdd71ee0256d4c122a0", "Pokemon SoulSilver (USA)" },
    };
    for (size_t i = 0; i < sizeof(kRoms) / sizeof(kRoms[0]); i++) {
        if (Md5Is(md5, kRoms[i].md5)) {
            return kRoms[i].name;
        }
    }
    return NULL;
}

const char *SIM_Prj_KnownPatch(const u8 md5[16])
{
    // Sacred Gold and Storm Silver 1.05
    static const struct { const char *md5, *name; } kPatches[] = {
        { "cb6d7d257fc62e95e3e6586d7c1a2ea3", "SacredGoldComplete" },
        { "84b8427f6e0860a9950e452ee536d2cf", "SacredGoldClassic" },
        { "046e09115c6ce4ca0c20122818b6e3c1", "StormSilverComplete" },
        { "0ece2ee14c1ebbfc67a6c9d17471e735", "StormSilverClassic" },
    };
    for (size_t i = 0; i < sizeof(kPatches) / sizeof(kPatches[0]); i++) {
        if (Md5Is(md5, kPatches[i].md5)) {
            return kPatches[i].name;
        }
    }
    return NULL;
}

const char *SIM_Prj_AddGameText(int which)
{
    switch (which) {
    case SIM_ADD_TEXT_INTRO:
        return "Add another game next to the ones you have, like Drayano's Sacred Gold or Storm Silver. Each game keeps "
               "its own saves.";
    case SIM_ADD_TEXT_HACK_TITLE:
        return "Sacred Gold and Storm Silver";
    case SIM_ADD_TEXT_HACK_HELP:
        return "Unzip Drayano's download, put your HeartGold (USA) ROM in the Patches folder (or choose it after the "
               "folder; SoulSilver for Storm Silver), and choose that folder. Complete and Classic (no changes to the "
               "Pokemon) are both offered: pick one, or add the other later as a game of its own.";
    case SIM_ADD_TEXT_FILES_HELP:
        return "Sacred Gold: your HeartGold (USA) ROM, then SacredGoldComplete.patch or SacredGoldClassic.patch. Storm "
               "Silver: your SoulSilver (USA) ROM, then StormSilverComplete.patch or StormSilverClassic.patch.";
    case SIM_ADD_TEXT_WRONG_ROM:
        return "(Sacred Gold goes on HeartGold (USA), Storm Silver on SoulSilver (USA).)";
    default:
        return "";
    }
}
