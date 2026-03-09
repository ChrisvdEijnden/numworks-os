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
#include "../kernel/memory.h"
#include "../micropython-port/mp_port.h"
#include <string.h>
#include <stdlib.h>

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
    static char fbuf[FFS_MAX_FILE_SIZE+1];
    if (sz > FFS_MAX_FILE_SIZE) sz = FFS_MAX_FILE_SIZE;
    flashfs_read(off, fbuf, sz);
    fbuf[sz] = 0;
    shell_puts(fbuf);
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

static void cmd_mem(void) {
    uint32_t used, free_b;
    mem_stats(&used, &free_b);
    shell_print("RAM heap:  %lu B used, %lu B free\n",
                (unsigned long)used, (unsigned long)free_b);
    flashfs_stats(&used, &free_b);
    shell_print("Flash FS:  %lu B used, %lu B free\n",
                (unsigned long)used, (unsigned long)free_b);
}

static void cmd_run(void) {
    if (s_argc < 2) { shell_puts("Usage: run <file.py>\n"); return; }
    uint32_t off, sz;
    if (flashfs_open_read(s_argv[1], &off, &sz) < 0) {
        shell_print("run: %s: not found\n", s_argv[1]); return;
    }
    static char script[FFS_MAX_FILE_SIZE+1];
    if (sz > FFS_MAX_FILE_SIZE) { shell_puts("run: file too large\n"); return; }
    flashfs_read(off, script, sz);
    script[sz] = 0;
    shell_print("Running: %s\n", s_argv[1]);
    mp_exec_str(script);
}

static void cmd_reboot(void) {
    /* SCB AIRCR software reset */
    volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0CUL;
    *aircr = (0x5FAUL << 16) | (1U << 2);
    while (1) {}
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
    {"run",    cmd_run   },
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
