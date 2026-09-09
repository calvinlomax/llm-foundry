#include "import_internal.h"
#include "runtime_internal.h"
#include "stream.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
    foundry_token_callback callback;
    void *data;
    double start, first_ms;
    bool emitted;
} emission;
static bool emit_timed(const char *p, size_t n, void *data) {
    emission *x = data;
    if (n && !x->emitted) {
        x->first_ms = foundry_now_ms() - x->start;
        x->emitted = true;
    }
    return !x->callback || x->callback(p, n, x->data);
}
static bool cancelled(void *p) {
    return atomic_load(&((foundry_session *)p)->cancelled);
}
static foundry_status finish(foundry_session *s, foundry_status status, foundry_error *e) {
    if (e)
        s->error = *e;
    atomic_store(&s->active, false);
    return status;
}
foundry_status foundry_session_create(foundry_model *m, const foundry_config *config,
                                      foundry_session **out, foundry_error *e) {
    foundry_clear_error(e);
    if (out)
        *out = NULL;
    if (!m || !out)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Model and session output required");
    if (m->sessions)
        return foundry_fail(
            e, FOUNDRY_BUSY,
            "v0.1 allows one session per model; destroy the previous session first");
    foundry_config c = config ? *config : m->config;
    foundry_status status = foundry_config_validate(&c, e);
    if (status)
        return status;
    if (c.gpu_layers != m->config.gpu_layers)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "GPU placement changes require reloading weights");
    foundry_model_info info;
    status = foundry_inspect(m->path, &info, NULL, e);
    if (status)
        return status;
    if (info.trained_context && c.context > info.trained_context)
        return foundry_fail(e, FOUNDRY_CONTEXT_EXCEEDED, "Context exceeds trained context");
    if (c.memory_budget) {
        uint64_t total;
        status = foundry_plan_estimate(m->path, &c, &total, e);
        if (status)
            return status;
    }
    foundry_session *s = calloc(1, sizeof *s);
    if (!s)
        return foundry_fail(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate session");
    s->model = m;
    s->config = c;
    s->load_ms = m->load_ms;
    atomic_init(&s->cancelled, false);
    atomic_init(&s->active, false);
    if (c.stop) {
        s->stop = fi_strdup(c.stop);
        if (!s->stop) {
            free(s);
            return foundry_fail(e, FOUNDRY_OUT_OF_MEMORY, "Cannot copy stop sequence");
        }
        s->config.stop = s->stop;
    }
    status = fb_session_create(m->backend, &s->config, cancelled, s, &s->backend, e);
    if (status) {
        free(s->stop);
        free(s);
        return status;
    }
    m->sessions++;
    *out = s;
    return FOUNDRY_OK;
}
foundry_status foundry_session_reset(foundry_session *s, foundry_error *e) {
    foundry_clear_error(e);
    if (!s)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Session required");
    if (atomic_exchange(&s->active, true))
        return foundry_fail(e, FOUNDRY_BUSY, "Session is generating");
    atomic_store(&s->cancelled, false);
    foundry_status status = fb_reset(s->backend, e);
    s->token_count = 0;
    s->ended = false;
    return finish(s, status, e);
}
void foundry_session_destroy(foundry_session *s) {
    if (!s)
        return;
    fb_session_destroy(s->backend);
    s->model->sessions--;
    free(s->stop);
    free(s);
}
void foundry_request_cancel(foundry_session *s) {
    if (s)
        atomic_store(&s->cancelled, true);
}
const foundry_error *foundry_last_error(const foundry_session *s) {
    return s ? &s->error : NULL;
}
foundry_status foundry_prefill(foundry_session *s, const int32_t *tokens, size_t count,
                               foundry_error *e) {
    foundry_clear_error(e);
    if (!s || !tokens || !count)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Nonempty token input required");
    if (atomic_exchange(&s->active, true))
        return foundry_fail(e, FOUNDRY_BUSY, "Session is in use");
    foundry_status st = FOUNDRY_OK;
    if (s->ended)
        st = foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Reset ended session before prefill");
    else if (count > s->config.context - s->token_count)
        st = foundry_fail(e, FOUNDRY_CONTEXT_EXCEEDED, "Prompt exceeds context");
    else {
        st = fb_prefill(s->backend, tokens, count, e);
        if (!st)
            s->token_count += count;
        else
            s->ended = true;
    }
    return finish(s, st, e);
}
foundry_status foundry_decode(foundry_session *s, int32_t *token, char **bytes, size_t *length,
                              foundry_error *e) {
    foundry_clear_error(e);
    if (bytes)
        *bytes = NULL;
    if (length)
        *length = 0;
    if (!s || !token || !bytes || !length)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Session and decode outputs required");
    if (atomic_exchange(&s->active, true))
        return foundry_fail(e, FOUNDRY_BUSY, "Session is in use");
    foundry_status st;
    if (s->ended)
        st = FOUNDRY_END_OF_GENERATION;
    else if (!s->token_count)
        st = foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Prefill before decoding");
    else if (s->token_count >= s->config.context)
        st = foundry_fail(e, FOUNDRY_CONTEXT_EXCEEDED, "Context is full");
    else {
        st = fb_decode(s->backend, token, bytes, length, e);
        if (!st)
            s->token_count++;
        else
            s->ended = true;
    }
    return finish(s, st, e);
}
foundry_status foundry_generate(foundry_session *s, const char *prompt, bool parse_special,
                                foundry_token_callback cb, void *data, foundry_metrics *metrics,
                                foundry_error *err) {
    foundry_error local = {0};
    foundry_error *e = err ? err : &local;
    foundry_clear_error(e);
    if (metrics)
        memset(metrics, 0, sizeof *metrics);
    if (!s || !prompt || !foundry_utf8_valid(prompt))
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Session and valid UTF-8 prompt required");
    if (atomic_exchange(&s->active, true))
        return foundry_fail(e, FOUNDRY_BUSY, "Session is in use");
    foundry_metrics m = {0};
    m.load_ms = s->load_ms;
    strcpy(m.stop_reason, "error");
    double start = foundry_now_ms();
    int32_t *tokens = NULL;
    size_t count = 0;
    emission output = {.callback = cb, .data = data, .start = start};
    foundry_stream stream = {.stop = s->stop, .callback = emit_timed, .userdata = &output};
    foundry_status status = FOUNDRY_OK;
    if (cancelled(s)) {
        status = foundry_fail(e, FOUNDRY_CANCELLED, "Reset cancelled session before reuse");
        goto done;
    }
    status = fb_reset(s->backend, e);
    s->token_count = 0;
    s->ended = false;
    if (status)
        goto done;
    status = foundry_tokenize(s->model, prompt, true, parse_special, &tokens, &count, e);
    if (status)
        goto done;
    if (!count) {
        status = foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                              "Empty prompt produced no tokens; provide text");
        goto done;
    }
    if (count > s->config.context - s->config.max_tokens) {
        status =
            foundry_fail(e, FOUNDRY_CONTEXT_EXCEEDED,
                         "Prompt (%zu tokens) plus generation budget (%u) exceeds context (%u)",
                         count, s->config.max_tokens, s->config.context);
        goto done;
    }
    m.prompt_tokens = (uint32_t)count;
    double t = foundry_now_ms();
    status = fb_prefill(s->backend, tokens, count, e);
    m.prefill_ms = foundry_now_ms() - t;
    if (status)
        goto done;
    s->token_count = count;
    t = foundry_now_ms();
    strcpy(m.stop_reason, "token_limit");
    for (uint32_t i = 0; i < s->config.max_tokens; i++) {
        int32_t token;
        char *piece = NULL;
        size_t length = 0;
        status = fb_decode(s->backend, &token, &piece, &length, e);
        if (status == FOUNDRY_END_OF_GENERATION) {
            status = FOUNDRY_OK;
            strcpy(m.stop_reason, "eos");
            break;
        }
        if (status) {
            free(piece);
            break;
        }
        s->token_count++;
        m.generated_tokens++;
        status = foundry_stream_push(&stream, piece, length, false);
        free(piece);
        if (status)
            break;
        if (stream.stopped) {
            strcpy(m.stop_reason, "stop_sequence");
            break;
        }
    }
    m.decode_ms = foundry_now_ms() - t;
    if (status == FOUNDRY_OK)
        status = foundry_stream_push(&stream, NULL, 0, true);
done:
    if (status == FOUNDRY_CANCELLED) {
        strcpy(m.stop_reason, "cancelled");
        foundry_fail(e, status, "Generation cancelled");
    } else if (status != FOUNDRY_OK) {
        strcpy(m.stop_reason, "error");
        if (!e->code)
            foundry_fail(e, status, "Generation failed");
    }
    m.total_ms = foundry_now_ms() - start;
    m.first_token_ms = output.first_ms;
    if (metrics)
        *metrics = m;
    free(tokens);
    foundry_stream_destroy(&stream);
    s->ended = true;
    return finish(s, status, e);
}
