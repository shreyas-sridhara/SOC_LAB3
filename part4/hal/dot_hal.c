#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "dot_hal.h"
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <string.h>
#include <unistd.h>

_Static_assert(sizeof(struct dot_run) == 56, "RUN ABI layout changed");
_Static_assert(offsetof(struct dot_run, xq) == 8, "RUN pointer alignment changed");
_Static_assert(sizeof(struct dot_info) == 16, "INFO ABI layout changed");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "FP32 host required");

static int little_endian(void)
{
    const uint16_t one = 1;
    return *(const unsigned char *)&one == 1;
}
static int fail(int error)
{
    errno = error;
    return -1;
}
int dot_hal_open(struct dot_hal *hal, const char *path)
{
    struct dot_info info = {0};
    int saved;
    if (!hal)
        return fail(EINVAL);
    memset(hal, 0, sizeof(*hal));
    hal->fd = -1;
    if (!little_endian())
        return fail(ENOTSUP);
    hal->fd = open(path ? path : DOT_DEVICE_PATH, O_RDWR | O_CLOEXEC);
    if (hal->fd < 0)
        return -1;
    if (ioctl(hal->fd, DOT_IOC_INFO, &info) < 0) {
        saved = errno;
        close(hal->fd);
        hal->fd = -1;
        return fail(saved);
    }
    if (info.id != DOT_ID_VALUE || info.max_blocks != DOT_CAPACITY_BLOCKS ||
        info.block_size != DOT_BLOCK_SIZE) {
        close(hal->fd);
        hal->fd = -1;
        return fail(EPROTO);
    }
    return 0;
}
void dot_hal_close(struct dot_hal *hal)
{
    if (!hal)
        return;
    if (hal->fd >= 0)
        close(hal->fd);
    memset(hal, 0, sizeof(*hal));
    hal->fd = -1;
}
int dot_hal_run_arrays(struct dot_hal *hal, unsigned nb,
                       const int8_t *xq, const uint16_t *xd,
                       const int8_t *yq, const uint16_t *yd,
                       float *result, uint32_t *cycles)
{
    struct dot_run r = {0};
    if (!hal || hal->fd < 0 || !result)
        return fail(EINVAL);
    if (nb > DOT_CAPACITY_BLOCKS)
        return fail(EMSGSIZE);
    if (nb && (!xq || !xd || !yq || !yd))
        return fail(EINVAL);
    r.nb = nb;
    r.xq = (uint64_t)(uintptr_t)xq;
    r.yq = (uint64_t)(uintptr_t)yq;
    r.xd = (uint64_t)(uintptr_t)xd;
    r.yd = (uint64_t)(uintptr_t)yd;
    /* RUN may already execute before an error: do not retry blindly. */
    if (ioctl(hal->fd, DOT_IOC_RUN, &r) < 0)
        return -1;
    if (!(r.status & DOT_STATUS_DONE) ||
        (r.status & (DOT_STATUS_ERR | DOT_STATUS_BUSY)))
        return fail(EIO);
    memcpy(result, &r.result_bits, sizeof(*result));
    if (cycles)
        *cycles = r.cycles;
    return 0;
}
int dot_hal_dot_q8_0(struct dot_hal *hal, int n, const void *x, const void *y,
                     float *result, uint32_t *cycles)
{
    /* Per-call buffers permit concurrent callers; driver serializes MMIO. */
    int8_t xq[DOT_XQ_BYTES], yq[DOT_XQ_BYTES];
    uint16_t xd[DOT_CAPACITY_BLOCKS], yd[DOT_CAPACITY_BLOCKS];
    const unsigned char *xb = x, *yb = y;
    unsigned nb, i;
    if (n < 0 || n % DOT_BLOCK_SIZE)
        return fail(EINVAL);
    if ((unsigned)n > DOT_XQ_BYTES)
        return fail(EMSGSIZE);
    if (n && (!x || !y))
        return fail(EINVAL);
    nb = (unsigned)n / DOT_BLOCK_SIZE;
    for (i = 0; i < nb; ++i) {
        size_t offset = i * (DOT_BLOCK_SIZE + 2);
        memcpy(&xd[i], xb + offset, 2);
        memcpy(&yd[i], yb + offset, 2);
        memcpy(xq + i * DOT_BLOCK_SIZE, xb + offset + 2, DOT_BLOCK_SIZE);
        memcpy(yq + i * DOT_BLOCK_SIZE, yb + offset + 2, DOT_BLOCK_SIZE);
    }
    return dot_hal_run_arrays(hal, nb, xq, xd, yq, yd, result, cycles);
}
