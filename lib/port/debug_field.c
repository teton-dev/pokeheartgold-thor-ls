// The debug window's moves on the overworld. See include/port_sdk/port_debug_field.h.
#include "port_debug_field.h"

#include <stdio.h>
#include <string.h>

#include "global.h"

#include "constants/trainer_class.h"

#include "msgdata/msg.naix"

#include "battle/battle_setup.h"
#include "blackout.h"
#include "encounter.h"
#include "field_system.h"
#include "launch_application.h"
#include "application/pc_box/pc_box_internal.h"
#include "heap.h"
#include "field_warp_tasks.h"
#include "map_header.h"
#include "map_object.h"
#include "msgdata.h"
#include "player_avatar.h"
#include "pm_string.h"
#include "script_manager.h"
#include "spawn_warp.h"
#include "task.h"
#include "trainer_data.h"

// The player is free on the overworld: the field map runs, with no application over it, no task (scripts and events
// run as field tasks) and no pause (as the test scripts check)
const char *DebugField_Unavailable(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->processManager == NULL || fieldSystem->location == NULL) {
        return "Load your save first: continue your game from the title screen.";
    }
    if (fieldSystem->processManager->parent == NULL || fieldSystem->processManager->child != NULL
        || !fieldSystem->runningFieldMap || fieldSystem->playerAvatar == NULL) {
        return "Close the game's menu or finish the battle first.";
    }
    if (fieldSystem->taskman != NULL || fieldSystem->processManager->isPaused) {
        return "Wait until you can walk (something is happening on the map).";
    }
    return NULL;
}

static FieldSystem *FreeField(void) {
    return DebugField_Unavailable() == NULL ? FieldSystem_PortGet() : NULL;
}

// The game's text to ASCII (as the save editor does): letters, digits, the punctuation in names
static void TextToAscii(const u16 *chars, int maxLen, char *out, int outSize) {
    static const struct {
        u16 c;
        char a;
    } kPunctuation[] = {
        { 0x1DE, ' ' }, { 0x1AB, '!' }, { 0x1AC, '?' }, { 0x1AD, ',' }, { 0x1AE, '.' }, { 0x1B1, '/' },
        { 0x1B3, '\'' }, { 0x1B9, '(' }, { 0x1BA, ')' }, { 0x1BE, '-' }, { 0x1C4, ':' }, { 0x188, 'e' },
        { 0x1C0, '&' },
    };
    int pos = 0;
    for (int i = 0; i < maxLen && chars[i] != EOS && pos < outSize - 1; i++) {
        const u16 c = chars[i];
        char a = 0;
        if (c >= 0x121 && c <= 0x12A) {
            a = '0' + (c - 0x121);
        } else if (c >= 0x12B && c <= 0x144) {
            a = 'A' + (c - 0x12B);
        } else if (c >= 0x145 && c <= 0x15E) {
            a = 'a' + (c - 0x145);
        } else {
            for (int k = 0; k < (int)NELEMS(kPunctuation); k++) {
                if (kPunctuation[k].c == c) {
                    a = kPunctuation[k].a;
                }
            }
        }
        if (a != 0) {
            out[pos++] = a;
        }
    }
    out[pos] = '\0';
}

// One string of a message bank, in ASCII
static void BankString(int bank, int id, char *out, int outSize) {
    out[0] = '\0';
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, bank, HEAP_ID_DEFAULT);
    if (msgData == NULL) {
        return;
    }
    if (id >= 0 && id < (int)MsgDataGetCount(msgData)) {
        String *string = String_New(128, HEAP_ID_DEFAULT);
        ReadMsgDataIntoString(msgData, id, string);
        if (String_IsTrainerName(string)) {
            // Trainer names may be packed (String_Cat_HandleTrainerName unpacks them)
            String *unpacked = String_New(128, HEAP_ID_DEFAULT);
            String_Cat_HandleTrainerName(unpacked, string);
            String_Delete(string);
            string = unpacked;
        }
        TextToAscii(String_cstr(string), String_GetLength(string), out, outSize);
        String_Delete(string);
    }
    DestroyMsgData(msgData);
}

int DebugField_MapId(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL && fieldSystem->location != NULL ? fieldSystem->location->mapId : -1;
}

