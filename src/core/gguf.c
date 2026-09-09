#define _POSIX_C_SOURCE 200809L
#include "import_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Deliberately bounded metadata reader. No tensor payload is allocated/read.
 * GGUF v2/v3 little endian. Unknown storage types remain inspectable but their
 * byte extents require the pinned compute backend to validate. */
#define MAX_META_BYTES (64u * 1024u * 1024u)
#define MAX_STRING (16u * 1024u * 1024u)
#define MAX_ITEMS 1000000u
typedef struct {
    FILE *f;
    uint64_t size, pos, nodes;
    foundry_error *error;
    foundry_status status;
} reader;
typedef struct {
    uint64_t offset, bytes;
    bool known;
} extent;
static bool fail(reader *r, foundry_status s, const char *msg) {
    if (!r->status)
        r->status = fi_error(r->error, s, "GGUF byte %llu: %s", (unsigned long long)r->pos, msg);
    return false;
}
static bool take(reader *r, void *p, size_t n) {
    if (r->status)
        return false;
    if (r->pos > r->size || n > r->size - r->pos || r->pos + n > MAX_META_BYTES)
        return fail(r, FOUNDRY_CORRUPT_MODEL, "truncated or oversized metadata");
    if (fread(p, 1, n, r->f) != n)
        return fail(r, FOUNDRY_IO_ERROR, "metadata read failed");
    r->pos += n;
    return true;
}
static bool u64(reader *r, unsigned bytes, uint64_t *v) {
    unsigned char p[8];
    *v = 0;
    if (!take(r, p, bytes))
        return false;
    for (unsigned i = 0; i < bytes; i++)
        *v |= (uint64_t)p[i] << (8 * i);
    return true;
}
static char *string(reader *r) {
    uint64_t n;
    if (!u64(r, 8, &n))
        return NULL;
    if (n > MAX_STRING || n > r->size - r->pos) {
        fail(r, FOUNDRY_CORRUPT_MODEL, "string length exceeds bounds");
        return NULL;
    }
    char *p = malloc((size_t)n + 1);
    if (!p) {
        fail(r, FOUNDRY_OUT_OF_MEMORY, "string allocation failed");
        return NULL;
    }
    if (!take(r, p, (size_t)n)) {
        free(p);
        return NULL;
    }
    if (memchr(p, 0, (size_t)n)) {
        free(p);
        fail(r, FOUNDRY_UNSUPPORTED, "embedded NUL metadata strings are not supported");
        return NULL;
    }
    p[n] = 0;
    return p;
}
static cJSON *number64(uint64_t v) {
    if (v <= UINT64_C(9007199254740991))
        return cJSON_CreateNumber((double)v);
    char b[32];
    snprintf(b, sizeof b, "%" PRIu64, v);
    return cJSON_CreateString(b);
}
static cJSON *value(reader *r, uint32_t type, unsigned depth) {
    if (++r->nodes > MAX_ITEMS || depth > 1) {
        fail(r, FOUNDRY_CORRUPT_MODEL, "metadata element/depth limit exceeded");
        return NULL;
    }
    uint64_t v = 0;
    cJSON *j = NULL;
    switch (type) {
    case 0:
    case 1:
    case 7:
        if (!u64(r, 1, &v))
            return NULL;
        break;
    case 2:
    case 3:
        if (!u64(r, 2, &v))
            return NULL;
        break;
    case 4:
    case 5:
    case 6:
        if (!u64(r, 4, &v))
            return NULL;
        break;
    case 10:
    case 11:
    case 12:
        if (!u64(r, 8, &v))
            return NULL;
        break;
    case 8: {
        char *p = string(r);
        if (!p)
            return NULL;
        j = cJSON_CreateString(p);
        free(p);
        break;
    }
    case 9: {
        uint64_t subtype, n;
        if (!u64(r, 4, &subtype) || !u64(r, 8, &n))
            return NULL;
        if (subtype == 9 || subtype > 12 || n > MAX_ITEMS - r->nodes) {
            fail(r, FOUNDRY_CORRUPT_MODEL, "invalid array type or length");
            return NULL;
        }
        j = cJSON_CreateArray();
        if (!j)
            break;
        for (uint64_t i = 0; i < n; i++) {
            cJSON *item = value(r, (uint32_t)subtype, depth + 1);
            if (!item || !cJSON_AddItemToArray(j, item)) {
                cJSON_Delete(item);
                cJSON_Delete(j);
                return NULL;
            }
        }
        break;
    }
    default:
        fail(r, FOUNDRY_UNSUPPORTED, "unknown metadata type");
        return NULL;
    }
    if (!j && type != 8 && type != 9) {
        if (type == 7) {
            if (v > 1) {
                fail(r, FOUNDRY_CORRUPT_MODEL, "invalid boolean");
                return NULL;
            }
            j = cJSON_CreateBool(v != 0);
        } else if (type == 6) {
            uint32_t b = (uint32_t)v;
            float f;
            memcpy(&f, &b, 4);
            j = isfinite(f)
                    ? cJSON_CreateNumber(f)
                    : cJSON_CreateString(isnan(f) ? "NaN" : (f < 0 ? "-Infinity" : "Infinity"));
        } else if (type == 12) {
            double d;
            memcpy(&d, &v, 8);
            j = isfinite(d)
                    ? cJSON_CreateNumber(d)
                    : cJSON_CreateString(isnan(d) ? "NaN" : (d < 0 ? "-Infinity" : "Infinity"));
        } else if (type == 1)
            j = cJSON_CreateNumber((int8_t)v);
        else if (type == 3)
            j = cJSON_CreateNumber((int16_t)v);
        else if (type == 5)
            j = cJSON_CreateNumber((int32_t)v);
        else if (type == 11) {
            int64_t n;
            memcpy(&n, &v, 8);
            if (n >= -INT64_C(9007199254740991) && n <= INT64_C(9007199254740991))
                j = cJSON_CreateNumber((double)n);
            else {
                char b[32];
                snprintf(b, sizeof b, "%" PRId64, n);
                j = cJSON_CreateString(b);
            }
        } else
            j = number64(v);
    }
    if (!j)
        fail(r, FOUNDRY_OUT_OF_MEMORY, "JSON allocation failed");
    return j;
}
static uint32_t getnum(cJSON *m, const char *key) {
    cJSON *n = cJSON_GetObjectItemCaseSensitive(m, key);
    return cJSON_IsNumber(n) && n->valuedouble >= 0 && n->valuedouble <= UINT32_MAX &&
                   floor(n->valuedouble) == n->valuedouble
               ? (uint32_t)n->valuedouble
               : 0;
}
static uint32_t arch_num(cJSON *m, const char *arch, const char *suffix) {
    char key[192];
    snprintf(key, sizeof key, "%s.%s", arch, suffix);
    return getnum(m, key);
}
static void getstr(cJSON *m, const char *key, char *dst, size_t n) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(m, key);
    if (cJSON_IsString(v))
        snprintf(dst, n, "%s", v->valuestring);
}
static bool storage(uint32_t type, uint32_t *block, uint32_t *bytes, const char **name) {
    static const struct {
        uint32_t type, block, bytes;
        const char *name;
    } formats[] = {{0, 1, 4, "F32"},       {1, 1, 2, "F16"},       {2, 32, 18, "Q4_0"},
                   {3, 32, 20, "Q4_1"},    {6, 32, 22, "Q5_0"},    {7, 32, 24, "Q5_1"},
                   {8, 32, 34, "Q8_0"},    {9, 32, 36, "Q8_1"},    {10, 256, 84, "Q2_K"},
                   {11, 256, 110, "Q3_K"}, {12, 256, 144, "Q4_K"}, {13, 256, 176, "Q5_K"},
                   {14, 256, 210, "Q6_K"}, {15, 256, 292, "Q8_K"}, {24, 1, 1, "I8"},
                   {25, 1, 2, "I16"},      {26, 1, 4, "I32"},      {27, 1, 8, "I64"},
                   {28, 1, 8, "F64"},      {30, 1, 2, "BF16"}};
    for (size_t i = 0; i < sizeof formats / sizeof formats[0]; i++)
        if (type == formats[i].type) {
            *block = formats[i].block;
            *bytes = formats[i].bytes;
            *name = formats[i].name;
            return true;
        }
    *name = "unknown";
    return false;
}
static int extent_order(const void *a, const void *b) {
    uint64_t x = ((const extent *)a)->offset, y = ((const extent *)b)->offset;
    return (x > y) - (x < y);
}
foundry_status fi_gguf(const char *path, foundry_model_info *info, cJSON **out, foundry_error *e) {
    *out = NULL;
    memset(info, 0, sizeof *info);
    FILE *f = fopen(path, "rb");
    if (!f)
        return fi_error(e, errno == ENOENT ? FOUNDRY_NOT_FOUND : FOUNDRY_IO_ERROR,
                        "Cannot inspect %s: %s", path, strerror(errno));
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        fclose(f);
        return fi_error(e, FOUNDRY_IO_ERROR, "Model is not a regular file");
    }
    reader r = {f, (uint64_t)st.st_size, 0, 0, e, FOUNDRY_OK};
    char magic[4];
    uint64_t version = 0, tensors = 0, kv = 0;
    cJSON *doc = cJSON_CreateObject(), *meta = cJSON_CreateObject(), *types = cJSON_CreateObject(),
          *ts = cJSON_CreateArray();
    extent *ex = NULL;
    if (!doc || !meta || !types || !ts) {
        cJSON_Delete(meta);
        cJSON_Delete(types);
        cJSON_Delete(ts);
        fail(&r, FOUNDRY_OUT_OF_MEMORY, "JSON allocation failed");
        goto done;
    }
    cJSON_AddItemToObject(doc, "metadata", meta);
    cJSON_AddItemToObject(doc, "metadata_types", types);
    cJSON_AddItemToObject(doc, "tensors", ts);
    if (!take(&r, magic, 4))
        goto done;
    if (memcmp(magic, "GGUF", 4)) {
        fail(&r, FOUNDRY_CORRUPT_MODEL, "expected GGUF signature");
        goto done;
    }
    if (!u64(&r, 4, &version) || !u64(&r, 8, &tensors) || !u64(&r, 8, &kv))
        goto done;
    if (version != 2 && version != 3) {
        fail(&r, FOUNDRY_UNSUPPORTED, "only little-endian GGUF v2/v3 are supported");
        goto done;
    }
    if (!tensors || tensors > 100000 || kv > 100000) {
        fail(&r, FOUNDRY_CORRUPT_MODEL, "invalid tensor or metadata count");
        goto done;
    }
    for (uint64_t i = 0; i < kv; i++) {
        char *key = string(&r);
        uint64_t type;
        if (!key)
            goto done;
        if (!*key || strlen(key) > 65535 || cJSON_HasObjectItem(meta, key)) {
            free(key);
            fail(&r, FOUNDRY_CORRUPT_MODEL, "empty, oversized, or duplicate metadata key");
            goto done;
        }
        if (!u64(&r, 4, &type)) {
            free(key);
            goto done;
        }
        cJSON *v = value(&r, (uint32_t)type, 0);
        if (!v) {
            free(key);
            goto done;
        }
        cJSON_AddItemToObject(meta, key, v);
        cJSON_AddNumberToObject(types, key, (double)type);
        free(key);
    }
    getstr(meta, "general.architecture", info->architecture, sizeof info->architecture);
    if (!info->architecture[0]) {
        fail(&r, FOUNDRY_CORRUPT_MODEL, "general.architecture is required");
        goto done;
    }
    uint32_t alignment = getnum(meta, "general.alignment");
    if (!cJSON_HasObjectItem(meta, "general.alignment"))
        alignment = 32;
    if (!alignment || (alignment & (alignment - 1)) || alignment > 1024 * 1024) {
        fail(&r, FOUNDRY_CORRUPT_MODEL, "alignment must be a bounded power of two");
        goto done;
    }
    if (getnum(meta, "split.count") > 1) {
        fail(&r, FOUNDRY_UNSUPPORTED, "sharded GGUF requires a future multi-file importer");
        goto done;
    }
    ex = calloc((size_t)tensors, sizeof *ex);
    if (!ex) {
        fail(&r, FOUNDRY_OUT_OF_MEMORY, "tensor table allocation failed");
        goto done;
    }
    bool all_known = true;
    uint64_t parameters = 0;
    for (uint64_t i = 0; i < tensors; i++) {
        char *name = string(&r);
        uint64_t dims;
        if (!name)
            goto done;
        if (!*name || strlen(name) > 1024 || !u64(&r, 4, &dims) || !dims || dims > 4) {
            free(name);
            fail(&r, FOUNDRY_CORRUPT_MODEL, "invalid tensor dimensions or name");
            goto done;
        }
        cJSON *t = cJSON_CreateObject(), *shape = cJSON_CreateArray();
        if (!t || !shape) {
            free(name);
            cJSON_Delete(t);
            cJSON_Delete(shape);
            fail(&r, FOUNDRY_OUT_OF_MEMORY, "tensor JSON allocation failed");
            goto done;
        }
        cJSON_AddItemToArray(ts, t);
        cJSON_AddStringToObject(t, "name", name);
        cJSON_AddItemToObject(t, "shape", shape);
        free(name);
        uint64_t count = 1, first = 0;
        for (uint64_t d = 0; d < dims; d++) {
            uint64_t n;
            if (!u64(&r, 8, &n))
                goto done;
            if (!n || n > UINT64_MAX / count) {
                fail(&r, FOUNDRY_CORRUPT_MODEL, "tensor shape overflows");
                goto done;
            }
            if (d == 0)
                first = n;
            count *= n;
            cJSON_AddItemToArray(shape, number64(n));
        }
        uint64_t type, offset;
        if (!u64(&r, 4, &type) || !u64(&r, 8, &offset))
            goto done;
        if (offset % alignment || offset >= r.size || count > UINT64_MAX - parameters) {
            fail(&r, FOUNDRY_CORRUPT_MODEL, "invalid tensor offset or parameter count");
            goto done;
        }
        parameters += count;
        uint32_t block = 0, bytes = 0;
        const char *type_name;
        ex[i].known = storage((uint32_t)type, &block, &bytes, &type_name);
        ex[i].offset = offset;
        if (ex[i].known) {
            if (first % block || count / block > UINT64_MAX / bytes) {
                fail(&r, FOUNDRY_CORRUPT_MODEL, "invalid quantization block dimensions");
                goto done;
            }
            ex[i].bytes = count / block * bytes;
            cJSON_AddItemToObject(t, "bytes", number64(ex[i].bytes));
        } else
            all_known = false;
        cJSON_AddNumberToObject(t, "type", (double)type);
        cJSON_AddStringToObject(t, "type_name", type_name);
        cJSON_AddItemToObject(t, "offset", number64(offset));
    }
    uint64_t data = (r.pos + alignment - 1) & ~((uint64_t)alignment - 1);
    if (data >= r.size) {
        fail(&r, FOUNDRY_CORRUPT_MODEL, "tensor data is absent");
        goto done;
    }
    qsort(ex, (size_t)tensors, sizeof *ex, extent_order);
    for (uint64_t i = 0; i < tensors; i++) {
        uint64_t remaining = r.size - data;
        if (ex[i].offset >= remaining || (ex[i].known && ex[i].bytes > remaining - ex[i].offset) ||
            (i + 1 < tensors && (ex[i].offset == ex[i + 1].offset ||
                                 (ex[i].known && ex[i].bytes > ex[i + 1].offset - ex[i].offset)))) {
            fail(&r, FOUNDRY_CORRUPT_MODEL, "tensor data is truncated or overlaps");
            goto done;
        }
    }
    info->file_bytes = r.size;
    info->container_version = (uint32_t)version;
    info->tensor_count = tensors;
    info->parameter_count = parameters;
    getstr(meta, "general.name", info->name, sizeof info->name);
    getstr(meta, "tokenizer.ggml.model", info->tokenizer, sizeof info->tokenizer);
    info->layers = arch_num(meta, info->architecture, "block_count");
    info->embedding = arch_num(meta, info->architecture, "embedding_length");
    info->heads = arch_num(meta, info->architecture, "attention.head_count");
    info->kv_heads = arch_num(meta, info->architecture, "attention.head_count_kv");
    info->key_length = arch_num(meta, info->architecture, "attention.key_length");
    info->value_length = arch_num(meta, info->architecture, "attention.value_length");
    info->trained_context = arch_num(meta, info->architecture, "context_length");
    cJSON *tokens = cJSON_GetObjectItemCaseSensitive(meta, "tokenizer.ggml.tokens");
    info->vocab_size = cJSON_IsArray(tokens) ? (uint32_t)cJSON_GetArraySize(tokens)
                                             : arch_num(meta, info->architecture, "vocab_size");
    cJSON_AddNumberToObject(doc, "schema_version", 1);
    cJSON_AddStringToObject(doc, "inspection_tool", "Cinder Foundry " FOUNDRY_VERSION);
    cJSON_AddStringToObject(doc, "path", path);
    cJSON_AddStringToObject(doc, "architecture", info->architecture);
    cJSON_AddStringToObject(doc, "name", info->name);
    cJSON_AddStringToObject(doc, "tokenizer", info->tokenizer);
    cJSON_AddItemToObject(doc, "file_bytes", number64(r.size));
    cJSON_AddNumberToObject(doc, "container_version", (double)version);
    cJSON_AddNumberToObject(doc, "tensor_count", (double)tensors);
    cJSON_AddItemToObject(doc, "parameter_count", number64(parameters));
    cJSON_AddNumberToObject(doc, "layers", info->layers);
    cJSON_AddNumberToObject(doc, "embedding", info->embedding);
    cJSON_AddNumberToObject(doc, "heads", info->heads);
    cJSON_AddNumberToObject(doc, "kv_heads", info->kv_heads);
    cJSON_AddNumberToObject(doc, "trained_context", info->trained_context);
    cJSON_AddNumberToObject(doc, "vocab_size", info->vocab_size);
    cJSON_AddNumberToObject(doc, "alignment", alignment);
    cJSON_AddItemToObject(doc, "tensor_data_offset", number64(data));
    cJSON_AddBoolToObject(doc, "all_tensor_extents_validated", all_known);
    cJSON_AddStringToObject(
        doc, "execution_compatibility",
        "requires backend load; structural inspection is not an execution guarantee");
done:
    free(ex);
    fclose(f);
    if (r.status)
        cJSON_Delete(doc);
    else
        *out = doc;
    return r.status;
}
