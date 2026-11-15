#ifndef POKEHEARTGOLD_PORT_SLOT_MAPS_H
#define POKEHEARTGOLD_PORT_SLOT_MAPS_H

// The Japanese Game Corner maps, used with Settings > Game Corner > Japanese Game Corner (lib/port/slots/slot_maps.c)

#include "global.h"

#include "map_events_internal.h"

// The doors into the US Game Corners lead to the Japanese ones (a map's warps, as they are loaded)
void SlotMaps_RedirectWarps(WarpEvent *warps, u32 count);

// The Japanese maps' English text (the US ROM's rows are empty): the message's characters, or NULL for any other
u16 const *SlotMaps_Message(u16 narcId, u16 fileId, u32 msgNo, u32 *length);
u32 SlotMaps_MessageCount(u16 narcId, u16 fileId, u32 count);

// A map's scripts as loaded: the Japanese maps' coin clerk gives a Coin Case and 50 Coins to a player without one.
// Returns the scripts to use (scripts itself, or a patched copy with it freed)
u8 *SlotMaps_PatchScripts(u32 mapId, u8 *scripts, u32 size, enum HeapID heapID);

#endif
