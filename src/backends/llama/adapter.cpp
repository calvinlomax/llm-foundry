#include "../backend.h"
#include "ggml-backend.h"
#include "llama.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#ifndef FOUNDRY_BACKEND_REVISION
#define FOUNDRY_BACKEND_REVISION "f3f1a8f2760f28325a5ec20c05b171e5b7c83a29"
#endif

struct fb_model {
    llama_model *model = nullptr;
    int gpu_layers = 0;
    ~fb_model() {
        if (model)
            llama_model_free(model);
    }
};
struct fb_session {
    fb_model *model = nullptr;
    llama_context *context = nullptr;
    llama_sampler *sampler = nullptr;
    foundry_config config{};
    size_t past = 0;
    fb_cancel_callback cancelled = nullptr;
    void *cancel_data = nullptr;
    ~fb_session() {
        if (sampler)
            llama_sampler_free(sampler);
        if (context)
            llama_free(context);
    }
};
static std::mutex backend_mutex;
static size_t backend_users = 0;
static foundry_status fail(foundry_error *e, foundry_status s, const char *message) {
    if (e) {
        e->code = s;
        std::snprintf(e->message, sizeof(e->message), "%s", message);
    }
    return s;
}
// Every adapter entry contains exceptions: no C++ exception may enter a C caller.
#define FB_CATCH(e)                                                                                \
    catch (const std::bad_alloc &) {                                                               \
        return fail(e, FOUNDRY_OUT_OF_MEMORY,                                                      \
                    "Backend allocation failed; reduce context or use a smaller model");           \
    }                                                                                              \
    catch (const std::exception &x) {                                                              \
        return fail(e, FOUNDRY_BACKEND_ERROR, x.what());                                           \
    }                                                                                              \
    catch (...) {                                                                                  \
        return fail(e, FOUNDRY_BACKEND_ERROR, "Unknown backend exception");                        \
    }

