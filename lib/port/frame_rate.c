// 60 FPS scope; see include/port_sdk/port_frame_rate.h
#include "port_frame_rate.h"

#include <nitro.h>
#include <simulator/sim.h>
#include <stdio.h>

#include "field_system.h"
#include "port_game_settings.h"

static BOOL s60Hz = FALSE;
static BOOL sSpeedFix = FALSE;
static BOOL sOddTick = FALSE; // alternates every 60 Hz iteration

BOOL FrameRate_PlayerIsFreeOnOverworld(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL
        && fieldSystem->processManager != NULL
        && fieldSystem->processManager->parent != NULL
        && fieldSystem->processManager->child == NULL
        && !fieldSystem->processManager->isPaused
        && fieldSystem->runningFieldMap
        && fieldSystem->taskman == NULL;
}

void FrameRate_Update(void) {
    const GameSettings *settings = GameSettings_Get();
    if (!settings->enable60fps) {
        s60Hz = FALSE;
    } else if (settings->enable60fpsEverywhere) {
        s60Hz = TRUE;
    } else {
        s60Hz = FrameRate_PlayerIsFreeOnOverworld();
    }
    sSpeedFix = s60Hz && settings->enable60fpsSpeedFix;
    sOddTick = sSpeedFix ? !sOddTick : FALSE;

    static int sLogged = -1;
    if (sLogged != (int)s60Hz) {
        sLogged = (int)s60Hz;
        printf("Frame rate: game logic at %s\n", s60Hz ? "60 Hz" : "30 Hz");
    }
}

BOOL FrameRate_Is60Hz(void) {
    return s60Hz;
}

void FrameRate_BeforeFirstVBlank(void) {
    if (!GameSettings_Get()->drawEveryFrame) {
        SIM_SkipNextFrame();
    }
}

BOOL FrameRate_SpeedFixActive(void) {
    return sSpeedFix;
}

BOOL FrameRate_SkipHalfRateTick(void) {
    return sSpeedFix && sOddTick;
}
