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
| `gen5_rom.txt` | nothing | imports the Black 2 or White 2 ROM (`GEN5_ROM`, or `~/thor-dev/PKMNB2.nds`) as Settings > Gen 5 battles does; BW2's own archive code reads it back. The files are kept in the data folder for the other Gen 5 scripts (`-Dpokebw2` builds) | ~30 s |
| `gen5_wild.txt` | `new-game` | a Cyndaquil given, then a wild Rattata in BW2's engine (as the settings' button starts it): its command and move menus, RUN, back on the overworld with the party as the battle left it (`-Dpokebw2` builds, after `gen5_rom.txt`) | ~1 min |
| `gen5_trainer.txt` | `new-game` | a level 50 Cyndaquil against a Team Rocket Grunt in BW2's engine (Rocket's arena, HGSS's Grunt sprite), A mashed until it is won | ~5 min |
| `gen5_rival.txt` | `new-game` | out to New Bark Town, then Silver (trainer 1) in BW2's engine, in the grass field with a dirt path (ArenaFromSetup's town case), A mashed until it is won | ~5 min |
| `battle_particles.txt` | `new-game` | a Cyndaquil given, a wild Rattata in HGSS's own engine, Tackle: screenshots every 2 frames through its animation (the impact burst and dust are SPL particles) | ~1 min |
| `starter_balls.txt` | `new-game` | out to Prof. Elm's lab, his introduction, the machine: screenshots of the three Poké Balls and the first one up close (each with one spotlight, not tiled) | ~2 min |
| `widescreen_newbark.txt` | `new-game` | down the stairs and out into New Bark Town, with screenshots there; run it with widescreen on (`TEST_CONFIG`, below) to check the overworld fills the 16:9 window | ~2 min |

## Running

`desktop.sh test SCRIPT [--from FIXTURE] [--keep FIXTURE]` (`GAME=ss` for SoulSilver):

- A run's data folder is `~/thor-dev/test-run-hg`: the extracted ROM linked from `~/thor-dev/desktop-hg`, no saves,
  and the default settings, with the 3D at the DS's own size.
- `TEST_ID=name` gives the run a container and a data folder of its own; `TEST_CONFIG='Key=value ...'` adds libntr
  settings to its `sim_config.ini`. Widescreen: `TEST_CONFIG='SecondaryScreen=true Widescreen=true WindowWidth=768
  WindowHeight=432'` (the bottom screen in its own window, the top one 16:9).
- `TEST_ENV='NAME=value ...'` sets environment variables in the run. `POKEHEARTGOLD_GEN5_ARENA=bg,terrain` puts every
  Gen 5 battle in that BW2 arena: `16,1` is Team Rocket's (`gen5_wild.txt` shows it).
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
| `give SPECIES LEVEL` | once the player is free, a new Pokémon into the party, the player's own |
| `wild SPECIES LEVEL [gen5]` | once the player is free, a wild battle as tall grass starts it; with `gen5` in BW2's engine, else as the setting says (`-Dpokebw2` builds) |
| `trainer ID [ID2]` | once the player is free, a battle against trainer ID (and ID2: two trainers; ID twice: its double battle), as a script starts it; in BW2's engine when the setting is on (`gen5 on`; `-Dpokebw2` builds) |
| `gen5 on\|off` | Settings > Gen 5 battles > Gen 5 battles |
| `gen5-import` | imports the Black 2 or White 2 ROM in `POKEHEARTGOLD_GEN5_ROM` (the harness's `GEN5_ROM`) and waits; fails if it is refused or BW2's code can't read it |
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
- Stairs and doors take one more push towards them once the player is on the tile before (the stairs in the
  player's room are left of (3, 4); the house's door is below (3, 10)).
- The game starts on the info topics; A walks into them, *No info needed* is the touch at (128, 160).
- The naming screen's keyboard: A at (34, 99), 16 pixels a column, 19 a row; START is OK.