extern "C" foundry_status fb_init(foundry_error *e) try {
    std::lock_guard<std::mutex> lock(backend_mutex);
    if (backend_users == 0)
        llama_backend_init();
    ++backend_users;
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" void fb_shutdown(void) {
    try {
        std::lock_guard<std::mutex> lock(backend_mutex);
        if (backend_users && --backend_users == 0)
            llama_backend_free();
    } catch (...) {
    }
}
static bool has_gpu() {
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto type = ggml_backend_dev_type(ggml_backend_dev_get(i));
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU)
            return true;
    }
    return false;
}
extern "C" const char *fb_capabilities(void) {
    // Compiled and discoverable placement capability; model compatibility remains a load-time
    // check.
    static const char cpu[] = "{\"backend\":\"llama.cpp\",\"revision\":\"" FOUNDRY_BACKEND_REVISION
                              "\",\"cpu\":true,\"gpu_available\":false,\"formats\":[\"gguf\"],"
                              "\"scope\":\"decoder-only text\",\"chat\":\"verified SmolLM2 ChatML "
                              "template only\",\"compatibility\":\"backend-reported; see "
                              "docs/compatibility.md for tested artifacts\"}";
    static const char gpu[] = "{\"backend\":\"llama.cpp\",\"revision\":\"" FOUNDRY_BACKEND_REVISION
                              "\",\"cpu\":true,\"gpu_available\":true,\"formats\":[\"gguf\"],"
                              "\"scope\":\"decoder-only text\",\"chat\":\"verified SmolLM2 ChatML "
                              "template only\",\"compatibility\":\"backend-reported; see "
                              "docs/compatibility.md for tested artifacts\"}";
    try {
        std::lock_guard<std::mutex> lock(backend_mutex);
        if (!backend_users) {
            llama_backend_init();
            bool gpu_ok = has_gpu();
            llama_backend_free();
            return gpu_ok ? gpu : cpu;
        }
        return has_gpu() ? gpu : cpu;
    } catch (...) {
        return cpu;
    }
}
extern "C" foundry_status fb_load(const char *path, const foundry_config *c, fb_model **out,
                                  foundry_error *e) try {
    *out = nullptr;
    bool gpu = has_gpu();
    if (c->gpu_layers > 0 && !gpu)
        return fail(e, FOUNDRY_UNSUPPORTED,
                    "GPU layers were requested but no GPU device is available in this build; use "
                    "gpu_layers=0 for CPU");
    auto m = std::make_unique<fb_model>();
    auto params = llama_model_default_params();
    m->gpu_layers = c->gpu_layers == -1 ? (gpu ? -1 : 0) : c->gpu_layers;
    params.n_gpu_layers = m->gpu_layers;
    // Empty device list prevents incidental GPU tensor placement for an explicit CPU request.
    ggml_backend_dev_t cpu_devices[] = {nullptr};
    if (m->gpu_layers == 0)
        params.devices = cpu_devices;
    params.check_tensors = true;
    m->model = llama_model_load_from_file(path, params);
    if (!m->model)
        return fail(e, FOUNDRY_BACKEND_ERROR,
                    "llama.cpp could not load the model; inspect backend stderr for unsupported "
                    "architecture, tensor or allocation details");
    if (llama_model_has_encoder(m->model) || !llama_model_has_decoder(m->model))
        return fail(e, FOUNDRY_UNSUPPORTED,
                    "This release supports decoder-only text models; encoder and embedding-only "
                    "models require a different execution path");
    *out = m.release();
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" void fb_unload(fb_model *m) {
    try {
        delete m;
    } catch (...) {
    }
}
extern "C" foundry_status fb_session_create(fb_model *m, const foundry_config *c,
                                            fb_cancel_callback cancel, void *data, fb_session **out,
                                            foundry_error *e) try {
    *out = nullptr;
    auto s = std::make_unique<fb_session>();
    s->model = m;
    s->config = *c;
    s->cancelled = cancel;
    s->cancel_data = data;
    auto p = llama_context_default_params();
    p.n_ctx = c->context;
    p.n_batch = c->batch_size;
    p.n_ubatch = c->batch_size;
    p.n_seq_max = 1;
    p.n_threads = (int32_t)c->threads;
    p.n_threads_batch = (int32_t)c->threads;
    p.abort_callback = cancel;
    p.abort_callback_data = data;
    p.no_perf = false;
    p.offload_kqv = m->gpu_layers != 0;
    p.op_offload = m->gpu_layers != 0;
    p.type_k = GGML_TYPE_F16;
    p.type_v = GGML_TYPE_F16;
    s->context = llama_init_from_model(m->model, p);
    if (!s->context)
        return fail(e, FOUNDRY_BACKEND_ERROR,
                    "Could not allocate the requested context; reduce context/batch size or "
                    "inspect backend stderr");
    s->sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!s->sampler)
        return fail(e, FOUNDRY_OUT_OF_MEMORY, "Could not create sampler");
    if (c->temperature == 0)
        llama_sampler_chain_add(s->sampler, llama_sampler_init_greedy());
    else {
        llama_sampler_chain_add(s->sampler, llama_sampler_init_top_p(c->top_p, 1));
        llama_sampler_chain_add(s->sampler, llama_sampler_init_temp(c->temperature));
        llama_sampler_chain_add(s->sampler, llama_sampler_init_dist(c->seed));
    }
    *out = s.release();
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" void fb_session_destroy(fb_session *s) {
    try {
        delete s;
    } catch (...) {
    }
}
extern "C" foundry_status fb_reset(fb_session *s, foundry_error *e) try {
    llama_synchronize(s->context);
    llama_memory_clear(llama_get_memory(s->context), true);
    llama_sampler_reset(s->sampler);
    s->past = 0;
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" foundry_status fb_tokenize(fb_model *m, const char *text, bool add_special,
                                      bool parse_special, int32_t **out, size_t *count,
                                      foundry_error *e) try {
    *out = nullptr;
    *count = 0;
    size_t len = std::strlen(text);
    if (len > INT32_MAX)
        return fail(e, FOUNDRY_INVALID_CONFIG, "Prompt exceeds tokenizer byte limit");
    const llama_vocab *vocab = llama_model_get_vocab(m->model);
    int n = llama_tokenize(vocab, text, (int32_t)len, nullptr, 0, add_special, parse_special);
    if (n == INT32_MIN)
        return fail(e, FOUNDRY_INVALID_CONFIG, "Token count exceeds backend limit");
    if (n < 0)
        n = -n;
    if (!n)
        return FOUNDRY_OK;
    std::unique_ptr<int32_t, decltype(&std::free)> tokens(
        (int32_t *)std::malloc((size_t)n * sizeof(int32_t)), std::free);
    if (!tokens)
        throw std::bad_alloc();
    int actual =
        llama_tokenize(vocab, text, (int32_t)len, tokens.get(), n, add_special, parse_special);
    if (actual < 0)
        return fail(e, FOUNDRY_BACKEND_ERROR, "Tokenizer returned inconsistent required capacity");
    *count = (size_t)actual;
    *out = tokens.release();
    return FOUNDRY_OK;
}
FB_CATCH(e)
static foundry_status decode_batch(fb_session *s, const int32_t *tokens, size_t count,
                                   foundry_error *e) {
    if (s->cancelled && s->cancelled(s->cancel_data))
        return fail(e, FOUNDRY_CANCELLED, "Generation cancelled");
    auto batch = llama_batch_get_one(const_cast<int32_t *>(tokens), (int32_t)count);
    int result = llama_decode(s->context, batch);
    llama_synchronize(s->context);
    if (result != 0) {
        // A failed or aborted decode may have partially changed KV state. Callers must reset.
        if (result == 2 || (s->cancelled && s->cancelled(s->cancel_data)))
            return fail(e, FOUNDRY_CANCELLED, "Generation cancelled during backend execution");
        if (result == 1)
            return fail(e, FOUNDRY_CONTEXT_EXCEEDED, "Backend could not find a KV cache slot");
        return fail(e, FOUNDRY_BACKEND_ERROR,
                    "Backend decode failed; reset the session before reuse");
    }
    s->past += count;
    return FOUNDRY_OK;
}
extern "C" foundry_status fb_prefill(fb_session *s, const int32_t *tokens, size_t count,
                                     foundry_error *e) try {
    int vocab_size = llama_vocab_n_tokens(llama_model_get_vocab(s->model->model));
    for (size_t i = 0; i < count; i++)
        if (tokens[i] < 0 || tokens[i] >= vocab_size)
            return fail(e, FOUNDRY_INVALID_CONFIG, "Token ID is outside the model vocabulary");
    for (size_t pos = 0; pos < count;) {
        size_t n = std::min(count - pos, (size_t)s->config.batch_size);
        foundry_status result = decode_batch(s, tokens + pos, n, e);
        if (result)
            return result;
        pos += n;
    }
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" foundry_status fb_decode(fb_session *s, int32_t *token, char **out, size_t *length,
                                    foundry_error *e) try {
    *out = nullptr;
    *length = 0;
    if (s->cancelled && s->cancelled(s->cancel_data))
        return fail(e, FOUNDRY_CANCELLED, "Generation cancelled");
    const llama_vocab *vocab = llama_model_get_vocab(s->model->model);
    *token = llama_sampler_sample(s->sampler, s->context, -1);
    if (llama_vocab_is_eog(vocab, *token))
        return FOUNDRY_END_OF_GENERATION;
    int n = llama_token_to_piece(vocab, *token, nullptr, 0, 0, false);
    if (n == INT32_MIN)
        return fail(e, FOUNDRY_BACKEND_ERROR, "Token text exceeds backend limit");
    if (n < 0)
        n = -n;
    std::unique_ptr<char, decltype(&std::free)> bytes((char *)std::malloc((size_t)n + 1),
                                                      std::free);
    if (!bytes)
        throw std::bad_alloc();
    int actual = llama_token_to_piece(vocab, *token, bytes.get(), n, 0, false);
    if (actual < 0)
        return fail(e, FOUNDRY_BACKEND_ERROR, "Token text returned inconsistent required capacity");
    bytes.get()[actual] = '\0';
    foundry_status result = decode_batch(s, token, 1, e);
    if (result)
        return result;
    *out = bytes.release();
    *length = (size_t)actual;
    return FOUNDRY_OK;
}
FB_CATCH(e)
extern "C" foundry_status fb_chat_render(fb_model *m, const foundry_message *messages, size_t count,
                                         char **out, foundry_error *e) try {
    *out = nullptr;
    const char *tmpl = llama_model_chat_template(m->model, nullptr);
    if (!tmpl || !*tmpl)
        return fail(e, FOUNDRY_UNSUPPORTED,
                    "Model has no embedded chat template; use raw completion or a model with a "
                    "supported template");
    const char *verified_smollm2 =
        "{% for message in messages %}{% if loop.first and messages[0]['role'] != 'system' %}{{ "
        "'<|im_start|>system\nYou are a helpful AI assistant named SmolLM, trained by Hugging "
        "Face<|im_end|>\n' }}{% endif %}{{'<|im_start|>' + message['role'] + '\n' + "
        "message['content'] + '<|im_end|>' + '\n'}}{% endfor %}{% if add_generation_prompt %}{{ "
        "'<|im_start|>assistant\n' }}{% endif %}";
    if (std::strcmp(tmpl, verified_smollm2) != 0)
        return fail(
            e, FOUNDRY_UNSUPPORTED,
            "This embedded chat template has not been verified by Foundry; use raw completion");
    std::vector<llama_chat_message> history;
    history.reserve(count + 1);
    if (std::strcmp(messages[0].role, "system") != 0)
        history.push_back(
            {"system", "You are a helpful AI assistant named SmolLM, trained by Hugging Face"});
    tmpl = "chatml"; // Exact translation of the verified embedded SmolLM2 template.
    size_t bytes = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = std::strlen(messages[i].content);
        if (len > INT32_MAX || bytes > (size_t)INT32_MAX - len)
            return fail(e, FOUNDRY_INVALID_CONFIG, "Chat history is too large");
        bytes += len;
        history.push_back({messages[i].role, messages[i].content});
    }
    int n = llama_chat_apply_template(tmpl, history.data(), history.size(), true, nullptr, 0);
    if (n < 0)
        return fail(e, FOUNDRY_UNSUPPORTED,
                    "Embedded chat template is unsupported by the pinned backend's template "
                    "renderer; use raw completion");
    std::unique_ptr<char, decltype(&std::free)> buffer((char *)std::malloc((size_t)n + 1),
                                                       std::free);
    if (!buffer)
        throw std::bad_alloc();
    int actual =
        llama_chat_apply_template(tmpl, history.data(), history.size(), true, buffer.get(), n);
    if (actual < 0 || actual > n)
        return fail(e, FOUNDRY_BACKEND_ERROR,
                    "Chat template returned inconsistent required capacity");
    buffer.get()[actual] = '\0';
    *out = buffer.release();
    return FOUNDRY_OK;
}
FB_CATCH(e)
