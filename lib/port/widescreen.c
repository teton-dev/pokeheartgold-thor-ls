// Widescreen overworld; see include/port_sdk/port_widescreen.h
#include "port_widescreen.h"

#include <nitro.h>
#include <simulator/sim.h>

#include "constants/weather.h"

#include "battle_transition_task.h"
#include "field/weather_manager.h"
#include "field_system.h"
#ifdef GEN5_BATTLES
#include "port_gen5_battle.h"
#endif

// The field camera's projection as last computed, and its widening
static const Camera *sFieldCamera = NULL;
static float sFieldRatio = 1.0f;

// The field map is running with no application over it (its camera exists then)
static FieldSystem *ShownFieldSystem(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->processManager == NULL || fieldSystem->processManager->child != NULL
        || !fieldSystem->runningFieldMap || fieldSystem->camera == NULL) {
        return NULL;
    }
    return fieldSystem;
}

// How the weather's 2D layers go wide: its sprites (rain, snow, sand, diamond dust) repeated across the sides, the
// 4:3 picture wrapping around, and its BG2 picture (fog) drawn across the whole width from its tilemap, which
// scrolls and repeats. FALSE: the scene stays 4:3 (Flash's darkness, a circle of windows drawn line by line)
static BOOL WeatherLayers(FieldSystem *fieldSystem, u8 *objWrap, u8 *wideBgs) {
    *objWrap = 0;
    *wideBgs = 0;
    if (fieldSystem->mapSystems == NULL || fieldSystem->mapSystems->weatherManager == NULL) {
        return TRUE;
    }
    const u32 weather = WeatherManager_GetWeather(fieldSystem->mapSystems->weatherManager);
    if (weather == WEATHER_FLASH || weather == WEATHER_FLASH_2) {
        return FALSE;
    }
    BOOL sprites, bg2;
    WeatherManager_PortLayers(weather, &sprites, &bg2);
    if (sprites) {
        *objWrap |= 0x0F;
    }
    if (bg2) {
        *wideBgs |= 1 << 2;
    }
    return TRUE;
}

static BOOL Gen5BattleShown(void) {
#ifdef GEN5_BATTLES
    return Gen5Battle_IsShown();
#else
    return FALSE;
#endif
}

static BOOL Gen5MoveBgShown(void) {
#ifdef GEN5_BATTLES
    return Gen5Battle_MoveBgShown();
#else
    return FALSE;
#endif
}

void Widescreen_Update(void) {
    FieldSystem *fieldSystem = ShownFieldSystem();
    u8 objWrap = 0;
    u8 wideBgs = 0;
    u8 edgeBgs = 0;
    u8 edgeMirrorBgs = 0;
    // A battle in Black 2 and White 2's engine: its arena is 3D, its camera widened as the field camera is
    // (gen5_battle.c); its text box (BG1), the ability bar and the full-screen moves' backgrounds reach the edges, their
    // patterns mirrored, and the party balls' bar (sprites of priority 0) too
    BOOL wide = Gen5BattleShown();
    if (wide) {
        edgeBgs = (1 << 1) | (1 << 2) | (1 << 3);
        if (Gen5MoveBgShown()) {
            edgeMirrorBgs = 1 << 3;
        }
    } else if (EncounterEffect_PortWildPlaying()) {
        // A wild battle's own transition (the black or white tiles that flip the overworld away, on BG3): its edges
        // reach the sides too, instead of a 4:3 box over the (already wide) field it covers
        wide = TRUE;
        edgeBgs = 1 << 3;
    } else if (fieldSystem != NULL) {
        wide = WeatherLayers(fieldSystem, &objWrap, &wideBgs);
    }
    SIM_SetWidescreenSceneOptions(FALSE, wideBgs);
    SIM_SetWidescreenObjWrap(objWrap);
    SIM_SetWidescreenBgMirror(0);
    SIM_SetWidescreenBgEdgeExtend(edgeBgs);
    SIM_SetWidescreenBgEdgeMirror(edgeMirrorBgs);
    SIM_SetWidescreenBgStripe(0, 255.0f / 256.0f);
    SIM_SetWidescreenObjEdgeExtend(Gen5BattleShown() ? 0x01 : 0);
    SIM_SetWidescreenScene(wide);
}

static float WantedRatio(const Camera *camera) {
    FieldSystem *fieldSystem = ShownFieldSystem();
    if (fieldSystem == NULL || camera != fieldSystem->camera || !SIM_IsWidescreenActive()) {
        return 1.0f;
    }
    return SIM_WidescreenRatio();
}

fx32 Widescreen_AspectRatio(const Camera *camera, fx32 aspectRatio) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || camera != fieldSystem->camera) {
        return aspectRatio;
    }
    const float ratio = WantedRatio(camera);
    sFieldCamera = camera;
    sFieldRatio = ratio;
    return ratio == 1.0f ? aspectRatio : (fx32)((float)aspectRatio * ratio);
}

void Widescreen_BeforeFieldRender(FieldSystem *fieldSystem) {
    Camera *camera = fieldSystem->camera;
    // Only while the field camera is the one in use: the projection is global
    if (camera == NULL || Camera_PortGetActive() != camera) {
        return;
    }
    if (sFieldCamera != camera || sFieldRatio != WantedRatio(camera)) {
        Camera_ApplyPerspectiveType(camera->perspectiveType, camera);
    }
}
