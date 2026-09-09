#include "cJSON.h"
#include "runtime_worker.h"
#include <gtk/gtk.h>
#include <string.h>

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GuiWorker *worker;
    GtkWidget *model, *name, *weights, *metadata, *dropdown, *prompt, *transcript, *details,
        *status, *metrics;
    GtkWidget *context, *tokens, *temperature, *top_p, *seed, *threads, *gpu;
    GtkWidget *load, *unload, *send, *stop, *fresh, *import_gguf, *import_ollama, *inspect, *plan,
        *refresh;
    GtkWidget *scroll;
    GtkStringList *models;
    GPtrArray *history;
    gboolean busy, loaded, generating, closing, smoke_test;
    guint64 session_id, generation_id;
    GCancellable *dialogs;
    char *preference_path;
} App;
static const char *entry(GtkWidget *w) {
    return gtk_editable_get_text(GTK_EDITABLE(w));
}
static void set_status(App *a, const char *s) {
    gtk_label_set_text(GTK_LABEL(a->status), s ? s : "");
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
static void controls(App *a) {
    gboolean idle = !a->busy && !a->closing;
    gtk_widget_set_sensitive(a->load, idle);
    gtk_widget_set_sensitive(a->unload, idle && a->loaded);
    gtk_widget_set_sensitive(a->send, idle && a->loaded);
    gtk_widget_set_sensitive(a->fresh, idle && a->loaded);
    gtk_widget_set_sensitive(a->stop, a->generating && !a->closing);
    GtkWidget *ws[] = {a->import_gguf, a->import_ollama, a->inspect, a->plan,
                       a->refresh,     a->model,         a->dropdown};
    for (size_t i = 0; i < G_N_ELEMENTS(ws); i++)
        gtk_widget_set_sensitive(ws[i], idle);
}
static void append(App *a, const char *text) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->transcript));
    GtkTextIter end;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(a->scroll));
    gboolean bottom = gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj) >=
                      gtk_adjustment_get_upper(adj) - 40;
    gtk_text_buffer_get_end_iter(b, &end);
    gtk_text_buffer_insert(b, &end, text, -1);
    if (bottom) {
        gtk_text_buffer_get_end_iter(b, &end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(a->transcript), &end, 0, FALSE, 0, 1);
    }
}
static void clear_chat(App *a) {
    g_ptr_array_set_size(a->history, 0);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->transcript)), "", 0);
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
    if (a->closing || !gui_worker_submit(a->worker, j)) {
        gui_job_free(j);
        set_status(a, "Finish the active operation first.");
        return FALSE;
    }
    a->busy = TRUE;
    controls(a);
    return TRUE;
}
static void selected(GObject *object, GParamSpec *spec, gpointer data) {
    (void)object;
    (void)spec;
    App *a = data;
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->dropdown));
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
    if (a->busy || !a->loaded)
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
        history_add(a, "user", text);
        history_add(a, "assistant", "");
        append(a, "You\n");
        append(a, text);
        append(a, "\n\nAssistant\n");
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
    if (key == GDK_KEY_Return && (state & GDK_CONTROL_MASK)) {
        send(NULL, a);
        return TRUE;
    }
    if (key == GDK_KEY_Escape && a->generating) {
        stop(NULL, a);
        return TRUE;
    }
    return FALSE;
}
static char *conversation_json(App *a) {
    cJSON *doc = cJSON_CreateObject(), *messages = cJSON_AddArrayToObject(doc, "messages");
    cJSON_AddNumberToObject(doc, "schema_version", 1);
    cJSON_AddStringToObject(doc, "model", entry(a->model));
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
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->transcript));
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    char *text = gtk_text_buffer_get_text(b, &s, &e, FALSE);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(a->window)), text);
    g_free(text);
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
    cJSON *j = cJSON_Parse(text ? text : "{}");
    if (j) {
        cJSON_DeleteItemFromObjectCaseSensitive(j, "metadata");
        cJSON_DeleteItemFromObjectCaseSensitive(j, "metadata_types");
        cJSON_DeleteItemFromObjectCaseSensitive(j, "tensors");
    }
    char *pretty = j ? cJSON_Print(j) : NULL;
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(a->details)),
                             pretty ? pretty : (text ? text : ""), -1);
    foundry_free(pretty);
    cJSON_Delete(j);
}
static gboolean poll(gpointer data) {
    App *a = data;
    if (a->closing && gui_worker_stopped(a->worker)) {
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
            char *label = g_strdup_printf(
                "Load %.0f ms  ·  First token %.0f ms\nPrefill %.0f ms  ·  Decode %.0f ms\n%u "
                "input / %u output tokens  ·  %.1f tokens/s\nStop: %s  ·  Observed memory: "
                "unavailable",
                m->load_ms, m->first_token_ms, m->prefill_ms, m->decode_ms, m->prompt_tokens,
                m->generated_tokens,
                m->decode_ms > 0 ? m->generated_tokens * 1000.0 / m->decode_ms : 0, m->stop_reason);
            gtk_label_set_text(GTK_LABEL(a->metrics), label);
            g_free(label);
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
                clear_chat(a);
                set_status(a, "Ready. Model weights stay loaded between turns.");
            } else if (ev->type == GUI_EVENT_UNLOADED) {
                a->loaded = FALSE;
                set_status(a, "Model unloaded.");
            } else if (ev->type == GUI_EVENT_RESET) {
                clear_chat(a);
                set_status(a, "New chat. Weights remain loaded.");
            } else if (ev->type == GUI_EVENT_REGISTRY) {
                cJSON *doc = cJSON_Parse(ev->text),
                      *items = cJSON_GetObjectItemCaseSensitive(doc, "models");
                gtk_string_list_splice(a->models, 0,
                                       g_list_model_get_n_items(G_LIST_MODEL(a->models)), NULL);
                cJSON *item;
                cJSON_ArrayForEach(item, items) {
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
                    if (cJSON_IsString(name))
                        gtk_string_list_append(a->models, name->valuestring);
                }
                cJSON_Delete(doc);
                set_status(a, "Local registry refreshed.");
            } else if (ev->type == GUI_EVENT_IMPORTED) {
                cJSON *doc = cJSON_Parse(ev->text),
                      *name = cJSON_GetObjectItemCaseSensitive(doc, "name");
                if (cJSON_IsString(name))
                    gtk_editable_set_text(GTK_EDITABLE(a->model), name->valuestring);
                cJSON_Delete(doc);
                set_status(
                    a, "Imported and hash verified. Review details and memory plan, then Load.");
            } else if (ev->type == GUI_EVENT_DETAILS || ev->type == GUI_EVENT_PLAN) {
                show_json(a, ev->text);
                set_status(a, "Model details updated.");
            } else if (ev->type == GUI_EVENT_FAILURE) {
                set_status(a, ev->text);
                append(a, "\n\n");
            } else {
                set_status(a, ev->type == GUI_EVENT_CANCELLED ? ev->text : "Ready.");
                append(a, "\n\n");
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
        preferences(a, TRUE);
        a->closing = TRUE;
        g_cancellable_cancel(a->dialogs);
        gui_worker_close(a->worker);
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
    gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    GtkWidget *e = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e), initial);
    gtk_box_append(GTK_BOX(box), e);
    return e;
}
static GtkWidget *spin(GtkWidget *box, const char *label, double low, double high, double step,
                       double initial) {
    gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    GtkWidget *s = gtk_spin_button_new_with_range(low, high, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), initial);
    gtk_box_append(GTK_BOX(box), s);
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
static void activate(GtkApplication *app, gpointer data) {
    App *a = data;
    if (a->window) {
        gtk_window_present(a->window);
        return;
    }
    a->application = app;
    a->worker = gui_worker_new();
    a->history = g_ptr_array_new_with_free_func(message_free);
    a->dialogs = g_cancellable_new();
    a->session_id = 1;
    a->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(a->window, "Cinder Foundry");
    gtk_window_set_default_size(a->window, 1150, 780);
    g_signal_connect(a->window, "close-request", G_CALLBACK(close_window), a);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_window_set_child(a->window, root);
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);
    GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6),
              *chat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(side, 280, -1);
    GtkWidget *sidebar_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroll), side);
    gtk_paned_set_start_child(GTK_PANED(paned), sidebar_scroll);
    gtk_paned_set_end_child(GTK_PANED(paned), chat);
    gtk_paned_set_position(GTK_PANED(paned), 300);
    a->models = gtk_string_list_new(NULL);
    a->dropdown = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(a->models)), NULL);
    gtk_box_append(GTK_BOX(side), gtk_label_new("Local model library"));
    gtk_box_append(GTK_BOX(side), a->dropdown);
    g_signal_connect(a->dropdown, "notify::selected", G_CALLBACK(selected), a);
    a->refresh = button(side, "Refresh library", G_CALLBACK(refresh), a);
    a->model = input(side, "Model ID or local file path", "");
    a->name = input(side, "Import name", "smollm2");
    a->weights = input(side, "Ollama weight directory", ".");
    a->metadata = input(side, "Ollama metadata directory", "metadata");
    a->import_gguf = button(side, "Import GGUF…", G_CALLBACK(import_model), a);
    a->import_ollama = button(side, "Import Ollama manifest…", G_CALLBACK(import_model), a);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(side), actions);
    a->inspect = button(actions, "Inspect", G_CALLBACK(simple), a);
    a->plan = button(actions, "Memory plan", G_CALLBACK(simple), a);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(side), actions);
    a->load = button(actions, "Load", G_CALLBACK(simple), a);
    a->unload = button(actions, "Unload", G_CALLBACK(simple), a);
    GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4),
              *expand = gtk_expander_new("Generation settings");
    gtk_expander_set_child(GTK_EXPANDER(expand), settings);
    gtk_box_append(GTK_BOX(side), expand);
    a->context = spin(settings, "Context (reload to apply)", 128, 131072, 128, 2048);
    a->tokens = spin(settings, "Maximum new tokens", 1, 32768, 1, 128);
    a->temperature = spin(settings, "Temperature (0 = greedy)", 0, 2, .05, 0);
    a->top_p = spin(settings, "Top-p", .01, 1, .01, .95);
    a->seed = spin(settings, "Seed", 0, 2147483647, 1, 0);
    a->threads = spin(settings, "CPU threads (reload to apply)", 1, 128, 1, 4);
    a->gpu = spin(settings, "GPU layers (0 CPU, -1 auto; reload)", -1, 999, 1, 0);
    a->details = text_view(side, FALSE, 130, NULL);
    gtk_widget_set_vexpand(gtk_widget_get_parent(a->details), TRUE);
    gtk_box_append(GTK_BOX(chat),
                   gtk_label_new("Local chat · Ctrl+Enter to send · Escape to stop"));
    a->transcript = text_view(chat, FALSE, 250, &a->scroll);
    gtk_widget_set_vexpand(a->scroll, TRUE);
    a->prompt = text_view(chat, TRUE, 95, NULL);
    GtkEventController *keys_controller = gtk_event_controller_key_new();
    g_signal_connect(keys_controller, "key-pressed", G_CALLBACK(keys), a);
    gtk_widget_add_controller(a->prompt, keys_controller);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(chat), actions);
    a->send = button(actions, "Send", G_CALLBACK(send), a);
    a->stop = button(actions, "Stop", G_CALLBACK(stop), a);
    a->fresh = button(actions, "New Chat", G_CALLBACK(simple), a);
    button(actions, "Copy", G_CALLBACK(copy_chat), a);
    button(actions, "Export…", G_CALLBACK(export_chat), a);
    a->metrics = gtk_label_new("Timing and memory observations appear after inference.");
    gtk_label_set_xalign(GTK_LABEL(a->metrics), 0);
    gtk_box_append(GTK_BOX(chat), a->metrics);
    a->status = gtk_label_new("Import a local model, inspect its memory plan, then Load.");
    gtk_label_set_xalign(GTK_LABEL(a->status), 0);
    gtk_label_set_wrap(GTK_LABEL(a->status), TRUE);
    gtk_box_append(GTK_BOX(root), a->status);
    a->preference_path =
        g_build_filename(g_get_user_config_dir(), "cinder-foundry", "preferences.ini", NULL);
    preferences(a, FALSE);
    controls(a);
    g_timeout_add(40, poll, a);
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
    g_free(a.preference_path);
    g_object_unref(app);
    return status;
}
