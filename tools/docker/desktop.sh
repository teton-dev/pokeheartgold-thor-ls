#!/bin/bash
# The Linux desktop build in Docker, built and played on a Mac (teton-dev fork). See tools/docker/README.md.
#
#   tools/docker/desktop.sh build [TARGET...]  configure build_linux if needed, then ninja (default: main)
#   tools/docker/desktop.sh start              run the game on a virtual display; watch and play it over VNC
#                                              (the first time, it imports the ROM into the data folder)
#   tools/docker/desktop.sh shot [file.png]    screenshot of the display
#   tools/docker/desktop.sh key KEY...         press keys (xdotool names: z = A, x = B, Return = Start, Up, ...)
#   tools/docker/desktop.sh hold MS KEY        hold a key for MS milliseconds (walking)
#   tools/docker/desktop.sh type TEXT          type text
#   tools/docker/desktop.sh click X Y          left click at display coordinates (the touch screen is the lower half)
#   tools/docker/desktop.sh log [N]            the game's last N lines of output (default 80)
#   tools/docker/desktop.sh where ADDR...      functions and lines for addresses the game printed ([assert] ... at)
#   tools/docker/desktop.sh stop               stop the game and the display
#   tools/docker/desktop.sh shell [CMD...]     a shell (or a command) in the build container
#   tools/docker/desktop.sh test SCRIPT [--from FIXTURE] [--keep FIXTURE]
#                                              run a test script (tests/README.md) in a fresh data folder, from a
#                                              fixture's save, and keep the save it ends with as a fixture
#   tools/docker/desktop.sh apk [--stable]     build the APK (tools/android/build_apk.sh; dev flavor by default)
#   tools/docker/desktop.sh install            install that APK on the device adb sees (ADB=... for adb's path)
#
# GAME=ss runs SoulSilver (its own data folder and ROM). The container is x86-64 and runs under Rosetta on Apple
# Silicon. THOR_DEV (default ~/thor-dev) holds the ROMs and the data folders (desktop-hg, desktop-ss: the extracted
# ROM, saves and settings, kept between runs); the game sees THOR_DEV at the same path as on the Mac. The APK build
# uses the Docker volumes android-ndk (/ndk/android-ndk-r27c) and android-tools (/tools: jdk, gradle-8.10.2, sdk,
# gradle-home), shared with the Platinum fork.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
THOR_DEV=${THOR_DEV:-$HOME/thor-dev}
GAME=${GAME:-hg}
case "$GAME" in
hg) ROM=${ROM:-$THOR_DEV/Pokemon - HeartGold Version (USA).nds} ;;
ss) ROM=${ROM:-$THOR_DEV/Pokemon - SoulSilver Version (USA).nds} ;;
*) echo "GAME is hg or ss"; exit 1 ;;
esac
DATA=${DATA:-$THOR_DEV/desktop-$GAME}
NAME=pokeheartgold-desktop
IMAGE=pokeheartgold-desktop
SCREEN=${SCREEN:-1280x800}
VNC_PORT=${VNC_PORT:-5901} # 5900 is the Platinum fork's
BIN=$ROOT/build_linux/main
# Gen 5 battles: a pokebw2 clone and the Platinum fork (its gen5/, which ./gen5 links to) next to this repository
POKEBW2=${POKEBW2:-$(cd "$ROOT/.." && pwd)/pokebw2}
PLATINUM=${PLATINUM:-$(cd "$ROOT/.." && pwd)/pokeplatinum-thor-ls}

export DOCKER_DEFAULT_PLATFORM=linux/amd64
command -v docker >/dev/null || export PATH="$HOME/.docker/bin:$PATH"

image() {
    if [ -z "$(docker images -q $IMAGE)" ]; then
        docker build -t $IMAGE -f "$ROOT/tools/docker/Dockerfile.desktop" "$ROOT/tools/docker"
    fi
}

