#ifndef PORT_INFINITE_TM_H
#define PORT_INFINITE_TM_H

// Infinite TM uses (Settings > Gameplay): a TM isn't used up when it teaches a move, the shops don't buy TMs and the
// bag doesn't count them. HMs are as they were.

#include <nitro.h>

#ifdef __cplusplus
extern "C" {
#endif

// TRUE when the setting is on and the item is a TM (not an HM)
BOOL InfiniteTM_Applies(u16 itemId);

#ifdef __cplusplus
}
#endif

#endif
