// The test scripts' editor and debug commands (tests/README.md): the save editor's and the debug window's functions,
// called as their windows call them, with checks that they did what they say.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"

#include "field_system.h"
#include "party.h"
#include "pokemon.h"
#include "save_arrays.h"
#include "save_pokegear.h"
#include "save_local_field_data.h"

#include "port_debug_field.h"
#include "port_nuzlocke_mode.h"
#include "use_item_on_mon.h"
#include "port_save_editor.h"
#include "port_test_script.h"

static char sError[128];

static const char *Failed(const char *format, int a, int b) {
    snprintf(sError, sizeof(sError), format, a, b);
    return sError;
}

static void PrintParty(void) {
    for (int i = 0; i < SaveEd_PartyCount(); i++) {
        SaveEdMon mon;
        SaveEd_GetMon((SaveEdRef) { SAVE_ED_PARTY, i }, &mon);
        printf("[test] party %d: %s (%s) Lv. %d, %s, %s%s, IVs %d/%d/%d/%d/%d/%d, moves %s/%s/%s/%s\n", i, mon.name,
            SaveEd_SpeciesName(mon.species), mon.level, SaveEd_NatureName(mon.nature), SaveEd_AbilityName(mon.ability),
            mon.shiny ? ", shiny" : "", mon.ivs[0], mon.ivs[1], mon.ivs[2], mon.ivs[3], mon.ivs[4], mon.ivs[5],
            SaveEd_MoveName(mon.moves[0]), SaveEd_MoveName(mon.moves[1]), SaveEd_MoveName(mon.moves[2]),
            SaveEd_MoveName(mon.moves[3]));
    }
}

