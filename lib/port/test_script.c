// Scripted test runs; see include/port_sdk/port_test_script.h and tests/README.md. The Platinum fork's runner
// (teton-dev/pokeplatinum-thor-ls, src/port/test_script.c) with the commands that don't depend on the game's
// internals, on HGSS's field system.
#include "port_test_script.h"

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <simulator/sim.h>

#include "field_system.h"
#include "overlay_01.h"
#include "heap.h"
#include "map_header.h"
#include "bag.h"
#include "font.h"
#include "msgdata.h"
#include "msgdata/msg.naix"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "port_game_settings.h"
#include "port_nuzlocke_mode.h"
#include "save_vars_flags.h"
#ifdef GEN5_BATTLES
#include "player_data.h"
#include "port_gen5_battle.h"
#include "port_gen5_rom.h"
#include "port_slot_app.h"
#include "field_overworld_transition.h"
#include "field_warp_tasks.h"
#include "task.h"
#include "port_slots_rom.h"
#endif

#define MAX_COMMANDS 1024
#define DEFAULT_TIMEOUT 3000 // Frames: 100 s on the overworld, where Docker's software 3D runs at about 30 a second
#define SHOT_PAUSE_MS 1500 // How long a shot keeps the game still, for the harness to take it
#define FREE_FRAMES 20 // The player counts as free once free this many frames in a row (events start a frame late)
#define WALK_TIMEOUT 600 // Frames a walk may push without reaching a new tile before it's taken for blocked

enum TestOp {
    OP_PRESS,
    OP_HOLD,
    OP_WAIT,
    OP_TOUCH,
    OP_WALK,
    OP_UNTIL,
    OP_MASH,
    OP_PACE,
    OP_SAVE,
    OP_GEN5_IMPORT,
    OP_GEN5_SETTING,
    OP_WILD,
    OP_TRAINER,
    OP_GIVE,
    OP_GIRL,
    OP_FLAG,
    OP_NUZLOCKE,
    OP_SLOTS_IMPORT,
    OP_SLOTS_SETTING,
    OP_CASINO,
    OP_COINS,
    OP_CHECK,
    OP_WARP,
    OP_SHOT,
    OP_LOG,
    OP_TEXT_WIDTHS,
    OP_EDITOR,
    OP_EXIT,
};

// What until and mash wait for: the events, the player free on the overworld, or a map
enum TestCondition {
    COND_FREE = TEST_EVENT_COUNT,
    COND_MAP,
    COND_NONE, // mash ... for N: just the frames
};

typedef struct TestCommand {
    enum TestOp op;
    int line;
    u16 keys;
    int frames;
    int x, y;
    int condition;
    BOOL touch; // mash touches (x, y) rather than pressing keys
    u16 keys2; // pace: the way back
    int map;
    int timeout;
    char text[96];
} TestCommand;

static struct {
    BOOL loaded;
    BOOL active;
    TestCommand commands[MAX_COMMANDS];
    int count;
    int current;
    u32 frame;
    u32 commandStart; // The frame the current command started
    u64 commandStartMs; // And when
    u32 lastWaitEnd; // The frame the last until or mash ... until was met: events count from there
    BOOL started; // The current command has been started
    BOOL failed;
    u64 pauseUntil;
    BOOL pausing; // A shot's pause is on
    u32 presentMs; // Test mode's TestModePresentMs, while a pause shows every frame
    u32 eventFrames[TEST_EVENT_COUNT]; // The frame each event last happened, plus one
    u16 touchX, touchY;
    BOOL touching;
    int freeFrames; // Frames in a row the player has been free on the overworld
    int walkX, walkZ, walkMap; // Where a walk started
    int walkLastX, walkLastZ; // The tile the walk was last on
    int walkStuck; // Frames the walk has pushed, free, without reaching a new tile
    u64 walkStuckMs; // Since when
} sTest;

static const struct {
    const char *name;
    u16 mask;
} sKeyNames[] = {
    { "A", PAD_BUTTON_A },
    { "B", PAD_BUTTON_B },
    { "X", PAD_BUTTON_X },
    { "Y", PAD_BUTTON_Y },
    { "L", PAD_BUTTON_L },
    { "R", PAD_BUTTON_R },
    { "START", PAD_BUTTON_START },
    { "SELECT", PAD_BUTTON_SELECT },
    { "UP", PAD_KEY_UP },
    { "DOWN", PAD_KEY_DOWN },
    { "LEFT", PAD_KEY_LEFT },
    { "RIGHT", PAD_KEY_RIGHT },
};

static const char *sConditionNames[] = {
    [TEST_EVENT_MENU] = "menu",
    [TEST_EVENT_NAMING_SCREEN] = "naming",
    [TEST_EVENT_BATTLE] = "battle",
    [TEST_EVENT_BATTLE_COMMAND] = "command",
    [TEST_EVENT_SLOTS] = "slots",
    [COND_FREE] = "free",
    [COND_MAP] = "map",
    [COND_NONE] = "end",
};

static u64 NowMs(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u64)now.tv_sec * 1000 + (u64)now.tv_nsec / 1000000;
}

