#ifndef POKEHEARTGOLD_PORT_SLOT_ASSETS_H
#define POKEHEARTGOLD_PORT_SLOT_ASSETS_H

// The extracted Japanese slot machine files (slots-jp/: a/1/2/7, a/1/5/5, ov77.bin), read with stdio, and the pieces
// of Gen 4 graphics files (NCLR, NCGR, NSCR) that the BG loaders take. Standalone C.

#include <stdint.h>

typedef struct SlotNarc {
    uint8_t *data; // the whole file
    uint32_t size;
    uint16_t count;
    uint32_t fatOffset; // the entries
    uint32_t imgOffset; // the member data's start
} SlotNarc;

typedef struct SlotBlock {
    const uint8_t *data;
    uint32_t size;
} SlotBlock;

// dir/a/1/2/7 and so on; 0 on failure
int SlotNarc_Open(SlotNarc *narc, const char *dir, const char *path);
void SlotNarc_Close(SlotNarc *narc);
SlotBlock SlotNarc_Member(const SlotNarc *narc, unsigned index);

// The data of a palette (NCLR), tile (NCGR) or screen (NSCR) file; size 0 if it isn't one
SlotBlock SlotGfx_Palette(SlotBlock file);
SlotBlock SlotGfx_Tiles(SlotBlock file);
SlotBlock SlotGfx_Screen(SlotBlock file);

// The whole of a file (ov77.bin), malloc'd; NULL on failure
uint8_t *SlotAssets_ReadFile(const char *dir, const char *name, uint32_t *size);

#endif
