#ifndef POKEHEARTGOLD_BATTLE_TRANSITION_TASK_H
#define POKEHEARTGOLD_BATTLE_TRANSITION_TASK_H

#include "task.h"

void CallTask_BattleTransition(TaskManager *man, int effect, int bgm);

#ifdef SDK_PORT
// A wild encounter's own transition (the grid or wave wipe, effects 0-5: see gEncounterEffectFuncs) is playing, for
// widescreen.c to widen it
BOOL EncounterEffect_PortWildPlaying(void);
// A Gym Leader's or Silver's banner, or the Elite Four's or Lance's band, is playing (encounter_effect_leaders.c)
BOOL EncounterEffect_PortGymLeaderPlaying(void);
BOOL EncounterEffect_PortEliteFourPlaying(void);
#endif

#endif