// The player is free on the overworld: the field map runs, with no application over it, no task (scripts and
// events run as field tasks) and no pause
// warp: a warp as the script command does, from a task of its own
static struct {
    int map, x, z;
} sTestWarp;

static BOOL Task_TestWarp(TaskManager *taskManager) {
    static int sState = 0;
    if (sState == 0) {
        CallTask_ScriptWarp(taskManager, sTestWarp.map, -1, sTestWarp.x, sTestWarp.z, 0);
        sState = 1;
        return FALSE;
    }
    sState = 0;
    return TRUE;
}

// A Game Corner machine as a script runs it: fade out and leave the overworld, the app, then the overworld back
static BOOL Task_TestCasino(TaskManager *taskManager) {
    static int sState = 0;
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    switch (sState) {
    case 0:
        CallTask_FadeOutAndLeaveOverworld(taskManager);
        sState = 1;
        break;
    case 1:
        SlotApp_LaunchApp(fieldSystem, 0);
        sState = 2;
        break;
    case 2:
        if (!FieldSystem_ApplicationIsRunning(fieldSystem)) {
            CallTask_RestoreOverworldAndFadeIn(taskManager);
            sState = 3;
        }
        break;
    case 3:
        sState = 0;
        return TRUE;
    }
    return FALSE;
}

static BOOL PlayerIsFree(void) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return fieldSystem != NULL
        && fieldSystem->processManager != NULL
        && fieldSystem->processManager->parent != NULL
        && fieldSystem->processManager->child == NULL
        && !fieldSystem->processManager->isPaused
        && fieldSystem->runningFieldMap
        && fieldSystem->taskman == NULL
        && fieldSystem->playerAvatar != NULL
        && fieldSystem->location != NULL;
}

static void PrintPlace(const char *what) {
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    if (fieldSystem != NULL && fieldSystem->location != NULL && fieldSystem->playerAvatar != NULL) {
        printf("[test] %s map %d at (%d, %d)\n", what, fieldSystem->location->mapId,
            (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar), (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar));
    }
}

static void Fail(const TestCommand *command, const char *why) {
    printf("[test] FAIL line %d: %s\n", command != NULL ? command->line : 0, why);
    PrintPlace("the player is on");
    printf("[test] shot failure\n");
    fflush(stdout);
    sTest.failed = TRUE;
    sTest.pauseUntil = NowMs() + SHOT_PAUSE_MS + 1000;
}

static BOOL ParseKeys(const char *word, u16 *out) {
    char copy[64];
    snprintf(copy, sizeof(copy), "%s", word);
    *out = 0;
    for (char *key = strtok(copy, "+"); key != NULL; key = strtok(NULL, "+")) {
        int i;
        for (i = 0; i < (int)NELEMS(sKeyNames); i++) {
            if (strcmp(key, sKeyNames[i].name) == 0) {
                *out |= sKeyNames[i].mask;
                break;
            }
        }
        if (i == (int)NELEMS(sKeyNames)) {
            return FALSE;
        }
    }
    return *out != 0;
}

// "until" CONDITION [ARG] [timeout N], from words[0]; returns FALSE if it can't be read
static BOOL ParseCondition(char **words, int count, TestCommand *command) {
    if (count < 1) {
        return FALSE;
    }
    command->condition = -1;
    for (int i = 0; i < (int)NELEMS(sConditionNames); i++) {
        if (sConditionNames[i] != NULL && strcmp(words[0], sConditionNames[i]) == 0) {
            command->condition = i;
        }
    }
    int next = 1;
    if (command->condition == COND_MAP) {
        if (count < 2) {
            return FALSE;
        }
        command->map = atoi(words[1]);
        next = 2;
    }
    command->timeout = DEFAULT_TIMEOUT;
    if (next + 1 < count && strcmp(words[next], "timeout") == 0) {
        command->timeout = atoi(words[next + 1]);
    }
    return command->condition >= 0;
}

