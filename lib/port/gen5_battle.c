// Gen 5 battles in HGSS; see include/port_sdk/port_gen5_battle.h. The Platinum fork's src/port/gen5_battle.c on
// HGSS's BattleSetup: the battle as the Gen 5 code takes it (gen5/api/gen5.h), and its results written back as HGSS's
// own battle leaves them in the BattleSetup.
#include "port_gen5_battle.h"
#include "port_gen5_battle_ui.h"

#ifdef GEN5_BATTLES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simulator/sim.h>

#include "constants/battle.h"
#include "constants/items.h"
#include "constants/sndseq.h"
#include "constants/trainer_class.h"

#include "bag.h"
#include "encounter.h"
#include "field_system.h"
#include "game_stats.h"
#include "gen5.h"
#include "gf_rtc.h"
#include "item.h"
#include "options.h"
#include "party.h"
#include "player_data.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "port_game_settings.h"
#include "port_gen5_rom.h"
#include "port_gen5_rocket.h"
#include "port_test_script.h"
#include "sound.h"
#include "system.h"
#include "task.h"

static BOOL sRequested = FALSE; // The next battle runs in BW2's engine
static BOOL sShown = FALSE; // BW2's battle is on screen
static BOOL sFailed = FALSE; // The battle couldn't start: back as if the player ran
static Gen5Battle sBattle; // The battle as handed to the Gen 5 code, and as it comes back
static Gen5Item sItemCountsBefore[GEN5_ITEMS_MAX];
static int sItemsBefore;

BOOL Gen5Battle_IsShown(void) {
    return sShown;
}

BOOL Gen5Battle_MoveBgShown(void) {
    return sShown && Gen5_MoveBgShown();
}

void Gen5Battle_RequestNext(void) {
    sRequested = TRUE;
}

BOOL Gen5Battle_Wants(const BattleSetup *setup) {
    if (sRequested) {
        sRequested = FALSE;
        return TRUE;
    }
    if (!GameSettings_Get()->gen5Battles || Gen5Rom_GameName() == NULL) {
        return FALSE;
    }
    // Wild battles for now, alone (roaming Pokémon too): the Safari Zone, the Bug-Catching Contest, Pal Park, the
    // tutorial, link and Frontier battles and the trainers stay HGSS's
    return (setup->battleType & ~BATTLE_TYPE_ROAMER) == 0;
}

// --- Text -------------------------------------------------------------------------------------------------------

// HGSS's text (the Gen 4 character table, charmap.txt) to Unicode, for BW2: letters, digits, the usual punctuation and
// the line and page breaks as BW2 writes them. Other characters are left out for now
static unsigned short CharToUnicode(u16 c) {
    if (c >= 0x121 && c <= 0x12A) {
        return '0' + (c - 0x121);
    }
    if (c >= 0x12B && c <= 0x144) {
        return 'A' + (c - 0x12B);
    }
    if (c >= 0x145 && c <= 0x15E) {
        return 'a' + (c - 0x145);
    }
    switch (c) {
    case 0x1DE:
        return ' ';
    case 0x1AB:
        return '!';
    case 0x1AC:
        return '?';
    case 0x1AD:
        return ',';
    case 0x1AE:
        return '.';
    case 0x1AF:
        return 0x2026; // …
    case 0x1B1:
        return '/';
    case 0x1B3:
        return '\'';
    case 0x1B9:
        return '(';
    case 0x1BA:
        return ')';
    case 0x1BB:
        return 0x2642; // ♂
    case 0x1BC:
        return 0x2640; // ♀
    case 0x1BE:
        return '-';
    case 0x1C4:
        return ':';
    case 0x1C5:
        return ';';
    case 0x188:
        return 0xE9; // é
    }
    return 0;
}

static void TextToGen5(const u16 *src, int srcLength, unsigned short *dest, int destLength) {
    int n = 0;
    for (int i = 0; i < srcLength && src[i] != EOS && n < destLength - 1; i++) {
        if (src[i] == 0xE000) { // A new line
            dest[n++] = 0xFFFE;
            continue;
        }
        if (src[i] == 0x25BC || src[i] == 0x25BD) { // A scroll or a new page: BW2's stream commands
            if (n + 3 > destLength - 1) {
                break;
            }
            dest[n++] = 0xF000;
            dest[n++] = 0xBE00 | (src[i] == 0x25BD);
            dest[n++] = 0;
            continue;
        }
        const unsigned short c = CharToUnicode(src[i]);
        if (c != 0) {
            dest[n++] = c;
        }
    }
    dest[n] = 0xFFFF;
}