static const char *Editor(char **words, int count) {
    if (SaveEd_Unavailable() != NULL) {
        return SaveEd_Unavailable();
    }
    const char *what = words[0];
    const int a = count >= 2 ? atoi(words[1]) : 0;
    const int b = count >= 3 ? atoi(words[2]) : 0;
    SaveEdMon mon;
    const SaveEdRef ref = { SAVE_ED_PARTY, a };

    if (strcmp(what, "almost") == 0) { // almost SLOT: one experience point short of its next level
        Pokemon *mon = Party_GetMonByIndex(SaveArray_Party_Get(FieldSystem_PortGet()->saveData), a);
        const int level = GetMonData(mon, MON_DATA_LEVEL, NULL);
        u32 exp = GetMonExpBySpeciesAndLevel(GetMonData(mon, MON_DATA_SPECIES, NULL), level + 1) - 1;
        SetMonData(mon, MON_DATA_EXPERIENCE, &exp);
        CalcMonLevelAndStats(mon);
        return (int)GetMonData(mon, MON_DATA_LEVEL, NULL) == level ? NULL : "the level changed";
    }
    if (strcmp(what, "species") == 0) { // species SLOT N: the party's Pokemon in SLOT is species N
        SaveEd_GetMon(ref, &mon);
        printf("[test] party %d: %s (species %d), level %d\n", a, mon.name, mon.species, mon.level);
        return mon.species == b ? NULL : Failed("species %d, not %d", mon.species, b);
    }
    if (strcmp(what, "mom") == 0) { // mom MONEY: Mom's savings hold some, and the player has MONEY (a quarter of the
                                     // prize went to Mom: the money before, plus the prize, less Mom's)
        const u32 savings = PhoneCallPersistentState_MomSavings_BalanceAction(
            SaveData_GetPhoneCallPersistentState(FieldSystem_PortGet()->saveData), MOMS_BALANCE_GET, 0);
        SaveEdTrainer trainer;
        SaveEd_GetTrainer(&trainer);
        printf("[test] Mom's savings %u, the player's money %u\n", (unsigned)savings, (unsigned)trainer.money);
        if (savings == 0) {
            return "Mom saved nothing";
        }
        // The prize was 4/3 of what the player kept beyond MONEY... checked as HGSS splits it: Mom has a quarter
        const u32 prize = trainer.money - (u32)a + savings;
        return savings == prize / 4 ? NULL : Failed("Mom has %d of a prize of %d, not a quarter", (int)savings, (int)prize);
    }
    if (strcmp(what, "safari") == 0) { // safari N: the Safari Zone's ball counter (the save's) holds N
        const int balls = *LocalFieldData_GetSafariBallsCounter(Save_LocalFieldData_Get(FieldSystem_PortGet()->saveData));
        printf("[test] %d Safari Balls\n", balls);
        return balls == a ? NULL : Failed("%d Safari Balls, not %d", balls, a);
    }
    if (strcmp(what, "ball") == 0) { // ball SLOT N: the party's Pokemon in SLOT is in ball N (constants/balls.h)
        SaveEd_GetMon(ref, &mon);
        printf("[test] party %d: %s in ball %d, friendship %d\n", a, mon.name, mon.ball, mon.friendship);
        return mon.ball == b ? NULL : Failed("ball %d, not %d", mon.ball, b);
    }
    if (strcmp(what, "dump") == 0) {
        PrintParty();
        return NULL;
    }
    // Nuzlocke mode's checks (the party member in SLOT):
    if (strcmp(what, "faint") == 0) { // faint SLOT: its HP to 0, as a battle's party update writes it
        Pokemon *pokemon = Party_GetMonByIndex(SaveArray_Party_Get(FieldSystem_PortGet()->saveData), a);
        u32 hp = 0;
        SetMonData(pokemon, MON_DATA_HP, &hp);
        printf("[test] party %d fainted: dead %d\n", a, NuzlockeMode_IsDead(pokemon));
        return NULL;
    }
    if (strcmp(what, "dead") == 0) { // dead SLOT 0|1: whether it is dead, and its HP then stays 0 (a Revive is refused)
        Pokemon *pokemon = Party_GetMonByIndex(SaveArray_Party_Get(FieldSystem_PortGet()->saveData), a);
        printf("[test] party %d: dead %d, HP %d\n", a, NuzlockeMode_IsDead(pokemon), (int)GetMonData(pokemon, MON_DATA_HP, NULL));
        return NuzlockeMode_IsDead(pokemon) == (b != 0) ? NULL : Failed("dead %d, not %d", NuzlockeMode_IsDead(pokemon), b != 0);
    }
    if (strcmp(what, "use") == 0) { // use ITEM SLOT: an item on the party member, as the party menu checks and applies it
        Party *party = SaveArray_Party_Get(FieldSystem_PortGet()->saveData);
        Pokemon *pokemon = Party_GetMonByIndex(party, b);
        const int before = (int)GetMonData(pokemon, MON_DATA_LEVEL, NULL);
        const BOOL can = CanUseItemOnPokemon(pokemon, (u16)a, 0, HEAP_ID_FIELD1);
        const BOOL used = can && UseItemOnPokemon(pokemon, (u16)a, 0, 0, HEAP_ID_FIELD1);
        printf("[test] item %d on party %d: can %d, used %d, level %d -> %d, HP %d\n", a, b, can, used, before,
            (int)GetMonData(pokemon, MON_DATA_LEVEL, NULL), (int)GetMonData(pokemon, MON_DATA_HP, NULL));
        return NULL;
    }
    if (strcmp(what, "allivs") == 0) { // allivs SLOT N: every IV is N
        SaveEd_GetMon(ref, &mon);
        for (int i = 0; i < SAVE_ED_STATS; i++) {
            if (mon.ivs[i] != b) {
                return Failed("an IV is %d, not %d", mon.ivs[i], b);
            }
        }
        return NULL;
    }
    if (strcmp(what, "cap") == 0) { // cap N: the level cap is N
        printf("[test] level cap %d\n", NuzlockeMode_LevelCap());
        return NuzlockeMode_LevelCap() == a ? NULL : Failed("level cap %d, not %d", NuzlockeMode_LevelCap(), a);
    }
    if (strcmp(what, "give") == 0) { // give SPECIES LEVEL: to the party, shiny, with perfect IVs and an Adamant nature
        const int before = SaveEd_PartyCount();
        SaveEdNewMon newMon = { (u16)a, b, 3, TRUE, TRUE, 0 };
        if (SaveEd_GiveMon(&newMon, TRUE, NULL, NULL) != 1 || SaveEd_PartyCount() != before + 1) {
            return "the Pokemon didn't join the party";
        }
        SaveEd_GetMon((SaveEdRef) { SAVE_ED_PARTY, before }, &mon);
        if (mon.species != a || mon.level != b || !mon.shiny || mon.nature != 3 || mon.ivs[SAVE_ED_SPEED] != 31) {
            return Failed("the new Pokemon is species %d, level %d, not as asked", mon.species, mon.level);
        }
        PrintParty();
        return NULL;
    }
    if (strcmp(what, "level") == 0) { // level SLOT N
        SaveEd_SetLevel(ref, b);
        SaveEd_GetMon(ref, &mon);
        return mon.level == b ? NULL : Failed("level %d, not %d", mon.level, b);
    }
    if (strcmp(what, "nature") == 0) { // nature SLOT N: keeps shininess and gender
        SaveEdMon before;
        SaveEd_GetMon(ref, &before);
        if (!SaveEd_SetPersonality(ref, b, before.genderFixed ? -1 : before.gender, before.shiny, -1)) {
            return "no personality found";
        }
        SaveEd_GetMon(ref, &mon);
        if (mon.nature != b || mon.shiny != before.shiny || mon.gender != before.gender || mon.level != before.level) {
            return Failed("nature %d, not %d (or the shininess, gender or level changed)", mon.nature, b);
        }
        return NULL;
    }
    if (strcmp(what, "shiny") == 0) { // shiny SLOT: keeps the nature
        SaveEdMon before;
        SaveEd_GetMon(ref, &before);
        SaveEd_SetPersonality(ref, before.nature, before.genderFixed ? -1 : before.gender, TRUE, -1);
        SaveEd_GetMon(ref, &mon);
        return mon.shiny && mon.nature == before.nature ? NULL : "not shiny, or the nature changed";
    }
    if (strcmp(what, "ivs") == 0) { // ivs SLOT: all 31
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            SaveEd_SetIV(ref, (enum SaveEdStat)s, SAVE_ED_MAX_IV);
        }
        SaveEd_GetMon(ref, &mon);
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            if (mon.ivs[s] != SAVE_ED_MAX_IV) {
                return Failed("IV %d is %d", s, mon.ivs[s]);
            }
        }
        return NULL;
    }
    if (strcmp(what, "move") == 0) { // move SLOT MOVE: learned in the first free move slot
        SaveEd_GetMon(ref, &mon);
        int slot = 0;
        while (slot < 3 && mon.moves[slot] != 0) {
            slot++;
        }
        SaveEd_SetMove(ref, slot, (u16)b);
        SaveEd_GetMon(ref, &mon);
        return mon.moves[slot] == b && mon.pp[slot] > 0 ? NULL : Failed("move slot %d holds %d", slot, mon.moves[slot]);
    }
    if (strcmp(what, "ability") == 0) { // ability SLOT N: its ability becomes N (25: Wonder Guard)
        Pokemon *pokemon = Party_GetMonByIndex(SaveArray_Party_Get(FieldSystem_PortGet()->saveData), a);
        u8 ability = (u8)b;
        SetMonData(pokemon, MON_DATA_ABILITY, &ability);
        return (int)GetMonData(pokemon, MON_DATA_ABILITY, NULL) == b ? NULL : "the ability didn't change";
    }
    if (strcmp(what, "allmoves") == 0) { // allmoves SLOT MOVE: all four of its moves become MOVE (the save editor
                                          // won't teach a move twice, so written as they are)
        Pokemon *pokemon = Party_GetMonByIndex(SaveArray_Party_Get(FieldSystem_PortGet()->saveData), a);
        for (int i = 0; i < 4; i++) {
            u16 move = (u16)b;
            u8 pp = 15;
            SetMonData(pokemon, MON_DATA_MOVE1 + i, &move);
            SetMonData(pokemon, MON_DATA_MOVE1_PP + i, &pp);
        }
        SaveEd_GetMon(ref, &mon);
        return mon.moves[0] == b && mon.moves[3] == b ? NULL : Failed("its moves are %d...%d", mon.moves[0], mon.moves[3]);
    }
    if (strcmp(what, "firstmove") == 0) { // firstmove SLOT MOVE: its first move becomes MOVE (what mashing A uses)
        SaveEd_SetMove(ref, 0, (u16)b);
        SaveEd_GetMon(ref, &mon);
        return mon.moves[0] == b ? NULL : Failed("its first move is %d, not %d", mon.moves[0], b);
    }
    if (strcmp(what, "learnable") == 0) { // learnable SLOT: its level-up and TM moves
        SaveEd_GetMon(ref, &mon);
        u16 moves[256];
        const int n = SaveEd_LearnableMoves(mon.species, mon.form, moves, 256);
        printf("[test] %s learns %d moves, first %s, last %s\n", SaveEd_SpeciesName(mon.species), n,
            n > 0 ? SaveEd_MoveName(moves[0]) : "-", n > 0 ? SaveEd_MoveName(moves[n - 1]) : "-");
        return n > 0 ? NULL : "no moves";
    }
    if (strcmp(what, "item") == 0) { // item ITEM N
        SaveEd_SetItemQuantity((u16)a, b);
        const int have = SaveEd_ItemQuantity((u16)a);
        printf("[test] %d %s\n", have, SaveEd_ItemName((u16)a));
        return have == b ? NULL : Failed("%d of the item, not %d", have, b);
    }
    if (strcmp(what, "money") == 0) { // money N
        SaveEd_SetMoney(a);
        SaveEdTrainer trainer;
        SaveEd_GetTrainer(&trainer);
        return (int)trainer.money == a ? NULL : Failed("money %d, not %d", trainer.money, a);
    }
    if (strcmp(what, "badge") == 0) { // badge B 0|1
        SaveEd_SetBadge(a, b);
        SaveEdTrainer trainer;
        SaveEd_GetTrainer(&trainer);
        printf("[test] %s: badges %04x\n", trainer.name, trainer.badges);
        return ((trainer.badges >> a) & 1) == (b != 0) ? NULL : Failed("badge %d is %d", a, (trainer.badges >> a) & 1);
    }
    if (strcmp(what, "pk4") == 0) { // pk4 SLOT: exported, then imported into the PC
        u8 data[SAVE_ED_PK4_SIZE];
        if (!SaveEd_ExportPk4(ref, data)) {
            return "nothing to export";
        }
        SaveEd_GetMon(ref, &mon);
        int box = -1, slot = -1;
        if (SaveEd_ImportPk4(data, sizeof(data), FALSE, &box, &slot) != 2) {
            return "the import didn't go to the PC";
        }
        SaveEdMon copy;
        SaveEd_GetMon((SaveEdRef) { box, slot }, &copy);
        printf("[test] .pk4: %s Lv. %d into box %d slot %d as %s Lv. %d\n", mon.name, mon.level, box, slot, copy.name, copy.level);
        if (copy.species != mon.species || copy.level != mon.level || copy.nature != mon.nature || copy.shiny != mon.shiny
            || memcmp(copy.ivs, mon.ivs, sizeof(mon.ivs)) != 0 || memcmp(copy.moves, mon.moves, sizeof(mon.moves)) != 0) {
            return "the imported Pokemon differs";
        }
        // A broken file is refused
        data[20] ^= 0x55;
        return SaveEd_ImportPk4(data, sizeof(data), FALSE, NULL, NULL) == -1 ? NULL : "a broken .pk4 was taken";
    }
    return "unknown editor command";
}

