// The Team Rocket arena for the Gen 5 battles (teton-dev fork): the Platinum fork's Team Galactic arena
// (pokeplatinum-thor-ls src/port/gen5_galactic.c, from which this is copied) in Team Rocket's colours. BW2's Champion
// room (field 18, the dark sky with the light streaks and the gold rings), darkened, with every streak in Team Rocket
// red, the sky's field of gleaming stars, and a red rocket ship and a crescent moon in place of the Galactic arena's
// Jupiter and Saturn. A Team Rocket battle is in it (lib/port/gen5_battle.c, ArenaFromSetup).
//
// Nothing of BW2's is kept in the repository: the arena is made from the player's own ROM at the first battle (see
// Gen5Rocket_PrepareArena), by changing a copy of the room's model resource (a BMD0 with its textures). The room's
// moving streaks are the A3I5 texture "batt_field21n" (white shapes, the alpha makes the glow) that BW2's animation
// scrolls around the room, on ribbons (the shapes "polygon3" and "polygon4": strips of triangles with a dark edge and a
// coloured one) whose vertex colours give each streak its rainbow; the sky has "batt_sky21n", a few dim dots. Here:
//   - the streaks' colour: each ribbon's vertex colours become Rocket red, at the brightness each vertex had, so the
//     fade along it stays;
//   - the sky: its texture is drawn again from nothing, with sparkles and a scatter of fainter points in colours 3
//     (ice) and 4 (white), the ship in 2, 5, 6 (red, dark red, flame) and 4, the moon in 7 of the palette the two textures share ("batt_field21n_pl"; colour 1, the streaks' white, is kept).

#include "port_gen5_rocket.h"

#ifdef GEN5_BATTLES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gen5.h"
#include "port_gen5_rom.h"

// 15-bit colours: the stars' ice and white (the ribbons' red is in RecolorRibbons, 5 bits a channel)
#define RGB15(r, g, b) ((u16)((((r) >> 3) & 31) | ((((g) >> 3) & 31) << 5) | ((((b) >> 3) & 31) << 10)))
#define COLOR_ICE    RGB15(0xE4, 0xF2, 0xFF)
#define COLOR_WHITE  RGB15(0xFF, 0xFF, 0xFF)

// The ship and the moon: colours 2, 5, 6 and 7 of the stars' palette
#define COLOR_ROCKET_RED    RGB15(0xE0, 0x18, 0x28)
#define COLOR_ROCKET_DARK   RGB15(0x80, 0x08, 0x14)
#define COLOR_FLAME         RGB15(0xFF, 0xB8, 0x30)
#define COLOR_MOON          RGB15(0xEE, 0xE8, 0xD0)

#define TEX_WIDTH  128
#define TEX_HEIGHT 64

static u16 Read16(const u8 *p)
{
    return p[0] | (p[1] << 8);
}