// --- Pokémon ----------------------------------------------------------------------------------------------------

static unsigned StatusToGen5(u32 status) {
    if (status & MON_STATUS_SLP_MASK) {
        return GEN5_STATUS_SLEEP;
    }
    if (status & (MON_STATUS_PSN_MASK | MON_STATUS_TOX_MASK)) {
        return GEN5_STATUS_POISON;
    }
    if (status & MON_STATUS_BRN_MASK) {
        return GEN5_STATUS_BURN;
    }
    if (status & MON_STATUS_FRZ_MASK) {
        return GEN5_STATUS_FREEZE;
    }
    if (status & MON_STATUS_PRZ_MASK) {
        return GEN5_STATUS_PARALYSIS;
    }
    return GEN5_STATUS_NONE;
}

// BW2 keeps no sleep counter or toxic flag outside a battle: a Pokémon asleep sleeps a few turns more, and one that
// was badly poisoned stays so if it is still poisoned
static u32 StatusFromGen5(unsigned status, u32 before) {
    switch (status) {
    case GEN5_STATUS_SLEEP:
        return (before & MON_STATUS_SLP_MASK) ? (before & MON_STATUS_SLP_MASK) : 3;
    case GEN5_STATUS_POISON:
        return (before & MON_STATUS_TOX_MASK) ? MON_STATUS_TOX_MASK : MON_STATUS_PSN_MASK;
    case GEN5_STATUS_BURN:
        return MON_STATUS_BRN_MASK;
    case GEN5_STATUS_FREEZE:
        return MON_STATUS_FRZ_MASK;
    case GEN5_STATUS_PARALYSIS:
        return MON_STATUS_PRZ_MASK;
    default:
        return 0;
    }
}

// Species, forms, moves, items, abilities, natures and balls have the same numbers in Gen 4 and Gen 5
static void PokemonToGen5(Pokemon *mon, Gen5Pokemon *dest) {
    u16 name[POKEMON_NAME_LENGTH + 1];
    u16 otName[PLAYER_NAME_LENGTH + 1];

    memset(dest, 0, sizeof(*dest));
    dest->species = GetMonData(mon, MON_DATA_SPECIES, NULL);
    dest->form = GetMonData(mon, MON_DATA_FORM, NULL);
    dest->level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    dest->exp = GetMonData(mon, MON_DATA_EXPERIENCE, NULL);
    dest->personality = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
    dest->otId = GetMonData(mon, MON_DATA_OT_ID, NULL);
    dest->heldItem = GetMonData(mon, MON_DATA_HELD_ITEM, NULL);
    dest->ability = GetMonData(mon, MON_DATA_ABILITY, NULL);
    dest->friendship = GetMonData(mon, MON_DATA_FRIENDSHIP, NULL);
    dest->gender = GetMonGender(mon);
    for (int i = 0; i < 4; i++) {
        dest->moves[i] = GetMonData(mon, MON_DATA_MOVE1 + i, NULL);
        dest->pp[i] = GetMonData(mon, MON_DATA_MOVE1_PP + i, NULL);
        dest->ppUps[i] = GetMonData(mon, MON_DATA_MOVE1_PP_UPS + i, NULL);
    }
    dest->hp = GetMonData(mon, MON_DATA_HP, NULL);
    dest->status = StatusToGen5(GetMonData(mon, MON_DATA_STATUS, NULL));
    for (int i = 0; i < 6; i++) {
        dest->ivs[i] = GetMonData(mon, MON_DATA_HP_IV + i, NULL);
        dest->evs[i] = GetMonData(mon, MON_DATA_HP_EV + i, NULL);
    }
    dest->ball = GetMonData(mon, MON_DATA_POKEBALL, NULL);
    dest->pokerus = GetMonData(mon, MON_DATA_POKERUS, NULL);
    dest->metLevel = GetMonData(mon, MON_DATA_MET_LEVEL, NULL);
    dest->otGender = GetMonData(mon, MON_DATA_OT_GENDER, NULL);
    dest->isEgg = GetMonData(mon, MON_DATA_IS_EGG, NULL);
    dest->hasNickname = GetMonData(mon, MON_DATA_HAS_NICKNAME, NULL);
    GetMonData(mon, MON_DATA_NICKNAME, name);
    TextToGen5(name, POKEMON_NAME_LENGTH + 1, dest->nickname, GEN5_NAME_LENGTH);
    GetMonData(mon, MON_DATA_OT_NAME, otName);
    TextToGen5(otName, PLAYER_NAME_LENGTH + 1, dest->otName, 8);
}

