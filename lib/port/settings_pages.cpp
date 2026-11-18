// The game's pages in libntr's settings screen (SIM_Prj_SettingsPages), grouped as in the Platinum fork's
// (src/port/gui_settings.cpp): Gameplay (the Japanese Game Corner), Battles (Gen 5), the frame rate on Display,
// the save editor with the tools
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>
#include <simulator/sim_carts.h>
#include <simulator/sim_files.h>

#include <SDL2/SDL.h>

#include <cctype>
#include <cstring>

#include <string>

#include "port_game_settings.h"
// Only its GUI part: its Pokemon functions need the game's headers, which this library doesn't see
#define PORT_NUZLOCKE_MODE_GUI_ONLY
#include "port_nuzlocke_mode.h"
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

void DrawFrameRate() {
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

void DrawBattles() {
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

void DrawGameplay() {
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

// --- Games: the games added beside HeartGold (Sacred Gold, Storm Silver...), as in the Platinum fork ------------------

std::string sGamesMessage;
int sConfirmSwitch = -1;
Uint64 sConfirmUntil = 0;

void GameAdded(BOOL ok, const char *id) {
    (void)id;
    sGamesMessage = ok ? "Added. Choose it below to switch to it." : "";
}

void DrawGames() {
    SIM_Cart carts[SIM_CARTS_MAX];
    const int count = SIM_Carts_List(carts);
    const SIM_Cart *current = SIM_Carts_Current();
    const Uint64 now = SDL_GetTicks64();
    if (now > sConfirmUntil) {
        sConfirmSwitch = -1;
    }
    Section("Your games");
    for (int i = 0; i < count; i++) {
        ImGui::PushID(i);
        const bool playing = strcmp(carts[i].id, current->id) == 0;
        std::string subtitle = playing ? "Playing now"
            : sConfirmSwitch == i      ? "Tap again to save and switch (the app restarts)"
                                       : "Tap to switch to it";
        if (carts[i].info[0] != '\0') {
            subtitle += std::string(". ") + carts[i].info;
        }
        if (Card(carts[i].name, subtitle.c_str(), playing) && !playing) {
            if (sConfirmSwitch == i) {
                SIM_Carts_SwitchTo(carts[i].id);
            } else {
                sConfirmSwitch = i;
                sConfirmUntil = now + 4000;
            }
        }
        ImGui::PopID();
    }
    Status(sGamesMessage.c_str());
    if (Button("Add a game", "A ROM hack like Sacred Gold or Storm Silver: choose a folder with your ROM and its patches, "
                             "and the app applies them.")) {
        sGamesMessage.clear();
        SIM_Carts_Add(GameAdded);
    }
    bool ask = SIM_Carts_AskAtLaunch() != FALSE;
    if (Toggle("Choose the game when the app starts", "Only asked when more than one game is installed.", &ask)) {
        SIM_Carts_SetAskAtLaunch(ask ? TRUE : FALSE);
    }
    Note("Each game has its own files and saves; the settings are shared. Switching saves your game first when it can "
         "be.");
}

// --- Challenges: Nuzlocke mode (port_nuzlocke_mode.h), as the Platinum fork's page -----------------------------

int sNuzlockeWarnLevel = 0; // a level waiting for the rules to be accepted
double sNuzlockeOffUntil = 0.0;

const char *NuzlockeRules(int level) {
    // What Goldenrod enforces so far (docs/thor/dev/hgss.md): Platinum's level cap, IVs of 15, item and nickname
    // rules aren't in yet
    switch (level) {
    case NUZLOCKE_SIMPLE:
        return "A Pokemon that faints is gone for good: nothing heals it again. Whiting out ends the run: the mode "
               "turns off and can never be turned on again on this save.";
    case NUZLOCKE_NORMAL:
        return "Simple's rules, and: once you have Poke Balls, you may catch only the first Pokemon you meet in each "
               "area (the same species again if it got away), and nothing else there once you've caught it. Shiny "
               "Pokemon can always be caught.";
    case NUZLOCKE_HARD:
        return "Normal's rules, and: static encounters (legendaries among them) count as the area's one, and there "
               "is no crit guard.";
    }
    return "";
}

// Everything a level asks, for the warning before it turns on (Normal and Hard build on the levels below)
std::string NuzlockeAllRules(int level) {
    std::string rules = NuzlockeRules(NUZLOCKE_SIMPLE);
    for (int i = NUZLOCKE_NORMAL; i <= level; i++) {
        const char *more = NuzlockeRules(i);
        std::string extra = strchr(more, ':') + 2; // without "Simple's rules, and: "
        extra[0] = (char)toupper((unsigned char)extra[0]);
        rules += "\n\n" + extra;
    }
    return rules;
}

void DrawChallenges() {
    Section("Nuzlocke mode");
    NuzlockeModeView view;
    NuzlockeMode_GetView(&view);
    if (!view.hasSave) {
        Note("Load a save (continue or start a game) to set its Nuzlocke mode.");
        return;
    }
    if (view.storageError) {
        Status("The Nuzlocke record couldn't be written to the save.", true);
    }
    if (view.dropped) {
        Note("This save was a Nuzlocke run and left it: it can't be one again.");
        return;
    }
    if (sNuzlockeWarnLevel != 0) {
        std::string title = std::string("Nuzlocke mode: ") + NuzlockeMode_LevelName(sNuzlockeWarnLevel);
        Panel(title.c_str(), NuzlockeAllRules(sNuzlockeWarnLevel).c_str());
        Note("Turning it off later, or whiting out, ends it on this save for good. Goldenrod doesn't have Platinum's "
             "level cap, IVs of 15, item and nickname rules yet.");
        const int pressed = ButtonPair("Turn it on", "Cancel", true);
        if (pressed == 1) {
            NuzlockeMode_RequestLevel(sNuzlockeWarnLevel);
        }
        if (pressed != 0) {
            sNuzlockeWarnLevel = 0;
        }
        return;
    }
    static const char *const kLevels[] = { "Off", "Simple", "Normal", "Hard" };
    int level = view.level;
    if (level == NUZLOCKE_OFF) {
        if (Choice("Nuzlocke mode", "For this save only. Simple, Normal or Hard: the rules show before it turns on.",
                &level, kLevels, NUZLOCKE_LEVEL_COUNT) && level != NUZLOCKE_OFF) {
            sNuzlockeWarnLevel = level;
        }
        return;
    }
    int choice = level - 1;
    if (Choice("Level", NuzlockeRules(level), &choice, kLevels + 1, NUZLOCKE_LEVEL_COUNT - 1)) {
        NuzlockeMode_RequestLevel(choice + 1);
    }
    bool guard = view.critGuard != FALSE;
    if (Toggle("Crit guard", "No critical hits, except from moves that always land one. Not with Hard.", &guard,
            level != NUZLOCKE_HARD)) {
        NuzlockeMode_RequestCritGuard(guard ? TRUE : FALSE);
    }
    const double now = ImGui::GetTime();
    const bool confirming = now < sNuzlockeOffUntil;
    if (Button(confirming ? "Tap again: it can't be turned on again###nuzOff" : "Turn Nuzlocke mode off###nuzOff",
            "This save stops being a Nuzlocke run, for good.", confirming)) {
        if (confirming) {
            NuzlockeMode_RequestLevel(NUZLOCKE_OFF);
            sNuzlockeOffUntil = 0.0;
        } else {
            sNuzlockeOffUntil = now + 4.0;
        }
    }
}

} // namespace

void SaveEditor_Draw(); // save_editor_gui.cpp

void SIM_Prj_SettingsPages() {
    // In groups, as in the Platinum fork: playing (30-34), the device (40-60; libntr's Display 40, Sound 50, Controls
    // 60), connections (libntr's Link play 68), tools (80-90; libntr's Advanced 90)
    SIM::Settings::AddPage("Games", "Switch games, add Sacred Gold or Storm Silver", DrawGames, 10);
    SIM::Settings::AddPage("Gameplay", "The Japanese Game Corner", DrawGameplay, 30);
#ifdef GEN5_BATTLES
    SIM::Settings::AddPage("Battles", "Gen 5 battles and their ROM", DrawBattles, 32);
#endif
    SIM::Settings::AddPage("Challenges", "Nuzlocke mode", DrawChallenges, 34);
    SIM::Settings::AddPage("Display", nullptr, DrawFrameRate, 40); // appended to libntr's page
    SIM::Settings::AddPage("Save editor", "Pokemon, items, money, badges", SaveEditor_Draw, 80);
}
