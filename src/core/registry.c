#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include "import_internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define JSON_LIMIT (96u * 1024u * 1024u)
static bool valid_name(const char *s) {
    if (!s || !*s || strlen(s) > 96 || *s == '.')
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '-' || *s == '_' || *s == '.'))
            return false;
    return true;
}
static bool valid_digest(const char *s) {
    if (!s || strlen(s) != 71 || strncmp(s, "sha256:", 7))
        return false;
    for (s += 7; *s; s++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f')))
            return false;
    return true;
}
static char *registry_home(void) {
    const char *p = getenv("FOUNDRY_HOME");
    if (p && *p)
        return fi_strdup(p);
    p = getenv("HOME");
    return p && *p ? fi_join(p, ".local/share/cinder-foundry") : NULL;
}
static int mkdirs(const char *path) {
    char *p = fi_strdup(path);
    if (!p)
        return -1;
    for (char *c = p + 1; *c; c++)
        if (*c == '/') {
            *c = 0;
            if (mkdir(p, 0700) && errno != EEXIST) {
                free(p);
                return -1;
            }
            *c = '/';
        }
    int rc = mkdir(p, 0700);
    if (rc && errno == EEXIST)
        rc = 0;
    free(p);
    return rc;
}
static char *record_path(const char *id) {
    char *home = registry_home();
    if (!home)
        return NULL;
    char *dir = fi_join(home, "registry");
    free(home);
    if (!dir)
        return NULL;
    char filename[112];
    snprintf(filename, sizeof filename, "%s.json", id);
    char *p = fi_join(dir, filename);
    free(dir);
    return p;
}
static foundry_status atomic_json(const char *path, cJSON *doc, foundry_error *e) {
    char *text = cJSON_PrintUnformatted(doc), *tmp = malloc(strlen(path) + 16);
    if (!text || !tmp) {
        free(text);
        free(tmp);
        return fi_error(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate registry JSON");
    }
    sprintf(tmp, "%s.tmp-XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        free(text);
        free(tmp);
        return fi_error(e, FOUNDRY_IO_ERROR, "Cannot create registry transaction: %s",
                        strerror(errno));
    }
    size_t n = strlen(text), pos = 0;
    bool ok = true;
    while (pos < n) {
        ssize_t w = write(fd, text + pos, n - pos);
        if (w <= 0) {
            ok = false;
            break;
        }
        pos += (size_t)w;
    }
    if (ok && fsync(fd))
        ok = false;
    if (close(fd))
        ok = false;
    if (ok && rename(tmp, path))
        ok = false;
    if (!ok)
        unlink(tmp);
    free(text);
    free(tmp);
    return ok ? FOUNDRY_OK
              : fi_error(e, FOUNDRY_IO_ERROR, "Registry transaction failed: %s", strerror(errno));
}
foundry_status foundry_resolve(const char *id, char **out, foundry_error *e) {
    if (out)
        *out = NULL;
    if (!id || !*id || !out)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Model path or ID required");
    struct stat st;
    if (!stat(id, &st)) {
        if (!S_ISREG(st.st_mode))
            return fi_error(e, FOUNDRY_INVALID_CONFIG, "Model path is not a regular file");
        if (strstr(id, ".partial"))
            return fi_error(e, FOUNDRY_CORRUPT_MODEL, "Partial downloads cannot be loaded");
        *out = realpath(id, NULL);
        return *out ? FOUNDRY_OK : fi_error(e, FOUNDRY_IO_ERROR, "Cannot resolve model path");
    }
    if (!valid_name(id))
        return fi_error(e, FOUNDRY_NOT_FOUND, "Model path does not exist: %s", id);
    char *p = record_path(id);
    if (!p)
        return fi_error(e, FOUNDRY_INVALID_CONFIG,
                        "Set FOUNDRY_HOME or HOME for the model registry");
    cJSON *doc = NULL;
    foundry_status s = fi_read_json(p, JSON_LIMIT, &doc, e);
    free(p);
    if (s)
        return s;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(doc, "schema_version"),
          *path = cJSON_GetObjectItemCaseSensitive(doc, "path");
    if (!cJSON_IsNumber(v) || v->valueint != 1 || !cJSON_IsString(path) || !path->valuestring[0])
        s = fi_error(e, FOUNDRY_CORRUPT_MODEL, "Unsupported or malformed registry record");
    else if (stat(path->valuestring, &st) || !S_ISREG(st.st_mode))
        s = fi_error(e, FOUNDRY_NOT_FOUND, "Registered weight file is missing; reimport the model");
    else {
        *out = realpath(path->valuestring, NULL);
        if (!*out)
            s = fi_error(e, FOUNDRY_IO_ERROR, "Cannot resolve registered model");
    }
    cJSON_Delete(doc);
    return s;
}
foundry_status foundry_registry_list(char **out, foundry_error *e) {
    if (!out)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "JSON output required");
    *out = NULL;
    char *home = registry_home();
    if (!home)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Set FOUNDRY_HOME or HOME");
    char *path = fi_join(home, "registry");
    free(home);
    if (!path)
        return FOUNDRY_OUT_OF_MEMORY;
    cJSON *doc = cJSON_CreateObject(), *models = cJSON_AddArrayToObject(doc, "models");
    cJSON_AddNumberToObject(doc, "schema_version", 1);
    DIR *dir = opendir(path);
    foundry_status s = FOUNDRY_OK;
    if (!dir && errno != ENOENT)
        s = fi_error(e, FOUNDRY_IO_ERROR, "Cannot read registry: %s", strerror(errno));
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (n < 6 || strcmp(entry->d_name + n - 5, ".json"))
            continue;
        char *p = fi_join(path, entry->d_name);
        cJSON *rec = NULL;
        s = fi_read_json(p, JSON_LIMIT, &rec, e);
        free(p);
        if (s)
            break;
        cJSON *item = cJSON_CreateObject();
        const char *keys[] = {"name", "path", "sha256", "imported_at", "mode"};
        for (size_t i = 0; i < 5; i++) {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(rec, keys[i]);
            if (v)
                cJSON_AddItemToObject(item, keys[i], cJSON_Duplicate(v, true));
        }
        cJSON_AddItemToArray(models, item);
        cJSON_Delete(rec);
    }
    if (dir)
        closedir(dir);
    free(path);
    if (!s) {
        *out = cJSON_PrintUnformatted(doc);
        if (!*out)
            s = FOUNDRY_OUT_OF_MEMORY;
    }
    cJSON_Delete(doc);
    return s;
}
static foundry_status copy_weight(const char *source, const char *hash, char **out,
                                  foundry_error *e) {
    char *home = registry_home();
    if (!home)
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "Set FOUNDRY_HOME or HOME");
    char *dir = fi_join(home, "artifacts");
    free(home);
    if (!dir)
        return FOUNDRY_OUT_OF_MEMORY;
    if (mkdirs(dir)) {
        free(dir);
        return fi_error(e, FOUNDRY_IO_ERROR, "Cannot create artifact directory");
    }
    char filename[80];
    snprintf(filename, sizeof filename, "%s.gguf", hash);
    char *dest = fi_join(dir, filename);
    free(dir);
    if (!dest)
        return FOUNDRY_OUT_OF_MEMORY;
    if (access(dest, F_OK) == 0) {
        char actual[65];
        foundry_status s = foundry_sha256_file(dest, actual, e);
        if (!s && strcmp(actual, hash))
            s = fi_error(e, FOUNDRY_CORRUPT_MODEL, "Existing copied artifact hash does not match");
        if (s) {
            free(dest);
            return s;
        }
        *out = realpath(dest, NULL);
        free(dest);
        return *out ? FOUNDRY_OK : FOUNDRY_IO_ERROR;
    }
    char *tmp = malloc(strlen(dest) + 16);
    if (!tmp) {
        free(dest);
        return FOUNDRY_OUT_OF_MEMORY;
    }
    sprintf(tmp, "%s.tmp-XXXXXX", dest);
    int fd = mkstemp(tmp);
    FILE *in = fopen(source, "rb");
    bool ok = fd >= 0 && in;
    unsigned char *buffer = malloc(1024 * 1024);
    if (!buffer)
        ok = false;
    while (ok) {
        size_t n = fread(buffer, 1, 1024 * 1024, in);
        if (!n) {
            if (ferror(in))
                ok = false;
            break;
        }
        size_t p = 0;
        while (p < n) {
            ssize_t w = write(fd, buffer + p, n - p);
            if (w <= 0) {
                ok = false;
                break;
            }
            p += (size_t)w;
        }
    }
    free(buffer);
    if (in)
        fclose(in);
    if (fd >= 0) {
        if (ok && fsync(fd))
            ok = false;
        if (close(fd))
            ok = false;
    }
    char actual[65];
    if (ok && (foundry_sha256_file(tmp, actual, e) || strcmp(actual, hash)))
        ok = false;
    if (ok && rename(tmp, dest))
        ok = false;
    if (!ok)
        unlink(tmp);
    free(tmp);
    if (!ok) {
        free(dest);
        return fi_error(e, FOUNDRY_IO_ERROR, "Cannot copy and verify weight artifact");
    }
    *out = realpath(dest, NULL);
    free(dest);
    return *out ? FOUNDRY_OK : FOUNDRY_IO_ERROR;
}
static foundry_status register_model(const char *path, const char *name, bool copy,
                                     cJSON *provenance, char **out, foundry_error *e) {
    if (out)
        *out = NULL;
    if (!path || !valid_name(name) || !out)
        return fi_error(
            e, FOUNDRY_INVALID_CONFIG,
            "Import name must contain 1–96 letters, digits, dots, underscores, or hyphens");
    foundry_model_info info;
    cJSON *inspection = NULL;
    foundry_status s = fi_gguf(path, &info, &inspection, e);
    if (s)
        return s;
    char hash[65];
    s = foundry_sha256_file(path, hash, e);
    char *absolute = NULL;
    if (!s) {
        if (copy)
            s = copy_weight(path, hash, &absolute, e);
        else {
            absolute = realpath(path, NULL);
            if (!absolute)
                s = fi_error(e, FOUNDRY_NOT_FOUND, "Cannot resolve import path");
        }
    }
    if (s) {
        cJSON_Delete(inspection);
        return s;
    }
    char *home = registry_home(), *dir = home ? fi_join(home, "registry") : NULL;
    free(home);
    if (!dir || mkdirs(dir)) {
        free(dir);
        free(absolute);
        cJSON_Delete(inspection);
        return fi_error(e, FOUNDRY_IO_ERROR, "Cannot create model registry");
    }
    free(dir);
    char *record = record_path(name);
    char *lock_path = record ? malloc(strlen(record) + 6) : NULL;
    int lock_fd = -1;
    if (lock_path) {
        sprintf(lock_path, "%s.lock", record);
        lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    }
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX) != 0)
        s = fi_error(e, FOUNDRY_IO_ERROR, "Cannot lock model registry record");
    cJSON *existing = NULL;
    if (!s && record && access(record, F_OK) == 0) {
        s = fi_read_json(record, JSON_LIMIT, &existing, e);
        cJSON *h = cJSON_GetObjectItemCaseSensitive(existing, "sha256");
        if (!s && (!cJSON_IsString(h) || strcmp(h->valuestring, hash)))
            s = fi_error(e, FOUNDRY_INVALID_CONFIG,
                         "Name already identifies different bytes; choose another name");
        cJSON_Delete(existing);
    }
    cJSON *doc = cJSON_CreateObject();
    if (!doc || !record)
        s = fi_error(e, FOUNDRY_OUT_OF_MEMORY, "Cannot allocate import record");
    if (!s) {
        time_t now = time(NULL);
        struct tm utc;
        gmtime_r(&now, &utc);
        char stamp[32];
        strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc);
        cJSON_AddNumberToObject(doc, "schema_version", 1);
        cJSON_AddStringToObject(doc, "name", name);
        cJSON_AddStringToObject(doc, "path", absolute);
        cJSON_AddStringToObject(doc, "sha256", hash);
        cJSON_AddStringToObject(doc, "mode", copy ? "copy" : "reference");
        cJSON_AddStringToObject(doc, "imported_at", stamp);
        cJSON_AddItemToObject(doc, "inspection", inspection);
        inspection = NULL;
        if (provenance)
            cJSON_AddItemToObject(doc, "provenance", cJSON_Duplicate(provenance, true));
        s = atomic_json(record, doc, e);
        if (!s) {
            *out = cJSON_PrintUnformatted(doc);
            if (!*out)
                s = FOUNDRY_OUT_OF_MEMORY;
        }
    }
    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
    }
    free(lock_path);
    free(record);
    free(absolute);
    cJSON_Delete(inspection);
    cJSON_Delete(doc);
    return s;
}
foundry_status foundry_import_gguf(const char *path, const char *name, bool copy, char **out,
                                   foundry_error *e) {
    if (!path || strstr(path, ".partial"))
        return fi_error(e, FOUNDRY_INVALID_CONFIG, "A complete GGUF file is required");
    return register_model(path, name, copy, NULL, out, e);
}
foundry_status foundry_import_ollama(const char *manifest, const char *weights,
                                     const char *metadata, const char *name, bool copy, char **out,
                                     foundry_error *e) {
    if (out)
        *out = NULL;
    if (!manifest || !weights || !metadata || !out || !valid_name(name))
        return fi_error(e, FOUNDRY_INVALID_CONFIG,
                        "Manifest, artifact roots, valid name, and output required");
    cJSON *doc = NULL;
    foundry_status s = fi_read_json(manifest, 16 * 1024 * 1024, &doc, e);
    if (s)
        return s;
    cJSON *layers = cJSON_GetObjectItemCaseSensitive(doc, "layers");
    if (!layers || cJSON_IsNull(layers) || !cJSON_GetArraySize(layers)) {
        cJSON_Delete(doc);
        return fi_error(
            e, FOUNDRY_REMOTE_ONLY_MODEL,
            "Manifest has no local weight layers; remote-only models cannot run offline");
    }
    if (!cJSON_IsArray(layers) || cJSON_GetArraySize(layers) > 4096) {
        cJSON_Delete(doc);
        return fi_error(e, FOUNDRY_CORRUPT_MODEL, "Manifest layers must be a bounded array");
    }
    cJSON *prov = cJSON_CreateObject(), *retained = cJSON_AddArrayToObject(prov, "layers");
    cJSON_AddItemToObject(prov, "manifest", cJSON_Duplicate(doc, true));
    char manifest_hash[65];
    s = foundry_sha256_file(manifest, manifest_hash, e);
    if (!s)
        cJSON_AddStringToObject(prov, "manifest_sha256", manifest_hash);
    char *model = NULL;
    char model_hash[65] = {0};
    int n = cJSON_GetArraySize(layers);
    for (int i = -1; i < n && !s; i++) {
        cJSON *item =
            i < 0 ? cJSON_GetObjectItemCaseSensitive(doc, "config") : cJSON_GetArrayItem(layers, i);
        if (!item && i < 0)
            continue;
        cJSON *d = cJSON_GetObjectItemCaseSensitive(item, "digest"),
              *media = cJSON_GetObjectItemCaseSensitive(item, "mediaType"),
              *size = cJSON_GetObjectItemCaseSensitive(item, "size");
        if (!cJSON_IsString(d) || !valid_digest(d->valuestring) || !cJSON_IsString(media)) {
            s = fi_error(e, FOUNDRY_CORRUPT_MODEL, "Layer digest or media type is invalid");
            break;
        }
        const char *m = media->valuestring;
        size_t ml = strcspn(m, ";");
        while (ml && m[ml - 1] == ' ')
            ml--;
        const char *model_media = "application/vnd.ollama.image.model";
        bool weight = ml == strlen(model_media) && !strncmp(m, model_media, ml);
        char filename[80];
        snprintf(filename, sizeof filename, "sha256-%s", d->valuestring + 7);
        char *p = fi_join(weight ? weights : metadata, filename);
        if (!p) {
            s = FOUNDRY_OUT_OF_MEMORY;
            break;
        }
        struct stat st;
        if (stat(p, &st) || !S_ISREG(st.st_mode))
            s = fi_error(e, FOUNDRY_NOT_FOUND, "Missing layer %s", p);
        if (!s && (!cJSON_IsNumber(size) || size->valuedouble < 0 ||
                   size->valuedouble > 9007199254740991.0 ||
                   floor(size->valuedouble) != size->valuedouble ||
                   (uint64_t)size->valuedouble != (uint64_t)st.st_size))
            s = fi_error(e, FOUNDRY_CORRUPT_MODEL, "Layer size mismatch: %s", p);
        char hash[65];
        if (!s)
            s = foundry_sha256_file(p, hash, e);
        if (!s && strcmp(hash, d->valuestring + 7))
            s = fi_error(e, FOUNDRY_CORRUPT_MODEL, "SHA-256 mismatch: %s", p);
        if (!s && weight) {
            if (model && strcmp(hash, model_hash))
                s = fi_error(e, FOUNDRY_UNSUPPORTED,
                             "Multiple model layers/shards are not supported");
            else if (!model) {
                model = fi_strdup(p);
                strcpy(model_hash, hash);
            }
        }
        if (!s && !weight) {
            char *text = NULL;
            size_t length = 0;
            s = fi_read_file(p, 16 * 1024 * 1024, &text, &length, e);
            if (!s) {
                cJSON *rec = cJSON_Duplicate(item, true);
                if (memchr(text, 0, length)) {
                    char *hex = malloc(length * 2 + 1);
                    if (!hex)
                        s = FOUNDRY_OUT_OF_MEMORY;
                    else {
                        static const char h[] = "0123456789abcdef";
                        for (size_t j = 0; j < length; j++) {
                            hex[2 * j] = h[(unsigned char)text[j] >> 4];
                            hex[2 * j + 1] = h[(unsigned char)text[j] & 15];
                        }
                        hex[length * 2] = 0;
                        cJSON_AddStringToObject(rec, "bytes_hex", hex);
                        free(hex);
                    }
                } else
                    cJSON_AddStringToObject(rec, "text", text);
                cJSON_AddItemToArray(retained, rec);
            }
            free(text);
        }
        free(p);
    }
    if (!s && !model)
        s = fi_error(e, FOUNDRY_REMOTE_ONLY_MODEL, "No local model layer in this manifest");
    if (!s)
        s = register_model(model, name, copy, prov, out, e);
    free(model);
    cJSON_Delete(prov);
    cJSON_Delete(doc);
    return s;
}
