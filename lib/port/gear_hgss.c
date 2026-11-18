// The Gear's data adapters for HeartGold and SoulSilver (port-common's port/gear_*.h), as the Platinum fork's
// src/port/gear_*.c, first part: the party, the trainer, names, the species lookup (stats, abilities, evolutions,
// level-up moves), the Pokemon pictures, location names, the area's wild Pokemon, and the bosses (leaders, the
// League, Red) with the level cap. Not yet (empty, so the Gear
// hides or explains them): the battle tab and its menu (Goldenrod's battles are mostly in BW2's engine, which has no
// BattleSystem to read), where to find a species, the map, the PC tab, the Day Care, berries and the Pokétch tab
// (HGSS's touch screen is the Pokégear).
#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"

#include "constants/species.h"

#include "battle/battle_util.h"
#include "field_system.h"
#include "filesystem.h"
#include "gf_rtc.h"
#include "map_events.h"
#include "map_header.h"
#include "msgdata.h"
#include "msgdata/msg.naix"
#include "pm_string.h"
#include "roamer.h"
#include "save_vars_flags.h"
#include "sys_flags.h"
#include "trainer_data.h"
#include "constants/trainers.h"
#include "swarm.h"
#include "wild_encounter.h"
#include "move.h"
#include "party.h"
#include "player_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_icon_idx.h"
#include "port/gear_berries.h"
#include "port/gear_data.h"
#include "port/gear_daycare.h"
#include "port/gear_dex.h"
#include "port/gear_map.h"
#include "port/gear_pc.h"
#include "port/gear_sprites.h"
#include "port_save_editor.h"
#include "save.h"

void UnscanPokepic(u8 *pRawData, NarcId narcId); // src/pokepic.c

// --- the running game -------------------------------------------------------------------------------------------

static SaveData *RunningSave(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL ? fieldSystem->saveData : NULL;
}

BOOL Gear_IsGameRunning(void) {
    return FieldSystem_PortGet() != NULL;
}

// --- names (the save editor's caches: lib/port/save_editor.c) ------------------------------------------------------

void Gear_TextToAscii(const u16 *chars, int maxLen, char *out, int outSize) {
    static const struct {
        u16 c;
        char a;
    } kPunctuation[] = {
        { 0x1DE, ' ' }, { 0x1AB, '!' }, { 0x1AC, '?' }, { 0x1AD, ',' }, { 0x1AE, '.' }, { 0x1B1, '/' },
        { 0x1B3, '\'' }, { 0x1B9, '(' }, { 0x1BA, ')' }, { 0x1BE, '-' }, { 0x1C4, ':' }, { 0x188, 'e' },
        { 0x1C0, '&' },
    };
    int pos = 0;
    for (int i = 0; i < maxLen && chars[i] != EOS && pos < outSize - 1; i++) {
        const u16 c = chars[i];
        // Nidoran's symbols
        if (c == 0x1BB || c == 0x1BC) {
            if (pos < outSize - 3) {
                out[pos++] = ' ';
                out[pos++] = c == 0x1BB ? 'M' : 'F';
            }
            continue;
        }
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

const char *Gear_SpeciesName(u16 species) {
    return SaveEd_SpeciesName(species);
}

const char *Gear_MoveName(u16 move) {
    return SaveEd_MoveName(move);
}

const char *Gear_ItemName(u16 item) {
    return SaveEd_ItemName(item);
}

const char *Gear_AbilityName(u8 ability) {
    return SaveEd_AbilityName(ability);
}

const char *Gear_NatureName(u8 nature) {
    return SaveEd_NatureName(nature);
}

const char *Gear_TypeName(u8 type) {
    return SaveEd_TypeName(type);
}

// Location names: the map sections' names (msg 0279), which HGSS's map labels are
#define LOCATION_NAMES_MAX 320
static char sLocationNames[LOCATION_NAMES_MAX][GEAR_NAME_LEN];
static int sLocationNameCount = -1;

const char *Gear_LocationLabelName(int labelTextID) {
    if (sLocationNameCount < 0) {
        MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0279_bin, HEAP_ID_DEFAULT);
        sLocationNameCount = (int)MsgDataGetCount(msgData);
        if (sLocationNameCount > LOCATION_NAMES_MAX) {
            sLocationNameCount = LOCATION_NAMES_MAX;
        }
        for (int i = 0; i < sLocationNameCount; i++) {
            String *string = NewString_ReadMsgData(msgData, i);
            Gear_TextToAscii(String_cstr(string), String_GetLength(string), sLocationNames[i], GEAR_NAME_LEN);
            String_Delete(string);
        }
        DestroyMsgData(msgData);
    }
    return labelTextID >= 0 && labelTextID < sLocationNameCount ? sLocationNames[labelTextID] : "";
}

const char *Gear_LocationName(int mapHeaderID) {
    return mapHeaderID >= 0 ? Gear_LocationLabelName((int)MapHeader_GetMapSec(mapHeaderID)) : "";
}

int Gear_CurrentLocationLabel(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->location == NULL) {
        return -1;
    }
    return (int)MapHeader_GetMapSec(fieldSystem->location->mapId);
}

// --- moves and types -------------------------------------------------------------------------------------------------

void Gear_MoveInfo(u16 move, GearMove *out) {
    memset(out, 0, sizeof(*out));
    out->id = move;
    if (move == 0) {
        return;
    }
    out->type = GetMoveAttr(move, MOVEATTR_TYPE);
    out->power = GetMoveAttr(move, MOVEATTR_POWER);
    out->accuracy = GetMoveAttr(move, MOVEATTR_ACCURACY);
    out->category = GetMoveAttr(move, MOVEATTR_CLASS);
}

