#ifndef DOT_GGML_H
#define DOT_GGML_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void lab3_vec_dot_q8_0_q8_0(int n, float *s, size_t bs, const void *vx, size_t bx,
                           const void *vy, size_t by, int nrc);
#ifdef __cplusplus
}
#endif
#endif
