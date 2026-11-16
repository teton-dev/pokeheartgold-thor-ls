#ifndef POKEHEARTGOLD_PORT_GEN5_BATTLE_H
#define POKEHEARTGOLD_PORT_GEN5_BATTLE_H

// Gen 5 battles in HGSS (teton-dev fork; the Platinum fork's docs/thor/dev/gen5-battles.md and hgss.md): with
// Settings > Gameplay > Gen 5 battles on and a Black 2 or White 2 ROM imported, the battles Battle_LaunchApp starts
// run in BW2's engine (gen5/api), the BattleSetup converted to the Gen 5 code's Gen5Battle and the results written
// back. The Platinum fork's src/port/gen5_battle.c on HGSS's BattleSetup. So far: wild battles.

#include "global.h"

#include "battle/battle_setup.h"
#include "overlay_manager.h"

#ifdef GEN5_BATTLES

// The application that runs a battle in BW2's engine, in place of gOverlayTemplate_Battle
extern const OverlayManagerTemplate gGen5BattleOverlayTemplate;

// The battle should run in BW2's engine: the setting is on, the ROM is there and the battle is one it can run (or
// the next one was asked for, Gen5Battle_RequestNext)
BOOL Gen5Battle_Wants(const BattleSetup *setup);

// The next battle runs in BW2's engine, whatever the setting
void Gen5Battle_RequestNext(void);

// A wild battle against SPECIES at LEVEL in BW2's engine, as tall grass would start it, once the player is free on
// the overworld. FALSE when there is no ROM or no field
BOOL Gen5Battle_StartWild(int species, int level, BOOL gen5);
// The same as a legendary's encounter (kind 1) or a roaming Pokémon's (kind 2)
BOOL Gen5Battle_StartWildKind(int species, int level, BOOL gen5, int kind);
// Once the player is free: a battle against trainer1 (and trainer2 too, a TAG battle, when it is another), as a script's
// trainerbattle starts it; in BW2's engine when the setting says so. FALSE if the field can't take a task now
BOOL Gen5Battle_StartTrainer(u32 trainer1, u32 trainer2);
// Once the player is free: a Safari Zone battle against SPECIES at LEVEL with BALLS Safari Balls, as the Safari Zone's
// grass starts one (its flag and ball counter set; out of balls, the player goes back to where they stood)
BOOL Gen5Battle_StartSafari(int species, int level, int balls);
// Once the player is free: a Bug-Catching Contest battle against SPECIES at LEVEL with BALLS Sport Balls, the contest
// started as the National Park's gate starts it if it isn't on (the party cut to its lead)
BOOL Gen5Battle_StartContest(int species, int level, int balls);
// Once the player is free: the catching tutorial, as Route 29's script starts it
BOOL Gen5Battle_StartTutorial(void);
// The contest's Sport Balls left (-1 with no contest), and the species of the Pokémon kept (0: none)
int Gen5Battle_ContestBalls(void);
int Gen5Battle_ContestSpecies(void);

// BW2's battle is on screen (for widescreen)
BOOL Gen5Battle_IsShown(void);
BOOL Gen5Battle_MoveBgShown(void);

#endif

#endif // POKEHEARTGOLD_PORT_GEN5_BATTLE_H