// The battle code's own table (in fortieths): in tenths
int Gear_TypeEffectiveness(u8 attackType, u8 defenderType1, u8 defenderType2) {
    return CalculateTypeEffectiveness(attackType, defenderType1, defenderType2) / 4;
}

BOOL Gear_TypeNineIsFairy(void) {
    return FALSE;
}

const char *Gear_GBASlotGameName(void) {
    return NULL;
}

// --- the Pokedex ------------------------------------------------------------------------------------------------------

BOOL Gear_SpeciesSeen(u16 species) {
    SaveData *saveData = RunningSave();
    return saveData != NULL && Pokedex_CheckMonSeenFlag(Save_Pokedex_Get(saveData), species);
}

BOOL Gear_SpeciesCaught(u16 species) {
    SaveData *saveData = RunningSave();
    return saveData != NULL && Pokedex_CheckMonCaughtFlag(Save_Pokedex_Get(saveData), species);
}

// --- the party and the trainer -------------------------------------------------------------------------------------

static void FillMon(Pokemon *mon, GearMon *gm) {
    memset(gm, 0, sizeof(*gm));
    gm->isEgg = GetMonData(mon, MON_DATA_IS_EGG, NULL);
    gm->species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    gm->form = GetMonData(mon, MON_DATA_FORM, NULL);
    gm->personality = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
    if (gm->isEgg) {
        snprintf(gm->nickname, sizeof(gm->nickname), "Egg");
        return;
    }
    if (GetMonData(mon, MON_DATA_HAS_NICKNAME, NULL)) {
        u16 nickname[POKEMON_NAME_LENGTH + 1];
        GetMonData(mon, MON_DATA_NICKNAME, nickname);
        Gear_TextToAscii(nickname, POKEMON_NAME_LENGTH + 1, gm->nickname, sizeof(gm->nickname));
    } else {
        snprintf(gm->nickname, sizeof(gm->nickname), "%s", Gear_SpeciesName(gm->species));
    }
    gm->shiny = MonIsShiny(mon) ? TRUE : FALSE;
    gm->level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    gm->gender = GetMonGender(mon);
    gm->hp = GetMonData(mon, MON_DATA_HP, NULL);
    gm->maxHp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
    gm->status = GetMonData(mon, MON_DATA_STATUS, NULL);
    gm->type1 = GetMonData(mon, MON_DATA_TYPE_1, NULL);
    gm->type2 = GetMonData(mon, MON_DATA_TYPE_2, NULL);
    gm->heldItem = GetMonData(mon, MON_DATA_HELD_ITEM, NULL);
    gm->ability = GetMonData(mon, MON_DATA_ABILITY, NULL);
    gm->nature = GetMonNature(mon);
    gm->exp = GetMonData(mon, MON_DATA_EXPERIENCE, NULL);
    gm->expLevelStart = GetMonExpBySpeciesAndLevel(gm->species, gm->level);
    gm->expNextLevel = gm->level < 100 ? GetMonExpBySpeciesAndLevel(gm->species, gm->level + 1) : gm->exp;
    gm->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP, NULL);
    static const int kStats[6] = { MON_DATA_MAX_HP, MON_DATA_ATK, MON_DATA_DEF, MON_DATA_SP_ATK, MON_DATA_SP_DEF, MON_DATA_SPEED };
    static const int kIVs[6] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV, MON_DATA_SPEED_IV };
    static const int kEVs[6] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV, MON_DATA_SPEED_EV };
    for (int s = 0; s < 6; s++) {
        gm->stats[s] = GetMonData(mon, kStats[s], NULL);
        gm->ivs[s] = GetMonData(mon, kIVs[s], NULL);
        gm->evs[s] = GetMonData(mon, kEVs[s], NULL);
    }
    for (int m = 0; m < 4; m++) {
        Gear_MoveInfo(GetMonData(mon, MON_DATA_MOVE1 + m, NULL), &gm->moves[m]);
        if (gm->moves[m].id != 0) {
            gm->moves[m].pp = GetMonData(mon, MON_DATA_MOVE1_PP + m, NULL);
            gm->moves[m].maxPp = GetMonData(mon, MON_DATA_MOVE1_MAX_PP + m, NULL);
        }
    }
}

int Gear_GetParty(GearMon out[GEAR_PARTY_MAX]) {
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return 0;
    }
    Party *party = SaveArray_Party_Get(saveData);
    int count = Party_GetCount(party);
    if (count > GEAR_PARTY_MAX) {
        count = GEAR_PARTY_MAX;
    }
    for (int i = 0; i < count; i++) {
        FillMon(Party_GetMonByIndex(party, i), &out[i]);
    }
    return count;
}

// Not yet: HGSS's save editor has no rename
const char *Gear_RenamePartyMon(int index, const char *name) {
    (void)index;
    (void)name;
    return "Renaming isn't in Goldenrod's Gear yet.";
}