static BOOL ParseLine(char *text, int line, TestCommand *command) {
    char *words[8];
    int count = 0;
    char *rest = NULL;
    memset(command, 0, sizeof(*command));
    command->line = line;

    // log and shot keep the rest of the line
    char *space = strchr(text, ' ');
    if (space != NULL) {
        rest = space + 1;
    }
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", text);
    for (char *word = strtok(copy, " \t"); word != NULL && count < 8; word = strtok(NULL, " \t")) {
        words[count++] = word;
    }
    if (count == 0) {
        return FALSE;
    }

    if (strcmp(words[0], "press") == 0 && count >= 2) {
        command->op = OP_PRESS;
        command->frames = count >= 3 ? atoi(words[2]) : 2;
        return ParseKeys(words[1], &command->keys);
    } else if (strcmp(words[0], "hold") == 0 && count >= 3) {
        command->op = OP_HOLD;
        command->frames = atoi(words[2]);
        return ParseKeys(words[1], &command->keys);
    } else if (strcmp(words[0], "wait") == 0 && count >= 2) {
        command->op = OP_WAIT;
        command->frames = atoi(words[1]);
        return TRUE;
    } else if (strcmp(words[0], "touch") == 0 && count >= 3) {
        command->op = OP_TOUCH;
        command->x = atoi(words[1]);
        command->y = atoi(words[2]);
        command->frames = count >= 4 ? atoi(words[3]) : 3;
        return TRUE;
    } else if (strcmp(words[0], "walk") == 0 && count >= 2) {
        // walk UP|DOWN|LEFT|RIGHT [tiles]
        command->op = OP_WALK;
        command->frames = count >= 3 ? atoi(words[2]) : 1;
        return ParseKeys(words[1], &command->keys)
            && (command->keys & ~(PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT)) == 0;
    } else if (strcmp(words[0], "until") == 0) {
        command->op = OP_UNTIL;
        return ParseCondition(words + 1, count - 1, command);
    } else if (strcmp(words[0], "mash") == 0 && count >= 3) {
        // mash [KEYS] [touch X Y], until CONDITION [timeout N] or for N frames
        command->op = OP_MASH;
        int next = 1;
        if (strcmp(words[next], "touch") != 0) {
            if (!ParseKeys(words[next], &command->keys)) {
                return FALSE;
            }
            next++;
        }
        if (next + 2 < count && strcmp(words[next], "touch") == 0) {
            command->touch = TRUE;
            command->x = atoi(words[next + 1]);
            command->y = atoi(words[next + 2]);
            next += 3;
        }
        if (next >= count) {
            return FALSE;
        }
        if (strcmp(words[next], "for") == 0 && next + 1 < count) {
            command->condition = COND_NONE;
            command->timeout = atoi(words[next + 1]);
            return TRUE;
        }
        return strcmp(words[next], "until") == 0 && ParseCondition(words + next + 1, count - next - 1, command);
    } else if (strcmp(words[0], "pace") == 0 && count >= 5) {
        // pace DIR DIR until CONDITION [timeout N]: one way and back (two tiles each) while the player is free
        command->op = OP_PACE;
        return ParseKeys(words[1], &command->keys) && ParseKeys(words[2], &command->keys2)
            && strcmp(words[3], "until") == 0 && ParseCondition(words + 4, count - 4, command);
    } else if (strcmp(words[0], "save") == 0) {
        command->op = OP_SAVE;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "gen5-import") == 0) {
        // The Black 2 or White 2 ROM in POKEHEARTGOLD_GEN5_ROM, imported as the settings do
        command->op = OP_GEN5_IMPORT;
        command->timeout = 9000;
        return TRUE;
    } else if (strcmp(words[0], "slots-import") == 0) {
        // slots-import: the Japanese HeartGold or SoulSilver ROM in POKEHEARTGOLD_SLOTS_ROM, imported as Settings >
        // Game Corner does
        command->op = OP_SLOTS_IMPORT;
        command->timeout = 9000;
        return TRUE;
    } else if (strcmp(words[0], "slots") == 0 && count >= 2) {
        // slots on|off: Settings > Game Corner > Japanese Game Corner
        command->op = OP_SLOTS_SETTING;
        command->x = strcmp(words[1], "on") == 0;
        return TRUE;
    } else if (strcmp(words[0], "warp") == 0 && count >= 4) {
        // warp MAP X Z: once the player is free, a warp there (as the script command Warp does), facing up
        command->op = OP_WARP;
        command->map = atoi(words[1]);
        command->x = atoi(words[2]);
        command->y = atoi(words[3]);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "coins") == 0 && count >= 2) {
        // coins N: once the player is free, the Coin Case holds N coins
        command->op = OP_COINS;
        command->x = atoi(words[1]);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "check") == 0 && count >= 3) {
        // check coins N, check party N, check item ITEM N: once the player is free, the test fails unless the Coin
        // Case holds N coins, the party has N Pokémon, or the bag holds N of item ITEM (include/constants/items.h)
        command->op = OP_CHECK;
        command->y = strcmp(words[1], "coins") == 0 ? 0 : strcmp(words[1], "party") == 0 ? 1 : 2;
        command->frames = command->y == 2 ? atoi(words[2]) : 0;
        command->x = atoi(words[command->y == 2 ? 3 : 2]);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return command->y != 2 || count >= 4;
    } else if (strcmp(words[0], "casino") == 0) {
        // casino [bonus]: (with bonus, the machine starts in a bonus game)
        command->x = count >= 2 && strcmp(words[1], "bonus") == 0;
        // casino: once the player is free, a Game Corner machine as the script starts it (the Japanese slot machine
        // when "slots on", else Voltorb Flip)
        command->op = OP_CASINO;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "gen5") == 0 && count >= 2) {
        // gen5 on|off: Settings > Gen 5 battles
        command->op = OP_GEN5_SETTING;
        command->x = strcmp(words[1], "on") == 0;
        return TRUE;
    } else if (strcmp(words[0], "girl") == 0) {
        // girl: the player is a girl (Lyra) from now on: the save's gender flag, once the player is free
        command->op = OP_GIRL;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "nuzlocke") == 0 && count >= 2) {
        // nuzlocke simple|normal|hard|off, nuzlocke crit on|off, nuzlocke state: as Settings > Gameplay > Nuzlocke
        // mode will (the warning taken as accepted). Once the player is free.
        static const char *const kLevels[] = { "off", "simple", "normal", "hard" };
        command->op = OP_NUZLOCKE;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        for (int i = 0; i < NUZLOCKE_LEVEL_COUNT; i++) {
            if (strcmp(words[1], kLevels[i]) == 0) {
                command->x = i;
                return TRUE;
            }
        }
        if (strcmp(words[1], "crit") == 0 && count >= 3) {
            command->x = strcmp(words[2], "on") == 0 ? 11 : 10;
            return strcmp(words[2], "on") == 0 || strcmp(words[2], "off") == 0;
        }
        command->x = 20;
        return strcmp(words[1], "state") == 0;
    } else if (strcmp(words[0], "flag") == 0 && count >= 2) {
        // flag N: sets script flag N (include/constants/flags.h) once the player is free, for whatever it unlocks
        // (an app on the start menu, a Pokédex or Pokégear flag...)
        command->op = OP_FLAG;
        command->x = atoi(words[1]);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "give") == 0 && count >= 3) {
        // give SPECIES LEVEL: a new Pokémon into the party, the player's own (as a gift is)
        command->op = OP_GIVE;
        command->x = atoi(words[1]);
        command->y = atoi(words[2]);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "wild") == 0 && count >= 3) {
        // wild SPECIES LEVEL [gen5]: once the player is free, a wild battle as tall grass starts it (in BW2's engine
        // with gen5, else as the setting says)
        command->op = OP_WILD;
        command->x = atoi(words[1]);
        command->y = atoi(words[2]);
        command->frames = count >= 4 && strcmp(words[3], "gen5") == 0;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "safari") == 0 && count >= 3) {
        // safari SPECIES LEVEL [BALLS]: once the player is free, a Safari Zone battle (30 balls unless given), as its
        // grass starts one; in BW2's engine when the setting is on
        command->op = OP_WILD;
        command->map = 2;
        command->x = atoi(words[1]);
        command->y = atoi(words[2]);
        command->frames = count >= 4 ? atoi(words[3]) : 30;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "trainer") == 0 && count >= 2) {
        // trainer ID [ID2]: once the player is free, a battle against trainer ID (and ID2, two trainers), as a script
        // starts it (in BW2's engine when the setting is on: "gen5 on")
        command->op = OP_TRAINER;
        command->x = atoi(words[1]);
        command->y = count >= 3 ? atoi(words[2]) : 0;
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if ((strcmp(words[0], "shot") == 0 || strcmp(words[0], "stillshot") == 0) && count >= 2) {
        command->op = OP_SHOT;
        command->frames = strcmp(words[0], "stillshot") == 0;
        snprintf(command->text, sizeof(command->text), "%s", words[1]);
        return TRUE;
    } else if (strcmp(words[0], "textwidths") == 0 && count >= 3) {
        // textwidths FILE MAX: every line of message bank FILE (files/msgdata/msg/msg_FILE_...) measured in the field's
        // message font; prints the widest and fails if a line is wider than MAX pixels. Buffers (names, items) count 0.
        command->op = OP_TEXT_WIDTHS;
        command->x = atoi(words[1]);
        command->y = atoi(words[2]);
        return TRUE;
    } else if ((strcmp(words[0], "editor") == 0 || strcmp(words[0], "debug") == 0) && rest != NULL) {
        // editor WHAT ..., debug WHAT ...: once the player is free, the save editor's or the debug window's function
        // (lib/port/test_editor.c); the test fails if it didn't do what it says
        command->op = OP_EDITOR;
        command->x = strcmp(words[0], "debug") == 0;
        snprintf(command->text, sizeof(command->text), "%s", rest);
        command->condition = COND_FREE;
        command->timeout = DEFAULT_TIMEOUT;
        return TRUE;
    } else if (strcmp(words[0], "log") == 0 && rest != NULL) {
        command->op = OP_LOG;
        snprintf(command->text, sizeof(command->text), "%s", rest);
        return TRUE;
    } else if (strcmp(words[0], "exit") == 0) {
        command->op = OP_EXIT;
        command->frames = count >= 2 ? atoi(words[1]) : 0;
        return TRUE;
    }
    return FALSE;
}

