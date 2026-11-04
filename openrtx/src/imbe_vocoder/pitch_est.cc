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


#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "typedef.h"
#include "globals.h"
#include "imbe.h"
#include "basic_op.h"
#include "aux_sub.h"
#include "math_sub.h"
#include "tbls.h"
#include "pitch_est.h"
#include "encode.h"
#include "dsp_sub.h"
#include "imbe_vocoder.h"

#ifdef ENC_DIAG
#include <stdio.h>
extern "C" int enc_diag_frame;
#define frame_cnt enc_diag_frame
#endif


#define CNST_0_75_Q1_15 0x6000
/* leash: tracking may only prefer continuity among candidates
   whose e_p is within ENH_LEASH (Q4.12) of the raw minimum, bounding
   the spectral-fit sacrifice */
#ifndef ENH_LEASH
#define ENH_LEASH 100   /* 0.024 in Q4.12 */
#endif

static const UWord16 min_max_tbl[203] = 
{
	0x0008, 0x0009, 0x000a, 0x000c, 0x000d, 0x000e, 0x000f, 0x0010, 0x0012, 0x0013, 
	0x0014, 0x0115, 0x0216, 0x0218, 0x0319, 0x041a, 0x051b, 0x061c, 0x061e, 0x071f, 
	0x0820, 0x0921, 0x0a22, 0x0a24, 0x0b25, 0x0c26, 0x0d27, 0x0e28, 0x0e2a, 0x0f2b, 
	0x102c, 0x112d, 0x122e, 0x1230, 0x1331, 0x1432, 0x1533, 0x1634, 0x1636, 0x1737, 
	0x1838, 0x1939, 0x1a3a, 0x1a3c, 0x1b3d, 0x1c3e, 0x1d3f, 0x1e40, 0x1e42, 0x1f43, 
	0x2044, 0x2145, 0x2246, 0x2248, 0x2349, 0x244a, 0x254b, 0x264c, 0x264e, 0x274f, 
	0x2850, 0x2951, 0x2a52, 0x2a54, 0x2b55, 0x2c56, 0x2d57, 0x2e58, 0x2e5a, 0x2f5b, 
	0x305c, 0x315d, 0x325e, 0x3260, 0x3361, 0x3462, 0x3563, 0x3664, 0x3666, 0x3767, 
	0x3868, 0x3969, 0x3a6a, 0x3a6c, 0x3b6d, 0x3c6e, 0x3d6f, 0x3e70, 0x3e72, 0x3f73, 
	0x4074, 0x4175, 0x4276, 0x4278, 0x4379, 0x447a, 0x457b, 0x467c, 0x467e, 0x477f, 
	0x4880, 0x4981, 0x4a82, 0x4a84, 0x4b85, 0x4c86, 0x4d87, 0x4e88, 0x4e8a, 0x4f8b, 
	0x508c, 0x518d, 0x528e, 0x5290, 0x5391, 0x5492, 0x5593, 0x5694, 0x5696, 0x5797, 
	0x5898, 0x5999, 0x5a9a, 0x5a9c, 0x5b9d, 0x5c9e, 0x5d9f, 0x5ea0, 0x5ea2, 0x5fa3, 
	0x60a4, 0x61a5, 0x62a6, 0x62a8, 0x63a9, 0x64aa, 0x65ab, 0x66ac, 0x66ae, 0x67af, 
	0x68b0, 0x69b1, 0x6ab2, 0x6ab4, 0x6bb5, 0x6cb6, 0x6db7, 0x6eb8, 0x6eba, 0x6fbb, 
	0x70bc, 0x71bd, 0x72be, 0x72c0, 0x73c1, 0x74c2, 0x75c3, 0x76c4, 0x76c6, 0x77c7, 
	0x78c8, 0x79c9, 0x7aca, 0x7aca, 0x7bca, 0x7cca, 0x7dca, 0x7eca, 0x7eca, 0x7fca, 
	0x80ca, 0x81ca, 0x82ca, 0x82ca, 0x83ca, 0x84ca, 0x85ca, 0x86ca, 0x86ca, 0x87ca, 
	0x88ca, 0x89ca, 0x8aca, 0x8aca, 0x8bca, 0x8cca, 0x8dca, 0x8eca, 0x8eca, 0x8fca, 
	0x90ca, 0x91ca, 0x92ca, 0x92ca, 0x93ca, 0x94ca, 0x95ca, 0x96ca, 0x96ca, 0x97ca, 
	0x98ca, 0x99ca, 0x9aca
};