void Gear_GetTrainer(GearTrainer *out) {
    memset(out, 0, sizeof(*out));
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return;
    }
    PlayerProfile *profile = Save_PlayerData_GetProfile(saveData);
    Gear_TextToAscii(PlayerProfile_GetNamePtr(profile), PLAYER_NAME_LENGTH + 1, out->name, sizeof(out->name));
    out->gender = PlayerProfile_GetTrainerGender(profile);
    out->id = PlayerProfile_GetTrainerID(profile) & 0xFFFF;
    out->money = PlayerProfile_GetMoney(profile);
    for (int i = 0; i < 8; i++) {
        if (PlayerProfile_TestBadgeFlag(profile, i)) {
            out->badgeMask |= 1 << i;
            out->badgeCount++;
        }
    }
    const IGT *igt = Save_PlayerData_GetIGTAddr(saveData);
    out->hours = igt->hours;
    out->minutes = igt->minutes;
    Pokedex *pokedex = Save_Pokedex_Get(saveData);
    out->pokedexObtained = Pokedex_IsEnabled(pokedex);
    out->nationalDex = Pokedex_GetNatDexFlag(pokedex);
    out->seenLocal = Pokedex_CountJohtoDexSeen(pokedex);
    out->caughtLocal = Pokedex_CountJohtoDexOwned(pokedex);
    out->seenNational = Pokedex_CountNationalDexSeen(pokedex);
    out->caughtNational = Pokedex_CountNationalDexOwned(pokedex);
}

// --- the species lookup -----------------------------------------------------------------------------------------

BOOL Gear_GetSpecies(u16 species, GearSpecies *out) {
    memset(out, 0, sizeof(*out));
    if (species == SPECIES_NONE || species > SPECIES_ARCEUS) {
        return FALSE;
    }
    out->species = species;
    out->types[0] = GetMonBaseStat(species, BASE_TYPE1);
    out->types[1] = GetMonBaseStat(species, BASE_TYPE2);
    out->abilities[0] = GetMonBaseStat(species, BASE_ABILITY_1);
    out->abilities[1] = GetMonBaseStat(species, BASE_ABILITY_2);
    // HP, Atk, Def, SpA, SpD, Spe (the base stats are stored HP, Atk, Def, Spe, SpA, SpD)
    static const int kBase[6] = { BASE_HP, BASE_ATK, BASE_DEF, BASE_SPATK, BASE_SPDEF, BASE_SPEED };
    static const int kYield[6] = { BASE_HP_YIELD, BASE_ATK_YIELD, BASE_DEF_YIELD, BASE_SPATK_YIELD, BASE_SPDEF_YIELD, BASE_SPEED_YIELD };
    for (int s = 0; s < 6; s++) {
        out->baseStats[s] = GetMonBaseStat(species, kBase[s]);
        out->evYield[s] = GetMonBaseStat(species, kYield[s]);
    }
    out->catchRate = GetMonBaseStat(species, BASE_CATCH_RATE);
    out->genderRatio = GetMonBaseStat(species, BASE_GENDER_RATIO);
    out->eggGroups[0] = GetMonBaseStat(species, BASE_EGG_GROUP_1);
    out->eggGroups[1] = GetMonBaseStat(species, BASE_EGG_GROUP_2);
    out->hatchCycles = GetMonBaseStat(species, BASE_EGG_CYCLES);
    out->expRate = GetMonBaseStat(species, BASE_GROWTH_RATE);
    out->baseFriendship = GetMonBaseStat(species, BASE_FRIENDSHIP);
    out->heldItems[0] = GetMonBaseStat(species, BASE_ITEM_1);
    out->heldItems[1] = GetMonBaseStat(species, BASE_ITEM_2);

    struct Evolution evos[MAX_EVOS_PER_POKE];
    LoadMonEvolutionTable(species, evos);
    for (int i = 0; i < MAX_EVOS_PER_POKE && out->evoCount < GEAR_DEX_MAX_EVOS; i++) {
        if (evos[i].method != 0 && evos[i].target != SPECIES_NONE) {
            GearDexEvolution *evo = &out->evos[out->evoCount++];
            evo->method = evos[i].method;
            evo->param = evos[i].param;
            evo->species = evos[i].target;
        }
    }
    // Pre-evolutions: every species that evolves into this one
    for (u16 other = 1; other <= SPECIES_ARCEUS && out->preEvoCount < (int)NELEMS(out->preEvos); other++) {
        if (other == species) {
            continue;
        }
        LoadMonEvolutionTable(other, evos);
        for (int i = 0; i < MAX_EVOS_PER_POKE; i++) {
            if (evos[i].method != 0 && evos[i].target == species && out->preEvoCount < (int)NELEMS(out->preEvos)) {
                GearDexEvolution *evo = &out->preEvos[out->preEvoCount++];
                evo->method = evos[i].method;
                evo->param = evos[i].param;
                evo->species = other;
            }
        }
    }

    u16 learnset[LEVEL_UP_LEARNSET_MAX + 1];
    LoadLevelUpLearnset_HandleAlternateForm(species, 0, learnset);
    for (int i = 0; i < LEVEL_UP_LEARNSET_MAX && learnset[i] != LEVEL_UP_LEARNSET_END
         && out->levelMoveCount < GEAR_DEX_MAX_LEVEL_MOVES; i++) {
        out->levelMoves[out->levelMoveCount].move = LEVEL_UP_LEARNSET_MOVE(learnset[i]);
        out->levelMoves[out->levelMoveCount].level = LEVEL_UP_LEARNSET_LVL(learnset[i]);
        out->levelMoveCount++;
    }
    // Not yet: TMs, egg and tutor moves, and where to find it
    return TRUE;
}

void Gear_SpeciesTypes(u16 species, u8 *type1, u8 *type2) {
    *type1 = GetMonBaseStat(species, BASE_TYPE1);
    *type2 = GetMonBaseStat(species, BASE_TYPE2);
}

static int CompareByName(const void *a, const void *b) {
    return strcmp(Gear_SpeciesName(*(const u16 *)a), Gear_SpeciesName(*(const u16 *)b));
}

