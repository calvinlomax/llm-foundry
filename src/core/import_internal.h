#ifndef FOUNDRY_IMPORT_INTERNAL_H
#define FOUNDRY_IMPORT_INTERNAL_H
#include "cJSON.h"
#include "foundry/foundry.h"
#include <stdio.h>
foundry_status fi_error(foundry_error *, foundry_status, const char *, ...);
char *fi_strdup(const char *);
char *fi_join(const char *, const char *);
foundry_status fi_read_json(const char *, size_t, cJSON **, foundry_error *);
foundry_status fi_read_file(const char *, size_t, char **, size_t *, foundry_error *);
foundry_status fi_gguf(const char *, foundry_model_info *, cJSON **, foundry_error *);
foundry_status foundry_plan_estimate(const char *, const foundry_config *, uint64_t *,
                                     foundry_error *);
#endif
