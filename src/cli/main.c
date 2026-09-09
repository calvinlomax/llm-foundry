#define _POSIX_C_SOURCE 200809L
#include "cJSON.h"
#include "foundry/foundry.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t interrupted = 0;
static void interrupt_handler(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}
typedef struct {
    foundry_session *session;
    atomic_bool done;
} signal_monitor;
static void *watch_signal(void *data) {
    signal_monitor *m = data;
    const struct timespec pause = {0, 20000000};
    while (!atomic_load(&m->done)) {
        if (interrupted)
            foundry_request_cancel(m->session);
        nanosleep(&pause, NULL);
    }
    return NULL;
}
static void help(void) {
    puts("Cinder Foundry " FOUNDRY_VERSION " — offline C17 inference\n"
         "Usage: foundry COMMAND [MODEL | --model PATH] [OPTIONS]\n\n"
         "  import gguf --path FILE --name ID [--copy]\n"
         "  import ollama --manifest FILE --weights-root DIR --metadata-root DIR --name ID "
         "[--copy]\n"
         "  list [--json]                  List registered local models\n"
         "  inspect MODEL [--json]         Inspect GGUF metadata and tensors\n"
         "  plan MODEL [--context N]       Show advisory memory requirements\n"
         "  run MODEL --prompt TEXT       Stream raw completion\n"
         "  chat MODEL [--system TEXT]    Interactive structured chat (/new, /quit)\n"
         "  tokenize MODEL --prompt TEXT [--chat]  Print canonical input token IDs\n"
         "  bench MODEL [--repetitions 7 --warmups 2]  Warm weights, fresh KV samples\n"
         "  backends [--json]              Report backend capabilities and revision\n"
         "  version                       Print version and ABI\n\n"
         "Generation options: --context 2048 --max-tokens 128 --batch-size 128\n"
         "  --threads 4 --temperature 0 --top-p .95 --seed 0 --gpu-layers 0\n"
         "  --backend llama|auto|cpu --memory-budget BYTES --stop TEXT\n"
         "  --prompt TEXT | --prompt-file FILE [--raw | --chat] --metrics-json\n"
         "Raw completion is the run default. --chat renders the embedded template.\n"
         "CPU is the default placement. Positive GPU layers require a GPU build.\n"
         "Text uses stdout; errors, backend logs and run metrics use stderr.\n"
         "Import references weights by default; --copy preserves a registry copy.\n"
         "Tuning: python3 scripts/tune.py --help (optional development tool).");
}
static int error_exit(foundry_status s, const foundry_error *e) {
    fprintf(stderr, "foundry: %s: %s\n", foundry_status_string(s),
            e && e->message[0] ? e->message : foundry_status_string(s));
    return s == FOUNDRY_CANCELLED ? 130 : s == FOUNDRY_INVALID_CONFIG ? 2 : 1;
}
static bool parse_u32(const char *v, uint32_t *out) {
    char *end;
    errno = 0;
    if (!*v || *v == '-')
        return false;
    unsigned long long n = strtoull(v, &end, 10);
    if (errno || *end || n > UINT32_MAX)
        return false;
    *out = (uint32_t)n;
    return true;
}
static bool parse_float(const char *v, float *out) {
    char *end;
    errno = 0;
    float x = strtof(v, &end);
    if (errno || !*v || *end || !isfinite(x))
        return false;
    *out = x;
    return true;
}
static char *read_prompt(const char *path) {
    FILE *f = !strcmp(path, "-") ? stdin : fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    char *p = malloc(cap);
    if (!p) {
        if (f != stdin)
            fclose(f);
        return NULL;
    }
    for (;;) {
        if (n == cap - 1) {
            if (cap >= 16 * 1024 * 1024) {
                free(p);
                p = NULL;
                break;
            }
            cap *= 2;
            char *q = realloc(p, cap);
            if (!q) {
                free(p);
                p = NULL;
                break;
            }
            p = q;
        }
        size_t got = fread(p + n, 1, cap - n - 1, f);
        n += got;
        if (!got) {
            if (ferror(f)) {
                free(p);
                p = NULL;
            }
            break;
        }
    }
    if (f != stdin)
        fclose(f);
    if (p) {
        p[n] = 0;
        if (memchr(p, 0, n)) {
            free(p);
            return NULL;
        }
    }
    return p;
}
typedef struct {
    char *text;
    size_t size;
    bool print;
    bool failed;
} output;
static bool token_output(const char *p, size_t n, void *data) {
    output *o = data;
    if (interrupted)
        return false;
    if (o->print && fwrite(p, 1, n, stdout) != n) {
        o->failed = true;
        return false;
    }
    if (o->print)
        fflush(stdout);
    if (o->text) {
        char *q = realloc(o->text, o->size + n + 1);
        if (!q) {
            o->failed = true;
            return false;
        }
        o->text = q;
        memcpy(o->text + o->size, p, n);
        o->size += n;
        o->text[o->size] = 0;
    }
    return true;
}
static cJSON *configuration(const foundry_config *c) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "context", c->context);
    cJSON_AddNumberToObject(j, "max_tokens", c->max_tokens);
    cJSON_AddNumberToObject(j, "batch_size", c->batch_size);
    cJSON_AddNumberToObject(j, "threads", c->threads);
    cJSON_AddNumberToObject(j, "temperature", c->temperature);
    cJSON_AddNumberToObject(j, "top_p", c->top_p);
    cJSON_AddNumberToObject(j, "seed", c->seed);
    cJSON_AddNumberToObject(j, "gpu_layers", c->gpu_layers);
    cJSON_AddNumberToObject(j, "memory_budget", (double)c->memory_budget);
    if (c->stop)
        cJSON_AddStringToObject(j, "stop", c->stop);
    else
        cJSON_AddNullToObject(j, "stop");
    return j;
}
static cJSON *measurement(const foundry_metrics *m, const foundry_config *c, foundry_status s) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "schema_version", 1);
    cJSON_AddStringToObject(j, "event", "metrics");
    cJSON_AddStringToObject(j, "status", foundry_status_string(s));
    cJSON_AddNumberToObject(j, "prompt_tokens", m->prompt_tokens);
    cJSON_AddNumberToObject(j, "generated_tokens", m->generated_tokens);
    cJSON_AddNumberToObject(j, "cached_prompt_tokens", m->cached_prompt_tokens);
    cJSON_AddNumberToObject(j, "load_ms", m->load_ms);
    cJSON_AddNumberToObject(j, "prefill_ms", m->prefill_ms);
    cJSON_AddNumberToObject(j, "decode_ms", m->decode_ms);
    cJSON_AddNumberToObject(j, "first_token_ms", m->first_token_ms);
    cJSON_AddNumberToObject(j, "total_ms", m->total_ms);
    if (m->decode_ms > 0 && m->generated_tokens)
        cJSON_AddNumberToObject(j, "decode_tokens_per_second",
                                m->generated_tokens * 1000.0 / m->decode_ms);
    else
        cJSON_AddNullToObject(j, "decode_tokens_per_second");
    cJSON_AddStringToObject(j, "stop_reason", m->stop_reason);
    cJSON_AddItemToObject(j, "effective_config", configuration(c));
    cJSON_AddItemToObject(j, "backend", cJSON_Parse(foundry_capabilities_json()));
    cJSON_AddStringToObject(
        j, "actual_placement",
        c->gpu_layers == 0
            ? "cpu"
            : "see backend load log; per-layer allocation is not exposed by this API");
    cJSON_AddNullToObject(j, "observed_memory_bytes");
    cJSON_AddStringToObject(j, "memory_method", "unavailable");
    return j;
}
static void print_json(FILE *f, cJSON *j) {
    char *s = cJSON_PrintUnformatted(j);
    if (s) {
        fprintf(f, "%s\n", s);
        free(s);
    }
    cJSON_Delete(j);
}
static void clear_history(foundry_message *h, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free((char *)h[i].role);
        free((char *)h[i].content);
    }
}
static bool add_message(foundry_message **h, size_t *n, const char *role, const char *text) {
    char *r = strdup(role), *t = strdup(text);
    if (!r || !t) {
        free(r);
        free(t);
        return false;
    }
    foundry_message *p = realloc(*h, (*n + 1) * sizeof **h);
    if (!p) {
        free(r);
        free(t);
        return false;
    }
    *h = p;
    p[*n] = (foundry_message){r, t};
    (*n)++;
    return true;
}
int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) {
        help();
        return 0;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version")) {
        printf("Cinder Foundry %s (C ABI %d)\n", FOUNDRY_VERSION, FOUNDRY_ABI_VERSION);
        return 0;
    }
    const char *id = NULL, *prompt = NULL, *prompt_file = NULL, *manifest = NULL, *weights = ".",
               *metadata = "metadata", *name = NULL, *path = NULL, *system = NULL, *format = NULL;
    bool metrics = false, chat_format = false, copy = false;
    uint32_t repetitions = 7, warmups = 2;
    foundry_config c = foundry_config_default();
    int start = 2;
    if (!strcmp(cmd, "import")) {
        if (argc < 3) {
            help();
            return 2;
        }
        format = argv[2];
        start = 3;
    }
    for (int i = start; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help")) {
            help();
            return 0;
        }
        if (!strcmp(a, "--json") || !strcmp(a, "--raw")) {
            if (!strcmp(a, "--raw"))
                chat_format = false;
            continue;
        }
        if (!strcmp(a, "--metrics-json")) {
            metrics = true;
            continue;
        }
        if (!strcmp(a, "--chat")) {
            chat_format = true;
            continue;
        }
        if (!strcmp(a, "--copy")) {
            copy = true;
            continue;
        }
        if (a[0] != '-') {
            if (id) {
                fprintf(stderr, "Unexpected positional argument: %s\n", a);
                return 2;
            }
            id = a;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "Missing value for %s\n", a);
            return 2;
        }
        const char *v = argv[++i];
        bool ok = true;
        if (!strcmp(a, "--model"))
            id = v;
        else if (!strcmp(a, "--prompt"))
            prompt = v;
        else if (!strcmp(a, "--prompt-file"))
            prompt_file = v;
        else if (!strcmp(a, "--system"))
            system = v;
        else if (!strcmp(a, "--manifest"))
            manifest = v;
        else if (!strcmp(a, "--weights-root"))
            weights = v;
        else if (!strcmp(a, "--metadata-root"))
            metadata = v;
        else if (!strcmp(a, "--name"))
            name = v;
        else if (!strcmp(a, "--path"))
            path = v;
        else if (!strcmp(a, "--stop"))
            c.stop = v;
        else if (!strcmp(a, "--context"))
            ok = parse_u32(v, &c.context);
        else if (!strcmp(a, "--max-tokens"))
            ok = parse_u32(v, &c.max_tokens);
        else if (!strcmp(a, "--batch-size"))
            ok = parse_u32(v, &c.batch_size);
        else if (!strcmp(a, "--threads"))
            ok = parse_u32(v, &c.threads);
        else if (!strcmp(a, "--seed"))
            ok = parse_u32(v, &c.seed);
        else if (!strcmp(a, "--temperature"))
            ok = parse_float(v, &c.temperature);
        else if (!strcmp(a, "--top-p"))
            ok = parse_float(v, &c.top_p);
        else if (!strcmp(a, "--repetitions"))
            ok = parse_u32(v, &repetitions);
        else if (!strcmp(a, "--warmups"))
            ok = parse_u32(v, &warmups);
        else if (!strcmp(a, "--gpu-layers")) {
            if (!strcmp(v, "-1"))
                c.gpu_layers = -1;
            else {
                uint32_t n;
                ok = parse_u32(v, &n) && n <= INT32_MAX;
                if (ok)
                    c.gpu_layers = (int32_t)n;
            }
        } else if (!strcmp(a, "--backend")) {
            if (!strcmp(v, "cpu"))
                c.gpu_layers = 0;
            else if (strcmp(v, "llama") && strcmp(v, "auto"))
                ok = false;
        } else if (!strcmp(a, "--memory-budget")) {
            char *end;
            errno = 0;
            c.memory_budget = strtoull(v, &end, 10);
            ok = *v && *v != '-' && !*end && !errno;
        } else {
            fprintf(stderr, "Unknown option: %s\n", a);
            return 2;
        }
        if (!ok) {
            fprintf(stderr, "Invalid value for %s: %s\n", a, v);
            return 2;
        }
    }
    foundry_error e = {0};
    foundry_status s;
    char *json = NULL;
    if (!strcmp(cmd, "backends")) {
        puts(foundry_capabilities_json());
        return 0;
    }
    if (!strcmp(cmd, "list")) {
        s = foundry_registry_list(&json, &e);
        if (s)
            return error_exit(s, &e);
        puts(json);
        free(json);
        return 0;
    }
    if (!strcmp(cmd, "import")) {
        if (!strcmp(format, "gguf"))
            s = foundry_import_gguf(path ? path : id, name, copy, &json, &e);
        else if (!strcmp(format, "ollama"))
            s = foundry_import_ollama(manifest, weights, metadata, name, copy, &json, &e);
        else {
            fprintf(stderr, "Import format must be gguf or ollama\n");
            return 2;
        }
        if (s)
            return error_exit(s, &e);
        cJSON *record = cJSON_Parse(json);
        cJSON_DeleteItemFromObjectCaseSensitive(record, "inspection");
        cJSON_DeleteItemFromObjectCaseSensitive(record, "provenance");
        print_json(stdout, record);
        free(json);
        return 0;
    }
    if (!id) {
        fprintf(stderr, "A model ID or path is required\n");
        return 2;
    }
    if (!strcmp(cmd, "inspect")) {
        foundry_model_info info;
        s = foundry_inspect(id, &info, &json, &e);
        if (s)
            return error_exit(s, &e);
        puts(json);
        free(json);
        return 0;
    }
    s = foundry_config_validate(&c, &e);
    if (s)
        return error_exit(s, &e);
    if (!strcmp(cmd, "plan")) {
        s = foundry_plan(id, &c, &json, &e);
        if (s)
            return error_exit(s, &e);
        puts(json);
        free(json);
        return 0;
    }
    if (strcmp(cmd, "run") && strcmp(cmd, "chat") && strcmp(cmd, "bench") &&
        strcmp(cmd, "tokenize")) {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return 2;
    }
    if (repetitions < 1 || repetitions > 1000 || warmups > 100) {
        fprintf(stderr, "Repetitions must be 1–1000 and warmups 0–100\n");
        return 2;
    }
    char *owned_prompt = NULL;
    if (prompt && prompt_file) {
        fprintf(stderr, "Use one prompt source\n");
        return 2;
    }
    if (prompt_file) {
        owned_prompt = read_prompt(prompt_file);
        if (!owned_prompt) {
            fprintf(stderr, "Cannot read prompt (16 MiB limit; no NUL bytes)\n");
            return 2;
        }
        prompt = owned_prompt;
    }
    if (!prompt && !strcmp(cmd, "bench"))
        prompt = "The capital of France is";
    if (!prompt && strcmp(cmd, "chat")) {
        fprintf(stderr, "--prompt or --prompt-file is required\n");
        free(owned_prompt);
        return 2;
    }
    foundry_runtime *r = NULL;
    foundry_model *m = NULL;
    foundry_session *session = NULL;
    signal_monitor monitor = {0};
    pthread_t watcher;
    bool watching = false;
    s = foundry_runtime_create(&r, &e);
    if (!s)
        s = foundry_model_load(r, id, &c, &m, &e);
    if (!s && strcmp(cmd, "tokenize"))
        s = foundry_session_create(m, &c, &session, &e);
    if (s)
        goto cleanup;
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);
    if (session) {
        monitor.session = session;
        atomic_init(&monitor.done, false);
        watching = pthread_create(&watcher, NULL, watch_signal, &monitor) == 0;
        if (!watching) {
            s = FOUNDRY_BACKEND_ERROR;
            snprintf(e.message, sizeof e.message, "Cannot start cancellation monitor");
            goto cleanup;
        }
    }
    if (!strcmp(cmd, "tokenize") || !strcmp(cmd, "run") || !strcmp(cmd, "bench")) {
        char *rendered = NULL;
        if (chat_format) {
            foundry_message messages[2] = {{"system", system}, {"user", prompt}};
            s = foundry_chat_render(m, system ? messages : messages + 1, system ? 2 : 1, &rendered,
                                    &e);
            if (s)
                goto cleanup;
            prompt = rendered;
        }
        if (!strcmp(cmd, "tokenize")) {
            int32_t *tokens = NULL;
            size_t count = 0;
            s = foundry_tokenize(m, prompt, true, chat_format, &tokens, &count, &e);
            if (!s) {
                cJSON *a = cJSON_CreateArray();
                for (size_t i = 0; i < count; i++)
                    cJSON_AddItemToArray(a, cJSON_CreateNumber(tokens[i]));
                print_json(stdout, a);
            }
            free(tokens);
        } else if (!strcmp(cmd, "run")) {
            output o = {.print = true};
            foundry_metrics result = {0};
            s = foundry_generate(session, prompt, chat_format, token_output, &o, &result, &e);
            if (metrics)
                print_json(stderr, measurement(&result, &c, s));
            if (o.failed && s == FOUNDRY_CANCELLED) {
                s = FOUNDRY_IO_ERROR;
                snprintf(e.message, sizeof e.message, "Output stream failed");
            }
        } else {
            cJSON *results = cJSON_CreateObject(),
                  *samples = cJSON_AddArrayToObject(results, "samples");
            cJSON_AddNumberToObject(results, "schema_version", 1);
            cJSON_AddStringToObject(results, "mode", "warm_model_fresh_session");
            cJSON_AddStringToObject(results, "model", id);
            cJSON_AddStringToObject(
                results, "clock_definition",
                "decode includes sample, token detokenization and synchronized evaluation of every "
                "emitted token; first-token starts before tokenization, excludes load");
            for (uint32_t i = 0; i < warmups + repetitions; i++) {
                foundry_metrics result = {0};
                output o = {0};
                s = foundry_session_reset(session, &e);
                if (!s)
                    s = foundry_generate(session, prompt, chat_format, token_output, &o, &result,
                                         &e);
                cJSON *sample = measurement(&result, &c, s);
                cJSON_AddBoolToObject(sample, "warmup", i < warmups);
                cJSON_AddNumberToObject(sample, "index", i);
                cJSON_AddItemToArray(samples, sample);
                if (s)
                    break;
            }
            print_json(stdout, results);
        }
        free(rendered);
    } else {
        foundry_message *history = NULL;
        size_t count = 0;
        char *line = NULL;
        size_t capacity = 0;
        if (system && !add_message(&history, &count, "system", system)) {
            s = FOUNDRY_OUT_OF_MEMORY;
            goto chat_done;
        }
        fprintf(stderr, "Chat commands: /new, /quit. One input line per turn.\n");
        for (;;) {
            fprintf(stderr, "you> ");
            if (getline(&line, &capacity, stdin) < 0)
                break;
            line[strcspn(line, "\r\n")] = 0;
            if (!strcmp(line, "/quit"))
                break;
            if (!strcmp(line, "/new")) {
                clear_history(history, count);
                count = 0;
                if (system && !add_message(&history, &count, "system", system)) {
                    s = FOUNDRY_OUT_OF_MEMORY;
                    break;
                }
                foundry_session_reset(session, &e);
                continue;
            }
            if (!*line)
                continue;
            if (!add_message(&history, &count, "user", line)) {
                s = FOUNDRY_OUT_OF_MEMORY;
                break;
            }
            char *rendered = NULL;
            s = foundry_chat_render(m, history, count, &rendered, &e);
            output o = {.text = calloc(1, 1), .print = true};
            foundry_metrics result = {0};
            if (!o.text)
                s = FOUNDRY_OUT_OF_MEMORY;
            if (!s)
                s = foundry_session_reset(session, &e);
            if (!s)
                s = foundry_generate(session, rendered, true, token_output, &o, &result, &e);
            free(rendered);
            puts("");
            if (metrics)
                print_json(stderr, measurement(&result, &c, s));
            if (!s && !add_message(&history, &count, "assistant", o.text))
                s = FOUNDRY_OUT_OF_MEMORY;
            free(o.text);
            if (s)
                break;
        }
    chat_done:
        free(line);
        clear_history(history, count);
        free(history);
    }
cleanup:
    if (watching) {
        atomic_store(&monitor.done, true);
        pthread_join(watcher, NULL);
    }
    foundry_session_destroy(session);
    foundry_model_unload(m, NULL);
    foundry_runtime_destroy(r, NULL);
    free(owned_prompt);
    return s ? error_exit(s, &e) : 0;
}
