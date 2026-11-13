// What the Gen 5 battle code (gen5/, the Platinum fork's) calls in the game, under the names it has in Platinum
#include "global.h"

#include "brightness.h"

#ifdef GEN5_BATTLES

// Gen5_BattleFrame steps the game's screen brightness transitions while BW2's battle runs, as the main loop would
void BrightnessController_Update(void) {
    DoAllScreenBrightnessTransitionStep();
}

#endif
