# Scripted tests (teton-dev fork)

The desktop build plays a script of keys and touches and checks that the game gets where it should. The runner is
the Platinum fork's (`teton-dev/pokeplatinum-thor-ls`, `tests/README.md` there) with the commands that don't reach
into the game's internals, on HGSS's hooks (`lib/port/test_script.c`, `include/port_sdk/port_test_script.h`). The
scripts run in Docker (`tools/docker/desktop.sh`, [tools/docker/README.md](../tools/docker/README.md)), as fast as
the game goes (libntr's test mode), in a data folder of their own, and need the ROM extracted once
(`desktop.sh start`).

```sh
tools/docker/desktop.sh test tests/scripts/new_game.txt --keep new-game
```

| Script | From | Checks | Time |
|---|---|---|---|
| `new_game.txt` | nothing | a new game (past the info topics, a boy named ETHAN, Prof. Oak's introduction) to the bedroom in New Bark Town, saved there | ~1 min |

## Running

`desktop.sh test SCRIPT [--from FIXTURE] [--keep FIXTURE]` (`GAME=ss` for SoulSilver):

- A run's data folder is `~/thor-dev/test-run-hg`: the extracted ROM linked from `~/thor-dev/desktop-hg`, no saves,
  and the default settings, with the 3D at the DS's own size.
- `TEST_ID=name` gives the run a container and a data folder of its own; `TEST_CONFIG='Key=value ...'` adds libntr
  settings to its `sim_config.ini`.
- `--from FIXTURE` starts it from `~/thor-dev/fixtures/hg/FIXTURE/save.bin`; `--keep FIXTURE` keeps the save it ends
  with there, when it passes. Fixtures are saves: never commit them.
- It prints the script's progress (`[test] ...`): each time the player is free, the map and tile
  (`free on map 64 at (6, 6)`), and the screenshots. They and the whole log are in `~/thor-dev/test-shots/hg/SCRIPT/`;
  a failure takes `failure.png`.
- It exits 0 when the script passes and 1 when it fails: a wait that times out, a walk that is blocked, or a run
  longer than `TEST_LIMIT` seconds (600).
- When the game crashes, quits by itself, or is still running at the limit, the run prints where, as functions and
  lines (`crash.txt`), from `lib/port/test_crash.c`'s backtraces (gdb can't run under Rosetta).

## Writing scripts

One command a line; `#` starts a comment. The game reads the script from `POKEHEARTGOLD_TEST_SCRIPT` and plays it
frame by frame, so a script does the same every time.

| Command | Does |
|---|---|
| `press KEYS [frames]` | holds the keys (2 frames), then lets go for 2. Keys: `A B X Y L R START SELECT UP DOWN LEFT RIGHT`, joined with `+` |
| `hold KEYS frames` | holds them |
| `wait frames` | waits |
| `touch X Y [frames]` | touches the touch screen at (X, Y), in its 256×192 pixels (3 frames) |
| `walk DIR [tiles]` | walks the tiles once the player is free; ends early when the map changes (doors, stairs). A through any scene it starts on the way |
| `until EVENT [timeout N]` | waits for an event |
| `mash KEYS [touch X Y] until EVENT [timeout N]` | presses (and touches) 2 frames out of 8 until the event |
| `mash KEYS [touch X Y] for N` | the same for N frames |
| `pace DIR DIR until EVENT [timeout N]` | walks one way and back, about two tiles each, while the player is free: tall grass until `battle` |
| `save` | saves the game (`Field_SaveGameNormal`, as the save menu does) once the player is free |
| `shot NAME` | a screenshot, `NAME.png` (the game runs on while it is taken) |
| `stillshot NAME` | the same with the game stopped on the frame just drawn |
| `log TEXT` | prints the text |
| `exit [code]` | ends the run: 0 passes |

Events:

| Event | When |
|---|---|
| `free` | the player is free on the overworld (the field map running, no application over it, no field task: scripts and events run as tasks) for 20 frames |
| `map N` | free on map N (`fieldSystem->location->mapId`) |
| `menu` | a menu, list or yes/no opened (`ListMenuInit`, `Create2dMenu`, `YesNoPrompt_Create`) |
| `naming` | a naming screen opened |
| `battle` | a battle started (`Battle_Init`) |
| `command` | a battle asks the player what to do (`Task_PlayerSelectCommand`) |

An event counts once, from where the last `until` or `mash ... until` ended. `mash A until free` presses A only while
the player isn't free. Timeouts are 3000 frames by default, and as long again in real time.

Tips:

- The screenshots are of the whole 1280×800 display: the window shows the top screen above the touch screen, each
  at twice the DS's size, from x = 384; the touch screen's (X, Y) is ((x − 384) / 2, (y − 400) / 2).
- The game starts on the info topics; A walks into them, *No info needed* is the touch at (128, 160).
- The naming screen's keyboard: A at (34, 99), 16 pixels a column, 19 a row; START is OK.
