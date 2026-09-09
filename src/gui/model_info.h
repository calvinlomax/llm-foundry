#ifndef FOUNDRY_GUI_MODEL_INFO_H
#define FOUNDRY_GUI_MODEL_INFO_H
#include <gtk/gtk.h>
GtkWidget *gui_model_info_new(void);
void gui_model_info_clear(GtkWidget *panel);
void gui_model_info_show(GtkWidget *panel, const char *json);
#endif