void imbe_encoder::pitch_est_init(void)
{
	prev_pitch = prev_prev_pitch = 158; // 100
	prev_e_p = prev_prev_e_p = 0;
	e_p_cache_valid[0] = e_p_cache_valid[1] = e_p_cache_valid[2] = 0;

	/* Pre-warm the e_p cache so the FIRST frame does not pay three
	   full e_p() calls.  At init the pitch buffer is all zeros (both
	   the constructor and encode_init() zero it before this runs),
	   and the first frame's buffer slide maps the zero windows
	   starting at FRAME and 2*FRAME onto exactly this frame's arr0
	   and arr1 windows; the cache slide at the top of pitch_est()
	   then moves cache[1]->cache[0] and cache[2]->cache[1].  Same
	   pure function over the same (zero) input, so the cached
	   results are bit-identical to computing them lazily - the
	   frame-0 golden bytes do not change.  Cost moves to init (once
	   per stream) instead of frame 0. */
	e_p(&pitch_est_buf[FRAME], e_p_cache[1]);
	e_p_cache_valid[1] = 1;
	e_p(&pitch_est_buf[2 * FRAME], e_p_cache[2]);
	e_p_cache_valid[2] = 1;
}



/* Exact reproducer of the original saturating autocorrelation chain
       L_sum = L_add(L_sum, L_shr(L_mult(a[i], b[i]), scale_shift))
   term by term.  Kept as the bit-exact fallback for the rare case
   where the true sum leaves the 32-bit range (see autocorr). */
Word32 imbe_encoder::autocorr_slow(const Word16 *sigin, Word16 shift, Word16 scale_shift)
{
    const Word16 *a = sigin;
    const Word16 *b = sigin + shift;
    const int n = PITCH_EST_FRAME - shift;
    const int ss = scale_shift;

    Word32 s = 0;
    for (int i = 0; i < n; i++)
    {
        Word32 p = (Word32)(int16_t)a[i] * (int16_t)b[i];
        Word32 t = p << 1;
        if (t == MIN_32)          /* p == 0x40000000: L_mult special case */
        {
            t = MAX_32;
            Overflow = 1;
        }
        if (ss > 0)
            t >>= ss;             /* L_shr(t, ss) */

        Word32 v = s + t;
        if ((((s ^ t) & MIN_32) == 0) && (((v ^ s) & MIN_32) != 0))
        {
            v = (s < 0) ? MIN_32 : MAX_32;
            Overflow = 1;
        }
        s = v;
    }
    return s;
}

