/*
 * Project 25 IMBE Encoder/Decoder Fixed-Point implementation
 * Developed by Pavel Yazev E-mail: pyazev@gmail.com
 * Version 1.0 (c) Copyright 2009
 * 
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * The software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Boston, MA
 * 02110-1301, USA.
 */


#include "typedef.h"
#include "globals.h"
#include "imbe.h"
#include "basic_op.h"
#include "dsp_sub.h"
#include "aux_sub.h"
#include "tbls.h"
#include "encode.h"
#include "dc_rmv.h"
#include "pe_lpf.h"
#include "pitch_est.h"
#include "pitch_ref.h"
#include "v_uv_det.h"
#include "sa_encode.h"
#include "ch_encode.h"
#include "imbe_vocoder.h"

#ifdef ENC_DIAG
#include <stdio.h>
extern "C" int enc_diag_frame;
#define frame_cnt enc_diag_frame
#endif




void imbe_encoder::encode_init(void)
{
	v_zap(pitch_est_buf, PITCH_EST_BUF_SIZE);
	v_zap(pitch_ref_buf, PITCH_EST_BUF_SIZE);
	v_zap(pe_lpf_mem, PE_LPF_ORD);
	pitch_est_init();
	fft_init(wr_array, wi_array);
	dc_rmv_mem = 0;
	sa_encode_init();
	pitch_ref_init();
}


void imbe_encoder::encode(IMBE_PARAM *imbe_param, Word16 *frame_vector, Word16 *snd)
{
	Word16 i;
	Word16 *wr_ptr, *sig_ptr;

	//=====================================================================
	// Enhanced mode: spectral noise gate, time domain.
	//
	//   Min-statistics noise floor (windowed minimum of frame power over
	//   a 3 s history) with asymmetric hysteresis:
	//     open  at E > 25*Fmin   (quiet speech sits 25x+ above the deep
	//                             minimum and must stay open)
	//     close at E <  4*Fmin   (~the mean noise power)
	//   Closed frames are muted to true digital silence BEFORE the
	//   analysis.  Synthesised partial gain steps on the SA parameters
	//   ring the predictive log2-DCT quantizer (bounded gain tables),
	//   but the codec encodes real zero input perfectly: a full mute
	//   lands on the decoder's idle floor within one frame and word
	//   onsets re-open instantly (measured on speech/silence/speech
	//   splices).  Pitch tracking also sees the muted signal, which is
	//   harmless: pause pitch is discarded with the pause energy.
	//=====================================================================
	if (enh)
	{
		Word32 eth = 0;
		Word32 ng_floor = MAX_32;
		Word16 ratio;
		static Word16 gated[FRAME];

		for(i = 0; i < FRAME; i++)
			eth = L_mac(eth, snd[i], snd[i]);
		ng_hist[ng_idx] = eth;
		ng_idx = (ng_idx + 1 == 150) ? 0 : ng_idx + 1;
		for(i = 0; i < 150; i++)
			if(ng_hist[i] < ng_floor)
				ng_floor = ng_hist[i];

		if(ng_floor > 4 && eth > 8)
		{
			i = norm_l(eth);
			ratio = div_s(extract_h(L_shl(ng_floor, i)),
			              extract_h(L_shl(eth, i)));
			if(!ng_open && ratio < 0x0524)      /* 0.040: E > 25*Fmin */
				ng_open = 1;
			else if(ng_open && ratio > 0x2000)  /* 0.250: E <  4*Fmin */
				ng_open = 0;
		}

#ifdef ENC_DIAG
		fprintf(stderr, "NG %d eth=%ld floor=%ld open=%d\n", frame_cnt,
		        (long)eth, (long)ng_floor, ng_open);
#endif

		if(!ng_open)
		{
			for(i = 0; i < FRAME; i++)
				gated[i] = 0;
			snd = gated;
		}
	}

	for(i = 0; i < PITCH_EST_BUF_SIZE - FRAME; i++)
	{
		pitch_est_buf[i] = pitch_est_buf[i + FRAME];
		pitch_ref_buf[i] = pitch_ref_buf[i + FRAME];
	}
		
	dc_rmv(snd, &pitch_ref_buf[PITCH_EST_BUF_SIZE - FRAME], &dc_rmv_mem, FRAME);
	pe_lpf(&pitch_ref_buf[PITCH_EST_BUF_SIZE - FRAME], &pitch_est_buf[PITCH_EST_BUF_SIZE - FRAME], pe_lpf_mem, FRAME);

	pitch_est(imbe_param, pitch_est_buf);

    //
	// Speech windowing and FFT calculation
	//
	wr_ptr  = (Word16 *)wr;
	sig_ptr = &pitch_ref_buf[40];
	for(i = 146; i < 256; i++) 
	{
		fft_buf[i].re = mult(*sig_ptr++, *wr_ptr++); 
		fft_buf[i].im = 0;
	}
	fft_buf[0].re = *sig_ptr++;
	fft_buf[0].im = 0;
	wr_ptr--;
	for(i = 1; i < 111; i++) 
	{
		fft_buf[i].re = mult(*sig_ptr++, *wr_ptr--); 
		fft_buf[i].im = 0;
	}
	for(i = 111; i < 146; i++) 
		fft_buf[i].re = fft_buf[i].im = 0;

	fft(wr_array, wi_array, (Word16 *)&fft_buf, FFTLENGTH, 1);

	pitch_ref(imbe_param, fft_buf);
	v_uv_det(imbe_param, fft_buf);
	sa_encode(imbe_param);
	encode_frame_vector(imbe_param, frame_vector);
}