int Gear_SpeciesByName(u16 *out, int max) {
    int count = 0;
    for (u16 species = 1; species <= SPECIES_ARCEUS && count < max; species++) {
        out[count++] = species;
    }
    qsort(out, count, sizeof(u16), CompareByName);
    return count;
}

// Gen 4's methods, numbered as both games number them
void Gear_EvolutionText(const GearDexEvolution *evo, char *out, int size) {
    const char *item = Gear_ItemName(evo->param);
    switch (evo->method) {
    case 1:
        snprintf(out, size, "High friendship");
        break;
    case 2:
        snprintf(out, size, "High friendship, daytime");
        break;
    case 3:
        snprintf(out, size, "High friendship, night");
        break;
    case 4:
    case 13:
        snprintf(out, size, "Level %u", evo->param);
        break;
    case 5:
        snprintf(out, size, "Trade");
        break;
    case 6:
        snprintf(out, size, "Trade holding %s", item);
        break;
    case 7:
        snprintf(out, size, "Use %s", item);
        break;
    case 8:
        snprintf(out, size, "Level %u, Attack > Defense", evo->param);
        break;
    case 9:
        snprintf(out, size, "Level %u, Attack = Defense", evo->param);
        break;
    case 10:
        snprintf(out, size, "Level %u, Attack < Defense", evo->param);
        break;
    case 11:
    case 12:
        snprintf(out, size, "Level %u (depends on the Pokemon)", evo->param);
        break;
    case 14:
        snprintf(out, size, "Level %u, a free party slot and a Poke Ball", evo->param);
        break;
    case 15:
        snprintf(out, size, "Level up with Beauty %u+", evo->param);
        break;
    case 16:
        snprintf(out, size, "Use %s (male)", item);
        break;
    case 17:
        snprintf(out, size, "Use %s (female)", item);
        break;
    case 18:
        snprintf(out, size, "Level up holding %s, daytime", item);
        break;
    case 19:
        snprintf(out, size, "Level up holding %s, night", item);
        break;
    case 20:
        snprintf(out, size, "Level up knowing %s", Gear_MoveName(evo->param));
        break;
    case 21:
        snprintf(out, size, "Level up with %s in the party", Gear_SpeciesName(evo->param));
        break;
    case 22:
        snprintf(out, size, "Level %u (male)", evo->param);
        break;
    case 23:
        snprintf(out, size, "Level %u (female)", evo->param);
        break;
    case 24:
        snprintf(out, size, "Level up at Mt. Coronet");
        break;
    case 25:
        snprintf(out, size, "Level up near the Moss Rock");
        break;
    case 26:
        snprintf(out, size, "Level up near the Ice Rock");
        break;
    default:
        snprintf(out, size, "Method %u (%u)", evo->method, evo->param);
        break;
    }
}

const char *Gear_EggGroupName(u8 group) {
    static const char *const kNames[] = { "-", "Monster", "Water 1", "Bug", "Flying", "Field", "Fairy", "Grass",
        "Human-Like", "Water 3", "Mineral", "Amorphous", "Water 2", "Ditto", "Dragon", "Undiscovered" };
    return group < NELEMS(kNames) ? kNames[group] : "?";
}

const char *Gear_ExpRateName(u8 rate) {
    static const char *const kNames[] = { "Medium Fast", "Erratic", "Fluctuating", "Medium Slow", "Fast", "Slow" };
    return rate < NELEMS(kNames) ? kNames[rate] : "?";
}

// Not yet: the TM table
u16 Gear_TMMove(int tm) {
    (void)tm;
    return 0;
}

// --- Pokemon pictures, from the ROM's files into our own memory (not the game's heaps) ------------------------------

static u16 Read16(const u8 *p) {
    return (u16)(p[0] | (p[1] << 8));
}

static u32 Read32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u8 *ReadMember(NarcId narc, int member, u32 *size) {
    *size = GetNarcMemberSizeByIdPair(narc, member);
    if (*size == 0) {
        return NULL;
    }
    u8 *data = malloc(*size);
    if (data != NULL) {
        ReadWholeNarcMemberByIdPair(data, narc, member);
    }
    return data;
}

// The data of a block ("RAHC", "TTLP") of a G2D file, after its 8-byte block header
static u8 *FindBlock(u8 *file, u32 fileSize, const char *magic, u32 *blockSize) {
    if (fileSize < 16) {
        return NULL;
    }
    u32 offset = Read16(file + 12);
    const u16 blockCount = Read16(file + 14);
    for (int i = 0; i < blockCount && offset + 8 <= fileSize; i++) {
        const u32 size = Read32(file + offset + 4);
        if (size < 8 || offset + size > fileSize) {
            return NULL;
        }
        if (memcmp(file + offset, magic, 4) == 0) {
            *blockSize = size - 8;
            return file + offset + 8;
        }
        offset += size;
    }
    return NULL;
}

// 16-colour characters: their pixels, whether they are a bitmap (rows) rather than 8x8 tiles, and their size
static u8 *Characters(u8 *file, u32 fileSize, BOOL *bitmap, u32 *size) {
    u32 blockSize = 0;
    u8 *block = FindBlock(file, fileSize, "RAHC", &blockSize);
    if (block == NULL || blockSize < 24 || Read32(block + 4) != 3) {
        return NULL;
    }
    *bitmap = (Read32(block + 12) & 0xFF) == 1;
    *size = Read32(block + 16);
    const u32 offset = Read32(block + 20);
    return offset + *size <= blockSize ? block + offset : NULL;
}

