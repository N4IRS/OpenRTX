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
#include "aux_sub.h"
#include "math_sub.h"
#include "dsp_sub.h"
#include "tbls.h"
#include "pitch_ref.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>


#define PITCH_REF_FRAME  221
#define MIN_INDEX         50 

#ifdef PREF_COUNT
#include <stdio.h>
unsigned long pref_cnt_outer = 0, pref_cnt_inner1 = 0, pref_cnt_mac = 0;
unsigned long pref_cnt_inner2 = 0, pref_cnt_lsum = 0, pref_cnt_frames = 0;
void pref_dump(void)
{
    if (pref_cnt_frames)
    {
        fprintf(stderr, "PREF frames=%lu outer/frame=%lu inner1/frame=%lu mac/frame=%lu inner2/frame=%lu lsum/frame=%lu\n",
                pref_cnt_frames,
                pref_cnt_outer / pref_cnt_frames,
                pref_cnt_inner1 / pref_cnt_frames,
                pref_cnt_mac / pref_cnt_frames,
                pref_cnt_inner2 / pref_cnt_frames,
                pref_cnt_lsum / pref_cnt_frames);
    }
}
#endif




void pitch_ref(IMBE_PARAM *imbe_param, Cmplx16 *fft_buf)
{
	Word16 i, j, index_a_save, pitch_est, tmp, shift, index_wr, up_lim;
	Cmplx16 sp_rec[FFTLENGTH/2];
	Word32 fund_freq, fund_freq_2, fund_freq_acc_a, fund_freq_acc_b, fund_freq_acc, L_tmp, L_diff_min;
	Word16 ha, hb, index_a, index_b, index_tbl[20], it_ind, re_tmp, im_tmp, pitch_cand=0;
	Word32 fund_freq_cand=0;

	/* Pattern-A rewrite of the MAC chains (same technique as the
	   validated autocorr/fft work).  Exactness proofs:

	   - wr_sp[] spans [16, 28165] (all positive, tbls.cc), and
	     |fft_buf[].re/.im| <= 32768, so every product |fft * wr_sp|
	     <= 32768 * 28165 < 2^30: L_mult's 0x40000000 special case is
	     unreachable and t = 2*p fits int32 exactly.  Only the
	     running amp accumulator can leave the 32-bit range; the
	     original chain saturates there (L_add), which is exactly a
	     32-bit add with hardware-overflow detection + clamp
	     (sat_add32 below).  The Overflow flag itself is write-only
	     (L_sat is never called anywhere in the tree).

	   - In the L_sum loop each term is 2*re_tmp^2 / 2*im_tmp^2 with
	     re_tmp/im_tmp 16-bit (saturating sub), i.e. every term is
	     non-negative, and re_tmp == -32768 maps to MAX_32 = 2^31-1
	     (== 2*re^2 - 1).  A saturating accumulator fed only
	     non-negative terms equals min(exact sum, MAX_32).

	   All other arithmetic (L_tmp stepper, fund_freq accumulators,
	   index ceil and bounds guards) is kept with the original
	   saturating operator calls; the code paths and branch
	   structure are unchanged. */

	
	pitch_est = shl(imbe_param->pitch, 7);                      // Convert to Q8.8
	pitch_est = sub(pitch_est, CNST_1_125_Q8_8);                // Sub 1.125 = 9/8

	L_diff_min = MAX_32;
	for(i = 0; i < 19; i++)
	{
		shift = norm_s(pitch_est);
		tmp = shl(pitch_est, shift);
		tmp = div_s(0x4000, tmp);
		fund_freq = L_shl(tmp, shift + 11);

		fund_freq_acc   = fund_freq;
		fund_freq_2     = L_shr(fund_freq, 1);
		fund_freq_acc_a = L_sub(fund_freq, fund_freq_2);
		fund_freq_acc_b = L_add(fund_freq, fund_freq_2);

        // Calculate upper limit for spectrum reconstruction
		up_lim = extract_h(L_shr((UWord32)CNST_0_9254_Q0_16 * pitch_est, 1));  // 0.9254/fund_freq
		up_lim = sub(up_lim, CNST_0_5_Q8_8);                                   // sub 0.5
		up_lim = up_lim & 0xFF00;                                              // extract fixed part
		up_lim = mult(up_lim, extract_h(fund_freq));
		up_lim = shr(up_lim, 1);
				
		index_b = 0;
		while(index_b <= up_lim)
		{
#ifdef PREF_COUNT
			pref_cnt_outer++;
#endif
			ha = extract_h(fund_freq_acc_a);
			hb = extract_h(fund_freq_acc_b);
			index_a = (ha >> 8) + ((ha & 0xFF)?1:0);
			index_b = (hb >> 8) + ((hb & 0xFF)?1:0);

			if(index_b >= MIN_INDEX)
			{				
				L_tmp = L_shl(L_deposit_h(index_a), 8);
				L_tmp = L_sub(L_tmp, fund_freq_acc);
				L_tmp = L_add(L_tmp, 0x00020000);   // for rounding purpose
				L_tmp = L_shr(L_tmp, 2);

				index_a_save = index_a;
				it_ind = 0;

				int32_t amp_re = 0, amp_im = 0;
				while(index_a < index_b)
				{
					#ifdef PREF_COUNT
						pref_cnt_inner1++;
					#endif
					index_wr = extract_h(L_tmp);
					if(index_wr < 0 && (L_tmp & 0xFFFF)) // truncating for negative number
						index_wr++;          /* add(): |L_tmp| < 2^25 (probe: +-2^24.7) so |index_wr| <= 1024 - no 16-bit overflow */
					index_wr += 160;           /* add(): lands in [-864, 1184], fits int16 (probe: [-249, 559]) */
					index_tbl[it_ind++] = index_wr;					
					if(index_wr >= 0 && index_wr <= 320)
					{
						#ifdef PREF_COUNT
							pref_cnt_mac++;
						#endif
						/* L_mac(amp, fft, wr_sp): exact 32-bit product
						   (see proof above) + saturating add */
						Word16 w = wr_sp[index_wr];
						int32_t t = (int32_t)(Word32)fft_buf[index_a].re * (Word32)w;
						if (__builtin_add_overflow(amp_re, t << 1, &amp_re))
							amp_re = (t < 0) ? MIN_32 : MAX_32;
						t = (int32_t)(Word32)fft_buf[index_a].im * (Word32)w;
						if (__builtin_add_overflow(amp_im, t << 1, &amp_im))
							amp_im = (t < 0) ? MIN_32 : MAX_32;
					}
	
					index_a++;
					L_tmp += 0x400000;   /* L_add(): |L_tmp| < 2^25 (probe: +-2^24.7), +12 steps of 2^22 stays < 2^27 */
					}

				/* extract_h once per band: amp_* are finished here and
				   loop-invariant below */
				int32_t amp_re_h = amp_re >> 16;
				int32_t amp_im_h = amp_im >> 16;
				it_ind = 0;
				index_a = index_a_save;	
				while(index_a < index_b)
				{
					#ifdef PREF_COUNT
						pref_cnt_inner2++;
					#endif
					index_wr = index_tbl[it_ind++];
					if(index_wr < 0 || index_wr > 320)
					{
						sp_rec[index_a].im = sp_rec[index_a].re = 0;
					}
					else
					{	
						sp_rec[index_a].im = mult(mult((Word16)amp_im_h, wr_sp[index_wr]), 0x6666);
						sp_rec[index_a].re = mult(mult((Word16)amp_re_h, wr_sp[index_wr]), 0x6666);
					}
					
					index_a++;
					}
			}

			/* fund_freq_acc_* <= 2^28.3 and fund_freq <= 2^28 (probe:
			   acc_b <= 324M), +fund_freq stays < 2^30 - L_add
			   saturation unreachable, plain adds are exact */
			fund_freq_acc_a += fund_freq;
			fund_freq_acc_b += fund_freq;
			fund_freq_acc   += fund_freq;
		}

		/* Non-negative saturating accumulation (see proof above):
		   exact 64-bit sum, clamped to MAX_32.  r == -32768 is the
		   L_mult special case: contributes 2^31-1 == 2*r*r - 1. */
		uint64_t usum = 0;
		for(j = MIN_INDEX; j <= up_lim; j++)
		{
#ifdef PREF_COUNT
			pref_cnt_lsum++;
#endif
			re_tmp = sub(fft_buf[j].re, sp_rec[j].re);
			im_tmp = sub(fft_buf[j].im, sp_rec[j].im);
			uint32_t r2 = (uint32_t)((int32_t)re_tmp * re_tmp) * 2u - (re_tmp == (Word16)-32768);
			uint32_t i2 = (uint32_t)((int32_t)im_tmp * im_tmp) * 2u - (im_tmp == (Word16)-32768);
			usum += r2 + i2;
		}
		Word32 L_sum = (usum >= (uint64_t)MAX_32) ? MAX_32 : (Word32)usum;

		if(L_sum < L_diff_min)
		{
			L_diff_min     = L_sum;
			pitch_cand     = pitch_est;				
			fund_freq_cand = fund_freq;
		}
		
		pitch_est = add(pitch_est,  CNST_0_125_Q8_8); // Add 0.125 = 1/8
	}

	imbe_param->ref_pitch = pitch_cand;	
	imbe_param->fund_freq = fund_freq_cand;
#ifdef PREF_COUNT
	pref_cnt_frames++;
#endif
}










