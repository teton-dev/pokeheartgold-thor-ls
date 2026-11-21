#ifndef POKEHEARTGOLD_PORT_GAME_SETTINGS_H
#define POKEHEARTGOLD_PORT_GAME_SETTINGS_H

// The game's own settings (teton-dev fork), beside libntr's: sim_config_prj.ini in the settings folder, with the
// Platinum fork's names for the same settings (its src/port/sim_config_prj.c), and Settings > Gameplay to change them.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameSettings {
    BOOL enable60fps; // 60Fps: the overworld's logic at 60 Hz (port_frame_rate.h)
    BOOL enable60fpsSpeedFix; // 60FpsSpeedFix: what moves on the overworld keeps the DS's speed at 60 Hz
    BOOL enable60fpsEverywhere; // 60FpsEverywhere: 60 Hz in battles, menus and events too (twice as fast)
    BOOL drawEveryFrame; // DrawEveryFrame: 30 Hz scenes draw their in-between DS frames too
    BOOL gen5Battles; // Gen5Battles: battles in Black 2 and White 2's engine (port_gen5_battle.h)
    BOOL japaneseGameCorner; // JapaneseGameCorner: the Japanese slot machines instead of Voltorb Flip (port_slots_rom.h)
    BOOL storyCallsOnly; // StoryCallsOnly: no random Pokégear calls from trainers; only story calls and Mom's (gear_phone_calls.c)
    int saveSlot; // [Saves] Slot: the save slot in use, 1-based (slot 1 is save.bin; port-common's port/save_slots.h)
    BOOL autosave; // [Saves] Autosave: save while exploring, every autosaveMinutes (port-common's port/autosave.h)
    int autosaveMinutes; // [Saves] AutosaveMinutes
    BOOL autosaveOnExit; // [Saves] AutosaveOnExit: also when the app is closed or sent to the background
    BOOL gearEnabled; // [Gear] Enabled: port-common's Gear on the lower screen (off by default in Goldenrod: partial)
    int gearDiscovery; // [Gear] Discovery: GearDiscovery (port/gear_data.h)
    BOOL gearBattleControls; // [Gear] BattleControls
} GameSettings;

// The settings, loaded the first time (defaults when there is no file)
GameSettings *GameSettings_Get(void);

void GameSettings_Save(void);

#ifdef __cplusplus
}
#endif

#endif // POKEHEARTGOLD_PORT_GAME_SETTINGS_H
