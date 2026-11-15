// The save editor on HGSS's save. See include/port_sdk/port_save_editor.h. The Platinum fork's src/port/save_editor.c
// with HGSS's names for the same things (GetMonData, PCStorage, PlayerProfile...); its trade box (between save slots,
// which this port doesn't have) and legendary events (Platinum's Mystery Gift key items) are left out.
#include "port_save_editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"

#include "constants/badge.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/pokemon.h"
#include "constants/species.h"

#include "msgdata/msg.naix"

#include "bag.h"
#include "field_system.h"
#include "frontier_data.h"
#include "frontier_data_access.h"
#include "igt.h"
#include "item.h"
#include "map_header.h"
#include "move.h"
#include "msgdata.h"
#include "party.h"
#include "player_data.h"
#include "pm_string.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "save.h"
#include "save_arrays.h"

// --- the save ----------------------------------------------------------------------------------------------------

const char *SaveEd_Unavailable(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL || fieldSystem->processManager == NULL) {
        return "Load your save first: continue your game from the title screen.";
    }
    // Battles and the game's menus (party, bag, PC...) work on copies of what's edited here
    if (!FieldSystem_IsFieldMapRunning(fieldSystem) || FieldSystem_IsChildAppRunning(fieldSystem)) {
        return "Close the game's menu or finish the battle first: the editor works while you're walking around.";
    }
    return NULL;
}

static SaveData *EditableSave(void) {
    return SaveEd_Unavailable() == NULL ? FieldSystem_PortGet()->saveData : NULL;
}

// Random numbers of our own (the game's generator stays as it was)
static u32 Random(void) {
    static u32 state = 0;
    if (state == 0) {
        state = (u32)time(NULL) ^ (u32)(uintptr_t)&state ^ 0x9E3779B9u;
        if (state == 0) {
            state = 1;
        }
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// --- names -------------------------------------------------------------------------------------------------------

// HGSS's text (the Gen 4 character table) to ASCII: letters, digits and the punctuation names use. Nidoran's symbols
// become " M" and " F", é an e.
static void TextToAscii(const u16 *chars, int maxLen, char *out, int outSize) {
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
            switch (c) {
            case 0x1BB: // ♂
            case 0x1BC: // ♀
                if (pos < outSize - 3) {
                    out[pos++] = ' ';
                    out[pos++] = c == 0x1BB ? 'M' : 'F';
                }
                continue;
            case 0x1DE:
                a = ' ';
                break;
            case 0x1AB:
                a = '!';
                break;
            case 0x1AC:
                a = '?';
                break;
            case 0x1AD:
                a = ',';
                break;
            case 0x1AE:
                a = '.';
                break;
            case 0x1B1:
                a = '/';
                break;
            case 0x1B3:
                a = '\'';
                break;
            case 0x1B9:
                a = '(';
                break;
            case 0x1BA:
                a = ')';
                break;
            case 0x1BE:
                a = '-';
                break;
            case 0x1C4:
                a = ':';
                break;
            case 0x188:
                a = 'e'; // é (Poké Ball)
                break;
            }
        }
        if (a != 0) {
            out[pos++] = a;
        }
    }
    out[pos] = '\0';
}

typedef struct NameCache {
    int bank;
    int count;
    BOOL titleCase;
    BOOL loaded;
    char (*names)[SAVE_ED_NAME_LEN];
} NameCache;

#define NAME_CACHE(var, textBank, size, titleCase)   \
    static char var##Names[size][SAVE_ED_NAME_LEN]; \
    static NameCache var = { textBank, size, titleCase, FALSE, var##Names }

NAME_CACHE(sSpeciesNames, NARC_msg_msg_0237_bin, SAVE_ED_MAX_SPECIES + 1, TRUE);
NAME_CACHE(sMoveNames, NARC_msg_msg_0750_bin, SAVE_ED_MAX_MOVE + 1, FALSE);
NAME_CACHE(sItemNames, NARC_msg_msg_0222_bin, ITEMS_COUNT, FALSE);
NAME_CACHE(sAbilityNames, NARC_msg_msg_0720_bin, 128, FALSE);
NAME_CACHE(sNatureNames, NARC_msg_msg_0034_bin, 25, FALSE);
NAME_CACHE(sTypeNames, NARC_msg_msg_0735_bin, 32, TRUE);

// The game spells species in capitals ("BULBASAUR"); the editor uses "Bulbasaur", "Mr. Mime", "Ho-Oh", "Farfetch'd"
static void TitleCase(char *name) {
    BOOL wordStart = TRUE;
    for (char *c = name; *c != '\0'; c++) {
        if (*c >= 'A' && *c <= 'Z') {
            if (!wordStart) {
                *c = *c - 'A' + 'a';
            }
            wordStart = FALSE;
        } else if (*c >= 'a' && *c <= 'z') {
            wordStart = FALSE;
        } else {
            wordStart = *c != '\'';
        }
    }
}

// The whole bank is read the first time one of its names is needed
static const char *CachedName(NameCache *cache, int id) {
    if (id < 0 || id >= cache->count) {
        return "?";
    }
    if (!cache->loaded) {
        MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, cache->bank, HEAP_ID_DEFAULT);
        if (msgData == NULL) {
            return "?";
        }
        const int count = (int)MsgDataGetCount(msgData);
        String *string = String_New(64, HEAP_ID_DEFAULT);
        for (int i = 0; i < cache->count; i++) {
            if (i >= count) {
                snprintf(cache->names[i], SAVE_ED_NAME_LEN, "?");
                continue;
            }
            ReadMsgDataIntoString(msgData, i, string);
            TextToAscii(String_cstr(string), String_GetLength(string), cache->names[i], SAVE_ED_NAME_LEN);
            if (cache->titleCase) {
                TitleCase(cache->names[i]);
            }
        }
        String_Delete(string);
        DestroyMsgData(msgData);
        cache->loaded = TRUE;
    }
    return cache->names[id];
}

