#ifndef POKEHEARTGOLD_PORT_NUZLOCKE_MODE_GAME_H
#define POKEHEARTGOLD_PORT_NUZLOCKE_MODE_GAME_H

// What Nuzlocke mode's core (port_nuzlocke_mode.h) needs from a game. HGSS's are in
// lib/port/nuzlocke_heartgold.c (the Platinum fork's src/port/nuzlocke_platinum.c, against HGSS's save and bag
// APIs). All called on the game thread.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The running save's identity (its trainer ID) and slot; FALSE when no game is running
BOOL NuzlockeModeGame_RunningSave(u32 *saveId, int *slot);

// Where the records go in the save image: count sectors from primary, mirrored from backup,
// none of them used by the game
void NuzlockeModeGame_RecordSectors(u32 *primary, u32 *backup, int *count, u32 *sectorSize);

// FALSE while the game is writing its save (the records wait for it to finish)
BOOL NuzlockeModeGame_CanWrite(void);

// The bag has a Poke Ball (any ball)
BOOL NuzlockeModeGame_HasPokeBalls(void);

BOOL NuzlockeModeGame_ReadFlash(u32 address, void *data, u32 size);
BOOL NuzlockeModeGame_WriteFlash(u32 address, void *data, u32 size);

#ifdef __cplusplus
}
#endif

#endif // POKEHEARTGOLD_PORT_NUZLOCKE_MODE_GAME_H
