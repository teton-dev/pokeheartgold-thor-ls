// Enhanced graphics in HGSS, the game's adapter: see include/port_sdk/port_enhanced_graphics.h
#include "port_enhanced_graphics.h"

#include <nitro.h>
#include <nnsys.h>
#include <port/enhanced_scene.h>

#include "constants/maps.h"
#include "field/area_data.h"
#include "field_system.h"
#include "field_terrain.h"
#include "gf_rtc.h"
#include "map_header.h"
#include "map_matrix.h"
#include "metatile_behavior.h"
#include "player_avatar.h"

// The forests' mist and sunlight. HGSS's camera is higher than Platinum's, so the mist is thinner than Eterna Forest's and
// has no distance fog (every ray is long here). Ilex Forest is not outdoors to the game's map type test: the sun's light
// and shadows stay off there, only these are drawn
typedef struct {
    u16 mapId;
    EnhancedFogLook fog;
    EnhancedSunPatchLook patches;
} ForestLook;

static const ForestLook sForests[] = {
    { MAP_ILEX_FOREST,
        { 0.72f, 0.80f, 0.76f, 0.20f, 0.25f, 0.34f, 0.010f, 5.0f, 0.0f, 0.0f, 40.0f, 0.85f, 5.0f, 1.5f },
        { 0.55f, 0.08f, 26.0f, 0.30f, 6.0f } },
};

// The lamps' textures in HGSS's area texture sets (prop textures, fielddata/areadata NARC a/0/7/0, found by name with
// their area's banks and checked on the screen at night; docs/thor/dev/enhanced-graphics.md)
static const EnhancedLampLook sLamps[] = {
    // Goldenrod's and Ecruteak's street lamps. Their globe is about 20 units across, wider than libntr groups a lamp's
    // points (16), so each is found as two lamps: half of Jubilife's colour, so the two give one lamp's light
    { "ko_light", 80.0f, 0.65f, 0.49f, 0.275f },
    // Candidates not yet looked at on the screen (a wrong entry lights things twice): kk_tourou_a, kk_tourou_b.1-3
    // (Violet's stone lanterns), ar_light, as_toudai1/2 (Olivine's lighthouse)
};

static const ForestLook *Forest(u32 mapId) {
    for (u32 i = 0; i < NELEMS(sForests); i++) {
        if (sForests[i].mapId == mapId) {
            return &sForests[i];
        }
    }
    return NULL;
}

void EnhancedGraphics_Update(FieldSystem *fieldSystem) {
    EnhancedSceneState state = { 0 };
    if (fieldSystem != NULL && fieldSystem->location != NULL) {
        RTCTime time;
        GF_RTC_CopyTime(&time);
        const u32 mapId = fieldSystem->location->mapId;
        const ForestLook *forest = Forest(mapId);
        state.field = TRUE;
        state.outdoors = MapHeader_IsOutdoors(mapId);
        state.hour = EnhancedScene_Hour(time.hour + time.minute / 60.0f, "POKEHEARTGOLD_ENHANCED_HOUR");
        state.fog = forest != NULL ? &forest->fog : NULL;
        state.patches = forest != NULL ? &forest->patches : NULL;
    }
    EnhancedScene_Update(&state);
}

void EnhancedGraphics_FieldDrawing(FieldSystem *fieldSystem) {
    if (fieldSystem == NULL || fieldSystem->playerAvatar == NULL || fieldSystem->areaDataManager == NULL) {
        EnhancedScene_FieldDrawing(0.0f, NULL, 0, NULL, 0);
    } else {
        const NNSG3dResTex *sets[2] = { AreaDataManager_GetMapTexture(fieldSystem->areaDataManager),
            AreaDataManager_GetMapPropTexture(fieldSystem->areaDataManager) };
        EnhancedScene_FieldDrawing(PlayerAvatar_GetPositionVector(fieldSystem->playerAvatar)->y / 4096.0f, sets,
            NELEMS(sets), sLamps, NELEMS(sLamps));
    }
    EnhancedScene_SetCamera(NNS_G3dGlbGetProjectionMtx(), NNS_G3dGlbGetCameraMtx());
}

typedef struct {
    FieldSystem *fieldSystem;
    fx32 playerHeight;
} WaterContext;

// Inside the map matrix only: the simple terrain's lookup has no bounds of its own
static BOOL IsWaterTile(void *context, int x, int z) {
    FieldSystem *fieldSystem = ((WaterContext *)context)->fieldSystem;
    if (fieldSystem->mapMatrix == NULL || x < 0 || z < 0 || x >= MapMatrix_GetWidth(fieldSystem->mapMatrix) * 32
        || z >= MapMatrix_GetHeight(fieldSystem->mapMatrix) * 32) {
        return FALSE;
    }
    const u8 behavior = GetMetatileBehavior(fieldSystem, x, z);
    return behavior != 0xFF && (MetatileBehavior_IsReflective(behavior) || MetatileBehavior_IsSurfableWater(behavior));
}

static fx32 WaterHeight(void *context, int x, int z) {
    WaterContext *water = context;
    u8 kind;
    return FieldSystem_GetTerrainHeight(water->fieldSystem, water->playerHeight, x * FX32_CONST(16) + FX32_CONST(8),
        z * FX32_CONST(16) + FX32_CONST(8), &kind);
}

void EnhancedGraphics_DrawWaterMask(FieldSystem *fieldSystem) {
    if (fieldSystem == NULL || fieldSystem->playerAvatar == NULL || fieldSystem->location == NULL) {
        EnhancedScene_DrawWaterMask(NULL);
        return;
    }
    WaterContext context = { fieldSystem, PlayerAvatar_GetPositionVector(fieldSystem->playerAvatar)->y };
    const EnhancedWaterQuery query = {
        .context = &context,
        .isWater = IsWaterTile,
        .height = WaterHeight,
        .playerX = PlayerAvatar_GetXCoord(fieldSystem->playerAvatar),
        .playerZ = PlayerAvatar_GetZCoord(fieldSystem->playerAvatar),
        .mapId = fieldSystem->location->mapId,
    };
    EnhancedScene_DrawWaterMask(&query);
}
