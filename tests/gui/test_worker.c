#include "runtime_worker.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static GuiJob *job(GuiJobType type, guint64 id, const char *path) {
    GuiJob *j = g_new0(GuiJob, 1);
    j->type = type;
    j->generation_id = id;
    j->session_id = 1;
    j->path = g_strdup(path);
    j->config = foundry_config_default();
    j->config.context = 256;
    j->config.max_tokens = 16;
    return j;
}
static gboolean terminal(GuiEventType t) {
    return t != GUI_EVENT_TOKEN && t != GUI_EVENT_PROGRESS && t != GUI_EVENT_PREFILL &&
           t != GUI_EVENT_METRICS;
}
static GuiEventType drain(GuiWorker *w, gboolean cancel_on_token, GString *text) {
    gint64 deadline = g_get_monotonic_time() + 90 * G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline) {
        GuiEvent *e = gui_worker_poll(w);
        if (!e) {
            g_usleep(1000);
            continue;
        }
        GuiEventType type = e->type;
        if (type == GUI_EVENT_TOKEN) {
            if (text)
                g_string_append(text, e->text);
            if (cancel_on_token)
                gui_worker_cancel(w);
        }
        if (type == GUI_EVENT_FAILURE)
            fprintf(stderr, "Worker error: %s\n", e->text);
        gui_event_free(e);
        if (terminal(type))
            return type;
    }
    return GUI_EVENT_FAILURE;
}
int main(int argc, char **argv) {
    GuiWorker *w = gui_worker_new();
    CHECK(gui_worker_submit(w, job(GUI_JOB_LIST, 1, "")));
    CHECK(drain(w, FALSE, NULL) == GUI_EVENT_REGISTRY);
    if (argc > 1) {
        CHECK(gui_worker_submit(w, job(GUI_JOB_LOAD, 2, argv[1])));
        CHECK(drain(w, FALSE, NULL) == GUI_EVENT_READY);
        GuiJob *j = job(GUI_JOB_GENERATE, 3, argv[1]);
        j->message_count = 1;
        j->messages = g_new0(foundry_message, 1);
        j->messages[0] =
            (foundry_message){g_strdup("user"), g_strdup("What is the capital of France?")};
        GString *text = g_string_new(NULL);
        CHECK(gui_worker_submit(w, j));
        CHECK(drain(w, FALSE, text) == GUI_EVENT_COMPLETE);
        CHECK(text->len > 0);
        g_print("Worker output: %s\n", text->str);
        g_string_free(text, TRUE);
        j = job(GUI_JOB_GENERATE, 4, argv[1]);
        j->message_count = 1;
        j->messages = g_new0(foundry_message, 1);
        j->messages[0] =
            (foundry_message){g_strdup("user"), g_strdup("Describe a hash table in detail.")};
        CHECK(gui_worker_submit(w, j));
        CHECK(drain(w, TRUE, NULL) == GUI_EVENT_CANCELLED);
        CHECK(gui_worker_submit(w, job(GUI_JOB_RESET, 5, argv[1])));
        CHECK(drain(w, FALSE, NULL) == GUI_EVENT_RESET);
    }
    gui_worker_close(w);
    gint64 deadline = g_get_monotonic_time() + 30 * G_TIME_SPAN_SECOND;
    while (!gui_worker_stopped(w) && g_get_monotonic_time() < deadline)
        g_usleep(1000);
    CHECK(gui_worker_stopped(w));
    gui_worker_free(w);
    puts("GUI worker lifecycle passed");
    return 0;
}