const char *SaveEd_SpeciesName(u16 species) {
    return CachedName(&sSpeciesNames, species);
}

const char *SaveEd_MoveName(u16 move) {
    return CachedName(&sMoveNames, move);
}

const char *SaveEd_ItemName(u16 item) {
    return CachedName(&sItemNames, item);
}

const char *SaveEd_AbilityName(u8 ability) {
    return CachedName(&sAbilityNames, ability);
}

const char *SaveEd_NatureName(u8 nature) {
    return CachedName(&sNatureNames, nature);
}

const char *SaveEd_TypeName(u8 type) {
    return CachedName(&sTypeNames, type);
}

BOOL SaveEd_SpeciesCaught(u16 species) {
    SaveData *saveData = EditableSave();
    return saveData != NULL && Pokedex_CheckMonCaughtFlag(Save_Pokedex_Get(saveData), species);
}

// --- Pokemon -----------------------------------------------------------------------------------------------------

static const int sIvParams[SAVE_ED_STATS] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
static const int sEvParams[SAVE_ED_STATS] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };
static const int sStatParams[SAVE_ED_STATS] = { MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SP_ATK, MON_DATA_SP_DEF, MON_DATA_SPEED };

// A Pokemon being read or changed. PC Pokemon are worked on as a party Pokemon (which has its level and stats) and
// written back.
typedef struct MonAccess {
    Pokemon *mon;
    BoxPokemon *boxSlot;
    int box;
    Pokemon temp;
} MonAccess;

static Pokemon *OpenMon(SaveEdRef ref, MonAccess *access) {
    SaveData *saveData = EditableSave();
    access->mon = NULL;
    access->boxSlot = NULL;
    if (saveData == NULL) {
        return NULL;
    }

    if (ref.box == SAVE_ED_PARTY) {
        Party *party = SaveArray_Party_Get(saveData);
        if (ref.slot < 0 || ref.slot >= Party_GetCount(party)) {
            return NULL;
        }
        access->mon = Party_GetMonByIndex(party, ref.slot);
        return access->mon;
    }

    if (ref.box < 0 || ref.box >= SAVE_ED_BOXES || ref.slot < 0 || ref.slot >= SAVE_ED_BOX_SLOTS) {
        return NULL;
    }
    BoxPokemon *boxMon = PCStorage_GetMonByIndexPair(SaveArray_PCStorage_Get(saveData), ref.box, ref.slot);
    if (!GetBoxMonData(boxMon, MON_DATA_SPECIES_EXISTS, NULL)) {
        return NULL;
    }
    memset(&access->temp, 0, sizeof(access->temp));
    CopyBoxPokemonToPokemon(boxMon, &access->temp);
    access->mon = &access->temp;
    access->boxSlot = boxMon;
    access->box = ref.box;
    return access->mon;
}

static BOOL CloseMon(MonAccess *access, BOOL changed) {
    if (changed && access->boxSlot != NULL) {
        *access->boxSlot = *Mon_GetBoxMon(&access->temp);
        // HGSS saves the boxes that changed
        PCStorage_SetBoxModified(SaveArray_PCStorage_Get(FieldSystem_PortGet()->saveData), access->box);
    }
    return changed;
}

int SaveEd_PartyCount(void) {
    SaveData *saveData = EditableSave();
    return saveData != NULL ? Party_GetCount(SaveArray_Party_Get(saveData)) : 0;
}

int SaveEd_BoxCount(int box) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES) {
        return 0;
    }
    return PCStorage_CountMonsAndEggsInBox(SaveArray_PCStorage_Get(saveData), box);
}