static void Load(void) {
    sTest.loaded = TRUE;
    const char *path = getenv("POKEHEARTGOLD_TEST_SCRIPT");
    if (path == NULL || path[0] == '\0') {
        return;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        printf("[test] FAIL can't open %s\n", path);
        TestScript_ExitExpected();
        exit(1);
    }

    char line[256];
    int number = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        number++;
        line[strcspn(line, "\r\n")] = '\0';
        char *text = line + strspn(line, " \t");
        if (text[0] == '\0' || text[0] == '#') {
            continue;
        }
        if (sTest.count == MAX_COMMANDS || !ParseLine(text, number, &sTest.commands[sTest.count])) {
            printf("[test] FAIL %s:%d: can't read \"%s\"\n", path, number, text);
            TestScript_ExitExpected();
            exit(1);
        }
        sTest.count++;
    }
    fclose(file);
    sTest.active = TRUE;
    TestScript_InstallCrashHandlers();
    printf("[test] running %s (%d commands)\n", path, sTest.count);
    fflush(stdout);
}

static BOOL ConditionMet(const TestCommand *command) {
    if (command->condition < TEST_EVENT_COUNT) {
        // An event counts once: since the last wait for something ended
        return sTest.eventFrames[command->condition] > sTest.lastWaitEnd;
    }
    if (command->condition == COND_FREE) {
        return sTest.freeFrames >= FREE_FRAMES;
    }
    FieldSystem *fieldSystem = FieldSystem_PortGet();
    return sTest.freeFrames >= FREE_FRAMES && fieldSystem->location->mapId == command->map;
}

