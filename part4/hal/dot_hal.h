#ifndef DOT_HAL_H
#define DOT_HAL_H
#include <stddef.h>
#include <stdint.h>
#include "dot_accel_ioctl.h"
#ifdef __cplusplus
extern "C" {
#endif
struct dot_hal { int fd; };
int dot_hal_open(struct dot_hal *hal, const char *path);
void dot_hal_close(struct dot_hal *hal);
int dot_hal_run_arrays(struct dot_hal *hal, unsigned nb,
                       const int8_t *xq, const uint16_t *xd,
                       const int8_t *yq, const uint16_t *yd,
                       float *result, uint32_t *cycles);
/* n elements in packed Q8_0 blocks: LE uint16 scale, then 32 int8 values. */
int dot_hal_dot_q8_0(struct dot_hal *hal, int n, const void *x, const void *y,
                     float *result, uint32_t *cycles);
#ifdef __cplusplus
}
#endif
#endif