void SaveEd_BoxName(int box, char *out, int size) {
    SaveData *saveData = EditableSave();
    snprintf(out, size, "Box %d", box + 1);
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES) {
        return;
    }
    String *name = String_New(32, HEAP_ID_DEFAULT);
    PCStorage_GetBoxName(SaveArray_PCStorage_Get(saveData), box, name);
    char ascii[SAVE_ED_NAME_LEN];
    TextToAscii(String_cstr(name), String_GetLength(name), ascii, sizeof(ascii));
    String_Delete(name);
    if (ascii[0] != '\0') {
        snprintf(out, size, "%s", ascii);
    }
}

BOOL SaveEd_GetBoxSlot(int box, int slot, SaveEdSlot *out) {
    memset(out, 0, sizeof(*out));
    SaveData *saveData = EditableSave();
    if (saveData == NULL || box < 0 || box >= SAVE_ED_BOXES || slot < 0 || slot >= SAVE_ED_BOX_SLOTS) {
        return FALSE;
    }
    BoxPokemon *boxMon = PCStorage_GetMonByIndexPair(SaveArray_PCStorage_Get(saveData), box, slot);
    if (!GetBoxMonData(boxMon, MON_DATA_SPECIES_EXISTS, NULL)) {
        return FALSE;
    }
    out->species = GetBoxMonData(boxMon, MON_DATA_SPECIES, NULL);
    out->isEgg = GetBoxMonData(boxMon, MON_DATA_IS_EGG, NULL);
    out->level = CalcBoxMonLevel(boxMon);
    out->shiny = BoxMonIsShiny(boxMon);
    return TRUE;
}

static void Abilities(u16 species, int form, u8 out[2]) {
    out[0] = GetMonBaseStat_HandleAlternateForm(species, form, BASE_ABILITY_1);
    out[1] = GetMonBaseStat_HandleAlternateForm(species, form, BASE_ABILITY_2);
    if (out[1] == out[0]) {
        out[1] = 0;
    }
}

static BOOL GenderFixed(u16 species) {
    int ratio = GetMonBaseStat(species, BASE_GENDER_RATIO);
    return ratio == MON_RATIO_MALE || ratio == MON_RATIO_FEMALE || ratio == MON_RATIO_UNKNOWN;
}

BOOL SaveEd_GetMon(SaveEdRef ref, SaveEdMon *out) {
    memset(out, 0, sizeof(*out));
    MonAccess access;
    Pokemon *mon = OpenMon(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }

    out->exists = TRUE;
    out->isEgg = GetMonData(mon, MON_DATA_IS_EGG, NULL);
    out->species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    out->form = GetMonData(mon, MON_DATA_FORM, NULL);
    if (out->isEgg) {
        snprintf(out->name, sizeof(out->name), "Egg");
    } else if (GetMonData(mon, MON_DATA_HAS_NICKNAME, NULL)) {
        u16 nickname[POKEMON_NAME_LENGTH + 1];
        GetMonData(mon, MON_DATA_NICKNAME, nickname);
        TextToAscii(nickname, POKEMON_NAME_LENGTH + 1, out->name, sizeof(out->name));
    } else {
        snprintf(out->name, sizeof(out->name), "%s", SaveEd_SpeciesName(out->species));
    }

    out->level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    out->nature = GetMonNature(mon);
    out->gender = GetMonGender(mon);
    out->genderFixed = GenderFixed(out->species);
    out->shiny = MonIsShiny(mon);
    out->ability = GetMonData(mon, MON_DATA_ABILITY, NULL);
    Abilities(out->species, out->form, out->abilities);
    out->item = GetMonData(mon, MON_DATA_HELD_ITEM, NULL);
    out->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP, NULL);
    out->ball = GetMonData(mon, MON_DATA_POKEBALL, NULL);
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        out->ivs[s] = GetMonData(mon, sIvParams[s], NULL);
        out->evs[s] = GetMonData(mon, sEvParams[s], NULL);
        out->stats[s] = GetMonData(mon, sStatParams[s], NULL);
    }
    for (int m = 0; m < 4; m++) {
        out->moves[m] = GetMonData(mon, MON_DATA_MOVE1 + m, NULL);
        out->pp[m] = GetMonData(mon, MON_DATA_MOVE1_PP + m, NULL);
        out->maxPp[m] = GetMonData(mon, MON_DATA_MOVE1_MAX_PP + m, NULL);
    }
    out->hp = GetMonData(mon, MON_DATA_HP, NULL);
    out->maxHp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
    out->status = GetMonData(mon, MON_DATA_STATUS, NULL);
    out->type1 = GetMonData(mon, MON_DATA_TYPE_1, NULL);
    out->type2 = GetMonData(mon, MON_DATA_TYPE_2, NULL);
    CloseMon(&access, FALSE);
    return TRUE;
}

// An editable Pokemon (not an egg)
static Pokemon *OpenEditable(SaveEdRef ref, MonAccess *access) {
    Pokemon *mon = OpenMon(ref, access);
    if (mon != NULL && GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
        return NULL;
    }
    return mon;
}

