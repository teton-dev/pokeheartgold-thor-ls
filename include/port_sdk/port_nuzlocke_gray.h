#ifndef POKEHEARTGOLD_PORT_NUZLOCKE_GRAY_H
#define POKEHEARTGOLD_PORT_NUZLOCKE_GRAY_H

// Gray icons for dead Pokemon (the Platinum fork's src/port/nuzlocke_gray.c): a Pokemon is drawn gray while it is
// dead and the save is a Nuzlocke run. Icons share three palettes per screen, so a dead one gets a gray copy of its
// palette in a free slot.

#include "pokemon.h"
#include "sprite.h"

BOOL NuzlockeGray_IsDead(Pokemon *mon);
BOOL NuzlockeGray_BoxIsDead(BoxPokemon *boxMon);

// Luminance, a little darker and flatter (index 0 of each palette stays)
void NuzlockeGray_Colors(u16 *colors, int count);

// Makes the sprite's palette gray: a copy of the one it draws with, in a free slot of the OBJ palettes (the manager's),
// which the sprite then uses. onSub: the sprite is drawn on the sub screen (its surface is the sub one). FALSE (and
// nothing changed) with no free slot
BOOL NuzlockeGray_Sprite(Sprite *sprite, BOOL onSub);

// The same, for a screen whose sprite system owns every palette slot (the manager has none free): the copy goes to the
// first of these slots that no other gray copy of the screen holds (slots the screen doesn't draw with)
BOOL NuzlockeGray_SpriteInSlots(Sprite *sprite, BOOL onSub, const int *slots, int slotCount);

// Once a frame (the main loop): the gray copies follow their palettes
void NuzlockeGray_Update(void);

#endif // POKEHEARTGOLD_PORT_NUZLOCKE_GRAY_H
