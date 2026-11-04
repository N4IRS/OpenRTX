/* imbe2_api.cc: small C API over the split imbe engine. */

#include <new>
#include <stddef.h>
#include <string.h>

#include "imbe2.h"
#include "imbe_vocoder.h"

extern "C" {

struct imbe2
{
    imbe_encoder enc; /* first member: imbe2_encode(&combined) works */
    imbe_decoder dec;
};

size_t imbe2_state_size(void)
{
    return sizeof(imbe2_t);
}

void imbe2_init(void *state)
{
    new (state) imbe2_t();
}

void *imbe2_encoder_from(void *combined)
{
    return &((imbe2_t *)combined)->enc;
}

void *imbe2_decoder_from(void *combined)
{
    return &((imbe2_t *)combined)->dec;
}

/* split states: encoder-only / decoder-only objects */

size_t imbe2_encoder_state_size(void)
{
    return sizeof(imbe_encoder);
}

size_t imbe2_decoder_state_size(void)
{
    return sizeof(imbe_decoder);
}

void imbe2_encoder_init(void *state)
{
    new (state) imbe_encoder();
}

void imbe2_decoder_init(void *state)
{
    new (state) imbe_decoder();
}

void imbe2_encoder_set_enh(void *enc_state, int on)
{
    ((imbe_encoder *)enc_state)->set_enh(on);
}

/* Standard IMBE 88-bit prioritized info stream: u0..u3 are 12-bit
   words, u4..u6 11-bit, u7 7-bit; u0's MSB is the first bit of the
   frame, big-endian byte order.  (The previous 8x11-bit packing
   truncated the top bit of u0..u3 - the four lowest-priority
   spectral bits of each frame - on ~half of all frames.) */
static const int word_bits[8] = {12, 12, 12, 12, 11, 11, 11, 7};

static void pack_words(const int16_t *w, uint8_t *out)
{
    int bit = 0;
    memset(out, 0, 11);
    for (int i = 0; i < 8; i++)
        for (int k = word_bits[i] - 1; k >= 0; k--)
        {
            out[bit >> 3] |= ((w[i] >> k) & 1) << (7 - (bit & 7));
            bit++;
        }
}

static void unpack_words(const uint8_t *in, int16_t *w)
{
    int bit = 0;
    for (int i = 0; i < 8; i++)
    {
        int v = 0;
        for (int k = 0; k < word_bits[i]; k++)
        {
            v = (v << 1) | ((in[bit >> 3] >> (7 - (bit & 7))) & 1);
            bit++;
        }
        w[i] = (int16_t)v;
    }
}

void imbe2_encode(void *state, uint8_t *bits, const int16_t *speech)
{
    int16_t words[8];
    ((imbe_encoder *)state)->imbe_encode(words,
                                         const_cast<int16_t *>(speech));
    pack_words(words, bits);
}

void imbe2_decode(void *state, int16_t *speech, const uint8_t *bits)
{
    int16_t words[8];
    unpack_words(bits, words);
    ((imbe_decoder *)state)->imbe_decode(words, speech);
}

} /* extern "C" */