BOOL SaveEd_SetLevel(SaveEdRef ref, int level) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    level = level < 1 ? 1 : (level > SAVE_ED_MAX_LEVEL ? SAVE_ED_MAX_LEVEL : level);
    if (level == (int)GetMonData(mon, MON_DATA_LEVEL, NULL)) {
        return FALSE;
    }
    u32 exp = GetMonExpBySpeciesAndLevel(GetMonData(mon, MON_DATA_SPECIES, NULL), level);
    SetMonData(mon, MON_DATA_EXPERIENCE, &exp);
    CalcMonLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

// A personality value with the nature (-1: any), gender (-1: any), shininess and ability slot (-1: any) asked for,
// for a Pokemon of this trainer
static BOOL FindPersonality(u16 species, int form, u32 otID, int nature, int gender, BOOL shiny, int abilitySlot, u32 *out) {
    u8 abilities[2];
    Abilities(species, form, abilities);
    if (abilities[1] == 0) {
        abilitySlot = -1;
    }
    if (GenderFixed(species)) {
        gender = -1;
    }
    const u32 trainerXor = (otID >> 16) ^ (otID & 0xFFFF);

    for (int i = 0; i < 1000000; i++) {
        u32 r = Random();
        u32 low = r & 0xFFFF;
        u32 high;
        if (shiny) {
            high = (trainerXor ^ low ^ (Random() & 7)) & 0xFFFF;
        } else {
            high = r >> 16;
            if ((trainerXor ^ low ^ high) < 8) {
                continue;
            }
        }
        u32 personality = (high << 16) | low;
        if (nature >= 0 && (int)(personality % 25) != nature) {
            continue;
        }
        if (gender >= 0 && GetGenderBySpeciesAndPersonality(species, personality) != gender) {
            continue;
        }
        if (abilitySlot >= 0 && (int)(personality & 1) != abilitySlot) {
            continue;
        }
        *out = personality;
        return TRUE;
    }
    return FALSE;
}

BOOL SaveEd_SetPersonality(SaveEdRef ref, int nature, int gender, BOOL shiny, int abilitySlot) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u16 species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    int form = GetMonData(mon, MON_DATA_FORM, NULL);
    u32 otID = GetMonData(mon, MON_DATA_OT_ID, NULL);
    u32 personality;
    if (!FindPersonality(species, form, otID, nature, gender, shiny, abilitySlot, &personality)) {
        return FALSE;
    }

    SetMonPersonality(mon, personality); // moves the data to the new personality's order
    u8 gend = GetGenderBySpeciesAndPersonality(species, personality);
    SetMonData(mon, MON_DATA_GENDER, &gend);
    UpdateMonAbility(mon);
    CalcMonLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetIV(SaveEdRef ref, enum SaveEdStat stat, int value) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || stat < 0 || stat >= SAVE_ED_STATS) {
        return FALSE;
    }
    u8 iv = value < 0 ? 0 : (value > SAVE_ED_MAX_IV ? SAVE_ED_MAX_IV : value);
    SetMonData(mon, sIvParams[stat], &iv);
    CalcMonLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetEV(SaveEdRef ref, enum SaveEdStat stat, int value) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || stat < 0 || stat >= SAVE_ED_STATS) {
        return FALSE;
    }
    int others = 0;
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        if (s != (int)stat) {
            others += GetMonData(mon, sEvParams[s], NULL);
        }
    }
    int max = SAVE_ED_MAX_EVS - others;
    max = max > SAVE_ED_MAX_EV ? SAVE_ED_MAX_EV : (max < 0 ? 0 : max);
    u8 ev = value < 0 ? 0 : (value > max ? max : value);
    SetMonData(mon, sEvParams[stat], &ev);
    CalcMonLevelAndStats(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetMove(SaveEdRef ref, int slot, u16 move) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || slot < 0 || slot >= 4 || move > SAVE_ED_MAX_MOVE) {
        return FALSE;
    }
    u16 moves[4];
    int count = 0;
    for (int m = 0; m < 4; m++) {
        moves[m] = GetMonData(mon, MON_DATA_MOVE1 + m, NULL);
        count += moves[m] != 0;
    }

    if (move != 0) {
        for (int m = 0; m < 4; m++) {
            if (moves[m] == move) {
                return FALSE; // it knows it already
            }
        }
        if (moves[slot] == 0) {
            slot = count; // no gaps
        }
        MonSetMoveInSlot_ResetPpUp(mon, move, slot);
        return CloseMon(&access, TRUE);
    }

    // Removing: the moves after it move up
    if (moves[slot] == 0 || count <= 1) {
        return FALSE;
    }
    for (int m = slot; m < 3; m++) {
        MonSwapMoves(mon, m, m + 1);
    }
    u16 none = 0;
    u8 zero = 0;
    SetMonData(mon, MON_DATA_MOVE4, &none);
    SetMonData(mon, MON_DATA_MOVE4_PP, &zero);
    SetMonData(mon, MON_DATA_MOVE4_PP_UPS, &zero);
    return CloseMon(&access, TRUE);
}

