#pragma once
#include "../../kernel/kernel.h"

/* The Games menu (Tetris, Snake, 2048) */
void games_init(void);
void games_redraw(void);
void games_handle_event(const kernel_event_t *ev);

/* Snake */
void snake_init(void);
void snake_redraw(void);
void snake_handle_event(const kernel_event_t *ev);
void snake_tick(void);

/* 2048 */
void g2048_init(void);
void g2048_redraw(void);
void g2048_handle_event(const kernel_event_t *ev);

/* Game rules, for tests */
#define SNAKE_W 32
#define SNAKE_H 21
typedef enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT } dir_t;
/* 2048: board[r][c] holds the tile's exponent (1 = 2, 2 = 4, ...), 0 empty.
 * Slides the board; returns the points scored, or -1 if nothing moved. */
int g2048_slide(uint8_t board[4][4], dir_t d);
bool g2048_can_move(uint8_t board[4][4]);
