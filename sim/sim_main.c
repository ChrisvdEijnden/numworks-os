/* ================================================================
 * NumWorks OS — simulator
 *
 * The OS's own main() runs unchanged in a second thread, on a stack of
 * its own (as on the calculator, where the linker script places it).
 * This thread keeps the window: it draws what the panel shows and
 * turns the PC's keyboard and mouse into calculator keys.
 *
 *   numworks-sim [options]
 *     --storage FILE   where the files are kept
 *                      (default ~/.numworks-sim/storage.bin)
 *     --fresh          start with empty storage, like a new calculator
 *     --scale F        window size, 1 = the picture's own (the screen in
 *                      it 640 x 480); by default the window fits the
 *                      screen
 *     --skin DIR       where NumWorks' simulator picture is (make
 *                      run-sim fetches it into build/sim/skin)
 *     --no-skin        the calculator drawn here instead
 *     --no-usb         no pseudo-terminal for PC transfer
 *     --script STEPS   run steps, then quit (see below)
 *     --headless       no window (with --script)
 *
 * Script steps, separated by ';':
 *   wait MS | key NAME | hold NAME MS | type TEXT | uart TEXT |
 *   shot FILE.bmp | screen FILE.bmp | quit
 * NAME is a key as in keyboard.h without KEY_ (OK, EXE, HOME, 7, ...);
 * type presses the keys for TEXT, uart sends TEXT and Enter to the
 * debug UART (the shell reads it while it is open); shot saves the
 * window, screen just the calculator screen.
 * ================================================================ */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sim.h"
#include "sim_window.h"
#include "sim_skin.h"

int nwos_main(void);                    /* main.c, built with -Dmain=nwos_main */
void hal_stack_paint(void);
extern uint8_t _sstack[];

static char **s_argv;                   /* for a restart, without --fresh */
static bool s_scripted;

/* ── Restart: hal_reset() and the crash screen ──────────────────── */
void sim_reboot(void) {
    sim_storage_sync();
    fflush(stdout);
    if (s_scripted) {                   /* a script ends at a restart */
        printf("sim: restart requested, script ends\n");
        _exit(0);
    }
    printf("sim: restart\n");
    fflush(stdout);
    execvp(s_argv[0], s_argv);
    perror(s_argv[0]);
    _exit(1);
}

static void *os_thread(void *arg) {
    (void)arg;
    nwos_main();                        /* the kernel loop never returns */
    return NULL;
}

/* ── Keys by name, and characters to keys ─────────────────────── */
static const struct { const char *name; key_code_t key; } NAMES[] = {
    {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN},
    {"OK", KEY_OK}, {"BACK", KEY_BACK}, {"HOME", KEY_HOME}, {"ONOFF", KEY_ONOFF},
    {"0", KEY_0}, {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4},
    {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7}, {"8", KEY_8}, {"9", KEY_9},
    {"DOT", KEY_DOT}, {"EE", KEY_EE}, {"PLUS", KEY_PLUS}, {"MINUS", KEY_MINUS},
    {"MUL", KEY_MUL}, {"DIV", KEY_DIV}, {"POW", KEY_POW}, {"SQRT", KEY_SQRT},
    {"SIN", KEY_SIN}, {"COS", KEY_COS}, {"TAN", KEY_TAN}, {"EXP", KEY_EXP},
    {"LN", KEY_LN}, {"LOG", KEY_LOG}, {"IMAG", KEY_IMAG}, {"COMMA", KEY_COMMA},
    {"PI", KEY_PI}, {"SQUARE", KEY_SQUARE}, {"LPAREN", KEY_LPAREN}, {"RPAREN", KEY_RPAREN},
    {"ANS", KEY_ANS}, {"SHIFT", KEY_SHIFT}, {"ALPHA", KEY_ALPHA}, {"XNT", KEY_XNT},
    {"VAR", KEY_VAR}, {"TOOLBOX", KEY_TOOLBOX}, {"BACKSPACE", KEY_BACKSPACE},
    {"DEL", KEY_BACKSPACE}, {"EXE", KEY_EXE},
};

static key_code_t key_named(const char *s) {
    for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (!strcasecmp(s, NAMES[i].name)) return NAMES[i].key;
    return KEY_NONE;
}

