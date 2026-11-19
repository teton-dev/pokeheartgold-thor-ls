// The alpha mark. See port_nuzlocke_mark.h
#include "port_nuzlocke_mark.h"

#include "port_nuzlocke_mode.h"

#include "font.h"
#include "heap.h"
#include "pm_string.h"
#include "render_text.h"
#include "string_util.h"

#define MARK_RED 3 // the font palette's red text color

// r: red, w: an eye (the background color), .: left as it is
static const char *const sMark[NUZLOCKE_MARK_SIZE] = {
    ".......rr.......",
    "......rrrr......",
    "..r..rrrrrr..r..",
    "..rr.rrrrrr.rr..",
    "..rrrrrrrrrrrr..",
    ".rrrrrrrrrrrrrr.",
    ".rrrrrrrrrrrrrr.",
    ".rwrrrrrrrrrrwr.",
    ".rwwrrrrrrrrwwr.",
    ".rwwwrrrrrrwwwr.",
    ".rrwwwrrrrwwwrr.",
    ".rrwwrrrrrrwwrr.",
    "..rrrrrrrrrrrr..",
    "...rrrrrrrrrr...",
    ".....rrrrrr.....",
    ".......rr.......",
};

void NuzlockeMark_Draw(Window *window, u16 x, u16 y, u8 bgColor) {
    for (int row = 0; row < NUZLOCKE_MARK_SIZE; row++) {
        // Runs of one color, as rectangles
        int col = 0;
        while (col < NUZLOCKE_MARK_SIZE) {
            char c = sMark[row][col];
            int end = col + 1;
            while (end < NUZLOCKE_MARK_SIZE && sMark[row][end] == c) {
                end++;
            }
            if (c != '.') {
                FillWindowPixelRect(window, c == 'r' ? MARK_RED : bgColor, x + col, y + row, end - col, 1);
            }
            col = end;
        }
    }
}

// HGSS's character codes for ASCII text (what MainMenu_PrintSlot does for "SLOT n/4")
static void PrintAscii(Window *window, const char *text, int x, BOOL rightAligned, u16 y, u32 color, enum HeapID heapId) {
    u16 chars[24];
    int n = 0;
    for (const char *c = text; *c != '\0' && n < 23; c++) {
        if (*c >= '0' && *c <= '9') {
            chars[n++] = 0x121 + (*c - '0');
        } else if (*c >= 'A' && *c <= 'Z') {
            chars[n++] = 0x12B + (*c - 'A');
        } else if (*c >= 'a' && *c <= 'z') {
            chars[n++] = 0x145 + (*c - 'a');
        } else {
            chars[n++] = 0x1DE; // a space
        }
    }
    chars[n] = EOS;
    String *string = String_New(24, heapId);
    CopyU16ArrayToString(string, chars);
    if (rightAligned) {
        x -= FontID_String_GetWidth(0, string, 0);
    }
    AddTextPrinterParameterizedWithColor(window, 0, string, x, y, TEXT_SPEED_NOTRANSFER, color, NULL);
    String_Delete(string);
}

void NuzlockeMark_PrintLine(Window *window, int level, u16 markX, u16 labelX, u16 rightEdge, u16 y, u32 textColor, u8 bgColor, enum HeapID heapId) {
    NuzlockeMark_Draw(window, markX, y, bgColor);
    PrintAscii(window, "NUZLOCKE", labelX, FALSE, y, textColor, heapId);
    PrintAscii(window, NuzlockeMode_LevelName(level), rightEdge, TRUE, y, textColor, heapId);
}
