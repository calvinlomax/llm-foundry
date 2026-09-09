#ifndef FOUNDRY_FOUNDRY_H
#define FOUNDRY_FOUNDRY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FOUNDRY_VERSION "0.1.0-dev"
#define FOUNDRY_ABI_VERSION 1
typedef enum {
    FOUNDRY_OK = 0,
    FOUNDRY_INVALID_CONFIG,
    FOUNDRY_NOT_FOUND,
    FOUNDRY_CORRUPT_MODEL,
    FOUNDRY_UNSUPPORTED,
    FOUNDRY_OUT_OF_MEMORY,
    FOUNDRY_BACKEND_ERROR,
    FOUNDRY_CANCELLED,
    FOUNDRY_BUSY,
    FOUNDRY_REMOTE_ONLY_MODEL,
    FOUNDRY_IO_ERROR,
    FOUNDRY_CONTEXT_EXCEEDED,
    FOUNDRY_END_OF_GENERATION
} foundry_status;
typedef struct {
    foundry_status code;
    char message[512];
} foundry_error;
typedef struct foundry_runtime foundry_runtime;
typedef struct foundry_model foundry_model;
typedef struct foundry_session foundry_session;
typedef struct {
    uint32_t context, max_tokens, batch_size, threads, seed;
    float temperature, top_p;
    int32_t gpu_layers;     /* 0: CPU; positive: requested GPU layers; -1: auto */
    uint64_t memory_budget; /* 0: advisory only; otherwise hard planned byte limit */
    const char *stop;       /* optional UTF-8 stop string, copied on session creation */
} foundry_config;
typedef struct {
    uint64_t file_bytes, tensor_count, parameter_count;
    uint32_t container_version, layers, embedding, heads, kv_heads;
    uint32_t key_length, value_length, trained_context, vocab_size;
    char architecture[64], name[256], tokenizer[64];
} foundry_model_info;
typedef struct {
    uint32_t prompt_tokens, generated_tokens, cached_prompt_tokens;
    double load_ms, prefill_ms, decode_ms, first_token_ms, total_ms;
    char stop_reason[32];
} foundry_metrics;
typedef struct {
    const char *role;
    const char *content;
} foundry_message;
/* Bytes are borrowed for the callback only, valid UTF-8, and not NUL terminated.
 * Return false to cancel. Only request_cancel may run concurrently on a session. */
typedef bool (*foundry_token_callback)(const char *bytes, size_t length, void *userdata);
const char *foundry_status_string(foundry_status status);
void foundry_free(void *allocation);
foundry_config foundry_config_default(void);
foundry_status foundry_config_validate(const foundry_config *, foundry_error *);
foundry_status foundry_runtime_create(foundry_runtime **out, foundry_error *);
foundry_status foundry_runtime_destroy(foundry_runtime *, foundry_error *);
const char *foundry_capabilities_json(void); /* static borrowed JSON */
foundry_status foundry_model_load(foundry_runtime *, const char *path_or_id, const foundry_config *,
                                  foundry_model **out, foundry_error *);
foundry_status foundry_model_unload(foundry_model *, foundry_error *);
foundry_status foundry_session_create(foundry_model *, const foundry_config *,
                                      foundry_session **out, foundry_error *);
foundry_status foundry_session_reset(foundry_session *, foundry_error *);
void foundry_session_destroy(foundry_session *);
void foundry_request_cancel(foundry_session *);
foundry_status foundry_tokenize(foundry_model *, const char *text, bool add_special,
                                bool parse_special, int32_t **tokens, size_t *count,
                                foundry_error *);
foundry_status foundry_prefill(foundry_session *, const int32_t *tokens, size_t count,
                               foundry_error *);
/* token text returned by decode is allocated; free with foundry_free. Byte fragments
 * can split UTF-8. generate assembles valid UTF-8 and filters stop strings. */
foundry_status foundry_decode(foundry_session *, int32_t *token, char **bytes, size_t *length,
                              foundry_error *);
/* generate starts a fresh KV history; chat renders full structured history first. */
foundry_status foundry_generate(foundry_session *, const char *prompt, bool parse_special,
                                foundry_token_callback, void *, foundry_metrics *, foundry_error *);
foundry_status foundry_chat_render(foundry_model *, const foundry_message *, size_t count,
                                   char **text, foundry_error *);
const foundry_error *foundry_last_error(const foundry_session *);
/* Native, weight-free metadata inspection; backend load is the execution check. */
foundry_status foundry_inspect(const char *path_or_id, foundry_model_info *, char **json,
                               foundry_error *);
foundry_status foundry_plan(const char *path_or_id, const foundry_config *, char **json,
                            foundry_error *);
/* Registry defaults to $FOUNDRY_HOME or $HOME/.local/share/cinder-foundry.
 * Returned strings are owned. Imports verify SHA-256 and commit atomically. */
foundry_status foundry_resolve(const char *path_or_id, char **path, foundry_error *);
foundry_status foundry_registry_list(char **json, foundry_error *);
foundry_status foundry_import_gguf(const char *path, const char *name, bool copy, char **json,
                                   foundry_error *);
foundry_status foundry_import_ollama(const char *manifest, const char *weights_root,
                                     const char *metadata_root, const char *name, bool copy,
                                     char **json, foundry_error *);
foundry_status foundry_sha256_file(const char *path, char hex[65], foundry_error *);
#ifdef __cplusplus
}
#endif
#endif
