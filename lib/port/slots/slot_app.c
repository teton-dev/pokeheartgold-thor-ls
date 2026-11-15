// The Japanese Game Corner's slot machine (overlay 77 of the JP HeartGold/SoulSilver), rewritten as an app of the
// port. So far: the machine's two screens from the imported files and the three reels. A bets 3 coins and spins
// (a Replay's spin is free), A stops the reels one by one, where the rules (slot_logic.c) let them land, and a win
// pays its coins into the Coin Case; B leaves. The digits, sound, text and the bonus game come next.
#include "port_slot_app.h"

#include <stdlib.h>

#include <stdio.h>

#include "global.h"
#include "bg_window.h"
#include "coins.h"
#include "constants/heap.h"
#include "gf_gfx_planes.h"
#include "overlay_manager.h"
#include "port_game_settings.h"
#include "port_slots_rom.h"
#include "screen_fade.h"
#include "slot_assets.h"
#include "slot_logic.h"
#include "system.h"

// The reels: each one is a BG layer of its own (main 1 to 3) behind the face (main 0), whose reel windows are
// transparent. The JP game draws them as sprites (cells of a/1/2/7 file 6) at x = 88, 128, 168 and rows 24 px apart;
// here the same cells' tiles are written into the layer's tilemap and the layer scrolls.
#define REEL_COUNT 3
#define REEL_SYMBOL_HEIGHT 24
#define REEL_TURN (SLOT_REEL_LENGTH * REEL_SYMBOL_HEIGHT)
#define REEL_ROWS 6 // symbols written per reel: the 3 in the window and those half seen above and below
#define REEL_SPEED 8 // pixels a frame (a divisor of 24, so a stop lands on a symbol)
#define REEL_CELLS 36
#define REEL_PAL_BANK 9 // the symbols' palette banks 0 to 6 go to the main BG's 9 to 15 (the face uses 0 to 8)

typedef struct SlotReel {
    int pos; // pixels along the strip, 0 to REEL_TURN - 1; going down the strip, the symbols move down
    BOOL spinning;
    BOOL stopping;
    int target; // where it stops (pixels), once stopping
} SlotReel;

#define SLOT_BET 3 // the JP game's bet: always 3 coins a spin (JP state 2)

typedef struct SlotAppData {
    enum HeapID heapID;
    BgConfig *bgConfig;
    SlotNarc gfx;
    u16 *coins;
    uint8_t *overlay;
    SlotTables tables;
    u16 cellTile[REEL_CELLS];
    u8 cellPal[REEL_CELLS];
    SlotReel reels[REEL_COUNT];
    int nextStop; // the reel A stops next
    SlotGame game;
    BOOL freeSpin; // after a Replay
} SlotAppData;

static u32 sSlotLuck; // SlotLuckiness's value for the machine launched (the odds tables' type)

static const struct GraphicsModes sSlotModes = {
    GX_DISPMODE_GRAPHICS,
    GX_BGMODE_0,
    GX_BGMODE_0,
    GX_BG0_AS_2D,
};

static const GraphicsBanks sSlotBanks = {
    GX_VRAM_BG_128_B,
    GX_VRAM_BGEXTPLTT_NONE,
    GX_VRAM_SUB_BG_128_C,
    GX_VRAM_SUB_BGEXTPLTT_NONE,
    GX_VRAM_OBJ_128_A,
    GX_VRAM_OBJEXTPLTT_NONE,
    GX_VRAM_SUB_OBJ_16_I,
    GX_VRAM_SUB_OBJEXTPLTT_NONE,
    GX_VRAM_TEX_NONE,
    GX_VRAM_TEXPLTT_NONE,
};

// a/1/2/7: the touch screen's face (20 palette, 21 tiles, 22 screen) and the top screen's frame (8, 9, 10)
enum {
    GFX_FACE_PAL = 20,
    GFX_FACE_TILES = 21,
    GFX_FACE_SCREEN = 22,
    GFX_FRAME_PAL = 8,
    GFX_FRAME_TILES = 9,
    GFX_FRAME_SCREEN = 10,
    GFX_REEL_PAL = 4, // the reel symbols' sprite set: palette, tiles, cells
    GFX_REEL_TILES = 5,
    GFX_REEL_CELLS = 6,
};

