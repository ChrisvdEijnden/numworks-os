#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "py/stackctrl.h"
#include "mp_port.h"
#include "../../hal/keyboard.h"
#include "../../kernel/kernel.h"
/* linker-provided regions, faked: 48 KB heap like the firmware. The
 * end symbols are set in assembly, where C names carry the platform's
 * prefix ("_" on macOS, none on Linux). */
#define STR_(x) #x
#define STR(x) STR_(x)
#define ASM_NAME(n) STR(__USER_LABEL_PREFIX__) #n
uint8_t _smp_heap[48 * 1024] __attribute__((aligned(16)));
__asm__(".globl " ASM_NAME(_emp_heap) "\n.set " ASM_NAME(_emp_heap) ", " ASM_NAME(_smp_heap) " + 49152\n");
uint8_t _sstack[16];
__asm__(".globl " ASM_NAME(_estack) "\n.set " ASM_NAME(_estack) ", " ASM_NAME(_sstack) " + 16384\n");
/* app API */
void python_app_init(void); void python_app_handle_event(const kernel_event_t *ev); void python_app_redraw(void);
void pa_type(const char *t); const char *pa_line(void); bool pa_cont(void); void pa_output(char *buf, int max); void pa_clear(void);
/* ── platform stubs ── */
static uint32_t now_ms = 1000;
uint32_t hal_tick_ms(void) { return now_ms++; }   /* time passes as code runs */
uint32_t hal_tick_us(void) { return now_ms * 1000u + 7; }
static int delays_ms = 0;
void hal_delay_ms(uint32_t ms) { now_ms += ms; delays_ms += ms; }
void hal_panic(const char *m) { printf("PANIC %s\n", m); exit(2); }
int hal_uart_getc(void) { return -1; }
static char shell_out[8192];
void shell_puts(const char *s) { strncat(shell_out, s, sizeof shell_out - strlen(shell_out) - 1); }
static int shell_shows = 0; void shell_show(void) { shell_shows++; }
static const char *shell_answer = NULL;
bool shell_read_line(char *buf, int max) { if (!shell_answer) return false; snprintf(buf, max, "%s", shell_answer); return true; }
/* a framebuffer, so kandinsky.get_pixel() can be checked */
static uint16_t fb[240][320];
static char last_str[64]; static uint16_t last_fg, last_bg;
void display_fill(uint16_t c) { for (int y = 0; y < 240; y++) for (int x = 0; x < 320; x++) fb[y][x] = c; }
void display_str(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg) { (void)x;(void)y; if (strcmp(s, "Hallo") && strcmp(s, "x")) return; /* only what the test draws */ snprintf(last_str, sizeof last_str, "%s", s); last_fg = fg; last_bg = bg; }
void display_pixel(int16_t x, int16_t y, uint16_t c) { if (x >= 0 && y >= 0 && x < 320 && y < 240) fb[y][x] = c; }
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) display_pixel(i, j, c); }
uint16_t display_get_pixel(int16_t x, int16_t y) { return (x >= 0 && y >= 0 && x < 320 && y < 240) ? fb[y][x] : 0; }
/* held keys, for ion.keydown() */
static key_code_t held = KEY_NONE;
bool keyboard_is_pressed(key_code_t k) { return k == held; }
void hal_delay_us(uint32_t us) { (void)us; }
static int flushes = 0; void display_flush(void) { flushes++; }
void kernel_set_app(app_state_t a) { (void)a; }
void kernel_request_redraw(void) {}
/* scripted keyboard */
static key_code_t keyq[64]; static int kq_n = 0, kq_pos = 0;
static void keys(const key_code_t *k, int n) { for (int i = 0; i < n; i++) keyq[kq_n++] = k[i]; }
bool keyboard_poll(key_event_t *ev) {
    if (kq_pos < kq_n) { ev->key = keyq[kq_pos++]; ev->action = 0; return true; }
    return false;
}
/* the real file system, on simulated flash (../ffs3/storage_sim.c) */
#include "../../fs/flashfs.h"
static const struct { const char *name, *text; } files[] = {
    { "mymod.py", "X = 42\ndef f():\n    return X + 1\nprint('mymod geladen')\n" },
    { "helper.py", "import mymod\ndef twice(v):\n    return 2 * v + mymod.X\n" },
    { "prog.py", "import helper\nprint('prog', helper.twice(4))\n" },
    { "bad.py", "def oops(:\n" },
    { "notes.txt", "regel 1\nregel 2\n" },
};
static void make_files(void) {
    flashfs_init(); flashfs_format();
    for (unsigned i = 0; i < sizeof files / sizeof files[0]; i++)
        flashfs_write(files[i].name, files[i].text, (uint32_t)strlen(files[i].text));
}
static int file_is(const char *name, const char *want) {
    uint32_t off, sz; static char b[9000];
    if (flashfs_open_read(name, &off, &sz) || sz != strlen(want) || flashfs_read(off, b, sz) != (int)sz) return 0;
    return !memcmp(b, want, sz);
}
static int fails = 0;
static void check(int ok, const char *what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static void ev(key_code_t k) { kernel_event_t e = { .key = k, .action = 0 }; python_app_handle_event(&e); }
static void line(const char *t) { pa_type(t); ev(KEY_EXE); }
static char out[8192];
static const char *output(void) { pa_output(out, sizeof out); return out; }
static void repl(const char *code) { pa_clear(); line(code); }
/* an entry that draws: the drawing stays up until a key is pressed */
static void draw_repl(const char *code) { key_code_t k[] = { KEY_OK }; keys(k, 1); repl(code); }

/* MicroPython on the host's own stack, not the firmware's */
static int *stack_top;
static void python_start(void) {
    mp_init_port();
    mp_stack_set_top(stack_top);
    mp_stack_set_limit(256 * 1024);
}

int main(void) {
    int marker;
    stack_top = &marker;
    setvbuf(stdout, NULL, _IONBF, 0);
    make_files();
    python_start();
    python_app_init();

    puts("multi-line REPL:");
    pa_clear();
    line("for i in range(3):");
    check(pa_cont() && !strcmp(pa_line(), "    "), "block opens: '... ' prompt, next line indented 4");
    line("print(i * 2)");
    check(pa_cont() && !strcmp(pa_line(), "    "), "still in the block, indent kept");
    ev(KEY_EXE);                                     /* indentation only = empty line */
    check(!pa_cont() && strstr(output(), "\n0\n2\n4\n"), "empty line runs the block: 0 2 4");
    check(strstr(output(), ">>> for i in range(3):\n...     print(i * 2)\n") != NULL, "block echoed with >>> and ... prompts");
    pa_clear();
    line("def sign(x):");
    line("if x < 0:");
    check(!strcmp(pa_line(), "        "), "nested block indents to 8");
    line("return -1");
    check(!strcmp(pa_line(), "        "), "inside nested block: 8");
    ev(KEY_BACKSPACE);
    check(!strcmp(pa_line(), "    "), "backspace on indentation removes one level");
    line("return 1");
    ev(KEY_BACKSPACE); ev(KEY_EXE);
    repl("sign(-5), sign(3)");
    check(strstr(output(), "(-1, 1)") != NULL, "function defined over several lines works");
    pa_clear();
    pa_type("print('x' * 200)"); { char big[200]; memset(big, 'y', sizeof big); big[199] = 0; pa_type(big); }
    check(strlen(pa_line()) == 127, "input line stops at 127 characters");
    { for (int i = 0; i < 127; i++) ev(KEY_BACKSPACE); }
    line("x = [1,");
    check(pa_cont() && !strcmp(pa_line(), ""), "open bracket continues (no indent)");
    line("2]");
    repl("x");
    check(strstr(output(), "[1, 2]") != NULL, "bracket continued over two lines");
    repl("7 * 6");
    check(strstr(output(), "\n42\n") != NULL, "expression value printed");
    repl("1/0");
    check(strstr(output(), "ZeroDivisionError") != NULL, "error shown");
    pa_clear(); ev(KEY_UP);
    check(!strcmp(pa_line(), "1/0"), "UP recalls the previous line");
    ev(KEY_BACKSPACE); ev(KEY_BACKSPACE); ev(KEY_BACKSPACE); ev(KEY_BACKSPACE);

    puts("live output and input():");
    int f0 = flushes;
    repl("import time");
    line("for i in range(4):");
    line("print('stap', i)");
    line("time.sleep_ms(100)");
    ev(KEY_EXE);
    check(flushes - f0 >= 4, "output pushed to the screen while the loop runs");
    check(strstr(output(), "stap 3") != NULL, "all output arrived");
    { key_code_t k[] = { KEY_ALPHA, KEY_EXP, KEY_LN, KEY_EXE }; keys(k, 4); }   /* a b */
    repl("naam = input('naam? ')");
    repl("print('hoi', naam)");
    check(strstr(output(), "hoi ab") != NULL, "input() reads a line from the keypad");
    { key_code_t k[] = { KEY_ALPHA, KEY_EXP, KEY_LN, KEY_EXE }; keys(k, 4); }
    pa_clear(); line("v = input('naam? ')");
    check(strstr(output(), "naam? ab\n") != NULL, "prompt and answer echoed on one line");
    { key_code_t k[] = { KEY_1, KEY_BACK }; keys(k, 2); }
    repl("input('getal: ')");
    check(strstr(output(), "KeyboardInterrupt") != NULL, "BACK cancels input() with KeyboardInterrupt");
    { key_code_t k[] = { KEY_BACK }; keys(k, 1); }
    repl("time.sleep(10)");
    check(strstr(output(), "KeyboardInterrupt") != NULL && delays_ms < 10000 + 50000, "BACK interrupts time.sleep");

    puts("import from flash:");
    repl("import mymod");
    check(strstr(output(), "mymod geladen") != NULL, "import mymod loads mymod.py");
    repl("mymod.f()");
    check(strstr(output(), "43") != NULL, "module function callable");
    repl("import mymod");
    check(strstr(output(), "geladen") == NULL, "second import uses the loaded module");
    repl("import helper; helper.twice(4)");
    check(strstr(output(), "50") != NULL, "module importing another module");
    repl("import nosuch");
    check(strstr(output(), "ImportError") != NULL, "missing module: ImportError");
    repl("import bad");
    check(strstr(output(), "SyntaxError") != NULL, "module with syntax error reported");
    ev(KEY_HOME);                                    /* leave the app ... */
    repl("import mymod");                            /* ... and come back */
    check(strstr(output(), "mymod geladen") != NULL, "after leaving the app, import reads the file again");
    repl("print(open('notes.txt').read().split()[1])");
    check(strstr(output(), "1") != NULL, "open() still works");

    puts("time module:");
    repl("t0 = time.ticks_ms(); time.sleep(0.25); d = time.ticks_diff(time.ticks_ms(), t0); print(250 <= d < 300)");
    check(strstr(output(), "True") != NULL, "sleep(0.25) waits 250 ms");
    repl("print(time.ticks_diff(time.ticks_add(5, -10), 5), time.ticks_add(100, 0) == 100)");
    check(strstr(output(), "-10 True") != NULL, "ticks_add / ticks_diff");
    repl("M = (1 << 30) - 1; print(time.ticks_diff(time.ticks_add(M, 3), M))");
    check(strstr(output(), "\n3\n") != NULL, "ticks_diff across the wrap");
    repl("print(type(time.time()) is int, time.monotonic() > 0, time.ticks_us() >= 0)");
    check(strstr(output(), "True True True") != NULL, "time(), monotonic(), ticks_us()");

    puts("random module:");
    repl("import random; random.seed(1); a = [random.randint(1, 6) for _ in range(20)]");
    repl("random.seed(1); b = [random.randint(1, 6) for _ in range(20)]");
    repl("print(a == b, min(a) >= 1, max(a) <= 6, len(set(a)) > 2)");
    check(strstr(output(), "True True True True") != NULL, "seed() repeats, randint in range and varied");
    repl("r = [random.random() for _ in range(200)]; print(min(r) >= 0, max(r) < 1, max(r) - min(r) > 0.5)");
    check(strstr(output(), "True True True") != NULL, "random() in [0, 1) and spread");
    repl("p = (random.randrange(0, 10, 2) % 2, 2 <= random.uniform(2, 3) < 3)");
    repl("print(*p, random.choice('xyz') in 'xyz', random.getrandbits(8) < 256, random.randrange(5) < 5)");
    check(strstr(output(), "0 True True True True") != NULL, "randrange step, uniform, choice, getrandbits");
    repl("random.randint(5, 4)");
    check(strstr(output(), "ValueError") != NULL, "empty range: ValueError");
    repl("random.choice([])");
    check(strstr(output(), "IndexError") != NULL, "choice([]): IndexError");

    puts("robustness:");
    repl("x = [0] * 100000");
    check(strstr(output(), "MemoryError") != NULL, "MemoryError, not a crash");
    repl("s = 0");
    line("for i in range(3000):");
    line("t = str(i) * 20");
    line("s += len(t)"); ev(KEY_EXE);
    repl("s");
    check(strstr(output(), "217800") != NULL, "garbage collector survives 3000 allocations");
    { static key_code_t k[1] = { KEY_BACK }; kq_n = kq_pos = 0; }
    /* BACK arrives while the loop runs: the VM hook polls the keyboard */
    { key_code_t k[] = { KEY_BACK }; keys(k, 1); }
    pa_clear(); line("while True:"); line("pass"); ev(KEY_EXE);
    check(strstr(output(), "KeyboardInterrupt") != NULL, "BACK stops an endless loop");
    repl("print('nog steeds', 1 + 1)");
    check(strstr(output(), "nog steeds 2") != NULL, "REPL works after an interrupt");

    puts("writing files and os:");
    repl("f = open('uit.txt', 'w'); f.write('regel A\\n'); f.write('regel B\\n'); f.close()");
    check(file_is("uit.txt", "regel A\nregel B\n"), "open('w'), write, close");
    repl("with open('uit.txt', 'a') as f: f.write('regel C\\n')");
    check(pa_cont(), "one-line with-block waits for an empty line, like CPython");
    ev(KEY_EXE);
    check(file_is("uit.txt", "regel A\nregel B\nregel C\n"), "append with 'a' and with-block");
    repl("print(open('uit.txt').read().count('regel'))");
    check(strstr(output(), "\n3\n") != NULL, "reading back what was written");
    repl("open('bin.dat', 'wb').write(bytes([0, 1, 2, 255]))");
    repl("print(list(open('bin.dat', 'rb').read()))");
    check(strstr(output(), "[0, 1, 2, 255]") != NULL, "binary write and read");
    pa_clear(); line("g = open('half.txt', 'w'); g.write('deel 1')");
    check(file_is("half.txt", "deel 1"), "REPL: what an entry wrote is saved, file stays open");
    line("g.write(', deel 2')");
    check(file_is("half.txt", "deel 1, deel 2"), "later entries add to the same file");
    mp_exec_str("h = open('script.txt', 'w')\nh.write('a')\nimport os\nprint('script.txt' in os.listdir())\n");
    check(strstr(shell_out, "False") != NULL, "in a script, a file is saved on close (or at the end)");
    check(file_is("script.txt", "a"), "... and at the end of the script it is");
    ev(KEY_HOME);
    check(file_is("half.txt", "deel 1, deel 2"), "leaving the app closes and saves open files");
    repl("f = open('mid.txt', 'w'); f.write('y' * 12000); f.close(); print(len(open('mid.txt').read()))");
    check(strstr(output(), "12000") != NULL, "a 12 000-byte file written from Python (over the old 8 KB)");
    /* The limit, on a fresh heap: after everything above, the 48 KB heap is
     * too fragmented for a write buffer that grows to 16 KB */
    mp_deinit_port();
    python_start();
    repl("big = open('big.txt', 'w'); [big.write('x' * 1024) for i in range(17)]");
    check(strstr(output(), "OSError: 28") != NULL, "writing more than 16 KB from Python: OSError (ENOSPC)");
    repl("big.close(); print(len(open('big.txt').read()))");
    check(strstr(output(), "\n16384\n") != NULL, "... and the first 16 KB were kept and saved");
    repl("import os; d = os.listdir(); print(len(d), 'uit.txt' in d, 'mymod.py' in d)");
    check(strstr(output(), "True True") != NULL, "os.listdir()");
    repl("print(os.stat('uit.txt')[6])");
    check(strstr(output(), "\n24\n") != NULL, "os.stat size");
    repl("os.rename('uit.txt', 'nieuw.txt'); print('nieuw.txt' in os.listdir(), 'uit.txt' in os.listdir())");
    check(strstr(output(), "True False") != NULL, "os.rename");
    repl("os.rename('nieuw.txt', 'notes.txt')");
    check(strstr(output(), "OSError") != NULL, "rename onto an existing file refused");
    repl("os.remove('nieuw.txt'); os.remove('nieuw.txt')");
    check(strstr(output(), "ENOENT") != NULL || strstr(output(), "OSError") != NULL, "os.remove, then ENOENT");
    repl("open('nope.txt', 'x')");
    check(strstr(output(), "ValueError") != NULL, "unsupported mode refused");
    repl("fs = [open('m%d.txt' % i, 'w') for i in range(5)]");
    check(strstr(output(), "OSError") != NULL, "at most 4 files open for writing");
    ev(KEY_HOME);

    puts("scripts from the shell:");
    shell_out[0] = 0;
    mp_set_console(NULL);
    check(mp_exec_file("prog.py") && strstr(shell_out, "mymod geladen") && strstr(shell_out, "prog 50"), "run prog.py: output in the shell, modules re-imported");
    mp_exec_str("log = open('log.txt', 'w')\nlog.write('vergeten te sluiten')\n");
    check(file_is("log.txt", "vergeten te sluiten"), "a file a script leaves open is saved when it ends");
    check(!mp_exec_file("nope.py"), "missing file reported");
    shell_out[0] = 0; shell_answer = "7";
    mp_exec_str("print(int(input('n? ')) * 3)");
    check(strstr(shell_out, "n? 7\n21") != NULL, "input() in the shell");
    puts("graphics:");
    int shows = shell_shows; f0 = flushes;
    { key_code_t k[] = { KEY_5 }; keys(k, 1); }
    mp_exec_str("import display\ndisplay.fill(display.RED)\nprint('na tekenen')\ndisplay.flush()\n");
    check(shell_shows == shows, "console doesn't paint over a drawing");
    int kp = kq_pos;
    mp_pause_after_graphics();
    check(kq_pos == kp + 1 && flushes > f0, "drawing stays until a key is pressed");
    kp = kq_pos; mp_pause_after_graphics();
    check(kq_pos == kp, "no pause without drawing");

    puts("kandinsky and ion (NumWorks' own modules):");
    repl("import kandinsky as k");
    draw_repl("k.fill_rect(10, 10, 5, 5, (255, 0, 0)); print(k.get_pixel(12, 12))");
    check(strstr(output(), "(255, 0, 0)") != NULL, "fill_rect with an (r, g, b) tuple, get_pixel reads it back");
    draw_repl("k.set_pixel(1, 2, '#00ff00'); print(k.get_pixel(1, 2), k.get_pixel(0, 0) == k.get_pixel(1, 2))");
    check(strstr(output(), "(0, 255, 0) False") != NULL, "set_pixel with '#rrggbb'");
    draw_repl("k.set_pixel(3, 3, 'blue'); print(k.get_pixel(3, 3))");
    check(strstr(output(), "(0, 0, 255)") != NULL, "colour names");
    repl("print(k.color(300, -5, 128), k.color('white'))");
    check(strstr(output(), "(255, 0, 128) (255, 255, 255)") != NULL, "color() clamps to 0..255, accepts a name");
    draw_repl("k.draw_string('Hallo', 0, 0)");
    check(!strcmp(last_str, "Hallo") && last_fg == 0x0000 && last_bg == 0xFFFF, "draw_string: black on white by default");
    draw_repl("k.draw_string('x', 0, 0, 'red', (0, 0, 0))");
    check(last_fg == 0xF800 && last_bg == 0, "draw_string with colours");
    draw_repl("k.fill_rect(0, 0, 1, 1, 'mauve')");
    check(strstr(output(), "ValueError") != NULL, "an unknown colour: ValueError");
    repl("import ion; print(ion.KEY_OK, ion.KEY_ONOFF, ion.KEY_EXE)");
    check(strstr(output(), "4 8 52") != NULL, "ion key numbers are NumWorks' (OK 4, ON/OFF 8, EXE 52)");
    held = KEY_OK;
    repl("print(ion.keydown(ion.KEY_OK), ion.keydown(ion.KEY_UP))");
    check(strstr(output(), "True False") != NULL, "keydown: True only for the held key");
    held = KEY_SIN;
    repl("print(ion.keydown(ion.KEY_SINE), ion.keydown(7), ion.keydown(99))");
    check(strstr(output(), "True False False") != NULL, "KEY_SINE maps to sin; gaps and out-of-range keys are False");
    held = KEY_NONE;

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
