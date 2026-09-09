#define _POSIX_C_SOURCE 200809L
#include "diagnostics.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

struct GuiDiagnostics {
    GtkWindow *window;
    GtkWidget *output, *scroll, *status, *spinner, *action;
    GSubprocess *process;
    GInputStream *stream;
    GByteArray *pending;
    char *directory;
    GuiDiagnosticsChanged changed;
    gpointer data;
    pid_t group;
    guint kill_timer;
    gboolean running, read_done, wait_done, cancelled, succeeded, hide_when_done;
};
static void output(GuiDiagnostics *d, const char *text, gssize length) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->output));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, length);
    /* Bound the popup's memory. The command retains the full log on disk. */
    int lines = gtk_text_buffer_get_line_count(buffer);
    if (lines > 2000) {
        GtkTextIter start, cut;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_iter_at_line(buffer, &cut, lines - 2000);
        gtk_text_buffer_delete(buffer, &start, &cut);
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(d->output), &end, 0, FALSE, 0, 1);
}
static void finish(GuiDiagnostics *d) {
    if (!d->read_done || !d->wait_done || (d->cancelled && d->kill_timer))
        return;
    if (d->kill_timer) {
        g_source_remove(d->kill_timer);
        d->kill_timer = 0;
    }
    d->running = FALSE;
    d->group = 0;
    g_clear_object(&d->stream);
    g_clear_object(&d->process);
    gtk_spinner_stop(GTK_SPINNER(d->spinner));
    gtk_widget_set_visible(d->spinner, FALSE);
    gtk_label_set_text(GTK_LABEL(d->status), d->cancelled ? "Diagnostics cancelled" :
                       d->succeeded ? "All diagnostics passed" : "Diagnostics failed — see output below");
    gtk_button_set_label(GTK_BUTTON(d->action), "Close");
    gtk_widget_set_sensitive(d->action, TRUE);
    if (d->hide_when_done)
        gtk_widget_set_visible(GTK_WIDGET(d->window), FALSE);
    if (d->changed)
        d->changed(FALSE, d->data);
}
static void read_output(GObject *source, GAsyncResult *result, gpointer data) {
    GuiDiagnostics *d = data;
    GError *error = NULL;
    GBytes *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
    gsize length = 0;
    const guint8 *chunk = bytes ? g_bytes_get_data(bytes, &length) : NULL;
    if (length) {
        g_byte_array_append(d->pending, chunk, length);
        /* Preserve UTF-8 characters split across pipe reads; replace malformed bytes. */
        guint complete = 0;
        while (complete < d->pending->len) {
            const char *p = (const char *)d->pending->data + complete;
            gunichar c = g_utf8_get_char_validated(p, d->pending->len - complete);
            if (c == (gunichar)-2)
                break;
            complete += c == (gunichar)-1 || c == 0 ? 1 : (guint)(g_utf8_next_char(p) - p);
        }
        if (complete) {
            /* Child output is text; strip NULs so GtkTextBuffer sees the entire chunk. */
            for (guint i = 0; i < complete; i++)
                if (d->pending->data[i] == 0) d->pending->data[i] = ' ';
            char *valid = g_utf8_make_valid((const char *)d->pending->data, complete);
            output(d, valid, -1);
            g_free(valid);
            g_byte_array_remove_range(d->pending, 0, complete);
        }
        g_bytes_unref(bytes);
        g_input_stream_read_bytes_async(d->stream, 4096, G_PRIORITY_DEFAULT, NULL, read_output, d);
        return;
    }
    if (d->pending->len) {
        char *valid = g_utf8_make_valid((const char *)d->pending->data, d->pending->len);
        output(d, valid, -1);
        g_free(valid);
        g_byte_array_set_size(d->pending, 0);
    }
    if (error) {
        output(d, error->message, -1);
        g_clear_error(&error);
    }
    if (bytes) g_bytes_unref(bytes);
    d->read_done = TRUE;
    finish(d);
}
static void waited(GObject *source, GAsyncResult *result, gpointer data) {
    GuiDiagnostics *d = data;
    GError *error = NULL;
    d->succeeded = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);
    if (error && !d->cancelled) {
        output(d, "\n", 1);
        output(d, error->message, -1);
        output(d, "\n", 1);
    }
    g_clear_error(&error);
    d->wait_done = TRUE;
    finish(d);
}
static gboolean force_stop(gpointer data) {
    GuiDiagnostics *d = data;
    d->kill_timer = 0;
    if (d->running && d->group > 0)
        kill(-d->group, SIGKILL);
    finish(d);
    return G_SOURCE_REMOVE;
}
void gui_diagnostics_cancel(GuiDiagnostics *d) {
    if (!d || !d->running || d->cancelled)
        return;
    d->cancelled = TRUE;
    gtk_label_set_text(GTK_LABEL(d->status), "Cancelling diagnostics…");
    gtk_widget_set_sensitive(d->action, FALSE);
    /* The shell, build tools, benchmarks, and tee all share this private group. */
    if (d->group > 0)
        kill(-d->group, SIGTERM);
    d->kill_timer = g_timeout_add_seconds(2, force_stop, d);
}
static gboolean close_popup(GtkWindow *window, gpointer data) {
    GuiDiagnostics *d = data;
    if (d->running) {
        d->hide_when_done = TRUE;
        gui_diagnostics_cancel(d);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    }
    return TRUE;
}
static void action(GtkButton *button, gpointer data) {
    (void)button;
    GuiDiagnostics *d = data;
    if (d->running)
        gui_diagnostics_cancel(d);
    else
        gtk_widget_set_visible(GTK_WIDGET(d->window), FALSE);
}
static void child_setup(gpointer data) {
    (void)data;
    /* Only async-signal-safe calls are allowed between fork and exec. */
    if (setsid() < 0)
        _exit(127);
}
GuiDiagnostics *gui_diagnostics_new(GtkWindow *parent, const char *directory,
                                    GuiDiagnosticsChanged changed, gpointer data) {
    GuiDiagnostics *d = g_new0(GuiDiagnostics, 1);
    d->directory = g_strdup(directory);
    d->changed = changed;
    d->data = data;
    d->pending = g_byte_array_new();
    d->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_application(d->window, gtk_window_get_application(parent));
    gtk_window_set_transient_for(d->window, parent);
    gtk_window_set_title(d->window, "Cinder Foundry · Diagnostics");
    gtk_window_set_default_size(d->window, 760, 480);
    gtk_widget_add_css_class(GTK_WIDGET(d->window), "foundry");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_window_set_child(d->window, box);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), header);
    d->spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(header), d->spinner);
    d->status = gtk_label_new("Preparing diagnostics…");
    gtk_label_set_xalign(GTK_LABEL(d->status), 0);
    gtk_label_set_wrap(GTK_LABEL(d->status), TRUE);
    gtk_widget_set_hexpand(d->status, TRUE);
    gtk_box_append(GTK_BOX(header), d->status);
    d->action = gtk_button_new_with_label("Cancel");
    g_signal_connect(d->action, "clicked", G_CALLBACK(action), d);
    gtk_box_append(GTK_BOX(header), d->action);
    d->scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(d->scroll, TRUE);
    gtk_box_append(GTK_BOX(box), d->scroll);
    d->output = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(d->output), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(d->output), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(d->output), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(d->output), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(d->output), 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(d->scroll), d->output);
    g_signal_connect(d->window, "close-request", G_CALLBACK(close_popup), d);
    return d;
}
void gui_diagnostics_present(GuiDiagnostics *d) {
    GtkWindow *parent = gtk_window_get_transient_for(d->window);
    if (gtk_widget_has_css_class(GTK_WIDGET(parent), "dark"))
        gtk_widget_add_css_class(GTK_WIDGET(d->window), "dark");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(d->window), "dark");
    gtk_window_present(d->window);
    if (d->running)
        return;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->output)), "", 0);
    d->read_done = d->wait_done = d->cancelled = d->succeeded = d->hide_when_done = FALSE;
    gtk_label_set_text(GTK_LABEL(d->status), "Running diagnostics · live output");
    gtk_button_set_label(GTK_BUTTON(d->action), "Cancel");
    gtk_widget_set_sensitive(d->action, TRUE);
    char *script = g_build_filename(d->directory, "cinder-diagnostics.command", NULL);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, d->directory);
    g_subprocess_launcher_set_child_setup(launcher, child_setup, NULL, NULL);
    GError *error = NULL;
    d->process = g_subprocess_launcher_spawn(launcher, &error, "/bin/bash", script, NULL);
    g_object_unref(launcher);
    g_free(script);
    if (!d->process) {
        output(d, error->message, -1);
        g_clear_error(&error);
        d->read_done = d->wait_done = TRUE;
        finish(d);
        return;
    }
    d->group = (pid_t)g_ascii_strtoll(g_subprocess_get_identifier(d->process), NULL, 10);
    d->running = TRUE;
    d->stream = g_object_ref(g_subprocess_get_stdout_pipe(d->process));
    gtk_widget_set_visible(d->spinner, TRUE);
    gtk_spinner_start(GTK_SPINNER(d->spinner));
    if (d->changed) d->changed(TRUE, d->data);
    g_input_stream_read_bytes_async(d->stream, 4096, G_PRIORITY_DEFAULT, NULL, read_output, d);
    g_subprocess_wait_check_async(d->process, NULL, waited, d);
}
gboolean gui_diagnostics_running(GuiDiagnostics *d) {
    return d && d->running;
}
void gui_diagnostics_free(GuiDiagnostics *d) {
    if (!d) return;
    g_return_if_fail(!d->running);
    gtk_window_destroy(d->window);
    g_byte_array_unref(d->pending);
    g_free(d->directory);
    g_free(d);
}
