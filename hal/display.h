#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../include/config.h"
#include "font.h"

/* RGB565 colour helpers */
#define RGB(r,g,b) ((uint16_t)(((r)&0xF8)<<8 | ((g)&0xFC)<<3 | (b)>>3))
#define BLACK   RGB(0,0,0)
#define WHITE   RGB(255,255,255)
#define RED     RGB(220,20,20)
#define GREEN   RGB(20,200,20)
#define BLUE    RGB(20,80,220)
#define YELLOW  RGB(240,200,0)
#define GREY    RGB(100,100,100)
#define DKGREY  RGB(40,40,40)
#define CYAN    RGB(0,200,220)

void display_init(void);
bool display_ready(void);      /* true once display_init() has run */
uint16_t display_get_pixel(int16_t x, int16_t y);   /* 0 outside the screen */
void display_power(bool on);   /* panel off (powered down) / on again */
void display_flush(void);   /* push the whole framebuffer to the LCD now */
void display_update(void);  /* push it only if something was drawn since */

/* Drawing primitives */
void display_fill(uint16_t colour);
void display_pixel(int16_t x, int16_t y, uint16_t colour);
void display_hline(int16_t x, int16_t y, int16_t w, uint16_t colour);
void display_vline(int16_t x, int16_t y, int16_t h, uint16_t colour);
void display_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t colour);
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t colour);
void display_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg);
void display_str(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg);
/* Smoothed text in one of the fonts in font.h, on one line, its edges
 * blended into bg */
void display_text(int16_t x, int16_t y, const char *s, const font_t *f, uint16_t fg, uint16_t bg);
void display_text_n(int16_t x, int16_t y, const char *s, int len, const font_t *f,
                    uint16_t fg, uint16_t bg);
int  display_text_width(const char *s, const font_t *f);
/* w x h pixels, row by row */
void display_image(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *px);
void display_str_len(int16_t x, int16_t y, const char *s, int len, uint16_t fg, uint16_t bg);

/* Screen buffer — write here, call display_flush() to push */
extern uint16_t g_framebuf[LCD_WIDTH * LCD_HEIGHT];
#define FB_PIX(x,y) g_framebuf[(y)*LCD_WIDTH+(x)]
