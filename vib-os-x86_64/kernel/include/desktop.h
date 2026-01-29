/*
 * VibCode x64 - Desktop Manager Header
 */

#ifndef _DESKTOP_H
#define _DESKTOP_H

/* Initialize desktop */
void desktop_init(void);

/* Refresh desktop icons from filesystem */
void desktop_refresh(void);

/* Draw desktop icons */
void desktop_draw_icons(void);

/* Draw context menu */
void desktop_draw_context_menu(void);

/* Handle desktop mouse click (returns 1 if handled) */
int desktop_click(int mx, int my, int button);

/* Update context menu hover state */
void desktop_context_menu_hover(int mx, int my);

/* Hide context menu */
void desktop_hide_context_menu(void);

/* Check if context menu is visible */
int desktop_menu_visible(void);

#endif /* _DESKTOP_H */
