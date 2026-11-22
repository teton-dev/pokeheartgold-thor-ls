// The Gear's map in Goldenrod (port-common's port/gear_map.h; Eterna's is src/port/gear_map.c): the current map's blocks
// of the map matrix and one block around them, each tile's terrain from the land data's attributes (collision and
// tile behavior), the warps, and each block's location (its map section) for the Gear's Nuzlocke catches. A first
// version: walkable ground is found by a flood from the player, the warps and the map's people, as Eterna's; the items
// on the map (item balls, hidden items) aren't read yet, and ledges, bridges and the like show as plain ground.
#include "port/gear_map.h"

#include <nitro.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"

#include "constants/metatile_behavior.h"

#include "field_system.h"
#include "filesystem.h"
#include "map_events_internal.h"
#include "map_header.h"
#include "map_matrix.h"
#include "player_avatar.h"
#include "port_debug_field.h"
#include "terrain_attributes.h"

#define MAX_BLOCKS GEAR_MAP_MAX_BLOCKS
#define COLLISION_MASK 0x8000
#define BEHAVIOR_MASK 0x00FF

static GearMap sMap;
static u8 *sTiles = NULL;
static u32 sKey = 0;
static int sOriginX;
static int sOriginZ;

static BOOL IsWater(u8 b) {
    return b == TILE_BEHAVIOR_WATER_RIVER || b == TILE_BEHAVIOR_WATER_SEA || b == TILE_BEHAVIOR_WATERFALL
        || b == TILE_BEHAVIOR_WHIRLPOOL || b == TILE_BEHAVIOR_SHALLOW_WATER;
}

static BOOL IsLedge(u8 b) {
    return b >= TILE_BEHAVIOR_JUMP_EAST && b <= TILE_BEHAVIOR_JUMP_SOUTH;
}

static BOOL IsWarp(u8 b) {
    return (b >= TILE_BEHAVIOR_WARP_STAIRS_EAST && b <= TILE_BEHAVIOR_WARP_SOUTH);
}

// Somewhere a trainer can be (water, ledges, warps and rock climbs count as well as open ground)
static BOOL IsPassable(u16 attributes) {
    const u8 b = attributes & BEHAVIOR_MASK;
    if (!(attributes & COLLISION_MASK)) {
        return TRUE;
    }
    return IsWater(b) || IsLedge(b) || IsWarp(b) || b == TILE_BEHAVIOR_ROCK_CLIMB_NORTH_SOUTH
        || b == TILE_BEHAVIOR_ROCK_CLIMB_EAST_WEST;
}

static u8 Classify(u16 attributes) {
    const u8 b = attributes & BEHAVIOR_MASK;
    if (b == TILE_BEHAVIOR_WATERFALL) {
        return GEAR_MAP_WATERFALL;
    }
    if (IsWater(b)) {
        return GEAR_MAP_WATER;
    }
    switch (b) {
    case TILE_BEHAVIOR_TALL_GRASS:
        return GEAR_MAP_GRASS;
    case TILE_BEHAVIOR_VERY_TALL_GRASS:
        return GEAR_MAP_TALL_GRASS;
    case TILE_BEHAVIOR_JUMP_NORTH:
        return GEAR_MAP_LEDGE_NORTH;
    case TILE_BEHAVIOR_JUMP_SOUTH:
        return GEAR_MAP_LEDGE_SOUTH;
    case TILE_BEHAVIOR_JUMP_WEST:
        return GEAR_MAP_LEDGE_WEST;
    case TILE_BEHAVIOR_JUMP_EAST:
        return GEAR_MAP_LEDGE_EAST;
    case TILE_BEHAVIOR_ROCK_CLIMB_NORTH_SOUTH:
    case TILE_BEHAVIOR_ROCK_CLIMB_EAST_WEST:
        return GEAR_MAP_ROCK_CLIMB;
    case TILE_BEHAVIOR_CAVE_FLOOR:
        return GEAR_MAP_CAVE;
    case TILE_BEHAVIOR_ICE:
        return GEAR_MAP_ICE;
    case TILE_BEHAVIOR_SAND:
        return GEAR_MAP_SAND;
    default:
        break;
    }
    return IsWarp(b) ? GEAR_MAP_WARP : GEAR_MAP_FLOOR;
}

static u32 Hash(u32 hash, u32 value) {
    return (hash ^ value) * 16777619u;
}

// A block's land data, when it has any
static BOOL BlockTerrain(u16 landDataID, u16 *out) {
    const u32 size = GetNarcMemberSizeByIdPair(NARC_fielddata_landdata_land_data, landDataID);
    if (size < TERRAIN_ATTRIBUTES_OFFSET + TERRAIN_ATTRIBUTES_SIZE) {
        return FALSE;
    }
    // The member is read whole (a few KB, outside the game's heaps) and its attributes copied
    u8 *file = malloc(size);
    if (file == NULL) {
        return FALSE;
    }
    ReadWholeNarcMemberByIdPair(file, NARC_fielddata_landdata_land_data, landDataID);
    memcpy(out, file + TERRAIN_ATTRIBUTES_OFFSET, TERRAIN_ATTRIBUTES_SIZE);
    free(file);
    return TRUE;
}

