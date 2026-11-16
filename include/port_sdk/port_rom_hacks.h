#ifndef PORT_SDK_PORT_ROM_HACKS_H
#define PORT_SDK_PORT_ROM_HACKS_H

// ROM hacks of HeartGold and SoulSilver (Sacred Gold and Storm Silver): see lib/port/rom_hacks.c

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The special marts (src/scrcmd_mart.c's _0210FA3C): the ROM's list `which`, read from its arm9 the first time
// (vanilla: the game's own lists, used when the ROM's aren't found)
#define ROM_HACKS_MART_COUNT 30
const u16 *RomHacks_MartList(int which, const u16 *const *vanilla);

// A name for the game extracted in dir: "Pokemon HeartGold", "Sacred Gold Classic", "Storm Silver"...
void RomHacks_DescribeRom(const char *dir, char *name, int nameSize);

#ifdef __cplusplus
}
#endif

#endif // PORT_SDK_PORT_ROM_HACKS_H
