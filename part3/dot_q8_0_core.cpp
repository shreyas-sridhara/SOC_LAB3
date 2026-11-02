/*
 * dot_q8_0_core.cpp  Lab 2 dot product (pure C++, no SystemC)
 */
#include "dot_q8_0_core.h"

#include <string.h>

/*
 * FP16 to FP32 with integer logic. Every FP16 value is a normal FP32 number,
 * so denormals are renormalized here instead.
 */
float dot_half_to_float(dot_half_t b)
{
    uint32_t sign = (uint32_t)(b & 0x8000) << 16;
    uint32_t exp  = (b >> 10) & 0x1F;
    uint32_t man  = b & 0x3FF;
    uint32_t out;

    if (exp == 0x1F) {                 
        out = sign | 0x7F800000 | (man << 13);
    } else if (exp != 0) {              /* normal: rebias 15 to 127 */
        out = sign | ((exp + 112) << 23) | (man << 13);
    } else if (man == 0) {              /* + or -0 */
        out = sign;
    } else {
        /* denormal: shift the leading 1 into the implicit-bit position.
         * Fixed 4-step leading-zero count instead of a data-dependent loop. */
        uint32_t w = man << 6;          /* mantissa bit 9 -> bit 15 */
        uint32_t n = 0;
        if (!(w & 0xFF00)) { n += 8; w <<= 8; }
        if (!(w & 0xF000)) { n += 4; w <<= 4; }
        if (!(w & 0xC000)) { n += 2; w <<= 2; }
        if (!(w & 0x8000)) { n += 1; w <<= 1; }
        out = sign | ((112 - n) << 23) | (((w >> 5) & 0x3FF) << 13);
    }

    float f;
    memcpy(&f, &out, sizeof f);
    return f;
}

/*
 * Body of the Lab 2 BLOCK_LOOP: one block of DOT_QK int8 pairs and their two  FP16 scales.
 */
float dot_q8_0_block(const int8_t *xq, const int8_t *yq, dot_half_t xd, dot_half_t yd)
{
    int sumi = 0;

    for (int j = 0; j < DOT_QK; j++)    /* ELEM_LOOP */
        sumi += xq[j] * yq[j];

    return (float)sumi * (dot_half_to_float(xd) * dot_half_to_float(yd));
}

/* The original Lab 2 function, now just a loop over dot_q8_0_block(). */
float dot_q8_0(int nb, const int8_t xq[DOT_MAX_N], const dot_half_t xd[DOT_MAX_NB],
                       const int8_t yq[DOT_MAX_N], const dot_half_t yd[DOT_MAX_NB])
{
    // Don't read past the end of the BRAMs
    if (nb > DOT_MAX_NB)
        nb = DOT_MAX_NB;

    float sumf = 0.0f;

    for (int ib = 0; ib < nb; ib++)     /* BLOCK_LOOP */
        sumf += dot_q8_0_block(&xq[ib * DOT_QK], &yq[ib * DOT_QK], xd[ib], yd[ib]);

    return sumf;
}
