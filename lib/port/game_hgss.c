// HeartGold and SoulSilver's side of port-common: the game adapter (port/port_game.h, in the port-common
// subproject), as the Platinum fork's src/port/game_platinum.c. Everything port-common needs to know about the
// game's save data, overworld and settings goes through here. The Gear isn't built for Goldenrod yet (its Gear_*
// data adapters are still to write), so neither are its settings here.
#include "port/port_game.h"

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "global.h"

#include "constants/battle.h"
#include "constants/species.h"

#include "field_system.h"
#include "map_header.h"
#include "overlay_01.h"
#include "party.h"
#include "player_avatar.h"
#include "player_avatar_movement.h"
#include "pokedex.h"
#include "pokemon.h"
#include "port_game_settings.h"
#include "save.h"
#include "save_vars_flags.h"
#include "sys_flags.h"
#include "system.h"

// --- Identity

const char *PortGame_Name(void) {
    return gGameVersion == VERSION_SOULSILVER ? "Pokemon SoulSilver" : "Pokemon HeartGold";
}

const char *PortGame_ShortName(void) {
    return gGameVersion == VERSION_SOULSILVER ? "SoulSilver" : "HeartGold";
}

// --- Settings (lib/port/game_settings.c)

int PortGame_Config_GetSaveSlot(void) {
    return GameSettings_Get()->saveSlot;
}

void PortGame_Config_SetSaveSlot(int slot) {
    GameSettings_Get()->saveSlot = slot;
    GameSettings_Save();
}

BOOL PortGame_Config_GetAutosave(PortAutosaveConfig *out) {
    const GameSettings *settings = GameSettings_Get();
    out->enabled = settings->autosave;
    out->minutes = settings->autosaveMinutes;
    out->onExit = settings->autosaveOnExit;
    return TRUE;
}

// Goldenrod has no cheats page yet: every cheat is off
BOOL PortGame_Config_GetCheats(PortCheatsConfig *out) {
    memset(out, 0, sizeof(*out));
    return TRUE;
}

// Upstream's Nuzlocke helper is replaced by Nuzlocke mode (port_nuzlocke_mode.h, Settings > Challenges), as on
// Platinum: always off
BOOL PortGame_Config_GetNuzlocke(PortNuzlockeConfig *out) {
    memset(out, 0, sizeof(*out));
    return TRUE;
}

// --- Save data

// The game's text to ASCII: letters, digits and the punctuation in names
static void TextToAscii(const u16 *chars, int maxLen, char *out, int outSize) {
    static const struct {
        u16 c;
        char a;
    } kPunctuation[] = {
        { 0x1DE, ' ' }, { 0x1AB, '!' }, { 0x1AC, '?' }, { 0x1AD, ',' }, { 0x1AE, '.' }, { 0x1B1, '/' },
        { 0x1B3, '\'' }, { 0x1B9, '(' }, { 0x1BA, ')' }, { 0x1BE, '-' }, { 0x1C4, ':' }, { 0x188, 'e' },
        { 0x1C0, '&' }, { 0x1BB, 'M' }, { 0x1BC, 'F' },
    };
    int pos = 0;
    for (int i = 0; i < maxLen && chars[i] != EOS && pos < outSize - 1; i++) {
        const u16 c = chars[i];
        char a = 0;
        if (c >= 0x121 && c <= 0x12A) {
            a = '0' + (c - 0x121);
        } else if (c >= 0x12B && c <= 0x144) {
            a = 'A' + (c - 0x12B);
        } else if (c >= 0x145 && c <= 0x15E) {
            a = 'a' + (c - 0x145);
        } else {
            for (int k = 0; k < (int)NELEMS(kPunctuation); k++) {
                if (kPunctuation[k].c == c) {
                    a = kPunctuation[k].a;
                }
            }
        }
        if (a != 0) {
            out[pos++] = a;
        }
    }
    out[pos] = '\0';
}

BOOL PortGame_Save_GetSummary(struct SaveData *saveData, PortSaveSummary *out) {
    if (!Save_FileExists(saveData)) {
        return FALSE;
    }
    memset(out, 0, sizeof(*out));
    PlayerProfile *profile = Save_PlayerData_GetProfile(saveData);
    TextToAscii(PlayerProfile_GetNamePtr(profile), PLAYER_NAME_LENGTH, out->name, sizeof(out->name));
    const IGT *igt = Save_PlayerData_GetIGTAddr(saveData);
    out->hours = igt->hours;
    out->minutes = igt->minutes;
    out->badges = PlayerProfile_CountBadges(profile);
    Pokedex *pokedex = Save_Pokedex_Get(saveData);
    out->pokedexObtained = Pokedex_IsEnabled(pokedex) ? TRUE : FALSE;
    out->seen = Pokedex_GetNatDexFlag(pokedex) ? Pokedex_CountNationalDexSeen(pokedex) : Pokedex_CountJohtoDexSeen(pokedex);
    return TRUE;
}

void PortGame_Save_Reload(struct SaveData *saveData) {
    SaveData_PortReload(saveData);
}

BOOL PortGame_Save_IsValidImage(const u8 *image, u32 size) {
    return SaveData_PortIsValidImage(image, size);
}

// --- Overworld

BOOL PortGame_Field_IsRunning(void) {
    return FieldSystem_PortGet() != NULL;
}

