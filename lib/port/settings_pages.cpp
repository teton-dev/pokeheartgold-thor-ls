// The game's pages in libntr's settings screen (SIM_Prj_SettingsPages), grouped as in the Platinum fork's
// (src/port/gui_settings.cpp): Gameplay (the Japanese Game Corner), Battles (Gen 5), the frame rate on Display,
// the save editor with the tools
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>
#include <simulator/sim_carts.h>
#include <simulator/sim_companion.h>
#include <simulator/sim_files.h>

#include <SDL2/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include <string>

#include "port/autosave.h"
#include "port/gear_data.h"
#include "port/port_game.h"
#include "port/save_slots.h"
#include "port/save_sync.h"
#include "port/save_version.h"
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
    if (game == nullptr
        && Button("Add Black 2 or White 2",
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
    Section("Items");
    SettingToggle("Infinite TM uses", "TMs are not used up when taught, and the shops don't buy them. The bag "
        "doesn't show how many you have.", &settings->infiniteTMs);
    Section("Pokegear");
    SettingToggle("Story calls only", "No calls from random trainers on the Pokegear. Only your mom's and the story's "
        "(Prof. Elm, the Bike Shop, Bill, Baoba...) still ring.", &settings->storyCallsOnly);
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
    if (game == nullptr
        && Button("Add the Japanese ROM",
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

// --- Saves: port-common's slots, save sync and autosave, as the Platinum fork's page --------------------------------

std::string sSavesStatus;
bool sSavesError = false;
int sSelectedSlot = -1;
int sImportSlot = -1;

void SetSavesStatus(const std::string &text, bool error) {
    sSavesStatus = text;
    sSavesError = error;
}

void OnImportPicked(const char *path, void *) {
    if (path == nullptr || sImportSlot < 0) {
        SetSavesStatus("", false);
        return;
    }
    char err[128] = "";
    if (SaveSlots_Import(sImportSlot, path, err, sizeof(err))) {
        char text[96];
        snprintf(text, sizeof(text), "Imported into slot %d.", sImportSlot + 1);
        SetSavesStatus(text, false);
    } else {
        SetSavesStatus(std::string("Import failed: ") + err, true);
    }
#ifdef SDK_BUILD_ANDROID
    // The picker made a private copy for us
    remove(path);
#endif
    sImportSlot = -1;
}

void OnExportDone(BOOL saved, void *) {
    SetSavesStatus(saved ? "Save exported." : "", false);
}

void ExportSlot(int slot) {
    // Padded to a full 512 KB image (what emulators expect) in a temporary file, then the player chooses where it
    // goes
    const char *tempPath = "export.sav";
    char err[128] = "";
    if (!SaveSlots_Export(slot, tempPath, err, sizeof(err))) {
        SetSavesStatus(std::string("Export failed: ") + err, true);
        return;
    }
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    char name[80];
    if (info.hasSummary && info.name[0] != '\0') {
        snprintf(name, sizeof(name), "%s - %s (slot %d).sav", PortGame_Name(), info.name, slot + 1);
    } else {
        snprintf(name, sizeof(name), "%s (slot %d).sav", PortGame_Name(), slot + 1);
    }
    SIM_Files_SaveCopy("Export save", tempPath, name, OnExportDone, nullptr);
}

// A word on the slot's sync for its card, or nullptr
const char *SyncStateText(int slot) {
    SaveSyncSlot sync;
    SaveSync_GetSlot(slot, &sync);
    switch (sync.state) {
    case SAVE_SYNC_IN_STEP:
        return "Synced";
    case SAVE_SYNC_SENDING:
        return "Sending to the sync folder";
    case SAVE_SYNC_NEWER_THERE:
        return "A newer save is waiting: it loads on the main menu";
    case SAVE_SYNC_ARRIVING:
        return "A newer save is arriving";
    case SAVE_SYNC_CONFLICT:
        return "Saved on two devices: choose one below";
    case SAVE_SYNC_ERROR:
        return "Couldn't sync";
    default:
        return nullptr;
    }
}

// The other device's save of the slot, when it differs: which to keep
void DrawSlotSync(int slot, bool atMenu) {
    SaveSyncSlot sync;
    SaveSync_GetSlot(slot, &sync);
    if (sync.state != SAVE_SYNC_CONFLICT && sync.state != SAVE_SYNC_NEWER_THERE) {
        return;
    }
    char text[256];
    if (sync.thereName[0] != '\0') {
        snprintf(text, sizeof(text), "%s saved this slot %s: %s, %d:%02d played, %d badge%s.", sync.thereDevice,
            sync.thereSavedAt[0] != '\0' ? sync.thereSavedAt : "too", sync.thereName, sync.thereHours,
            sync.thereMinutes, sync.thereBadges, sync.thereBadges == 1 ? "" : "s");
    } else {
        snprintf(text, sizeof(text), "%s saved this slot too.", sync.thereDevice);
    }
    Note(text);
    char label[96];
    snprintf(label, sizeof(label), "Use %s's save", sync.thereDevice);
    if (Button(label, atMenu ? "Loads it here. This device's is kept as .bak."
                             : "Go back to the main menu first (Games > Reset to the title screen).",
            sync.state == SAVE_SYNC_CONFLICT, atMenu)) {
        char err[160] = "";
        if (SaveSync_Resolve(slot, FALSE, err, sizeof(err))) {
            SetSavesStatus(slot == SaveSlots_Current() ? "It loads when you close the settings." : "Loaded.", false);
        } else {
            SetSavesStatus(err, true);
        }
    }
    if (sync.state == SAVE_SYNC_CONFLICT && Button("Keep this device's save", "Sends it to the folder, and the other "
                                                   "device loads it on its main menu.")) {
        char err[160] = "";
        if (SaveSync_Resolve(slot, TRUE, err, sizeof(err))) {
            SetSavesStatus("Kept this one. The other device gets it next.", false);
        } else {
            SetSavesStatus(err, true);
        }
    }
}

void OnSyncFolderPicked(const char *path, void *) {
    if (path == nullptr) {
        return;
    }
    SaveSync_SetFolder(path);
    SaveSync_CheckNow();
}

void DrawSync() {
    Section("Sync with your other devices");
    Note("Play the same game on the Thor and a computer: the saves go through a folder that Syncthing, a network "
         "share or a cloud drive keeps the same on both. After you save, this device copies the save there; the "
         "other one loads it on its main menu.");
    const char *folder = SaveSync_Folder();
    const bool busy = SIM_Files_IsBusy() != FALSE;
    if (folder[0] == '\0') {
        if (Button("Choose the sync folder", "The same folder on every device: Syncthing's, a network share...",
                true, !busy)) {
            SIM_Files_PickFolderPath("Choose the sync folder", OnSyncFolderPicked, nullptr);
        }
    } else {
        bool on = SaveSync_Enabled() != FALSE;
        if (Toggle("Sync saves", nullptr, &on)) {
            SaveSync_SetEnabled(on ? TRUE : FALSE);
            SaveSync_CheckNow();
        }
        char gameFolder[1200];
        SaveSync_GameFolder(gameFolder, sizeof(gameFolder));
        std::string where = std::string("This game's saves go in ") + gameFolder;
        Note(where.c_str());
        if (Button("Choose another folder", nullptr, false, !busy)) {
            SIM_Files_PickFolderPath("Choose the sync folder", OnSyncFolderPicked, nullptr);
        }
    }
    if (!SIM_Files_HasStorageAccess()) {
        if (Button("Allow access to the folder", "Android asks for \"All files access\": turn it on for this app, "
                                                 "then come back.", folder[0] != '\0')) {
            SIM_Files_RequestStorageAccess();
        }
    }
    Status(SaveSync_LastEvent());
    char device[96];
    snprintf(device, sizeof(device), "This device shows as \"%s\".", SIM_DeviceName());
    Note(device);
}

void DrawSaves() {
    GameSettings *settings = GameSettings_Get();
    const int current = SaveSlots_Current();
    if (sSelectedSlot < 0) {
        sSelectedSlot = current;
    }

    Note("Each slot is a separate game. On the main menu, L and R switch slots too.");
    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        SaveSlotInfo info;
        SaveSlots_GetInfo(slot, &info);
        char title[64];
        char subtitle[128];
        if (!info.hasData) {
            snprintf(title, sizeof(title), "Slot %d: empty", slot + 1);
            snprintf(subtitle, sizeof(subtitle), "%s", slot == current ? "In use" : "");
        } else if (info.hasSummary) {
            snprintf(title, sizeof(title), "Slot %d: %s", slot + 1, info.name);
            snprintf(subtitle, sizeof(subtitle), "%s%d:%02d played, %d badge%s%s%s", slot == current ? "In use. " : "",
                info.hours, info.minutes, info.badges, info.badges == 1 ? "" : "s",
                info.savedAt[0] != '\0' ? ", saved " : "", info.savedAt);
        } else {
            snprintf(title, sizeof(title), "Slot %d: saved game", slot + 1);
            snprintf(subtitle, sizeof(subtitle), "%sDetails show once it has been loaded", slot == current ? "In use. " : "");
        }
        const char *sync = SyncStateText(slot);
        if (sync != nullptr) {
            const size_t used = strlen(subtitle);
            snprintf(subtitle + used, sizeof(subtitle) - used, "%s%s", used > 0 ? ". " : "", sync);
        }
        ImGui::PushID(slot);
        if (Card(title, subtitle, slot == sSelectedSlot)) {
            sSelectedSlot = slot;
            sSavesStatus.clear();
        }
        ImGui::PopID();
    }

    // What to do with the selected slot
    const int slot = sSelectedSlot;
    const bool busy = SIM_Files_IsBusy() != FALSE;
    const bool atMenu = SaveSlots_IsMainMenuActive() != FALSE;
    char label[64];
    snprintf(label, sizeof(label), "Slot %d", slot + 1);
    Section(label);
    if (slot != current) {
        if (Button("Use this slot", atMenu ? "Loads it on the main menu." : "Go back to the main menu first (or press L/R "
                   "there).", false, atMenu)) {
            SaveSlots_RequestSwitch(slot);
        }
    }
    const bool inUse = SaveSlots_IsInUse(slot) != FALSE;
    if (Button("Import a save", inUse ? "This slot's game is running: go back to the main menu to replace it."
                                      : "A .sav (melonDS, most emulators) or .dsv (DeSmuME) from an emulator or "
                                        "another device. The old one is kept as .bak.",
            false, !busy && !inUse)) {
        sImportSlot = slot;
        sSavesStatus.clear();
        SIM_Files_PickToOpen("Import save (.sav / .dsv)", ".sav,.dsv,.bin,.*", OnImportPicked, nullptr);
    }
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    if (Button("Export this save", "A 512 KB .sav that melonDS and other emulators load as it is.", false,
            !busy && info.hasData)) {
        sSavesStatus.clear();
        ExportSlot(slot);
    }
    DrawSlotSync(slot, atMenu);
    Status(sSavesStatus.c_str(), sSavesError);

    DrawSync();

    Section("Autosave");
    SettingToggle("Save automatically", "While exploring, every few minutes.", &settings->autosave);
    int minutes = settings->autosaveMinutes;
    if (Stepper("Every", nullptr, &minutes, 1, 30, 1, "%d min", settings->autosave != FALSE)) {
        settings->autosaveMinutes = minutes;
        GameSettings_Save();
    }
    SettingToggle("Save when leaving the app", "When it's closed or sent to the background.", &settings->autosaveOnExit);
    Note("Autosave only saves where the menu's SAVE would, while you're standing still, and after you've saved this "
         "game yourself once.");

    const char *reason = nullptr;
    const bool canSave = Autosave_CanSaveNow(&reason) != FALSE;
    std::string help = canSave ? std::string("Saves the game now, like the menu's SAVE.")
                               : std::string("Not right now: ") + (reason != nullptr ? reason : "the game can't save here");
    if (Button("Save now", help.c_str(), false, canSave)) {
        if (Autosave_SaveNow("Saved")) {
            SetSavesStatus("Saved.", false);
        } else {
            SetSavesStatus("The game could not be saved.", true);
        }
    }
}

// --- the Gear (port-common's companion on the lower screen), as the Platinum fork's page ------------------------------

void DrawGear() {
    GameSettings *settings = GameSettings_Get();
    if (SettingToggle("Show the Gear", "The companion on the lower screen: your party, the Pokedex's species, your "
            "trainer card. Goldenrod's is partial: no battle, encounter, map or PC tabs yet.", &settings->gearEnabled)) {
        if (!settings->gearEnabled) {
            SIM_Companion_SetFullScreen(FALSE);
        }
    }
    static const char *const kLevels[] = { "Vanilla", "Guide", "Everything" };
    static const char *const kHelp[] = {
        "Only what the game itself shows: no hidden values.",
        "Type matchups; species you haven't seen stay hidden.",
        "Also hidden values: IVs and EVs, and species you haven't seen.",
    };
    int level = settings->gearDiscovery;
    if (level < 0 || level > 2) {
        level = GEAR_DISCOVERY_GUIDE;
    }
    if (Choice("What it reveals", kHelp[level], &level, kLevels, 3, settings->gearEnabled != FALSE)) {
        settings->gearDiscovery = level;
        GameSettings_Save();
    }
    Note("Tap Full in the Gear (or press L3) to give it the whole lower screen, Split for the DS screen, which "
         "HeartGold and SoulSilver use for the Pokegear.");
}

// --- Challenges: Nuzlocke mode (port_nuzlocke_mode.h), as the Platinum fork's page -----------------------------

int sNuzlockeWarnLevel = 0; // a level waiting for the rules to be accepted
double sNuzlockeOffUntil = 0.0;

const char *NuzlockeRules(int level) {
    // The Platinum fork's text (src/port/gui_settings.cpp). The level cap is the next boss's ace (Hard: its weakest);
    // the crit guard and the battle bag rule are enforced in BW2's engine (Settings > Battles > Gen 5 battles)
    switch (level) {
    case NUZLOCKE_SIMPLE:
        return "Every Pokemon has IVs of 15. Your Pokemon gain no EXP past the next gym leader's strongest Pokemon's "
               "level. A Pokemon that faints is gone for good: nothing heals it again. No items in battle (only Poke "
               "Balls), and trainers use none. Caught Pokemon must be nicknamed. Whiting out ends the run: the mode "
               "turns off and can never be turned on again on this save.";
    case NUZLOCKE_NORMAL:
        return "Simple's rules, and: once you have Poke Balls, you may catch only the first Pokemon you meet in each "
               "area (the same species again if it got away), and nothing else there once you've caught it. Shiny "
               "Pokemon can always be caught.";
    case NUZLOCKE_HARD:
        return "Normal's rules, and: if the first Pokemon you meet in an area gets away, the area is failed and "
               "nothing can be caught there, the level cap is the next gym leader's weakest Pokemon's level, static "
               "encounters (legendaries among them) count as the area's one, and there is no crit guard.";
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
        Note("Turning it off later, or whiting out, ends it on this save for good. The battle rules (no items, the crit "
             "guard) are enforced in BW2's engine: keep Settings > Battles > Gen 5 battles on.");
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
    SIM::Settings::AddPage("Saves", "Slots, autosave, import and export, sync", DrawSaves, 20);
    SIM::Settings::AddPage("Gameplay", "The Japanese Game Corner", DrawGameplay, 30);
#ifdef GEN5_BATTLES
    SIM::Settings::AddPage("Battles", "Gen 5 battles and their ROM", DrawBattles, 32);
#endif
    SIM::Settings::AddPage("Challenges", "Nuzlocke mode", DrawChallenges, 34);
    SIM::Settings::AddPage("Display", nullptr, DrawFrameRate, 40);
    SIM::Settings::AddPage("Gear", "The lower-screen companion (partial)", DrawGear, 65); // appended to libntr's page
    SIM::Settings::AddPage("Save editor", "Pokemon, items, money, badges", SaveEditor_Draw, 80);
}

// The on-screen save notice ("Autosaved", "Saved"), drawn every frame, settings open or not (libntr's weak
// SIM::GUI::PrjOverlay), as the Platinum fork's src/port/gui_saves.cpp
namespace SIM::GUI {

void PrjOverlay() {
    float alpha = 0.0f;
    const char *notice = Autosave_GetNotice(&alpha);
    if (notice == nullptr) {
        return;
    }
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float margin = 12.0f * ImGui::GetIO().FontGlobalScale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##saveNotice", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(notice);
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace SIM::GUI