/* The key that carries a character, as printed on the calculator:
 * letters are their ALPHA key, the rest plain, then SHIFT, then ALPHA.
 * SHIFT and ALPHA themselves are up to the user, as on the calculator. */
static key_code_t key_for_char(char c) {
    static const bool MODES[4][2] = { {false, false}, {true, false}, {false, true}, {true, true} };
    static const int LETTER_ORDER[4] = { 2, 3, 0, 1 }, OTHER_ORDER[4] = { 0, 1, 2, 3 };
    const int *order = isalpha((unsigned char)c) ? LETTER_ORDER : OTHER_ORDER;
    for (int m = 0; m < 4; m++)
        for (int k = KEY_NONE + 1; k < KEY_COUNT; k++)
            if (key_to_char((key_code_t)k, MODES[order[m]][0], MODES[order[m]][1]) == c) return (key_code_t)k;
    return KEY_NONE;
}

/* ── Taps: a press long enough for the debounce, then a release ──── */
#define TAP_MS 40
static key_code_t s_taps[256];
static unsigned s_tap_head, s_tap_tail;
static uint64_t s_tap_next;              /* when the queue may move on */
static bool s_tap_down;

static void tap(key_code_t k) {
    unsigned next = (s_tap_tail + 1) % 256;
    if (k != KEY_NONE && next != s_tap_head) { s_taps[s_tap_tail] = k; s_tap_tail = next; }
}

static void type_text(const char *s) { for (; *s; s++) tap(key_for_char(*s)); }

static void run_taps(uint64_t now) {
    if (s_tap_head == s_tap_tail || now < s_tap_next) return;
    if (!s_tap_down) { sim_key(s_taps[s_tap_head], true); s_tap_down = true; }
    else {
        sim_key(s_taps[s_tap_head], false);
        s_tap_down = false;
        s_tap_head = (s_tap_head + 1) % 256;
    }
    s_tap_next = now + TAP_MS * 1000U;
}

static bool taps_idle(void) { return s_tap_head == s_tap_tail; }

/* ── Script ───────────────────────────────────────────────────── */
static char *s_script;                   /* the steps still to run */
static uint64_t s_script_wait;           /* don't go on before this */
static key_code_t s_hold_key;
static bool s_quit;
static void save_window(const char *path) {
    int w = sim_window_w(), h = sim_window_h();
    uint32_t *shot = malloc(sizeof(uint32_t) * (size_t)w * (size_t)h);
    if (!shot) return;
    sim_window_draw(shot);
    if (sim_save_bmp(path, shot, w, h, w)) printf("sim: saved %s\n", path);
    free(shot);
}

static void save_screen(const char *path) {
    static uint32_t px[SIM_PANEL_H][SIM_PANEL_W];
    sim_screen_pixels(px);
    if (sim_save_bmp(path, &px[0][0], SIM_PANEL_W, SIM_PANEL_H, SIM_PANEL_W)) printf("sim: saved %s\n", path);
}

static void run_script(uint64_t now) {
    if (!s_script || now < s_script_wait || !taps_idle()) return;
    if (s_hold_key != KEY_NONE) { sim_key(s_hold_key, false); s_hold_key = KEY_NONE; }
    while (*s_script == ';' || isspace((unsigned char)*s_script)) s_script++;
    if (!*s_script) { s_quit = true; return; }
    char *end = strchr(s_script, ';');
    if (end) *end = 0;
    char *arg = s_script;
    while (*arg && !isspace((unsigned char)*arg)) arg++;
    if (*arg) *arg++ = 0;
    while (isspace((unsigned char)*arg)) arg++;
    const char *cmd = s_script;
    s_script = end ? end + 1 : s_script + strlen(s_script);

    if (!strcmp(cmd, "wait")) s_script_wait = now + (uint64_t)atol(arg) * 1000U;
    else if (!strcmp(cmd, "key") || !strcmp(cmd, "hold")) {
        char name[32] = "";
        long ms = 0;
        sscanf(arg, "%31s %ld", name, &ms);
        key_code_t k = key_named(name);
        if (k == KEY_NONE) { fprintf(stderr, "sim: no key '%s'\n", name); exit(2); }
        if (!strcmp(cmd, "key")) tap(k);
        else { sim_key(k, true); s_hold_key = k; s_script_wait = now + (uint64_t)ms * 1000U; }
    } else if (!strcmp(cmd, "type")) type_text(arg);
    else if (!strcmp(cmd, "uart")) { sim_uart_type(arg); sim_uart_type("\r"); }
    else if (!strcmp(cmd, "shot")) save_window(arg);
    else if (!strcmp(cmd, "screen")) save_screen(arg);
    else if (!strcmp(cmd, "quit")) s_quit = true;
    else { fprintf(stderr, "sim: unknown script step '%s'\n", cmd); exit(2); }
}

