#ifndef VIBOS_INSTALLER_GUI_H
#define VIBOS_INSTALLER_GUI_H

/* Live-media installer wizard, implemented by the desktop compositor. */
void installer_gui_open(void);
int installer_gui_is_open(void);
int installer_gui_handle_click(int x, int y, int button);
int installer_gui_handle_key(int key);
void installer_gui_draw(void);
void installer_gui_report_progress(const char *phase);

#endif /* VIBOS_INSTALLER_GUI_H */