// What the battle changed. TRUE if the Pokémon gained a level
static BOOL PokemonFromGen5(Pokemon *mon, const Gen5Pokemon *src) {
    const u32 level = GetMonData(mon, MON_DATA_LEVEL, NULL);
    const u32 status = GetMonData(mon, MON_DATA_STATUS, NULL);
    u32 value;

    SetMonData(mon, MON_DATA_EXPERIENCE, &src->exp);
    for (int i = 0; i < 4; i++) {
        SetMonData(mon, MON_DATA_MOVE1 + i, &src->moves[i]);
        SetMonData(mon, MON_DATA_MOVE1_PP_UPS + i, &src->ppUps[i]);
        SetMonData(mon, MON_DATA_MOVE1_PP + i, &src->pp[i]);
    }
    for (int i = 0; i < 6; i++) {
        value = src->evs[i];
        SetMonData(mon, MON_DATA_HP_EV + i, &value);
    }
    SetMonData(mon, MON_DATA_FRIENDSHIP, &src->friendship);
    SetMonData(mon, MON_DATA_HELD_ITEM, &src->heldItem);
    SetMonData(mon, MON_DATA_POKERUS, &src->pokerus);
    CalcMonLevelAndStats(mon);
    SetMonData(mon, MON_DATA_HP, &src->hp);
    value = StatusFromGen5(src->status, status);
    SetMonData(mon, MON_DATA_STATUS, &value);
    return GetMonData(mon, MON_DATA_LEVEL, NULL) > level;
}

// --- Where the battle is ----------------------------------------------------------------------------------------

// BW2's battle backgrounds (the field environment's bgType)
enum {
    ARENA_BG_GRASS = 0,
    ARENA_BG_CITY = 2,
    ARENA_BG_INDOORS = 4,
    ARENA_BG_OVERCAST = 6,
    ARENA_BG_MOUNTAIN = 7,
    ARENA_BG_SEA = 8,
};

static BOOL IsRocketClass(int trainerClass) {
    return trainerClass == TRAINERCLASS_TEAM_ROCKET || trainerClass == TRAINERCLASS_TEAM_ROCKET_F
        || trainerClass == TRAINERCLASS_EXECUTIVE_ARIANA || trainerClass == TRAINERCLASS_EXECUTIVE_ARCHER
        || trainerClass == TRAINERCLASS_EXECUTIVE_PROTON || trainerClass == TRAINERCLASS_EXECUTIVE_PETREL
        || trainerClass == TRAINERCLASS_ROCKET_BOSS;
}

