#include "dot-ggml.h"
#include "dot_hal.h"
#include "quants.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(QK8_0 == 32 && sizeof(block_q8_0) == 34, "Q8_0 layout changed");
_Static_assert(offsetof(block_q8_0, d) == 0 && offsetof(block_q8_0, qs) == 2,
               "Q8_0 packing assumptions changed");

static pthread_once_t once = PTHREAD_ONCE_INIT;
static struct dot_hal hal = { .fd = -1 };

static void shutdown_hal(void)
{
    dot_hal_close(&hal);
}
static void initialize(void)
{
    if (dot_hal_open(&hal, DOT_DEVICE_PATH) < 0) {
        perror("dot accelerator open");
        abort();
    }
    if (atexit(shutdown_hal)) {
        dot_hal_close(&hal);
        fputs("dot accelerator cleanup registration failed\n", stderr);
        abort();
    }
}
void lab3_vec_dot_q8_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx,
                           const void *vy, size_t by, int nrc)
{
    /* Unsupported shapes retain the original application's CPU behavior. */
    if (nrc != 1 || n < 0 || n % DOT_BLOCK_SIZE || (unsigned)n > DOT_XQ_BYTES) {
        ggml_vec_dot_q8_0_q8_0(n, s, bs, vx, bx, vy, by, nrc);
        return;
    }
    if (pthread_once(&once, initialize)) {
        fputs("dot accelerator initialization failed\n", stderr);
        abort();
    }
    if (dot_hal_dot_q8_0(&hal, n, vx, vy, s, NULL) < 0) {
        perror("dot accelerator request");
        abort();
    }
}
