#define _POSIX_C_SOURCE 200809L
#include "import_internal.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

foundry_status fi_error(foundry_error *e, foundry_status s, const char *fmt, ...) {
    if (e) {
        va_list a;
        e->code = s;
        va_start(a, fmt);
        vsnprintf(e->message, sizeof e->message, fmt, a);
        va_end(a);
    }
    return s;
}
char *fi_strdup(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}
char *fi_join(const char *a, const char *b) {
    size_t na = strlen(a), nb = strlen(b);
    if (na > SIZE_MAX - nb - 2)
        return NULL;
    char *p = malloc(na + nb + 2);
    if (p)
        snprintf(p, na + nb + 2, "%s/%s", a, b);
    return p;
}
foundry_status fi_read_file(const char *path, size_t limit, char **out, size_t *length,
                            foundry_error *e) {
    *out = NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return fi_error(e, errno == ENOENT ? FOUNDRY_NOT_FOUND : FOUNDRY_IO_ERROR,
                        "Cannot open %s: %s", path, strerror(errno));
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > limit) {
        fclose(f);
        return fi_error(e, FOUNDRY_CORRUPT_MODEL,
                        "File is not regular or exceeds %zu-byte metadata limit: %s", limit, path);
    }
    size_t n = (size_t)st.st_size;
    char *p = malloc(n + 1);
    if (!p) {
        fclose(f);
        return fi_error(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate metadata buffer");
    }
    if (fread(p, 1, n, f) != n) {
        free(p);
        fclose(f);
        return fi_error(e, FOUNDRY_IO_ERROR, "Cannot read %s", path);
    }
    fclose(f);
    p[n] = 0;
    *out = p;
    if (length)
        *length = n;
    return FOUNDRY_OK;
}
foundry_status fi_read_json(const char *path, size_t limit, cJSON **out, foundry_error *e) {
    char *p = NULL;
    size_t n = 0;
    *out = NULL;
    foundry_status s = fi_read_file(path, limit, &p, &n, e);
    if (s)
        return s;
    *out = cJSON_ParseWithLengthOpts(p, n + 1, NULL, 1);
    free(p);
    if (!*out)
        return fi_error(e, FOUNDRY_CORRUPT_MODEL, "Invalid JSON in %s", path);
    return FOUNDRY_OK;
}
foundry_status foundry_inspect(const char *id, foundry_model_info *info, char **json,
                               foundry_error *e) {
    if (json)
        *json = NULL;
    if (!id || (!info && !json))
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Inspection requires a model and an output");
    char *path = NULL;
    foundry_status s = foundry_resolve(id, &path, e);
    if (s)
        return s;
    foundry_model_info scratch;
    cJSON *doc = NULL;
    s = fi_gguf(path, info ? info : &scratch, &doc, e);
    free(path);
    if (!s && json) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive(doc, "path");
        char hash[65];
        s = foundry_sha256_file(p->valuestring, hash, e);
        if (!s) {
            cJSON_AddStringToObject(doc, "sha256", hash);
            *json = cJSON_PrintUnformatted(doc);
            if (!*json)
                s = fi_error(e, FOUNDRY_OUT_OF_MEMORY, "Cannot serialize inspection");
        }
    }
    cJSON_Delete(doc);
    return s;
}
