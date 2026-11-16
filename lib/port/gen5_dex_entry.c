// The Pokédex entry HGSS's battle shows for a newly caught species (battle_command.c's catch sequence:
// PokedexEntryPopup_Create), after a battle in BW2's engine, whose own entry screen is in its field (not built). The
// battle sets up the display the popup draws into (the 3D Pokémon picture, palettes, BGs, OAM and its transfers); this
// does the same alone, as the evolution scene does after a battle (evolution_scene.c), then runs HGSS's popup unchanged:
// it fades in, plays the cry, and A or a touch closes it. The Platinum fork's src/port/gen5_dex_entry.c on HGSS's APIs.
#include "port_gen5_dex_entry.h"

#include <stdio.h>
#include <string.h>

#include "global.h"

#include "battle/battle.h"
#include "bg_window.h"
#include "font.h"
#include "gf_3d_vramman.h"
#include "gf_gfx_planes.h"
#include "heap.h"
#include "obj_char_transfer.h"
#include "overlay_18.h"
#include "palette.h"
#include "palette_vram_addr.h"
#include "particle_scene.h"
#include "pokedex_util.h"
#include "pokemon.h"
#include "pokepic.h"
#include "screen_fade.h"
#include "sound_playback.h"
#include "sprite_system.h"
#include "swap_3d_buffers.h"
#include "system.h"
#include "touchscreen.h"
#include "vram_transfer_manager.h"

#include "constants/sndseq.h"

// The battle's (battle_main.c)
static const OamManagerParam sOamManagerParam = {
    .fromOBJmain = 0,
    .numOBJmain = 128,
    .fromAffineMain = 0,
    .numAffineMain = 32,
    .fromOBJsub = 0,
    .numOBJsub = 128,
    .fromAffineSub = 0,
    .numAffineSub = 32,
};

static const OamCharTransferParam sOamCharTransferParam = {
    .maxTasks = 96,
    .sizeMain = 0x10000,
    .sizeSub = 0x4000,
    .charModeMain = GX_OBJVRAMMODE_CHAR_1D_64K,
    .charModeSub = GX_OBJVRAMMODE_CHAR_1D_32K,
};

// The evolution scene's (evolution_scene.c)
static const GraphicsBanks sBanks = {
    .bg = GX_VRAM_BG_128_C,
    .bgextpltt = GX_VRAM_BGEXTPLTT_NONE,
    .subbg = GX_VRAM_SUB_BG_32_H,
    .subbgextpltt = GX_VRAM_SUB_BGEXTPLTT_NONE,
    .obj = GX_VRAM_OBJ_64_E,
    .objextpltt = GX_VRAM_OBJEXTPLTT_NONE,
    .subobj = GX_VRAM_SUB_OBJ_16_I,
    .subobjextpltt = GX_VRAM_SUB_OBJEXTPLTT_NONE,
    .tex = GX_VRAM_TEX_01_AB,
    .texpltt = GX_VRAM_TEXPLTT_01_FG,
};

static const GraphicsModes sModes = {
    .dispMode = GX_DISPMODE_GRAPHICS,
    .bgMode = GX_BGMODE_0,
    .subMode = GX_BGMODE_0,
    ._2d3dMode = GX_BG0_AS_3D,
};

enum {
    DEX_ENTRY_SHOWING,
    DEX_ENTRY_FADING_OUT,
    DEX_ENTRY_DONE,
};

static struct {
    enum HeapID heapID;
    int state;
    BgConfig *bgConfig;
    PaletteData *paletteData;
    PokepicManager *pokepicManager;
    GF3DVramMan *vramMan;
    SpriteSystem *spriteSystem;
    ObjCharTransferTasksManager *charTasks;
    OverlayManager *popup;
} sDex;

static void VBlank(void *data) {
    (void)data;
    PokepicManager_HandleLoadImgAndOrPltt(sDex.pokepicManager);
    SpriteSystem_TransferOam();
    GF_RunVramTransferTasks();
    PaletteData_PushTransparentBuffers(sDex.paletteData);
    DoScheduledBgGpuUpdates(sDex.bgConfig);
    OS_SetIrqCheckFlag(OS_IE_V_BLANK);
}

