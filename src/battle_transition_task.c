#include "battle_transition_task.h"

#include "global.h"

#include "field/encounter_effect.h"

#include "heap.h"
#include "sound_system.h"
#include "task.h"

typedef struct BattleTransitionTaskEnv {
    int state;
    int done; // set to 1 by the transition effect
    int effect;
    u32 bgm;
} BattleTransitionTaskEnv;

enum {
    BATTLE_TRANSITION_STATE_START,
    BATTLE_TRANSITION_STATE_WAIT,
};

static BOOL Task_BattleTransition(TaskManager *taskMan);

#ifdef SDK_PORT
// Effects 0-5 (gEncounterEffectFuncs) are the ordinary wild encounter's own transition (the grid or wave wipe),
// confined to the DS's 256 pixels, and 46 Red's; widescreen.c widens it while this is TRUE
static BOOL sPortWildPlaying = FALSE;

BOOL EncounterEffect_PortWildPlaying(void) {
    return sPortWildPlaying;
}
#endif

// Plays the battle transition effect and starts the battle music, then waits for the effect to finish.
static BOOL Task_BattleTransition(TaskManager *taskMan) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskMan);
    BattleTransitionTaskEnv *env = TaskManager_GetEnvironment(taskMan);

    switch (env->state) {
    case BATTLE_TRANSITION_STATE_START:
        EncounterEffect_LoadOverlay(env->effect);
        EncounterEffect_Start(env->effect, fieldSystem, &env->done);
        Sound_SetSceneAndPlayBGM(5, env->bgm, 1);
#ifdef SDK_PORT
        // Red's (46) is built the same way: black blocks filling BG3 over the overworld
        sPortWildPlaying = env->effect <= 5 || env->effect == 46;
#endif
        env->state++;
        break;
    case BATTLE_TRANSITION_STATE_WAIT:
        if (env->done == 1) {
#ifdef SDK_PORT
            sPortWildPlaying = FALSE;
#endif
            EncounterEffect_UnloadOverlay(env->effect);
            Heap_Free(env);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void CallTask_BattleTransition(TaskManager *taskMan, int effect, int bgm) {
    BattleTransitionTaskEnv *env = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(BattleTransitionTaskEnv));
    env->state = BATTLE_TRANSITION_STATE_START;
    env->done = 0;
    env->effect = effect;
    env->bgm = bgm;
    TaskManager_Call(taskMan, Task_BattleTransition, env);
}
