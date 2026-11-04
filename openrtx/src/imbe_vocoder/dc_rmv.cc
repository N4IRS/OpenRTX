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
#include "dc_rmv.h"


#define CNST_0_99_Q1_15   0x7EB8


//-----------------------------------------------------------------------------
//	PURPOSE:
//		High-pass filter to remove DC
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
/* Exact raw equivalents of the L_add / L_sub primitives: 32-bit wrap
   arithmetic plus the XOR sign test, clamping to MAX_32/MIN_32 and
   setting Overflow exactly like the originals (basicop2.cc). */
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

/* round(L) = extract_h(L_add(L, 32768)) */
static inline Word16 raw_round(Word32 a)
{
	return (Word16)(raw_l_add(a, 0x00008000L) >> 16);
}

/* Exact raw form of L_mpy_ls(a, b):
       L_mac(L_shr(L_mult(b, swtemp), 15), b, extract_h(a)),
   swtemp = (extract_l(a) shr 1) & 0x7fff.  |b*swtemp*2| < 2^31 and
   |b*hi*2| < 2^31, so both L_mults are exact; only the final L_add
   (inside L_mac) can saturate. */
static inline Word32 raw_l_mpy_ls(Word32 a, Word16 b)
{
	Word16 lo = (Word16)a;
	Word16 sw = (Word16)(((uint16_t)(Word16)(lo >> 1)) & 0x7FFFu);
	Word16 hi = (Word16)(a >> 16);
	Word32 p = (Word32)b * (Word32)sw * 2;
	return raw_l_add((p >> 15), (Word32)b * (Word32)hi * 2);
}

void dc_rmv(Word16 *sigin, Word16 *sigout, Word32 *mem, Word16 len)
{
	Word32 L_mem;

	/* Bit-exact raw-integer form: same L_deposit_h / L_add / round /
	   L_mpy_ls / L_sub chain, with the original saturating semantics
	   (the one-pole state can approach +-2^31 on full-scale input). */
	L_mem = *mem;
	while(len--)
	{
		Word32 x = (Word32)(int16_t)*sigin++ << 16;   /* L_deposit_h */
		L_mem = raw_l_add(L_mem, x);
		*sigout++ = raw_round(L_mem);
		L_mem = raw_l_sub(raw_l_mpy_ls(L_mem, CNST_0_99_Q1_15), x);
	}
	*mem = L_mem;
}