// A wait times out after its frames, and as long in real time as they take at the DS's 30 a second: scenes that wait
// for music or a sound effect to end wait in real time (libntr plays them from its audio callback), while test mode
// runs frames as fast as it can
static BOOL TimedOut(int frames, int limit, u64 sinceMs) {
    return frames >= limit && NowMs() - sinceMs >= (u64)limit * 1000 / 30;
}

// A shot's pause: the game runs at about its real speed and shows every frame, so that the window has the frame the
// script is at when the harness takes it (test mode shows one every TestModePresentMs and runs thousands between)
static BOOL Paused(void) {
    SIM_config_type *config = SIM_GetConfigPtr();
    if (NowMs() < sTest.pauseUntil) {
        if (!sTest.pausing) {
            sTest.pausing = TRUE;
            sTest.presentMs = config->testModePresentMs;
            config->testModePresentMs = 1;
        }
        usleep(16000);
        return TRUE;
    }
    if (sTest.pausing) {
        sTest.pausing = FALSE;
        config->testModePresentMs = sTest.presentMs;
    }
    return FALSE;
}

static void Next(void) {
    sTest.current++;
    sTest.started = FALSE;
}

// Runs the script for this frame: the keys to hold
static u16 Step(void) {
    sTest.touching = FALSE;
    if (sTest.failed) {
        if (!Paused()) {
            TestScript_ExitExpected();
            exit(1);
        }
        return 0;
    }
    if (Paused()) {
        return 0;
    }

    if (PlayerIsFree()) {
        if (++sTest.freeFrames == FREE_FRAMES) {
            PrintPlace("free on");
        }
    } else {
        sTest.freeFrames = 0;
    }

    while (sTest.current < sTest.count) {
        TestCommand *command = &sTest.commands[sTest.current];
        if (!sTest.started) {
            sTest.started = TRUE;
            sTest.commandStart = sTest.frame;
            sTest.commandStartMs = NowMs();
            if (command->op != OP_LOG && command->op != OP_SHOT) {
                printf("[test] %d (frame %u)\n", command->line, (unsigned)sTest.frame);
            }
        }
        const int elapsed = (int)(sTest.frame - sTest.commandStart);

        switch (command->op) {
        case OP_PRESS:
            // Held for its frames, then let go for two, so that the next press is a new one
            if (elapsed < command->frames) {
                return command->keys;
            }
            if (elapsed < command->frames + 2) {
                return 0;
            }
            Next();
            continue;
        case OP_HOLD:
            if (elapsed < command->frames) {
                return command->keys;
            }
            Next();
            continue;
        case OP_WAIT:
            if (elapsed < command->frames) {
                return 0;
            }
            Next();
            continue;
        case OP_TOUCH:
            if (elapsed < command->frames) {
                sTest.touching = TRUE;
                sTest.touchX = (u16)command->x;
                sTest.touchY = (u16)command->y;
                return 0;
            }
            if (elapsed < command->frames + 3) {
                return 0;
            }
            Next();
            continue;
        case OP_WALK: {
            // From where the player is free: held until it has moved the tiles, or a door or warp changed the map
            FieldSystem *fieldSystem = FieldSystem_PortGet();
            if (elapsed == 0 || sTest.walkMap < 0) {
                if (sTest.freeFrames < FREE_FRAMES) {
                    // A message in the way (someone talking after a step): A until the player is free
                    if (TimedOut(elapsed, DEFAULT_TIMEOUT, sTest.commandStartMs)) {
                        Fail(command, "the player was never free to walk");
                    }
                    sTest.walkMap = -1;
                    return elapsed % 8 < 2 ? PAD_BUTTON_A : 0;
                }
                sTest.commandStart = sTest.frame; // The walk's own timeout starts once it walks
                sTest.commandStartMs = NowMs();
                sTest.walkMap = fieldSystem->location->mapId;
                sTest.walkX = sTest.walkLastX = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
                sTest.walkZ = sTest.walkLastZ = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
                sTest.walkStuck = 0;
                sTest.walkStuckMs = NowMs();
            }
            if (fieldSystem == NULL || fieldSystem->location == NULL || fieldSystem->playerAvatar == NULL) {
                // Off the field (a battle the step started...): on to the next command
                sTest.freeFrames = 0;
                Next();
                return 0;
            }
            const int x = (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
            const int z = (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
            const int moved = abs(x - sTest.walkX) + abs(z - sTest.walkZ);
            if (moved >= command->frames || fieldSystem->location->mapId != sTest.walkMap) {
                // The step may start an event a frame later: free counts again from here
                sTest.freeFrames = 0;
                Next();
                return 0;
            }
            if (!PlayerIsFree()) {
                // A scene the step started (someone stops the player...): A through it, then on
                return elapsed % 8 < 2 ? PAD_BUTTON_A : 0;
            }
            if (x != sTest.walkLastX || z != sTest.walkLastZ) {
                sTest.walkLastX = x;
                sTest.walkLastZ = z;
                sTest.walkStuck = 0;
                sTest.walkStuckMs = NowMs();
            } else if (TimedOut(++sTest.walkStuck, WALK_TIMEOUT, sTest.walkStuckMs)) {
                Fail(command, "the walk is blocked");
                return 0;
            }
            return command->keys;
        }
        case OP_UNTIL:
        case OP_MASH:
        case OP_PACE:
            if (command->condition == COND_NONE && elapsed >= command->timeout) {
                Next();
                continue;
            }
            if (command->condition != COND_NONE && ConditionMet(command)) {
                printf("[test] %s after %d frames\n", sConditionNames[command->condition], elapsed);
                sTest.lastWaitEnd = sTest.frame;
                Next();
                continue;
            }
            if (command->condition != COND_NONE && TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                char why[64];
                snprintf(why, sizeof(why), "no %s in %d frames", sConditionNames[command->condition], elapsed);
                Fail(command, why);
                return 0;
            }
            if (command->op == OP_PACE) {
                // About two tiles each way (a step takes 8 frames)
                return PlayerIsFree() ? ((elapsed / 16) % 2 ? command->keys2 : command->keys) : 0;
            }
            // Mashing: pressed (or touched) for 2 frames out of 8. Waiting for the player to be free, only while
            // not: pressed on a free frame, A would talk to whatever is in front and start it all over again
            {
                const BOOL waitsForFree = command->condition == COND_FREE || command->condition == COND_MAP;
                if (command->op == OP_MASH && elapsed % 8 < 2 && !(waitsForFree && PlayerIsFree())) {
                    if (command->touch) {
                        sTest.touching = TRUE;
                        sTest.touchX = (u16)command->x;
                        sTest.touchY = (u16)command->y;
                    }
                    return command->keys;
                }
            }
            return 0;
        case OP_SAVE:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free to save");
                }
                return 0;
            }
            if (!Field_SaveGameNormal(FieldSystem_PortGet())) {
                Fail(command, "the save failed");
                return 0;
            }
            printf("[test] saved\n");
            Next();
            continue;
        case OP_GEN5_IMPORT: {
#ifdef GEN5_BATTLES
            if (elapsed == 0) {
                const char *rom = getenv("POKEHEARTGOLD_GEN5_ROM");
                if (rom == NULL || rom[0] == '\0') {
                    Fail(command, "no POKEHEARTGOLD_GEN5_ROM to import");
                    return 0;
                }
                Gen5Rom_Import(rom, FALSE);
                return 0;
            }
            Gen5Rom_Update();
            const enum Gen5RomState state = Gen5Rom_State();
            if (state == GEN5_ROM_IMPORTING) {
                if (elapsed % 300 == 0) {
                    printf("[test] %s\n", Gen5Rom_Status());
                }
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the import is taking too long");
                }
                return 0;
            }
            printf("[test] gen5: %s\n", Gen5Rom_Status());
            if (state != GEN5_ROM_READY) {
                Fail(command, "the import failed");
                return 0;
            }
#else
            Fail(command, "built without the Gen 5 code (-Dpokebw2)");
            return 0;
#endif
            Next();
            continue;
        }
        case OP_SLOTS_IMPORT: {
            if (elapsed == 0) {
                const char *rom = getenv("POKEHEARTGOLD_SLOTS_ROM");
                if (rom == NULL || rom[0] == '\0') {
                    Fail(command, "no POKEHEARTGOLD_SLOTS_ROM to import");
                    return 0;
                }
                SlotsRom_Import(rom, FALSE);
                return 0;
            }
            SlotsRom_Update();
            if (SlotsRom_State() == SLOTS_ROM_IMPORTING) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the import is taking too long");
                }
                return 0;
            }
            printf("[test] slots: %s\n", SlotsRom_Status());
            if (SlotsRom_State() != SLOTS_ROM_READY) {
                Fail(command, "the import failed");
                return 0;
            }
            Next();
            continue;
        }
        case OP_SLOTS_SETTING:
            GameSettings_Get()->japaneseGameCorner = command->x;
            printf("[test] Japanese Game Corner %s\n", command->x ? "on" : "off");
            Next();
            continue;
        case OP_WARP:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for the warp");
                }
                return 0;
            }
            sTestWarp.map = command->map;
            sTestWarp.x = command->x;
            sTestWarp.z = command->y;
            FieldSystem_CreateTask(FieldSystem_PortGet(), Task_TestWarp, NULL);
            printf("[test] warp to map %d at (%d, %d)\n", command->map, command->x, command->y);
            sTest.freeFrames = 0;
            Next();
            return 0;
        case OP_COINS:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for the coins");
                }
                return 0;
            }
            *Save_PlayerData_GetCoinsAddr(FieldSystem_PortGet()->saveData) = (u16)command->x;
            printf("[test] %d coins\n", command->x);
            Next();
            continue;
        case OP_CHECK: {
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for the check");
                }
                return 0;
            }
            SaveData *saveData = FieldSystem_PortGet()->saveData;
            static const char *const kWhat[] = { "coins", "Pokémon in the party", "of the item" };
            int value = command->y == 0 ? *Save_PlayerData_GetCoinsAddr(saveData)
                : command->y == 1       ? Party_GetCount(SaveArray_Party_Get(saveData))
                                        : Bag_GetQuantity(Save_Bag_Get(saveData), (u16)command->frames, HEAP_ID_FIELD1);
            printf("[test] %d %s\n", value, kWhat[command->y]);
            if (value != command->x) {
                char message[96];
                snprintf(message, sizeof(message), "%d %s, not %d", value, kWhat[command->y], command->x);
                Fail(command, message);
                return 0;
            }
            Next();
            continue;
        }
        case OP_CASINO:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for the casino");
                }
                return 0;
            }
            printf("[test] casino: slots %s\n", SlotApp_IsEnabled() ? "on" : "off");
            if (SlotApp_IsEnabled()) {
                if (command->x) {
                    SlotApp_TestStartInBonus();
                }
                FieldSystem_CreateTask(FieldSystem_PortGet(), Task_TestCasino, NULL);
            } else {
                Fail(command, "the Japanese slot machine isn't enabled");
                return 0;
            }
            sTest.freeFrames = 0;
            Next();
            return 0;
        case OP_GEN5_SETTING:
            GameSettings_Get()->gen5Battles = command->x;
            printf("[test] Gen 5 battles %s\n", command->x ? "on" : "off");
            Next();
            continue;
        case OP_GIRL: {
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free to be made a girl");
                }
                return 0;
            }
            PlayerProfile_SetTrainerGender(Save_PlayerData_GetProfile(FieldSystem_PortGet()->saveData), 1);
            printf("[test] the player is a girl\n");
            Next();
            continue;
        }
        case OP_FLAG: {
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for a flag to be set");
                }
                return 0;
            }
            Save_VarsFlags_SetFlagInArray(Save_VarsFlags_Get(FieldSystem_PortGet()->saveData), (u16)command->x);
            printf("[test] flag %d set\n", command->x);
            Next();
            continue;
        }
        case OP_NUZLOCKE:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free");
                }
                return 0;
            }
            // Requests are applied by NuzlockeMode_Update, at the end of this frame
            if (command->x < NUZLOCKE_LEVEL_COUNT) {
                NuzlockeMode_RequestLevel(command->x);
            } else if (command->x < 20) {
                NuzlockeMode_RequestCritGuard(command->x == 11);
            } else {
                u16 areaSpecies = 0;
                FieldSystem *fieldSystem = FieldSystem_PortGet();
                const u16 location = fieldSystem != NULL ? (u16)MapHeader_GetMapSec(fieldSystem->location->mapId) : 0xFFFF;
                const int area = NuzlockeMode_AreaState(location, &areaSpecies);
                printf("[test] nuzlocke: %s%s%s%s; area %u %s %u; cap %d\n", NuzlockeMode_LevelName(NuzlockeMode_Level()),
                    NuzlockeMode_CritGuard() ? ", crit guard" : "", NuzlockeMode_WasDropped() ? ", dropped" : "",
                    NuzlockeMode_BallsReceived() ? ", has had balls" : "", location,
                    area == NUZLOCKE_AREA_CAUGHT ? "caught" : area == NUZLOCKE_AREA_MISSED ? "missed" : "open",
                    areaSpecies, NuzlockeMode_LevelCap());
            }
            Next();
            continue;
        case OP_GIVE: {
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free to be given a Pokemon");
                }
                return 0;
            }
            FieldSystem *fieldSystem = FieldSystem_PortGet();
            Pokemon *mon = AllocMonZeroed(HEAP_ID_FIELD1);
            // The player's own, as a gift: their ID, name and gender as its OT (BW2's battles make a Pokémon of
            // another OT disobey above the badges' level)
            PlayerProfile *profile = Save_PlayerData_GetProfile(fieldSystem->saveData);
            CreateMon(mon, command->x, command->y, 32, FALSE, 0, OT_ID_PLAYER_ID, PlayerProfile_GetTrainerID(profile));
            SetMonData(mon, MON_DATA_OT_NAME, (void *)PlayerProfile_GetNamePtr(profile));
            u32 otGender = PlayerProfile_GetTrainerGender(profile);
            SetMonData(mon, MON_DATA_OT_GENDER, &otGender);
            Party *party = SaveArray_Party_Get(fieldSystem->saveData);
            const BOOL added = Party_AddMon(party, mon);
            Heap_Free(mon);
            if (!added) {
                Fail(command, "the party is full");
                return 0;
            }
            printf("[test] gave species %d at level %d (party of %d)\n", command->x, command->y, Party_GetCount(party));
            Next();
            continue;
        }
        case OP_WILD:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for a wild battle");
                }
                return 0;
            }
