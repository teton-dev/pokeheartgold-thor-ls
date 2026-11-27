#ifndef POKEHEARTGOLD_BILLBOARD_LISTS_H
#define POKEHEARTGOLD_BILLBOARD_LISTS_H

#include "constants/heap.h"

#include "billboard_internal.h"

void BillboardLists_Create(int count, enum HeapID heapID);
void BillboardLists_Delete(void);
void BillboardLists_Draw(void);
void Billboard_ClearPendingFree(Billboard *billboard);
void BillboardObject_SetVisible(void *billboard, u8 visible);
NNSG3dResMdl *BillboardObject_GetModel(BillboardObject *object);
#ifdef SDK_PORT
// Enhanced graphics: the sprite casts the sun's shadow (a map object whose blob shadow the sun's replaces)
void BillboardObject_SetCastsShadow(BillboardObject *object, BOOL casts);
#endif

#endif // POKEHEARTGOLD_BILLBOARD_LISTS_H
