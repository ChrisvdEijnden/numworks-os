/* ================================================================
 * NumWorks OS — Main Entry Point (N0120 Custom Firmware)
 * Boot sequence:
 *   HAL → FS → USB CDC → USB Host → Kernel → Home → Loop
 * ================================================================ */
#include "hal/hal.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "hal/timer.h"
#include "hal/led.h"
#include "kernel/kernel.h"
#include "fs/flashfs.h"
#include "shell/shell.h"
#include "ui/filemanager.h"
#include "usb/usb_cdc.h"
#include "usb/usb_host.h"
#include "micropython-port/mp_port.h"
#include "apps/home/home.h"
#include "apps/calculator/calculator.h"
#include "apps/functions/functions.h"
#include "apps/equations/equations.h"
#include "apps/python_app/python_app.h"
#include "apps/tetris/tetris.h"
#include "apps/docs_app/docs_app.h"
#include "apps/settings/settings.h"
#include "apps/photo_viewer/photo_viewer.h"
#include "apps/text_editor/text_editor.h"
#include "include/config.h"
#include "include/string.h"

/* ── Boot splash ──────────────────────────────────────────────── */
static void boot_splash(uint32_t ms_start) {
    display_fill(RGB(10,10,20));
    display_fill_rect(0, 0, LCD_WIDTH, 30, RGB(30,80,200));
    display_str(10,  8, "NumWorks OS",        WHITE, RGB(30,80,200));
    display_str(10, 40, "N0120 Custom Firmware",GREY, RGB(10,10,20));
    display_str(10, 56, "STM32F730 @ 216 MHz",  GREY, RGB(10,10,20));
    display_str(10, 72, "Initialiseren...",     RGB(100,200,255), RGB(10,10,20));

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

    display_str(10, 140, "Klaar!", RGB(100,255,100), RGB(10,10,20));
    display_flush();
    hal_delay_ms(300);
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
    photo_viewer_init();
    text_editor_init();
    shell_init();
}

/* ── Main ─────────────────────────────────────────────────────── */
int main(void) {
    uint32_t boot_start = 0;

    /* 1. HAL init */
    hal_init();
    display_init();
    hal_boot_log("display");
    keyboard_init();
    hal_boot_log("keyboard");
    hal_timer_init();
    led_init();
    boot_start = hal_tick_ms();

    /* 2. Kernel */
    kernel_init();
    hal_boot_log("kernel");

    /* 3. Flash FS. When it has to compact, it borrows the framebuffer as
     *    scratch space and the current app is redrawn afterwards. */
    flashfs_set_scratch(g_framebuf, sizeof(g_framebuf), kernel_request_redraw);
    int fs = flashfs_init();
    if (fs == FFS_ERR_FORMAT) {
        display_fill(RGB(20,0,0));
        display_str(10, 20, "FS ongeldig - formatteren...", RED, RGB(20,0,0));
        display_flush();
        hal_delay_ms(800);
        flashfs_format();
    } else if (fs == FFS_ERR_NODEV) {
        display_fill(RGB(20,0,0));
        display_str(10, 20, "Geen flash-opslag gevonden.", RED, RGB(20,0,0));
        display_str(10, 32, "Bestanden worden niet bewaard.", RED, RGB(20,0,0));
        display_flush();
        hal_delay_ms(1500);
    }
    hal_boot_log(fs == FFS_OK ? "flashfs: mounted" :
                 fs == FFS_ERR_FORMAT ? "flashfs: formatted" : "flashfs: no storage");

    /* 4. USB CDC (virtual serial for PC transfer) */
    usb_cdc_init();

    /* 5. USB host mode (for USB drives) is not started: the OTG core is in
     *    device mode for CDC, and the board can't power a drive on VBUS. */

    /* 6. MicroPython */
    mp_init_port();
    hal_boot_log("micropython");

    /* 7. Boot splash */
    boot_splash(boot_start);

    /* 8. Init default files */
    if (!flashfs_exists("welkom.py")) {
        const char *hello =
            "# Welkom bij NumWorks OS!\n"
            "import display\n"
            "display.fill(display.BLACK)\n"
            "display.str(40, 100, 'Hallo, NumWorks!', display.WHITE, display.BLACK)\n"
            "display.flush()\n";
        flashfs_write("welkom.py", hello, (uint32_t)strlen(hello));
    }

    /* 9. Init all app modules */
    app_init_all();
    hal_boot_log("apps; starting event loop");

    /* 10. Start at Home (or Shell if HOME held) */
    if (home_held_at_boot()) {
        kernel_set_app(APP_SHELL);
    } else {
        kernel_set_app(APP_HOME);
    }

    /* 11. Kernel event loop — never returns */
    kernel_run();

    return 0;
}