#ifdef GEN5_BATTLES
            if (!(command->map == 2 ? Gen5Battle_StartSafari(command->x, command->y, command->frames)
                                    : Gen5Battle_StartWild(command->x, command->y, command->frames))) {
                Fail(command, "the wild battle couldn't start");
                return 0;
            }
#else
            Fail(command, "built without the Gen 5 code (-Dpokebw2)");
            return 0;
#endif
            sTest.freeFrames = 0;
            Next();
            return 0;
        case OP_TRAINER:
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for a trainer battle");
                }
                return 0;
            }
#ifdef GEN5_BATTLES
            if (!Gen5Battle_StartTrainer(command->x, command->y)) {
                Fail(command, "the trainer battle couldn't start");
                return 0;
            }
#else
            Fail(command, "built without the Gen 5 code (-Dpokebw2)");
            return 0;
#endif
            sTest.freeFrames = 0;
            Next();
            return 0;
        case OP_SHOT:
            if (command->frames == 1) {
                // stillshot: every frame is shown for three frames, then the game stops on the one just drawn for as
                // long as the shot takes
                static u32 sStillPresentMs;
                SIM_config_type *config = SIM_GetConfigPtr();
                if (elapsed == 0) {
                    sStillPresentMs = config->testModePresentMs;
                    config->testModePresentMs = 1;
                }
                if (elapsed < 3) {
                    return 0;
                }
                printf("[test] shot %s\n", command->text);
                fflush(stdout);
                usleep(SHOT_PAUSE_MS * 1000);
                config->testModePresentMs = sStillPresentMs;
                Next();
                return 0;
            }
            // The harness takes the screenshot while the game runs on without input
            printf("[test] shot %s\n", command->text);
            fflush(stdout);
            sTest.pauseUntil = NowMs() + SHOT_PAUSE_MS;
            Next();
            return 0;
        case OP_TEXT_WIDTHS: {
            MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, NARC_msgdata_msg, command->x, HEAP_ID_FIELD1);
            static u16 text[1024], line[1024];
            u32 widest = 0, over = 0;
            for (u32 row = 0; row < MsgDataGetCount(msgData); row++) {
                ReadMsgDataIntoU16Array(msgData, row, text);
                u32 n = 0, part = 0;
                for (u32 i = 0;; i++) {
                    const u16 c = text[i];
                    if (c == EOS || c == CHAR_LF || c == 0x25BC || c == 0x25BD) {
                        line[n] = EOS;
                        const u32 width = n == 0 ? 0 : FontID_FlatArray_GetWidth(1, line, 0);
                        widest = width > widest ? width : widest;
                        if (width > (u32)command->y) {
                            printf("[test] msg %d row %u line %u: %u px\n", command->x, row, part, width);
                            over++;
                        }
                        if (c == EOS) {
                            break;
                        }
                        n = 0;
                        part++;
                    } else if (c == 0xFFFE) {
                        // A control code: copied whole (the width skips it)
                        const u32 length = 3 + text[i + 2];
                        for (u32 k = 0; k < length; k++) {
                            line[n++] = text[i + k];
                        }
                        i += length - 1;
                    } else {
                        line[n++] = c;
                    }
                }
            }
            DestroyMsgData(msgData);
            printf("[test] msg %d: widest line %u px, %u over %d\n", command->x, widest, over, command->y);
            if (over > 0) {
                Fail(command, "lines too wide");
                return 0;
            }
            Next();
            continue;
        }
        case OP_EDITOR: {
            if (!ConditionMet(command)) {
                if (TimedOut(elapsed, command->timeout, sTest.commandStartMs)) {
                    Fail(command, "the player was never free for the command");
                }
                return 0;
            }
            BOOL startedTask = FALSE;
            printf("[test] %s %s\n", command->x ? "debug" : "editor", command->text);
            const char *error = TestEditor_Run(command->x, command->text, &startedTask);
            if (error != NULL) {
                Fail(command, error);
                return 0;
            }
            Next();
            if (startedTask) {
                sTest.freeFrames = 0;
                return 0;
            }
            continue;
        }
        case OP_LOG:
            printf("[test] %s\n", command->text);
            Next();
            continue;
        case OP_EXIT:
            printf("[test] %s\n", command->frames == 0 ? "PASS" : "exit");
            fflush(stdout);
            TestScript_ExitExpected();
            exit(command->frames);
        }
    }

    printf("[test] PASS (end of script)\n");
    fflush(stdout);
    TestScript_ExitExpected();
    exit(0);
}

u16 TestScript_Keys(void) {
    if (!sTest.loaded) {
        Load();
    }
    if (!sTest.active) {
        return 0;
    }
    sTest.frame++;
    if (sTest.frame % 120 == 0 && sTest.current < sTest.count) {
        printf("[test] frame %u at %.1f s: line %d, free for %d frames\n", (unsigned)sTest.frame,
            NowMs() / 1000.0, sTest.commands[sTest.current].line, sTest.freeFrames);
    }
    const u16 keys = Step();
    fflush(stdout);
    return keys;
}

BOOL TestScript_Touch(u16 *x, u16 *y) {
    if (!sTest.active || !sTest.touching) {
        return FALSE;
    }
    *x = sTest.touchX;
    *y = sTest.touchY;
    return TRUE;
}

void TestScript_OnEvent(enum TestScriptEvent event) {
    if (!sTest.active) {
        return;
    }
    sTest.eventFrames[event] = sTest.frame + 1;
    printf("[test] event %s\n", sConditionNames[event]);
}
