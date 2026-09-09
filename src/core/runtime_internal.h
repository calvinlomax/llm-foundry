#ifndef FOUNDRY_RUNTIME_INTERNAL_H
#define FOUNDRY_RUNTIME_INTERNAL_H
#include "../backends/backend.h"
#include "foundry/foundry.h"
#include <stdatomic.h>
struct foundry_runtime {
    size_t models;
};
struct foundry_model {
    foundry_runtime *runtime;
    fb_model *backend;
    size_t sessions;
    double load_ms;
    foundry_config config;
    char *path;
};
struct foundry_session {
    foundry_model *model;
    fb_session *backend;
    foundry_config config;
    char *stop;
    atomic_bool cancelled;
    atomic_bool active;
    size_t token_count;
    bool ended;
    double load_ms;
    foundry_error error;
};
foundry_status foundry_fail(foundry_error *, foundry_status, const char *, ...);
void foundry_clear_error(foundry_error *);
double foundry_now_ms(void);
bool foundry_utf8_valid(const char *);
#endif
