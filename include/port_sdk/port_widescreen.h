#ifndef POKEHEARTGOLD_PORT_WIDESCREEN_H
#define POKEHEARTGOLD_PORT_WIDESCREEN_H

// Widescreen overworld (teton-dev fork; Settings > Display > Widescreen, with the top screen on a display of its
// own): the field map's camera sees as wide as the display, and the weather's 2D layers fill the sides. The Platinum
// fork's src/port/widescreen.c on HGSS's field system. Everything else (battles, menus, other scenes) stays 4:3.

#include "global.h"

#include "camera.h"
#include "field_types_def.h"

// Once a frame: tells libntr whether this frame's scene is wide, and how its 2D layers go wide
void Widescreen_Update(void);

// The aspect ratio to give the camera's projection: the field camera's widened while the scene is wide
fx32 Widescreen_AspectRatio(const Camera *camera, fx32 aspectRatio);

// Before the field map draws: the projection again when the widening changed since it was last computed
void Widescreen_BeforeFieldRender(FieldSystem *fieldSystem);

#endif // POKEHEARTGOLD_PORT_WIDESCREEN_H
