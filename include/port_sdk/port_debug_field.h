#ifndef POKEHEARTGOLD_PORT_DEBUG_FIELD_H
#define POKEHEARTGOLD_PORT_DEBUG_FIELD_H

// The debug window's moves on the overworld (libntr's developer windows > Application, lib/port/debug_window.cpp):
// warps to the towns or anywhere, the trainers on this map (go to one, battle it, mark it beaten or not), a battle
// against any trainer, a wild battle. Battles run in BW2's engine when Settings > Gen 5 battles is on.
//
// C, with only nitro's types: the debug window (C++) can't include the game's headers.

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_FIELD_NAME_LEN 40

// NULL when the player is free on the overworld (what everything below needs), else why not
const char *DebugField_Unavailable(void);

// Where the player is: the map's number and name, the tile
int DebugField_MapId(void);
const char *DebugField_MapName(void);
void DebugField_Position(int *x, int *z);

// --- Warps -------------------------------------------------------------------------------------------------------

// The places Fly goes to (spawn points), in the game's order: New Bark Town first
int DebugField_TownCount(void);
const char *DebugField_TownName(int town);
BOOL DebugField_WarpToTown(int town);
// Any map, at a tile (as the script command Warp does)
BOOL DebugField_WarpTo(int mapId, int x, int z);

// --- Trainers ----------------------------------------------------------------------------------------------------

typedef struct DebugFieldTrainer {
    int objectId; // on this map
    int trainerId;
    int x, z;
    BOOL beaten;
    BOOL isDouble;
    char name[DEBUG_FIELD_NAME_LEN]; // "Youngster Joey"
} DebugFieldTrainer;

// The trainers standing on this map
int DebugField_MapTrainers(DebugFieldTrainer *out, int max);
// To the tile in front of the trainer, facing it
BOOL DebugField_GoToTrainer(int objectId);

int DebugField_TrainerCount(void);
const char *DebugField_TrainerName(int trainerId); // with its class
BOOL DebugField_TrainerBeaten(int trainerId);
void DebugField_SetTrainerBeaten(int trainerId, BOOL beaten);
// A battle against the trainer from where the player stands. Winning marks it beaten; losing whites out, as in the game
BOOL DebugField_BattleTrainer(int trainerId);

// --- Wild battles ------------------------------------------------------------------------------------------------

BOOL DebugField_WildBattle(int species, int level);

#ifdef __cplusplus
}
#endif

#endif // POKEHEARTGOLD_PORT_DEBUG_FIELD_H
