# The desktop build in Docker (teton-dev fork)

`tools/docker/desktop.sh` builds the Linux desktop build in Docker on a Mac and runs it on a virtual display that
can be watched and played over VNC, with commands to take screenshots and press keys so that an agent can check
things on screen. The container is arm64 Linux, native on Apple Silicon (no Rosetta). The APK is built on macOS
itself, with no container.

## Setup

- Docker Desktop. The image (`Dockerfile.desktop`: Ubuntu 24.04 and gcc 13, as in `build_port.yml`) is built on
  first use.
- The ROMs in `~/thor-dev` (or `THOR_DEV=...`): `Pokemon - HeartGold Version (USA).nds`, and for `GAME=ss`
  `Pokemon - SoulSilver Version (USA).nds` (or `ROM=...`). They stay there; nothing from them goes into git.

## Use

```sh
tools/docker/desktop.sh build      # meson setup build_linux (first time), then ninja main: ~2 min from scratch
tools/docker/desktop.sh start      # open vnc://localhost:5901 to watch and play
tools/docker/desktop.sh key Return z z
tools/docker/desktop.sh shot       # ~/thor-dev/shots/hgss.png
tools/docker/desktop.sh stop
```

Keys (the port's defaults): A = `z`, B = `x`, X = `c`, Y = `v`, L = `q`, R = `e`, Start = `Return`, the arrows. The
window shows the top screen above the touch screen; `click X Y` touches it.

The data folder is `~/thor-dev/desktop-hg` (`desktop-ss` for SoulSilver): the extracted ROM, `save.bin`,
`sim_config.ini` and `sim_config_prj.ini`. On the first `start` the script copies in `firmware.bin` (the port's DS
Wi-Fi settings block, `res/pc_port/dwc_nv.bin`, which the APK ships too: without it the game crashes at boot) and
imports the ROM through the desktop build's file dialog, then restarts the game.

## Scripted tests

`desktop.sh test SCRIPT [--from FIXTURE] [--keep FIXTURE]` plays a script of keys and touches in a fresh data folder
and checks where the game gets: [tests/README.md](../../tests/README.md).

## The APK

`desktop.sh apk` builds it (`tools/android/build_apk.sh`, the dev flavor `app.goldenrod.dev`, "Goldenrod dev", unless
`--stable`) natively on the Mac, no Docker. It needs, set up once:

- from Homebrew: `brew install openjdk@17 meson ninja cmake nasm pkgconf bison python@3.12` (Homebrew's bison and
  Python go ahead of macOS's 2.3 and 3.9 on the script's PATH; the Gen 5 build needs Python 3.11+);
- Gradle 8.10.2 (`gradle-8.10.2-bin.zip` from services.gradle.org, unzipped into `~/Library/Android`; Homebrew's
  Gradle 9 is too new for the Android Gradle plugin 8.7);
- Google's command-line tools for Mac (`commandlinetools-mac_arm64-..._latest.zip`, into
  `~/Library/Android/sdk/cmdline-tools/latest`), then `sdkmanager --licenses` and `sdkmanager "platforms;android-35"
  "build-tools;34.0.0" "ndk;27.2.12479018"` (NDK r27c: its compilers are universal binaries, native on Apple Silicon).

`ANDROID_DIR` (default `~/Library/Android`), `ANDROID_NDK_HOME` and `JAVA_HOME` override where they are. The build is
in `build_android` (SDL2 and enet built there the first time). Its version code is the date and hour
(`VERSION_CODE=...` to choose). `desktop.sh install` puts it on the device adb sees (`ADB=...` if adb isn't on the
PATH or in `~/Documents/platform-tools`). The log is `~/thor-dev/hgss-apk.log`.

## Known problems

- `[assert] GF_ASSERT failed` from `GetOverlayRamBounds` at every overlay load is harmless: asserts only log on the
  port, and the overlay table libntr reads from `header.bin` doesn't match the 64-bit `FSOverlayInfoHeader` (its
  pointers put `file_id` on the ARM9 load address). Overlays are native code on the port. `where ADDR` turns an
  assert's address into a function and line.
- The desktop build is arm64 (`pokeheartgold-desktop`, `--platform linux/arm64`): about 7 times faster than it was
  under Rosetta to build, and tests run at full speed (`new_game.txt` in seconds). meson refuses NASM, which the project
  and libntr declare for the DS build only, on a non-x86 host, so `build_linux` is set up with `linux-arm64.ini`, a
  "cross" file naming the host x86-64 while compiling with the container's own gcc (as `tools/android/build_apk.sh`
  does). gdb works (`desktop.sh shell`, then `gdb build_linux/main`), and `where` takes addresses from arm64's PIE base
  (0xaaaaaaaa0000).
- The APK isn't built in Docker: the NDK's and the SDK's Linux tools are x86-64 only (Rosetta), their macOS ones run
  natively.
