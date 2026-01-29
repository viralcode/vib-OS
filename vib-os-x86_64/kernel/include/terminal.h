/*
 * VibCode x64 - Terminal Header
 */

#ifndef _TERMINAL_H
#define _TERMINAL_H

#include "types.h"

/* Forward declaration */
typedef struct terminal terminal_t;

/* Create terminal at position */
terminal_t *term_create(int x, int y);

/* Render terminal */
void term_render(terminal_t *term);

/* Handle keyboard input */
void term_handle_key(terminal_t *term, int key);

/* Output */
void term_putc(terminal_t *term, char c);
void term_puts_t(terminal_t *term, const char *str);
void term_prompt(terminal_t *term);

/* Execute command */
void term_execute(terminal_t *term, const char *cmd);

/* Active terminal */
terminal_t *term_get_active(void);
void term_set_active(terminal_t *term);

#endif /* _TERMINAL_H */
