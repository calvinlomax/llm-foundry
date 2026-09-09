#ifndef FOUNDRY_GUI_RUNTIME_WORKER_H
#define FOUNDRY_GUI_RUNTIME_WORKER_H
#include "foundry/foundry.h"
#include <glib.h>

typedef enum {
    GUI_JOB_LIST,
    GUI_JOB_INSPECT,
    GUI_JOB_PLAN,
    GUI_JOB_IMPORT_GGUF,
    GUI_JOB_IMPORT_OLLAMA,
    GUI_JOB_LOAD,
    GUI_JOB_UNLOAD,
    GUI_JOB_GENERATE,
    GUI_JOB_RESET
} GuiJobType;

typedef struct {
    GuiJobType type;
    guint64 session_id, generation_id;
    foundry_config config;
    char *path, *name, *weights_root, *metadata_root, *stop;
    gboolean copy;
    foundry_message *messages;
    size_t message_count;
} GuiJob;

typedef enum {
    GUI_EVENT_PROGRESS,
    GUI_EVENT_READY,
    GUI_EVENT_PREFILL,
    GUI_EVENT_TOKEN,
    GUI_EVENT_METRICS,
    GUI_EVENT_COMPLETE,
    GUI_EVENT_CANCELLED,
    GUI_EVENT_FAILURE,
    GUI_EVENT_REGISTRY,
    GUI_EVENT_DETAILS,
    GUI_EVENT_PLAN,
    GUI_EVENT_IMPORTED,
    GUI_EVENT_UNLOADED,
    GUI_EVENT_RESET
} GuiEventType;

typedef struct {
    GuiEventType type;
    guint64 session_id, generation_id;
    foundry_status status;
    char *text;
    foundry_metrics metrics;
} GuiEvent;

typedef struct GuiWorker GuiWorker;
GuiWorker *gui_worker_new(void);
/* submit takes ownership only on success; exactly one job can be outstanding. */
gboolean gui_worker_submit(GuiWorker *worker, GuiJob *job);
GuiEvent *gui_worker_poll(GuiWorker *worker);
void gui_worker_cancel(GuiWorker *worker);
void gui_worker_close(GuiWorker *worker);
gboolean gui_worker_stopped(GuiWorker *worker);
/* Call only after stopped; joins a completed thread without blocking GTK. */
void gui_worker_free(GuiWorker *worker);
void gui_job_free(GuiJob *job);
void gui_event_free(GuiEvent *event);
#endif