Word32 imbe_encoder::autocorr(Word16 *sigin, Word16 shift, Word16 scale_shift)
{
    /* Fast path.  In production the input is the mult_r()-windowed
       signal, and with the fixed wi table (max 17779) every sample
       satisfies |sig_wndwed[i]| <= 17779 < 32768, so:
         - L_mult's 0x40000000 special case is unreachable
           (needs a == b == -32768),
         - every product p = a*b fits in int32 (|p| <= 17779^2),
           2p fits in int32, and 2p >> ss fits in int32.
       The only remaining hazard is the running sum leaving the
       32-bit range, where the original L_add chain saturates.  Until
       the first such event the chain equals the exact integer sum, so
       accumulate exactly in 64 bits (no per-term overflow test) and
       check the range once per term; on the first crossing replay the
       lag with the exact sat chain.  Partial sums span at most
       280 * 2^29 < 2^35, well inside 64 bits.  On real speech a
       crossing lag is rare (~3%); on full-scale adversarial input the
       cost degrades to the original, never the result. */
    const Word16 *a = sigin;
    const Word16 *b = sigin + shift;
    const int n = PITCH_EST_FRAME - shift;
    const int ss = scale_shift;

    /* Exact 32-bit accumulation with hardware-overflow crossing
       detection (Pattern A): until the first partial sum leaves
       [-2^31, 2^31) the chain of L_adds equals the exact integer sum,
       and the first escape is exactly a 32-bit signed overflow of the
       running sum - so __builtin_add_overflow replaces the 64-bit
       accumulate+range-compare (add+jo on x86-64, adds+bvs on M4).
       2p>>ss is folded to p>>(ss-1) (exact: floor(2p/2^ss) ==
       floor(p/2^(ss-1))), with the loop split on the constant shift so
       immediate shifts are emitted.  |a[i]|,|b[i]| <= 17779 (mult_r
       window bound), so p and 2p fit int32 and L_mult's -32768^2
       special case is unreachable.  First crossing replays the lag
       with the exact saturating chain (autocorr_slow). */
    int32_t T = 0;
    if (ss == 0)
    {
#if defined(__clang__)
#pragma clang loop unroll_count(8)
#else
#pragma GCC unroll 8
#endif
        for (int i = 0; i < n; i++)
        {
            int32_t p = (int32_t)(int16_t)a[i] * (int16_t)b[i];
            if (__builtin_add_overflow(T, p << 1, &T))
                return autocorr_slow(a, shift, (Word16)ss);
        }
    }
    else
    {
#if defined(__clang__)
#pragma clang loop unroll_count(8)
#else
#pragma GCC unroll 8
#endif
        for (int i = 0; i < n; i++)
        {
            int32_t p = (int32_t)(int16_t)a[i] * (int16_t)b[i];
            if (__builtin_add_overflow(T, p >> (ss - 1), &T))
                return autocorr_slow(a, shift, (Word16)ss);
        }
    }
    return (Word32)T;
}



/* Exact raw equivalents of the L_add / L_mpy_ls / mult_r primitives
   (basicop2.cc, math_sub.cc): 32-bit wrap arithmetic plus the XOR sign
   test, clamping to MAX_32/MIN_32 and setting Overflow exactly like
   the originals.  Inlined into e_p() below to kill the per-term call
   overhead on the M4. */
static inline Word32 raw_l_add(Word32 a, Word32 b)
{
	Word32 v = a + b;
	if ((((a ^ b) & MIN_32) == 0) && (((v ^ a) & MIN_32) != 0))
	{
		v = (a < 0) ? MIN_32 : MAX_32;
		Overflow = 1;
	}
	return v;
}

static inline Word32 raw_l_sub(Word32 a, Word32 b)
{
	Word32 v = a - b;
	if ((((a ^ b) & MIN_32) != 0) && (((v ^ a) & MIN_32) != 0))
	{
		v = (a < 0) ? MIN_32 : MAX_32;
		Overflow = 1;
	}
	return v;
}

static inline Word16 raw_mult_r(Word16 a, Word16 b)
{
	Word32 p = (Word32)a * (Word32)b + 0x00004000L;
	return (Word16)((p & (Word32)0xFFFF8000L) >> 15);
}

/* L_mpy_ls(a, b) = L_mac(L_shr(L_mult(b, swtemp), 15), b, extract_h(a)),
   swtemp = (extract_l(a) shr 1) & 0x7fff.  |b*swtemp*2| < 2^31 and
   |b*hi*2| < 2^31, so both L_mults are exact; the two parts sum to at
   most 32767 + (2^31 - 2^16) < 2^31, so the final L_add (inside
   L_mac) can never saturate either - raw_l_add is exact here. */