const char *DebugField_MapName(void) {
    static char name[DEBUG_FIELD_NAME_LEN];
    static int cachedMap = -2;
    const int mapId = DebugField_MapId();
    if (mapId != cachedMap) {
        cachedMap = mapId;
        if (mapId < 0) {
            name[0] = '\0';
        } else {
            BankString(NARC_msg_msg_0279_bin, MapHeader_GetMapSec(mapId), name, sizeof(name)); // the landmark names
        }
    }
    return name;
}

void DebugField_Position(int *x, int *z) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    *x = *z = 0;
    if (fieldSystem != NULL && fieldSystem->playerAvatar != NULL) {
        *x = PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
        *z = PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
    }
}

// --- Warps -------------------------------------------------------------------------------------------------------

// The places Fly goes to: the game's spawn points (spawn_warp.c), named after the map each lands on (the decompilation's
// SPAWN_ names don't follow its table's order)
#define NUM_TOWNS 30

int DebugField_TownCount(void) {
    return NUM_TOWNS;
}

const char *DebugField_TownName(int town) {
    static char names[NUM_TOWNS][DEBUG_FIELD_NAME_LEN];
    static BOOL loaded = FALSE;
    if (town < 0 || town >= NUM_TOWNS) {
        return "?";
    }
    if (!loaded) {
        loaded = TRUE;
        for (int i = 0; i < NUM_TOWNS; i++) {
            Location dest;
            GetFlyWarpData(i + 1, &dest);
            BankString(NARC_msg_msg_0279_bin, MapHeader_GetMapSec(dest.mapId), names[i], DEBUG_FIELD_NAME_LEN);
        }
    }
    return names[town];
}

// A warp as the script command does, from a task of its own (as the test scripts' warp)
static Location sWarp;

static BOOL Task_DebugWarp(TaskManager *taskManager) {
    u32 *state = TaskManager_GetStatePtr(taskManager);
    if (*state == 0) {
        (*state)++;
        CallTask_ScriptWarp(taskManager, sWarp.mapId, -1, sWarp.x, sWarp.y, sWarp.direction);
        return FALSE;
    }
    return TRUE;
}

static BOOL Warp(int mapId, int x, int z, int direction) {
    FieldSystem *fieldSystem = FreeField();
    if (fieldSystem == NULL || mapId < 0) {
        return FALSE;
    }
    sWarp.mapId = mapId;
    sWarp.warpId = -1;
    sWarp.x = x;
    sWarp.y = z;
    sWarp.direction = direction;
    FieldSystem_CreateTask(fieldSystem, Task_DebugWarp, NULL);
    printf("[debug] warp to map %d at (%d, %d)\n", mapId, x, z);
    return TRUE;
}

BOOL DebugField_WarpToTown(int town) {
    if (town < 0 || town >= DebugField_TownCount() || FreeField() == NULL) {
        return FALSE;
    }
    Location dest;
    GetFlyWarpData(town + 1, &dest);
    return Warp(dest.mapId, dest.x, dest.y, DIR_SOUTH);
}

BOOL DebugField_WarpTo(int mapId, int x, int z) {
    return Warp(mapId, x, z, DIR_SOUTH);
}

// --- Trainers ----------------------------------------------------------------------------------------------------

int DebugField_TrainerCount(void) {
    static int count = 0;
    if (count == 0) {
        MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0729_bin, HEAP_ID_DEFAULT);
        if (msgData != NULL) {
            count = (int)MsgDataGetCount(msgData);
            DestroyMsgData(msgData);
        }
    }
    return count;
}

// Every trainer's name with its class, read once (the debug window shows and searches them every frame)
static char (*sTrainerNames)[DEBUG_FIELD_NAME_LEN] = NULL;

