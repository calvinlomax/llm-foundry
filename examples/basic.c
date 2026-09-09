#include "foundry/foundry.h"
#include <stdio.h>
static bool emit(const char *text, size_t length, void *user) {
    (void)user;
    return fwrite(text, 1, length, stdout) == length;
}
int main(int argc, char **argv) {
    foundry_runtime *runtime = NULL;
    foundry_model *model = NULL;
    foundry_session *session = NULL;
    foundry_error error = {0};
    foundry_config config = foundry_config_default();
    config.max_tokens = 32;
    foundry_status status = foundry_runtime_create(&runtime, &error);
    if (!status && argc > 1)
        status = foundry_model_load(runtime, argv[1], &config, &model, &error);
    if (!status && model)
        status = foundry_session_create(model, &config, &session, &error);
    foundry_metrics metrics = {0};
    if (!status && session)
        status = foundry_generate(session, "The capital of France is", false, emit, NULL, &metrics,
                                  &error);
    if (status)
        fprintf(stderr, "%s: %s\n", foundry_status_string(status), error.message);
    foundry_session_destroy(session);
    foundry_model_unload(model, NULL);
    foundry_runtime_destroy(runtime, NULL);
    return status ? 1 : 0;
}
