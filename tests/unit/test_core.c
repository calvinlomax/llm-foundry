#include "foundry/foundry.h"
#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct {
    char text[1024];
    size_t length;
    bool cancel;
} sink;
static bool collect(const char *p, size_t n, void *u) {
    sink *s = u;
    if (s->cancel)
        return false;
    if (n > sizeof(s->text) - s->length - 1)
        return false;
    memcpy(s->text + s->length, p, n);
    s->length += n;
    s->text[s->length] = 0;
    return true;
}
int main(void) {
    foundry_error e = {0};
    foundry_config c = foundry_config_default();
    CHECK(foundry_config_validate(&c, &e) == FOUNDRY_OK);
    c.context = 0;
    CHECK(foundry_config_validate(&c, &e) == FOUNDRY_INVALID_CONFIG);
    c = foundry_config_default();
    c.max_tokens = c.context;
    CHECK(foundry_config_validate(&c, &e) == FOUNDRY_INVALID_CONFIG);
    c = foundry_config_default();
    c.top_p = 0;
    CHECK(foundry_config_validate(&c, &e) == FOUNDRY_INVALID_CONFIG);
    for (int i = 0; i < 3; i++) {
        foundry_runtime *r = NULL;
        CHECK(foundry_runtime_create(&r, &e) == FOUNDRY_OK);
        foundry_model *m = NULL;
        CHECK(foundry_model_load(r, "/nonexistent/foundry-model", NULL, &m, &e) ==
              FOUNDRY_NOT_FOUND);
        CHECK(m == NULL);
        CHECK(foundry_runtime_destroy(r, &e) == FOUNDRY_OK);
    }
    sink out = {0};
    foundry_stream stream = {.stop = "STOP", .callback = collect, .userdata = &out};
    CHECK(foundry_stream_push(&stream, "hello S", 7, false) == 0);
    CHECK(!strcmp(out.text, "hello "));
    CHECK(foundry_stream_push(&stream, "TO", 2, false) == 0);
    CHECK(!strcmp(out.text, "hello "));
    CHECK(foundry_stream_push(&stream, "P hidden", 8, false) == 0);
    CHECK(stream.stopped);
    CHECK(!strcmp(out.text, "hello "));
    foundry_stream_destroy(&stream);
    out = (sink){0};
    stream = (foundry_stream){.callback = collect, .userdata = &out};
    CHECK(foundry_stream_push(&stream, "\xf0\x9f", 2, false) == 0);
    CHECK(out.length == 0);
    CHECK(foundry_stream_push(&stream, "\x98\x80!", 3, false) == 0);
    CHECK(!strcmp(out.text, "\xf0\x9f\x98\x80!"));
    foundry_stream_destroy(&stream);
    out = (sink){0};
    stream = (foundry_stream){.stop = "STOP", .callback = collect, .userdata = &out};
    CHECK(foundry_stream_push(&stream, "ST", 2, false) == 0);
    CHECK(out.length == 0);
    CHECK(foundry_stream_push(&stream, NULL, 0, true) == 0);
    CHECK(!strcmp(out.text, "ST"));
    foundry_stream_destroy(&stream);
    out = (sink){.cancel = true};
    stream = (foundry_stream){.callback = collect, .userdata = &out};
    CHECK(foundry_stream_push(&stream, "x", 1, false) == FOUNDRY_CANCELLED);
    foundry_stream_destroy(&stream);
    puts("Core lifecycle, configuration, stop and UTF-8 checks passed");
    return 0;
}