static void LoadTrainerNames(void) {
    const int count = DebugField_TrainerCount();
    if (sTrainerNames != NULL || count <= 0) {
        return;
    }
    static char names[1024][DEBUG_FIELD_NAME_LEN];
    sTrainerNames = names;
    MsgData *classes = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0730_bin, HEAP_ID_DEFAULT);
    MsgData *own = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, NARC_msg_msg_0729_bin, HEAP_ID_DEFAULT);
    String *string = String_New(128, HEAP_ID_DEFAULT);
    String *unpacked = String_New(128, HEAP_ID_DEFAULT);
    const int classCount = classes != NULL ? (int)MsgDataGetCount(classes) : 0;
    for (int id = 0; id < count && id < 1024; id++) {
        char className[DEBUG_FIELD_NAME_LEN] = "", ownName[DEBUG_FIELD_NAME_LEN] = "";
        const int trainerClass = id > 0 ? TrainerData_GetAttr(id, TRATTR_CLASS) : -1;
        if (trainerClass >= 0 && trainerClass < classCount) {
            ReadMsgDataIntoString(classes, trainerClass, string);
            TextToAscii(String_cstr(string), String_GetLength(string), className, sizeof(className));
        }
        if (own != NULL && trainerClass != TRAINERCLASS_RIVAL) { // the rival's name is the player's choice
            ReadMsgDataIntoString(own, id, string);
            String *name = string;
            if (String_IsTrainerName(string)) {
                // Trainer names may be packed (String_Cat_HandleTrainerName unpacks them)
                String_SetEmpty(unpacked);
                String_Cat_HandleTrainerName(unpacked, string);
                name = unpacked;
            }
            TextToAscii(String_cstr(name), String_GetLength(name), ownName, sizeof(ownName));
        }
        snprintf(names[id], DEBUG_FIELD_NAME_LEN, "%s%s%s", className, ownName[0] != '\0' ? " " : "", ownName);
    }
    String_Delete(unpacked);
    String_Delete(string);
    if (own != NULL) {
        DestroyMsgData(own);
    }
    if (classes != NULL) {
        DestroyMsgData(classes);
    }
}

const char *DebugField_TrainerName(int trainerId) {
    if (trainerId <= 0 || trainerId >= DebugField_TrainerCount() || trainerId >= 1024) {
        return "?";
    }
    LoadTrainerNames();
    return sTrainerNames != NULL ? sTrainerNames[trainerId] : "?";
}

BOOL DebugField_TrainerBeaten(int trainerId) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL && fieldSystem->saveData != NULL && trainerId > 0 && TrainerFlagCheck(fieldSystem->saveData, trainerId);
}

void DebugField_SetTrainerBeaten(int trainerId, BOOL beaten) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->saveData == NULL || trainerId <= 0 || trainerId >= DebugField_TrainerCount()) {
        return;
    }
    if (beaten) {
        TrainerFlagSet(fieldSystem->saveData, trainerId);
    } else {
        TrainerFlagClear(fieldSystem->saveData, trainerId);
    }
}

// A trainer on the map: its script is one of the trainer scripts (std_npc_trainer...), as the game's sight checks
// read it (map_object_behavior.c)
static BOOL IsTrainer(LocalMapObject *object, int *trainerId) {
    switch (MapObject_GetType(object)) {
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        break;
    default:
        return FALSE;
    }
    const int id = ScriptNumToTrainerNum((u16)MapObject_GetScriptID(object));
    if (id <= 0 || id >= DebugField_TrainerCount()) {
        return FALSE;
    }
    *trainerId = id;
    return TRUE;
}

int DebugField_MapTrainers(DebugFieldTrainer *out, int max) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem == NULL || fieldSystem->mapObjectManager == NULL || fieldSystem->saveData == NULL) {
        return 0;
    }
    int count = 0;
    s32 index = 0;
    LocalMapObject *object = NULL;
    while (count < max && MapObjectManager_GetNextObjectWithFlagFromIndex(fieldSystem->mapObjectManager, &object, &index, MAPOBJECTFLAG_ACTIVE)) {
        int trainerId;
        if (!IsTrainer(object, &trainerId)) {
            continue;
        }
        DebugFieldTrainer *trainer = &out[count++];
        trainer->objectId = MapObject_GetID(object);
        trainer->trainerId = trainerId;
        trainer->x = MapObject_GetXCoord(object);
        trainer->z = MapObject_GetZCoord(object);
        trainer->beaten = TrainerFlagCheck(fieldSystem->saveData, trainerId);
        trainer->isDouble = TrainerNumIsDouble(trainerId);
        snprintf(trainer->name, sizeof(trainer->name), "%s", DebugField_TrainerName(trainerId));
    }
    return count;
}

BOOL DebugField_GoToTrainer(int objectId) {
    FieldSystem *fieldSystem = FreeField();
    if (fieldSystem == NULL) {
        return FALSE;
    }
    LocalMapObject *object = MapObjectManager_GetFirstActiveObjectByID(fieldSystem->mapObjectManager, objectId);
    if (object == NULL) {
        return FALSE;
    }
    // The tile it looks at, facing it back
    int x = MapObject_GetXCoord(object), z = MapObject_GetZCoord(object);
    const int facing = MapObject_GetFacingDirection(object);
    switch (facing) {
    case DIR_NORTH:
        z--;
        break;
    case DIR_SOUTH:
        z++;
        break;
    case DIR_WEST:
        x--;
        break;
    default:
        x++;
        break;
    }
    static const int kOpposite[] = { DIR_SOUTH, DIR_NORTH, DIR_EAST, DIR_WEST };
    return Warp(fieldSystem->location->mapId, x, z, facing >= 0 && facing < 4 ? kOpposite[facing] : DIR_NORTH);
}