static inline Word32 raw_l_mpy_ls(Word32 a, Word16 b)
{
	Word16 lo = (Word16)a;
	Word16 sw = (Word16)(((unsigned short)(Word16)(lo >> 1)) & 0x7FFFu);
	Word16 hi = (Word16)(a >> 16);
	Word32 p = (Word32)b * (Word32)sw * 2;
	return raw_l_add((p >> 15), (Word32)b * (Word32)hi * 2);
}

/* sum(s^2 * wi): the 301 L_mpy_ls terms in a 64-bit accumulator.
   wi[] spans [15, 17779] (all positive, tbls.cc) and a = 2*x^2
   (>> rshift) >= 0, so every term is non-negative and the partial
   sums are monotone: the exact 64-bit sum equals the original
   saturating L_add chain whenever the FINAL value is inside int32 -
   one final range check replaces the per-term crossing test.  The
   L_mpy_ls internals cannot saturate (proof at raw_l_mpy_ls), so the
   raw product form is computed directly in 64 bits.  x == -32768 is
   the L_mult special case and contributes MAX_32 = 2^31-1; the exact
   sat-chain replay remains for an out-of-range final sum. */
static Word32 ep_energy_sum(Word16 *sigin, Word16 rshift)
{
	int64_t T = 0;
	int special = 0;
	for (int i = 0; i < PITCH_EST_FRAME; i++)
	{
		Word16 x = sigin[i];
		Word32 a;
		if (x == -32768)            /* L_mult special case */
		{
			a = MAX_32;
			special = 1;
		}
		else
			a = (Word32)(int32_t)x * x * 2;
		if (rshift > 0)
			a >>= rshift;
		/* raw_l_mpy_ls(a, wi[i]) without the dead saturation test */
		Word16 b = wi[i];
		Word16 lo = (Word16)a;
		Word16 sw = (Word16)(((uint16_t)(Word16)(lo >> 1)) & 0x7FFFu);
		Word16 hi = (Word16)(a >> 16);
		T += (int64_t)(((Word32)b * (Word32)sw * 2) >> 15)
		   + (int64_t)(Word32)b * (Word32)hi * 2;
	}
	if (special)
		Overflow = 1;
	if (T <= MAX_32 && T >= MIN_32)
		return (Word32)T;
	/* exact sat-chain replay */
	Word32 s = 0;
	for (int i = 0; i < PITCH_EST_FRAME; i++)
	{
		Word16 x = sigin[i];
		Word32 a;
		if (x == -32768)
		{
			a = MAX_32;
			Overflow = 1;
		}
		else
			a = (Word32)x * x * 2;
		if (rshift > 0)
			a >>= rshift;
		s = raw_l_add(s, raw_l_mpy_ls(a, wi[i]));
	}
	return s;
}

