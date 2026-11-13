#ifndef PORT_GEN5_ROCKET_H
#define PORT_GEN5_ROCKET_H

// The Team Rocket arena for the Gen 5 battles (lib/port/gen5_rocket.c): BW2's Champion room, darkened, with Team
// Rocket red streaks, stars, a red rocket ship and a crescent moon. BW2's background 16 (the Champion's), terrain
// GEN5_ROCKET_TERRAIN.

#include <nitro.h>

#define GEN5_ROCKET_BG 16
#define GEN5_ROCKET_TERRAIN 1

// Changes a copy of the Champion room's model resource (a BMD0 with its textures) into the arena's room, and of its
// stage's grounds into dark crimson ones. FALSE if they aren't what is expected
BOOL Gen5Rocket_Make(u8 *res, u32 size);
BOOL Gen5Rocket_MakeStage(u8 *res, u32 size);

// Before a battle: makes the arena's files from the BW2 ROM's (in gen5_data/, once, again when they change) and has
// BW2 read them. FALSE if they couldn't be made: background 16 is then the Champion's own room in every terrain
BOOL Gen5Rocket_PrepareArena(void);

#endif // PORT_GEN5_ROCKET_H
