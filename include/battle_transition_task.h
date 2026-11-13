#ifndef POKEHEARTGOLD_BATTLE_TRANSITION_TASK_H
#define POKEHEARTGOLD_BATTLE_TRANSITION_TASK_H

#include "task.h"

void CallTask_BattleTransition(TaskManager *man, int effect, int bgm);

#ifdef SDK_PORT
// A wild encounter's own transition (the grid or wave wipe, effects 0-5: see gEncounterEffectFuncs) is playing, for
// widescreen.c to widen it
BOOL EncounterEffect_PortWildPlaying(void);
#endif

#endif
