/* ================================================================
 * NumWorks OS — Shell Commands
 * File: shell/commands.c
 *
 * Commands: help ls cat touch rm mkdir echo run mem reboot fm
 * Each command is a small static function.
 * Code size target: < 6 KB
 * ================================================================ */
#include "commands.h"
#include "shell.h"
#include "../fs/flashfs.h"
#include "../kernel/kernel.h"
#include "../hal/hal.h"
#include "../micropython-port/mp_port.h"
#include "../include/string.h"
#include "../include/stdlib.h"

/* ── Argument parsing (no malloc) ────────────────────────────── */
#define MAX_ARGS 8
static char *s_argv[MAX_ARGS];
static int   s_argc;
static char  s_linebuf[SHELL_LINE_LEN+1];

static void parse_args(const char *line) {
    strncpy(s_linebuf, line, SHELL_LINE_LEN);
    s_argc = 0;
    char *p = s_linebuf;
    while (*p && s_argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        s_argv[s_argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
}

/* ── Individual commands ─────────────────────────────────────── */
static void cmd_help(void) {
    shell_puts("Commands:\n");
    shell_puts("  ls              list files\n");
    shell_puts("  cat <file>      print file\n");
    shell_puts("  touch <file>    create empty file\n");
    shell_puts("  rm <file>       delete file\n");
    shell_puts("  mkdir <dir>     create directory (alias)\n");
    shell_puts("  echo <text>     print text\n");
    shell_puts("  run <file.py>   execute Python script\n");
    shell_puts("  mem             show memory usage\n");
    shell_puts("  fm              open file manager\n");
    shell_puts("  reboot          restart system\n");
}

static void ls_cb(const ffs_entry_t *e, void *ctx) {
    (void)ctx;
    shell_print("  %-20s  %5lu B\n", e->name, (unsigned long)e->size);
}

static void cmd_ls(void) {
    int n = flashfs_ls(ls_cb, NULL);
    if (n == 0) shell_puts("  (empty)\n");
    uint32_t used, free_b;
    flashfs_stats(&used, &free_b);
    shell_print("  %lu KB used, %lu KB free\n",
                (unsigned long)(used/1024), (unsigned long)(free_b/1024));
}

static void cmd_cat(void) {
    if (s_argc < 2) { shell_puts("Usage: cat <file>\n"); return; }
    uint32_t off, sz;
    if (flashfs_open_read(s_argv[1], &off, &sz) < 0) {
        shell_print("cat: %s: not found\n", s_argv[1]); return;
    }
    /* In pieces: no buffer for the whole file */
    char chunk[129];
    for (uint32_t pos = 0; pos < sz; ) {
        uint32_t n = sz - pos < 128 ? sz - pos : 128;
        if (flashfs_read(off + pos, chunk, n) != (int)n) { shell_puts("\ncat: read error"); break; }
        chunk[n] = 0;
        shell_puts(chunk);
        pos += n;
    }
    shell_putc('\n');
}

static void cmd_touch(void) {
    if (s_argc < 2) { shell_puts("Usage: touch <file>\n"); return; }
    if (flashfs_exists(s_argv[1])) { return; }
    flashfs_write(s_argv[1], "", 0);
    shell_print("Created: %s\n", s_argv[1]);
}

static void cmd_rm(void) {
    if (s_argc < 2) { shell_puts("Usage: rm <file>\n"); return; }
    if (flashfs_delete(s_argv[1]) < 0)
        shell_print("rm: %s: not found\n", s_argv[1]);
    else
        shell_print("Deleted: %s\n", s_argv[1]);
}

static void cmd_echo(void) {
    for (int i = 1; i < s_argc; i++) {
        shell_puts(s_argv[i]);
        if (i + 1 < s_argc) shell_putc(' ');
    }
    shell_putc('\n');
}

static void count_cb(const ffs_entry_t *e, void *ctx) { (void)e; (*(int *)ctx)++; }

static void cmd_mem(void) {
    uint32_t a, b;
    hal_stack_stats(&a, &b);
    shell_print("Stack:  %lu / %lu B (piek)\n", (unsigned long)a, (unsigned long)b);
    hal_heap_stats(&a, &b);
    shell_print("C-heap: %lu / %lu B\n", (unsigned long)a, (unsigned long)b);
    if (mp_heap_stats(&a, &b))
        shell_print("Python: %lu / %lu B\n", (unsigned long)a, (unsigned long)b);
    else
        shell_puts("Python: niet ingebouwd\n");
    if (flashfs_mounted()) {
        int n = 0;
        flashfs_ls(count_cb, &n);
        flashfs_stats(&a, &b);
        shell_print("Flash:  %lu / %lu B, %d bestand(en)\n",
                    (unsigned long)a, (unsigned long)(a + b), n);
    } else {
        shell_puts("Flash:  geen opslag\n");
    }
}

static void cmd_run_script(void) {
    if (s_argc < 2) { shell_puts("Usage: run <file.py>\n"); return; }
    if (!flashfs_exists(s_argv[1])) {
        shell_print("run: %s: not found\n", s_argv[1]); return;
    }
    shell_print("Running: %s\n", s_argv[1]);
    mp_set_console(NULL);                 /* output and input() in the shell */
    mp_exec_file(s_argv[1]);
    mp_pause_after_graphics();
    kernel_request_redraw();
}

static void cmd_reboot(void) {
    hal_reset();
}

static void cmd_fm(void) {
    kernel_set_app(APP_FILEMANAGER);
}

/* ── Command dispatch table ──────────────────────────────────── */
typedef struct { const char *name; void (*fn)(void); } cmd_entry_t;
static const cmd_entry_t s_cmds[] = {
    {"help",   cmd_help  },
    {"ls",     cmd_ls    },
    {"cat",    cmd_cat   },
    {"touch",  cmd_touch },
    {"rm",     cmd_rm    },
    {"mkdir",  cmd_touch },  /* Alias — flat FS, dirs are prefix convention */
    {"echo",   cmd_echo  },
    {"run",    cmd_run_script },
    {"mem",    cmd_mem   },
    {"fm",     cmd_fm    },
    {"reboot", cmd_reboot},
    {NULL, NULL}
};

void cmd_run(const char *line) {
    parse_args(line);
    if (s_argc == 0) return;
    for (const cmd_entry_t *e = s_cmds; e->name; e++) {
        if (strcmp(s_argv[0], e->name) == 0) {
            e->fn();
            return;
        }
    }
    shell_print("Unknown command: %s\n", s_argv[0]);
}