// Forms that come from the held item (Giratina's Griseous Orb, Arceus' plates)
static void UpdateItemForm(Pokemon *mon) {
    if (Mon_UpdateGiratinaForm(mon) >= 0) {
        UpdateMonAbility(mon);
    }
    Pokemon_UpdateArceusForm(mon);
    CalcMonLevelAndStats(mon);
}

BOOL SaveEd_SetItem(SaveEdRef ref, u16 item) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL || item >= ITEMS_COUNT) {
        return FALSE;
    }
    u32 value = item;
    SetMonData(mon, MON_DATA_HELD_ITEM, &value);
    UpdateItemForm(mon);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_SetFriendship(SaveEdRef ref, int value) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u8 friendship = value < 0 ? 0 : (value > 255 ? 255 : value);
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    return CloseMon(&access, TRUE);
}

BOOL SaveEd_Heal(SaveEdRef ref) {
    MonAccess access;
    Pokemon *mon = OpenEditable(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    u32 hp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
    u32 status = 0;
    SetMonData(mon, MON_DATA_HP, &hp);
    SetMonData(mon, MON_DATA_STATUS, &status);
    RestoreBoxMonPP(Mon_GetBoxMon(mon));
    return CloseMon(&access, TRUE);
}

static int CompareMoveNames(const void *a, const void *b) {
    return strcmp(SaveEd_MoveName(*(const u16 *)a), SaveEd_MoveName(*(const u16 *)b));
}

int SaveEd_LearnableMoves(u16 species, int form, u16 *out, int max) {
    int count = 0;
    u16 learnset[MAX_LEARNED_MOVES + 1];
    for (int i = 0; i < (int)NELEMS(learnset); i++) {
        learnset[i] = LEVEL_UP_LEARNSET_END;
    }
    LoadLevelUpLearnset_HandleAlternateForm(species, form, learnset);
    int levelCount = 0;
    while (levelCount < MAX_LEARNED_MOVES && learnset[levelCount] != LEVEL_UP_LEARNSET_END) {
        levelCount++;
    }

    for (int i = 0; i < levelCount + 100 && count < max; i++) {
        u16 move;
        if (i < levelCount) {
            move = LEVEL_UP_LEARNSET_MOVE(learnset[i]);
        } else {
            int tm = i - levelCount; // TM01..TM92, HM01..HM08
            if (!GetTMHMCompatBySpeciesAndForm(species, form, tm)) {
                continue;
            }
            move = TMHMGetMove(ITEM_TM01 + tm);
        }
        if (move == 0 || move > SAVE_ED_MAX_MOVE) {
            continue;
        }
        BOOL known = FALSE;
        for (int j = 0; j < count && !known; j++) {
            known = out[j] == move;
        }
        if (!known) {
            out[count++] = move;
        }
    }
    qsort(out, count, sizeof(u16), CompareMoveNames);
    return count;
}

int SaveEd_MoveType(u16 move) {
    return GetMoveAttr(move, MOVEATTR_TYPE);
}

int SaveEd_MovePower(u16 move) {
    return GetMoveAttr(move, MOVEATTR_POWER);
}

int SaveEd_MovePP(u16 move) {
    return GetMoveAttr(move, MOVEATTR_PP);
}

void SaveEd_NatureStats(int nature, int *raised, int *lowered) {
    // Natures go through Attack, Defense, Speed, Sp. Atk, Sp. Def
    static const int stats[5] = { SAVE_ED_ATK, SAVE_ED_DEF, SAVE_ED_SPEED, SAVE_ED_SPATK, SAVE_ED_SPDEF };
    nature = nature < 0 ? 0 : nature % 25;
    *raised = stats[nature / 5];
    *lowered = stats[nature % 5];
}

// Into the party (1) or the PC's first free slot (2, *box and *slot), as the game's GiveMon and the PC do; 0: no room
static int Store(SaveData *saveData, Pokemon *mon, BOOL toParty, int *box, int *slot) {
    Party *party = SaveArray_Party_Get(saveData);
    PCStorage *pcStorage = SaveArray_PCStorage_Get(saveData);
    if (toParty && Party_GetCount(party) < PARTY_SIZE) {
        Party_AddMon(party, mon);
        return 1;
    }
    int boxID = -1, boxSlot = 0;
    if (!PCStorage_FindFirstEmptySlot(pcStorage, &boxID, &boxSlot)) {
        return 0;
    }
    PCStorage_PlaceMonInBoxByIndexPair(pcStorage, boxID, boxSlot, Mon_GetBoxMon(mon));
    PCStorage_SetBoxModified(pcStorage, boxID);
    if (box != NULL) {
        *box = boxID;
    }
    if (slot != NULL) {
        *slot = boxSlot;
    }
    return 2;
}

int SaveEd_GiveMon(const SaveEdNewMon *newMon, BOOL toParty, int *box, int *slot) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL || newMon->species < 1 || newMon->species > SAVE_ED_MAX_SPECIES) {
        return 0;
    }

    PlayerProfile *profile = Save_PlayerData_GetProfile(saveData);
    const u32 otID = PlayerProfile_GetTrainerID(profile);
    u32 personality;
    if (!FindPersonality(newMon->species, 0, otID, newMon->nature, -1, newMon->shiny, -1, &personality)) {
        return 0;
    }
    int level = newMon->level < 1 ? 1 : (newMon->level > SAVE_ED_MAX_LEVEL ? SAVE_ED_MAX_LEVEL : newMon->level);

    // As the script command GiveMon does (script_pokemon_util.c), with the personality chosen here
    Pokemon *mon = AllocMonZeroed(HEAP_ID_DEFAULT);
    ZeroMonData(mon);
    CreateMon(mon, newMon->species, level, 32, TRUE, personality, OT_ID_PRESET, otID);
    const u32 mapsec = MapHeader_GetMapSec(FieldSystem_PortGet()->location->mapId);
    MonSetCatchData(mon, profile, ITEM_POKE_BALL, mapsec, 24, HEAP_ID_DEFAULT);
    if (newMon->maxIVs) {
        u8 iv = SAVE_ED_MAX_IV;
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            SetMonData(mon, sIvParams[s], &iv);
        }
    }
    u32 item = newMon->item < ITEMS_COUNT ? newMon->item : ITEM_NONE;
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
    UpdateItemForm(mon);

    const int result = Store(saveData, mon, toParty, box, slot);
    if (result != 0) {
        UpdatePokedexWithReceivedSpecies(saveData, mon);
    }
    Heap_Free(mon);
    return result;
}

