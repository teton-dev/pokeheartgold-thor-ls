#ifndef POKEHEARTGOLD_PORT_ENHANCED_GRAPHICS_H
#define POKEHEARTGOLD_PORT_ENHANCED_GRAPHICS_H

// Enhanced graphics in HGSS (libntr's Settings > Display: time-of-day light, sun shadows, highlights, water, atmosphere,
// glow, fog, lamp light, forest sunlight). The game-neutral part is port-common's port/enhanced_scene.h, shared with the
// Platinum fork; this is HGSS's adapter: the hour, the outdoor test, the per-map looks (Ilex Forest's fog and sunlight,
// the lamps' textures) and the water tiles. Plan and findings: the Platinum fork's docs/thor/dev/enhanced-graphics.md.

#include "field_types_def.h"

// Once per main loop iteration. fieldSystem: the field map being shown, or NULL anywhere else (menus, battles), which
// draws nothing extra
void EnhancedGraphics_Update(FieldSystem *fieldSystem);

// In the field map's 3D render, once the camera is pushed to NNS and before the map is drawn: the ground under the
// player, the lamps' textures in the area's texture sets, and the camera (EnhancedScene_SetCamera)
void EnhancedGraphics_FieldDrawing(FieldSystem *fieldSystem);

// In the field map's 3D render, after the scene: the water tiles around the player as the water mask
void EnhancedGraphics_DrawWaterMask(FieldSystem *fieldSystem);

#endif // POKEHEARTGOLD_PORT_ENHANCED_GRAPHICS_H
