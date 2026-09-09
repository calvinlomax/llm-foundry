#define _POSIX_C_SOURCE 200809L
#include "runtime_internal.h"
#include <time.h>
double foundry_now_ms(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}