// --- .pk4 files --------------------------------------------------------------------------------------------------

// pokemon.c's own (not in its header)
PokemonDataBlock *GetSubstruct(BoxPokemon *boxMon, u32 pid, u8 which_struct);
void MonEncryptSegment(void *data, u32 size, u32 key);
u32 CalcMonChecksum(void *data, u32 size);

// .pk4: the 136-byte stored format of PKHeX and other tools (personality, flags, checksum, then the four 32-byte blocks
// decrypted and in A B C D order)
static void BoxMonToPk4(const BoxPokemon *boxMon, u8 *out) {
    BoxPokemon copy = *boxMon;
    AcquireBoxMonLock(&copy); // decrypted

    memset(out, 0, SAVE_ED_PK4_SIZE);
    memcpy(out, &copy.personality, 4);
    memcpy(out + 6, &copy.checksum, 2);
    for (int i = 0; i < 4; i++) {
        memcpy(out + 8 + 32 * i, GetSubstruct(&copy, copy.personality, i), 32);
    }
}

// FALSE when the data is no Pokemon (bad checksum); out is encrypted as a stored Pokemon
static BOOL BoxMonFromPk4(const u8 *pk4, BoxPokemon *out) {
    BoxPokemon boxMon;
    memset(&boxMon, 0, sizeof(boxMon));
    memcpy(&boxMon.personality, pk4, 4);
    u16 checksum;
    memcpy(&checksum, pk4 + 6, 2);
    for (int i = 0; i < 4; i++) {
        memcpy(GetSubstruct(&boxMon, boxMon.personality, i), pk4 + 8 + 32 * i, 32);
    }
    if ((u16)CalcMonChecksum(boxMon.dataBlocks, sizeof(boxMon.dataBlocks)) != checksum) {
        return FALSE;
    }
    boxMon.checksum = checksum;
    MonEncryptSegment(boxMon.dataBlocks, sizeof(boxMon.dataBlocks), boxMon.checksum);
    *out = boxMon;
    return TRUE;
}

BOOL SaveEd_ExportPk4(SaveEdRef ref, u8 *out) {
    MonAccess access;
    Pokemon *mon = OpenMon(ref, &access);
    if (mon == NULL) {
        return FALSE;
    }
    BoxMonToPk4(Mon_GetBoxMon(mon), out);
    CloseMon(&access, FALSE);
    return TRUE;
}

int SaveEd_ImportPk4(const u8 *data, int size, BOOL toParty, int *box, int *slot) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return 0;
    }
    // 136 bytes stored, 236 with the party stats (which are worked out again)
    if (size != SAVE_ED_PK4_SIZE && size != 236) {
        return -1;
    }

    BoxPokemon boxMon;
    if (!BoxMonFromPk4(data, &boxMon)) {
        return -1;
    }
    Pokemon *mon = AllocMonZeroed(HEAP_ID_DEFAULT);
    CopyBoxPokemonToPokemon(&boxMon, mon);
    const u32 species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    if (species < 1 || species > SAVE_ED_MAX_SPECIES) {
        Heap_Free(mon);
        return -1;
    }
    u32 hp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
    SetMonData(mon, MON_DATA_HP, &hp);

    const int result = Store(saveData, mon, toParty, box, slot);
    if (result != 0 && !GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
        UpdatePokedexWithReceivedSpecies(saveData, mon);
    }
    Heap_Free(mon);
    return result;
}

