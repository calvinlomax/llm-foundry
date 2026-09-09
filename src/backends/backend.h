#ifndef FOUNDRY_BACKEND_H
#define FOUNDRY_BACKEND_H
#include "foundry/foundry.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct fb_model fb_model;
typedef struct fb_session fb_session;
typedef bool (*fb_cancel_callback)(void *);
foundry_status fb_init(foundry_error *);
void fb_shutdown(void);
const char *fb_capabilities(void);
foundry_status fb_load(const char *, const foundry_config *, fb_model **, foundry_error *);
void fb_unload(fb_model *);
foundry_status fb_session_create(fb_model *, const foundry_config *, fb_cancel_callback, void *,
                                 fb_session **, foundry_error *);
void fb_session_destroy(fb_session *);
foundry_status fb_reset(fb_session *, foundry_error *);
foundry_status fb_tokenize(fb_model *, const char *, bool, bool, int32_t **, size_t *,
                           foundry_error *);
foundry_status fb_prefill(fb_session *, const int32_t *, size_t, foundry_error *);
foundry_status fb_decode(fb_session *, int32_t *, char **, size_t *, foundry_error *);
foundry_status fb_chat_render(fb_model *, const foundry_message *, size_t, char **,
                              foundry_error *);
#ifdef __cplusplus
}
#endif
#endif