static const char *Debug(char **words, int count, BOOL *task) {
    const char *what = words[0];
    const int a = count >= 2 ? atoi(words[1]) : 0;
    const int b = count >= 3 ? atoi(words[2]) : 0;
    static DebugFieldTrainer trainers[64];
    const int n = DebugField_MapTrainers(trainers, 64);

    if (strcmp(what, "trainers") == 0) { // trainers [N]: lists this map's, at least N
        printf("[test] %s (map %d): %d trainers\n", DebugField_MapName(), DebugField_MapId(), n);
        for (int i = 0; i < n; i++) {
            printf("[test]   %d: %s #%d at (%d, %d)%s%s\n", i, trainers[i].name, trainers[i].trainerId, trainers[i].x,
                trainers[i].z, trainers[i].beaten ? ", beaten" : "", trainers[i].isDouble ? ", double" : "");
        }
        return n >= a ? NULL : Failed("%d trainers, not %d or more", n, a);
    }
    if (strcmp(what, "towns") == 0) { // towns: the warp list's names
        for (int i = 0; i < DebugField_TownCount(); i++) {
            printf("[test]   town %d: %s\n", i, DebugField_TownName(i));
        }
        return NULL;
    }
    if (strcmp(what, "map") == 0) { // map ID: the player is on it
        printf("[test] on %s (map %d)\n", DebugField_MapName(), DebugField_MapId());
        return DebugField_MapId() == a ? NULL : Failed("on map %d, not %d", DebugField_MapId(), a);
    }
    if (strcmp(what, "unbeat") == 0) { // unbeat I: this map's trainer I can battle again
        if (a >= n) {
            return "no such trainer";
        }
        DebugField_SetTrainerBeaten(trainers[a].trainerId, FALSE);
        return DebugField_TrainerBeaten(trainers[a].trainerId) ? "still beaten" : NULL;
    }
    if (strcmp(what, "beaten") == 0) { // beaten I 0|1: whether this map's trainer I is beaten
        if (a >= n) {
            return "no such trainer";
        }
        return (DebugField_TrainerBeaten(trainers[a].trainerId) != FALSE) == (b != 0) ? NULL : "not as expected";
    }
    if (DebugField_Unavailable() != NULL) {
        return DebugField_Unavailable();
    }
    *task = TRUE;
    if (strcmp(what, "goto") == 0) { // goto I: in front of this map's trainer I
        return a < n && DebugField_GoToTrainer(trainers[a].objectId) ? NULL : "no such trainer";
    }
    if (strcmp(what, "battle") == 0) { // battle I: this map's trainer I
        return a < n && DebugField_BattleTrainer(trainers[a].trainerId) ? NULL : "no such trainer";
    }
    if (strcmp(what, "trainer") == 0) { // trainer ID: any trainer by number
        printf("[test] trainer %d: %s\n", a, DebugField_TrainerName(a));
        return DebugField_BattleTrainer(a) ? NULL : "no such trainer";
    }
    if (strcmp(what, "town") == 0) { // town N (DebugField_TownName)
        printf("[test] warp to %s\n", DebugField_TownName(a));
        return DebugField_WarpToTown(a) ? NULL : "no such town";
    }
    if (strcmp(what, "wild") == 0) { // wild SPECIES LEVEL
        return DebugField_WildBattle(a, b) ? NULL : "no wild battle";
    }
    *task = FALSE;
    return "unknown debug command";
}

const char *TestEditor_Run(BOOL debug, const char *args, BOOL *startedTask) {
    char text[96];
    snprintf(text, sizeof(text), "%s", args);
    char *words[6];
    int count = 0;
    for (char *word = strtok(text, " \t"); word != NULL && count < 6; word = strtok(NULL, " \t")) {
        words[count++] = word;
    }
    *startedTask = FALSE;
    if (count == 0) {
        return "nothing to do";
    }
    return debug ? Debug(words, count, startedTask) : Editor(words, count);
}