static void ArenaFromSetup(const BattleSetup *setup, int *bg, int *terrain) {
    if ((setup->battleType & BATTLE_TYPE_TRAINER) && IsRocketClass(setup->trainer[BATTLER_ENEMY].data.trainerClass)) {
        // Team Rocket's own arena (lib/port/gen5_rocket.c): BW2's Champion room, darkened, with Rocket red streaks, a
        // rocket ship and the moon in its sky
        *bg = GEN5_ROCKET_BG;
        *terrain = GEN5_ROCKET_TERRAIN;
        return;
    }
    switch (setup->battleBg) {
    case BATTLE_BG_OCEAN:
        *bg = ARENA_BG_SEA;
        break;
    case BATTLE_BG_CITY:
        *bg = ARENA_BG_CITY;
        break;
    case BATTLE_BG_MOUNTAIN:
        *bg = ARENA_BG_MOUNTAIN;
        break;
    case BATTLE_BG_SNOW:
        *bg = ARENA_BG_OVERCAST;
        break;
    case BATTLE_BG_BUILDING_1:
    case BATTLE_BG_BUILDING_2:
    case BATTLE_BG_BUILDING_3:
    case BATTLE_BG_CAVE_1:
    case BATTLE_BG_CAVE_2:
    case BATTLE_BG_CAVE_3:
        *bg = ARENA_BG_INDOORS;
        break;
    default:
        *bg = ARENA_BG_GRASS;
        break;
    }
    switch (setup->terrain) {
    case TERRAIN_SAND:
        *terrain = 8;
        break;
    case TERRAIN_GRASS:
        *terrain = 5;
        break;
    case TERRAIN_PUDDLE:
        *terrain = 9;
        break;
    case TERRAIN_MOUNTAIN:
    case TERRAIN_CAVE:
        *terrain = 10;
        break;
    case TERRAIN_SNOW:
        *terrain = 14;
        break;
    case TERRAIN_ICE:
        *terrain = 13;
        break;
    case TERRAIN_WATER:
        *terrain = 6;
        break;
    case TERRAIN_BUILDING:
        *terrain = *bg == ARENA_BG_INDOORS ? 19 : 0;
        break;
    default:
        *terrain = 0;
        break;
    }
}

// --- The application --------------------------------------------------------------------------------------------

static void VBlank(void *data) {
    (void)data;
    Gen5_BattleVBlank();
}

// The pockets with what a battle can use
static const u8 sBattlePockets[] = { POCKET_ITEMS, POCKET_MEDICINE, POCKET_BALLS, POCKET_BERRIES, POCKET_BATTLE_ITEMS };

static BOOL Init(OverlayManager *man, int *state) {
    (void)state;
    BattleSetup *setup = OverlayManager_GetArgs(man);
    Party *party = setup->party[BATTLER_PLAYER];
    PlayerProfile *profile = setup->profile[BATTLER_PLAYER];

    TestScript_OnEvent(TEST_EVENT_BATTLE);
    memset(&sBattle, 0, sizeof(sBattle));
    TextToGen5(PlayerProfile_GetNamePtr(profile), PLAYER_NAME_LENGTH + 1, sBattle.player.name, 8);
    ArenaFromSetup(setup, &sBattle.arenaBg, &sBattle.arenaTerrain);
    {
        // For looking at arenas: POKEHEARTGOLD_GEN5_ARENA=bg,terrain (16,1 is Team Rocket's)
        const char *arena = getenv("POKEHEARTGOLD_GEN5_ARENA");
        int bg, terrain;
        if (arena != NULL && sscanf(arena, "%d,%d", &bg, &terrain) == 2) {
            sBattle.arenaBg = bg;
            sBattle.arenaTerrain = terrain;
        }
    }
    {
        RTCTime now;
        GF_RTC_CopyTime(&now);
        sBattle.hour = now.hour;
        sBattle.minute = now.minute;
        sBattle.zone = 0;
    }
    sBattle.textSpeed = Options_GetTextSpeed(setup->options);
    sBattle.setStyle = Options_GetBattleStyle(setup->options) == 1;
    sBattle.player.id = PlayerProfile_GetTrainerID(profile);
    sBattle.player.gender = PlayerProfile_GetTrainerGender(profile);
    sBattle.player.money = PlayerProfile_GetMoney(profile);
    sBattle.player.badges = PlayerProfile_CountBadges(profile);
    sBattle.partyCount = Party_GetCount(party);
    if (sBattle.partyCount > GEN5_PARTY_MAX) {
        sBattle.partyCount = GEN5_PARTY_MAX;
    }
    for (int i = 0; i < sBattle.partyCount; i++) {
        PokemonToGen5(Party_GetMonByIndex(party, i), &sBattle.party[i]);
    }
    PokemonToGen5(Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0), &sBattle.wild);
    for (int p = 0; p < (int)NELEMS(sBattlePockets); p++) {
        ItemSlot *slot;
        for (int i = 0; (slot = Bag_GetPocketSlotN(setup->bag, sBattlePockets[p], i)) != NULL && sBattle.itemCount < GEN5_ITEMS_MAX; i++) {
            if (slot->id != ITEM_NONE && slot->quantity != 0) {
                sBattle.items[sBattle.itemCount].item = slot->id;
                sBattle.items[sBattle.itemCount].count = slot->quantity;
                sBattle.itemCount++;
            }
        }
    }
    sItemsBefore = sBattle.itemCount;
    memcpy(sItemCountsBefore, sBattle.items, sizeof(sBattle.items));
    // Seen, as HGSS's battle registers a wild Pokémon when it appears
    Pokedex_SetMonSeenFlag(setup->pokedex, Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0));

    // HGSS takes White 2's light HP boxes (Platinum keeps Black 2's dark ones)
    Gen5_SetWhiteGauges(1);
    // Before every battle: BW2 takes the archives it reads at its first start
    Gen5Rocket_PrepareArena();
    sFailed = !Gen5_BattleBegin(&sBattle, Gen5Rom_Dir());
    if (sFailed) {
        printf("Gen 5: the battle couldn't start\n");
    } else {
        Main_SetVBlankIntrCB(VBlank, NULL);
        sShown = TRUE;
        // BW2's shadows are translucent quads that overlap: the DS doesn't blend polygons of one ID over each other
        SIM_SetTranslucentPolygonIdRule(TRUE);
    }
    return TRUE;
}

