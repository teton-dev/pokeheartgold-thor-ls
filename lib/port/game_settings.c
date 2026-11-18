// The game's own settings; see include/port_sdk/port_game_settings.h
#include "port_game_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simulator/config/ini.h>
#include <simulator/sim_carts.h>

#define SETTINGS_FILE "sim_config_prj.ini"

static GameSettings sSettings;
static BOOL sLoaded = FALSE;

static BOOL StringToBool(const char *value) {
    return strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
}

static const char *BoolToString(BOOL value) {
    return value ? "true" : "false";
}

static int IniHandler(void *user, const char *section, const char *name, const char *value) {
    GameSettings *settings = user;
    // The saves' settings in their own section, with the Platinum fork's names
    if (strcmp(section, "Saves") == 0) {
        if (strcmp(name, "Slot") == 0) {
            settings->saveSlot = atoi(value);
        } else if (strcmp(name, "Autosave") == 0) {
            settings->autosave = StringToBool(value);
        } else if (strcmp(name, "AutosaveMinutes") == 0) {
            settings->autosaveMinutes = atoi(value);
        } else if (strcmp(name, "AutosaveOnExit") == 0) {
            settings->autosaveOnExit = StringToBool(value);
        }
        return 1;
    }
    if (strcmp(section, "General") != 0) {
        return 1;
    }
    if (strcmp(name, "60Fps") == 0) {
        settings->enable60fps = StringToBool(value);
    } else if (strcmp(name, "60FpsSpeedFix") == 0) {
        settings->enable60fpsSpeedFix = StringToBool(value);
    } else if (strcmp(name, "60FpsEverywhere") == 0) {
        settings->enable60fpsEverywhere = StringToBool(value);
    } else if (strcmp(name, "DrawEveryFrame") == 0) {
        settings->drawEveryFrame = StringToBool(value);
    } else if (strcmp(name, "Gen5Battles") == 0) {
        settings->gen5Battles = StringToBool(value);
    } else if (strcmp(name, "JapaneseGameCorner") == 0) {
        settings->japaneseGameCorner = StringToBool(value);
    }
    return 1;
}

GameSettings *GameSettings_Get(void) {
    if (!sLoaded) {
        sLoaded = TRUE;
        sSettings.enable60fps = TRUE;
        sSettings.enable60fpsSpeedFix = TRUE;
        sSettings.enable60fpsEverywhere = FALSE;
        sSettings.drawEveryFrame = FALSE;
        sSettings.gen5Battles = FALSE;
        sSettings.japaneseGameCorner = FALSE;
        sSettings.saveSlot = 1;
        sSettings.autosave = TRUE;
        sSettings.autosaveMinutes = 5;
        sSettings.autosaveOnExit = TRUE;
        if (ini_parse(SIM_SettingsPath(SETTINGS_FILE), IniHandler, &sSettings) < 0) {
            GameSettings_Save();
        }
    }
    return &sSettings;
}

void GameSettings_Save(void) {
    FILE *file = fopen(SIM_SettingsPath(SETTINGS_FILE), "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "[General]\n");
    fprintf(file, "60Fps=%s\n", BoolToString(sSettings.enable60fps));
    fprintf(file, "60FpsSpeedFix=%s\n", BoolToString(sSettings.enable60fpsSpeedFix));
    fprintf(file, "60FpsEverywhere=%s\n", BoolToString(sSettings.enable60fpsEverywhere));
    fprintf(file, "DrawEveryFrame=%s\n", BoolToString(sSettings.drawEveryFrame));
    fprintf(file, "Gen5Battles=%s\n", BoolToString(sSettings.gen5Battles));
    fprintf(file, "JapaneseGameCorner=%s\n", BoolToString(sSettings.japaneseGameCorner));
    fprintf(file, "\n[Saves]\n");
    fprintf(file, "Slot=%d\n", sSettings.saveSlot);
    fprintf(file, "Autosave=%s\n", BoolToString(sSettings.autosave));
    fprintf(file, "AutosaveMinutes=%d\n", sSettings.autosaveMinutes);
    fprintf(file, "AutosaveOnExit=%s\n", BoolToString(sSettings.autosaveOnExit));
    fclose(file);
}
