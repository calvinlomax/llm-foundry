#define _POSIX_C_SOURCE 200809L
#include "import_internal.h"
#include "runtime_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void foundry_clear_error(foundry_error *e) {
    if (e) {
        e->code = FOUNDRY_OK;
        e->message[0] = '\0';
    }
}
foundry_status foundry_fail(foundry_error *e, foundry_status code, const char *fmt, ...) {
    if (e) {
        va_list args;
        e->code = code;
        va_start(args, fmt);
        vsnprintf(e->message, sizeof(e->message), fmt, args);
        va_end(args);
    }
    return code;
}
const char *foundry_status_string(foundry_status s) {
    static const char *names[] = {"ok",
                                  "invalid_config",
                                  "not_found",
                                  "corrupt_model",
                                  "unsupported",
                                  "out_of_memory",
                                  "backend_error",
                                  "cancelled",
                                  "busy",
                                  "remote_only_model",
                                  "io_error",
                                  "context_exceeded",
                                  "end_of_generation"};
    return s >= FOUNDRY_OK && s <= FOUNDRY_END_OF_GENERATION ? names[s] : "unknown";
}
void foundry_free(void *p) {
    free(p);
}
foundry_status foundry_runtime_create(foundry_runtime **out, foundry_error *e) {
    foundry_clear_error(e);
    if (!out)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Runtime output pointer is required");
    *out = NULL;
    foundry_runtime *r = calloc(1, sizeof(*r));
    if (!r)
        return foundry_fail(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate runtime");
    foundry_status s = fb_init(e);
    if (s != FOUNDRY_OK) {
        free(r);
        return s;
    }
    *out = r;
    return FOUNDRY_OK;
}
foundry_status foundry_runtime_destroy(foundry_runtime *r, foundry_error *e) {
    foundry_clear_error(e);
    if (!r)
        return FOUNDRY_OK;
    if (r->models)
        return foundry_fail(e, FOUNDRY_BUSY, "Unload all models before destroying the runtime");
    fb_shutdown();
    free(r);
    return FOUNDRY_OK;
}
const char *foundry_capabilities_json(void) {
    return fb_capabilities();
}
foundry_status foundry_model_load(foundry_runtime *r, const char *id, const foundry_config *config,
                                  foundry_model **out, foundry_error *e) {
    foundry_clear_error(e);
    if (out)
        *out = NULL;
    if (!r || !id || !*id || !out)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "Runtime, model path/ID and output are required");
    foundry_config c = config ? *config : foundry_config_default();
    foundry_status s = foundry_config_validate(&c, e);
    if (s)
        return s;
    foundry_model *m = calloc(1, sizeof(*m));
    if (!m)
        return foundry_fail(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate model handle");
    double start = foundry_now_ms();
    s = foundry_resolve(id, &m->path, e);
    if (!s) {
        foundry_model_info info;
        s = foundry_inspect(m->path, &info, NULL, e);
        if (!s && info.trained_context && c.context > info.trained_context)
            s = foundry_fail(
                e, FOUNDRY_CONTEXT_EXCEEDED,
                "Requested context %u exceeds model training context %u; select a smaller context",
                c.context, info.trained_context);
    }
    if (!s && c.memory_budget) {
        uint64_t bytes = 0;
        s = foundry_plan_estimate(m->path, &c, &bytes, e);
    }
    if (!s)
        s = fb_load(m->path, &c, &m->backend, e);
    if (s) {
        free(m->path);
        free(m);
        return s;
    }
    m->runtime = r;
    m->config = c;
    m->config.stop = NULL;
    m->load_ms = foundry_now_ms() - start;
    r->models++;
    *out = m;
    return FOUNDRY_OK;
}
foundry_status foundry_model_unload(foundry_model *m, foundry_error *e) {
    foundry_clear_error(e);
    if (!m)
        return FOUNDRY_OK;
    if (m->sessions)
        return foundry_fail(e, FOUNDRY_BUSY, "Destroy all sessions before unloading the model");
    fb_unload(m->backend);
    m->runtime->models--;
    free(m->path);
    free(m);
    return FOUNDRY_OK;
}
foundry_status foundry_tokenize(foundry_model *m, const char *text, bool add_special,
                                bool parse_special, int32_t **tokens, size_t *count,
                                foundry_error *e) {
    foundry_clear_error(e);
    if (tokens)
        *tokens = NULL;
    if (count)
        *count = 0;
    if (!m || !text || !tokens || !count || !foundry_utf8_valid(text))
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "Model, valid UTF-8 text and token outputs are required");
    return fb_tokenize(m->backend, text, add_special, parse_special, tokens, count, e);
}
foundry_status foundry_chat_render(foundry_model *m, const foundry_message *messages, size_t count,
                                   char **text, foundry_error *e) {
    foundry_clear_error(e);
    if (text)
        *text = NULL;
    if (!m || !messages || !count || !text || count > 100000)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "Model, nonempty message history and output are required");
    for (size_t i = 0; i < count; i++) {
        if (!messages[i].role || !messages[i].content || !foundry_utf8_valid(messages[i].content))
            return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                                "Chat roles and valid UTF-8 content are required");
        if (strcmp(messages[i].role, "system") && strcmp(messages[i].role, "user") &&
            strcmp(messages[i].role, "assistant"))
            return foundry_fail(e, FOUNDRY_UNSUPPORTED,
                                "Chat supports system, user and assistant roles only");
    }
    return fb_chat_render(m->backend, messages, count, text, e);
}