// A caught Pokémon, as HGSS's battle takes it (battle_command.c's catch sequence): what the battle changed, the
// player's catch data, the Pokédex and the game stats, then into the party or the first box with room
static void StoreCaught(BattleSetup *setup) {
    Pokemon *mon = Party_GetMonByIndex(setup->party[BATTLER_ENEMY], 0);

    PokemonFromGen5(mon, &sBattle.caughtMon);
    MonSetCatchData(mon, setup->profile[BATTLER_PLAYER], sBattle.caughtMon.ball, setup->mapSection, setup->terrain, HEAP_ID_FIELD2);
    Pokedex_SetMonCaughtFlag(setup->pokedex, mon);
    GameStats_Inc(setup->gameStats, GAME_STAT_CAUGHT_MON);
    setup->caughtBattlerId = BATTLER_ENEMY;
    if (Party_AddMon(setup->party[BATTLER_PLAYER], mon)) {
        printf("Gen 5: caught species %u, into the party\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
        return;
    }
    const int box = PCStorage_FindFirstBoxWithEmptySlot(setup->storagePC);
    if (box < 0 || box >= NUM_BOXES) {
        printf("Gen 5: caught species %u, but every box is full\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL));
        return;
    }
    PCStorage_SetActiveBox(setup->storagePC, box);
    for (int i = 0; i < 4; i++) {
        u32 maxPP = GetMonData(mon, MON_DATA_MOVE1_MAX_PP + i, NULL);
        SetMonData(mon, MON_DATA_MOVE1_PP + i, &maxPP);
    }
    Mon_UpdateGiratinaForm(mon);
    PCStorage_PlaceMonInBoxFirstEmptySlot(setup->storagePC, box, Mon_GetBoxMon(mon));
    printf("Gen 5: caught species %u, into box %d\n", (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL), box + 1);
}

// What the battle left, into the BattleSetup, as HGSS's battle leaves it
static void Finish(BattleSetup *setup) {
    Main_SetVBlankIntrCB(NULL, NULL);
    sShown = FALSE;
    SIM_SetTranslucentPolygonIdRule(FALSE);
    Gen5_SetWidescreen(1.0f);
    const enum Gen5BattleResult result = sFailed ? GEN5_BATTLE_FAILED : Gen5_BattleEnd(&sBattle);

    if (!sFailed) {
        Party *party = setup->party[BATTLER_PLAYER];
        for (int i = 0; i < sBattle.partyCount; i++) {
            Pokemon *mon = Party_GetMonByIndex(party, i);
            if (PokemonFromGen5(mon, &sBattle.party[i])) {
                setup->levelUpFlag |= 1 << i;
            }
            printf("Gen 5: party %d after the battle: species %u, level %u, exp %u, HP %u/%u\n", i,
                (unsigned)GetMonData(mon, MON_DATA_SPECIES, NULL), (unsigned)GetMonData(mon, MON_DATA_LEVEL, NULL),
                (unsigned)GetMonData(mon, MON_DATA_EXPERIENCE, NULL), (unsigned)GetMonData(mon, MON_DATA_HP, NULL),
                (unsigned)GetMonData(mon, MON_DATA_MAX_HP, NULL));
        }
        for (int i = 0; i < sItemsBefore; i++) {
            const int before = sItemCountsBefore[i].count;
            const int after = sBattle.items[i].count;
            if (after < before) {
                Bag_TakeItem(setup->bag, sBattle.items[i].item, before - after, HEAP_ID_FIELD2);
            } else if (after > before) {
                Bag_AddItem(setup->bag, sBattle.items[i].item, after - before, HEAP_ID_FIELD2);
            }
        }
        PlayerProfile *profile = setup->profile[BATTLER_PLAYER];
        if (sBattle.moneyChange > 0) {
            PlayerProfile_AddMoney(profile, sBattle.moneyChange);
        } else if (sBattle.moneyChange < 0) {
            PlayerProfile_SubMoney(profile, -sBattle.moneyChange);
        }
    }
    switch (result) {
    case GEN5_BATTLE_WON:
        setup->winFlag = BATTLE_OUTCOME_WIN;
        break;
    case GEN5_BATTLE_LOST:
        setup->winFlag = BATTLE_OUTCOME_LOSE;
        break;
    case GEN5_BATTLE_CAUGHT:
        setup->winFlag = BATTLE_OUTCOME_MON_CAUGHT;
        StoreCaught(setup);
        break;
    default:
        // Ran, or failed: back to the overworld as if the player ran
        setup->winFlag = BATTLE_OUTCOME_PLAYER_FLED;
        break;
    }
}

static BOOL Main(OverlayManager *man, int *state) {
    BattleSetup *setup = OverlayManager_GetArgs(man);
    (void)state;

    if (!sFailed) {
        // The keys and touch the game read this frame (the test scripts' among them)
        Gen5_SetInput(gSystem.heldKeysRaw, gSystem.touchHeld, gSystem.touchX, gSystem.touchY);
        Gen5_SetWidescreen(SIM_IsWidescreenActive() ? SIM_WidescreenRatio() : 1.0f);
        const BOOL running = Gen5_BattleFrame();
        const unsigned events = Gen5_TakeEvents();
        if (events & GEN5_EVENT_COMMAND) {
            TestScript_OnEvent(TEST_EVENT_BATTLE_COMMAND);
        }
        if (events & GEN5_EVENT_VICTORY_MUSIC) {
            // HGSS's victory music for a wild Pokémon (battle_command.c)
            PlayBGM(SEQ_GS_WIN2);
        }
        if (running) {
            return FALSE;
        }
    }
    Finish(setup);
    return TRUE;
}

static BOOL Exit(OverlayManager *man, int *state) {
    (void)man;
    (void)state;
    return TRUE;
}

const OverlayManagerTemplate gGen5BattleOverlayTemplate = {
    .init = Init,
    .exec = Main,
    .exit = Exit,
    .ovy_id = FS_OVERLAY_ID_NONE,
};

// --- Starting a wild battle (the settings' debug button, the test scripts' wild) ----------------------------------

typedef struct {
    int species;
    int level;
} WildStart;

static BOOL Task_StartWild(TaskManager *taskManager) {
    WildStart *start = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        (*state)++;
        SetupAndStartWildBattle(taskManager, start->species, start->level, NULL, TRUE, FALSE);
        return FALSE;
    default:
        Heap_Free(start);
        return TRUE;
    }
}

BOOL Gen5Battle_StartWild(int species, int level, BOOL gen5) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->taskman != NULL) {
        return FALSE;
    }
    if (gen5) {
        if (Gen5Rom_GameName() == NULL) {
            printf("Gen 5: no Black 2 or White 2 ROM imported\n");
            return FALSE;
        }
        Gen5Battle_RequestNext();
    }
    WildStart *start = Heap_Alloc(HEAP_ID_FIELD1, sizeof(WildStart));
    start->species = species;
    start->level = level;
    FieldSystem_CreateTask(fieldSystem, Task_StartWild, start);
    return TRUE;
}

BOOL Gen5BattleUI_StartWild(int species, int level) {
    return Gen5Battle_StartWild(species, level, TRUE);
}

#endif
