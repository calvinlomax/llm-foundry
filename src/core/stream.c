#include "stream.h"
#include <stdlib.h>
#include <string.h>
static size_t utf8_width(const unsigned char *p, size_t n) {
    unsigned a = p[0], w = a < 128                  ? 1
                           : a >= 0xc2 && a <= 0xdf ? 2
                           : a >= 0xe0 && a <= 0xef ? 3
                           : a >= 0xf0 && a <= 0xf4 ? 4
                                                    : 0;
    if (!w)
        return SIZE_MAX;
    if (n < w)
        return 0;
    for (unsigned i = 1; i < w; i++)
        if ((p[i] & 0xc0) != 0x80)
            return SIZE_MAX;
    if ((a == 0xe0 && p[1] < 0xa0) || (a == 0xed && p[1] >= 0xa0) || (a == 0xf0 && p[1] < 0x90) ||
        (a == 0xf4 && p[1] >= 0x90))
        return SIZE_MAX;
    return w;
}
foundry_status foundry_stream_push(foundry_stream *s, const char *bytes, size_t n, bool final) {
    if (s->stopped)
        return FOUNDRY_OK;
    if (n > SIZE_MAX - s->size - 1)
        return FOUNDRY_OUT_OF_MEMORY;
    size_t needed = s->size + n + 1;
    if (needed > s->capacity) {
        size_t cap = needed < 4096 ? 4096 : needed;
        char *p = realloc(s->pending, cap);
        if (!p)
            return FOUNDRY_OUT_OF_MEMORY;
        s->pending = p;
        s->capacity = cap;
    }
    if (n)
        memcpy(s->pending + s->size, bytes, n);
    s->size += n;
    s->pending[s->size] = 0;
    size_t emit = s->size, stoplen = s->stop ? strlen(s->stop) : 0;
    if (stoplen) {
        for (size_t i = 0; i + stoplen <= s->size; i++)
            if (!memcmp(s->pending + i, s->stop, stoplen)) {
                emit = i;
                s->stopped = true;
                break;
            }
        if (!s->stopped && !final) {
            size_t hold = stoplen - 1 < s->size ? stoplen - 1 : s->size;
            while (hold && memcmp(s->pending + s->size - hold, s->stop, hold))
                hold--;
            emit -= hold;
        }
    }
    size_t consumed = 0, start = 0;
    while (consumed < emit) {
        size_t width = utf8_width((unsigned char *)s->pending + consumed, emit - consumed);
        if (width == 0 && !final && !s->stopped)
            break;
        if (width == SIZE_MAX || width == 0) {
            if (consumed > start && s->callback &&
                !s->callback(s->pending + start, consumed - start, s->userdata))
                return FOUNDRY_CANCELLED;
            if (s->callback && !s->callback("\xef\xbf\xbd", 3, s->userdata))
                return FOUNDRY_CANCELLED;
            consumed++;
            start = consumed;
        } else
            consumed += width;
    }
    if (consumed > start && s->callback &&
        !s->callback(s->pending + start, consumed - start, s->userdata))
        return FOUNDRY_CANCELLED;
    if (s->stopped)
        s->size = 0;
    else {
        memmove(s->pending, s->pending + consumed, s->size - consumed);
        s->size -= consumed;
    }
    return FOUNDRY_OK;
}
void foundry_stream_destroy(foundry_stream *s) {
    free(s->pending);
    s->pending = NULL;
    s->size = s->capacity = 0;
}
