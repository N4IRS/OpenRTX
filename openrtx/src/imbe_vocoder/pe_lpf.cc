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
#include <stdint.h>
#include "globals.h"
#include "imbe.h"
#include "aux_sub.h"
#include "basic_op.h"
#include "math_sub.h"
#include "pe_lpf.h"



static const Word16 lpf_coef[PE_LPF_ORD] =
{
	 -94,  -92,  185,   543,  288, -883, -1834, -495, 3891, 9141, 11512,        
	9141, 3891, -495, -1834, -883,  288,   543,  185,  -92,  -94
};


//-----------------------------------------------------------------------------
//	PURPOSE:
//		Low-pass filter for pitch estimator
//     
//
//  INPUT:
//		*sigin  - pointer to input signal buffer
//      *sigout - pointer to output signal buffer
//      *mem    - pointer to filter's memory element
//       len    - number of input signal samples
//
//	OUTPUT:
//		None
//
//	RETURN:
//       Saved filter state in mem
//
//-----------------------------------------------------------------------------
void pe_lpf(Word16 *sigin, Word16 *sigout, Word16 *mem, Word16 len)
{
	Word16 i;

	/* Bit-exact inlining of
	       L_sum = L_mac(L_sum, mem[i], lpf_coef[i]);  out = round(L_sum).
	   Each term 2*mem*coef is exact int32 (|coef| <= 11512, so the
	   L_mult 0x40000000 special case is impossible), so the chain is
	   plain repeated L_add — reproduced with the 32-bit XOR saturation
	   test (same as the original L_add in basicop2.cc).  All 32-bit:
	   one muls + a few ALU ops per coefficient on the M4.

	   Frame-linear form: instead of shifting the 21-sample memory
	   once per output sample (20 moves x 160 samples), the previous
	   state mem[0..19] (= x_{-20}..x_{-1}, oldest first) and the
	   frame are laid out consecutively in W, so output j is
	       sum_{i=0..20} coef[i] * W[j+i]           (W[j+20] = x_j)
	   — the identical coefficient order and the identical values as
	   the shifting form, at the cost of one 360-byte copy per frame.
	   The new state is W[len-1 .. len+19] (the last 21 samples). */
	Word16 W[PE_LPF_ORD - 1 + 160];

	/* The shifting form discards mem[0] before the first MAC of each
	   sample, so the frame's effective history is mem[1..ORD-1] */
	for(i = 1; i < PE_LPF_ORD; i++)
		W[i - 1] = mem[i];
	for(i = 0; i < len; i++)
		W[PE_LPF_ORD - 1 + i] = sigin[i];

	for(i = 0; i < len; i++)
	{
		const Word16 *w = &W[i];

		/* Terms are exact int32 (|2*x*coef| <= 754M); the exact sum
		   of 21 terms can reach ~3.2G > 2^31, so saturation is
		   reachable in theory — but until the first crossing the
		   chain equals the exact integer sum, and after a crossing
		   the original L_add chain resumes from the clamped value,
		   which is exactly per-term add_overflow + clamp (the
		   Overflow flag itself is write-only: L_sat is never
		   called). */
		Word32 s = 0;
		int k;
		for(k = 0; k < PE_LPF_ORD; k++)
		{
			Word32 t = (Word32)w[k] * (Word32)lpf_coef[k] * 2;
			Word32 v;
			if (__builtin_add_overflow(s, t, &v))
				v = (s < 0) ? MIN_32 : MAX_32;
			s = v;
		}

		/* round(s) = extract_h(L_add(s, 0x8000)): the add can only
		   saturate on the top side. */
		Word32 r;
		if (s > MAX_32 - 0x8000L) { r = MAX_32; Overflow = 1; }
		else                       r = s + 0x8000L;
		sigout[i] = (Word16)(r >> 16);
	}

	for(i = 0; i < PE_LPF_ORD; i++)
		mem[i] = W[len - 1 + i];
}
