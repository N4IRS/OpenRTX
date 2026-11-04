/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal CMSIS-DSP q15 subset, vendored for the P25 stack
 * so it does not depend on the D-STAR tree.
 */
#ifndef P25_VENDOR_ARM_MATH_H
#define P25_VENDOR_ARM_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int16_t q15_t;
typedef int32_t q31_t;
typedef int64_t q63_t;
typedef float float32_t;

/* CMSIS signed saturation (GCC statement expression, valid in C and C++) */
#ifndef __SSAT
#define __SSAT(ARG1, ARG2)                              \
    ({                                                  \
        int32_t __X = (int32_t)(ARG1);                  \
        if (__X > (int32_t)((1U << ((ARG2) - 1)) - 1U)) \
            __X = (int32_t)((1U << ((ARG2) - 1)) - 1U); \
        else if (__X < -(int32_t)(1U << ((ARG2) - 1)))  \
            __X = -(int32_t)(1U << ((ARG2) - 1));       \
        __X;                                            \
    })
#endif

/* Instance structure for q15 FIR filter (CMSIS layout) */
typedef struct {
    uint16_t numTaps;     /* Number of filter coefficients */
    q15_t *pState;        /* Points to the state variable array */
    const q15_t *pCoeffs; /* Points to the coefficient array */
} arm_fir_instance_q15;

/* Instance structure for q15 FIR interpolation filter (CMSIS layout) */
typedef struct {
    uint8_t L;            /* Upsampling factor */
    uint16_t phaseLength; /* Phase length = numTaps / L */
    const q15_t *pCoeffs; /* Points to the coefficient array */
    q15_t *pState;        /* Points to the state variable array */
} arm_fir_interpolate_instance_q15;

/*
 * Fast q15 FIR filter: processes blockSize samples from pSrc into pDst
 * using S->numTaps taps.  State buffer must hold numTaps + blockSize - 1.
 */
void arm_fir_fast_q15(const arm_fir_instance_q15 *S, q15_t *pSrc, q15_t *pDst,
                      uint32_t blockSize);

/*
 * q15 FIR interpolation filter: produces S->L * blockSize output samples
 * from blockSize input samples.
 */
void arm_fir_interpolate_q15(const arm_fir_interpolate_instance_q15 *S,
                             q15_t *pSrc, q15_t *pDst, uint32_t blockSize);

#ifdef __cplusplus
}
#endif

#endif /* P25_VENDOR_ARM_MATH_H */