static u32 TilesKey(FieldSystem *fieldSystem, int blockX, int blockZ) {
    const MapMatrix *matrix = fieldSystem->mapMatrix;
    u32 key = Hash(2166136261u, (u32)fieldSystem->location->mapId);
    key = Hash(key, matrix->matrix_id);
    key = Hash(key, ((u32)matrix->width << 8) | matrix->height);
    for (int i = 0; i < matrix->width * matrix->height; i++) {
        key = Hash(key, matrix->data.maps.models[i] | ((u32)matrix->data.headers[i] << 16));
    }
    key = Hash(key, (u32)(blockX / MAX_BLOCKS) | ((u32)(blockZ / MAX_BLOCKS) << 8));
    return key == 0 ? 1 : key;
}

static void BuildTiles(FieldSystem *fieldSystem, int playerX, int playerZ) {
    const MapMatrix *matrix = fieldSystem->mapMatrix;
    const int width = matrix->width, height = matrix->height;
    const int header = fieldSystem->location->mapId;

    // The map's blocks, and one block around them
    int x0 = width, x1 = -1, z0 = height, z1 = -1;
    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {
            if (matrix->data.headers[z * width + x] == header) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                z0 = z < z0 ? z : z0;
                z1 = z > z1 ? z : z1;
            }
        }
    }
    const int playerBlockX = playerX / MAP_TILES_COUNT_X, playerBlockZ = playerZ / MAP_TILES_COUNT_Z;
    if (x1 < 0) {
        x0 = x1 = playerBlockX;
        z0 = z1 = playerBlockZ;
    }
    x0 = x0 > 0 ? x0 - 1 : 0;
    z0 = z0 > 0 ? z0 - 1 : 0;
    x1 = x1 < width - 1 ? x1 + 1 : width - 1;
    z1 = z1 < height - 1 ? z1 + 1 : height - 1;
    if (x1 - x0 + 1 > MAX_BLOCKS) {
        x0 = playerBlockX - MAX_BLOCKS / 2;
        x0 = x0 < 0 ? 0 : (x0 + MAX_BLOCKS > width ? width - MAX_BLOCKS : x0);
        x1 = x0 + MAX_BLOCKS - 1;
    }
    if (z1 - z0 + 1 > MAX_BLOCKS) {
        z0 = playerBlockZ - MAX_BLOCKS / 2;
        z0 = z0 < 0 ? 0 : (z0 + MAX_BLOCKS > height ? height - MAX_BLOCKS : z0);
        z1 = z0 + MAX_BLOCKS - 1;
    }

    const int w = (x1 - x0 + 1) * MAP_TILES_COUNT_X, h = (z1 - z0 + 1) * MAP_TILES_COUNT_Z;
    u16 *attributes = malloc(sizeof(u16) * w * h);
    u8 *other = calloc(w * h, 1);
    u8 *reached = calloc(w * h, 1);
    int *queue = malloc(sizeof(int) * w * h);
    free(sTiles);
    sTiles = calloc(w * h, 1);
    if (attributes == NULL || other == NULL || reached == NULL || queue == NULL || sTiles == NULL) {
        free(attributes);
        free(other);
        free(reached);
        free(queue);
        free(sTiles);
        sTiles = NULL;
        sMap.width = sMap.height = 0;
        return;
    }

    static u16 block[MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z];
    for (int bz = z0; bz <= z1; bz++) {
        for (int bx = x0; bx <= x1; bx++) {
            const int index = bz * width + bx;
            const BOOL valid = BlockTerrain(matrix->data.maps.models[index], block);
            const BOOL isOther = matrix->data.headers[index] != header;
            sMap.blockLabels[(bz - z0) * (x1 - x0 + 1) + (bx - x0)] =
                valid ? (u16)MapHeader_GetMapSec(matrix->data.headers[index]) : GEAR_MAP_NO_LABEL;
            for (int t = 0; t < MAP_TILES_COUNT_X * MAP_TILES_COUNT_Z; t++) {
                const int x = (bx - x0) * MAP_TILES_COUNT_X + t % MAP_TILES_COUNT_X;
                const int z = (bz - z0) * MAP_TILES_COUNT_Z + t / MAP_TILES_COUNT_X;
                attributes[z * w + x] = valid ? block[t] : (COLLISION_MASK | 0x7F);
                other[z * w + x] = isOther;
            }
        }
    }
    sOriginX = x0 * MAP_TILES_COUNT_X;
    sOriginZ = z0 * MAP_TILES_COUNT_Z;

    // Where you can get to: from the player, the warps and the people
    int head = 0, tail = 0;