static const u8 sReelLayers[REEL_COUNT] = { GF_BG_LYR_MAIN_1, GF_BG_LYR_MAIN_2, GF_BG_LYR_MAIN_3 };

BOOL SlotApp_IsEnabled(void) {
    return GameSettings_Get()->japaneseGameCorner && SlotsRom_State() == SLOTS_ROM_READY;
}

static void SlotApp_RenderingOff(void) {
    HBlankInterruptDisable();
    GfGfx_DisableEngineAPlanes();
    GfGfx_DisableEngineBPlanes();
    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);
}

static void SlotApp_VBlank(void *data) {
    SlotAppData *work = data;
    DoScheduledBgGpuUpdates(work->bgConfig);
    OS_SetIrqCheckFlag(OS_IE_V_BLANK);
}

static void SlotApp_InitLayer(SlotAppData *work, GFBgLayer layer, u32 screenBase, u32 charBase, u8 priority) {
    BgTemplate template = {
        .x = 0,
        .y = 0,
        .bufferSize = 0x0800,
        .baseTile = 0,
        .size = GF_BG_SCR_SIZE_256x256,
        .colorMode = GX_BG_COLORMODE_16,
        .screenBase = screenBase,
        .charBase = charBase,
        .bgExtPltt = GX_BG_EXTPLTT_01,
        .priority = priority,
        .areaOver = GX_BG_AREAOVER_XLU,
        .dummy = 0,
        .mosaic = FALSE,
    };
    InitBgFromTemplate(work->bgConfig, layer, &template, 0);
    BgClearTilemapBufferAndCommit(work->bgConfig, layer);
    BG_FillCharDataRange(work->bgConfig, layer, 0, 1, 0);
    ToggleBgLayer(layer, GF_PLANE_TOGGLE_ON);
}

// A palette, tiles and screen from a/1/2/7 onto a layer
static void SlotApp_ShowScreen(SlotAppData *work, GFBgLayer layer, enum GFPalLoadLocation location, int pal, int tiles, int screen) {
    SlotBlock palette = SlotGfx_Palette(SlotNarc_Member(&work->gfx, pal));
    SlotBlock chars = SlotGfx_Tiles(SlotNarc_Member(&work->gfx, tiles));
    SlotBlock map = SlotGfx_Screen(SlotNarc_Member(&work->gfx, screen));
    if (palette.size == 0 || chars.size == 0 || map.size == 0) {
        return;
    }
    BG_LoadPlttData(location, palette.data, palette.size, 0);
    BG_LoadCharTilesData(work->bgConfig, layer, chars.data, chars.size, 0);
    BG_LoadScreenTilemapData(work->bgConfig, layer, map.data, map.size);
    BgCommitTilemapBufferToVram(work->bgConfig, layer);
}

// The reels' symbols: the sprite set's palette and tiles, and where each cell's tiles start
static BOOL SlotApp_LoadReelGfx(SlotAppData *work) {
    SlotBlock palette = SlotGfx_Palette(SlotNarc_Member(&work->gfx, GFX_REEL_PAL));
    SlotBlock chars = SlotGfx_Tiles(SlotNarc_Member(&work->gfx, GFX_REEL_TILES));
    SlotBlock cells = SlotNarc_Member(&work->gfx, GFX_REEL_CELLS);
    if (palette.size < (16 - REEL_PAL_BANK) * 32 || chars.size == 0) {
        return FALSE;
    }
    for (int i = 0; i < REEL_CELLS; i++) {
        uint16_t tile;
        uint8_t pal;
        if (SlotGfx_ReelCell(cells, i, &tile, &pal) && pal < 16 - REEL_PAL_BANK) {
            work->cellTile[i] = tile;
            work->cellPal[i] = pal;
        } else {
            work->cellTile[i] = 0xFFFF; // drawn blank
        }
    }
    BG_LoadPlttData(GF_PAL_LOCATION_MAIN_BG, palette.data, (16 - REEL_PAL_BANK) * 32, REEL_PAL_BANK * 32);
    // From tile 1: tile 0 stays blank, for the rest of the layers (the reels share their tiles)
    BG_LoadCharTilesData(work->bgConfig, sReelLayers[0], chars.data, chars.size, 1);
    return TRUE;
}

