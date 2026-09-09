#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#include "cJSON.h"
#include "runtime_worker.h"
#include "diagnostics.h"
#include "model_info.h"
#ifndef FOUNDRY_SOURCE_DIR
#define FOUNDRY_SOURCE_DIR "."
#endif
#include <gtk/gtk.h>
#include <string.h>
#include <sys/resource.h>

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GuiWorker *worker;
    GuiDiagnostics *diagnostics;
    GtkWidget *run_diagnostics;
    GtkWidget *model, *name, *weights, *metadata, *dropdown, *prompt, *transcript, *details,
        *status, *metrics;
    GtkWidget *context, *tokens, *temperature, *top_p, *seed, *threads, *gpu;
    GtkWidget *load, *unload, *send, *stop, *fresh, *import_gguf, *import_ollama, *inspect, *plan,
        *refresh;
    GtkWidget *scroll, *paned, *import_section, *settings_section, *diagnostics_section;
    GtkWidget *model_state, *model_title, *spinner, *context_meter, *summary, *logs;
    GtkWidget *prompt_scroll, *copy, *export, *dark, *active_message, *empty_chat;
    GString *response;
    char *loaded_model, *conversation_model;
    guint used_tokens;
    double tokens_per_second;
    GuiJobType active_job;
    GtkStringList *models;
    GPtrArray *history;
    gboolean busy, loaded, generating, closing, smoke_test, gpu_available;
    guint64 session_id, generation_id;
    GCancellable *dialogs;
    char *preference_path;
} App;
static const char *entry(GtkWidget *w) {
    return gtk_editable_get_text(GTK_EDITABLE(w));
}
static void set_status(App *a, const char *s) {
    gtk_label_set_text(GTK_LABEL(a->status), s ? s : "");
    if (a->logs && s && *s) {
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->logs));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(b, &end);
        gtk_text_buffer_insert(b, &end, s, -1);
        gtk_text_buffer_insert(b, &end, "\n", 1);
        if (gtk_text_buffer_get_line_count(b) > 200) {
            GtkTextIter start, cut;
            gtk_text_buffer_get_start_iter(b, &start);
            gtk_text_buffer_get_iter_at_line(b, &cut, 1);
            gtk_text_buffer_delete(b, &start, &cut);
        }
    }
}
static void message_free(gpointer p) {
    foundry_message *m = p;
    g_free((char *)m->role);
    g_free((char *)m->content);
    g_free(m);
}
static void history_add(App *a, const char *role, const char *text) {
    foundry_message *m = g_new0(foundry_message, 1);
    m->role = g_strdup(role);
    m->content = g_strdup(text);
    g_ptr_array_add(a->history, m);
}
static foundry_config config(App *a) {
    foundry_config c = foundry_config_default();
    c.context = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->context));
    c.max_tokens = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->tokens));
    c.batch_size = MIN(128, c.context);
    c.temperature = (float)gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->temperature));
    c.top_p = (float)gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->top_p));
    c.seed = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->seed));
    c.threads = (uint32_t)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->threads));
    c.gpu_layers = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->gpu));
    return c;
}
static void update_usage(App *a) {
    guint context = config(a).context;
    char *usage = g_strdup_printf("%u / %u tokens · last request", a->used_tokens, context);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->context_meter),
                                  MIN(1.0, (double)a->used_tokens / context));
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(a->context_meter), usage);
    g_free(usage);
    struct rusage ru;
    char memory[80] = "Memory —";
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#ifdef __APPLE__
        double mib = ru.ru_maxrss / (1024.0 * 1024.0);
#else
        double mib = ru.ru_maxrss / 1024.0;
#endif
        g_snprintf(memory, sizeof(memory), "Peak app %.0f MiB", mib);
    }
    char *summary = g_strdup_printf("%.1f tok/s  ·  %u / %u tokens  ·  %s  ·  %s",
        a->tokens_per_second, a->used_tokens, context,
        a->loaded_model ? a->loaded_model : "No model loaded", memory);
    gtk_label_set_text(GTK_LABEL(a->summary), summary);
    g_free(summary);
}
static void controls(App *a) {
    gboolean idle = !a->busy && !a->closing && !gui_diagnostics_running(a->diagnostics);
    gtk_widget_set_sensitive(a->run_diagnostics, idle && !a->loaded);
    gboolean has_model = *entry(a->model) != '\0';
    GtkTextBuffer *prompt = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->prompt));
    gtk_widget_set_sensitive(a->load, idle && has_model && !a->loaded);
    gtk_widget_set_sensitive(a->unload, idle && a->loaded);
    gtk_widget_set_sensitive(a->send, idle && a->loaded && gtk_text_buffer_get_char_count(prompt));
    gtk_widget_set_sensitive(a->fresh, idle && a->history->len > 0);
    gtk_widget_set_sensitive(a->copy, a->history->len > 0);
    gtk_widget_set_sensitive(a->export, idle && a->history->len > 0);
    gtk_widget_set_visible(a->stop, a->generating);
    gtk_widget_set_sensitive(a->stop, a->generating && !a->closing);
    GtkWidget *ws[] = {a->import_section, a->refresh, a->model, a->dropdown};
    for (size_t i = 0; i < G_N_ELEMENTS(ws); i++)
        gtk_widget_set_sensitive(ws[i], idle && !a->loaded);
    gtk_widget_set_sensitive(a->inspect, idle && has_model);
    gtk_widget_set_sensitive(a->plan, idle && has_model);
    GtkWidget *fixed[] = {a->context, a->threads, a->gpu};
    for (size_t i = 0; i < G_N_ELEMENTS(fixed); i++)
        gtk_widget_set_sensitive(fixed[i], idle && !a->loaded);
    GtkWidget *sampling[] = {a->tokens, a->temperature, a->top_p, a->seed};
    for (size_t i = 0; i < G_N_ELEMENTS(sampling); i++)
        gtk_widget_set_sensitive(sampling[i], idle);
    gtk_widget_set_sensitive(a->top_p, idle && config(a).temperature > 0);
    gtk_widget_set_sensitive(a->seed, idle && config(a).temperature > 0);
    gtk_widget_set_sensitive(a->gpu, idle && !a->loaded && a->gpu_available);
    gtk_label_set_text(GTK_LABEL(a->model_title), a->loaded_model ? a->loaded_model :
                       (has_model ? entry(a->model) : "Choose a model"));
    gtk_label_set_text(GTK_LABEL(a->model_state), a->busy && a->active_job == GUI_JOB_LOAD ?
                       "Loading…" : a->loaded ? "● Loaded" : "○ Unloaded");
    if (a->loaded)
        gtk_widget_add_css_class(a->model_state, "loaded");
    else
        gtk_widget_remove_css_class(a->model_state, "loaded");
    gtk_spinner_set_spinning(GTK_SPINNER(a->spinner), a->busy);
    gtk_widget_set_visible(a->spinner, a->busy);
    update_usage(a);
}
/* A deliberately small, escaped Markdown renderer. Raw HTML is always literal.
 * Supports fenced code, headings, lists, bold, emphasis and inline code. */
