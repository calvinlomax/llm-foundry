#include "runtime_worker.h"
#include <string.h>

/* Tokens are split into <= 2048-byte UTF-8 chunks. At most 128 events are queued.
 * A slow renderer exerts backpressure; close/cancel always wakes the producer. */
#define EVENT_CAPACITY 128u
#define TOKEN_CHUNK 2048u
struct GuiWorker {
    GThread *thread;
    GMutex mutex;
    GCond changed;
    GQueue events;
    GuiJob *pending;
    gboolean busy, closing, cancelled, stopped;
    foundry_runtime *runtime;
    foundry_model *model;
    foundry_session *session; /* protected by mutex for cancellation/lifetime */
    foundry_config loaded_config;
};
typedef struct {
    GuiWorker *worker;
    GuiJob *job;
} TokenContext;

void gui_job_free(GuiJob *job) {
    if (!job)
        return;
    g_free(job->path);
    g_free(job->name);
    g_free(job->weights_root);
    g_free(job->metadata_root);
    g_free(job->stop);
    for (size_t i = 0; i < job->message_count; ++i) {
        g_free((char *)job->messages[i].role);
        g_free((char *)job->messages[i].content);
    }
    g_free(job->messages);
    g_free(job);
}
void gui_event_free(GuiEvent *event) {
    if (event) {
        g_free(event->text);
        g_free(event);
    }
}
static gboolean is_cancelled(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    gboolean value = worker->cancelled || worker->closing;
    g_mutex_unlock(&worker->mutex);
    return value;
}
static gboolean emit(GuiWorker *worker, GuiJob *job, GuiEventType type, const char *text,
                     size_t length, foundry_status status, const foundry_metrics *metrics) {
    GuiEvent *event = g_new0(GuiEvent, 1);
    event->type = type;
    event->session_id = job->session_id;
    event->generation_id = job->generation_id;
    event->status = status;
    if (text)
        event->text = g_strndup(text, length);
    if (metrics)
        event->metrics = *metrics;
    g_mutex_lock(&worker->mutex);
    while (worker->events.length >= EVENT_CAPACITY && !worker->closing &&
           !(worker->cancelled && type == GUI_EVENT_TOKEN))
        g_cond_wait(&worker->changed, &worker->mutex);
    if (worker->closing || (worker->cancelled && type == GUI_EVENT_TOKEN)) {
        g_mutex_unlock(&worker->mutex);
        gui_event_free(event);
        return FALSE;
    }
    if (type != GUI_EVENT_PROGRESS && type != GUI_EVENT_PREFILL && type != GUI_EVENT_TOKEN &&
        type != GUI_EVENT_METRICS)
        worker->busy = FALSE;
    g_queue_push_tail(&worker->events, event);
    g_mutex_unlock(&worker->mutex);
    return TRUE;
}
static void notice(GuiWorker *worker, GuiJob *job, GuiEventType type, const char *text) {
    emit(worker, job, type, text, text ? strlen(text) : 0, FOUNDRY_OK, NULL);
}
static bool on_token(const char *bytes, size_t length, void *userdata) {
    TokenContext *ctx = userdata;
    while (length) {
        if (is_cancelled(ctx->worker))
            return false;
        size_t take = MIN(length, TOKEN_CHUNK);
        if (take < length) {
            while (take && (((unsigned char)bytes[take] & 0xc0) == 0x80))
                --take;
        }
        if (!take || !emit(ctx->worker, ctx->job, GUI_EVENT_TOKEN, bytes, take, FOUNDRY_OK, NULL))
            return false;
        bytes += take;
        length -= take;
    }
    return !is_cancelled(ctx->worker);
}
static void destroy_session(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    foundry_session *session = worker->session;
    worker->session = NULL;
    g_mutex_unlock(&worker->mutex);
    if (session)
        foundry_session_destroy(session);
}
static foundry_status unload(GuiWorker *worker, foundry_error *error) {
    destroy_session(worker);
    if (!worker->model)
        return FOUNDRY_OK;
    foundry_status status = foundry_model_unload(worker->model, error);
    if (status == FOUNDRY_OK)
        worker->model = NULL;
    return status;
}
static foundry_status generate(GuiWorker *worker, GuiJob *job, foundry_error *error) {
    if (!worker->model) {
        error->code = FOUNDRY_NOT_FOUND;
        g_strlcpy(error->message, "Load a model before sending a message.", sizeof(error->message));
        return error->code;
    }
    if (job->config.context != worker->loaded_config.context ||
        job->config.gpu_layers != worker->loaded_config.gpu_layers ||
        job->config.threads != worker->loaded_config.threads) {
        error->code = FOUNDRY_INVALID_CONFIG;
        g_strlcpy(error->message, "Unload and reload to change context, threads, or GPU placement.",
                  sizeof(error->message));
        return error->code;
    }
    destroy_session(worker);
    foundry_session *session = NULL;
    foundry_status status = foundry_session_create(worker->model, &job->config, &session, error);
    if (status != FOUNDRY_OK)
        return status;
    g_mutex_lock(&worker->mutex);
    worker->session = session;
    if (worker->cancelled || worker->closing)
        foundry_request_cancel(session);
    g_mutex_unlock(&worker->mutex);
    if (is_cancelled(worker))
        return FOUNDRY_CANCELLED;
    char *prompt = NULL;
    status = foundry_chat_render(worker->model, job->messages, job->message_count, &prompt, error);
    if (status != FOUNDRY_OK)
        return status;
    notice(worker, job, GUI_EVENT_PREFILL, "Processing conversation…");
    TokenContext context = {worker, job};
    foundry_metrics metrics = {0};
    status = foundry_generate(session, prompt, true, on_token, &context, &metrics, error);
    foundry_free(prompt);
    emit(worker, job, GUI_EVENT_METRICS, NULL, 0, status, &metrics);
    return status;
}
static void run_job(GuiWorker *worker, GuiJob *job) {
    foundry_error error = {0};
    foundry_status status = FOUNDRY_OK;
    char *json = NULL;
    GuiEventType result = GUI_EVENT_COMPLETE;
    switch (job->type) {
    case GUI_JOB_LIST:
        status = foundry_registry_list(&json, &error);
        result = GUI_EVENT_REGISTRY;
        break;
    case GUI_JOB_INSPECT: {
        foundry_model_info info = {0};
        status = foundry_inspect(job->path, &info, &json, &error);
        result = GUI_EVENT_DETAILS;
        break;
    }
    case GUI_JOB_PLAN:
        status = foundry_plan(job->path, &job->config, &json, &error);
        result = GUI_EVENT_PLAN;
        break;
    case GUI_JOB_IMPORT_GGUF:
        notice(worker, job, GUI_EVENT_PROGRESS, "Validating and importing local weights…");
        status = foundry_import_gguf(job->path, job->name, job->copy, &json, &error);
        result = GUI_EVENT_IMPORTED;
        break;
    case GUI_JOB_IMPORT_OLLAMA:
        notice(worker, job, GUI_EVENT_PROGRESS, "Checking manifest, hashes, and local artifacts…");
        status = foundry_import_ollama(job->path, job->weights_root, job->metadata_root, job->name,
                                       job->copy, &json, &error);
        result = GUI_EVENT_IMPORTED;
        break;
    case GUI_JOB_LOAD:
        notice(worker, job, GUI_EVENT_PROGRESS, "Loading model…");
        status = unload(worker, &error);
        if (status == FOUNDRY_OK && !worker->runtime)
            status = foundry_runtime_create(&worker->runtime, &error);
        if (status == FOUNDRY_OK && !is_cancelled(worker)) {
            status = foundry_model_load(worker->runtime, job->path, &job->config, &worker->model,
                                        &error);
            if (status == FOUNDRY_OK)
                worker->loaded_config = job->config;
        }
        result = GUI_EVENT_READY;
        break;
    case GUI_JOB_UNLOAD:
        status = unload(worker, &error);
        result = GUI_EVENT_UNLOADED;
        break;
    case GUI_JOB_RESET:
        destroy_session(worker);
        result = GUI_EVENT_RESET;
        break;
    case GUI_JOB_GENERATE:
        status = generate(worker, job, &error);
        break;
    }
    if (status == FOUNDRY_CANCELLED || (is_cancelled(worker) && job->type == GUI_JOB_GENERATE))
        notice(worker, job, GUI_EVENT_CANCELLED, "Stopped. Partial response retained.");
    else if (status != FOUNDRY_OK) {
        const char *message = error.message[0] ? error.message : foundry_status_string(status);
        emit(worker, job, GUI_EVENT_FAILURE, message, strlen(message), status, NULL);
    } else
        notice(worker, job, result, json);
    foundry_free(json);
}
static gpointer worker_main(gpointer data) {
    GuiWorker *worker = data;
    for (;;) {
        g_mutex_lock(&worker->mutex);
        while (!worker->pending && !worker->closing)
            g_cond_wait(&worker->changed, &worker->mutex);
        if (worker->closing) {
            g_mutex_unlock(&worker->mutex);
            break;
        }
        GuiJob *job = worker->pending;
        worker->pending = NULL;
        g_mutex_unlock(&worker->mutex);
        run_job(worker, job);
        gui_job_free(job);
    }
    foundry_error error = {0};
    unload(worker, &error);
    if (worker->runtime)
        foundry_runtime_destroy(worker->runtime, &error);
    g_mutex_lock(&worker->mutex);
    worker->stopped = TRUE;
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->mutex);
    return NULL;
}
GuiWorker *gui_worker_new(void) {
    GuiWorker *worker = g_new0(GuiWorker, 1);
    g_mutex_init(&worker->mutex);
    g_cond_init(&worker->changed);
    g_queue_init(&worker->events);
    worker->thread = g_thread_new("foundry-runtime", worker_main, worker);
    return worker;
}
gboolean gui_worker_submit(GuiWorker *worker, GuiJob *job) {
    g_mutex_lock(&worker->mutex);
    if (worker->busy || worker->closing) {
        g_mutex_unlock(&worker->mutex);
        return FALSE;
    }
    worker->busy = TRUE;
    worker->cancelled = FALSE;
    worker->pending = job;
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->mutex);
    return TRUE;
}
GuiEvent *gui_worker_poll(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    GuiEvent *event = g_queue_pop_head(&worker->events);
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->mutex);
    return event;
}
void gui_worker_cancel(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    worker->cancelled = TRUE;
    if (worker->session)
        foundry_request_cancel(worker->session);
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->mutex);
}
void gui_worker_close(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    worker->closing = worker->cancelled = TRUE;
    if (worker->session)
        foundry_request_cancel(worker->session);
    g_cond_broadcast(&worker->changed);
    g_mutex_unlock(&worker->mutex);
}
gboolean gui_worker_stopped(GuiWorker *worker) {
    g_mutex_lock(&worker->mutex);
    gboolean value = worker->stopped;
    g_mutex_unlock(&worker->mutex);
    return value;
}
void gui_worker_free(GuiWorker *worker) {
    g_return_if_fail(gui_worker_stopped(worker));
    g_thread_join(worker->thread);
    gui_job_free(worker->pending);
    GuiEvent *event;
    while ((event = g_queue_pop_head(&worker->events)))
        gui_event_free(event);
    g_cond_clear(&worker->changed);
    g_mutex_clear(&worker->mutex);
    g_free(worker);
}