// A reel's symbols around its position into its layer, and the layer's scroll for the pixels in between
static void SlotApp_DrawReel(SlotAppData *work, int reel) {
    const SlotReel *r = &work->reels[reel];
    const int first = r->pos / REEL_SYMBOL_HEIGHT, offset = r->pos % REEL_SYMBOL_HEIGHT;
    u16 map[REEL_ROWS * 3][4];
    for (int row = 0; row < REEL_ROWS; row++) {
        const int cell = Slot_StripSymbol(&work->tables, reel, first + row);
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 4; x++) {
                u16 entry = 0;
                if (cell < REEL_CELLS && work->cellTile[cell] != 0xFFFF) {
                    entry = (u16)((1 + work->cellTile[cell] + y * 4 + x) | (REEL_PAL_BANK + work->cellPal[cell]) << 12);
                }
                map[row * 3 + y][x] = entry;
            }
        }
    }
    // The column under the reel's window: centred on x = 88 + 40 * reel
    const u8 column = (u8)((88 + 40 * reel - 16) / 8);
    CopyToBgTilemapRect(work->bgConfig, sReelLayers[reel], column, 0, 4, REEL_ROWS * 3, map, 0, 0, 4, REEL_ROWS * 3);
    ScheduleBgTilemapBufferTransfer(work->bgConfig, sReelLayers[reel]);
    // Row 1 is the window's top symbol: on screen at y = 48 - (the pixels past it), the symbols in rows 24 px apart
    ScheduleSetBgPosText(work->bgConfig, sReelLayers[reel], BG_POS_OP_SET_Y, (offset - REEL_SYMBOL_HEIGHT) & 0xFF);
}

