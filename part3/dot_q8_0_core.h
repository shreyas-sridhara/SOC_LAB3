/*
 * dot_q8_0_core.h  
 */
#ifndef DOT_Q8_0_CORE_H
#define DOT_Q8_0_CORE_H

#include <stdint.h>

#define DOT_QK     32
#define DOT_MAX_N  4864                   /* largest dot length in Qwen2.5-0.5B */
#define DOT_MAX_NB (DOT_MAX_N / DOT_QK)   /* 152 blocks */

/* FP16 stored as raw bits (replaces the HLS `half` type) */
typedef uint16_t dot_half_t;

/* FP16 -> FP32 with integer logic */
float dot_half_to_float(dot_half_t h);

/*
 * One BLOCK_LOOP iteration:  (float)sum(xq[j]*yq[j]) * (h2f(xd) * h2f(yd))
 * xq / yq point at the first element of the block (DOT_QK int8 values).
 */
float dot_q8_0_block(const int8_t *xq, const int8_t *yq, dot_half_t xd, dot_half_t yd);

/* nb = number of 32-element blocks, clamped to DOT_MAX_NB. */
float dot_q8_0(int nb, const int8_t xq[DOT_MAX_N], const dot_half_t xd[DOT_MAX_NB],
                       const int8_t yq[DOT_MAX_N], const dot_half_t yd[DOT_MAX_NB]);

#endif