BOOL Gen5DexEntry_Start(Pokemon *mon, const Pokedex *pokedex, enum HeapID heapID) {
    memset(&sDex, 0, sizeof(sDex));
    sDex.heapID = heapID;

    // The top screen as the main one, as the battle and the evolution scene have it
    gSystem.screensFlipped = FALSE;
    GfGfx_SwapDisplay();
    MI_CpuClear16(GetMainBgPlttAddr(), GetMainBgPlttSize());
    MI_CpuClear16(GetSubBgPlttAddr(), GetSubBgPlttSize());

    sDex.paletteData = PaletteData_Init(heapID);
    PaletteData_SetAutoTransparent(sDex.paletteData, TRUE);
    PaletteData_AllocBuffers(sDex.paletteData, PLTTBUF_MAIN_BG, 0x200, heapID);
    PaletteData_AllocBuffers(sDex.paletteData, PLTTBUF_SUB_BG, 0x200, heapID);
    PaletteData_AllocBuffers(sDex.paletteData, PLTTBUF_MAIN_OBJ, 0x1C0, heapID);
    PaletteData_AllocBuffers(sDex.paletteData, PLTTBUF_SUB_OBJ, 0x200, heapID);
    sDex.bgConfig = BgConfig_Alloc(heapID);
    sDex.vramMan = ParticleScene_CreateVramMan(heapID);
    ParticleScene_HidePlanes();
    ParticleScene_SetBlend();
    ParticleScene_ReserveVram();

    GfGfx_DisableEngineAPlanes();
    GraphicsBanks banks = sBanks;
    GfGfx_SetBanks(&banks);
    MI_CpuClear32((void *)HW_BG_VRAM, HW_BG_VRAM_SIZE);
    MI_CpuClear32((void *)HW_DB_BG_VRAM, HW_DB_BG_VRAM_SIZE);
    MI_CpuClear32((void *)HW_OBJ_VRAM, HW_OBJ_VRAM_SIZE);
    MI_CpuClear32((void *)HW_DB_OBJ_VRAM, HW_DB_OBJ_VRAM_SIZE);
    GraphicsModes modes = sModes;
    SetBothScreensModesAndDisable(&modes);

    // The OAM, palette and character transfers the battle has; the popup makes its own character transfer, so the
    // sprite system's is set aside meanwhile, as the battle does
    GF_CreateVramTransferManager(64, heapID);
    sDex.spriteSystem = SpriteSystem_Alloc(heapID);
    SpriteSystem_Init(sDex.spriteSystem, &sOamManagerParam, &sOamCharTransferParam, 32);
    sDex.charTasks = ObjCharTransfer_PopTaskManager();
    sDex.pokepicManager = PokepicManager_Create(heapID);
    // The small font the popup's texts are in, which the battle loads
    FontID_Alloc(4, heapID);

    GfGfx_BothDispOn();
    GfGfx_EngineATogglePlanes(GX_PLANEMASK_OBJ, GF_PLANE_TOGGLE_ON);
    // BW2's battle leaves both screens faded out by their master brightness; the popup fades in by its palettes
    SetMasterBrightnessNeutral(PM_LCD_TOP);
    SetMasterBrightnessNeutral(PM_LCD_BOTTOM);

    PokedexEntryPopupArgs args;
    args.bgConfig = sDex.bgConfig;
    args.paletteData = sDex.paletteData;
    args.pokepicManager = sDex.pokepicManager;
    args.heapID = heapID;
    args.mon = mon;
    args.natDexEnabled = Pokedex_IsNatDexEnabled(pokedex);
    sDex.popup = PokedexEntryPopup_Create(&args);
    sDex.state = DEX_ENTRY_SHOWING;
    Main_SetVBlankIntrCB(VBlank, NULL);
    printf("Gen 5: the Pokédex entry of species %u\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
    return TRUE;
}

BOOL Gen5DexEntry_Run(void) {
    switch (sDex.state) {
    case DEX_ENTRY_SHOWING:
        // Shown (faded in, its cry played): A or a touch closes it, as in the battle
        if (PokedexEntryPopup_IsDone(sDex.popup) == 1) {
            BOOL close = FALSE;
            if (gSystem.newKeys & PAD_BUTTON_A) {
                close = TRUE;
            } else if (System_GetTouchNew()) {
                PlaySE(SEQ_SE_DP_SELECT);
                close = TRUE;
            }
            if (close) {
                PaletteData_BeginPaletteFade(sDex.paletteData, PLTTBUF_MAIN_BG_F | PLTTBUF_MAIN_OBJ_F, 0xFFFF, 1, 0, 16, RGB_BLACK);
                Pokepic_StartPaletteFadeAll(sDex.pokepicManager, 0, 16, 0, RGB_BLACK);
                sDex.state = DEX_ENTRY_FADING_OUT;
            }
        }
        break;
    case DEX_ENTRY_FADING_OUT:
        if (!PaletteData_GetSelectedBuffersBitmask(sDex.paletteData)) {
            sDex.state = DEX_ENTRY_DONE;
        }
        break;
    default:
        break;
    }
    PokepicManager_DrawAll(sDex.pokepicManager);
    ParticleScene_DrawAndSwap();
    RequestSwap3DBuffers(GX_SORTMODE_MANUAL, GX_BUFFERMODE_Z);
    return sDex.state == DEX_ENTRY_DONE;
}

void Gen5DexEntry_Free(void) {
    Main_SetVBlankIntrCB(NULL, NULL);
    PokedexEntryPopup_Delete(sDex.popup);
    FontID_Release(4);
    ObjCharTransfer_PushTaskManager(sDex.charTasks);
    PokepicManager_Delete(sDex.pokepicManager);
    SpriteSystem_Free(sDex.spriteSystem);
    GF_DestroyVramTransferManager();
    GF_3DVramMan_Delete(sDex.vramMan);
    Heap_Free(sDex.bgConfig);
    PaletteData_FreeBuffers(sDex.paletteData, PLTTBUF_MAIN_BG);
    PaletteData_FreeBuffers(sDex.paletteData, PLTTBUF_SUB_BG);
    PaletteData_FreeBuffers(sDex.paletteData, PLTTBUF_MAIN_OBJ);
    PaletteData_FreeBuffers(sDex.paletteData, PLTTBUF_SUB_OBJ);
    PaletteData_Free(sDex.paletteData);
    SetMasterBrightnessByColor(PM_LCD_TOP, RGB_BLACK);
    SetMasterBrightnessByColor(PM_LCD_BOTTOM, RGB_BLACK);
    gSystem.screensFlipped = TRUE;
    GfGfx_SwapDisplay();
    memset(&sDex, 0, sizeof(sDex));
}
