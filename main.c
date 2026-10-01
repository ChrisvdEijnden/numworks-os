/* ================================================================
 * NumWorks OS — Main Entry Point (N0120 Custom Firmware)
 * Boot sequence:
 *   HAL → FS → USB CDC → USB Host → Kernel → Home → Loop
 * ================================================================ */
#include "hal/hal.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "hal/led.h"
#include "hal/backlight.h"
#include "hal/battery.h"
#include "ui/lang.h"
#include "apps/settings/prefs.h"
#include "kernel/kernel.h"
#include "fs/flashfs.h"
#include "fs/storage.h"
#include "shell/shell.h"
#include "ui/filemanager.h"
#include "usb/usb_cdc.h"
#include "micropython-port/mp_port.h"
#include "apps/home/home.h"
#include "apps/calculator/calculator.h"
#include "apps/functions/functions.h"
#include "apps/equations/equations.h"
#include "apps/python_app/python_app.h"
#include "apps/tetris/tetris.h"
#include "apps/docs_app/docs_app.h"
#include "apps/settings/settings.h"
#include "apps/text_editor/text_editor.h"
#include "apps/statistics/statistics.h"
#include "apps/games/games.h"
#include "include/config.h"
#include <string.h>

/* ── Boot splash ──────────────────────────────────────────────── */
static void boot_splash(uint32_t ms_start) {
    display_fill(RGB(10,10,20));
    display_fill_rect(0, 0, LCD_WIDTH, 30, RGB(30,80,200));
    display_str(10,  8, "NumWorks OS",        WHITE, RGB(30,80,200));
    display_str(10, 40, TR("Firmware voor de N0110", "Firmware for the N0110"), GREY, RGB(10,10,20));
    display_str(10, 56, "STM32F730 @ 192 MHz",  GREY, RGB(10,10,20));
    display_str(10, 72, TR("Opstarten...", "Starting..."), RGB(100,200,255), RGB(10,10,20));

    /* Progress bar */
    display_rect(20, 110, 280, 14, RGB(60,60,80));
    display_flush();
    uint32_t elapsed;
    int shown = -1;
    do {
        elapsed = hal_tick_ms() - ms_start;
        int pct = (int)(elapsed * 280 / 1500);
        if (pct > 280) pct = 280;
        if (pct != shown) {
            display_fill_rect(21, 111, pct, 12, RGB(60,130,255));
            display_flush();
            shown = pct;
        }
    } while (elapsed < 1500);

    if (hal_reset_by_watchdog())
        display_str(10, 124, TR("Herstart na een vastloper (watchdog).",
                                "Restarted after a hang (watchdog)."), YELLOW, RGB(10,10,20));
    display_str(10, 140, TR("Klaar!", "Ready!"), RGB(100,255,100), RGB(10,10,20));
    display_flush();
    hal_delay_ms(300);
}

/* ── Storage at boot ──────────────────────────────────────────── */
static void boot_message(const char *a, const char *b, const char *c) {
    const uint16_t bg = RGB(20,0,0);
    display_fill(bg);
    display_str(10, 20, a, YELLOW, bg);
    if (b) display_str(10, 36, b, WHITE, bg);
    if (c) display_str(10, 52, c, WHITE, bg);
    display_flush();
}

/* No file system yet (first start) or an unreadable one. Formatting
 * erases the storage area, so ask instead of doing it silently: if the
 * flash driver misbehaves on this board, BACK keeps the calculator
 * usable. */
static int ask_format(void) {
    boot_message(TR("Geen bestandssysteem gevonden.", "No file system found."),
                 TR("OK: formatteren (wist de opslag)", "OK: format (erases the storage)"),
                 TR("BACK: overslaan, niets wordt bewaard", "BACK: skip, nothing will be saved"));
    key_event_t ev;
    for (;;) {
        hal_delay_ms(10);
        if (!keyboard_poll(&ev) || ev.action != 0) continue;
        if (key_is_exe((key_code_t)ev.key)) break;
        if (ev.key == KEY_BACK || ev.key == KEY_HOME) return FFS_ERR_FORMAT;
    }
    boot_message(TR("Formatteren...", "Formatting..."), NULL, NULL);
    int r = flashfs_format();
    if (r != FFS_OK) {
        boot_message(TR("Formatteren mislukt.", "Formatting failed."), storage_status(),
                     TR("Bestanden worden niet bewaard.", "Files will not be saved."));
        hal_delay_ms(2500);
    }
    return r;
}

/* ── Check if HOME key held at boot (go to shell) ─────────────── */
static bool home_held_at_boot(void) {
    return keyboard_is_pressed(KEY_HOME);
}

/* ── Initialise all apps ──────────────────────────────────────── */
static void app_init_all(void) {
    home_init();
    calculator_init();
    functions_init();
    equations_init();
    python_app_init();
    fm_init();
    tetris_init();
    docs_init();
    settings_init();
    text_editor_init();
    statistics_init();
    games_init();
    snake_init();
    g2048_init();
    shell_init();
}

/* ── Main ─────────────────────────────────────────────────────── */
int main(void) {
    uint32_t boot_start = 0;

    /* 1. HAL init */
    hal_init();
    hal_watchdog_start();
    display_init();
    backlight_init();
    hal_boot_log("display");
    keyboard_init();
    hal_boot_log("keyboard");
    led_init();
    battery_init();
    boot_start = hal_tick_ms();

    /* 2. Kernel */
    kernel_init();
    hal_boot_log("kernel");

    /* 3. File system on the external flash */
    int fs = flashfs_init();
    hal_boot_log("storage:");
    hal_boot_log(storage_status());
    if (fs == FFS_ERR_FORMAT) fs = ask_format();
    else if (fs == FFS_ERR_NODEV) {
        boot_message(TR("Geen opslag gevonden:", "No storage found:"), storage_status(),
                     TR("Bestanden worden niet bewaard.", "Files will not be saved."));
        hal_delay_ms(2000);
    }
    hal_boot_log(fs == FFS_OK ? "flashfs: mounted" : "flashfs: not in use");
    prefs_load();          /* LED, brightness, language */

    /* 4. USB CDC (virtual serial for PC transfer) */
    usb_cdc_init();

    /* 5. MicroPython */
    mp_init_port();
    hal_boot_log("micropython");

    /* 6. Boot splash */
    boot_splash(boot_start);

    /* 7. Init default files */
    if (!flashfs_exists("welkom.py")) {
        const char *hello =
            "# Welkom bij NumWorks OS!\n"
            "import display\n"
            "display.fill(display.BLACK)\n"
            "display.str(40, 100, 'Hallo, NumWorks!', display.WHITE, display.BLACK)\n"
            "display.flush()\n";
        flashfs_write("welkom.py", hello, (uint32_t)strlen(hello));
    }

    /* 8. Init all app modules */
    app_init_all();
    hal_boot_log("apps; starting event loop");

    /* 9. Start at Home (or Shell if HOME held) */
    if (home_held_at_boot()) {
        kernel_set_app(APP_SHELL);
    } else {
        kernel_set_app(APP_HOME);
    }

    /* 10. Kernel event loop — never returns */
    kernel_run();

    return 0;
}
