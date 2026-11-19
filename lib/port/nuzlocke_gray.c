// Gray icons for dead Pokemon. See port_nuzlocke_gray.h
#include <nitro.h>

#include "global.h"

#include "obj_pltt_transfer.h"
#include "port_nuzlocke_gray.h"
#include "port_nuzlocke_mode.h"
#include "sprite.h"

BOOL NuzlockeGray_IsDead(Pokemon *mon) {
    return mon != NULL && NuzlockeMode_On() && NuzlockeMode_IsDead(mon);
}

BOOL NuzlockeGray_BoxIsDead(BoxPokemon *boxMon) {
    return boxMon != NULL && NuzlockeMode_On() && NuzlockeMode_BoxIsDead(boxMon);
}

// The gray a color becomes (0 to 31): its luminance, a little darker and flatter
static int Gray(u16 color) {
    const int r = color & 0x1F, g = (color >> 5) & 0x1F, b = (color >> 10) & 0x1F;
    return 4 + ((r * 77 + g * 151 + b * 28) >> 8) * 3 / 4;
}

void NuzlockeGray_Colors(u16 *colors, int count) {
    for (int i = 0; i < count; i++) {
        if (i % 16 == 0) {
            continue;
        }
        const int y = Gray(colors[i]);
        colors[i] = (colors[i] & 0x8000) | GX_RGB(y, y, y);
    }
}

// The gray copies made, so that each icon palette is copied once per screen
typedef struct GrayCopy {
    u32 generation;
    int type;
    int from;
    int gray;
} GrayCopy;
static GrayCopy sCopies[16];

static void Copy(BOOL sub, int from, int gray) {
    u16 *palettes = (u16 *)(sub ? HW_DB_OBJ_PLTT : HW_OBJ_PLTT);
    u16 colors[16];
    MI_CpuCopy16(palettes + from * 16, colors, sizeof(colors));
    NuzlockeGray_Colors(colors, 16);
    MI_CpuCopy16(colors, palettes + gray * 16, sizeof(colors));
}

// A palette's colors reach the hardware only when the game's transfer runs (a frame or more after a screen makes its
// sprites), and fades change them: the gray copies follow them each frame
void NuzlockeGray_Update(void) {
    const u32 generation = ObjPlttTransfer_PortGeneration();
    for (int i = 0; i < (int)NELEMS(sCopies); i++) {
        if (sCopies[i].generation == generation && sCopies[i].gray >= 0) {
            Copy(sCopies[i].type == NNS_G2D_VRAM_TYPE_2DSUB, sCopies[i].from, sCopies[i].gray);
        }
    }
}

BOOL NuzlockeGray_Sprite(Sprite *sprite, BOOL onSub) {
    NNSG2dImagePaletteProxy *proxy = Sprite_GetPaletteProxy(sprite);
    BOOL done = FALSE;
    for (int sub = onSub != FALSE; sub == (onSub != FALSE); sub++) {
        const NNS_G2D_VRAM_TYPE type = sub ? NNS_G2D_VRAM_TYPE_2DSUB : NNS_G2D_VRAM_TYPE_2DMAIN;
        if (!NNS_G2dIsImagePaletteReadyToUse(proxy, type)) {
            continue;
        }
        // The sprite's palette number is the slot (the override is absolute, whatever the proxy's location is)
        const int from = Sprite_GetPalIndex(sprite);
        const u32 generation = ObjPlttTransfer_PortGeneration();
        int gray = -1;
        GrayCopy *free = NULL;
        for (int i = 0; i < (int)NELEMS(sCopies); i++) {
            if (sCopies[i].generation == generation && sCopies[i].type == (int)type && sCopies[i].from == from) {
                gray = sCopies[i].gray;
            } else if (sCopies[i].generation != generation && free == NULL) {
                free = &sCopies[i];
            }
        }
        if (gray < 0) {
            gray = ObjPlttTransfer_PortReserveFreeSlots(1, type);
            if (gray < 0) {
                continue;
            }
            if (free != NULL) {
                *free = (GrayCopy) { generation, (int)type, from, gray };
            }
            printf("Nuzlocke gray: %s palette %d copied to slot %d\n", sub ? "sub" : "main", from, gray);
        }
        Copy(sub, from, gray);
        if (!done) {
            Sprite_SetPaletteOverride(sprite, gray);
        }
        done = TRUE;
    }
    return done;
}
