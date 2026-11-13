// The game's pages in libntr's settings screen (SIM_Prj_SettingsPages): Gameplay, with the Platinum fork's 60 FPS
// settings (its src/port/gui_settings.cpp)
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>

#include "port_game_settings.h"

namespace {

using namespace SIM::UI;

bool SettingToggle(const char *label, const char *help, BOOL *value, bool enabled = true) {
    bool on = *value != FALSE;
    if (Toggle(label, help, &on, enabled)) {
        *value = on ? TRUE : FALSE;
        GameSettings_Save();
        return true;
    }
    return false;
}

void DrawGameplay() {
    GameSettings *settings = GameSettings_Get();
    Section("Frame rate");
    SettingToggle("60 FPS", "Smooth walking and scrolling: the overworld runs at 60 frames a second. Music keeps its speed.",
        &settings->enable60fps);
    const bool fps60 = settings->enable60fps != FALSE;
    SettingToggle("Keep the real speed", "Walking, biking, animations and map props move at the DS speed at 60 FPS.",
        &settings->enable60fpsSpeedFix, fps60);
    SettingToggle("60 FPS in battles and menus too", "Everything off the overworld then runs twice as fast.",
        &settings->enable60fpsEverywhere, fps60);
    SettingToggle("Draw every DS frame", "Battles and menus draw both of the DS frames they show for each of theirs.",
        &settings->drawEveryFrame);
}

} // namespace

void SIM_Prj_SettingsPages() {
    SIM::Settings::AddPage("Gameplay", "60 FPS", DrawGameplay, 30);
}