static void inline_markdown(GString *out, const char *text) {
    const char *p = text;
    while (*p) {
        const char *marker = NULL, *tag = NULL;
        size_t n = 0;
        if (*p == '`') { marker = "`"; tag = "tt"; n = 1; }
        else if (g_str_has_prefix(p, "**")) { marker = "**"; tag = "b"; n = 2; }
        else if (*p == '*') { marker = "*"; tag = "i"; n = 1; }
        const char *end = marker ? strstr(p + n, marker) : NULL;
        if (end && end > p + n) {
            char *escaped = g_markup_escape_text(p + n, end - p - n);
            g_string_append_printf(out, "<%s>%s</%s>", tag, escaped, tag);
            g_free(escaped);
            p = end + n;
        } else {
            const char *next = g_utf8_next_char(p);
            char *escaped = g_markup_escape_text(p, next - p);
            g_string_append(out, escaped);
            g_free(escaped);
            p = next;
        }
    }
}
static char *markdown(const char *text) {
    GString *out = g_string_new(NULL);
    char **lines = g_strsplit(text, "\n", -1);
    gboolean code = FALSE;
    for (guint i = 0; lines[i]; i++) {
        const char *line = lines[i];
        if (g_str_has_prefix(line, "```")) {
            g_string_append(out, code ? "</tt>" : "<tt>");
            code = !code;
        } else if (code) {
            char *escaped = g_markup_escape_text(line, -1);
            g_string_append(out, escaped);
            g_free(escaped);
        } else {
            guint heading = 0;
            while (line[heading] == '#' && heading < 6) heading++;
            if (heading && line[heading] == ' ') {
                g_string_append(out, "<b>");
                inline_markdown(out, line + heading + 1);
                g_string_append(out, "</b>");
            } else {
                if (g_str_has_prefix(line, "- ") || g_str_has_prefix(line, "* ")) {
                    g_string_append(out, "• ");
                    line += 2;
                }
                inline_markdown(out, line);
            }
        }
        if (lines[i + 1]) g_string_append_c(out, '\n');
    }
    if (code) g_string_append(out, "</tt>");
    g_strfreev(lines);
    return g_string_free(out, FALSE);
}
static void message_block(App *a, const char *role, const char *text) {
    gtk_widget_set_visible(a->empty_chat, FALSE);
    GtkWidget *block = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(block, "message");
    gtk_widget_add_css_class(block, !strcmp(role, "user") ? "user-message" : "assistant-message");
    GtkWidget *speaker = gtk_label_new(!strcmp(role, "user") ? "YOU" : "ASSISTANT");
    gtk_label_set_xalign(GTK_LABEL(speaker), 0);
    gtk_widget_add_css_class(speaker, "speaker");
    gtk_box_append(GTK_BOX(block), speaker);
    GtkWidget *body = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(body), 0);
    gtk_label_set_wrap(GTK_LABEL(body), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(body), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(body), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(body), 90);
    gtk_widget_set_hexpand(body, TRUE);
    gtk_box_append(GTK_BOX(block), body);
    gtk_box_append(GTK_BOX(a->transcript), block);
    a->active_message = body;
    g_string_assign(a->response, text);
}
static void append(App *a, const char *text) {
    if (!a->active_message) return;
    g_string_append(a->response, text);
    gtk_label_set_text(GTK_LABEL(a->active_message), a->response->str);
}
static void finish_message(App *a) {
    if (!a->active_message) return;
    char *rendered = markdown(a->response->str);
    gtk_label_set_markup(GTK_LABEL(a->active_message), rendered);
    g_free(rendered);
    a->active_message = NULL;
}
static void clear_chat(App *a) {
    g_ptr_array_set_size(a->history, 0);
    g_clear_pointer(&a->conversation_model, g_free);
    GtkWidget *child = gtk_widget_get_first_child(a->transcript);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (child != a->empty_chat) gtk_box_remove(GTK_BOX(a->transcript), child);
        child = next;
    }
    gtk_widget_set_visible(a->empty_chat, TRUE);
    a->active_message = NULL;
    g_string_truncate(a->response, 0);
    a->used_tokens = 0;
    a->tokens_per_second = 0;
    update_usage(a);
}
static GuiJob *job_new(App *a, GuiJobType type) {
    GuiJob *j = g_new0(GuiJob, 1);
    j->type = type;
    j->session_id = a->session_id;
    j->generation_id = ++a->generation_id;
    j->config = config(a);
    j->path = g_strdup(entry(a->model));
    return j;
}
static gboolean submit(App *a, GuiJob *j) {
    GuiJobType type = j->type;
    if (a->closing || !gui_worker_submit(a->worker, j)) {
        gui_job_free(j);
        set_status(a, "Finish the active operation first.");
        return FALSE;
    }
    a->busy = TRUE;
    a->active_job = type;
    controls(a);
    return TRUE;
}
static void selected(GObject *object, GParamSpec *spec, gpointer data) {
    (void)object;
    (void)spec;
    App *a = data;
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->dropdown));
    if (i == GTK_INVALID_LIST_POSITION) return;
    const char *s = gtk_string_list_get_string(a->models, i);
    if (s)
        gtk_editable_set_text(GTK_EDITABLE(a->model), s);
}
static void refresh(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    submit(a, job_new(a, GUI_JOB_LIST));
}
static void simple(GtkButton *button, gpointer data) {
    App *a = data;
    GuiJobType type = GTK_WIDGET(button) == a->load      ? GUI_JOB_LOAD
                      : GTK_WIDGET(button) == a->unload  ? GUI_JOB_UNLOAD
                      : GTK_WIDGET(button) == a->inspect ? GUI_JOB_INSPECT
                      : GTK_WIDGET(button) == a->plan    ? GUI_JOB_PLAN
                                                         : GUI_JOB_RESET;
    if (type == GUI_JOB_LOAD || type == GUI_JOB_RESET)
        a->session_id++;
    if (type == GUI_JOB_LOAD)
        a->loaded = FALSE;
    GuiJob *j = job_new(a, type);
    if (submit(a, j))
        set_status(a, type == GUI_JOB_LOAD ? "Loading local model…" : "Working…");
}
static void stop(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    gui_worker_cancel(a->worker);
    set_status(a, "Cancelling… waiting for the current backend operation.");
}
static void send(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    if (a->busy || !a->loaded || gui_diagnostics_running(a->diagnostics))
        return;
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->prompt));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(b, &start, &end);
    char *text = gtk_text_buffer_get_text(b, &start, &end, FALSE);
    if (!*text) {
        g_free(text);
        return;
    }
    GuiJob *j = job_new(a, GUI_JOB_GENERATE);
    j->message_count = a->history->len + 1;
    j->messages = g_new0(foundry_message, j->message_count);
    for (guint i = 0; i < a->history->len; i++) {
        foundry_message *m = g_ptr_array_index(a->history, i);
        j->messages[i] = (foundry_message){g_strdup(m->role), g_strdup(m->content)};
    }
    j->messages[j->message_count - 1] = (foundry_message){g_strdup("user"), g_strdup(text)};
    if (submit(a, j)) {
        if (!a->history->len) {
            g_free(a->conversation_model);
            a->conversation_model = g_strdup(a->loaded_model);
        }
        history_add(a, "user", text);
        history_add(a, "assistant", "");
        message_block(a, "user", text);
        message_block(a, "assistant", "");
        gtk_text_buffer_set_text(b, "", 0);
        a->generating = TRUE;
        controls(a);
        set_status(a, "Processing conversation…");
    }
    g_free(text);
}
static gboolean keys(GtkEventControllerKey *controller, guint key, guint code,
                     GdkModifierType state, gpointer data) {
    (void)controller;
    (void)code;
    App *a = data;
    if ((key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && !(state & GDK_SHIFT_MASK)) {
        send(NULL, a);
        return TRUE;
    }
    if (key == GDK_KEY_Escape && a->generating) {
        stop(NULL, a);
        return TRUE;
    }
    return FALSE;
}
static gboolean window_keys(GtkEventControllerKey *controller, guint key, guint code,
                            GdkModifierType state, gpointer data) {
    if (key == GDK_KEY_Escape) return keys(controller, key, code, state, data);
    return FALSE;
}
static char *conversation_json(App *a) {
    cJSON *doc = cJSON_CreateObject(), *messages = cJSON_AddArrayToObject(doc, "messages");
    cJSON_AddNumberToObject(doc, "schema_version", 1);
    cJSON_AddStringToObject(doc, "model", a->conversation_model ? a->conversation_model : entry(a->model));
    for (guint i = 0; i < a->history->len; i++) {
        foundry_message *m = g_ptr_array_index(a->history, i);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "role", m->role);
        cJSON_AddStringToObject(item, "content", m->content);
        cJSON_AddItemToArray(messages, item);
    }
    char *s = cJSON_Print(doc);
    cJSON_Delete(doc);
    return s;
}
static void copy_chat(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    GString *text = g_string_new(NULL);
    for (guint i = 0; i < a->history->len; i++) {
        foundry_message *m = g_ptr_array_index(a->history, i);
        g_string_append_printf(text, "%s\n%s\n\n", m->role, m->content);
    }
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(a->window)), text->str);
    g_string_free(text, TRUE);
    set_status(a, "Conversation copied.");
}
static void saved(GObject *source, GAsyncResult *result, gpointer data) {
    App *a = data;
    GError *error = NULL;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
    if (f && !a->closing) {
        char *text = conversation_json(a);
        if (!text || !g_file_replace_contents(f, text, strlen(text), NULL, FALSE,
                                              G_FILE_CREATE_PRIVATE, NULL, NULL, &error))
            set_status(a, error ? error->message : "Export allocation failed");
        else
            set_status(a, "Conversation exported.");
        foundry_free(text);
    }
    g_clear_object(&f);
    g_clear_error(&error);
}
static void export_chat(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, "Export conversation JSON");
    gtk_file_dialog_set_initial_name(d, "conversation.json");
    gtk_file_dialog_save(d, a->window, a->dialogs, saved, a);
    g_object_unref(d);
}
typedef struct {
    App *app;
    gboolean ollama;
} ImportDialog;
static void chosen(GObject *source, GAsyncResult *result, gpointer data) {
    ImportDialog *ctx = data;
    App *a = ctx->app;
    GError *error = NULL;
    GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
    if (f && !a->closing) {
        char *path = g_file_get_path(f);
        if (!path)
            set_status(a, "Choose a local file.");
        else {
            GuiJob *j = job_new(a, ctx->ollama ? GUI_JOB_IMPORT_OLLAMA : GUI_JOB_IMPORT_GGUF);
            g_free(j->path);
            j->path = path;
            j->name = g_strdup(entry(a->name));
            j->weights_root = g_strdup(entry(a->weights));
            j->metadata_root = g_strdup(entry(a->metadata));
            submit(a, j);
        }
    }
    g_clear_object(&f);
    g_clear_error(&error);
    g_free(ctx);
}
static void import_model(GtkButton *button, gpointer data) {
    App *a = data;
    ImportDialog *ctx = g_new0(ImportDialog, 1);
    ctx->app = a;
    ctx->ollama = GTK_WIDGET(button) == a->import_ollama;
    GtkFileDialog *d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, ctx->ollama ? "Import Ollama manifest" : "Import local GGUF");
    gtk_file_dialog_open(d, a->window, a->dialogs, chosen, ctx);
    g_object_unref(d);
}
static void show_json(App *a, const char *text) {
    gui_model_info_show(a->details, text);
}
static gboolean poll(gpointer data) {
    App *a = data;
    if (a->closing && gui_worker_stopped(a->worker) && !gui_diagnostics_running(a->diagnostics)) {
        gui_worker_free(a->worker);
        a->worker = NULL;
        gtk_window_destroy(a->window);
        g_application_quit(G_APPLICATION(a->application));
        return G_SOURCE_REMOVE;
    }
    GuiEvent *ev;
    while ((ev = gui_worker_poll(a->worker))) {
        if (a->closing || ev->generation_id != a->generation_id ||
            ev->session_id != a->session_id) {
            gui_event_free(ev);
            continue;
        }
        switch (ev->type) {
        case GUI_EVENT_TOKEN:
            if (a->response->len == 0) set_status(a, "Generating response…");
            append(a, ev->text ? ev->text : "");
            if (a->history->len) {
                foundry_message *m = g_ptr_array_index(a->history, a->history->len - 1);
                char *next = g_strconcat(m->content, ev->text ? ev->text : "", NULL);
                g_free((char *)m->content);
                m->content = next;
            }
            break;
        case GUI_EVENT_METRICS: {
            const foundry_metrics *m = &ev->metrics;
            a->used_tokens = m->prompt_tokens + m->generated_tokens;
            a->tokens_per_second = m->decode_ms > 0 ?
                m->generated_tokens * 1000.0 / m->decode_ms : 0;
            char *label = g_strdup_printf(
                "Load %.0f ms · First token %.0f ms · Prefill %.0f ms · Decode %.0f ms\n"
                "%u input / %u output tokens · Stop: %s",
                m->load_ms, m->first_token_ms, m->prefill_ms, m->decode_ms,
                m->prompt_tokens, m->generated_tokens, m->stop_reason);
            gtk_label_set_text(GTK_LABEL(a->metrics), label);
            g_free(label);
            update_usage(a);
            break;
        }
        case GUI_EVENT_PROGRESS:
        case GUI_EVENT_PREFILL:
            set_status(a, ev->text);
            break;
        default:
            a->busy = FALSE;
            a->generating = FALSE;
            if (ev->type == GUI_EVENT_READY) {
                a->loaded = TRUE;
                g_free(a->loaded_model);
                a->loaded_model = g_strdup(entry(a->model));
                clear_chat(a);
                set_status(a, "Ready. Model weights stay loaded between turns.");
                submit(a, job_new(a, GUI_JOB_INSPECT));
            } else if (ev->type == GUI_EVENT_UNLOADED) {
                a->loaded = FALSE;
                g_clear_pointer(&a->loaded_model, g_free);
                set_status(a, "Model unloaded. Conversation retained for Copy or Export.");
            } else if (ev->type == GUI_EVENT_RESET) {
                clear_chat(a);
                set_status(a, "New chat. Weights remain loaded.");
            } else if (ev->type == GUI_EVENT_REGISTRY) {
                cJSON *doc = cJSON_Parse(ev->text),
                      *items = cJSON_GetObjectItemCaseSensitive(doc, "models");
                char *selection = g_strdup(entry(a->model));
                gtk_string_list_splice(a->models, 0,
                                       g_list_model_get_n_items(G_LIST_MODEL(a->models)), NULL);
                cJSON *item;
                cJSON_ArrayForEach(item, items) {
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
                    if (cJSON_IsString(name))
                        gtk_string_list_append(a->models, name->valuestring);
                }
                if (*selection) {
                    gtk_editable_set_text(GTK_EDITABLE(a->model), selection);
                    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->dropdown), GTK_INVALID_LIST_POSITION);
                    for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(a->models)); i++)
                        if (!strcmp(selection, gtk_string_list_get_string(a->models, i)))
                            gtk_drop_down_set_selected(GTK_DROP_DOWN(a->dropdown), i);
                }
                g_free(selection);
                cJSON_Delete(doc);
                set_status(a, "Local registry refreshed.");
            } else if (ev->type == GUI_EVENT_IMPORTED) {
                cJSON *doc = cJSON_Parse(ev->text),
                      *name = cJSON_GetObjectItemCaseSensitive(doc, "name");
                if (cJSON_IsString(name))
                    gtk_editable_set_text(GTK_EDITABLE(a->model), name->valuestring);
                cJSON_Delete(doc);
                show_json(a, ev->text);
                set_status(
                    a, "Imported and hash verified. Review details and memory plan, then Load.");
            } else if (ev->type == GUI_EVENT_DETAILS || ev->type == GUI_EVENT_PLAN) {
                show_json(a, ev->text);
                set_status(a, "Model details updated.");
            } else if (ev->type == GUI_EVENT_FAILURE) {
                set_status(a, ev->text);
                finish_message(a);
            } else {
                set_status(a, ev->type == GUI_EVENT_CANCELLED ? ev->text : "Ready.");
                finish_message(a);
            }
            controls(a);
            break;
        }
        gui_event_free(ev);
    }
    return G_SOURCE_CONTINUE;
}
static void preferences(App *a, gboolean save) {
    GKeyFile *f = g_key_file_new();
    if (save) {
        foundry_config c = config(a);
        g_key_file_set_integer(f, "ui", "sidebar_width", gtk_paned_get_position(GTK_PANED(a->paned)));
        g_key_file_set_boolean(f, "ui", "dark", gtk_switch_get_active(GTK_SWITCH(a->dark)));
        g_key_file_set_boolean(f, "ui", "import_expanded", gtk_expander_get_expanded(GTK_EXPANDER(a->import_section)));
        g_key_file_set_boolean(f, "ui", "settings_expanded", gtk_expander_get_expanded(GTK_EXPANDER(a->settings_section)));
        g_key_file_set_boolean(f, "ui", "diagnostics_expanded", gtk_expander_get_expanded(GTK_EXPANDER(a->diagnostics_section)));
        g_key_file_set_string(f, "ui", "import_name", entry(a->name));
        g_key_file_set_string(f, "ui", "weights_directory", entry(a->weights));
        g_key_file_set_string(f, "ui", "metadata_directory", entry(a->metadata));
        g_key_file_set_string(f, "ui", "model", entry(a->model));
        g_key_file_set_integer(f, "generation", "context", c.context);
        g_key_file_set_integer(f, "generation", "tokens", c.max_tokens);
        g_key_file_set_integer(f, "generation", "threads", c.threads);
        g_key_file_set_integer(f, "generation", "gpu_layers", c.gpu_layers);
        g_key_file_set_double(f, "generation", "temperature", c.temperature);
        g_key_file_set_double(f, "generation", "top_p", c.top_p);
        g_key_file_set_integer(f, "generation", "seed", c.seed);
        char *dir = g_path_get_dirname(a->preference_path);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        g_key_file_save_to_file(f, a->preference_path, NULL);
    } else if (g_key_file_load_from_file(f, a->preference_path, G_KEY_FILE_NONE, NULL)) {
        if (g_key_file_has_key(f, "ui", "sidebar_width", NULL))
            gtk_paned_set_position(GTK_PANED(a->paned), CLAMP(g_key_file_get_integer(f, "ui", "sidebar_width", NULL), 240, 600));
        if (g_key_file_has_key(f, "ui", "dark", NULL))
            gtk_switch_set_active(GTK_SWITCH(a->dark), g_key_file_get_boolean(f, "ui", "dark", NULL));
        const char *expander_keys[] = {"import_expanded", "settings_expanded", "diagnostics_expanded"};
        GtkWidget *expanders[] = {a->import_section, a->settings_section, a->diagnostics_section};
        for (size_t i = 0; i < G_N_ELEMENTS(expanders); i++)
            if (g_key_file_has_key(f, "ui", expander_keys[i], NULL))
                gtk_expander_set_expanded(GTK_EXPANDER(expanders[i]), g_key_file_get_boolean(f, "ui", expander_keys[i], NULL));
        const char *entry_keys[] = {"import_name", "weights_directory", "metadata_directory"};
        GtkWidget *entries[] = {a->name, a->weights, a->metadata};
        for (size_t i = 0; i < G_N_ELEMENTS(entries); i++) {
            char *value = g_key_file_get_string(f, "ui", entry_keys[i], NULL);
            if (value) gtk_editable_set_text(GTK_EDITABLE(entries[i]), value);
            g_free(value);
        }
        char *id = g_key_file_get_string(f, "ui", "model", NULL);
        if (id)
            gtk_editable_set_text(GTK_EDITABLE(a->model), id);
        g_free(id);
        const char *keys[] = {"context",     "tokens", "threads", "gpu_layers",
                              "temperature", "top_p",  "seed"};
        GtkWidget *widgets[] = {a->context,     a->tokens, a->threads, a->gpu,
                                a->temperature, a->top_p,  a->seed};
        for (size_t i = 0; i < G_N_ELEMENTS(keys); i++)
            if (g_key_file_has_key(f, "generation", keys[i], NULL))
                gtk_spin_button_set_value(GTK_SPIN_BUTTON(widgets[i]),
                                          g_key_file_get_double(f, "generation", keys[i], NULL));
    }
    g_key_file_unref(f);
}
static gboolean close_window(GtkWindow *window, gpointer data) {
    (void)window;
    App *a = data;
    if (!a->closing) {
        if (!a->smoke_test)
            preferences(a, TRUE);
        a->closing = TRUE;
        g_cancellable_cancel(a->dialogs);
        gui_worker_close(a->worker);
        gui_diagnostics_cancel(a->diagnostics);
        controls(a);
        set_status(a, "Closing… waiting for runtime cleanup.");
    }
    return TRUE;
}
static gboolean smoke_close(gpointer data) {
    App *a = data;
    close_window(a->window, a);
    return G_SOURCE_REMOVE;
}
static GtkWidget *button(GtkWidget *box, const char *label, GCallback callback, App *a) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_box_append(GTK_BOX(box), b);
    g_signal_connect(b, "clicked", callback, a);
    return b;
}
static GtkWidget *input(GtkWidget *box, const char *label, const char *initial) {
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    gtk_widget_add_css_class(caption, "field-label");
    gtk_box_append(GTK_BOX(box), caption);
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), initial);
    gtk_box_append(GTK_BOX(box), e);
    return e;
}
static GtkWidget *spin(GtkWidget *box, const char *label, double low, double high, double step,
                       double initial) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *caption = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(caption), 0);
    gtk_widget_set_hexpand(caption, TRUE);
    gtk_widget_add_css_class(caption, "field-label");
    gtk_box_append(GTK_BOX(row), caption);
    GtkWidget *s = gtk_spin_button_new_with_range(low, high, step);
    gtk_editable_set_width_chars(GTK_EDITABLE(s), 6);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(s), 8);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), initial);
    gtk_box_append(GTK_BOX(row), s);
    gtk_box_append(GTK_BOX(box), row);
    return s;
}
static GtkWidget *text_view(GtkWidget *box, gboolean editable, int height, GtkWidget **scroll) {
    GtkWidget *s = gtk_scrolled_window_new(), *v = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(v), editable);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(v), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(v), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(v), 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s), v);
    gtk_widget_set_size_request(s, -1, height);
    gtk_box_append(GTK_BOX(box), s);
    if (scroll)
        *scroll = s;
    return v;
}
static void diagnostics_changed(gboolean running, gpointer data) {
    App *a = data;
    controls(a);
    set_status(a, running ? "Diagnostics running in a separate window…" :
                          "Diagnostics finished. Review the result in the diagnostics window.");
}
static void run_diagnostics(GtkButton *button, gpointer data) {
    (void)button;
    App *a = data;
    if (a->busy || a->loaded || a->closing) return;
    if (!a->diagnostics) {
        const char *directory = g_getenv("FOUNDRY_PROJECT_DIR");
        a->diagnostics = gui_diagnostics_new(a->window,
            directory && *directory ? directory : FOUNDRY_SOURCE_DIR, diagnostics_changed, a);
    }
    gui_diagnostics_present(a->diagnostics);
}
static GtkWidget *heading(GtkWidget *box, const char *text) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_add_css_class(label, "section-title");
    gtk_box_append(GTK_BOX(box), label);
    return label;
}
static GtkWidget *section(GtkWidget *box) {
    GtkWidget *part = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(part, "section");
    gtk_box_append(GTK_BOX(box), part);
    return part;
}
static void theme_changed(GObject *object, GParamSpec *spec, gpointer data) {
    (void)object;
    (void)spec;
    App *a = data;
    gboolean dark = gtk_switch_get_active(GTK_SWITCH(a->dark));
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);
    if (dark) gtk_widget_add_css_class(GTK_WIDGET(a->window), "dark");
    else gtk_widget_remove_css_class(GTK_WIDGET(a->window), "dark");
}
static void inputs_changed(GtkWidget *widget, gpointer data) {
    App *a = data;
    if (widget == a->model) {
        guint match = GTK_INVALID_LIST_POSITION;
        for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(a->models)); i++)
            if (!strcmp(entry(a->model), gtk_string_list_get_string(a->models, i))) match = i;
        if (gtk_drop_down_get_selected(GTK_DROP_DOWN(a->dropdown)) != match)
            gtk_drop_down_set_selected(GTK_DROP_DOWN(a->dropdown), match);
        gui_model_info_clear(a->details);
    }
    controls(a);
}
static void prompt_changed(GtkTextBuffer *buffer, gpointer data) {
    (void)buffer;
    controls(data);
}
static void scroll_changed(GtkAdjustment *adjustment, gpointer data) {
    /* Preserve reading position unless the reader was already at the bottom. */
    double *previous_upper = data;
    double page = gtk_adjustment_get_page_size(adjustment);
    double value = gtk_adjustment_get_value(adjustment);
    if (value + page >= *previous_upper - 48)
        gtk_adjustment_set_value(adjustment, MAX(0, gtk_adjustment_get_upper(adjustment) - page));
    *previous_upper = gtk_adjustment_get_upper(adjustment);
}
static void activate(GtkApplication *app, gpointer data) {
    App *a = data;
    if (a->window) {
        gtk_window_present(a->window);
        return;
    }
    a->application = app;
    a->worker = gui_worker_new();
    a->history = g_ptr_array_new_with_free_func(message_free);
    a->response = g_string_new(NULL);
    a->dialogs = g_cancellable_new();
    a->session_id = 1;
    a->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(a->window, "Cinder Foundry");
    gtk_window_set_default_size(a->window, 1200, 820);
    gtk_widget_add_css_class(GTK_WIDGET(a->window), "foundry");
    g_signal_connect(a->window, "close-request", G_CALLBACK(close_window), a);

    GtkCssProvider *css = gtk_css_provider_new();
    const char *stylesheet =
        ".foundry { background: #f4f5f8; color: #202633; }"
        ".foundry.dark { background: #171b22; color: #e2e7ef; }"
        ".foundry .sidebar { background: #e9ecf2; padding: 16px 12px; }"
        ".foundry.dark .sidebar { background: #1e242e; }"
        ".foundry .chat-area { padding: 16px 24px; }"
        ".foundry .section { margin-bottom: 20px; }"
        ".foundry .section-title { font-weight: 700; font-size: 14px; margin-bottom: 6px; }"
        ".foundry .app-title { font-weight: 700; font-size: 20px; }"
        ".foundry .field-label, .foundry .speaker { font-size: 11px; opacity: .7; }"
        ".foundry .speaker { font-weight: 600; letter-spacing: 1px; }"
        ".foundry entry, .foundry spinbutton { min-height: 28px; border: none; box-shadow: none; }"
        ".foundry button { min-height: 28px; padding: 4px 10px; border: none; box-shadow: none; }"
        ".foundry button.suggested-action { background: #426bda; color: white; font-weight: 600; }"
        ".foundry button:disabled { opacity: .4; }"
        ".foundry button.destructive-action { background: alpha(#d66572, .16); color: #c34958; }"
        ".foundry.dark button.destructive-action { color: #ff9da8; }"
        ".foundry .message { background: #ffffff; border-radius: 12px; padding: 16px 20px; }"
        ".foundry .user-message { background: #e4eafa; margin-left: 36px; }"
        ".foundry .assistant-message { margin-right: 24px; }"
        ".foundry.dark .message { background: #222a36; }"
        ".foundry.dark .user-message { background: #283650; }"
        ".foundry .composer { border-radius: 12px; background: white; padding: 8px; }"
        ".foundry.dark .composer { background: #222a36; }"
        ".foundry textview, .foundry textview text { background: transparent; color: inherit; }"
        ".foundry .muted { font-size: 11px; opacity: .65; }"
        ".foundry .loaded { color: #31926a; font-weight: 600; }"
        ".foundry.dark .loaded { color: #7fd5ac; }"
        ".foundry .info-headline { font-size: 16px; font-weight: 700; margin-bottom: 6px; }"
        ".foundry .info-value { font-size: 12px; }"
        ".foundry .info-warning { color: #d66572; }"
        ".foundry .statusbar { padding: 8px 16px; font-size: 11px; }"
        ".foundry progressbar trough { min-height: 5px; border: none; }"
        ".foundry progressbar progress { background: #6285e6; border: none; }";
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(css, stylesheet);
#else
    gtk_css_provider_load_from_data(css, stylesheet, -1);
#endif
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(a->window)),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(a->window, root);
    a->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(a->paned, TRUE);
    gtk_paned_set_wide_handle(GTK_PANED(a->paned), TRUE);
    gtk_paned_set_resize_start_child(GTK_PANED(a->paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(a->paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(a->paned), FALSE);
    gtk_box_append(GTK_BOX(root), a->paned);
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *chat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(side, "sidebar");
    gtk_widget_add_css_class(chat, "chat-area");
    gtk_widget_set_size_request(side, 240, -1);
    gtk_widget_set_size_request(chat, 400, -1);
    GtkWidget *sidebar_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroll), side);
    gtk_paned_set_start_child(GTK_PANED(a->paned), sidebar_scroll);
    gtk_paned_set_end_child(GTK_PANED(a->paned), chat);
    gtk_paned_set_position(GTK_PANED(a->paned), 280);

    GtkWidget *library = section(side);
    heading(library, "Model");
    a->models = gtk_string_list_new(NULL);
    a->dropdown = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(a->models)), NULL);
    gtk_widget_set_tooltip_text(a->dropdown, "Choose a registered model. Unload before switching models.");
    gtk_box_append(GTK_BOX(library), a->dropdown);
    a->model = input(library, "Selected model / local path", "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->model), "Model ID or /path/to/model.gguf");
    gtk_editable_set_width_chars(GTK_EDITABLE(a->model), 16);
    a->model_state = gtk_label_new("○ Unloaded");
    gtk_label_set_xalign(GTK_LABEL(a->model_state), 0);
    gtk_box_append(GTK_BOX(library), a->model_state);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(library), actions);
    a->load = button(actions, "Load", G_CALLBACK(simple), a);
    a->unload = button(actions, "Unload", G_CALLBACK(simple), a);
    gtk_widget_add_css_class(a->load, "suggested-action");
    gtk_widget_add_css_class(a->unload, "destructive-action");
    gtk_widget_set_hexpand(a->load, TRUE);
    gtk_widget_set_tooltip_text(a->unload, "Release model weights. The conversation stays available to Copy or Export until the next Load or New Chat.");
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(library), actions);
    a->inspect = button(actions, "Inspect", G_CALLBACK(simple), a);
    a->plan = button(actions, "Memory plan", G_CALLBACK(simple), a);
    gtk_widget_add_css_class(a->inspect, "flat");
    gtk_widget_add_css_class(a->plan, "flat");
    a->refresh = button(library, "Refresh library", G_CALLBACK(refresh), a);
    gtk_widget_add_css_class(a->refresh, "flat");

    GtkWidget *imports = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    a->import_section = gtk_expander_new("Import model");
    gtk_widget_add_css_class(a->import_section, "section");
    gtk_expander_set_child(GTK_EXPANDER(a->import_section), imports);
    gtk_box_append(GTK_BOX(side), a->import_section);
    a->name = input(imports, "Registry name", "smollm2");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->name), "A short name for your model");
    a->import_gguf = button(imports, "Choose GGUF…", G_CALLBACK(import_model), a);
    a->weights = input(imports, "Ollama weights directory", ".");
    a->metadata = input(imports, "Ollama metadata directory", "metadata");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->weights), "/path/to/model/blobs");
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->metadata), "/path/to/copied/metadata");
    gtk_widget_set_tooltip_text(a->weights, "Ollama only: folder containing the sha256 model weight file; absolute paths are recommended.");
    gtk_widget_set_tooltip_text(a->metadata, "Ollama only: folder containing the manifest's config, template, license and parameter blobs.");
    a->import_ollama = button(imports, "Choose Ollama manifest…", G_CALLBACK(import_model), a);

    GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    a->settings_section = gtk_expander_new("Generation settings");
    gtk_expander_set_expanded(GTK_EXPANDER(a->settings_section), TRUE);
    gtk_expander_set_child(GTK_EXPANDER(a->settings_section), settings);
    gtk_widget_add_css_class(a->settings_section, "section");
    gtk_box_append(GTK_BOX(side), a->settings_section);
    a->context = spin(settings, "Context window", 128, 131072, 128, 2048);
    a->tokens = spin(settings, "Output tokens", 1, 32768, 1, 128);
    a->temperature = spin(settings, "Temperature", 0, 2, .05, 0);
    a->top_p = spin(settings, "Top-p", .01, 1, .01, .95);
    a->seed = spin(settings, "Seed", 0, 2147483647, 1, 0);
    a->threads = spin(settings, "CPU threads", 1, 128, 1, 4);
    a->gpu = spin(settings, "GPU layers", -1, 999, 1, 0);
    gtk_widget_set_tooltip_text(a->temperature, "Sampling temperature; zero uses deterministic greedy sampling.");
    GtkWidget *fixed[] = {a->context, a->threads, a->gpu};
    for (size_t i = 0; i < G_N_ELEMENTS(fixed); i++)
        gtk_widget_set_tooltip_text(fixed[i], "Unload the model to change this setting, then Load to apply it.");
    a->context_meter = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(a->context_meter), TRUE);
    gtk_widget_set_tooltip_text(a->context_meter,
        "Actual input plus generated tokens from the last request. Draft text and chat-template changes are not counted until inference.");
    gtk_box_append(GTK_BOX(side), a->context_meter);

    GtkWidget *information = section(side);
    gtk_widget_set_margin_top(information, 20);
    heading(information, "Model information");
    a->details = gui_model_info_new();
    gtk_box_append(GTK_BOX(information), a->details);
    gtk_widget_set_vexpand(information, TRUE);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(chat), header);
    GtkWidget *titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(titles, TRUE);
    gtk_box_append(GTK_BOX(header), titles);
    GtkWidget *title = heading(titles, "Cinder Foundry");
    gtk_widget_add_css_class(title, "app-title");
    a->model_title = gtk_label_new("Choose a model");
    gtk_label_set_xalign(GTK_LABEL(a->model_title), 0);
    gtk_label_set_ellipsize(GTK_LABEL(a->model_title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(titles), a->model_title);
    gtk_box_append(GTK_BOX(header), gtk_label_new("Dark mode"));
    a->dark = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(a->dark), TRUE);
    gtk_widget_set_valign(a->dark, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(a->dark, "Switch between dark and light appearance");
    gtk_box_append(GTK_BOX(header), a->dark);
    g_signal_connect(a->dark, "notify::active", G_CALLBACK(theme_changed), a);
    theme_changed(NULL, NULL, a);

    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(chat), actions);
    a->fresh = button(actions, "New Chat", G_CALLBACK(simple), a);
    a->copy = button(actions, "Copy", G_CALLBACK(copy_chat), a);
    a->export = button(actions, "Export…", G_CALLBACK(export_chat), a);
    GtkWidget *secondary[] = {a->fresh, a->copy, a->export};
    for (size_t i = 0; i < G_N_ELEMENTS(secondary); i++)
        gtk_widget_add_css_class(secondary[i], "flat");
    gtk_widget_set_tooltip_text(a->fresh, "Clear the conversation while keeping the model loaded");

    a->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(a->scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(a->scroll, TRUE);
    a->transcript = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(a->transcript, 12);
    gtk_widget_set_margin_bottom(a->transcript, 16);
    gtk_widget_set_margin_start(a->transcript, 4);
    gtk_widget_set_margin_end(a->transcript, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(a->scroll), a->transcript);
    gtk_box_append(GTK_BOX(chat), a->scroll);
    a->empty_chat = gtk_label_new("Your local workspace\n\nLoad a model from the sidebar, then send a message.");
    gtk_label_set_wrap(GTK_LABEL(a->empty_chat), TRUE);
    gtk_widget_set_margin_top(a->empty_chat, 80);
    gtk_widget_add_css_class(a->empty_chat, "muted");
    gtk_box_append(GTK_BOX(a->transcript), a->empty_chat);
    GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(a->scroll));
    double *previous_upper = g_new0(double, 1);
    g_object_set_data_full(G_OBJECT(a->scroll), "previous-upper", previous_upper, g_free);
    g_signal_connect(adjustment, "changed", G_CALLBACK(scroll_changed), previous_upper);

    GtkWidget *composer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(composer, "composer");
    gtk_box_append(GTK_BOX(chat), composer);
    a->prompt = text_view(composer, TRUE, -1, &a->prompt_scroll);
    gtk_widget_set_hexpand(a->prompt_scroll, TRUE);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(a->prompt), 10);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(a->prompt_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(a->prompt_scroll), 48);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(a->prompt_scroll), 200);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(a->prompt_scroll), TRUE);
    gtk_widget_set_tooltip_text(a->prompt, "Write a message. Return sends · Shift+Return adds a line · Escape stops generation.");
    a->send = button(composer, "Send", G_CALLBACK(send), a);
    gtk_widget_add_css_class(a->send, "suggested-action");
    gtk_widget_set_valign(a->send, GTK_ALIGN_END);
    gtk_widget_set_tooltip_text(a->send, "Send message (Return)");
    a->stop = button(composer, "Stop", G_CALLBACK(stop), a);
    gtk_widget_set_valign(a->stop, GTK_ALIGN_END);
    gtk_widget_add_css_class(a->stop, "destructive-action");
    gtk_widget_set_tooltip_text(a->stop, "Stop generation (Escape)");
    GtkEventController *key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(keys), a);
    gtk_widget_add_controller(a->prompt, key_controller);
    GtkEventController *window_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(window_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(window_controller, "key-pressed", G_CALLBACK(window_keys), a);
    gtk_widget_add_controller(GTK_WIDGET(a->window), window_controller);

    GtkWidget *diagnostics_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(chat), diagnostics_row);
    a->diagnostics_section = gtk_expander_new("Diagnostics");
    gtk_widget_set_hexpand(a->diagnostics_section, TRUE);
    gtk_box_append(GTK_BOX(diagnostics_row), a->diagnostics_section);
    a->run_diagnostics = button(diagnostics_row, "Run diagnostics", G_CALLBACK(run_diagnostics), a);
    gtk_widget_set_valign(a->run_diagnostics, GTK_ALIGN_START);
    gtk_widget_add_css_class(a->run_diagnostics, "flat");
    gtk_widget_set_tooltip_text(a->run_diagnostics,
        "Run verification, build/tests, inference checks, benchmarks and tuning in a live-output window. Unload the model first.");
    GtkWidget *diagnostics = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_expander_set_child(GTK_EXPANDER(a->diagnostics_section), diagnostics);
    a->metrics = gtk_label_new("Request timings appear after generation.");
    gtk_label_set_xalign(GTK_LABEL(a->metrics), 0);
    gtk_label_set_wrap(GTK_LABEL(a->metrics), TRUE);
    gtk_widget_add_css_class(a->metrics, "muted");
    gtk_box_append(GTK_BOX(diagnostics), a->metrics);
    a->logs = text_view(diagnostics, FALSE, 100, NULL);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(a->logs), TRUE);

    GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(statusbar, "statusbar");
    gtk_box_append(GTK_BOX(root), statusbar);
    a->summary = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(a->summary), 0);
    gtk_label_set_ellipsize(GTK_LABEL(a->summary), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text(a->summary, "Throughput and context describe the last request. Peak app memory includes the entire process, not just model weights.");
    gtk_box_append(GTK_BOX(statusbar), a->summary);
    GtkWidget *activity = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(statusbar), activity);
    a->spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(activity), a->spinner);
    a->status = gtk_label_new("Import or select a model, then Load.");
    gtk_label_set_xalign(GTK_LABEL(a->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(a->status), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(a->status, "muted");
    gtk_box_append(GTK_BOX(activity), a->status);

    a->preference_path =
        g_build_filename(g_get_user_config_dir(), "cinder-foundry", "preferences.ini", NULL);
    preferences(a, FALSE);
    cJSON *capabilities = cJSON_Parse(foundry_capabilities_json());
    a->gpu_available = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(capabilities, "gpu_available"));
    cJSON_Delete(capabilities);
    if (!a->gpu_available) {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->gpu), 0);
        gtk_widget_set_tooltip_text(a->gpu, "This build has no available GPU backend; inference uses the CPU.");
    }
    if (a->gpu_available)
        gtk_widget_set_tooltip_text(a->gpu, "0 uses CPU; -1 selects automatic GPU placement. Unload before changing.");
    gtk_widget_set_tooltip_text(a->top_p, "Nucleus sampling cutoff; used only when temperature is above zero.");
    gtk_widget_set_tooltip_text(a->seed, "Random sampling seed; used only when temperature is above zero.");
    g_signal_connect(a->temperature, "value-changed", G_CALLBACK(inputs_changed), a);
    g_signal_connect(a->dropdown, "notify::selected", G_CALLBACK(selected), a);
    g_signal_connect(a->model, "changed", G_CALLBACK(inputs_changed), a);
    g_signal_connect(a->context, "value-changed", G_CALLBACK(inputs_changed), a);
    g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->prompt)), "changed", G_CALLBACK(prompt_changed), a);
    controls(a);
    g_timeout_add(40, poll, a);
    gtk_window_maximize(a->window);
    gtk_window_present(a->window);
    refresh(NULL, a);
    if (a->smoke_test)
        g_timeout_add(750, smoke_close, a);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        g_print("Cinder Foundry %s · GTK %u.%u.%u · C17\n", FOUNDRY_VERSION,
                gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());
        return 0;
    }
    App a = {0};
    if (argc == 2 && !strcmp(argv[1], "--smoke-test")) {
        a.smoke_test = TRUE;
        argc = 1;
    }
    GtkApplication *app =
        gtk_application_new("io.github.cinderfoundry.desktop", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &a);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (a.worker) {
        gui_worker_close(a.worker);
        while (!gui_worker_stopped(a.worker))
            g_usleep(1000);
        gui_worker_free(a.worker);
    }
    if (a.history)
        g_ptr_array_unref(a.history);
    g_clear_object(&a.dialogs);
    g_clear_object(&a.models);
    gui_diagnostics_free(a.diagnostics);
    g_free(a.preference_path);
    g_free(a.loaded_model);
    g_free(a.conversation_model);
    if (a.response) g_string_free(a.response, TRUE);
    g_object_unref(app);
    return status;
}