// --- Bag ---------------------------------------------------------------------------------------------------------

const char *SaveEd_PocketName(int pocket) {
    static const char *const names[SAVE_ED_POCKETS] = {
        [POCKET_ITEMS] = "Items",
        [POCKET_MEDICINE] = "Medicine",
        [POCKET_BALLS] = "Poke Balls",
        [POCKET_TMHMS] = "TMs & HMs",
        [POCKET_BERRIES] = "Berries",
        [POCKET_MAIL] = "Mail",
        [POCKET_BATTLE_ITEMS] = "Battle Items",
        [POCKET_KEY_ITEMS] = "Key Items",
    };
    return pocket >= 0 && pocket < SAVE_ED_POCKETS ? names[pocket] : "?";
}

static int PocketSize(int pocket) {
    switch (pocket) {
    case POCKET_ITEMS:
        return NUM_BAG_ITEMS;
    case POCKET_MEDICINE:
        return NUM_BAG_MEDICINE;
    case POCKET_BALLS:
        return NUM_BAG_BALLS;
    case POCKET_TMHMS:
        return NUM_BAG_TMS_HMS;
    case POCKET_BERRIES:
        return NUM_BAG_BERRIES;
    case POCKET_MAIL:
        return NUM_BAG_MAIL;
    case POCKET_BATTLE_ITEMS:
        return NUM_BAG_BATTLE_ITEMS;
    case POCKET_KEY_ITEMS:
        return NUM_BAG_KEY_ITEMS;
    default:
        return 0;
    }
}

int SaveEd_BagPocket(int pocket, SaveEdBagItem *out, int max) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return 0;
    }
    Bag *bag = Save_Bag_Get(saveData);
    const int size = PocketSize(pocket);
    int count = 0;
    for (int i = 0; i < size && count < max; i++) {
        ItemSlot *slot = Bag_GetPocketSlotN(bag, pocket, i);
        if (slot != NULL && slot->id != ITEM_NONE && slot->quantity > 0) {
            out[count].item = slot->id;
            out[count].quantity = slot->quantity;
            count++;
        }
    }
    return count;
}

static s8 sItemPockets[ITEMS_COUNT];
static BOOL sItemPocketsLoaded = FALSE;

static void LoadItemPockets(void) {
    if (sItemPocketsLoaded) {
        return;
    }
    sItemPocketsLoaded = TRUE;
    sItemPockets[ITEM_NONE] = -1;
    for (int item = 1; item < ITEMS_COUNT; item++) {
        const char *name = SaveEd_ItemName(item);
        // Unused entries are named "???"
        if (name[0] == '\0' || strcmp(name, "???") == 0 || strcmp(name, "?") == 0) {
            sItemPockets[item] = -1;
            continue;
        }
        sItemPockets[item] = GetItemAttr(item, ITEMATTR_FIELD_POCKET, HEAP_ID_DEFAULT);
    }
}

int SaveEd_ItemPocket(u16 item) {
    if (item == ITEM_NONE || item >= ITEMS_COUNT) {
        return -1;
    }
    LoadItemPockets();
    return sItemPockets[item];
}

static int CompareItemNames(const void *a, const void *b) {
    u16 itemA = *(const u16 *)a, itemB = *(const u16 *)b;
    // TMs and HMs keep their numbers' order
    BOOL tmA = itemA >= ITEM_TM01 && itemA <= ITEM_HM08, tmB = itemB >= ITEM_TM01 && itemB <= ITEM_HM08;
    if (tmA && tmB) {
        return (int)itemA - (int)itemB;
    }
    return strcmp(SaveEd_ItemName(itemA), SaveEd_ItemName(itemB));
}

int SaveEd_PocketItems(int pocket, u16 *out, int max) {
    LoadItemPockets();
    int count = 0;
    for (int item = 1; item < ITEMS_COUNT && count < max; item++) {
        if (sItemPockets[item] == pocket) {
            out[count++] = item;
        }
    }
    qsort(out, count, sizeof(u16), CompareItemNames);
    return count;
}

int SaveEd_ItemMaxQuantity(u16 item) {
    switch (SaveEd_ItemPocket(item)) {
    case POCKET_KEY_ITEMS:
        return 1;
    case POCKET_TMHMS:
        return item >= ITEM_HM01 ? 1 : 99;
    case -1:
        return 0;
    default:
        return 999;
    }
}

int SaveEd_ItemQuantity(u16 item) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL || SaveEd_ItemPocket(item) < 0) {
        return 0;
    }
    return Bag_GetQuantity(Save_Bag_Get(saveData), item, HEAP_ID_DEFAULT);
}