static BOOL Palette(u8 *file, u32 fileSize, int first, u32 colors[16]) {
    u32 blockSize = 0;
    u8 *block = FindBlock(file, fileSize, "TTLP", &blockSize);
    if (block == NULL || blockSize < 16) {
        return FALSE;
    }
    const u32 dataSize = Read32(block + 8);
    const u32 offset = Read32(block + 12);
    const u8 *data = block + 16 + (offset > 16 ? offset - 16 : 0);
    if ((u32)(first + 16) * 2 > dataSize) {
        return FALSE;
    }
    for (int i = 0; i < 16; i++) {
        const u16 c = Read16(data + (first + i) * 2);
        const u32 r = (c & 0x1F) * 255 / 31, g = ((c >> 5) & 0x1F) * 255 / 31, b = ((c >> 10) & 0x1F) * 255 / 31;
        colors[i] = r | (g << 8) | (b << 16) | (i == 0 ? 0u : 0xFF000000u);
    }
    return TRUE;
}

static u8 Pixel(const u8 *pixels, BOOL bitmap, int width, int x, int y) {
    u32 index;
    if (bitmap) {
        index = (u32)(y * width + x);
    } else {
        const int tilesWide = width / 8;
        index = (u32)(((y / 8) * tilesWide + (x / 8)) * 64 + (y % 8) * 8 + (x % 8));
    }
    const u8 byte = pixels[index / 2];
    return (index & 1) ? (byte >> 4) : (byte & 0xF);
}

static void ToRGBA(const u8 *pixels, BOOL bitmap, int sourceWidth, int width, int height, const u32 *palette, u32 *rgba) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            rgba[y * width + x] = palette[Pixel(pixels, bitmap, sourceWidth, x, y)];
        }
    }
}

BOOL GearSprites_Icon(u16 species, u8 form, BOOL isEgg, u32 *rgba) {
    if (species == SPECIES_NONE || species > SPECIES_ARCEUS) {
        return FALSE;
    }
    u32 charSize = 0, palSize = 0;
    u8 *charFile = ReadMember(NARC_poketool_icongra_poke_icon, (int)GetMonIconNaixEx(species, isEgg, form), &charSize);
    u8 *palFile = ReadMember(NARC_poketool_icongra_poke_icon, (int)GetMonIconPlttNaix(), &palSize);
    BOOL ok = FALSE;
    BOOL bitmap = FALSE;
    u32 size = 0;
    u32 palette[16];
    u8 *pixels = charFile != NULL ? Characters(charFile, charSize, &bitmap, &size) : NULL;
    if (pixels != NULL && palFile != NULL && size >= GEAR_ICON_WIDTH * GEAR_ICON_HEIGHT / 2
        && Palette(palFile, palSize, GetMonIconPaletteEx(species, form, isEgg) * 16, palette)) {
        ToRGBA(pixels, bitmap, GEAR_ICON_WIDTH, GEAR_ICON_WIDTH, GEAR_ICON_HEIGHT, palette, rgba);
        ok = TRUE;
    }
    free(charFile);
    free(palFile);
    return ok;
}

#define SPRITE_SHEET_WIDTH 160 // two 80x80 frames side by side

BOOL GearSprites_Front(u16 species, u8 gender, u8 form, BOOL shiny, BOOL isEgg, u32 personality, u32 *rgba) {
    if (species == SPECIES_NONE || species > SPECIES_ARCEUS) {
        return FALSE;
    }
    PokepicTemplate sprite;
    if (isEgg) {
        GetMonSpriteCharAndPlttNarcIdsEx(&sprite, SPECIES_EGG, 0, MON_PIC_FACING_FRONT, FALSE, species == SPECIES_MANAPHY ? 1 : 0, 0);
    } else {
        GetMonSpriteCharAndPlttNarcIdsEx(&sprite, species, gender, MON_PIC_FACING_FRONT, shiny ? 1 : 0, form, personality);
    }
    u32 charSize = 0, palSize = 0;
    u8 *charFile = ReadMember((NarcId)sprite.narcID, sprite.charDataID, &charSize);
    u8 *palFile = ReadMember((NarcId)sprite.narcID, sprite.palDataID, &palSize);
    BOOL ok = FALSE;
    BOOL bitmap = FALSE;
    u32 size = 0;
    u32 palette[16];
    u8 *pixels = charFile != NULL ? Characters(charFile, charSize, &bitmap, &size) : NULL;
    if (pixels != NULL && palFile != NULL && size >= SPRITE_SHEET_WIDTH * GEAR_FRONT_SIZE / 2
        && Palette(palFile, palSize, 0, palette)) {
        UnscanPokepic(pixels, (NarcId)sprite.narcID);
        ToRGBA(pixels, TRUE, SPRITE_SHEET_WIDTH, GEAR_FRONT_SIZE, GEAR_FRONT_SIZE, palette, rgba);
        ok = TRUE;
    }
    free(charFile);
    free(palFile);
    return ok;
}

// --- not yet in Goldenrod ------------------------------------------------------------------------------------------

void Gear_SetBattleSystem(struct BattleSystem *battleSys) {
    (void)battleSys;
}

struct BattleSystem *Gear_ActiveBattleSystem(void) {
    return NULL;
}

BOOL Gear_GetBattle(GearBattle *out) {
    memset(out, 0, sizeof(*out));
    return FALSE;
}

BOOL Gear_GetBattleMenu(GearBattleMenu *out) {
    memset(out, 0, sizeof(*out));
    out->kind = GEAR_MENU_NONE;
    return FALSE;
}

void Gear_PressBattleButton(int button) {
    (void)button;
}

BOOL Gear_BattleNeedsDsScreen(void) {
    return FALSE;
}

