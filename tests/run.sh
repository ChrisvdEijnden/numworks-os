#!/usr/bin/env bash
# Host tests. Each suite compiles the OS code it covers with gcc (with
# the address and undefined-behaviour sanitizers) against simulated
# hardware, then runs it. See tests/README.md.
#
#   tests/run.sh             all suites
#   tests/run.sh qspi usb    just these
#   tests/run.sh -l          list the suites
#
# Output goes to tests/build/ (logs in tests/build/logs/). A suite
# whose tools are missing is skipped and says why. Exit status 1 if
# any suite failed.
set -u
cd "$(dirname "$0")/.."
R=$PWD
T=$R/tests
B=$T/build
mkdir -p "$B/logs"

export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_stack_use_after_return=0}
CC=${HOSTCC:-gcc}
SAN="-g -w -fsanitize=address,undefined"
JOBS=$(nproc 2> /dev/null || sysctl -n hw.ncpu 2> /dev/null || echo 4)

SUITES="flashfs qspi expr equations keyboard tetris functions editor scheduler
        shell crash sleep display hal usb apps python transfer web loader sim"

# ── Helpers ────────────────────────────────────────────────────────
skip() { echo "SKIP: $*"; return 77; }
has() { command -v "$1" > /dev/null 2>&1; }
gen() { python3 "$T/gen_host.py" "$R" "$B/gen"; }

