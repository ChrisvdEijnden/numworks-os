#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
#define WHITE 0xFFFF
#define YELLOW 0xFFE0
static uint32_t host_aircr, host_cfsr, host_hfsr, host_mmfar, host_bfar;
static uint32_t stk[256];
#define _sstack stk
#define _estack (stk + 256)
static volatile uint32_t g_tick_ms = 4321;
static jmp_buf jb; static int resets;
static void host_barrier(void) {}
static void host_reset(void) { resets++; longjmp(jb, 1); }
static char uart[4096]; static char screen[4096]; static int flushes;
void hal_uart_puts(const char *s) { strncat(uart, s, sizeof(uart)-strlen(uart)-1); }
bool display_ready(void) { return true; }
void display_fill(uint16_t c) { (void)c; }
void display_fill_rect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void display_str(int16_t x,int16_t y,const char*s,uint16_t f,uint16_t b){(void)x;(void)y;(void)f;(void)b; strncat(screen,s,sizeof(screen)-strlen(screen)-2); strcat(screen,"|");}
void display_flush(void){flushes++;}
static int key_calls;
bool keyboard_raw_any(void){ key_calls++; return key_calls > 30; }  /* released, then pressed */
__attribute__((noreturn)) void hal_panic(const char *msg);
__attribute__((noreturn)) void hal_reset(void);
__attribute__((noreturn)) void fault_report(const uint32_t *frame, uint32_t ipsr);
static int feeds; static void hal_watchdog_feed(void) { feeds++; }
#include "fault_host.c"
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)
static void reset_state(void) { uart[0]=screen[0]=0; resets=0; key_calls=0; flushes=0; host_cfsr=host_hfsr=host_bfar=host_mmfar=0; }
static int run_case(int which) {
    pid_t p = fork();
    if (p == 0) {
        reset_state();
        if (!setjmp(jb)) {
            if (which == 0) { stk[200+6]=0x90001234; stk[200+5]=0x90005679; host_cfsr=1U<<25; fault_report(stk+200, 6); }
            if (which == 1) { host_cfsr=1U<<12; host_hfsr=1U<<30; fault_report((uint32_t*)0x1FFFFFE0, 3); }
            if (which == 2) { host_cfsr=(1U<<9)|(1U<<15); host_bfar=0x60020000; fault_report(stk+100, 5); }
            if (which == 3) { hal_panic("assert foo.c:12"); }
            if (which == 4) { fault_report(stk+100, 16+54); }
            if (which == 5) { fault_report(stk+252, 6); }
            if (which == 6) { hal_panic("assert apps/text_editor/text_editor.c:1234 and then some more text"); }
        }
        printf("--- case %d uart:%s", which, uart);
        CHECK(resets == 1, "reset once");
        CHECK(flushes == 1, "screen flushed");
        CHECK(key_calls > 30, "waited for release then press");
        CHECK(strstr(screen, "Systeemfout") != NULL, "title shown");
        if (which == 0) { CHECK(strstr(uart,"UsageFault") && strstr(uart,"deling door nul") && strstr(uart,"PC:   0x90001234") && strstr(uart,"LR:   0x90005679"), "usage fault decoded"); CHECK(strstr(screen,"PC:   0x90001234")!=0, "pc on screen"); CHECK(strstr(uart,"(ms): 4321")!=0,"uptime"); }
        if (which == 1) { CHECK(strstr(uart,"HardFault") && strstr(uart,"stack vol") && strstr(uart,"SP buiten de stack: 0x1FFFFFE0") && !strstr(uart,"PC:"), "overflow: frame not read"); CHECK(strstr(uart,"HFSR: 0x40000000")!=0,"hfsr"); }
        if (which == 2) { CHECK(strstr(uart,"BusFault") && strstr(uart,"Adres: 0x60020000") && strstr(uart,"ongeldig geheugenadres"), "bus fault address"); }
        if (which == 3) { CHECK(strstr(uart,"Fout: assert foo.c:12") && strstr(uart,"Aanroeper: 0x9000ABCD"), "panic message"); }
        if (which == 4) { CHECK(strstr(uart,"Onverwachte interrupt: IRQ 54")!=0, "unexpected irq"); }
        if (which == 6) { CHECK(strstr(uart,"\n  ")!=0 && strstr(uart,"more text")!=0, "long message continues on next line"); }
        { int ok=1; for (int i=0;i<s_nlines;i++) if (strlen(s_lines[i])>44) ok=0; CHECK(ok, "every line fits the screen"); }
        if (which == 5) { CHECK(strstr(uart,"SP buiten de stack")!=0, "frame bounds checked"); }
        fflush(stdout); _exit(fails);
    }
    int st; waitpid(p, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 99;
}
int main(void) {
    int f = 0;
    for (int i = 0; i < 7; i++) f += run_case(i);
    reset_state();
    if (!setjmp(jb)) { enter(); enter(); }
    CHECK(resets == 1 && uart[0] == 0, "nested fault resets without reporting"); f += fails;
    printf(f ? "FAILURES: %d\n" : "ALL PASS\n", f);
    return f != 0;
}
