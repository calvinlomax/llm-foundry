#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "import_internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
static foundry_status estimate(const char *id, const foundry_config *c, uint64_t *weights,
                               uint64_t *kv, uint64_t *work, uint64_t *total, foundry_error *e) {
    foundry_status s = foundry_config_validate(c, e);
    if (s)
        return s;
    foundry_model_info info;
    s = foundry_inspect(id, &info, NULL, e);
    if (s)
        return s;
    if (strcmp(info.architecture, "llama") && strcmp(info.architecture, "qwen2") &&
        strcmp(info.architecture, "mistral") && strcmp(info.architecture, "gemma") &&
        strcmp(info.architecture, "gemma2"))
        return fi_error(e, FOUNDRY_UNSUPPORTED,
                        "No validated conventional KV estimator for architecture %s",
                        info.architecture);
    if (!info.layers || !info.embedding || !info.heads || !info.kv_heads ||
        info.embedding % info.heads)
        return fi_error(e, FOUNDRY_CORRUPT_MODEL,
                        "Memory planning requires layer, embedding, and attention dimensions");
    if (info.trained_context && c->context > info.trained_context)
        return fi_error(e, FOUNDRY_CONTEXT_EXCEEDED, "Requested context exceeds trained context %u",
                        info.trained_context);
    uint64_t key = info.key_length ? info.key_length : info.embedding / info.heads,
             value = info.value_length ? info.value_length : info.embedding / info.heads;
    uint64_t parts[] = {c->context, info.layers, info.kv_heads, (key + value) * 2};
    *kv = 1;
    for (size_t i = 0; i < 4; i++) {
        if (!parts[i] || *kv > UINT64_MAX / parts[i])
            return fi_error(e, FOUNDRY_INVALID_CONFIG, "KV estimate overflows");
        *kv *= parts[i];
    }
    *weights = info.file_bytes;
    *work = UINT64_C(256) * 1024 * 1024 + (uint64_t)c->batch_size * info.embedding * 16 +
            (uint64_t)info.vocab_size * 4;
    uint64_t reserve = UINT64_C(512) * 1024 * 1024;
    if (*weights > UINT64_MAX - *kv || *weights + *kv > UINT64_MAX - *work ||
        *weights + *kv + *work > UINT64_MAX - reserve)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Memory estimate overflows");
    *total = *weights + *kv + *work + reserve;
    return FOUNDRY_OK;
}
foundry_status foundry_plan_estimate(const char *id, const foundry_config *c, uint64_t *total,
                                     foundry_error *e) {
    uint64_t w, k, b;
    foundry_status s = estimate(id, c, &w, &k, &b, total, e);
    if (!s && c->memory_budget && *total > c->memory_budget)
        s = fi_error(e, FOUNDRY_OUT_OF_MEMORY,
                     "Estimated %llu bytes exceeds memory budget %llu; reduce context or select a "
                     "smaller model",
                     (unsigned long long)*total, (unsigned long long)c->memory_budget);
    return s;
}
foundry_status foundry_plan(const char *id, const foundry_config *c, char **out, foundry_error *e) {
    if (!out || !c)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Configuration and JSON output required");
    *out = NULL;
    uint64_t w, k, b, total;
    foundry_status s = estimate(id, c, &w, &k, &b, &total, e);
    if (s)
        return s;
    uint64_t ram = 0;
#ifdef __APPLE__
    size_t len = sizeof ram;
    if (sysctlbyname("hw.memsize", &ram, &len, NULL, 0))
        ram = 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page > 0)
        ram = (uint64_t)pages * (uint64_t)page;
#endif
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "schema_version", 1);
    cJSON_AddNumberToObject(j, "weights_bytes", (double)w);
    cJSON_AddNumberToObject(j, "kv_bytes", (double)k);
    cJSON_AddNumberToObject(j, "workspace_estimate_bytes", (double)b);
    cJSON_AddNumberToObject(j, "reserve_bytes", 512.0 * 1024 * 1024);
    cJSON_AddNumberToObject(j, "estimated_total_bytes", (double)total);
    cJSON_AddNumberToObject(j, "context", c->context);
    cJSON_AddNumberToObject(j, "sequences", 1);
    cJSON_AddStringToObject(j, "kv_precision", "F16");
    if (ram)
        cJSON_AddNumberToObject(j, "machine_memory_bytes", (double)ram);
    else
        cJSON_AddNullToObject(j, "machine_memory_bytes");
    cJSON_AddNullToObject(j, "available_memory_bytes");
    cJSON_AddNumberToObject(j, "memory_budget", (double)c->memory_budget);
    if (c->memory_budget)
        cJSON_AddBoolToObject(j, "fits_budget", total <= c->memory_budget);
    else
        cJSON_AddNullToObject(j, "fits_budget");
    cJSON_AddStringToObject(
        j, "uncertainty",
        "advisory conventional-transformer estimate; backend repacking, alignment, workspace and "
        "other applications may require more; availability is not measured");
    cJSON_AddStringToObject(j, "requested_placement",
                            c->gpu_layers == 0  ? "cpu"
                            : c->gpu_layers < 0 ? "auto"
                                                : "gpu");
    cJSON_AddStringToObject(j, "actual_placement", "not allocated; inspect backend load log");
    *out = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    return *out ? FOUNDRY_OK : FOUNDRY_OUT_OF_MEMORY;
}
