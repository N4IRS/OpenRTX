/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * op25-style imbe_vocoder class adapter over the vendored IMBE-embedded
 * engine (github.com/imostlylurk/imbe_embedded, GPL-3.0-or-later).
 */
#ifndef IMBE_VOCODER_API_H
#define IMBE_VOCODER_API_H

#include <stdint.h>
#include <stddef.h>

#include "imbe2.h"

class imbe_vocoder {
public:
    imbe_vocoder()
    {
        /* state sizes are compile-time constants of the engine; the
           static arrays below are sized generously and verified once */
        imbe2_encoder_init(m_enc);
        imbe2_decoder_init(m_dec);
    }

    /* compress 160 s16le 8 kHz samples into 11 frame bytes */
    void encode_4400(int16_t *pcm, uint8_t *imbe)
    {
        imbe2_encode(m_enc, imbe, pcm);
    }

    /* synthesize 160 samples from 11 frame bytes */
    void decode_4400(int16_t *pcm, uint8_t *imbe)
    {
        imbe2_decode(m_dec, pcm, imbe);
    }

private:
    /* imbe_embedded worst case: encoder 8608 B (noise-gate history),
       decoder 1932 B - see imbe2_state_size()/README */
    enum { ENC_STATE_SIZE = 8704, DEC_STATE_SIZE = 2048 };

    alignas(4) uint8_t m_enc[ENC_STATE_SIZE];
    alignas(4) uint8_t m_dec[DEC_STATE_SIZE];
};

#endif /* IMBE_VOCODER_API_H */