# ── Suites: build into $D, then run (the exit status is the result) ─
t_flashfs() {
    $CC $SAN -o "$D/t" "$T/flashfs/ffs_test.c" "$T/common/storage_sim.c" fs/flashfs.c && "$D/t"
}
t_qspi() {
    $CC $SAN -O1 -DQSPI_SIM -I"$T/qspi" -o "$D/t" "$T/qspi/qspi_test.c" "$T/qspi/qspi_sim.c" \
        fs/storage_qspi.c fs/flashfs.c && "$D/t"
}
t_expr() {
    $CC $SAN -o "$D/t" "$T/expr/expr_test.c" apps/common/expr.c ui/lang.c -lm && "$D/t"
}
t_equations() {
    $CC $SAN -o "$D/t" "$T/equations/eq_test.c" "$T/equations/eq_wrap.c" apps/common/expr.c ui/lang.c -lm && "$D/t"
}
t_keyboard() {
    $CC $SAN -o "$D/t" "$T/keyboard/kbd_test.c" "$T/keyboard/kbd_wrap.c" && "$D/t"
}
t_tetris() {
    $CC $SAN -o "$D/t" "$T/tetris/tet_test.c" "$T/tetris/tet_wrap.c" ui/lang.c && "$D/t"
}
t_functions() {
    $CC $SAN -o "$D/t" "$T/functions/fe_test.c" "$T/functions/fe_wrap.c" "$T/functions/ed_wrap.c" \
        "$T/common/kbonly_wrap.c" apps/common/expr.c apps/common/analysis.c ui/lang.c -lm && "$D/t"
}
t_editor() {
    $CC $SAN -o "$D/t" "$T/functions/ed2_test.c" "$T/functions/ed_wrap.c" "$T/common/kbonly_wrap.c" \
        ui/lang.c && "$D/t"
}
t_scheduler() {
    $CC $SAN -Ikernel -o "$D/t" "$T/scheduler/sched_test.c" kernel/scheduler.c && "$D/t"
}
t_shell() {
    $CC $SAN -o "$D/t" "$T/shell/shell_test.c" shell/shell.c ui/lang.c && "$D/t"
}
t_crash() {
    gen && $CC $SAN -std=gnu11 -I"$B/gen" -o "$D/t" "$T/crash/crash_test.c" ui/lang.c && "$D/t"
}
t_sleep() {
    gen && $CC $SAN -I"$R" -o "$D/t" "$T/sleep/sleep_test.c" "$B/gen/kernel_host.c" "$T/sleep/stubs.c" && "$D/t"
}
t_display() {
    gen && $CC $SAN -I"$T/display" -o "$D/t" "$T/display/lcd_test.c" "$B/gen/display_host.c" ui/font.c && "$D/t"
}
t_hal() {
    gen && $CC $SAN -I"$T/hal" -o "$D/t" "$T/hal/hw_test.c" \
        "$B/gen/backlight_host.c" "$B/gen/led_host.c" "$B/gen/battery_host.c" && "$D/t"
}
t_usb() {
    $CC $SAN -DUSB_SIM -Iusb -o "$D/t" "$T/usb/usb_test.c" "$T/usb/otg_sim.c" usb/usb_device.c usb/usb_cdc.c \
        fs/flashfs.c "$T/common/storage_sim.c" && "$D/t"
}
t_apps() {
    $CC $SAN -o "$D/t" "$T/apps/apps_test.c" apps/calculator/calculator.c apps/statistics/statistics.c \
        apps/common/stats.c apps/common/expr.c apps/games/snake.c apps/games/g2048.c apps/games/games.c \
        apps/settings/prefs.c apps/settings/settings.c ui/lang.c fs/flashfs.c "$T/common/storage_sim.c" \
        "$T/common/kbonly_wrap.c" -lm && "$D/t"
}
t_python() {
    local E=micropython-port/micropython_embed
    [ -f $E/genhdr/qstrdefs.generated.h ] || { skip "no MicroPython package: run 'make mp' first"; return; }
    local CF="-std=gnu99 -O1 $SAN -fno-sanitize=alignment -DNWOS_MICROPYTHON -DNDEBUG -DHOST_REGIONS -Imicropython-port -I$E"
    # MicroPython's own core works in ways the sanitizers flag but that are
    # intended: pointers that step outside their array, functions called
    # through a generic pointer type, and a garbage collector that reads
    # the whole C stack, the sanitizer's guard zones included. Those checks
    # are off for the core only; our port code keeps all of them.
    local CORE="$CF -fno-sanitize=pointer-overflow"
    $CC --version 2> /dev/null | grep -q clang && CORE="$CORE -fno-sanitize=function"
    local f o fl objs=() n=0
    # compiled once, again when a source or the compiler changes
    [ "$(cat "$D/o/flags" 2> /dev/null)" = "$CC $CORE" ] || rm -rf "$D/o"
    mkdir -p "$D/o"
    echo "$CC $CORE" > "$D/o/flags"
    for f in $E/py/*.c $E/shared/runtime/gchelper_generic.c $E/port/embed_util.c \
             micropython-port/modules/nwos/*.c micropython-port/mp_port.c; do
        o=$D/o/$(echo "$f" | tr / _).o
        objs+=("$o")
        [ "$o" -nt "$f" ] && continue
        case $f in
            $E/py/gc.c) fl="$CORE -fno-sanitize=address" ;;
            $E/*)       fl=$CORE ;;
            *)          fl=$CF ;;
        esac
        rm -f "$o"
        $CC $fl -c "$f" -o "$o" &
        n=$((n + 1))
        if [ $((n % JOBS)) = 0 ]; then wait; fi    # (macOS's bash 3.2 has no wait -n)
    done
    wait
    for o in "${objs[@]}"; do [ -f "$o" ] || return 1; done     # a compile failed
    $CC $SAN -Dkeyboard_poll=kb_real_poll -Dkeyboard_raw_any=kb_real_raw_any \
        -Dkeyboard_is_pressed=kb_real_is_pressed -c "$T/python/kb_host.c" -o "$D/kb_host.o" &&
    $CC $CF -o "$D/t" "${objs[@]}" "$D/kb_host.o" "$T/python/py_test.c" "$T/python/pa_wrap.c" \
        "$T/python/stubs2.c" ui/line_input.c ui/lang.c fs/flashfs.c "$T/common/storage_sim.c" -lm &&
    "$D/t"
}
t_transfer() {
    python3 -c 'import serial, pty' 2> /dev/null || { skip "needs Python's pyserial (pip install pyserial)"; return; }
    $CC $SAN -o "$D/cdc_bridge" "$T/transfer/cdc_bridge.c" usb/usb_cdc.c fs/flashfs.c "$T/common/storage_sim.c" &&
    python3 "$T/transfer/cdc_e2e.py" "$D/cdc_bridge"
}
t_web() {
    has node || { skip "needs Node.js"; return; }
    export NODE_PATH=${NODE_PATH:-$(npm root -g 2> /dev/null)}
    node -e "require('playwright').chromium.executablePath()" > /dev/null 2>&1 ||
        { skip "needs Playwright with Chromium (npm install -g playwright)"; return; }
    $CC -O1 -w -o "$D/cdc_pipe" "$T/web/cdc_pipe.c" usb/usb_cdc.c fs/flashfs.c "$T/common/storage_sim.c" &&
    node "$T/web/web_test.js" "$D/cdc_pipe"
}
t_loader() {
    has arm-none-eabi-gcc || { skip "needs the arm-none-eabi toolchain"; return; }
    python3 -c 'import unicorn' 2> /dev/null || { skip "needs Python's unicorn (pip install unicorn)"; return; }
    # the loader as shipped, one with a 5 ms busy timeout for the dead-flash
    # case, and the OS image whose vector table the loader has to accept
    make -s loader BUILD="$D/loader" &&
    make -s loader BUILD="$D/quick" LOADER_DEFS=-DLOADER_BUSY_TIMEOUT_MS=5 > /dev/null &&
    make -s -j"$JOBS" BUILD="$D/os" > /dev/null &&
    python3 "$T/loader/loader_emu.py" "$D/loader/loader.bin" "$D/os/numworks_os_n0120.bin" "$D/quick/loader.bin"
}

t_sim() {
    { pkg-config --exists sdl2 2> /dev/null || sdl2-config --version > /dev/null 2>&1; } ||
        { skip "needs SDL2 (brew install sdl2, or apt install libsdl2-dev)"; return; }
    make -s -j"$JOBS" sim BUILD="$D/build" SIM_CC="$CC" > /dev/null || return 1
    local out=$D/out.txt port up=no mp=no
    [ -f micropython-port/micropython_embed/genhdr/qstrdefs.generated.h ] && mp=yes
    rm -f "$D/storage.bin" "$D"/*.bmp
    printf 'print("hello from the PC")\n' > "$D/hello.py"
    # Empty storage: OK formats it. Then the Shell, and commands over the
    # debug UART; meanwhile tools/upload.py sends hello.py over the
    # pseudo-terminal, as to the calculator's serial port.
    "$D/build/sim/numworks-sim" --headless --fresh --storage "$D/storage.bin" --script "wait 1500;
        screen $D/format.bmp; key OK; wait 3500; key DOWN; key DOWN; key RIGHT; key OK; wait 800;
        uart ls; wait 400; uart run hello.py; wait 1500; uart mem; wait 400; screen $D/shell.bmp" \
        < /dev/null > "$out" 2>&1 &
    local pid=$! i
    for i in $(seq 100); do grep -q "flashfs: mounted" "$out" && break; sleep 0.1; done
    port=$(sed -n 's/^usb: PC transfer on \([^,]*\),.*/\1/p' "$out")
    if [ -n "$port" ] && python3 -c 'import serial' 2> /dev/null; then
        python3 tools/upload.py --port "$port" upload "$D/hello.py" > /dev/null && up=yes
    fi
    wait $pid || { cat "$out"; return 1; }
    cat "$out"
    local fails=0
    ck() { if eval "$1"; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
    ck 'grep -q "lcd: id 85 85 52" "$out"' "the display driver finds the simulated panel"
    ck 'grep -q "flashfs: mounted" "$out"' "OK formats the empty storage, the file system mounts"
    ck 'grep -q "^  welkom.py" "$out"' "shell over the UART: ls lists the files"
    ck 'grep -q "^Flash: " "$out"' "shell: mem"
    if [ $up = yes ] && [ $mp = yes ]; then
        ck 'grep -q "^hello from the PC" "$out"' "a file sent with tools/upload.py runs in Python"
    fi
    ck '[ "$(wc -c < "$D/format.bmp")" -eq 230454 ] && [ "$(wc -c < "$D/shell.bmp")" -eq 230454 ]' "screenshots saved"
    ck '! cmp -s "$D/format.bmp" "$D/shell.bmp"' "the screen changed between them"
    [ $fails = 0 ]
}

# ── Run ────────────────────────────────────────────────────────────
if [ "${1:-}" = -l ]; then echo $SUITES; exit 0; fi
want=${*:-$SUITES}
for s in $want; do
    case " $(echo $SUITES) " in *" $s "*) ;; *) echo "unknown suite: $s (tests/run.sh -l lists them)"; exit 2;; esac
done

pass=0 fail=0 skipped=0 failed=""
for s in $want; do
    D=$B/$s
    mkdir -p "$D"
    log=$B/logs/$s.log
    printf '%-10s ' "$s"
    start=$SECONDS
    ( "t_$s" ) > "$log" 2>&1
    rc=$?
    if [ $rc = 77 ]; then
        echo "skipped: $(sed -n 's/^SKIP: //p' "$log" | tail -1)"
        skipped=$((skipped + 1))
    elif [ $rc = 0 ]; then
        n=$(grep -cE '^ *ok( |$)' "$log")       # suites that list their checks
        echo "ok   ($([ "$n" = 0 ] || echo "$n checks, ")$((SECONDS - start)) s)"
        pass=$((pass + 1))
    else
        echo "FAILED (exit $rc), see tests/build/logs/$s.log:"
        grep -E 'FAIL|error|ERROR|runtime error' "$log" | head -15 | sed 's/^/      /'
        fail=$((fail + 1)); failed="$failed $s"
    fi
done
echo "passed $pass, failed $fail, skipped $skipped${failed:+ (failed:$failed)}"
[ $fail = 0 ]