/* ── PC keys held down: the calculator key is held as long ────────── */
static key_code_t held_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_LEFT: return KEY_LEFT;   case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_UP: return KEY_UP;       case SDLK_DOWN: return KEY_DOWN;
    case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_EXE;
    case SDLK_TAB: return KEY_OK;
    case SDLK_ESCAPE: return KEY_BACK;
    case SDLK_BACKSPACE: case SDLK_DELETE: return KEY_BACKSPACE;
    case SDLK_HOME: case SDLK_F1: return KEY_HOME;
    case SDLK_END: case SDLK_F2: return KEY_ONOFF;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_SHIFT;
    case SDLK_LALT: case SDLK_RALT: return KEY_ALPHA;
    default: return KEY_NONE;
    }
}

static void usage(void) {
    fputs("usage: numworks-sim [--storage FILE] [--fresh] [--scale F] [--no-usb]\n"
          "                    [--skin DIR | --no-skin] [--script STEPS] [--headless]\n", stderr);
    exit(2);
}

static void default_storage(char *out, size_t n) {
    const char *home = getenv("HOME");
    snprintf(out, n, "%s/.numworks-sim", home ? home : ".");
    if (mkdir(out, 0755) != 0 && errno != EEXIST) perror(out);
    strncat(out, "/storage.bin", n - strlen(out) - 1);
}

