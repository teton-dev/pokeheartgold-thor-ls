// Widescreen overworld; see include/port_sdk/port_widescreen.h
#include "port_widescreen.h"

#include <nitro.h>
#include <simulator/sim.h>

#include "constants/weather.h"

#include "field/weather_manager.h"
#include "field_system.h"

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

void Widescreen_Update(void) {
    FieldSystem *fieldSystem = ShownFieldSystem();
    u8 objWrap = 0;
    u8 wideBgs = 0;
    BOOL wide = FALSE;
    if (fieldSystem != NULL) {
        wide = WeatherLayers(fieldSystem, &objWrap, &wideBgs);
    }
    SIM_SetWidescreenSceneOptions(FALSE, wideBgs);
    SIM_SetWidescreenObjWrap(objWrap);
    SIM_SetWidescreenBgMirror(0);
    SIM_SetWidescreenBgEdgeExtend(0);
    SIM_SetWidescreenBgEdgeMirror(0);
    SIM_SetWidescreenBgStripe(0, 255.0f / 256.0f);
    SIM_SetWidescreenObjEdgeExtend(0);
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
