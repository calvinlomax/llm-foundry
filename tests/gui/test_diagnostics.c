/* Private runner state is inspected without adding testing API to the application. */
#include "../../src/gui/diagnostics.c"
#include <glib/gstdio.h>
#include <string.h>

static void drain(GuiDiagnostics *d) {
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (gui_diagnostics_running(d)) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
}
static char *contents(GuiDiagnostics *d) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->output));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}
int main(void) {
    gtk_init();
    GError *error = NULL;
    char *directory = g_dir_make_tmp("foundry diagnostics-XXXXXX", &error);
    g_assert_no_error(error);
    char *script = g_build_filename(directory, "cinder-diagnostics.command", NULL);
    GtkWindow *parent = GTK_WINDOW(gtk_window_new());
    GuiDiagnostics *d = gui_diagnostics_new(parent, directory, NULL, NULL);
    g_assert_true(g_file_set_contents(script,
        "printf 'stage one\\n'; printf '\\342'; /bin/sleep 0.1; printf '\\234\\223 done\\n'", -1, &error));
    gui_diagnostics_present(d);
    g_assert_true(d->running);
    pid_t first = d->group;
    gui_diagnostics_present(d);
    g_assert_cmpint(d->group, ==, first);
    drain(d);
    g_assert_true(d->succeeded);
    char *text = contents(d);
    g_assert_nonnull(strstr(text, "stage one\n✓ done\n"));
    g_assert_true(g_utf8_validate(text, -1, NULL));
    g_free(text);

    g_assert_true(g_file_set_contents(script, "printf 'failure detail\\n' >&2; exit 7", -1, &error));
    gui_diagnostics_present(d);
    drain(d);
    g_assert_false(d->succeeded);
    text = contents(d);
    g_assert_nonnull(strstr(text, "failure detail"));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(d->status)), "failed"));
    g_free(text);

    g_assert_true(g_file_set_contents(script,
        "i=0; while [ $i -lt 2500 ]; do printf 'log line\\n'; i=$((i+1)); done", -1, &error));
    gui_diagnostics_present(d);
    drain(d);
    g_assert_true(d->succeeded);
    g_assert_cmpint(gtk_text_buffer_get_line_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->output))), <=, 2000);

    g_assert_true(g_file_set_contents(script,
        "trap '' TERM; printf 'ready\\n'; /bin/sleep 30 & wait", -1, &error));
    gui_diagnostics_present(d);
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND * 3;
    while (TRUE) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        text = contents(d);
        gboolean ready = strstr(text, "ready") != NULL;
        g_free(text);
        if (ready) break;
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
        g_usleep(1000);
    }
    close_popup(d->window, d);
    drain(d);
    g_assert_true(d->cancelled);
    g_assert_false(gtk_widget_get_visible(GTK_WIDGET(d->window)));
    g_assert_cmpuint(d->kill_timer, ==, 0);
    gui_diagnostics_free(d);
    gtk_window_destroy(parent);
    g_unlink(script);
    g_rmdir(directory);
    g_free(script);
    g_free(directory);
    g_print("Diagnostics streaming, UTF-8, failure, bounded output, repeat-run and process-group cancellation passed.\n");
    return 0;
}
