/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Portable scalar implementation of the CMSIS-DSP q15 FIR kernels used by
 * the P25 modem (arm_fir_fast_q15 and arm_fir_interpolate_q15).  Written in
 * plain C so it builds on every target; semantics follow CMSIS-DSP.
 */
#include <string.h>
#include <P25/vendor/arm_math.h>

void arm_fir_fast_q15(const arm_fir_instance_q15 *S, q15_t *pSrc, q15_t *pDst,
                      uint32_t blockSize)
{
    const uint16_t numTaps = S->numTaps;
    q15_t *state = S->pState;

    if ((numTaps == 0U) || (blockSize == 0U))
        return;

    /* append the new block after the numTaps-1 samples kept from the
       previous call: window for output i is state[i .. i+numTaps-1] */
    memcpy(&state[numTaps - 1U], pSrc, blockSize * sizeof(q15_t));

    for (uint32_t i = 0U; i < blockSize; i++) {
        const q15_t *window = &state[i];
        const q15_t *coeffs = S->pCoeffs;
        q31_t acc = 0;

        for (uint32_t j = 0U; j < numTaps; j++)
            acc += (q31_t)window[j] * (q31_t)coeffs[j];

        pDst[i] = (q15_t)(__SSAT((acc >> 15), 16));
    }

    /* keep the last numTaps-1 samples for the next call */
    memmove(state, &state[blockSize], (numTaps - 1U) * sizeof(q15_t));
}

void arm_fir_interpolate_q15(const arm_fir_interpolate_instance_q15 *S,
                             q15_t *pSrc, q15_t *pDst, uint32_t blockSize)
{
    const uint8_t L = S->L;
    const uint16_t phaseLen = S->phaseLength;
    q15_t *state = S->pState;

    if ((L == 0U) || (phaseLen == 0U) || (blockSize == 0U))
        return;

    /* append the new block after the phaseLength-1 kept samples */
    memcpy(&state[phaseLen - 1U], pSrc, blockSize * sizeof(q15_t));

    for (uint32_t n = 0U; n < blockSize; n++) {
        const q15_t *window = &state[n];

        /* CMSIS emits the L outputs of input sample n with polyphase
           coefficient offsets L-1, L-2, ..., 0 */
        for (uint32_t ph = 0U; ph < L; ph++) {
            const q15_t *coeffs = &S->pCoeffs[(L - 1U) - ph];
            q63_t acc = 0;

            for (uint32_t k = 0U; k < phaseLen; k++)
                acc += (q63_t)window[k] * (q63_t)coeffs[k * L];

            pDst[n * L + ph] = (q15_t)(__SSAT((acc >> 15), 16));
        }
    }

    /* keep the last phaseLength-1 samples for the next call */
    memmove(state, &state[blockSize], (phaseLen - 1U) * sizeof(q15_t));
}
