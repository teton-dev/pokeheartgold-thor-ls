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
#
# GAME=ss runs SoulSilver (its own data folder and ROM). The container is x86-64 and runs under Rosetta on Apple
# Silicon. THOR_DEV (default ~/thor-dev) holds the ROMs and the data folders (desktop-hg, desktop-ss: the extracted
# ROM, saves and settings, kept between runs); the game sees THOR_DEV at the same path as on the Mac.
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
    mkdir -p "$DATA"
    echo "--mount type=bind,src=$THOR_DEV,target=$THOR_DEV"
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
    [ -f "$ROOT/build_linux/build.ninja" ] ||
        in_container meson setup build_linux -Dbuild_target=linux -Dlibntr:card_game_code=IPKE,IPGE
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
    sed -n 2,19p "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
