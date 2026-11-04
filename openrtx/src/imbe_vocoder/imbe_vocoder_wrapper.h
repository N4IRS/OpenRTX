#ifndef IMBE_VOCODER_WRAPPER_H
#define IMBE_VOCODER_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* one codec instance; internally imbe_vocoder (~28 KiB, static-friendly) */
void *imbe_new(void);
void imbe_free(void *v);

/* snd: 160 samples (20 ms @ 8 kHz) in; frame_words: 8 x int16 not used */
void imbe_encode_frame(void *v, uint8_t *frame11, const int16_t *snd);

/* frame11: 11 bytes (88 bits); snd: 160 samples out */
void imbe_decode_frame(void *v, int16_t *snd, const uint8_t *frame11);

#ifdef __cplusplus
}
#endif

#endif /* IMBE_VOCODER_WRAPPER_H */