typedef struct {
    u32 trainerId;
    u32 winFlag;
} TrainerStart;

static BOOL Task_DebugTrainer(TaskManager *taskManager) {
    TrainerStart *start = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);

    switch (*state) {
    case 0:
        (*state)++;
        SetupAndStartTrainerBattle(taskManager, start->trainerId, 0, 0, 0, 0, HEAP_ID_FIELD2, &start->winFlag);
        return FALSE;
    case 1:
        (*state)++;
        printf("[debug] trainer battle %u over, win flag %u\n", (unsigned)start->trainerId, (unsigned)start->winFlag);
        // As the trainer's script does: a win marks it beaten, a loss whites out (scrcmd_battle.c)
        if (!IsBattleResultWin(start->winFlag)) {
            CallTask_Blackout(taskManager);
            return FALSE;
        }
        TrainerFlagSet(fieldSystem->saveData, start->trainerId);
        return TRUE;
    default:
        return TRUE;
    }
}

BOOL DebugField_BattleTrainer(int trainerId) {
    FieldSystem *fieldSystem = FreeField();
    if (fieldSystem == NULL || trainerId <= 0 || trainerId >= DebugField_TrainerCount()) {
        return FALSE;
    }
    // Static: the field's heaps are torn down while the battle runs, under the task
    static TrainerStart sStart;
    sStart.trainerId = trainerId;
    sStart.winFlag = 0;
    FieldSystem_CreateTask(fieldSystem, Task_DebugTrainer, &sStart);
    printf("[debug] trainer battle %d (%s)\n", trainerId, DebugField_TrainerName(trainerId));
    return TRUE;
}

// --- Wild battles ------------------------------------------------------------------------------------------------

typedef struct {
    int species;
    int level;
    u32 winFlag;
} WildStart;

static BOOL Task_DebugWild(TaskManager *taskManager) {
    WildStart *start = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);
    switch (*state) {
    case 0:
        (*state)++;
        SetupAndStartWildBattle(taskManager, start->species, start->level, &start->winFlag, TRUE, FALSE);
        return FALSE;
    case 1:
        (*state)++;
        // The encounter leaves a loss to its caller, as a script's wild battle does (it whites out)
        if (!IsBattleResultWin(start->winFlag)) {
            CallTask_Blackout(taskManager);
            return FALSE;
        }
        return TRUE;
    default:
        return TRUE;
    }
}

BOOL DebugField_WildBattle(int species, int level) {
    FieldSystem *fieldSystem = FreeField();
    if (fieldSystem == NULL || species < 1 || species > 493 || level < 1 || level > 100) {
        return FALSE;
    }
    static WildStart sStart;
    sStart.species = species;
    sStart.level = level;
    sStart.winFlag = 0;
    FieldSystem_CreateTask(fieldSystem, Task_DebugWild, &sStart);
    printf("[debug] wild battle: species %d, level %d\n", species, level);
    return TRUE;
}

// --- The PC box on its own ---------------------------------------------------------------------------------------

static PCBoxArgs *sPcArgs;

static BOOL Task_DebugPC(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);
    switch (*state) {
    case 0:
        (*state)++;
        sPcArgs = Heap_Alloc(HEAP_ID_FIELD2, sizeof(PCBoxArgs));
        sPcArgs->saveData = fieldSystem->saveData;
        sPcArgs->mode = PC_BOX_MODE_MOVE_POKEMON;
        sPcArgs->menuInputStatePtr = &fieldSystem->menuInputState;
        PCBox_LaunchApp(fieldSystem, sPcArgs);
        return FALSE;
    case 1:
        if (FieldSystem_ApplicationIsRunning(fieldSystem)) {
            return FALSE;
        }
        Heap_Free(sPcArgs);
        sPcArgs = NULL;
        return TRUE;
    }
    return TRUE;
}

BOOL DebugField_OpenPC(void) {
    FieldSystem *fieldSystem = FreeField();
    if (fieldSystem == NULL) {
        return FALSE;
    }
    FieldSystem_CreateTask(fieldSystem, Task_DebugPC, NULL);
    printf("[debug] PC box opened\n");
    return TRUE;
}
