#ifndef IMBE2_H
#define IMBE2_H

/*
  IMBE-embedded: IMBE (4400 bps) vocoder with a small C API.

  State is owned by the caller and can be allocated statically:
  allocate imbe2_state_size() bytes, then imbe2_init().  No heap is
  used after the placement construction in imbe2_init (the underlying
  engine is a fixed-size C++ object with no dynamic members).

  Engine: Pavel Yazev's fixed-point IMBE encoder/decoder (via op25,
  GPLv3+), with the imbe-mod optimizations (bit-exact: e_p result
  caching, LTO-friendly build, guarded exact-sum autocorrelation).
*/

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define IMBE2_SAMPLES_PER_FRAME 160 /* 20 ms at 8 kHz */
#define IMBE2_BYTES_PER_FRAME 11   /* 88 bits */

/* imbe file header (tools): magic "IMB1", version, reserved */
#define IMBE2_FILE_MAGIC ((uint32_t)0x31424D49) /* "IMB1" LE */
#define IMBE2_FILE_VERSION 2

typedef struct imbe2 imbe2_t; /* opaque; see imbe2_state_size() */

size_t imbe2_state_size(void);
void imbe2_init(void *state);

/* member accessors for the combined object (imbe2_encode takes an
   encoder state, imbe2_decode a decoder state) */
void *imbe2_encoder_from(void *combined);
void *imbe2_decoder_from(void *combined);

/* split states: allocate imbe2_encoder_state_size() /
   imbe2_decoder_state_size() bytes for encoder-only or decoder-only
   use (modem and codec in separate tasks) */
size_t imbe2_encoder_state_size(void);
size_t imbe2_decoder_state_size(void);
void imbe2_encoder_init(void *state);
/* enable the enhanced encoder (leashed pitch tracking, voicing
   hysteresis, noise gate) on an encoder state; default off */
void imbe2_encoder_set_enh(void *enc_state, int on);
void imbe2_decoder_init(void *state);

/* speech: 160 samples in; bits: 11 bytes out (word 0 in the low
 bits, big-endian byte order; u0..u3 are 12-bit words, u4..u6 11-bit, u7 7-bit) */
void imbe2_encode(void *state, uint8_t *bits, const int16_t *speech);

/* bits: 11 bytes in; speech: 160 samples out */
void imbe2_decode(void *state, int16_t *speech, const uint8_t *bits);

#ifdef __cplusplus
}
#endif

#endif /* IMBE2_H */
