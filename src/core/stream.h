#ifndef FOUNDRY_STREAM_H
#define FOUNDRY_STREAM_H
#include "foundry/foundry.h"
typedef struct {
    char *pending;
    size_t size, capacity;
    const char *stop;
    bool stopped;
    foundry_token_callback callback;
    void *userdata;
} foundry_stream;
foundry_status foundry_stream_push(foundry_stream *, const char *, size_t, bool final);
void foundry_stream_destroy(foundry_stream *);
#endif