static BOOL SlotApp_Create(OverlayManager *man) {
    VoltorbFlipArgs *args = OverlayManager_GetArgs(man);
    SlotNarc gfx;
    uint32_t overlaySize;
    uint8_t *overlay = SlotAssets_ReadFile(SlotsRom_Dir(), "ov77.bin", &overlaySize);
    if (overlay == NULL || !SlotNarc_Open(&gfx, SlotsRom_Dir(), "a/1/2/7")) {
        free(overlay);
        return FALSE;
    }

    Main_SetVBlankIntrCB(NULL, NULL);
    GX_SetDispSelect(GX_DISP_SELECT_SUB_MAIN);
    SlotApp_RenderingOff();
    GraphicsBanks banks = sSlotBanks;
    GfGfx_SetBanks(&banks);

    Heap_Create(HEAP_ID_3, HEAP_ID_VOLTORB_FLIP, 0x50000);
    SlotAppData *work = OverlayManager_CreateAndGetData(man, sizeof(SlotAppData), HEAP_ID_VOLTORB_FLIP);
    MI_CpuFill8(work, 0, sizeof(SlotAppData));
    work->heapID = HEAP_ID_VOLTORB_FLIP;
    work->gfx = gfx;
    work->coins = args->coins;
    work->overlay = overlay;
    work->tables.data = overlay;
    work->tables.size = overlaySize;
    Slot_Init(&work->game, &work->tables, OS_GetTick() ^ (u32)(uintptr_t)work, (uint8_t)sSlotLuck, Coins_GetValue(work->coins));

    work->bgConfig = BgConfig_Alloc(work->heapID);
    const struct GraphicsModes modes = sSlotModes;
    SetBothScreensModesAndDisable(&modes);
    SlotApp_InitLayer(work, GF_BG_LYR_MAIN_0, GX_BG_SCRBASE_0x0000, GX_BG_CHARBASE_0x04000, 0);
    SlotApp_InitLayer(work, GF_BG_LYR_MAIN_1, GX_BG_SCRBASE_0x0800, GX_BG_CHARBASE_0x08000, 1);
    SlotApp_InitLayer(work, GF_BG_LYR_MAIN_2, GX_BG_SCRBASE_0x1000, GX_BG_CHARBASE_0x08000, 1);
    SlotApp_InitLayer(work, GF_BG_LYR_MAIN_3, GX_BG_SCRBASE_0x1800, GX_BG_CHARBASE_0x08000, 1);
    SlotApp_InitLayer(work, GF_BG_LYR_SUB_0, GX_BG_SCRBASE_0x0000, GX_BG_CHARBASE_0x04000, 1);
    SlotApp_ShowScreen(work, GF_BG_LYR_MAIN_0, GF_PAL_LOCATION_MAIN_BG, GFX_FACE_PAL, GFX_FACE_TILES, GFX_FACE_SCREEN);
    SlotApp_ShowScreen(work, GF_BG_LYR_SUB_0, GF_PAL_LOCATION_SUB_BG, GFX_FRAME_PAL, GFX_FRAME_TILES, GFX_FRAME_SCREEN);
    if (SlotApp_LoadReelGfx(work)) {
        for (int i = 0; i < REEL_COUNT; i++) {
            SlotApp_DrawReel(work, i);
        }
    }

    ResetVisibleHardwareWindows(PM_LCD_TOP);
    ResetVisibleHardwareWindows(PM_LCD_BOTTOM);
    Main_SetVBlankIntrCB(SlotApp_VBlank, work);
    return TRUE;
}