void Gear_NoteBattleDsScreen(void) {
}

BOOL Gear_BattlePartyOnGear(int listMode) {
    (void)listMode;
    return FALSE;
}

// HGSS has no Pokétch: its tab stays hidden
int Gear_PoketchState(void) {
    return Gear_IsGameRunning() ? GEAR_POKETCH_NOT_OBTAINED : GEAR_POKETCH_NO_GAME;
}

int Gear_PoketchApp(void) {
    return -1;
}

const char *Gear_PoketchAppName(int app) {
    (void)app;
    return "";
}

// --- the area's wild Pokemon (the Area tab), from the map's encounter table as the field reads it ---------------------

static const u8 sGrassRates[NUM_ENCOUNTERS_LAND] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
static const u8 sWaterRates[NUM_ENCOUNTERS_SURF] = { 60, 30, 5, 4, 1 };
static const u8 sRodRates[NUM_ENCOUNTERS_FISH] = { 40, 40, 15, 4, 1 };

static void AddEncounter(GearArea *area, u8 method, u16 species, u8 minLevel, u8 maxLevel, u8 rate) {
    if (species == SPECIES_NONE) {
        return;
    }
    // The same species again by the same method: one entry, its rates added up and its levels widened
    for (int i = 0; i < area->count; i++) {
        GearEncounter *entry = &area->entries[i];
        if (entry->method == method && entry->species == species) {
            entry->rate += rate;
            entry->minLevel = minLevel < entry->minLevel ? minLevel : entry->minLevel;
            entry->maxLevel = maxLevel > entry->maxLevel ? maxLevel : entry->maxLevel;
            return;
        }
    }
    if (area->count < GEAR_AREA_MAX_ENTRIES) {
        GearEncounter *entry = &area->entries[area->count++];
        entry->method = method;
        entry->species = species;
        entry->minLevel = minLevel;
        entry->maxLevel = maxLevel;
        entry->rate = rate;
    }
}

static void AddWater(GearArea *area, u8 method, const EncounterDataSlot *slots, const u8 *rates) {
    for (int i = 0; i < NUM_ENCOUNTERS_FISH; i++) {
        AddEncounter(area, method, slots[i].species, slots[i].level_min, slots[i].level_max, rates[i]);
    }
}

static int CompareEncounters(const void *a, const void *b) {
    const GearEncounter *x = a, *y = b;
    if (x->method != y->method) {
        return x->method - y->method;
    }
    return y->rate - x->rate;
}

void Gear_GetArea(GearArea *out) {
    memset(out, 0, sizeof(*out));
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->location == NULL || fieldSystem->saveData == NULL) {
        return;
    }
    const u32 mapId = fieldSystem->location->mapId;
    out->valid = TRUE;
    out->mapHeaderID = (int)mapId;
    snprintf(out->name, sizeof(out->name), "%s", Gear_LocationName((int)mapId));
    // HGSS's grass has three tables: morning, day (and evening), night (and late night)
    const int timeOfDay = GF_RTC_GetTimeOfDay();
    out->timeOfDay = timeOfDay;
    if (!MapHeader_HasWildEncounters(mapId)) {
        return;
    }
    static EncounterData data;
    WildEncounters_ReadFromNarc(&data, mapId);
    const u16 *grass = timeOfDay == RTC_TIMEOFDAY_MORN ? data.landSlots.species_morn
        : (timeOfDay == RTC_TIMEOFDAY_DAY || timeOfDay == RTC_TIMEOFDAY_EVE) ? data.landSlots.species_day
                                                                             : data.landSlots.species_nite;
    if (data.encounterRate_walking != 0) {
        for (int i = 0; i < NUM_ENCOUNTERS_LAND; i++) {
            AddEncounter(out, GEAR_ENC_GRASS, grass[i], data.landSlots.levels[i], data.landSlots.levels[i], sGrassRates[i]);
        }
    }
    if (data.encounterRate_surfing != 0) {
        AddWater(out, GEAR_ENC_SURF, data.surfSlots, sWaterRates);
    }
    if (data.encounterRate_oldRod != 0) {
        AddWater(out, GEAR_ENC_OLD_ROD, data.oldRodSlots, sRodRates);
    }
    if (data.encounterRate_goodRod != 0) {
        AddWater(out, GEAR_ENC_GOOD_ROD, data.goodRodSlots, sRodRates);
    }
    if (data.encounterRate_superRod != 0) {
        AddWater(out, GEAR_ENC_SUPER_ROD, data.superRodSlots, sRodRates);
    }
    // A mass outbreak reported here (the radio's news): its species takes the first two grass slots
    RoamerSaveData *roamers = Save_Roamers_Get(fieldSystem->saveData);
    if (data.landSwarm != SPECIES_NONE && RoamerSave_OutbreakActive(roamers)
        && IsSwarmEncounterFromRand(Roamers_GetRand(roamers, 2), (u16)mapId, 0)) {
        AddEncounter(out, GEAR_ENC_SWARM, data.landSwarm, data.landSlots.levels[0], data.landSlots.levels[1], 40);
    }
    out->hasEncounters = out->count > 0;
    qsort(out->entries, out->count, sizeof(GearEncounter), CompareEncounters);
}

// --- the bosses (the Trainer tab's road ahead) and the level cap, from the game's own trainer data -------------------

typedef struct BossDef {
    u16 trainerID;
    u8 kind;  // GearBossKind
    u8 badge; // the badge it gives (0-15: Johto, then Kanto), 0xFF none
    u8 stage; // badges the player has when it comes
    const char *title;
    const char *name;
    const char *place;
} BossDef;

