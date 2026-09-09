#include "runtime_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

foundry_config foundry_config_default(void) {
    return (foundry_config){.context = 2048,
                            .max_tokens = 128,
                            .batch_size = 128,
                            .threads = 4,
                            .seed = 0,
                            .temperature = 0,
                            .top_p = 0.95f,
                            .gpu_layers = 0,
                            .memory_budget = 0,
                            .stop = NULL};
}

bool foundry_utf8_valid(const char *text) {
    if (!text)
        return false;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned c = *p++, n = 0, lo = 0x80, hi = 0xbf;
        if (c < 0x80)
            continue;
        if (c >= 0xc2 && c <= 0xdf)
            n = 1;
        else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            if (c == 0xe0)
                lo = 0xa0;
            if (c == 0xed)
                hi = 0x9f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            if (c == 0xf0)
                lo = 0x90;
            if (c == 0xf4)
                hi = 0x8f;
        } else
            return false;
        if (*p < lo || *p > hi)
            return false;
        ++p;
        while (--n) {
            if (*p < 0x80 || *p > 0xbf)
                return false;
            ++p;
        }
    }
    return true;
}

foundry_status foundry_config_validate(const foundry_config *c, foundry_error *e) {
    foundry_clear_error(e);
    if (!c)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "Configuration is required");
    if (c->context < 8 || c->context > 1048576)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "context must be between 8 and 1048576");
    if (c->max_tokens == 0 || c->max_tokens >= c->context)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "max_tokens must be positive and smaller than context");
    if (c->batch_size == 0 || c->batch_size > c->context)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "batch_size must be positive and no larger than context");
    if (c->threads == 0 || c->threads > 1024)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "threads must be between 1 and 1024");
    if (!isfinite(c->temperature) || c->temperature < 0 || c->temperature > 100)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "temperature must be finite and between 0 and 100 (0 selects greedy)");
    if (!isfinite(c->top_p) || c->top_p <= 0 || c->top_p > 1)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG, "top_p must be finite and in (0, 1]");
    if (c->gpu_layers < -1)
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "gpu_layers must be -1 (auto), 0 (CPU), or positive");
    if (c->stop && (!*c->stop || strlen(c->stop) > 65536 || !foundry_utf8_valid(c->stop)))
        return foundry_fail(e, FOUNDRY_INVALID_CONFIG,
                            "stop must be nonempty valid UTF-8 of at most 65536 bytes");
    return FOUNDRY_OK;
}
