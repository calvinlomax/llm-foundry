#include "foundry/foundry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #x, e.message);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static bool cancel_first(const char *p, size_t n, void *u) {
    (void)p;
    (void)n;
    (*(int *)u)++;
    return false;
}
static bool collect(const char *p, size_t n, void *u) {
    (void)p;
    *(size_t *)u += n;
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    foundry_error e = {0};
    foundry_runtime *r = NULL;
    foundry_model *m = NULL;
    foundry_session *s = NULL;
    foundry_config c = foundry_config_default();
    c.context = 256;
    c.max_tokens = 8;
    c.batch_size = 128;
    CHECK(foundry_runtime_create(&r, &e) == 0);
    CHECK(foundry_model_load(r, argv[1], &c, &m, &e) == 0);
    CHECK(foundry_runtime_destroy(r, &e) == FOUNDRY_BUSY);
    CHECK(foundry_session_create(m, &c, &s, &e) == 0);
    CHECK(foundry_model_unload(m, &e) == FOUNDRY_BUSY);
    foundry_metrics metrics;
    int calls = 0;
    CHECK(foundry_generate(s, "The capital of France is", false, cancel_first, &calls, &metrics,
                           &e) == FOUNDRY_CANCELLED);
    CHECK(calls == 1);
    CHECK(!strcmp(metrics.stop_reason, "cancelled"));
    CHECK(foundry_session_reset(s, &e) == 0);
    size_t bytes = 0;
    CHECK(foundry_generate(s, "The capital of France is", false, collect, &bytes, &metrics, &e) ==
          0);
    CHECK(bytes > 0 && metrics.generated_tokens > 0);
    CHECK(metrics.cached_prompt_tokens == 0);
    int32_t *tokens = NULL;
    size_t count = 0;
    CHECK(foundry_tokenize(m, "The capital of France is", true, false, &tokens, &count, &e) == 0);
    CHECK(count > 0);
    CHECK(foundry_session_reset(s, &e) == 0);
    CHECK(foundry_prefill(s, tokens, count, &e) == 0);
    foundry_free(tokens);
    int32_t token;
    char *piece = NULL;
    size_t length = 0;
    CHECK(foundry_decode(s, &token, &piece, &length, &e) == 0);
    foundry_free(piece);
    foundry_message messages[] = {{"system", "Be brief."},
                                  {"user", "Hello, 世界"},
                                  {"assistant", "Hello."},
                                  {"user", "What is 2+2?"}};
    char *rendered = NULL;
    CHECK(foundry_chat_render(m, messages, 4, &rendered, &e) == 0);
    CHECK(strcmp(rendered, "<|im_start|>system\nBe brief.<|im_end|>\n"
                           "<|im_start|>user\nHello, 世界<|im_end|>\n"
                           "<|im_start|>assistant\nHello.<|im_end|>\n"
                           "<|im_start|>user\nWhat is 2+2?<|im_end|>\n"
                           "<|im_start|>assistant\n") == 0);
    foundry_free(rendered);
    foundry_message empty = {"user", ""};
    CHECK(foundry_chat_render(m, &empty, 1, &rendered, &e) == 0);
    const char *golden = "<|im_start|>system\nYou are a helpful AI assistant named SmolLM, "
                         "trained by Hugging Face<|im_end|>\n"
                         "<|im_start|>user\n<|im_end|>\n<|im_start|>assistant\n";
    CHECK(strcmp(rendered, golden) == 0);
    int32_t *expected_tokens = NULL, *actual_tokens = NULL;
    size_t expected_count = 0, actual_count = 0;
    CHECK(foundry_tokenize(m, golden, true, true, &expected_tokens, &expected_count, &e) == 0);
    CHECK(foundry_tokenize(m, rendered, true, true, &actual_tokens, &actual_count, &e) == 0);
    CHECK(expected_count == actual_count);
    CHECK(memcmp(expected_tokens, actual_tokens, expected_count * sizeof(int32_t)) == 0);
    foundry_free(expected_tokens);
    foundry_free(actual_tokens);
    foundry_free(rendered);
    char long_prompt[8192];
    memset(long_prompt, 'x', sizeof long_prompt - 1);
    long_prompt[sizeof long_prompt - 1] = 0;
    CHECK(foundry_session_reset(s, &e) == 0);
    CHECK(foundry_generate(s, long_prompt, false, NULL, NULL, &metrics, &e) ==
          FOUNDRY_CONTEXT_EXCEEDED);
    foundry_request_cancel(s);
    CHECK(foundry_generate(s, "hello", false, NULL, NULL, &metrics, &e) == FOUNDRY_CANCELLED);
    foundry_session_destroy(s);
    CHECK(foundry_model_unload(m, &e) == 0);
    CHECK(foundry_runtime_destroy(r, &e) == 0);
    puts("Real-model lifecycle, generation, chat, boundary and cancellation checks passed");
    return 0;
}
