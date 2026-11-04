/*
 * Project 25 IMBE Encoder/Decoder Fixed-Point implementation
 * Developed by Pavel Yazev E-mail: pyazev@gmail.com
 * Version 1.0 (c) Copyright 2009
 */
/* -*- c++ -*- */
#ifndef INCLUDED_IMBE_VOCODER_H
#define INCLUDED_IMBE_VOCODER_H

#include <stdint.h>
#include <string.h>

#include "imbe.h"
#include "dsp_sub.h"
#include "basic_op.h"
#include "math_sub.h"
#include "encode.h"
#include "decode.h"

/*
  Split-state design: separate encoder and decoder objects.
  All parameter exchange between the two happens through the frame
  bits, so each side is fully self-contained.  The shared DSP
  transforms (fft/dct/idct) are free functions taking the caller's
  twiddle arrays (dsp_sub.h).
*/

class imbe_encoder
{
public:
	imbe_encoder(void);	// constructor
	~imbe_encoder() {}	// destructor
	// imbe_encode compresses 160 samples
	// outputs u[] vectors as frame_vector[]
	void imbe_encode(int16_t *frame_vector, int16_t *snd) {
		encode(&my_imbe_param, frame_vector, snd);
	}
	// enable gain adjust for derived codecs (e.g. AMBE experiments)
	void set_gain_adjust(float gain_adjust) {d_gain_adjust = gain_adjust;}

	/* enhanced encoder: leashed-continuity pitch tracking, wider
	   voicing hysteresis, stricter octave gating and the noise gate
	   (the imbe_enc2 tool enables this; default off = bit-exact
	   imbe-mod output) */
	void set_enh(int on) {enh = on;}

private:
	IMBE_PARAM my_imbe_param;

	/* encoder state (was mixed into imbe_vocoder) */
	Word16 prev_pitch, prev_prev_pitch, prev_e_p, prev_prev_e_p;
	Word16 num_harms_prev2;
	Word32 sa_prev2[NUM_HARMS_MAX + 2];
	Word32 th_max;
	Word16 v_uv_dsn[NUM_BANDS_MAX];
	Word16 wr_array[FFTLENGTH / 2 + 1];
	Word16 wi_array[FFTLENGTH / 2 + 1];
	Word16 pitch_est_buf[PITCH_EST_BUF_SIZE];
	Word16 pitch_ref_buf[PITCH_EST_BUF_SIZE];
	/* e_p() is a pure function of its 301-sample input window; the
	   pitch buffer slides by FRAME per encode(), so the look-ahead
	   results of previous frames are the current-frame results.  Cache
	   them (bit-exact: same inputs, same pure function). */
	Word16 e_p_cache[3][203];
	int e_p_cache_valid[3];
	Word32 dc_rmv_mem;
	Cmplx16 fft_buf[FFTLENGTH];
	Word16 pe_lpf_mem[PE_LPF_ORD];
	float d_gain_adjust;
	int enh;
	/* noise gate: min-statistics noise floor history (frame power,
	   windowed minimum) */
	Word32 ng_hist[150]; /* 3 s min-statistics window */
	int ng_idx;
	int ng_open;

	/* member functions (encoder subset) */
	void encode(IMBE_PARAM *imbe_param, Word16 *frame_vector, Word16 *snd);
	void pitch_est_init(void);
	Word32 autocorr(Word16 *sigin, Word16 shift, Word16 scale_shift);
	Word32 autocorr_slow(const Word16 *sigin, Word16 shift, Word16 scale_shift);
	void e_p(Word16 *sigin, Word16 *res_buf);
	void pitch_est(IMBE_PARAM *imbe_param, Word16 *frames_buf);
	void sa_encode_init(void);
	void sa_encode(IMBE_PARAM *imbe_param);
	void pitch_ref_init(void);
	Word16 voiced_sa_calc(Word32 num, Word16 den);
	Word16 unvoiced_sa_calc(Word32 num, Word16 den);
	void v_uv_det(IMBE_PARAM *imbe_param, Cmplx16 *fft_buf);
	void encode_init(void);
};

class imbe_decoder
{
public:
	imbe_decoder(void);	// constructor
	~imbe_decoder() {}	// destructor
	// imbe_decode decodes IMBE codewords (frame_vector),
	// outputs the resulting 160 audio samples (snd)
	void imbe_decode(int16_t *frame_vector, int16_t *snd) {
		decode(&my_imbe_param, frame_vector, snd);
	}

private:
	IMBE_PARAM my_imbe_param;

	/* decoder state (was mixed into imbe_vocoder) */
	UWord32 seed ;
	Word16 num_harms_prev1;
	Word32 sa_prev1[NUM_HARMS_MAX + 2];
	Word16 uv_mem[105];
	UWord32 ph_mem[NUM_HARMS_MAX];
	Word16 num_harms_prev3;
	Word32 fund_freq_prev;
	Word16 vu_dsn_prev[NUM_HARMS_MAX];
	Word16 sa_prev3[NUM_HARMS_MAX];
	Word16 v_uv_dsn[NUM_BANDS_MAX];
	Word16 wr_array[FFTLENGTH / 2 + 1];
	Word16 wi_array[FFTLENGTH / 2 + 1];

	/* member functions (decoder subset) */
	void sa_decode_init(void);
	void sa_decode(IMBE_PARAM *imbe_param);
	void uv_synt_init(void);
	void uv_synt(IMBE_PARAM *imbe_param, Word16 *snd);
	void v_synt_init(void);
	void v_synt(IMBE_PARAM *imbe_param, Word16 *snd);
	void decode_init(IMBE_PARAM *imbe_param);
	void decode(IMBE_PARAM *imbe_param, Word16 *frame_vector, Word16 *snd);
};

#endif /* INCLUDED_IMBE_VOCODER_H */