#define SEED(gx, gz, force)                                                                        \
    do {                                                                                           \
        const int sx = (gx) - sOriginX, sz = (gz) - sOriginZ;                                      \
        if (sx >= 0 && sx < w && sz >= 0 && sz < h && !reached[sz * w + sx]                        \
            && ((force) || IsPassable(attributes[sz * w + sx]))) {                                 \
            reached[sz * w + sx] = 1;                                                              \
            queue[tail++] = sz * w + sx;                                                           \
        }                                                                                          \
    } while (0)
    SEED(playerX, playerZ, TRUE);
    const MapEvents *events = fieldSystem->mapEvents;
    if (events != NULL) {
        for (u32 i = 0; i < events->num_warp_events; i++) {
            SEED(events->warp_events[i].x, events->warp_events[i].z, FALSE);
        }
        for (u32 i = 0; i < events->num_object_events; i++) {
            SEED(events->object_events[i].x, events->object_events[i].z, FALSE);
        }
    }
#undef SEED
    while (head < tail) {
        const int index = queue[head++];
        const int x = index % w, z = index / w;
        static const int kSteps[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int s = 0; s < 4; s++) {
            const int nx = x + kSteps[s][0], nz = z + kSteps[s][1];
            if (nx < 0 || nx >= w || nz < 0 || nz >= h) {
                continue;
            }
            const int next = nz * w + nx;
            if (!reached[next] && IsPassable(attributes[next])) {
                reached[next] = 1;
                queue[tail++] = next;
            }
        }
    }

    int areaX0 = w, areaZ0 = h, areaX1 = -1, areaZ1 = -1;
    for (int z = 0; z < h; z++) {
        for (int x = 0; x < w; x++) {
            const int index = z * w + x;
            u8 tile = GEAR_MAP_VOID;
            if (reached[index]) {
                tile = (attributes[index] & COLLISION_MASK) && !IsPassable(attributes[index]) ? GEAR_MAP_FLOOR
                                                                                              : Classify(attributes[index]);
            } else if (!IsPassable(attributes[index])) {
                for (int dz = -1; dz <= 1 && tile == GEAR_MAP_VOID; dz++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = x + dx, nz = z + dz;
                        if (nx >= 0 && nx < w && nz >= 0 && nz < h && reached[nz * w + nx]) {
                            tile = GEAR_MAP_WALL;
                            break;
                        }
                    }
                }
            }
            if (tile != GEAR_MAP_VOID && other[index]) {
                tile |= GEAR_MAP_OTHER_AREA;
            } else if (tile != GEAR_MAP_VOID) {
                areaX0 = x < areaX0 ? x : areaX0;
                areaX1 = x > areaX1 ? x : areaX1;
                areaZ0 = z < areaZ0 ? z : areaZ0;
                areaZ1 = z > areaZ1 ? z : areaZ1;
            }
            sTiles[index] = tile;
        }
    }
    free(attributes);
    free(other);
    free(reached);
    free(queue);
    if (areaX1 < 0) {
        areaX0 = areaZ0 = 0;
        areaX1 = w - 1;
        areaZ1 = h - 1;
    }
    sMap.areaX0 = areaX0;
    sMap.areaZ0 = areaZ0;
    sMap.areaX1 = areaX1;
    sMap.areaZ1 = areaZ1;
    sMap.width = w;
    sMap.height = h;
    sMap.blocksX = x1 - x0 + 1;
    sMap.blocksZ = z1 - z0 + 1;
    sMap.tilesVersion++;
}

const GearMap *Gear_GetMap(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->location == NULL || fieldSystem->mapMatrix == NULL
        || fieldSystem->mapEvents == NULL || fieldSystem->playerAvatar == NULL || fieldSystem->mapMatrix->width == 0) {
        sMap.valid = FALSE;
        return &sMap;
    }
    // Only while the overworld runs: meanwhile the map stays as it was
    if (!fieldSystem->runningFieldMap) {
        return &sMap;
    }
    const int playerX = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
    const int playerZ = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
    const u32 key = TilesKey(fieldSystem, playerX / MAP_TILES_COUNT_X, playerZ / MAP_TILES_COUNT_Z);
    if (key != sKey || sTiles == NULL) {
        sKey = key;
        BuildTiles(fieldSystem, playerX, playerZ);
    }
    if (sTiles == NULL) {
        sMap.valid = FALSE;
        return &sMap;
    }
    sMap.valid = TRUE;
    sMap.mapHeaderID = fieldSystem->location->mapId;
    sMap.tiles = sTiles;
    sMap.playerX = playerX - sOriginX;
    sMap.playerZ = playerZ - sOriginZ;
    sMap.playerDir = (int)PlayerAvatar_GetFacingDirection(fieldSystem->playerAvatar);
    sMap.itemCount = 0; // not read yet

    const MapEvents *events = fieldSystem->mapEvents;
    sMap.warpCount = 0;
    for (u32 i = 0; i < events->num_warp_events && sMap.warpCount < GEAR_MAP_MAX_WARPS; i++) {
        sMap.warps[sMap.warpCount][0] = (s16)(events->warp_events[i].x - sOriginX);
        sMap.warps[sMap.warpCount][1] = (s16)(events->warp_events[i].z - sOriginZ);
        sMap.warpLabels[sMap.warpCount] = (u16)MapHeader_GetMapSec(events->warp_events[i].header);
        sMap.warpCount++;
    }
    return &sMap;
}
