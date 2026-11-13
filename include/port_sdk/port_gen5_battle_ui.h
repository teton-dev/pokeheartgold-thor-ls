#ifndef POKEHEARTGOLD_PORT_GEN5_BATTLE_UI_H
#define POKEHEARTGOLD_PORT_GEN5_BATTLE_UI_H

// For the settings pages (C++, which can't include the game's headers): port_gen5_battle.h's debug battle

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// A wild battle against SPECIES at LEVEL in BW2's engine, from where the player stands
BOOL Gen5BattleUI_StartWild(int species, int level);

#ifdef __cplusplus
}
#endif

#endif // POKEHEARTGOLD_PORT_GEN5_BATTLE_UI_H
