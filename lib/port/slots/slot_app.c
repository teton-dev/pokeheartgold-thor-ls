// The Japanese Game Corner's slot machine (overlay 77 of the JP HeartGold/SoulSilver), rewritten as an app of the
// port. This is the skeleton: it shows the machine's two screens from the imported files; the rules are in
// slot_logic.c and the reels, input and sound come next.
#include "port_slot_app.h"

#include "global.h"
#include "bg_window.h"
#include "constants/heap.h"
#include "gf_gfx_planes.h"
#include "overlay_manager.h"
#include "port_game_settings.h"
#include "port_slots_rom.h"
#include "screen_fade.h"
#include "slot_assets.h"
#include "system.h"

typedef struct SlotAppData {
    enum HeapID heapID;
    BgConfig *bgConfig;
    SlotNarc gfx;
    u16 *coins;
} SlotAppData;

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
};

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

static void SlotApp_InitLayer(SlotAppData *work, GFBgLayer layer, u32 screenBase, u32 charBase) {
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
        .priority = 1,
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

static BOOL SlotApp_Create(OverlayManager *man) {
    VoltorbFlipArgs *args = OverlayManager_GetArgs(man);
    SlotNarc gfx;
    if (!SlotNarc_Open(&gfx, SlotsRom_Dir(), "a/1/2/7")) {
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

    work->bgConfig = BgConfig_Alloc(work->heapID);
    const struct GraphicsModes modes = sSlotModes;
    SetBothScreensModesAndDisable(&modes);
    SlotApp_InitLayer(work, GF_BG_LYR_MAIN_0, GX_BG_SCRBASE_0x0000, GX_BG_CHARBASE_0x04000);
    SlotApp_InitLayer(work, GF_BG_LYR_SUB_0, GX_BG_SCRBASE_0x0000, GX_BG_CHARBASE_0x04000);
    SlotApp_ShowScreen(work, GF_BG_LYR_MAIN_0, GF_PAL_LOCATION_MAIN_BG, GFX_FACE_PAL, GFX_FACE_TILES, GFX_FACE_SCREEN);
    SlotApp_ShowScreen(work, GF_BG_LYR_SUB_0, GF_PAL_LOCATION_SUB_BG, GFX_FRAME_PAL, GFX_FRAME_TILES, GFX_FRAME_SCREEN);

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
        BeginNormalPaletteFade(FADE_BOTH_SCREENS, FADE_TYPE_UNK_21, FADE_TYPE_UNK_21, RGB_BLACK, 6, 1, work->heapID);
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

static BOOL SlotApp_Main(OverlayManager *man, int *state) {
    // A or B (or a touch) leaves, for now
    return (gSystem.newKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) != 0 || gSystem.touchNew;
}

static BOOL SlotApp_Exit(OverlayManager *man, int *state) {
    SlotAppData *work = OverlayManager_GetData(man);
    switch (*state) {
    case 0:
        BeginNormalPaletteFade(FADE_BOTH_SCREENS, FADE_TYPE_UNK_20, FADE_TYPE_UNK_20, RGB_BLACK, 6, 1, work->heapID);
        *state += 1;
        break;
    case 1:
        if (IsPaletteFadeFinished()) {
            Main_SetVBlankIntrCB(NULL, NULL);
            FreeBgTilemapBuffer(work->bgConfig, GF_BG_LYR_MAIN_0);
            FreeBgTilemapBuffer(work->bgConfig, GF_BG_LYR_SUB_0);
            Heap_Free(work->bgConfig);
            SlotNarc_Close(&work->gfx);
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
    (void)luck;
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
