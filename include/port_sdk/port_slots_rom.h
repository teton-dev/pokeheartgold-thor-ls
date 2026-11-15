#ifndef POKEHEARTGOLD_PORT_SLOTS_ROM_H
#define POKEHEARTGOLD_PORT_SLOTS_ROM_H

// The player's Japanese HeartGold or SoulSilver ROM for the Japanese Game Corner (teton-dev fork; the plan is
// docs/thor/dev/hgss-game-corner.md in the Platinum fork). Its slot machine files are extracted into the data
// folder's slots-jp/; the ROM isn't kept.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum SlotsRomState {
    SLOTS_ROM_NONE,
    SLOTS_ROM_IMPORTING, // on a thread of its own
    SLOTS_ROM_READY,
    SLOTS_ROM_FAILED, // SlotsRom_Status says why; an earlier import may still be ready
};

// deleteAfter: remove that file when done (the picker's copy on Android)
void SlotsRom_Import(const char *path, BOOL deleteAfter);
void SlotsRom_Update(void); // every frame the settings show
enum SlotsRomState SlotsRom_State(void);
const char *SlotsRom_Status(void);
const char *SlotsRom_GameName(void); // "HeartGold" or "SoulSilver" once imported, else NULL
const char *SlotsRom_Dir(void); // the extracted files' folder
BOOL SlotsRom_Remove(void);

#ifdef __cplusplus
}
#endif

#endif