static const BossDef sBossDefs[] = {
    { TRAINER_LEADER_FALKNER_FALKNER, GEAR_BOSS_GYM, 0, 0, "Leader", "Falkner", "Violet City" },
    { TRAINER_LEADER_BUGSY_BUGSY, GEAR_BOSS_GYM, 1, 1, "Leader", "Bugsy", "Azalea Town" },
    { TRAINER_LEADER_WHITNEY, GEAR_BOSS_GYM, 2, 2, "Leader", "Whitney", "Goldenrod City" },
    { TRAINER_LEADER_MORTY_MORTY, GEAR_BOSS_GYM, 3, 3, "Leader", "Morty", "Ecruteak City" },
    { TRAINER_LEADER_CHUCK_CHUCK, GEAR_BOSS_GYM, 4, 4, "Leader", "Chuck", "Cianwood City" },
    { TRAINER_LEADER_JASMINE_JASMINE, GEAR_BOSS_GYM, 5, 5, "Leader", "Jasmine", "Olivine City" },
    { TRAINER_LEADER_PRYCE_PRYCE, GEAR_BOSS_GYM, 6, 6, "Leader", "Pryce", "Mahogany Town" },
    { TRAINER_LEADER_CLAIR_CLAIR, GEAR_BOSS_GYM, 7, 7, "Leader", "Clair", "Blackthorn City" },
    { TRAINER_ELITE_FOUR_WILL_WILL, GEAR_BOSS_ELITE_FOUR, 0xFF, 8, "Elite Four", "Will", "Indigo Plateau" },
    { TRAINER_ELITE_FOUR_KOGA_KOGA, GEAR_BOSS_ELITE_FOUR, 0xFF, 8, "Elite Four", "Koga", "Indigo Plateau" },
    { TRAINER_ELITE_FOUR_BRUNO_BRUNO, GEAR_BOSS_ELITE_FOUR, 0xFF, 8, "Elite Four", "Bruno", "Indigo Plateau" },
    { TRAINER_ELITE_FOUR_KAREN_KAREN, GEAR_BOSS_ELITE_FOUR, 0xFF, 8, "Elite Four", "Karen", "Indigo Plateau" },
    { TRAINER_CHAMPION_LANCE, GEAR_BOSS_CHAMPION, 0xFF, 8, "Champion", "Lance", "Indigo Plateau" },
    { TRAINER_LEADER_LT_SURGE_LT__SURGE, GEAR_BOSS_GYM, 10, 8, "Leader", "Lt. Surge", "Vermilion City" },
    { TRAINER_LEADER_SABRINA_SABRINA, GEAR_BOSS_GYM, 13, 8, "Leader", "Sabrina", "Saffron City" },
    { TRAINER_LEADER_MISTY_MISTY, GEAR_BOSS_GYM, 9, 8, "Leader", "Misty", "Cerulean City" },
    { TRAINER_LEADER_ERIKA_ERIKA, GEAR_BOSS_GYM, 11, 8, "Leader", "Erika", "Celadon City" },
    { TRAINER_LEADER_JANINE_JANINE, GEAR_BOSS_GYM, 12, 8, "Leader", "Janine", "Fuchsia City" },
    { TRAINER_LEADER_BROCK_BROCK, GEAR_BOSS_GYM, 8, 8, "Leader", "Brock", "Pewter City" },
    { TRAINER_LEADER_BLAINE_BLAINE, GEAR_BOSS_GYM, 14, 8, "Leader", "Blaine", "Seafoam Islands" },
    { TRAINER_LEADER_BLUE_BLUE, GEAR_BOSS_GYM, 15, 8, "Leader", "Blue", "Viridian City" },
    { TRAINER_PKMN_TRAINER_RED_RED, GEAR_BOSS_CHAMPION, 0xFF, 8, "Trainer", "Red", "Mt. Silver" },
};

static GearBoss sBosses[NELEMS(sBossDefs)];
static BOOL sBossBuilt[NELEMS(sBossDefs)];

int Gear_BossCount(void) {
    return Gear_IsGameRunning() ? (int)NELEMS(sBossDefs) : 0;
}

// The default moves a trainer's Pokemon gets: the last four it learned by its level
static void DefaultMoves(u16 species, int form, int level, GearMove moves[4]) {
    u16 learnset[LEVEL_UP_LEARNSET_MAX + 1];
    LoadLevelUpLearnset_HandleAlternateForm(species, form, learnset);
    u16 known[4] = { 0, 0, 0, 0 };
    int count = 0;
    for (int i = 0; i < LEVEL_UP_LEARNSET_MAX && learnset[i] != LEVEL_UP_LEARNSET_END; i++) {
        if ((int)LEVEL_UP_LEARNSET_LVL(learnset[i]) > level) {
            break;
        }
        const u16 move = LEVEL_UP_LEARNSET_MOVE(learnset[i]);
        BOOL already = FALSE;
        for (int k = 0; k < 4; k++) {
            already = already || known[k] == move;
        }
        if (already) {
            continue;
        }
        if (count < 4) {
            known[count++] = move;
        } else {
            memmove(known, known + 1, sizeof(u16) * 3);
            known[3] = move;
        }
    }
    for (int m = 0; m < 4; m++) {
        Gear_MoveInfo(known[m], &moves[m]);
    }
}