int main(int argc, char **argv) {
    char storage[1024] = "";
    bool fresh = false, headless = false, usb = true;
    double scale = 0;                   /* 0: fit the screen */
#ifdef SIM_SKIN_DIR
    const char *skin = SIM_SKIN_DIR;    /* where make run-sim puts NumWorks' picture */
#else
    const char *skin = NULL;
#endif
    s_argv = calloc((size_t)argc + 1, sizeof *s_argv);
    int kept = 0;
    s_argv[kept++] = argv[0];
    for (int i = 1; i < argc; i++) {
        const char *opt = argv[i], *val = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(opt, "--fresh")) { fresh = true; continue; }   /* not again after a restart */
        s_argv[kept++] = argv[i];
        if (!strcmp(opt, "--headless")) headless = true;
        else if (!strcmp(opt, "--no-usb")) usb = false;
        else if (!strcmp(opt, "--no-skin")) skin = NULL;
        else if (val && !strcmp(opt, "--skin")) skin = val;
        else if (val && !strcmp(opt, "--storage")) snprintf(storage, sizeof storage, "%s", val);
        else if (val && !strcmp(opt, "--scale")) scale = atof(val);
        else if (val && !strcmp(opt, "--script")) s_script = strdup(val);
        else usage();
        if (strcmp(opt, "--headless") && strcmp(opt, "--no-usb") && strcmp(opt, "--no-skin")) s_argv[kept++] = argv[++i];
    }
    if (headless && !s_script) usage();
    s_scripted = s_script != NULL;
    if (scale < 0.1 || scale > 4) scale = 0;
    if (!storage[0]) default_storage(storage, sizeof storage);
    if (!sim_storage_open(storage, fresh)) return 1;
    sim_usb_enable(usb);
    bool numworks = sim_window_init(skin);
    const int W = sim_window_w(), H = sim_window_h();

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture *tex = NULL;
    if (!headless) {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "sim: %s\n", SDL_GetError()); return 1; }
        SDL_Rect usable;
        if (scale == 0) {                    /* as large as fits, at most half the drawing (2x on Retina) */
            scale = 0.5;
            if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
                double fit = fmin(0.94 * usable.h / H, 0.94 * usable.w / W);
                if (fit < scale) scale = fit;
            }
        }
        win = SDL_CreateWindow("NumWorks OS simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               (int)(W * scale), (int)(H * scale),
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (ren) {
            SDL_RenderSetLogicalSize(ren, W, H);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
        }
        if (!tex) { fprintf(stderr, "sim: no window: %s\n", SDL_GetError()); return 1; }
        SDL_StartTextInput();
        if (numworks) printf("sim: NumWorks' simulator picture, from %s\n", skin);
        else if (!sim_skin_supported())
            printf("sim: the drawn calculator. For NumWorks' own picture: brew install sdl2_image\n"
                   "sim:   (or apt install libsdl2-image-dev), then make run-sim again\n");
        else if (skin) printf("sim: the drawn calculator (make run-sim fetches NumWorks' picture)\n");
        printf("sim: files in %s\n"
               "sim: keys: arrows, Enter = EXE, Tab = OK, Esc = BACK, Backspace = DEL, F1/Home = HOME,\n"
               "sim:   F2/End = ON/OFF, Ctrl = shift, Alt/Option = alpha; a character presses the key\n"
               "sim:   that carries it (letters: their alpha key). Or click the keys.\n", storage);
        fflush(stdout);
    }

    /* The OS, on its own stack */
    hal_stack_paint();
    pthread_attr_t attr;
    pthread_t os;
    pthread_attr_init(&attr);
    if (pthread_attr_setstack(&attr, _sstack, SIM_STACK_SIZE) != 0 ||
        pthread_create(&os, &attr, os_thread, NULL) != 0) {
        fprintf(stderr, "sim: can't start the OS thread\n");
        return 1;
    }

    uint32_t *fb = malloc(sizeof(uint32_t) * (size_t)W * (size_t)H);
    if (!fb) { perror("sim"); return 1; }
    const char *shown_port = NULL;
    key_code_t mouse_key = KEY_NONE;
    uint64_t next_frame = 0;
    while (!s_quit) {
        uint64_t now = sim_now_us();
        const char *port = sim_usb_port();
        if (win && port != shown_port) {    /* the PC transfer port, in the title */
            char title[256];
            snprintf(title, sizeof title, "NumWorks OS simulator%s%s", port ? "  -  PC transfer: " : "",
                     port ? port : "");
            SDL_SetWindowTitle(win, title);
            shown_port = port;
        }

        SDL_Event e;
        while (win && SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: s_quit = true; break;
            case SDL_KEYDOWN: case SDL_KEYUP: {
                key_code_t k = held_key(e.key.keysym.sym);
                if (k != KEY_NONE && !e.key.repeat) sim_key(k, e.type == SDL_KEYDOWN);
                break;
            }
            case SDL_TEXTINPUT: type_text(e.text.text); break;
            case SDL_WINDOWEVENT:               /* key releases go elsewhere now */
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    for (int k = KEY_NONE + 1; k < KEY_COUNT; k++)
                        if (!s_tap_down || k != (int)s_taps[s_tap_head]) sim_key((key_code_t)k, false);
                if (e.window.event == SDL_WINDOWEVENT_LEAVE) sim_window_hover(-1, -1);
                break;
            case SDL_MOUSEMOTION: sim_window_hover(e.motion.x, e.motion.y); break;
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    mouse_key = sim_window_key_at(e.button.x, e.button.y);
                    sim_key(mouse_key, true);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT && mouse_key != KEY_NONE) {
                    sim_key(mouse_key, false);
                    mouse_key = KEY_NONE;
                }
                break;
            }
        }
        run_taps(now);
        run_script(now);
        if (win && now >= next_frame) {
            next_frame = now + 16000;
            sim_window_draw(fb);
            SDL_UpdateTexture(tex, NULL, fb, W * 4);
            SDL_SetRenderDrawColor(ren, fb[0] >> 16 & 0xFF, fb[0] >> 8 & 0xFF, fb[0] & 0xFF, 255);   /* beside it */
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
        }
        SDL_Delay(2);
    }
    sim_storage_sync();
    fflush(stdout);
    if (win) SDL_Quit();
    _exit(0);                           /* the OS thread never ends by itself */
}
