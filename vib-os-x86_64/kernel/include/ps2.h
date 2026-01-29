/*
 * PS/2 Keyboard and Mouse Driver Header
 */

#ifndef _PS2_H
#define _PS2_H

#include "types.h"

/* ===================================================================== */
/* Special Key Codes                                                     */
/* ===================================================================== */

#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_CTRL 0x104
#define KEY_SHIFT 0x105
#define KEY_ALT 0x106
#define KEY_ESC 0x107
#define KEY_F1 0x108
#define KEY_F2 0x109
#define KEY_F3 0x10A
#define KEY_F4 0x10B
#define KEY_F5 0x10C
#define KEY_F6 0x10D
#define KEY_F7 0x10E
#define KEY_F8 0x10F
#define KEY_F9 0x110
#define KEY_F10 0x111
#define KEY_F11 0x112
#define KEY_F12 0x113

/* ===================================================================== */
/* Keyboard Callback                                                     */
/* ===================================================================== */

typedef void (*keyboard_callback_t)(int key);

/* ===================================================================== */
/* API Functions                                                         */
/* ===================================================================== */

/* Initialize PS/2 controller, keyboard, and mouse */
int ps2_init(void);

/* Poll for keyboard and mouse data (non-interrupt mode) */
void ps2_poll(void);

/* Set keyboard event callback */
void ps2_set_keyboard_callback(keyboard_callback_t callback);

/* Set screen bounds for mouse clamping */
void ps2_set_screen_bounds(int width, int height);

/* Set mouse movement scaling (speed) */
void ps2_set_mouse_scale(int scale);

/* Get current mouse position and buttons */
int ps2_get_mouse_x(void);
int ps2_get_mouse_y(void);
int ps2_get_mouse_buttons(void);

/* External mouse state (for direct access) */
extern volatile int ps2_mouse_x;
extern volatile int ps2_mouse_y;
extern volatile int ps2_mouse_buttons;

#endif /* _PS2_H */