void imbe_encoder::e_p(Word16 *sigin, Word16 *res_buf)
{
	Word16 i, j, den_part_acc, tmp;
	Word32 L_sum, L_num, L_den, L_e0, L_tmp;
	Word16 sig_wndwed[PITCH_EST_FRAME];
	Word32 corr[259];
	Word16 index_beg, index_step;
	Word16 scale_shift;

	// Windowing input signal s * wi
	for(i = 0 ; i < PITCH_EST_FRAME; i++)
		sig_wndwed[i] = raw_mult_r(sigin[i], wi[i]);

	// sum(s^2 * wi^2): 64-bit exact accumulation (see ep_energy_sum)
	L_sum = ep_energy_sum(sigin, 0);

	// Check for the overflow
	if(L_sum == MAX_32)
	{
		// Recalculate with scaling
		L_sum = ep_energy_sum(sigin, 5);
		scale_shift = 5;
	}
	else
		scale_shift = 0;

	// sum(s^2 * wi^4): raw form of L_add(L_e0, L_shr(L_mult(w,w), ss)).
	// sig_wndwed values satisfy |w| <= 17779 (mult_r output bound), so
	// the L_mult special case is unreachable and 2w^2 fits in int32;
	// terms are non-negative so the partial sums are monotone - exact
	// 64-bit accumulation with a single final range check (the
	// sat-chain replay covers an out-of-range result).
	{
		int64_t T = 0;
		for(i = 0 ; i < PITCH_EST_FRAME; i++)
		{
			int32_t y = (int32_t)(int16_t)sig_wndwed[i];
			int32_t a = y * y * 2;
			if (scale_shift > 0)
				a >>= scale_shift;
			T += a;
		}
		if (T <= MAX_32 && T >= MIN_32)
		{
			L_e0 = (Word32)T;
		}
		else
		{
			/* exact sat-chain replay */
			L_e0 = 0;
			for(i = 0 ; i < PITCH_EST_FRAME; i++)
			{
				Word16 y = sig_wndwed[i];
				Word32 a = (Word32)(int32_t)y * y * 2;
				if (scale_shift > 0)
					a >>= scale_shift;
				L_e0 = raw_l_add(L_e0, a);
			}
		}
	}

    // Calculate correlation for time shift in range 21...150 with step 0.5
	// For integer shifts
	for(tmp = 21, i = 0; tmp <= 150; tmp++, i += 2)
		corr[i] = autocorr(sig_wndwed, tmp, scale_shift);
	// For intermediate shifts (raw L_shr(L_add(a,b), 1))
	for(i = 1; i < 258; i += 2)
		corr[i] = raw_l_add(corr[i - 1], corr[i + 1]) >> 1;


	// variable to calculate 1 - P * sum(wi ^4) in denominator
	den_part_acc = CNST_0_8717_Q1_15;

	index_step = 42;               // Note: 42 = 21 in Q15.1 format, so index_step will be used also as p in Q15.1 format
	index_beg  = 0;
	L_e0 = L_shr(L_e0, 7);         // divide by 64 to compensate wi scaling
	// p = 21...122 by step 0.5
	for(i = 0; i < 203; i++)
	{
		// Calculate sum( corr ( n * p) ): 64-bit exact accumulation
		// (<= 7 terms, |corr| <= 2^31) with sat-chain replay on crossing.
		int64_t T = 0;
		int crossed = 0;
		j = index_beg;
		while(j <= 258)
		{
			T += corr[j];
			if (!crossed && (T > MAX_32 || T < MIN_32))
				crossed = 1;
			j += index_step;
		}
		int64_t L_tmp64;
		if (!crossed)
			L_tmp64 = T;
		else
		{
			Word32 s = 0;
			j = index_beg;
			while(j <= 258)
			{
				s = raw_l_add(s, corr[j]);
				j += index_step;
			}
			L_tmp64 = (int32_t)s;
		}

		// L_shr 6 + L_add(L_e0): L_e0 >= 0 and T/64 + L_e0 can only
		// overflow at the top, so clamp there (bit-exact).
		L_tmp64 = (L_tmp64 >> 6) + (int64_t)(int32_t)L_e0;
		if (L_tmp64 > MAX_32)
		{
			L_tmp64 = MAX_32;
			Overflow = 1;
		}
		L_tmp = (Word32)(L_tmp64 * index_step);   // plain 32-bit wrap multiply (as original)
		// L_sub(L_sum, L_tmp): L_sum >= 0, only the top end is reachable
		{
			int64_t d = (int64_t)(int32_t)L_sum - (int32_t)L_tmp;
			if (d > MAX_32)
			{
				L_num = MAX_32;
				Overflow = 1;
			}
			else
				L_num = (Word32)d;
		}

		index_beg++;
		index_step++;
		
		L_den = raw_l_mpy_ls(L_sum, den_part_acc);

		if(L_num < L_den && L_den != 0)
		{
			//res_buf[i] = (Word16)((double)L_num/(double)L_den * 4096.);  // Q4.12

			if(L_num <= 0)
				res_buf[i] = 0;
			else
			{
				tmp = norm_l(L_den);
				tmp = div_s(extract_h(L_shl(L_num, tmp)), extract_h(L_shl(L_den, tmp)));
				res_buf[i] = shr(tmp, 3);  // convert to Q4.12
			}
		}
		else if(L_num >= L_den)
		{
			res_buf[i] = CNST_1_00_Q4_12;
			//res_buf[i] = (Word16)((double)L_num/(double)L_den * 4096.);  // Q4.12
		}
		else 
			res_buf[i] = CNST_1_00_Q4_12;

		den_part_acc = (Word16)(den_part_acc - CNST_0_0031_Q1_15);
	}
}