static void BuildBoss(int index) {
    const BossDef *def = &sBossDefs[index];
    GearBoss *boss = &sBosses[index];
    memset(boss, 0, sizeof(*boss));
    boss->trainerID = def->trainerID;
    boss->kind = def->kind;
    boss->stage = def->stage;
    boss->doubles = TrainerData_GetAttr(def->trainerID, TRATTR_DOUBLEBTL) != 0;
    snprintf(boss->title, sizeof(boss->title), "%s", def->title);
    snprintf(boss->name, sizeof(boss->name), "%s", def->name);
    snprintf(boss->place, sizeof(boss->place), "%s", def->place);

    // The party as the trainer data stores it: each entry's size depends on the trainer's type (moves, item)
    const int type = TrainerData_GetAttr(def->trainerID, TRATTR_TYPE);
    int count = TrainerData_GetAttr(def->trainerID, TRATTR_NPOKE);
    if (count > GEAR_PARTY_MAX) {
        count = GEAR_PARTY_MAX;
    }
    const BOOL hasMoves = (type & (1 << TRTYPE_MOVES_F)) != 0;
    const BOOL hasItem = (type & (1 << TRTYPE_ITEM_F)) != 0;
    const size_t stride = hasMoves ? (hasItem ? sizeof(TRPOKE_ITEM_CUSTMOVES) : sizeof(TRPOKE_NOITEM_CUSTMOVES))
                                   : (hasItem ? sizeof(TRPOKE_ITEM_DFLTMOVES) : sizeof(TRPOKE_NOITEM_DFLTMOVES));
    static u8 buffer[GEAR_PARTY_MAX * sizeof(TRPOKE)];
    memset(buffer, 0, sizeof(buffer));
    TrainerData_ReadTrPoke(def->trainerID, (TRPOKE *)buffer);
    for (int i = 0; i < count; i++) {
        const u8 *entry = buffer + i * stride;
        const u16 level = *(const u16 *)(entry + 2);
        const u16 speciesField = *(const u16 *)(entry + 4);
        GearMon *mon = &boss->party[boss->partyCount++];
        memset(mon, 0, sizeof(*mon));
        mon->species = speciesField & 0x3FF;
        mon->form = speciesField >> 10;
        mon->level = (u8)level;
        mon->gender = GEAR_GENDER_NONE;
        snprintf(mon->nickname, sizeof(mon->nickname), "%s", Gear_SpeciesName(mon->species));
        Gear_SpeciesTypes(mon->species, &mon->type1, &mon->type2);
        int at = 6;
        if (hasItem) {
            mon->heldItem = *(const u16 *)(entry + at);
            at += 2;
        }
        if (hasMoves) {
            for (int m = 0; m < 4; m++) {
                Gear_MoveInfo(*(const u16 *)(entry + at + m * 2), &mon->moves[m]);
            }
        } else {
            DefaultMoves(mon->species, mon->form, mon->level, mon->moves);
        }
        if (mon->level > boss->aceLevel) {
            boss->aceLevel = mon->level;
            boss->aceSpecies = mon->species;
        }
    }
}

static BOOL BossBeaten(int index, SaveData *saveData) {
    const BossDef *def = &sBossDefs[index];
    if (def->badge != 0xFF) {
        return PlayerProfile_TestBadgeFlag(Save_PlayerData_GetProfile(saveData), def->badge) ? TRUE : FALSE;
    }
    // The Elite Four and Lance: the Hall of Fame (Red: not known)
    return def->trainerID != TRAINER_PKMN_TRAINER_RED_RED && CheckGameClearFlag(Save_VarsFlags_Get(saveData));
}

const GearBoss *Gear_Boss(int index) {
    SaveData *saveData = RunningSave();
    if (saveData == NULL || index < 0 || index >= (int)NELEMS(sBossDefs)) {
        return NULL;
    }
    if (!sBossBuilt[index]) {
        BuildBoss(index);
        sBossBuilt[index] = TRUE;
    }
    sBosses[index].beaten = BossBeaten(index, saveData);
    return &sBosses[index];
}

// The next boss not beaten yet, in the order above (Johto's leaders, the League, Kanto's leaders, Red)
int Gear_NextGymBoss(void) {
    SaveData *saveData = RunningSave();
    if (saveData == NULL) {
        return -1;
    }
    for (int i = 0; i < (int)NELEMS(sBossDefs); i++) {
        if (!BossBeaten(i, saveData)) {
            return i;
        }
    }
    return -1;
}

int Gear_LevelCap(void) {
    const int next = Gear_NextGymBoss();
    const GearBoss *boss = next >= 0 ? Gear_Boss(next) : NULL;
    return boss != NULL ? boss->aceLevel : 0;
}

const GearMap *Gear_GetMap(void) {
    static GearMap map;
    memset(&map, 0, sizeof(map));
    return &map;
}

const char *Gear_PCUnavailable(void) {
    return "The PC tab isn't in Goldenrod's Gear yet.";
}

BOOL Gear_PCReadBox(int box, GearBoxMon out[GEAR_PC_BOX_SLOTS]) {
    (void)box;
    memset(out, 0, sizeof(GearBoxMon) * GEAR_PC_BOX_SLOTS);
    return FALSE;
}

void Gear_PCBoxName(int box, char *out, int size) {
    snprintf(out, size, "Box %d", box + 1);
}

int Gear_PCCurrentBox(void) {
    return 0;
}

BOOL Gear_PCSort(int box, int key) {
    (void)box;
    (void)key;
    return FALSE;
}

BOOL Gear_BagSort(int key) {
    (void)key;
    return FALSE;
}

BOOL Gear_GetDaycare(GearDaycare *out) {
    memset(out, 0, sizeof(*out));
    return FALSE;
}

int Gear_GetBerryPatches(GearBerryPatch *out, int max) {
    (void)out;
    (void)max;
    return 0;
}