BOOL PortGame_Field_CanSave(const char **reason) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    const char *why = NULL;
    if (fieldSystem == NULL) {
        why = "No game is running";
    } else if (fieldSystem->processManager == NULL || fieldSystem->processManager->isPaused
        || fieldSystem->processManager->child != NULL || !fieldSystem->runningFieldMap || fieldSystem->location == NULL
        || fieldSystem->playerAvatar == NULL) {
        why = "Not on the overworld";
    } else if (fieldSystem->taskman != NULL) {
        why = "Something is happening (event, menu or warp)";
    } else if (Save_VarsFlags_CheckSafariSysFlag(Save_VarsFlags_Get(fieldSystem->saveData))
        || Save_VarsFlags_CheckBugContestFlag(Save_VarsFlags_Get(fieldSystem->saveData))
        || Save_VarsFlags_CheckPalParkSysFlag(Save_VarsFlags_Get(fieldSystem->saveData))
        || MapHeader_MapIsUnionRoom(fieldSystem->location->mapId)) {
        // Where the start menu has no SAVE
        why = "Saving isn't allowed here";
    } else if (!Save_FileExists(fieldSystem->saveData) || Save_PortIsNewGame(fieldSystem->saveData)) {
        // Never autosave a new game until the player has chosen to save it (and, if the slot held another game,
        // agreed to overwrite it)
        why = "Save once from the menu first";
    } else if (!PlayerAvatar_IsBetweenSteps(fieldSystem->playerAvatar) || (gSystem.heldKeys & (PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT)) != 0) {
        // PlayerAvatar_IsBetweenSteps is TRUE when the player stands on a tile (the start menu's own check)
        why = "Player is moving";
    }
    if (reason != NULL) {
        *reason = why;
    }
    return why == NULL;
}

BOOL PortGame_Field_Save(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL && Field_SaveGameNormal(fieldSystem);
}

// --- Versions: HeartGold and SoulSilver have none (Sacred Gold and Storm Silver are games of their own)

const char *PortGame_VersionDescription(const char *versionName) {
    (void)versionName;
    return NULL;
}

// --- Pokemon (port-common's cheats, shiny hunt and Nuzlocke helper; none of them is hooked into Goldenrod yet)

_Static_assert(PORT_SPECIES_MAX == SPECIES_BAD_EGG, "port-common's species count is HGSS's");

// port-common names the game's types as struct Party and struct Pokemon; HGSS's are untagged typedefs
#define MON(mon) ((Pokemon *)(mon))
#define PARTY(party) ((Party *)(party))

int PortGame_Party_Count(struct Party *party) {
    return Party_GetCount(PARTY(party));
}

struct Pokemon *PortGame_Party_Mon(struct Party *party, int index) {
    return (struct Pokemon *)Party_GetMonByIndex(PARTY(party), index);
}

void PortGame_Mon_GetInfo(struct Pokemon *mon, PortMonInfo *out) {
    out->species = (u16)GetMonData(MON(mon), MON_DATA_SPECIES, NULL);
    out->level = (u8)GetMonData(MON(mon), MON_DATA_LEVEL, NULL);
    out->isEgg = GetMonData(MON(mon), MON_DATA_IS_EGG, NULL) ? TRUE : FALSE;
    out->shiny = MonIsShiny(MON(mon)) ? TRUE : FALSE;
    out->hp = GetMonData(MON(mon), MON_DATA_HP, NULL);
    out->personality = GetMonData(MON(mon), MON_DATA_PERSONALITY, NULL);
    out->otID = GetMonData(MON(mon), MON_DATA_OT_ID, NULL);
}

void PortGame_Mon_GetName(struct Pokemon *mon, char *out, int outSize) {
    u16 nickname[POKEMON_NAME_LENGTH + 1];
    GetMonData(MON(mon), MON_DATA_NICKNAME, nickname);
    TextToAscii(nickname, POKEMON_NAME_LENGTH + 1, out, outSize);
}

// Only the shiny hunt and the cheats' shiny odds use it, and Goldenrod has neither yet
void PortGame_Mon_SetPersonality(struct Pokemon *mon, u32 personality) {
    (void)mon;
    (void)personality;
}

BOOL PortGame_IsPersonalityShiny(u32 otID, u32 personality) {
    const u32 value = ((otID >> 16) ^ (otID & 0xFFFF) ^ (personality >> 16) ^ (personality & 0xFFFF));
    return value < 8;
}

u32 PortGame_BaseShinyOdds(void) {
    return 8192;
}

// Only the Nuzlocke helper uses it, and it is always off here
BOOL PortGame_Party_MoveToPC(struct Party *party, int index) {
    (void)party;
    (void)index;
    return FALSE;
}

BOOL PortGame_PlayerTrainerID(u32 *out) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return FALSE;
    }
    *out = PlayerProfile_GetTrainerID(Save_PlayerData_GetProfile(fieldSystem->saveData));
    return TRUE;
}

BOOL PortGame_Pokedex_HasCaught(u16 species) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL) {
        return FALSE;
    }
    return Pokedex_CheckMonCaughtFlag(Save_Pokedex_Get(fieldSystem->saveData), species) ? TRUE : FALSE;
}

u16 PortGame_Field_LocationName(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->location == NULL) {
        return 0xFFFF;
    }
    return (u16)MapHeader_GetMapSec(fieldSystem->location->mapId);
}

BOOL PortGame_Battle_IsWild(u32 battleType) {
    return (battleType & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_PAL_PARK | BATTLE_TYPE_TUTORIAL)) == 0;
}

BOOL PortGame_Battle_Captured(int resultMask) {
    return resultMask == BATTLE_OUTCOME_MON_CAUGHT;
}

BOOL PortGame_Battle_Lost(int resultMask) {
    return resultMask == BATTLE_OUTCOME_LOSE;
}