mounts() {
    echo "--mount type=bind,src=$ROOT,target=$ROOT"
    GIT_COMMON=$(cd "$ROOT" && cd "$(git rev-parse --git-common-dir)" && pwd)
    case "$GIT_COMMON" in "$ROOT"/*) ;; *) echo "--mount type=bind,src=$GIT_COMMON,target=$GIT_COMMON" ;; esac
    [ -d "$POKEBW2" ] && echo "--mount type=bind,src=$POKEBW2,target=$POKEBW2,readonly"
    [ -d "$PLATINUM/gen5" ] && echo "--mount type=bind,src=$PLATINUM/gen5,target=$PLATINUM/gen5,readonly"
    mkdir -p "$DATA"
    echo "--mount type=bind,src=$THOR_DEV,target=$THOR_DEV"
}

# The Gen 5 code is built when pokebw2 and the Platinum fork's gen5/ are there
gen5_option() {
    if [ -d "$POKEBW2/src/ov167" ] && [ -d "$PLATINUM/gen5" ]; then
        echo "-Dpokebw2=$POKEBW2"
    else
        echo "-Dpokebw2="
    fi
}

in_container() {
    docker run --rm -i --user "$(id -u):$(id -g)" -e HOME=/tmp $(mounts) -w "$ROOT" $IMAGE sh -c "$*"
}

on_display() {
    docker exec -e DISPLAY=:1 $NAME "$@"
}

running() {
    [ "$(docker inspect -f '{{.State.Running}}' $NAME 2>/dev/null)" = true ]
}

# Starts the game in DATA, detached, with the display and VNC
run_game() {
    docker rm -f $NAME >/dev/null 2>&1 || true
    docker run -d --name $NAME --user "$(id -u):$(id -g)" -e HOME=/tmp -p 127.0.0.1:$VNC_PORT:5900 \
        -e DISPLAY=:1 -e SDL_AUDIODRIVER=dummy -e LIBGL_ALWAYS_SOFTWARE=1 $(mounts) -w "$DATA" $IMAGE \
        sh -c "Xvfb :1 -screen 0 ${SCREEN}x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
               sleep 1
               x11vnc -display :1 -forever -shared -nopw -rfbport 5900 -quiet >/tmp/x11vnc.log 2>&1 &
               exec stdbuf -oL -eL '$BIN'" >/dev/null # Line-buffered: nothing lost if it dies
}

# Waits up to $2 seconds for a line matching $1 in the game's output
wait_for_log() {
    for _ in $(seq "$2"); do
        docker logs $NAME 2>&1 | grep -q "$1" && return 0
        running || return 1
        sleep 1
    done
    return 1
}

# The first start: the desktop build asks for the ROM in a file dialog listing the working directory and extracts
# it there. The ROM is copied in, picked (the dialog's only file, double-clicked), and deleted once extracted; the
# game is then restarted, since it set up its file system before the files were there.
import_rom() {
    [ -f "$ROM" ] || { echo "No ROM at $ROM (set ROM=...)"; exit 1; }
    echo "Importing $(basename "$ROM") into $DATA"
    cp "$ROM" "$DATA/rom.nds"
    run_game
    sleep 8
    on_display xdotool mousemove 470 191 click --repeat 2 --delay 120 1
    if ! wait_for_log '^\[I\]  Done' 180; then
        rm -f "$DATA/rom.nds"
        echo "The import didn't finish: see $0 log, and $0 shot for the dialog"
        exit 1
    fi
    rm -f "$DATA/rom.nds"
}

case "$1" in
build)
    image
    shift
    if [ -f "$ROOT/build_linux/build.ninja" ]; then
        in_container meson configure build_linux "$(gen5_option)" >/dev/null
    else
        in_container meson setup build_linux -Dbuild_target=linux -Dlibntr:card_game_code=IPKE,IPGE "$(gen5_option)"
    fi
    in_container ninja -C build_linux "${@:-main}" 2>&1 | tee "$ROOT/build_linux/desktop-build.log"
    exit "${PIPESTATUS[0]}"
    ;;
start)
    image
    [ -x "$BIN" ] || { echo "No build: run $0 build first"; exit 1; }
    mkdir -p "$DATA"
    # The DS Wi-Fi settings block the port reads at boot (the APK ships the same file); without it the game crashes
    [ -f "$DATA/firmware.bin" ] || cp "$ROOT/res/pc_port/dwc_nv.bin" "$DATA/firmware.bin"
    [ -f "$DATA/header.bin" ] || import_rom
    run_game
    echo "Running. Watch and play: open vnc://localhost:$VNC_PORT (Finder > Go > Connect to Server, no password)."
    ;;
shot)
    OUT=${2:-$THOR_DEV/shots/hgss.png}
    mkdir -p "$(dirname "$OUT")"
    on_display import -window root /tmp/shot.png
    docker cp $NAME:/tmp/shot.png "$OUT" >/dev/null
    echo "$OUT"
    ;;
key)
    shift
    # SDL ignores xdotool's zero-length presses: each key is held for a few frames
    for k in "$@"; do
        on_display sh -c "xdotool keydown '$k'; sleep 0.12; xdotool keyup '$k'; sleep 0.12"
    done
    ;;
hold)
    on_display sh -c "xdotool keydown '$3'; sleep $(awk "BEGIN{print $2/1000}"); xdotool keyup '$3'"
    ;;
type)
    shift
    on_display xdotool type --delay 40 "$*"
    ;;
click)
    on_display xdotool mousemove "$2" "$3" click 1
    ;;
log)
    docker logs $NAME 2>&1 | tail -n "${2:-80}"
    ;;
where)
    shift
    image
    # The game runs at the fixed base Linux gives a PIE without ASLR in Docker
    for a in "$@"; do
        printf '%s: ' "$a"
        in_container addr2line -f -C -i -p -e "$BIN" "$(printf '0x%x' $((a - 0x555555554000)))" | sed "s#$ROOT/build_linux/../##"
    done
    ;;
test)
    image
    shift
    SCRIPT=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
    shift
    [ -f "$SCRIPT" ] || { echo "No script $SCRIPT"; exit 1; }
    FROM= KEEP=
    while [ $# -gt 0 ]; do
        case "$1" in
        --from) FROM=$2; shift 2 ;;
        --keep) KEEP=$2; shift 2 ;;
        *) echo "Unknown option $1"; exit 1 ;;
        esac
    done
    [ -x "$BIN" ] || { echo "No build: run $0 build first"; exit 1; }
    [ -f "$DATA/header.bin" ] || { echo "No extracted ROM in $DATA: start the game once ($0 start)"; exit 1; }
    TEST_NAME=$(basename "$SCRIPT" .txt)
    # Kept apart from the Platinum fork's runs and fixtures, which share THOR_DEV
    RUN=$THOR_DEV/test-run-$GAME${TEST_ID:+-$TEST_ID}
    SHOTS=$THOR_DEV/test-shots/$GAME/$TEST_NAME
    FIXTURES=$THOR_DEV/fixtures/$GAME
    rm -rf "$RUN" "$SHOTS"
    mkdir -p "$RUN" "$SHOTS"
    # The extracted ROM, linked; no saves, default settings, the 3D at the DS's own size (software 3D is slow)
    for entry in "$DATA"/*; do
        name=$(basename "$entry")
        case "$name" in
        save.bin | save_slot* | *.sav | DbgLog.txt | *.nds | sim_config.ini | sim_config_prj.ini | imgui.ini) ;;
        carts.ini | cart.name) cp "$entry" "$RUN/" ;;
        *) ln -s "$entry" "$RUN/$name" ;;
        esac
    done
    [ -e "$RUN/firmware.bin" ] || cp "$ROOT/res/pc_port/dwc_nv.bin" "$RUN/firmware.bin"
    # Gen 5 battles: the BW2 ROM a test's gen5-import imports (GEN5_ROM, or Black 2 / White 2 in THOR_DEV); its files
    # are kept in the data folder's gen5/ and linked into the next runs
    GEN5_ROM=${GEN5_ROM:-$(ls "$THOR_DEV"/PKMNB2.nds "$THOR_DEV"/PKMNW2.nds 2>/dev/null | head -n 1)}
    printf '[General]\nInternalResolutionScale=1\n' > "$RUN/sim_config.ini"
    for setting in ${TEST_CONFIG:-}; do
        echo "$setting" >> "$RUN/sim_config.ini"
    done
    if [ -n "$FROM" ]; then
        [ -f "$FIXTURES/$FROM/save.bin" ] || { echo "No fixture $FIXTURES/$FROM"; exit 1; }
        cp "$FIXTURES/$FROM/save.bin" "$RUN/save.bin"
    fi
    TEST=pokeheartgold-test${TEST_ID:+-$TEST_ID}
    docker rm -f $TEST >/dev/null 2>&1 || true
    docker run -d --name $TEST --user "$(id -u):$(id -g)" -e HOME=/tmp -e DISPLAY=:1 -e SDL_AUDIODRIVER=dummy \
        -e LIBGL_ALWAYS_SOFTWARE=1 -e LIBNTR_TEST_MODE=1 -e POKEHEARTGOLD_TEST_SCRIPT="$SCRIPT" \
        -e POKEHEARTGOLD_GEN5_ROM="$GEN5_ROM" $(for e in $TEST_ENV; do printf -- "-e %s " "$e"; done) $(mounts) -w "$RUN" \
        $IMAGE sh -c "Xvfb :1 -screen 0 ${SCREEN}x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
                      sleep 1
                      exec stdbuf -oL -eL '$BIN'" >/dev/null # Line-buffered: nothing lost if it dies
    echo "Running $TEST_NAME${FROM:+ from $FROM}"
    docker logs -f $TEST 2>&1 | while IFS= read -r line; do
        case "$line" in
        "[test] shot "*)
            shot=${line#\[test\] shot }
            docker exec -e DISPLAY=:1 $TEST import -window root /tmp/shot.png 2>/dev/null &&
                docker cp $TEST:/tmp/shot.png "$SHOTS/$shot.png" >/dev/null 2>&1 && echo "$line -> $SHOTS/$shot.png" ;;
        "[test] frame "*) ;;
        "[test] "* | *"[assert]"*) echo "$line" ;;
        esac
    done &
    READER=$!
    START=$(date +%s)
    TIMED_OUT=
    while [ "$(docker inspect -f '{{.State.Running}}' $TEST 2>/dev/null)" = true ]; do
        if [ $(($(date +%s) - START)) -ge "${TEST_LIMIT:-600}" ]; then
            echo "[test] FAIL: still running after ${TEST_LIMIT:-600} s"
            docker exec -e DISPLAY=:1 $TEST import -window root /tmp/shot.png 2>/dev/null &&
                docker cp $TEST:/tmp/shot.png "$SHOTS/timeout.png" >/dev/null 2>&1
            # Where the game is: three samples of its backtrace, a second apart (lib/port/test_crash.c)
            for sample in 1 2 3; do
                docker exec $TEST pkill -QUIT -x main >/dev/null 2>&1 && sleep 1
            done
            docker stop -t 1 $TEST >/dev/null 2>&1
            TIMED_OUT=1
            break
        fi
        sleep 1
    done
    wait $READER 2>/dev/null || true
    CODE=$(docker inspect -f '{{.State.ExitCode}}' $TEST)
    # Anything but the script's 0 or 1: the game quit by itself (128 + a signal it doesn't catch)
    [ "$CODE" = 0 ] || [ "$CODE" = 1 ] || [ -n "$TIMED_OUT" ] || echo "[test] FAIL: the game exited with code $CODE"
    [ "$CODE" = 0 ] || [ "$CODE" = 1 ] || CODE=1
    [ -z "$TIMED_OUT" ] || CODE=1
    docker logs $TEST > "$SHOTS/log.txt" 2>&1
    # A crash's or an unexpected exit's backtrace, or a hang's samples, in functions and lines
    if grep -q '^\[test\] \(CRASH\|HANG\|EXIT\)' "$SHOTS/log.txt"; then
        grep '^\[test\] \(CRASH\|HANG\|EXIT\)' "$SHOTS/log.txt" | head -n 1 | grep 'CRASH\|EXIT' ||
            echo "[test] still running at the limit: samples of the game thread's backtrace"
        grep '^\[test\] \(frame 0x\|HANG\|EXIT\)' "$SHOTS/log.txt" | sed 's/.*frame //; s/.*\(HANG\|EXIT\).*/HANG/' |
            awk '/HANG/ { n++; next } { print n + 0, $0 }' > "$SHOTS/frames.txt"
        for n in $(cut -d' ' -f1 "$SHOTS/frames.txt" | uniq); do
            [ "$n" = 0 ] || echo "  -- sample $n"
            awk -v n="$n" '$1 == n { print $2 }' "$SHOTS/frames.txt" |
                docker run --rm -i $(mounts) $IMAGE addr2line -f -C -i -p -e "$BIN" | sed 's#'"$ROOT"'/build_linux/../##; s#^#  #'
        done | tee "$SHOTS/crash.txt"
        rm -f "$SHOTS/frames.txt"
    fi
    docker rm -f $TEST >/dev/null 2>&1 || true
    # A Gen 5 import made in this run (not the data folder's, linked): kept for the next runs
    if [ "$CODE" = 0 ] && [ -d "$RUN/gen5" ] && [ ! -L "$RUN/gen5" ] && [ ! -e "$DATA/gen5" ]; then
        mv "$RUN/gen5" "$DATA/gen5"
        echo "Kept the Gen 5 files in $DATA/gen5"
    fi
    if [ "$CODE" = 0 ] && [ -n "$KEEP" ]; then
        [ -f "$RUN/save.bin" ] || { echo "The run ended without a save to keep"; exit 1; }
        mkdir -p "$FIXTURES/$KEEP"
        cp "$RUN/save.bin" "$FIXTURES/$KEEP/save.bin"
        echo "Kept the save as the fixture $KEEP ($FIXTURES/$KEEP)"
    fi
    echo "$TEST_NAME: $([ "$CODE" = 0 ] && echo passed || echo "FAILED (exit $CODE)"); log and screenshots in $SHOTS"
    exit "$CODE"
    ;;
apk)
    image
    shift
    # A version code from the date and time, so each build installs over the last
    VERSION_CODE=${VERSION_CODE:-$(date +%y%m%d%H)}
    POKEBW2_ARG=$(gen5_option | sed 's/^-Dpokebw2=//')
    docker run --rm -i --user "$(id -u):$(id -g)" -e HOME=/tmp -e VERSION_CODE="$VERSION_CODE" -e POKEBW2="$POKEBW2_ARG" \
        -v android-ndk:/ndk -v android-tools:/tools $(mounts) -w "$ROOT" $IMAGE sh -c "
        export JAVA_HOME=/tools/jdk PATH=/tools/jdk/bin:/tools/gradle-8.10.2/bin:\$PATH
        export ANDROID_HOME=/tools/sdk ANDROID_SDK_ROOT=/tools/sdk GRADLE_USER_HOME=/tools/gradle-home
        export ANDROID_NDK_HOME=/ndk/android-ndk-r27c NDK_HOST_TAG=linux-x86_64
        git config --global --add safe.directory '*'
        ./tools/android/build_apk.sh $*" 2>&1 | tee "$THOR_DEV/hgss-apk.log"
    exit "${PIPESTATUS[0]}"
    ;;
install)
    ADB=${ADB:-$(command -v adb || echo "$HOME/Documents/platform-tools/adb")}
    "$ADB" install -r "$ROOT/android/app/build/outputs/apk/release/app-release.apk"
    ;;
stop)
    docker rm -f $NAME >/dev/null 2>&1 || true
    ;;
shell)
    image
    shift
    if [ $# -eq 0 ]; then
        docker run --rm -it --user "$(id -u):$(id -g)" -e HOME=/tmp $(mounts) -w "$ROOT" $IMAGE bash
    else
        in_container "$*"
    fi
    ;;
*)
    sed -n 2,27p "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
