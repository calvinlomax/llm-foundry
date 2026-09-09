#ifndef FOUNDRY_GUI_DIAGNOSTICS_H
#define FOUNDRY_GUI_DIAGNOSTICS_H
#include <gtk/gtk.h>

typedef struct GuiDiagnostics GuiDiagnostics;
typedef void (*GuiDiagnosticsChanged)(gboolean running, gpointer data);
GuiDiagnostics *gui_diagnostics_new(GtkWindow *parent, const char *directory,
                                    GuiDiagnosticsChanged changed, gpointer data);
void gui_diagnostics_present(GuiDiagnostics *diagnostics);
gboolean gui_diagnostics_running(GuiDiagnostics *diagnostics);
void gui_diagnostics_cancel(GuiDiagnostics *diagnostics);
/* Free only after running becomes false, when all asynchronous callbacks have finished. */
void gui_diagnostics_free(GuiDiagnostics *diagnostics);
#endif