static BOOL SlotApp_Init(OverlayManager *man, int *state) {
    switch (*state) {
    case 0:
        if (!SlotApp_Create(man)) {
            return TRUE; // the files are gone: leave at once (Main sees no data)
        }
        *state += 1;
        break;
    case 1: {
        SlotAppData *work = OverlayManager_GetData(man);
        BeginNormalPaletteFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, RGB_BLACK, 6, 1, work->heapID);
        *state += 1;
        break;
    }
    case 2:
        if (IsPaletteFadeFinished()) {
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// A spin: 3 coins (or the Replay's free spin), the draw, the reels started
static BOOL SlotApp_StartSpin(SlotAppData *work) {
    SlotGame *g = &work->game;
    if (!work->freeSpin) {
        if (g->coins < SLOT_BET) {
            return FALSE;
        }
        g->coins -= SLOT_BET;
        *work->coins = (u16)g->coins;
    }
    work->freeSpin = FALSE;
    g->flags = 0;
    Slot_DrawSymbol(g);
    for (int i = 0; i < REEL_COUNT; i++) {
        g->reelStopped[i] = 0;
        work->reels[i].spinning = TRUE;
        work->reels[i].stopping = FALSE;
    }
    work->nextStop = 0;
    return TRUE;
}

// A stop pressed: the rules choose where, up to 4 symbols on
static void SlotApp_StopReel(SlotAppData *work, int reel) {
    SlotReel *r = &work->reels[reel];
    const int pressed = r->pos / REEL_SYMBOL_HEIGHT; // the next symbol to reach the top row
    const int index = Slot_ChooseStop(&work->game, reel, pressed);
    r->target = index * REEL_SYMBOL_HEIGHT;
    r->stopping = TRUE;
}

// The reels stand: what they show pays, or the machine's mode moves (the payout decision, JP state 10)
static void SlotApp_EndSpin(SlotAppData *work) {
    SlotGame *g = &work->game;
    int line = 0;
    const int win = Slot_Judge(g, &line);
    int paid = 0;
    if (win == 1) {
        work->freeSpin = TRUE; // Replay
    } else if (win > 1) {
        paid = Slot_WinPayout(g, (uint8_t)win);
        Slot_AddCoins(g, (uint32_t)paid);
        *work->coins = (u16)g->coins;
    } else {
        Slot_PayoutDecision(g);
        if (g->outcome < 3) {
            g->betIndex = g->outcome;
        }
    }
    printf("[slots] drew %d (class %d), reels %d %d %d, won %d on line %d: %d coins, %u left, mode %d\n",
        g->symbol, g->outcomeClass, g->reelStop[0], g->reelStop[1], g->reelStop[2], win, line, paid,
        (unsigned)g->coins, g->betIndex);
}

static BOOL SlotApp_Main(OverlayManager *man, int *state) {
    SlotAppData *work = OverlayManager_GetData(man);
    BOOL spinning = FALSE;
    for (int i = 0; i < REEL_COUNT; i++) {
        spinning |= work->reels[i].spinning;
    }
    if (!spinning) {
        if (gSystem.newKeys & PAD_BUTTON_B) {
            return TRUE;
        }
        if (gSystem.newKeys & PAD_BUTTON_A) {
            SlotApp_StartSpin(work);
        }
        return FALSE;
    }
    if ((gSystem.newKeys & PAD_BUTTON_A) && work->nextStop < REEL_COUNT) {
        SlotApp_StopReel(work, work->nextStop++);
    }
    for (int i = 0; i < REEL_COUNT; i++) {
        SlotReel *r = &work->reels[i];
        if (!r->spinning) {
            continue;
        }
        if (r->stopping && r->pos == r->target) {
            r->spinning = FALSE;
            continue;
        }
        r->pos = (r->pos + REEL_TURN - REEL_SPEED) % REEL_TURN;
        SlotApp_DrawReel(work, i);
    }
    if (!work->reels[0].spinning && !work->reels[1].spinning && !work->reels[2].spinning) {
        SlotApp_EndSpin(work);
    }
    return FALSE;
}

static BOOL SlotApp_Exit(OverlayManager *man, int *state) {
    SlotAppData *work = OverlayManager_GetData(man);
    switch (*state) {
    case 0:
        BeginNormalPaletteFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, RGB_BLACK, 6, 1, work->heapID);
        *state += 1;
        break;
    case 1:
        if (IsPaletteFadeFinished()) {
            Main_SetVBlankIntrCB(NULL, NULL);
            FreeBgTilemapBuffer(work->bgConfig, GF_BG_LYR_MAIN_0);
            for (int i = 0; i < REEL_COUNT; i++) {
                FreeBgTilemapBuffer(work->bgConfig, sReelLayers[i]);
            }
            FreeBgTilemapBuffer(work->bgConfig, GF_BG_LYR_SUB_0);
            Heap_Free(work->bgConfig);
            SlotNarc_Close(&work->gfx);
            free(work->overlay);
            SlotApp_RenderingOff();
            OverlayManager_FreeData(man);
            Heap_Destroy(HEAP_ID_VOLTORB_FLIP);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

VoltorbFlipArgs *SlotApp_LaunchApp(FieldSystem *fieldSystem, u32 luck) {
    static const OverlayManagerTemplate sTemplate = { SlotApp_Init, SlotApp_Main, SlotApp_Exit, FS_OVERLAY_ID_NONE };
    sSlotLuck = luck;
    VoltorbFlipArgs *args = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(VoltorbFlipArgs));
    MI_CpuFill8(args, 0, sizeof(VoltorbFlipArgs));
    SaveData *saveData = FieldSystem_GetSaveData(fieldSystem);
    args->coins = Save_PlayerData_GetCoinsAddr(saveData);
    args->options = Save_PlayerData_GetOptionsAddr(saveData);
    args->menuInputStatePtr = &fieldSystem->menuInputState;
    args->profile = Save_PlayerData_GetProfile(saveData);
    args->saveData = fieldSystem->saveData;
    FieldSystem_LaunchApplication(fieldSystem, &sTemplate, args);
    return args;
}
