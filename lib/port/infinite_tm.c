// Infinite TM uses. See port_infinite_tm.h
#include <nitro.h>

#include "constants/items.h"
#include "port_game_settings.h"
#include "port_infinite_tm.h"

BOOL InfiniteTM_Applies(u16 itemId) {
    return GameSettings_Get()->infiniteTMs && itemId >= ITEM_TM01 && itemId <= ITEM_TM92;
}