/* Monotone deque sliding-window minimum of a[] over the windows
   [HI(min_max_tbl[k]), LO(min_max_tbl[k])] (k = 0..202).  The table's
   endpoints are non-decreasing, so the deque's insertion pointer only
   moves forward (candidates up to the current window's right edge are
   inserted as the edge advances) and each window's minimum is read off
   the head in amortized O(1) with the identical value: a min is
   order-independent, so the original first-wins tie handling can only
   pick between equal values. */
static void window_min_203(const Word16 *a, Word16 *out)
{
	static Word16 dq[203];
	int head = 0, tail = 0, ins = 0;
	for (int k = 0; k < 203; k++)
	{
		int hi = HI_BYTE(min_max_tbl[k]);
		int lo = LO_BYTE(min_max_tbl[k]);
		while (ins <= lo)           /* extend the deque to the window's right edge */
		{
			while (head < tail && a[dq[tail - 1]] >= a[ins])
				tail--;
			dq[tail++] = (Word16)ins;
			ins++;
		}
		while (head < tail && dq[head] < hi)
			head++;
		out[k] = a[dq[head]];
	}
}



void imbe_encoder::pitch_est(IMBE_PARAM *imbe_param, Word16 *frames_buf)
{
	Word16 e_p_arr0[203], e_p_arr1[203], e_p_arr2[203], e1p1_e2p2_est_save[203];
	Word16 min_index, max_index, p, i, p_index;
	UWord16 tmp=0, p_fp;
	UWord32 UL_tmp;
	Word16 e_p_cur, pb, pf, ceb;
	Word16 cef_est, cef, p0_est, p0;
        Word16 e_p_arr2_min[203];

	// Slide the e_p() result cache: what were look-ahead windows are
	// now the current/look-ahead windows of this frame.  A 3-way
	// rotate is NOT needed: slot 2 is invalidated below and always
	// recomputed before use, so writing the old slot-0 data into it
	// was dead.
	{
		memcpy(e_p_cache[0], e_p_cache[1], sizeof(e_p_cache[0]));
		memcpy(e_p_cache[1], e_p_cache[2], sizeof(e_p_cache[0]));
		e_p_cache_valid[0] = e_p_cache_valid[1];
		e_p_cache_valid[1] = e_p_cache_valid[2];
		e_p_cache_valid[2] = 0;
	}

	// Calculate E(p) function for current and two future frames.
	// The look-ahead windows are computed on EVERY frame (even when the
	// look-back decision is about to fire): the next frame's
	// current/look-ahead windows are exactly this frame's look-ahead
	// windows, so every frame pays for exactly one new e_p() instead of
	// bursts of two or three on look-back-to-look-ahead transitions.
	// Same pure function, same input window - bit-identical results.
	if (e_p_cache_valid[0])
		memcpy(e_p_arr0, e_p_cache[0], sizeof(e_p_arr0));
	else
	{
		e_p(&frames_buf[0], e_p_arr0);
		memcpy(e_p_cache[0], e_p_arr0, sizeof(e_p_arr0));
		e_p_cache_valid[0] = 1;
	}
	if (e_p_cache_valid[1])
		memcpy(e_p_arr1, e_p_cache[1], sizeof(e_p_arr1));
	else
	{
		e_p(&frames_buf[FRAME], e_p_arr1);
		memcpy(e_p_cache[1], e_p_arr1, sizeof(e_p_arr1));
		e_p_cache_valid[1] = 1;
	}
	if (!e_p_cache_valid[2])
	{
		e_p(&frames_buf[2 * FRAME], e_p_arr2);
		memcpy(e_p_cache[2], e_p_arr2, sizeof(e_p_arr2));
		e_p_cache_valid[2] = 1;
	}
	else
		memcpy(e_p_arr2, e_p_cache[2], sizeof(e_p_arr2));

	// Look-Back Pitch Tracking
	min_index = HI_BYTE(min_max_tbl[prev_pitch]);
	max_index = LO_BYTE(min_max_tbl[prev_pitch]);

	p = pb = min_index;
	if (enh)
	/* leashed continuity look-back (expired-patent soft-decision
	   tracking): among candidates whose e_p is within ENH_LEASH of the
	   raw minimum (fit sacrifice bounded), pick the one closest to the
	   previous pitch */
	{
		Word16 e_raw = e_p_arr0[min_index];
		Word32 L_best;
		while (++p <= max_index)
			if (e_p_arr0[p] < e_raw)
				e_raw = e_p_arr0[p];
		p = min_index;
		pb = -1;
		L_best = 0x7FFFFFFF;
		while (p <= max_index)
		{
			if (e_p_arr0[p] <= e_raw + ENH_LEASH)
			{
				Word32 L_c = (Word32)labs(p - prev_pitch);
				if (L_c < L_best)
				{
					L_best = L_c;
					pb = p;
				}
			}
			p++;
		}
		if (pb < 0)
			pb = min_index;
		e_p_cur = e_p_arr0[pb];
	}
	else
	{
		e_p_cur = e_p_arr0[min_index];
		while(++p <= max_index)
			if(e_p_arr0[p] < e_p_cur)
			{
				e_p_cur = e_p_arr0[p];
				pb = p;
			}
	}
	ceb = add(e_p_cur, add(prev_e_p, prev_prev_e_p));


	if(ceb <= CNST_0_48_Q4_12)
	{
		prev_prev_pitch = prev_pitch;
		prev_pitch      = pb;
		prev_prev_e_p   = prev_e_p;
		prev_e_p        = e_p_arr0[pb];

		imbe_param->pitch = pb + 42;  // Result in Q15.1 format
		imbe_param->e_p = prev_e_p;
#ifdef ENC_DIAG
		fprintf(stderr, "PE %d LB pb=%d ceb=%d.%04d prev=%d\n", frame_cnt,
		        pb, ceb*10000/4096, (ceb*10000/4096*4096-ceb*10000)%10000, prev_pitch);
#endif
		return;
	}


	// Look-Ahead Pitch Tracking

	p0_est = p0 = 0;
	cef_est = e_p_arr0[p0] + e_p_arr1[p0] + e_p_arr2[p0];

	// min of e_p_arr2 over each pitch neighbourhood: sliding-window
	// minima (the window endpoints are monotone - see window_min_203).
	window_min_203(e_p_arr2, e_p_arr2_min);

	// min of add(e_p_arr1[p1], e_p_arr2_min[p1]) over the same windows,
	// per p0.  Precompute the 203 saturating sums, then the second
	// window-min pass.  (The original nested loops evaluate the same
	// values in a different order - a min is order-independent.)
	{
		Word16 sums[203];
		for (p0 = 0; p0 < 203; p0++)
			sums[p0] = add(e_p_arr1[p0], e_p_arr2_min[p0]);
		window_min_203(sums, e1p1_e2p2_est_save);
	}
	for (p0 = 0; p0 < 203; p0++)
	{
		cef = add(e_p_arr0[p0], e1p1_e2p2_est_save[p0]);
		if (cef < cef_est)
		{
			cef_est = cef;
			p0_est  = p0;
		}
	}

	if (enh)
	/* leashed continuity on the 3-window look-ahead (exact two-pass
	   over the saved costs): among candidates whose cost is within
	   ENH_LEASH of the raw minimum, prefer the pitch nearest the
	   previous frame's - bounds the fit sacrifice while killing jitter
	   between near-equal candidates */
	{
		Word16 cef_raw = e_p_arr0[0] + e1p1_e2p2_est_save[0];
		Word32 L_nearbest = 0x7FFFFFFF;
		Word16 p0_near = 0;
		for (p0 = 1; p0 < 203; p0++)
		{
			Word16 c = add(e_p_arr0[p0], e1p1_e2p2_est_save[p0]);
			if (c < cef_raw)
				cef_raw = c;
		}
		for (p0 = 0; p0 < 203; p0++)
		{
			if (add(e_p_arr0[p0], e1p1_e2p2_est_save[p0]) <= cef_raw + ENH_LEASH)
			{
				Word32 L_c = (Word32)labs(p0 - prev_pitch);
				if (L_c < L_nearbest)
				{
					L_nearbest = L_c;
					p0_near = p0;
				}
			}
		}
		p0_est = p0_near;
	}
	pf = p0_est;
	// Sub-multiples analysis
	if(pf >= 42)  // Check if Sub-multiples are possible
	{	
		if(pf < 84)
			i = 1;
		else if(pf < 126)
			i = 2;
		else if(pf < 168)
			i = 3;
		else
			i = 4;

		p_fp = (pf + 42) << 8; // Convert pitch estimation from array index to unsigned Q7.19 format

		while(i--)
		{
			switch(i)
			{
				case 0:
					tmp = p_fp >> 1;                     // P_est/2
				break;

				case 1:
					UL_tmp = (UWord32)p_fp * 0x5555;     // P_est/3
					tmp = UL_tmp >> 16;
				break;

				case 2:
					tmp = p_fp >> 2;                     // P_est/4
				break;

				case 3:
					UL_tmp = (UWord32)p_fp * 0x3333;     // P_est/5
					tmp = UL_tmp >> 16;
				break;
			}

			p_index = ((tmp + 0x0080) >> 8) - 42;       // Convert fixed-point pitch value to integer array index with rounding

			cef = add(e_p_arr0[p_index], e1p1_e2p2_est_save[p_index]);

			if(cef <= CNST_0_85_Q4_12 && mult_r(cef, enh ? CNST_0_75_Q1_15 : CNST_0_5882_Q1_15) <= cef_est)  // sub-multiple switch needs strong evidence when enhanced
			{
				pf = p_index;
				break;
			}

			if(cef <= CNST_0_4_Q4_12 && mult_r(cef, CNST_0_2857_Q1_15) <= cef_est)   // 1/3.5 = 0.2857
			{
				pf = p_index;
				break;
			}

			if(cef <= CNST_0_05_Q4_12)
			{
				pf = p_index;
				break;
			}	
		}
	}

	cef = add(e_p_arr0[pf], e1p1_e2p2_est_save[pf]);

	if(ceb <= cef)
		p = pb;
	else    
		p = pf; 

	prev_prev_pitch = prev_pitch;
	prev_pitch      = p;
	prev_prev_e_p   = prev_e_p;
	prev_e_p        = e_p_arr0[p];

#ifdef ENC_DIAG
	fprintf(stderr, "PE %d LA p=%d ceb=%d cef=%d pf0=%d prev=%d sub=%d\n",
	        frame_cnt, p, ceb, cef, p0_est, prev_pitch, (p != p0_est));
#endif

	imbe_param->pitch = p + 42;  // Result in Q15.1 format
	imbe_param->e_p = prev_e_p; 
}
