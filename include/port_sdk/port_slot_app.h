#ifndef POKEHEARTGOLD_PORT_SLOT_APP_H
#define POKEHEARTGOLD_PORT_SLOT_APP_H

// The Japanese Game Corner's slot machine as an app of the port (teton-dev fork): the Japanese games' machine, from
// the files imported in Settings > Game Corner. Launched like Voltorb Flip is, with the same arguments.

#include "global.h"
#include "field_system.h"
#include "voltorb_flip/voltorb_flip.h"

// luck: SlotLuckiness's value for the machine, the Voltorb Flip argument (0 to 3)
VoltorbFlipArgs *SlotApp_LaunchApp(FieldSystem *fieldSystem, u32 luck);

// Whether the Japanese files are imported and the setting is on
BOOL SlotApp_IsEnabled(void);

// Tests: the next machine starts in a bonus game (as after a 7: 15 spins)
void SlotApp_TestStartInBonus(void);

#endif
