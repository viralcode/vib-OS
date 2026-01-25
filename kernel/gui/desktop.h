/*
 * Vib-OS - Desktop Manager Header
 */

#ifndef _DESKTOP_H
#define _DESKTOP_H

#include "types.h"

/* Initialize desktop manager */
void desktop_manager_init(void);

/* Refresh desktop from filesystem */
void desktop_refresh(void);

/* Sort and arrange icons */
void desktop_sort_icons(void);
void desktop_arrange_icons(void);

/* Event handling */
int desktop_handle_click(int x, int y, int button, int shift_held);
int desktop_handle_double_click(int x, int y);
int desktop_handle_key(int key);  /* Returns 1 if consumed */
int desktop_context_menu_hover(int mx, int my);
int desktop_context_menu_click(int mx, int my);
int desktop_is_renaming(void);

/* Drawing */
void desktop_draw_icons(void);

/* Dirty region tracking */
void desktop_mark_dirty(int x, int y, int w, int h);
void desktop_mark_full_redraw(void);
int desktop_needs_redraw(void);
void desktop_clear_dirty(void);

/* State queries */
int desktop_get_icon_count(void);
int desktop_is_context_menu_visible(void);

/* Context menu */
void desktop_show_context_menu(int x, int y, int on_icon);
void desktop_hide_context_menu(void);

#endif /* _DESKTOP_H */
