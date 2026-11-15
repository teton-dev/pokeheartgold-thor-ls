// The game's pages in libntr's settings screen (SIM_Prj_SettingsPages): Gameplay, with the Platinum fork's 60 FPS
// settings (its src/port/gui_settings.cpp)
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>
#include <simulator/sim_files.h>

#include <string>

#include "port_game_settings.h"
#include "port_slots_rom.h"
#ifdef GEN5_BATTLES
#include <simulator/sim_files.h>

#include <string>

#include "port_gen5_battle_ui.h"
#include "port_gen5_rom.h"
#endif

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

#ifdef GEN5_BATTLES
// --- Gen 5 battles: the Black 2 or White 2 ROM (the Platinum fork's Settings > Games > Gen 5 battles) ---------------

void OnGen5RomPicked(const char *path, void *) {
    if (path != nullptr) {
#ifdef SDK_BUILD_ANDROID
        // The picker's copy in the app's folder: no need to keep it
        Gen5Rom_Import(path, TRUE);
#else
        Gen5Rom_Import(path, FALSE);
#endif
    }
}

void DrawGen5() {
    Gen5Rom_Update();
    const enum Gen5RomState state = Gen5Rom_State();
    const char *game = Gen5Rom_GameName();
    GameSettings *settings = GameSettings_Get();
    Section("Gen 5 battles");
    SettingToggle("Gen 5 battles", "Battles in Black 2 and White 2's engine: their 3D arenas, moving camera, animated "
        "Pokemon and Gen 5 rules, with your party, your bag and the results kept, and catching. Experimental: wild "
        "Pokemon so far. Needs the ROM below.", &settings->gen5Battles, game != nullptr);
    Section("Black 2 or White 2");
    if (game != nullptr) {
        std::string title = std::string("Pokemon ") + game + " (USA)";
        Card(title.c_str(), "Its battle files are in the app's folder (about 270 MB).", true);
    }
    if (state == GEN5_ROM_IMPORTING) {
        Status(Gen5Rom_Status());
    } else if (state == GEN5_ROM_FAILED) {
        Status(Gen5Rom_Status(), true);
    } else if (state == GEN5_ROM_READY) {
        Note(Gen5Rom_Status());
    }
    const bool busy = state == GEN5_ROM_IMPORTING || SIM_Files_IsBusy() != FALSE;
    if (Button(game == nullptr ? "Add Black 2 or White 2" : "Replace it",
            "Your own dump of Pokemon Black 2 or White 2 (USA), unpatched: the Gen 5 battles' graphics, sounds and "
            "data come from it. Its files are extracted once; the ROM isn't kept.",
            false, !busy)) {
        SIM_Files_PickToOpen("Black 2 or White 2 (.nds)", ".nds,.*", OnGen5RomPicked, nullptr);
    }
    if (game != nullptr && Button("Remove it", "Deletes the extracted files.", false, !busy)) {
        Gen5Rom_Remove();
    }
    Section("Try it");
    if (Button("Wild battle in the Gen 5 engine", "A level 5 Rattata against your first Pokemon, in Black 2 and White "
            "2's engine, from where you stand. Close the settings to see it.", false, game != nullptr)) {
        Gen5BattleUI_StartWild(19, 5);
    }
}
#endif

// --- The Japanese Game Corner: a Japanese HeartGold or SoulSilver ROM -------------------------------------------

void OnSlotsRomPicked(const char *path, void *) {
    if (path != nullptr) {
#ifdef SDK_BUILD_ANDROID
        SlotsRom_Import(path, TRUE);
#else
        SlotsRom_Import(path, FALSE);
#endif
    }
}

void DrawGameCorner() {
    SlotsRom_Update();
    const enum SlotsRomState state = SlotsRom_State();
    const char *game = SlotsRom_GameName();
    GameSettings *settings = GameSettings_Get();
    Section("Game Corner");
    SettingToggle("Japanese Game Corner", "The Japanese games' slot machines in the Game Corners, in place of Voltorb "
        "Flip. Needs the Japanese ROM below.", &settings->japaneseGameCorner, game != nullptr);
    Section("Japanese HeartGold or SoulSilver");
    if (game != nullptr) {
        std::string title = std::string("Pokemon ") + game + " (Japan)";
        Card(title.c_str(), "Its slot machine files are in the app's folder (under 1 MB).", true);
    }
    if (state == SLOTS_ROM_IMPORTING) {
        Status(SlotsRom_Status());
    } else if (state == SLOTS_ROM_FAILED) {
        Status(SlotsRom_Status(), true);
    }
    const bool busy = state == SLOTS_ROM_IMPORTING || SIM_Files_IsBusy() != FALSE;
    if (Button(game == nullptr ? "Add the Japanese ROM" : "Replace it",
            "Your own dump of Pokemon HeartGold or SoulSilver (Japan): the slot machine's graphics and rules come "
            "from it. Only those files are kept; the ROM isn't.",
            false, !busy)) {
        SIM_Files_PickToOpen("HeartGold or SoulSilver, Japan (.nds)", ".nds,.*", OnSlotsRomPicked, nullptr);
    }
    if (game != nullptr && Button("Remove it", "Deletes the extracted files.", false, !busy)) {
        SlotsRom_Remove();
        settings->japaneseGameCorner = FALSE;
        GameSettings_Save();
    }
}

} // namespace

void SaveEditor_Draw(); // save_editor_gui.cpp

void SIM_Prj_SettingsPages() {
    SIM::Settings::AddPage("Save editor", "Pokemon, items, money, badges", SaveEditor_Draw, 25);
    SIM::Settings::AddPage("Gameplay", "60 FPS", DrawGameplay, 30);
    SIM::Settings::AddPage("Game Corner", "The Japanese slot machines, from your own ROM", DrawGameCorner, 36);
#ifdef GEN5_BATTLES
    SIM::Settings::AddPage("Gen 5 battles", "Black 2 and White 2's battles, from your own ROM", DrawGen5, 35);
#endif
}
