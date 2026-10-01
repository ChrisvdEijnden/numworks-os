/* ================================================================
 * NumWorks OS — Photo Viewer (Foto's)
 * File: apps/photo_viewer/photo_viewer.c
 *
 * Shows uncompressed 24-bit BMP images (up to 320x216) from the USB
 * mass-storage FAT volume, converted to RGB565 line by line.
 * JPEG and PNG files are recognised but not decoded yet.
 * ================================================================ */
#include "photo_viewer.h"
#include "../../hal/display.h"
#include "../../hal/keyboard.h"
#include "../../fs/flashfs.h"
#include "../../include/config.h"
#include <string.h>
#include <stdio.h>

#include "../../usb/usb_host.h"

#define C_BG   RGB(0,0,0)
#define C_HDR  RGB(30,80,200)
#define HEADER_H 24
#define MAX_FILES 32
#define LIST_ROWS 13          /* rows between the header and the footer */
#define IMG_MAX_W LCD_WIDTH
#define IMG_MAX_H (LCD_HEIGHT - HEADER_H)

static char  s_names[MAX_FILES][32];
static int   s_nfiles = 0;
static int   s_cursor = 0;
static int   s_top = 0;       /* first file shown in the list */
static bool  s_viewing = false;
/* Images are streamed one scan line at a time into this buffer and
 * drawn straight into the framebuffer, so RAM use stays tiny. */
static uint8_t s_imgbuf[IMG_MAX_W * 3];

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Uncompressed 24-bit BMP. Every size comes from the file, so each is
 * checked before it is used; returns an error message or NULL. */
static const char *show_bmp(const char *name) {
    uint8_t hdr[54];
    uint32_t got = 0;
    if (usb_host_read_at(name, 0, hdr, sizeof(hdr), &got) < 0 || got < sizeof(hdr))
        return "Bestand te kort";
    uint32_t offset = rd32(hdr + 10);
    uint32_t dib    = rd32(hdr + 14);
    int32_t  w      = (int32_t)rd32(hdr + 18);
    int32_t  h      = (int32_t)rd32(hdr + 22);
    uint16_t bpp    = rd16(hdr + 28);
    uint32_t comp   = rd32(hdr + 30);
    if (dib < 40 || bpp != 24 || comp != 0) return "Alleen 24-bit BMP";
    bool top_down = h < 0;                  /* negative height: rows top-down */
    uint32_t rows = top_down ? (uint32_t)0 - (uint32_t)h : (uint32_t)h;
    if (w <= 0 || w > IMG_MAX_W || rows == 0 || rows > IMG_MAX_H)
        return "Max 320x216 pixels";
    uint32_t row_bytes = (uint32_t)w * 3;
    uint32_t stride    = (row_bytes + 3) & ~3U;
    if (offset < sizeof(hdr) || offset > UINT32_MAX - rows * stride)
        return "Ongeldige BMP";

    for (uint32_t y = 0; y < rows; y++) {
        uint32_t src = top_down ? y : rows - 1 - y;
        if (usb_host_read_at(name, offset + src * stride, s_imgbuf, row_bytes, &got) < 0 ||
            got < row_bytes)
            return "Bestand afgekapt";
        for (int32_t x = 0; x < w; x++) {
            const uint8_t *px = s_imgbuf + x * 3;          /* B, G, R */
            display_pixel((int16_t)x, (int16_t)(HEADER_H + y), RGB(px[2], px[1], px[0]));
        }
    }
    return NULL;
}

static void load_and_show(void) {
    const char *name = s_names[s_cursor];
    uint8_t magic[4];
    uint32_t got = 0;
    display_fill_rect(0, HEADER_H, LCD_WIDTH, LCD_HEIGHT-HEADER_H, C_BG);
    if (usb_host_read_at(name, 0, magic, sizeof(magic), &got) < 0 || got < 2) {
        display_str(40, 110, "Kan bestand niet lezen", RED, C_BG);
        return;
    }
    /* Detect format by magic bytes */
    const char *err;
    if (magic[0]=='B' && magic[1]=='M')                 err = show_bmp(name);
    else if (magic[0]==0xFF && magic[1]==0xD8)          err = "JPEG: nog geen decoder, gebruik .bmp";
    else if (got >= 4 && magic[0]==0x89 && magic[1]=='P') err = "PNG: nog geen decoder, gebruik .bmp";
    else                                                err = "Onbekend formaat";
    if (err) display_str(20, 110, err, RED, C_BG);
}

static void draw_file_list(void) {
    display_fill(C_BG);
    display_fill_rect(0,0,LCD_WIDTH,HEADER_H,C_HDR);
    display_str(8,6,"Foto's",WHITE,C_HDR);
    if (!usb_host_mounted()) {
        display_str(20, 60, "Geen USB schijf gevonden.", YELLOW, C_BG);
        display_str(20, 76, "Sluit USB-C schijf aan.", RGB(180,180,180), C_BG);
        return;
    }
    s_nfiles = usb_host_ls(s_names, MAX_FILES);
    if (s_nfiles == 0) {
        display_str(20, 60, "Geen afbeeldingen op USB.", RGB(180,180,180), C_BG);
        return;
    }
    /* Scroll so the cursor stays in view */
    if (s_cursor >= s_nfiles) s_cursor = s_nfiles - 1;
    if (s_cursor < s_top) s_top = s_cursor;
    if (s_cursor >= s_top + LIST_ROWS) s_top = s_cursor - LIST_ROWS + 1;
    for (int r = 0; r < LIST_ROWS && s_top + r < s_nfiles; r++) {
        int i = s_top + r;
        bool sel = (i == s_cursor);
        uint16_t bg = sel ? RGB(50,80,180) : C_BG;
        uint16_t fg = sel ? WHITE : RGB(200,220,255);
        display_fill_rect(0, HEADER_H+r*15, LCD_WIDTH, 15, bg);
        display_str(6, HEADER_H+r*15+3, s_names[i], fg, bg);
    }
    if (s_top > 0)
        display_str(LCD_WIDTH-12, HEADER_H+3, "^", YELLOW, C_BG);
    if (s_top + LIST_ROWS < s_nfiles)
        display_str(LCD_WIDTH-12, HEADER_H+(LIST_ROWS-1)*15+3, "v", YELLOW, C_BG);
    display_str(4, LCD_HEIGHT-12, "EXE:Openen  HOME:Terug", YELLOW, C_BG);
}

void photo_viewer_redraw(void) {
    if (s_viewing) {
        display_fill_rect(0,0,LCD_WIDTH,HEADER_H,C_HDR);
        display_str(8,6,s_names[s_cursor],WHITE,C_HDR);
        display_str(LCD_WIDTH-66,6,"BACK:Lijst",RGB(200,220,255),C_HDR);
        load_and_show();
    } else {
        draw_file_list();
    }
}

void photo_viewer_init(void) { s_cursor=0; s_top=0; s_viewing=false; s_nfiles=0; }

void photo_viewer_handle_event(const kernel_event_t *ev) {
    if (ev->action != 0) return;
    key_code_t k = (key_code_t)ev->key;

    if (k==KEY_HOME || k==KEY_BACK) {
        if (s_viewing) { s_viewing=false; draw_file_list(); }
        else kernel_set_app(APP_HOME);
        return;
    }
    if (!s_viewing) {
        if (k==KEY_UP && s_cursor>0) { s_cursor--; draw_file_list(); }
        else if (k==KEY_DOWN && s_cursor<s_nfiles-1) { s_cursor++; draw_file_list(); }
        else if (key_is_exe(k) && s_nfiles>0) { s_viewing=true; photo_viewer_redraw(); }
    }
}
