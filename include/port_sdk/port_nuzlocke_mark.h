#ifndef POKEHEARTGOLD_PORT_NUZLOCKE_MARK_H
#define POKEHEARTGOLD_PORT_NUZLOCKE_MARK_H

// The alpha mark a Nuzlocke save shows on the save screens and the main menu (the Platinum fork's
// src/port/nuzlocke_mark.c): a 16x16 pixel version of the owner's alpha icon, drawn with the font palette (red,
// its eyes in the window's background color).

#include "bg_window.h"
#include "heap.h"
#include "text.h"

#define NUZLOCKE_MARK_SIZE 16

// bgColor: the window's background color (the mark's eyes)
void NuzlockeMark_Draw(Window *window, u16 x, u16 y, u8 bgColor);

// A line of an info window: the mark at markX, "NUZLOCKE" at labelX, the level's name ending at rightEdge.
// bgColor is the window's background color (the mark's eyes)
void NuzlockeMark_PrintLine(Window *window, int level, u16 markX, u16 labelX, u16 rightEdge, u16 y, u32 textColor, u8 bgColor, enum HeapID heapId);

// A String of ASCII text in HGSS's character codes (\x01 is an e acute); free it with String_Delete
String *NuzlockeMark_NewString(const char *text, enum HeapID heapId);

#endif // POKEHEARTGOLD_PORT_NUZLOCKE_MARK_H