static u32 Read32(const u8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

// An entry of a NITRO dictionary at dict (from the start of the section), by name: its data (entrySize bytes). The
// dictionary is a header of 8 bytes, a tree of count + 1 nodes of 4 bytes, the entries' size and their block's, the
// entries, then their names in 16 bytes each
static const u8 *DictFind(const u8 *section, u32 sectionSize, u32 dict, const char *name, u32 *entrySize)
{
    if (dict + 8 > sectionSize) {
        return NULL;
    }
    const u32 count = section[dict + 1];
    const u32 sizeAt = dict + 8 + 4 * (count + 1);
    if (sizeAt + 4 > sectionSize) {
        return NULL;
    }
    const u32 size = Read16(section + sizeAt);
    const u32 entries = sizeAt + 4;
    const u32 names = entries + count * size;
    if (names + count * 16 > sectionSize) {
        return NULL;
    }
    for (u32 i = 0; i < count; i++) {
        if (strncmp((const char *)section + names + 16 * i, name, 16) == 0) {
            *entrySize = size;
            return section + entries + i * size;
        }
    }
    return NULL;
}

// The texture's pixels (A3I5, TEX_WIDTH x TEX_HEIGHT) and the palette (8 colours) a model resource has for them
static BOOL Locate(u8 *res, u32 size, const char *texture, const char *palette, u8 **pixels, u16 **colors)
{
    if (size < 0x20 || memcmp(res, "BMD0", 4) != 0) {
        return FALSE;
    }
    const u32 blocks = Read16(res + 0x0E);
    u8 *tex = NULL;
    u32 texSize = 0;
    for (u32 i = 0; i < blocks && 0x10 + 4 * i + 4 <= size; i++) {
        const u32 offset = Read32(res + 0x10 + 4 * i);
        if (offset + 0x40 <= size && memcmp(res + offset, "TEX0", 4) == 0) {
            tex = res + offset;
            texSize = size - offset;
            if (Read32(tex + 4) < texSize) {
                texSize = Read32(tex + 4);
            }
        }
    }
    if (tex == NULL || texSize < 0x40) {
        return FALSE;
    }
    u32 entrySize = 0;
    const u8 *info = DictFind(tex, texSize, Read16(tex + 0x0E), texture, &entrySize);
    if (info == NULL || entrySize != 8) {
        return FALSE;
    }
    const u32 param = Read16(info + 2);
    // A3I5 (format 1), 128 x 64
    if (((param >> 10) & 7) != 1 || (8u << ((param >> 4) & 7)) != TEX_WIDTH || (8u << ((param >> 7) & 7)) != TEX_HEIGHT) {
        return FALSE;
    }
    const u32 pixelsAt = Read32(tex + 0x14) + ((u32)Read16(info) << 3);
    const u8 *palInfo = DictFind(tex, texSize, Read32(tex + 0x34), palette, &entrySize);
    if (palInfo == NULL || entrySize != 4) {
        return FALSE;
    }
    const u32 colorsAt = Read32(tex + 0x38) + ((u32)Read16(palInfo) << 3);
    if (pixelsAt + TEX_WIDTH * TEX_HEIGHT > texSize || colorsAt + 16 > texSize) {
        return FALSE;
    }
    *pixels = tex + pixelsAt;
    *colors = (u16 *)(tex + colorsAt);
    return TRUE;
}

static void Put16(u16 *where, u16 value)
{
    u8 *p = (u8 *)where;
    p[0] = value & 0xFF;
    p[1] = value >> 8;
}

// The display lists' commands (GX geometry commands) and how many words of parameters each takes
static int CommandWords(u8 command)
{
    switch (command) {
    case 0x10: case 0x12: case 0x13: case 0x14: case 0x20: case 0x21: case 0x22: case 0x24: case 0x25: case 0x26:
    case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x30: case 0x31: case 0x32: case 0x33: case 0x40:
    case 0x50: case 0x60: case 0x72:
        return 1;
    case 0x23: case 0x71:
        return 2;
    case 0x70:
        return 3;
    case 0x11: case 0x15: case 0x41:
        return 0;
    case 0x16:
        return 16;
    case 0x17:
        return 12;
    case 0x18: case 0x19:
        return 48;
    case 0x1A:
        return 36;
    case 0x1B: case 0x1C:
        return 12;
    case 0x34:
        return 32;
    default:
        return -1;
    }
}

// The ribbons of a shape's display list: the vertex colours (command 0x20) in each BEGIN_VTXS (0x40) to END_VTXS (0x41)
// become Rocket red, at the brightness they had. The dark edge's colour (all its channels under 8) is kept. (*ribbon
// counts them, as the Galactic arena's alternating colours did)
static void RecolorRibbons(u8 *list, u32 size, int *ribbon)
{
    static const int hue[3] = { 31, 3, 5 }; // Team Rocket's red: 5 bits a channel
    u32 at = 0;
    while (at + 4 <= size) {
        u8 commands[4];
        memcpy(commands, list + at, 4);
        at += 4;
        for (int c = 0; c < 4; c++) {
            if (commands[c] == 0) {
                continue;
            }
            const int words = CommandWords(commands[c]);
            if (words < 0 || at + 4 * words > size) {
                return;
            }
            if (commands[c] == 0x40) {
                (*ribbon)++;
            } else if (commands[c] == 0x20) {
                const u32 color = Read32(list + at);
                const int r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
                int bright = r > g ? r : g;
                bright = bright > b ? bright : b;
                if (bright >= 8) {
                    // A little brighter than it was, up to the hue's own
                    int gain = bright * 13 / 10;
                    gain = gain > 31 ? 31 : gain;
                    const u32 out = (hue[0] * gain / 31) | ((hue[1] * gain / 31) << 5) | ((hue[2] * gain / 31) << 10);
                    list[at] = out & 0xFF;
                    list[at + 1] = (color & 0x8000) ? ((out >> 8) | 0x80) : (out >> 8);
                }
            }
            at += 4 * words;
        }
    }
}

// The sky's stars' shape: its vertex colours (blues, dim toward one edge) become grey of a higher brightness, so the stars
// and planets keep their own colours and shine
static void WhitenSky(u8 *list, u32 size)
{
    u32 at = 0;
    while (at + 4 <= size) {
        u8 commands[4];
        memcpy(commands, list + at, 4);
        at += 4;
        for (int c = 0; c < 4; c++) {
            if (commands[c] == 0) {
                continue;
            }
            const int words = CommandWords(commands[c]);
            if (words < 0 || at + 4 * words > size) {
                return;
            }
            if (commands[c] == 0x20) {
                const u32 color = Read32(list + at);
                const int r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
                int bright = r > g ? r : g;
                bright = bright > b ? bright : b;
                int gain = bright * 2 + 4;
                gain = gain > 31 ? 31 : gain;
                const u32 out = gain | (gain << 5) | (gain << 10);
                list[at] = out & 0xFF;
                list[at + 1] = out >> 8;
            }
            at += 4 * words;
        }
    }
}

// The model's shape named name: its display list
static BOOL FindShape(u8 *res, u32 size, const char *name, u8 **list, u32 *listSize)
{
    const u32 blocks = Read16(res + 0x0E);
    for (u32 i = 0; i < blocks && 0x10 + 4 * i + 4 <= size; i++) {
        const u32 offset = Read32(res + 0x10 + 4 * i);
        if (offset + 0x20 > size || memcmp(res + offset, "MDL0", 4) != 0) {
            continue;
        }
        u8 *mdl = res + offset;
        const u32 mdlSize = size - offset;
        u32 entrySize = 0;
        const u8 *model = mdlSize > 0x40 ? mdl + 8 : NULL;
        if (model == NULL || model[1] < 1) {
            return FALSE;
        }
        // The first (only) model: the dictionary's first entry, then its shapes' dictionary
        const u32 count = model[1];
        const u32 entries = 8 + 8 + 4 * (count + 1) + 4; // the dictionary starts at 8
        if (entries + 4 > mdlSize) {
            return FALSE;
        }
        const u32 start = Read32(mdl + entries);
        if (start + 16 > mdlSize) {
            return FALSE;
        }
        const u32 shapes = start + Read32(mdl + start + 12);
        if (shapes + 8 > mdlSize) {
            return FALSE;
        }
        const u8 *shape = DictFind(mdl, mdlSize, shapes, name, &entrySize);
        if (shape == NULL || entrySize != 4) {
            return FALSE;
        }
        const u32 header = shapes + Read32(shape);
        if (header + 16 > mdlSize) {
            return FALSE;
        }
        const u32 listAt = header + Read32(mdl + header + 8);
        const u32 length = Read32(mdl + header + 12);
        if (listAt + length > mdlSize) {
            return FALSE;
        }
        *list = mdl + listAt;
        *listSize = length;
        return TRUE;
    }
    return FALSE;
}

static void Plot(u8 *pixels, int x, int y, int color, int alpha)
{
    x = ((x % TEX_WIDTH) + TEX_WIDTH) % TEX_WIDTH; // the sky wraps around the room
    if (y < 0 || y >= TEX_HEIGHT || alpha <= 0) {
        return;
    }
    u8 *p = &pixels[y * TEX_WIDTH + x];
    if ((*p >> 5) >= alpha) {
        return;
    }
    *p = (u8)((alpha << 5) | color);
}

// A sparkle: a bright centre and four arms that fade out (armX across, armY up and down), faint diagonals when big
static void Sparkle(u8 *pixels, int cx, int cy, int armX, int armY, BOOL diagonals)
{
    Plot(pixels, cx, cy, 4, 7);
    for (int k = 1; k <= armX; k++) {
        const int alpha = 7 - (4 * (k - 1)) / armX;
        Plot(pixels, cx - k, cy, k == 1 ? 4 : 3, alpha);
        Plot(pixels, cx + k, cy, k == 1 ? 4 : 3, alpha);
    }
    for (int k = 1; k <= armY; k++) {
        const int alpha = 7 - (4 * (k - 1)) / armY;
        Plot(pixels, cx, cy - k, k == 1 ? 4 : 3, alpha);
        Plot(pixels, cx, cy + k, k == 1 ? 4 : 3, alpha);
    }
    if (diagonals) {
        Plot(pixels, cx - 1, cy - 1, 3, 5);
        Plot(pixels, cx + 1, cy - 1, 3, 5);
        Plot(pixels, cx - 1, cy + 1, 3, 5);
        Plot(pixels, cx + 1, cy + 1, 3, 5);
    }
}

static void Set(u8 *pixels, int x, int y, int color, int alpha)
{
    x = ((x % TEX_WIDTH) + TEX_WIDTH) % TEX_WIDTH;
    if (y >= 0 && y < TEX_HEIGHT) {
        pixels[y * TEX_WIDTH + x] = (u8)((alpha << 5) | color);
    }
}

// In the room (as the widescreen battle shows it) a texel of the sky is about 0.8 as wide as it is high on screen, and
// only the texture's top 20 rows or so are seen
// The ship: a rocket standing up, lit from the left (red, dark red on the right), a window, fins and a flame. Pixels by
// row: R red, D dark red, W white, I ice, F flame, . none
static void Ship(u8 *pixels, int cx, int top)
{
    static const char *const rows[] = {
        "...R...",
        "..RRD..",
        "..RRD..",
        "..RRD..",
        "..RWD..",
        "..RID..",
        "..RRD..",
        "..RRD..",
        "..RRD..",
        "..RRD..",
        ".RRRDD.",
        "RRRRDDD",
        "R..F..D",
        "..FFF..",
        "...F...",
    };
    for (u32 y = 0; y < sizeof(rows) / sizeof(rows[0]); y++) {
        for (int x = 0; rows[y][x] != '\0'; x++) {
            int color;
            switch (rows[y][x]) {
            case 'R': color = 2; break;
            case 'D': color = 5; break;
            case 'W': color = 4; break;
            case 'I': color = 3; break;
            case 'F': color = 6; break;
            default: continue;
            }
            // The flame's tip a little see-through
            Set(pixels, cx - 3 + x, top + (int)y, color, (color == 6 && y == 14) ? 5 : 7);
        }
    }
}

// The moon's glyph: a crescent opening to the right, 15 wide and 13 high (a disc less the same disc moved right), its
// outer rim dimmer
static void Moon(u8 *pixels, int cx, int cy)
{
    for (int y = -6; y <= 6; y++) {
        for (int x = -8; x <= 8; x++) {
            // In tenths of a row, a texel being 0.8 as wide as high on screen
            const int outer = (x * 8) * (x * 8) + (y * 10) * (y * 10);
            const int inner = (x * 8 - 30) * (x * 8 - 30) + (y * 10) * (y * 10);
            if (outer <= 62 * 62 && inner > 52 * 52) {
                Set(pixels, cx + x, cy + y, 7, outer > 54 * 54 ? 5 : 7);
            }
        }
    }
}

// The stars: sparkles of a few sizes at fixed places (so every battle has the same sky), and a scatter of points
static void DrawStars(u8 *pixels)
{
    memset(pixels, 0, TEX_WIDTH * TEX_HEIGHT);
    static const struct {
        u8 x, y, armX, armY, diagonals;
    } sparkles[] = {
        { 6, 9, 5, 4, 1 },
        { 22, 31, 3, 3, 0 },
        { 54, 5, 4, 3, 1 },
        { 47, 44, 3, 2, 0 },
        { 58, 22, 6, 5, 1 },
        { 71, 8, 3, 3, 0 },
        { 80, 36, 5, 4, 1 },
        { 106, 9, 3, 2, 0 },
        { 104, 49, 4, 3, 1 },
        { 116, 26, 6, 5, 1 },
        { 120, 6, 3, 3, 0 },
        { 14, 53, 3, 2, 0 },
        { 64, 56, 4, 3, 0 },
        { 78, 3, 2, 2, 0 }, // clear of the moon
    };
    // Points first, so the sparkles are drawn over them
    u32 seed = 0x5EED5EEDu;
    for (int i = 0; i < 70; i++) {
        seed = seed * 1664525u + 1013904223u;
        const int x = (seed >> 8) % TEX_WIDTH;
        seed = seed * 1664525u + 1013904223u;
        const int y = (seed >> 8) % (TEX_HEIGHT - 4);
        seed = seed * 1664525u + 1013904223u;
        Plot(pixels, x, y, (seed >> 20) & 1 ? 4 : 3, 3 + (int)((seed >> 12) % 4));
    }
    for (u32 i = 0; i < sizeof(sparkles) / sizeof(sparkles[0]); i++) {
        Sparkle(pixels, sparkles[i].x, sparkles[i].y, sparkles[i].armX, sparkles[i].armY, sparkles[i].diagonals);
    }
    // The ship and the moon, over the stars
    Ship(pixels, 40, 4);
    Moon(pixels, 92, 8); // high enough to stay above the ribbons, which pass in front of the sky
}

// The colours of a palette of the model's textures by name, up to the end of the texture block
static BOOL FindPalette(u8 *res, u32 size, const char *name, u16 **colors, u32 *count)
{
    const u32 blocks = Read16(res + 0x0E);
    for (u32 i = 0; i < blocks && 0x10 + 4 * i + 4 <= size; i++) {
        const u32 offset = Read32(res + 0x10 + 4 * i);
        if (offset + 0x40 > size || memcmp(res + offset, "TEX0", 4) != 0) {
            continue;
        }
        u8 *tex = res + offset;
        u32 texSize = size - offset;
        u32 entrySize = 0;
        const u8 *info = DictFind(tex, texSize, Read32(tex + 0x34), name, &entrySize);
        if (info == NULL || entrySize != 4) {
            return FALSE;
        }
        const u32 at = Read32(tex + 0x38) + ((u32)Read16(info) << 3);
        if (at >= texSize) {
            return FALSE;
        }
        *colors = (u16 *)(tex + at);
        *count = (texSize - at) / 2;
        return TRUE;
    }
    return FALSE;
}

// The background's levels (the sky's and the ground's 256-colour textures): each channel's darkest value over both
// palettes becomes black and the brightest stays, the ones between fall off with the square (darker mids). The room's darkest part (the top of
// the sky) reaches black and the brightest is as it was
static BOOL DarkenBackground(u8 *res, u32 size)
{
    static const char *const names[2] = { "batt_field21_pl", "batt_sky21_pl" };
    u16 *palettes[2];
    u32 counts[2];
    int low[3] = { 31, 31, 31 };
    int high[3] = { 0, 0, 0 };
    for (int p = 0; p < 2; p++) {
        if (!FindPalette(res, size, names[p], &palettes[p], &counts[p])) {
            return FALSE;
        }
        if (p == 0 && counts[0] > 80) {
            counts[0] = 80; // the ground's palette is 160 bytes, the sky's follows it
        }
        for (u32 i = 0; i < counts[p]; i++) {
            const u16 c = Read16((const u8 *)&palettes[p][i]);
            for (int ch = 0; ch < 3; ch++) {
                const int v = (c >> (5 * ch)) & 31;
                low[ch] = v < low[ch] ? v : low[ch];
                high[ch] = v > high[ch] ? v : high[ch];
            }
        }
    }
    for (int p = 0; p < 2; p++) {
        for (u32 i = 0; i < counts[p]; i++) {
            const u16 c = Read16((const u8 *)&palettes[p][i]);
            u16 out = 0;
            for (int ch = 0; ch < 3; ch++) {
                const int v = (c >> (5 * ch)) & 31;
                const int range = high[ch] - low[ch];
                // Stretched to 0..range, then squared: the mid colours fall toward black, the brightest stays
                const int t = range > 0 ? v - low[ch] : 0;
                const int n = range > 0 ? (t * t * high[ch] + range * range / 2) / (range * range) : v;
                out |= (u16)(n << (5 * ch));
            }
            Put16(&palettes[p][i], out);
        }
    }
    return TRUE;
}

BOOL Gen5Rocket_Make(u8 *res, u32 size)
{
    u8 *streaks, *stars;
    u16 *colors, *skyColors;
    if (!Locate(res, size, "batt_field21n", "batt_field21n_pl", &streaks, &colors)
        || !Locate(res, size, "batt_sky21n", "batt_sky21n_pl", &stars, &skyColors) || colors != skyColors) {
        return FALSE;
    }
    u8 *list;
    u32 listSize;
    int ribbon = 0;
    if (!FindShape(res, size, "polygon3", &list, &listSize)) {
        return FALSE;
    }
    RecolorRibbons(list, listSize, &ribbon);
    if (!FindShape(res, size, "polygon4", &list, &listSize)) {
        return FALSE;
    }
    RecolorRibbons(list, listSize, &ribbon);
    (void)streaks;
    if (!FindShape(res, size, "polygon2", &list, &listSize)) {
        return FALSE;
    }
    WhitenSky(list, listSize);
    if (!DarkenBackground(res, size)) {
        return FALSE;
    }
    DrawStars(stars);
    Put16(&colors[0], 0);
    Put16(&colors[1], COLOR_WHITE); // the streaks', as BW2's: the ribbons' vertex colours give their hue
    Put16(&colors[2], COLOR_ROCKET_RED);
    Put16(&colors[3], COLOR_ICE);
    Put16(&colors[4], COLOR_WHITE);
    Put16(&colors[5], COLOR_ROCKET_DARK);
    Put16(&colors[6], COLOR_FLAME);
    Put16(&colors[7], COLOR_MOON);
    return TRUE;
}

// The platforms: BW2's Champion room's two grounds are one model ("batt_stage21": a gold ring on cream). Its palette
// (all but the 4 colours of the shadow's "batt_bk21_pl") becomes dark crimson-steel: each colour's brightness, squared,
// times a dark tint
BOOL Gen5Rocket_MakeStage(u8 *res, u32 size)
{
    u16 *colors;
    u32 count;
    if (!FindPalette(res, size, "batt_stage21_pl", &colors, &count)) {
        return FALSE;
    }
    static const int tint[3] = { 140, 70, 76 }; // the brightest colour's: dark crimson
    for (u32 i = 0; i < count && i < 256; i++) {
        const u16 c = Read16((const u8 *)&colors[i]);
        const int r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
        const int luma = (r * 3 + g * 6 + b) / 10; // 0..255
        const int level = luma * luma / 255;
        Put16(&colors[i], RGB15(tint[0] * level / 255, tint[1] * level / 255, tint[2] * level / 255));
    }
    return TRUE;
}

// --- The arena's files ------------------------------------------------------------------------------------------
//
// The Platinum fork's Gen5Data_PrepareGalacticArena (src/port/gen5_data.c), for this arena: it is made from the
// player's ROM into copies of two archives, in gen5_data/ (in the game's folder), that BW2 then reads in place of its
// own:
//   archive 11 (ARCID_BTLV, a/0/1/1): the changed copies of the room's model resource (member 115) and of its stage's
//     grounds (member 38) are added as the last members
//   archive 151 (ARCID_BATTLE_BG_DATA, a/1/5/1): file 0 (the arenas) has, for the Champion's background (16), terrain 1
//     in a new field (file 1's row of the Champion room's, 18, with the new model) and a new stage (file 2's row of the
//     Champion room's, 21, with the new grounds). The room's animations (the scrolling of the streaks and the sky, by the
//     materials' names) are the Champion room's. HGSS's battles use background 16 only for Team Rocket.
#define DIR "gen5_data"
#define BTLV_ARC 11 // ARCID_BTLV: a/0/1/1
#define BTLV_PATH DIR "/a/0/1/1"
#define BG_DATA_ARC 151 // ARCID_BATTLE_BG_DATA: a/1/5/1
#define BG_DATA_PATH DIR "/a/1/5/1"
#define ROCKET_STAMP_PATH DIR "/rocket_arena.txt"
#define ROCKET_VERSION 7 // 5: the Safari Zone's labels too; 6, 7: the Apricorn Balls' throw palettes
#define BALLS_ARC 94 // ARCID_BTLV_EFFVM_BG: a/0/9/4, the thrown balls' sprites
#define BALLS_PATH DIR "/a/0/9/4"
#define BALL_POKE 4
#define BALL_FAST 17
#define APRICORN_BALLS 8 // the Fast Ball (17) to the Sport Ball (24)
#define CHAMPION_MODEL 115 // member of archive 11
#define CHAMPION_STAGE 21
#define STAGE_MODEL 38 // member of archive 11: both grounds of the Champion room's stage
#define STAGE_ROW_SIZE 0x42 // a stage of file 2: the edge colour, 4 kinds of resources, 8 variants each
#define CHAMPION_FIELD 18
#define CHAMPION_BG 16
#define BG_ENTRY_SIZE 44 // an arena of file 0: light, seasonal, fieldIds[20], stageIds[20], 2 more
#define FIELD_ROW_SIZE 64 // a field of file 1: 4 kinds of resources, 8 variants each, 2 bytes each

typedef struct Archive {
    u8 *data;
    u32 size;
    u32 count;
    u32 *start; // of each member, from the start of the file
    u32 *end;
} Archive;

static void Write32(u8 *p, u32 value)
{
    p[0] = value & 0xFF;
    p[1] = (value >> 8) & 0xFF;
    p[2] = (value >> 16) & 0xFF;
    p[3] = (value >> 24) & 0xFF;
}

static u8 *ReadWhole(const char *path, u32 *sizeOut)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    u8 *data = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && size < 16 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            data = malloc(size);
            if (data != NULL && fread(data, 1, size, f) == (size_t)size) {
                *sizeOut = (u32)size;
            } else {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(f);
    return data;
}

// Written over the file that is there, if any, so that BW2 keeps reading the one it opened
static BOOL WriteWhole(const char *path, const u8 *data, u32 size)
{
    FILE *f = fopen(path, "r+b");
    if (f == NULL) {
        f = fopen(path, "wb");
    }
    if (f == NULL) {
        return FALSE;
    }
    BOOL ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

static void ArchiveClose(Archive *archive)
{
    free(archive->data);
    free(archive->start);
    free(archive->end);
    memset(archive, 0, sizeof(*archive));
}

// A NARC's members (BTAF's table of start and end, from GMIF's data)
static BOOL ArchiveOpen(Archive *archive, const char *path)
{
    memset(archive, 0, sizeof(*archive));
    archive->data = ReadWhole(path, &archive->size);
    if (archive->data == NULL || archive->size < 0x30 || memcmp(archive->data, "NARC", 4) != 0
        || memcmp(archive->data + 0x10, "BTAF", 4) != 0) {
        return FALSE;
    }
    u32 btafSize = Read32(archive->data + 0x14);
    archive->count = archive->data[0x18] | (archive->data[0x19] << 8);
    u32 btnf = 0x10 + btafSize;
    if (btnf + 8 > archive->size || memcmp(archive->data + btnf, "BTNF", 4) != 0) {
        return FALSE;
    }
    u32 gmif = btnf + Read32(archive->data + btnf + 4);
    if (gmif + 8 > archive->size || memcmp(archive->data + gmif, "GMIF", 4) != 0) {
        return FALSE;
    }
    archive->start = malloc(archive->count * sizeof(u32));
    archive->end = malloc(archive->count * sizeof(u32));
    for (u32 i = 0; i < archive->count; i++) {
        archive->start[i] = gmif + 8 + Read32(archive->data + 0x1C + 8 * i);
        archive->end[i] = gmif + 8 + Read32(archive->data + 0x1C + 8 * i + 4);
        if (archive->end[i] > archive->size || archive->start[i] > archive->end[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

// The archive with some members replaced (count of them: index, data and size each)
static u8 *ArchiveWithMembers(const Archive *archive, u32 count, const u32 *indices, const u8 *const *data, const u32 *sizes, u32 *outSize)
{
    const u32 btaf = 0x10;
    const u32 btnf = btaf + Read32(archive->data + btaf + 4);
    const u32 gmif = btnf + Read32(archive->data + btnf + 4);
    const u32 gmifData = gmif + 8;
    u32 total = gmifData;
    for (u32 i = 0; i < archive->count; i++) {
        u32 length = archive->end[i] - archive->start[i];
        for (u32 k = 0; k < count; k++) {
            if (indices[k] == i) {
                length = sizes[k];
            }
        }
        total += (length + 3) & ~3u;
    }
    u8 *out = calloc(1, total);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, archive->data, gmifData);
    u32 position = 0;
    for (u32 i = 0; i < archive->count; i++) {
        const u8 *from = archive->data + archive->start[i];
        u32 length = archive->end[i] - archive->start[i];
        for (u32 k = 0; k < count; k++) {
            if (indices[k] == i) {
                from = data[k];
                length = sizes[k];
            }
        }
        memcpy(out + gmifData + position, from, length);
        Write32(out + 0x1C + 8 * i, position);
        Write32(out + 0x1C + 8 * i + 4, position + length);
        position += (length + 3) & ~3u;
    }
    Write32(out + 8, total);
    Write32(out + gmif + 4, 8 + position);
    *outSize = total;
    return out;
}

// The archive with members added at the end (BTAF's table grows with them)
static u8 *ArchiveWithAdded(const Archive *archive, u32 count, const u8 *const *data, const u32 *sizes, u32 *outSize)
{
    const u32 btaf = 0x10;
    const u32 btnf = btaf + Read32(archive->data + btaf + 4);
    const u32 gmif = btnf + Read32(archive->data + btnf + 4);
    const u32 btnfSize = gmif - btnf;
    const u32 total = archive->count + count;
    const u32 btafSize = 12 + 8 * total;
    const u32 gmifData = btaf + btafSize + btnfSize + 8;
    u32 dataSize = 0;
    for (u32 i = 0; i < archive->count; i++) {
        dataSize += (archive->end[i] - archive->start[i] + 3) & ~3u;
    }
    for (u32 k = 0; k < count; k++) {
        dataSize += (sizes[k] + 3) & ~3u;
    }
    u8 *out = calloc(1, gmifData + dataSize);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, archive->data, btaf); // the NARC's header
    memcpy(out + btaf, "BTAF", 4);
    Write32(out + btaf + 4, btafSize);
    out[btaf + 8] = total & 0xFF;
    out[btaf + 9] = total >> 8;
    memcpy(out + btaf + btafSize, archive->data + btnf, btnfSize);
    memcpy(out + btaf + btafSize + btnfSize, "GMIF", 4);
    Write32(out + btaf + btafSize + btnfSize + 4, 8 + dataSize);
    u32 position = 0;
    for (u32 i = 0; i < total; i++) {
        const u8 *from;
        u32 length;
        if (i < archive->count) {
            from = archive->data + archive->start[i];
            length = archive->end[i] - archive->start[i];
        } else {
            from = data[i - archive->count];
            length = sizes[i - archive->count];
        }
        memcpy(out + gmifData + position, from, length);
        Write32(out + btaf + 12 + 8 * i, position);
        Write32(out + btaf + 12 + 8 * i + 4, position + length);
        position += (length + 3) & ~3u;
    }
    Write32(out + 8, gmifData + dataSize);
    *outSize = gmifData + dataSize;
    return out;
}

// The model resource a field's row (of file 1) names first
static u32 Read16Table(const u8 *fields, u32 field)
{
    return fields[field * FIELD_ROW_SIZE] | (fields[field * FIELD_ROW_SIZE + 1] << 8);
}

// --- The Safari Zone's labels: the Platinum fork's src/port/gen5_data.c (MakeSafariButtons and its helpers), unchanged.
// They go in after the arena's two models, at the members btlv_input.c's patch asks for, as in Platinum's copy


// The Safari Zone's command screen labels: BALL, BAIT and ROCK where FIGHT!, BAG and POKÉMON are. The labels are tiles of
// archive 11 (the tile file 358, the maps 361 to 371 of BG 4 with BAG's and POKÉMON's, the map 372 of BG 5 with FIGHT!'s). The
// copy gets that tile file and those maps again as new members (574 to 586) whose label cells point at new tiles (ids 400 on, free
// in the file) with the port's own 5 by 5 letters; btlv_input.c's patch asks for them in a Safari battle (BtlvInput_FileId).
#define SAFARI_TILE_FILE 358
#define SAFARI_MAP_FIRST 361
#define SAFARI_MAP_COUNT 11
#define SAFARI_FIGHT_MAP 372
#define SAFARI_MEMBERS (2 + SAFARI_MAP_COUNT)
#define SAFARI_TILE_BASE 400
#define NCGR_TILES_AT 48 // the tile data's place in the file
#define NSCR_CELLS_AT 36 // the map's
#define MAP_COLUMNS 64

static const char *const sSafariGlyphs[][5] = {
    { "ffff.", "f...f", "ffff.", "f...f", "ffff." }, // B
    { ".fff.", "f...f", "fffff", "f...f", "f...f" }, // A
    { "f....", "f....", "f....", "f....", "fffff" }, // L
    { "fffff", "..f..", "..f..", "..f..", "fffff" }, // I
    { "fffff", "..f..", "..f..", "..f..", "..f.." }, // T
    { "ffff.", "f...f", "ffff.", "f..f.", "f...f" }, // R
    { ".fff.", "f...f", "f...f", "f...f", ".fff." }, // O
    { ".ffff", "f....", "f....", "f....", ".ffff" }, // C
    { "f...f", "f..f.", "fff..", "f..f.", "f...f" }, // K
};

static int GlyphOf(char letter)
{
    static const char order[] = "BALITROCK";
    const char *at = strchr(order, letter);
    return at != NULL ? (int)(at - order) : 0;
}

// A tile of 8 by 8 4-bit pixels. The letters are 9 rows tall: yBase is the letter's row at the tile's row 0 (-3 in the upper cells,
// where the letters start at row 3, 5 in the lower), width its width in pixels (the 5 by 5 glyphs, stretched)
static void SafariTile(u8 *tile, int lineColour, int lineRow, int colour, const char *word, int pitch, int start, int cell,
    int background, int yBase, int width)
{
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int value = background;
            if (y == lineRow) {
                value = lineColour;
            }
            const int strip = cell * 8 + x - start;
            const int row = y + yBase;
            if (word != NULL && row >= 0 && row < 9 && strip >= 0) {
                const int letter = strip / pitch;
                const int column = strip % pitch;
                if (word[letter] != '\0' && column < width
                    && sSafariGlyphs[GlyphOf(word[letter])][row * 5 / 9][column * 5 / width] == 'f') {
                    value = colour;
                }
            }
            u8 *byte = tile + y * 4 + x / 2;
            *byte = (x & 1) ? (u8)((*byte & 0x0F) | (value << 4)) : (u8)((*byte & 0xF0) | value);
        }
    }
}

static u32 SafariRead16(const u8 *p)
{
    return p[0] | (p[1] << 8);
}

static u32 NextTile;

// Makes the members: [0] the tile file, [1 to 11] the maps 361 to 371, [12] the map 372. Each is allocated, or all NULL
static BOOL MakeSafariButtons(const Archive *a, u8 **out, u32 *outSize)
{
    BOOL ok = a->count > SAFARI_FIGHT_MAP;
    for (int i = 0; i < SAFARI_MEMBERS; i++) {
        out[i] = NULL;
        const u32 member = i == 0 ? SAFARI_TILE_FILE : i <= SAFARI_MAP_COUNT ? SAFARI_MAP_FIRST + i - 1 : SAFARI_FIGHT_MAP;
        if (ok) {
            outSize[i] = a->end[member] - a->start[member];
            out[i] = malloc(outSize[i]);
            ok = out[i] != NULL && outSize[i] >= 8000;
            if (ok) {
                memcpy(out[i], a->data + a->start[member], outSize[i]);
            }
        }
    }
    if (!ok || outSize[0] < NCGR_TILES_AT + 1024 * 32) {
        for (int i = 0; i < SAFARI_MEMBERS; i++) {
            free(out[i]);
            out[i] = NULL;
        }
        return FALSE;
    }
    u8 *tiles = out[0] + NCGR_TILES_AT;
    NextTile = SAFARI_TILE_BASE;
    // The plain button face (a line of colour 15 at the top, 1 below), BAG's and POKÉMON's; FIGHT!'s row of letters (line 9 at the
    // top) and the reflection below it (line 9 at the bottom, 1 above)
    NextTile++; // the blank tile
    NextTile++; // 401, unused
    memset(tiles + SAFARI_TILE_BASE * 32, 0, 32); // the blank tile (400): the labels' second halves go
    // BAG's cells (4, ids 322 to 325 in a row), POKÉMON's (8, 332 to 338 with 334 twice) and FIGHT!'s (8, 378 to 385 in a row, the
    // cells below them a reflection)
    static const char *const words[3] = { "BAIT", "ROCK", "BALL" };
    u32 lowerTile[3][8];
    static const struct {
        u32 first, count;
    } groups[3] = { { 322, 4 }, { 332, 8 }, { 378, 8 } };
    for (int g = 0; g < 3; g++) {
        const BOOL fight = g == 2;
        const int pitch = g == 0 ? 8 : 11;
        const int start = g == 0 ? 1 : 12;
        const int width = g == 0 ? 6 : 7;
        u32 cellTile[8];
        const int letterColour = fight ? 9 : 15;
        for (u32 c = 0; c < groups[g].count; c++) {
            cellTile[c] = NextTile++;
            SafariTile(tiles + cellTile[c] * 32, fight ? 9 : 15, 0, letterColour, words[g], pitch, start, (int)c, 1, -3, width);
            lowerTile[g][c] = NextTile++;
            SafariTile(tiles + lowerTile[g][c] * 32, fight ? 9 : 15, 7, letterColour, words[g], pitch, start, (int)c, 1, 5, width);
        }
        for (int m = fight ? SAFARI_MEMBERS - 1 : 1; m < (fight ? SAFARI_MEMBERS : SAFARI_MEMBERS - 1); m++) {
            u8 *cells = out[m] + NSCR_CELLS_AT;
            const u32 cellCount = (outSize[m] - NSCR_CELLS_AT) / 2;
            for (u32 i = 0; i + groups[g].count <= cellCount; i++) {
                BOOL match = TRUE;
                for (u32 c = 0; c < groups[g].count && match; c++) {
                    const u32 id = SafariRead16(cells + (i + c) * 2) & 0x3FF;
                    if (g == 0) {
                        match = id == 322 + c;
                    } else if (g == 1) {
                        static const u16 pokemon[8] = { 332, 333, 334, 335, 336, 337, 334, 338 };
                        match = id == pokemon[c];
                    } else {
                        match = id == 378 + c;
                    }
                }
                if (!match) {
                    continue;
                }
                for (u32 c = 0; c < groups[g].count; c++) {
                    u8 *cell = cells + (i + c) * 2;
                    const u16 entry = (u16)(SafariRead16(cell) & 0xF000); // the palette stays
                    const u16 now = (u16)(entry | cellTile[c]);
                    cell[0] = now & 0xFF;
                    cell[1] = now >> 8;
                    if (fight) {
                        u8 *below = cell + MAP_COLUMNS * 2;
                        const u16 under = (u16)((SafariRead16(below) & 0xF000) | SAFARI_TILE_BASE);
                        below[0] = under & 0xFF;
                        below[1] = under >> 8;
                    }
                }
                i += groups[g].count - 1;
            }
        }
    }
    // The labels' second halves: each label is drawn twice (BG 4's, and FIGHT!'s on BG 5), the upper copy by the cells above and the
    // lower by these, which the screen shows 4 rows below; they get the letters' lower rows
    static const struct {
        BOOL fight;
        u32 count;
        u16 ids[8];
    } lower[3] = {
        { FALSE, 4, { 322, 339, 340, 341 } },
        { FALSE, 8, { 332, 346, 334, 335, 347, 348, 334, 349 } },
        { TRUE, 8, { 378, 386, 380, 387, 382, 388, 389, 390 } },
    };
    for (int g = 0; g < 3; g++) {
        for (int m = lower[g].fight ? SAFARI_MEMBERS - 1 : 1; m < (lower[g].fight ? SAFARI_MEMBERS : SAFARI_MEMBERS - 1); m++) {
            u8 *cells = out[m] + NSCR_CELLS_AT;
            const u32 cellCount = (outSize[m] - NSCR_CELLS_AT) / 2;
            for (u32 i = 0; i + lower[g].count <= cellCount; i++) {
                BOOL match = TRUE;
                for (u32 c = 0; c < lower[g].count && match; c++) {
                    match = (SafariRead16(cells + (i + c) * 2) & 0x3FF) == lower[g].ids[c];
                }
                for (u32 c = 0; match && c < lower[g].count; c++) {
                    u8 *cell = cells + (i + c) * 2;
                    const u16 now = (u16)((SafariRead16(cell) & 0xF000) | lowerTile[g == 0 ? 0 : g == 1 ? 1 : 2][c]);
                    cell[0] = now & 0xFF;
                    cell[1] = now >> 8;
                }
                if (match) {
                    i += lower[g].count - 1;
                }
            }
        }
    }
    return TRUE;
}

static BOOL MakeRocketArena(const char *bw2Dir)
{
    char path[512];
    Archive models, arenas;
    memset(&arenas, 0, sizeof(arenas));
    snprintf(path, sizeof(path), "%s/a/0/1/1", bw2Dir);
    BOOL ok = ArchiveOpen(&models, path);
    snprintf(path, sizeof(path), "%s/a/1/5/1", bw2Dir);
    ok = ok && ArchiveOpen(&arenas, path);
    u8 *model = NULL;
    u8 *bgs = NULL;
    u8 *fields = NULL;
    u8 *stageModel = NULL;
    u8 *stages = NULL;
    u8 *modelsFile = NULL;
    u8 *arenasFile = NULL;
    u8 *safariButtons[SAFARI_MEMBERS] = { NULL };
    u32 modelSize = 0, bgsSize = 0, fieldsSize = 0, modelsFileSize = 0, arenasFileSize = 0;
    u32 newModel = 0, newField = 0, stagesSize = 0, newStage = 0, stageModelSize = 0;

    if (ok && arenas.count >= 3 && models.count > CHAMPION_MODEL && models.count > STAGE_MODEL) {
        modelSize = models.end[CHAMPION_MODEL] - models.start[CHAMPION_MODEL];
        bgsSize = arenas.end[0] - arenas.start[0];
        fieldsSize = arenas.end[1] - arenas.start[1];
        newModel = models.count;
        newField = fieldsSize / FIELD_ROW_SIZE;
        stageModelSize = models.end[STAGE_MODEL] - models.start[STAGE_MODEL];
        stagesSize = arenas.end[2] - arenas.start[2];
        newStage = stagesSize / STAGE_ROW_SIZE; // the file ends with 2 bytes more than its rows
    } else {
        ok = FALSE;
    }
    if (ok && (bgsSize < (CHAMPION_BG + 1) * BG_ENTRY_SIZE || fieldsSize % FIELD_ROW_SIZE != 0 || newField <= CHAMPION_FIELD
            || newField > 255 || newStage <= CHAMPION_STAGE || newStage > 255)) {
        ok = FALSE;
    }
    if (ok) {
        model = malloc(modelSize);
        bgs = malloc(bgsSize);
        fields = malloc(fieldsSize + FIELD_ROW_SIZE);
        stageModel = malloc(stageModelSize);
        stages = malloc(newStage * STAGE_ROW_SIZE + STAGE_ROW_SIZE);
        ok = model != NULL && bgs != NULL && fields != NULL && stageModel != NULL && stages != NULL;
    }
    if (ok) {
        memcpy(model, models.data + models.start[CHAMPION_MODEL], modelSize);
        memcpy(bgs, arenas.data + arenas.start[0], bgsSize);
        memcpy(fields, arenas.data + arenas.start[1], fieldsSize);
        // The new field's row: the Champion room's, its model the new one
        memcpy(fields + fieldsSize, fields + CHAMPION_FIELD * FIELD_ROW_SIZE, FIELD_ROW_SIZE);
        fields[fieldsSize] = newModel & 0xFF;
        fields[fieldsSize + 1] = newModel >> 8;
        // Only a Champion room made of that model, with its stage's models (stage ids stay)
        u8 *entry = bgs + CHAMPION_BG * BG_ENTRY_SIZE;
        ok = entry[2 + 0] == CHAMPION_FIELD && Read16Table(fields, CHAMPION_FIELD) == CHAMPION_MODEL && Gen5Rocket_Make(model, modelSize);
        entry[2 + GEN5_ROCKET_TERRAIN] = (u8)newField;
        // The new stage: the Champion room's (21), its grounds' model (38, one for both) a dark copy
        memcpy(stageModel, models.data + models.start[STAGE_MODEL], stageModelSize);
        memcpy(stages, arenas.data + arenas.start[2], newStage * STAGE_ROW_SIZE);
        memcpy(stages + newStage * STAGE_ROW_SIZE, stages + CHAMPION_STAGE * STAGE_ROW_SIZE, STAGE_ROW_SIZE);
        u8 *row = stages + newStage * STAGE_ROW_SIZE;
        ok = ok && row[2] == STAGE_MODEL && row[3] == 0 && row[4] == STAGE_MODEL && row[5] == 0
            && Gen5Rocket_MakeStage(stageModel, stageModelSize);
        row[2] = row[4] = (u8)((newModel + 1) & 0xFF);
        row[3] = row[5] = (u8)((newModel + 1) >> 8);
        entry[2 + 20 + GEN5_ROCKET_TERRAIN] = (u8)newStage;
    }
    if (ok) {
        // The arena's two models, then the Safari Zone's labels (574 to 586)
        const u8 *added[2 + SAFARI_MEMBERS] = { model, stageModel };
        u32 addedSize[2 + SAFARI_MEMBERS] = { modelSize, stageModelSize };
        u32 safariSize[SAFARI_MEMBERS];
        ok = MakeSafariButtons(&models, safariButtons, safariSize);
        for (int i = 0; ok && i < SAFARI_MEMBERS; i++) {
            added[2 + i] = safariButtons[i];
            addedSize[2 + i] = safariSize[i];
        }
        const u8 *replaced[3] = { bgs, fields, stages };
        const u32 replacedSize[3] = { bgsSize, fieldsSize + FIELD_ROW_SIZE, (newStage + 1) * STAGE_ROW_SIZE };
        const u32 indices[3] = { 0, 1, 2 };
        modelsFile = ok ? ArchiveWithAdded(&models, 2 + SAFARI_MEMBERS, added, addedSize, &modelsFileSize) : NULL;
        arenasFile = ArchiveWithMembers(&arenas, 3, indices, replaced, replacedSize, &arenasFileSize);
        ok = modelsFile != NULL && arenasFile != NULL;
    }
    if (ok) {
        mkdir(DIR, 0755);
        mkdir(DIR "/a", 0755);
        mkdir(DIR "/a/0", 0755);
        mkdir(DIR "/a/0/1", 0755);
        mkdir(DIR "/a/1", 0755);
        mkdir(DIR "/a/1/5", 0755);
        remove(BTLV_PATH);
        remove(BG_DATA_PATH);
        ok = WriteWhole(BTLV_PATH, modelsFile, modelsFileSize) && WriteWhole(BG_DATA_PATH, arenasFile, arenasFileSize);
    }
    free(model);
    free(bgs);
    free(fields);
    free(stageModel);
    free(stages);
    free(modelsFile);
    free(arenasFile);
    for (int i = 0; i < SAFARI_MEMBERS; i++) {
        free(safariButtons[i]);
    }
    ArchiveClose(&models);
    ArchiveClose(&arenas);
    return ok;
}

// BW2's thrown balls are a sheet of frames and a palette each (archive 94: ball * 2 + 231 and + 232; 17 is the Dream
// Ball), none for HGSS's Apricorn Balls, and HGSS's own sheets are another size (13 frames of 16x16 to BW2's 17 of
// 32x32). So an Apricorn Ball is drawn with BW2's Poke Ball frames in a palette of its own, added to a copy of the
// archive (from member 267, in ball order): the Poke Ball's six reds (colours 15, 1 to 5, dark to light) become shades
// of the Apricorn Ball's main colour, the commonest coloured pixel of the top half of its first frame in HGSS's own
// sheet (a/0/0/8: sheet 0x105 + 3 * (ball - 1), LZ-compressed, palette 0x52 + ball). BW2 throws and opens these balls
// with its own art of them (its particles); the palette is for the frames after, the ball shaking on the ground
static u16 BlendColor(u16 a, u16 b, int weightB, int total)
{
    int out = 0;
    for (int shift = 0; shift <= 10; shift += 5) {
        const int ca = (a >> shift) & 31, cb = (b >> shift) & 31;
        out |= ((ca * (total - weightB) + cb * weightB) / total) << shift;
    }
    return (u16)out;
}

// An LZ10-compressed file (the DS BIOS's), or the file itself when it isn't; free it when not the same
static u8 *Uncompress(u8 *data, u32 size, u32 *outSize)
{
    if (size < 4 || data[0] != 0x10) {
        *outSize = size;
        return data;
    }
    const u32 length = data[1] | (data[2] << 8) | (data[3] << 16);
    u8 *out = malloc(length);
    u32 in = 4, n = 0;
    while (n < length && in < size) {
        u8 flags = data[in++];
        for (int bit = 0; bit < 8 && n < length && in < size; bit++, flags <<= 1) {
            if (flags & 0x80) {
                if (in + 1 >= size) {
                    break;
                }
                const u32 count = (data[in] >> 4) + 3;
                const u32 back = (((data[in] & 0xF) << 8) | data[in + 1]) + 1;
                in += 2;
                for (u32 k = 0; k < count && n < length; k++, n++) {
                    out[n] = n >= back ? out[n - back] : 0;
                }
            } else {
                out[n++] = data[in++];
            }
        }
    }
    *outSize = length;
    return out;
}

// The commonest coloured (not grey, white or black) colour of the top half of a ball's first 16x16 frame (tiles 0 and
// 1 of its sheet, 4 bits a pixel)
static u16 MainColor(u8 *sheet, u32 sheetSize, const u16 *colors)
{
    u32 size = 0;
    u8 *file = Uncompress(sheet, sheetSize, &size);
    u16 best = colors[6];
    if (size >= 0x30 && memcmp(file + 0x10, "RAHC", 4) == 0) {
        const u32 offset = 0x18 + Read32(file + 0x2C); // the tiles: from the RAHC block (0x10), past its header
        int counts[16] = { 0 };
        for (u32 i = 0; i < 64 && offset + i < size; i++) {
            for (int half = 0; half < 2; half++) {
                const int c = (file[offset + i] >> (4 * half)) & 0xF;
                const int r = colors[c] & 31, g = (colors[c] >> 5) & 31, b = (colors[c] >> 10) & 31;
                const int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
                const int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
                if (c != 0 && hi - lo >= 4) {
                    counts[c]++;
                }
            }
        }
        int top = 0;
        for (int c = 1; c < 16; c++) {
            if (counts[c] > counts[top]) {
                top = c;
            }
        }
        if (counts[top] > 0) {
            best = colors[top];
        }
    }
    if (file != sheet) {
        free(file);
    }
    return best;
}

// A palette file's 16 colours (its TTLP block's data)
static u16 *PaletteColors(u8 *file, u32 size)
{
    for (u32 i = 0; i + 24 <= size; i += 4) {
        if (memcmp(file + i, "TTLP", 4) == 0) {
            const u32 offset = i + 8 + Read32(file + i + 20);
            return offset + 32 <= size ? (u16 *)(file + offset) : NULL;
        }
    }
    return NULL;
}

static BOOL MakeApricornBalls(const char *bw2Dir)
{
    char path[512];
    Archive balls, hgss;
    snprintf(path, sizeof(path), "%s/a/0/9/4", bw2Dir);
    BOOL ok = ArchiveOpen(&balls, path);
    // HGSS's own data, from the game's folder
    ok = ArchiveOpen(&hgss, "a/0/0/8") && ok;
    const u32 pokePalette = BALL_POKE * 2 + 232;
    u8 *palettes[APRICORN_BALLS] = { NULL };
    u32 sizes[APRICORN_BALLS];
    if (ok && balls.count == 267 && hgss.count > 0x6A) {
        const u32 size = balls.end[pokePalette] - balls.start[pokePalette];
        for (int i = 0; i < APRICORN_BALLS && ok; i++) {
            palettes[i] = malloc(size);
            memcpy(palettes[i], balls.data + balls.start[pokePalette], size);
            sizes[i] = size;
            u16 *colors = PaletteColors(palettes[i], size);
            const u32 ball = BALL_FAST + i;
            u8 *hgFile = hgss.data + hgss.start[0x52 + ball];
            const u16 *hgColors = PaletteColors(hgFile, hgss.end[0x52 + ball] - hgss.start[0x52 + ball]);
            const u32 sheet = 0x105 + 3 * (ball - 1);
            if (colors == NULL || hgColors == NULL || sheet >= hgss.count) {
                ok = FALSE;
                break;
            }
            const u16 main = MainColor(hgss.data + hgss.start[sheet], hgss.end[sheet] - hgss.start[sheet], hgColors);
            // BW2's Poke Ball's reds, darkest to lightest: two thirds, three quarters, five sixths, all, then towards
            // white by a third and two thirds
            Put16(&colors[15], BlendColor(main, 0, 1, 2));
            Put16(&colors[1], BlendColor(main, 0, 1, 4));
            Put16(&colors[2], BlendColor(main, 0, 1, 8));
            Put16(&colors[3], main);
            Put16(&colors[4], BlendColor(main, 0x7FFF, 1, 3));
            Put16(&colors[5], BlendColor(main, 0x7FFF, 2, 3));
        }
    } else {
        printf("Gen 5: the balls' archive has %u members, HGSS's a/0/0/8 %u (%s)\n", (unsigned)balls.count,
            (unsigned)hgss.count, ok ? "both read" : "not read");
        ok = FALSE;
    }
    if (ok) {
        u32 fileSize = 0;
        u8 *file = ArchiveWithAdded(&balls, APRICORN_BALLS, (const u8 *const *)palettes, sizes, &fileSize);
        mkdir(DIR, 0755);
        mkdir(DIR "/a", 0755);
        mkdir(DIR "/a/0", 0755);
        mkdir(DIR "/a/0/9", 0755);
        remove(BALLS_PATH);
        ok = file != NULL && WriteWhole(BALLS_PATH, file, fileSize);
        free(file);
    }
    for (int i = 0; i < APRICORN_BALLS; i++) {
        free(palettes[i]);
    }
    ArchiveClose(&balls);
    ArchiveClose(&hgss);
    return ok;
}

BOOL Gen5Rocket_PrepareArena(void)
{
    static BOOL sRegistered;
    if (sRegistered) {
        return TRUE;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/a/0/1/1", Gen5Rom_Dir());
    u32 romSize = 0;
    {
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            return FALSE;
        }
        fseek(f, 0, SEEK_END);
        romSize = (u32)ftell(f);
        fclose(f);
    }
    char stamp[64];
    snprintf(stamp, sizeof(stamp), "%d %u\n", ROCKET_VERSION, romSize);
    u32 stampSize = 0;
    u8 *old = ReadWhole(ROCKET_STAMP_PATH, &stampSize);
    const BOOL current = old != NULL && stampSize == strlen(stamp) && memcmp(old, stamp, stampSize) == 0;
    free(old);
    if (!current) {
        if (!MakeRocketArena(Gen5Rom_Dir())) {
            printf("Gen 5: the Team Rocket arena couldn't be made: the Champion room is used\n");
            return FALSE;
        }
        if (!MakeApricornBalls(Gen5Rom_Dir())) {
            printf("Gen 5: the Apricorn Balls' palettes couldn't be made: they fly as a Poke Ball\n");
            remove(BALLS_PATH);
        }
        WriteWhole(ROCKET_STAMP_PATH, (const u8 *)stamp, (u32)strlen(stamp));
        printf("Gen 5: the Team Rocket arena made in %s\n", DIR);
    }
    Gen5_FilesOverride(BTLV_ARC, BTLV_PATH);
    Gen5_FilesOverride(BG_DATA_ARC, BG_DATA_PATH);
    {
        // The thrown balls with the Apricorn Balls' palettes, if they were made
        FILE *f = fopen(BALLS_PATH, "rb");
        if (f != NULL) {
            fclose(f);
            Gen5_FilesOverride(BALLS_ARC, BALLS_PATH);
            Gen5_SetApricornBallSprites(267);
        }
    }
    sRegistered = TRUE;
    return TRUE;
}

#endif // GEN5_BATTLES