BOOL SaveEd_SetItemQuantity(u16 item, int quantity) {
    SaveData *saveData = EditableSave();
    const int max = SaveEd_ItemMaxQuantity(item);
    if (saveData == NULL || max == 0) {
        return FALSE;
    }
    quantity = quantity < 0 ? 0 : (quantity > max ? max : quantity);
    Bag *bag = Save_Bag_Get(saveData);
    int current = Bag_GetQuantity(bag, item, HEAP_ID_DEFAULT);
    if (quantity > current) {
        return Bag_AddItem(bag, item, quantity - current, HEAP_ID_DEFAULT);
    }
    if (quantity < current) {
        Bag_TakeItem(bag, item, current - quantity, HEAP_ID_DEFAULT);
        if (quantity == 0) {
            Bag_UnregisterItem(bag, item); // off the Y button
        }
        return TRUE;
    }
    return FALSE;
}

// --- Trainer -----------------------------------------------------------------------------------------------------

const char *SaveEd_BadgeName(int badge) {
    static const char *const names[SAVE_ED_BADGES] = {
        [BADGE_ZEPHYR] = "Zephyr",
        [BADGE_HIVE] = "Hive",
        [BADGE_PLAIN] = "Plain",
        [BADGE_FOG] = "Fog",
        [BADGE_STORM] = "Storm",
        [BADGE_MINERAL] = "Mineral",
        [BADGE_GLACIER] = "Glacier",
        [BADGE_RISING] = "Rising",
        [BADGE_BOULDER] = "Boulder",
        [BADGE_CASCADE] = "Cascade",
        [BADGE_THUNDER] = "Thunder",
        [BADGE_RAINBOW] = "Rainbow",
        [BADGE_SOUL] = "Soul",
        [BADGE_MARSH] = "Marsh",
        [BADGE_VOLCANO] = "Volcano",
        [BADGE_EARTH] = "Earth",
    };
    return badge >= 0 && badge < SAVE_ED_BADGES ? names[badge] : "?";
}

void SaveEd_GetTrainer(SaveEdTrainer *out) {
    memset(out, 0, sizeof(*out));
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return;
    }
    PlayerProfile *profile = Save_PlayerData_GetProfile(saveData);
    TextToAscii(PlayerProfile_GetNamePtr(profile), PLAYER_NAME_LENGTH + 1, out->name, sizeof(out->name));
    out->id = PlayerProfile_GetTrainerID_VisibleHalf(profile);
    out->money = PlayerProfile_GetMoney(profile);
    out->battlePoints = FrontierData_BattlePointAction(Save_FrontierData_Get(saveData), 0, FRONTIER_DATA_GET);
    for (int badge = 0; badge < SAVE_ED_BADGES; badge++) {
        if (PlayerProfile_TestBadgeFlag(profile, badge)) {
            out->badges |= 1 << badge;
        }
    }
    IGT *igt = Save_PlayerData_GetIGTAddr(saveData);
    out->hours = GetIGTHours(igt);
    out->minutes = GetIGTMinutes(igt);
    out->nationalDex = Pokedex_GetNatDexFlag(Save_Pokedex_Get(saveData));
}

void SaveEd_SetMoney(u32 money) {
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        PlayerProfile_SetMoney(Save_PlayerData_GetProfile(saveData), money > SAVE_ED_MAX_MONEY ? SAVE_ED_MAX_MONEY : money);
    }
}

void SaveEd_SetBattlePoints(int points) {
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        points = points < 0 ? 0 : (points > SAVE_ED_MAX_BP ? SAVE_ED_MAX_BP : points);
        FrontierData_BattlePointAction(Save_FrontierData_Get(saveData), points, FRONTIER_DATA_SET);
    }
}

void SaveEd_SetBadge(int badge, BOOL has) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL || badge < 0 || badge >= SAVE_ED_BADGES) {
        return;
    }
    PlayerProfile *profile = Save_PlayerData_GetProfile(saveData);
    if (has) {
        PlayerProfile_SetBadgeFlag(profile, badge);
    } else if (badge < 8) {
        profile->johtoBadges &= ~(1 << badge);
    } else {
        profile->kantoBadges &= ~(1 << (badge - 8));
    }
}

void SaveEd_SetPlayTime(int hours, int minutes) {
    SaveData *saveData = EditableSave();
    if (saveData == NULL) {
        return;
    }
    IGT *igt = Save_PlayerData_GetIGTAddr(saveData);
    igt->hours = hours < 0 ? 0 : (hours > SAVE_ED_MAX_HOURS ? SAVE_ED_MAX_HOURS : hours);
    igt->minutes = minutes < 0 ? 0 : (minutes > 59 ? 59 : minutes);
}

void SaveEd_GiveNationalDex(void) {
    SaveData *saveData = EditableSave();
    if (saveData != NULL) {
        // As the game does (scrcmd_c.c, the National Pokedex's script command)
        Pokedex_SetNatDexFlag(Save_Pokedex_Get(saveData));
        PlayerProfile_SetNatDexFlag(Save_PlayerData_GetProfile(saveData));
    }
}
