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
#include "basic_op.h"
#include "imbe.h"
#include "tbls.h"
#include "dsp_sub.h"
#include "math_sub.h"
#include "encode.h"
#include "imbe_vocoder.h"

//-----------------------------------------------------------------------------
//	PURPOSE:
//				Perform inverse DCT
//
//
//  INPUT:
//              in     -  pointer to input data
//              m_lim  -  input data's size
//              i_lim  -  result's size
//              out    -  pointer to save result 
//
//	OUTPUT:
//		None
//
//	RETURN:
//		        Saved in out result of conversion
//
//-----------------------------------------------------------------------------
void idct(Word16 *in, Word16 m_lim, Word16 i_lim, Word16 *out)
{
	UWord16 angl_step, angl_intl, angl_intl_2;
	UWord16 angl_acc;
	Word32  sum;
	Word16  i, m;

	if(m_lim == 1)
	{
		angl_intl   = CNST_0_5_Q1_15;
		angl_intl_2 = CNST_1_0_Q1_15;
	}
	else
	{
		angl_intl   = div_s ((Word16) CNST_0_5_Q5_11, m_lim << 11); // calculate 0.5/m_lim
		angl_intl_2 = shl(angl_intl, 1);
	}

	angl_step = angl_intl;
	for(i = 0; i < i_lim; i++)
	{
		sum = 0;
		angl_acc = angl_step;
		for(m = 1; m < m_lim; m++)
		{
			sum = L_add(sum, L_shr( L_mult(in[m], cos_fxp(angl_acc)), 7));
			angl_acc += angl_step;			
		}
		sum = L_add(sum, L_shr( L_deposit_h(in[0]), 8));
		out[i] = extract_l(L_shr_r (sum, 8)); 
		angl_step += angl_intl_2; 
	}
}

//-----------------------------------------------------------------------------
//	PURPOSE:
//				Perform DCT
//
//
//  INPUT:
//              in     -  pointer to input data
//              m_lim  -  input data's size
//              i_lim  -  result's size
//              out    -  pointer to save result 
//
//	OUTPUT:
//		None
//
//	RETURN:
//		        Saved in out result of conversion
//
//-----------------------------------------------------------------------------
void dct(Word16 *in, Word16 m_lim, Word16 i_lim, Word16 *out)
{
	UWord16 angl_step, angl_intl, angl_intl_2, angl_begin;
	UWord16 angl_acc;
	Word32  sum;
	Word16  i, m;

	if(m_lim == 1)
	{
		angl_intl   = CNST_0_5_Q1_15;
		angl_intl_2 = CNST_1_0_Q1_15;
	}
	else
	{
		angl_intl   = div_s ((Word16) CNST_0_5_Q5_11, m_lim << 11); // calculate 0.5/m_lim
		angl_intl_2 = shl(angl_intl, 1);
	}

	// Calculate first coefficient
	sum = 0;
	for(m = 0; m < m_lim; m++)
		sum = L_add(sum, L_deposit_l(in[m]));
	out[0] = extract_l(L_mpy_ls(sum, angl_intl_2));

	// Calculate the others coefficients
	angl_begin = angl_intl;
	angl_step  = angl_intl_2;
	for(i = 1; i < i_lim; i++)
	{
		sum = 0;
		angl_acc = angl_begin;
		for(m = 0; m < m_lim; m++)
		{
			sum = L_add(sum, L_deposit_l(mult(in[m], cos_fxp(angl_acc))));
			angl_acc += angl_step;			
		}
		out[i] = extract_l(L_mpy_ls(sum, angl_intl_2));

		angl_step  += angl_intl_2;  
		angl_begin += angl_intl;
	}
}




void fft_init(Word16 *wr_array, Word16 *wi_array)
{
	Word16 i, fft_len2, shift, step, theta;

	fft_len2 = shr(FFTLENGTH, 1);
	shift    = norm_s(fft_len2);
	step     = shl(2, shift);
	theta    = 0;

	for(i = 0; i <= fft_len2; i++) 
	{
		wr_array[i] = cos_fxp(theta);    
		wi_array[i] = sin_fxp(theta);    
		if(i >= (fft_len2 - 1))
			theta = ONE_Q15;
		else
			theta = add(theta, step);
	}
}


//  	Subroutine FFT: Fast Fourier Transform 		
// ***************************************************************
// * Replaces data by its DFT, if isign is 1, or replaces data   *
// * by inverse DFT times nn if isign is -1.  data is a complex  *
// * array of length nn, input as a real array of length 2*nn.   *
// * nn MUST be an integer power of two.  This is not checked    *
// * The real part of the number should be in the zeroeth        *
// * of data , and the imaginary part should be in the next      *
// * element.  Hence all the real parts should have even indeces *
// * and the imaginary parts, odd indeces.			             *
// *                                                             * 
// * Data is passed in an array starting in position 0, but the  *
// * code is copied from Fortran so uses an internal pointer     *
// * which accesses position 0 as position 1, etc.		         *
// *                                                             *
// * This code uses e+jwt sign convention, so isign should be    *
// * reversed for e-jwt.                                         *
// ***************************************************************
//
// Q values:
// datam1 - Q14
// isign  - Q15 

#define	SWAP(a,b) temp1 = (a);(a) = (b); (b) = temp1

/* Saturation+round of ti - t / ti + t in pure 32-bit (see proof in
   fft()).  |t| <= 2*32767^2 and |ti| <= 32767<<15, so only the final
   combine can leave the range.  Overflow and the top clamp merge
   into one rare branch (with ti < 0 neither fires: the |t| bound
   keeps e <= 0x7FFF7FFF), and both return (ti>>31)^0x7FFF. */
static inline Word16 fft_sr_sub(int32_t ti, int32_t t)
{
	int32_t e;
	if (__builtin_expect((__builtin_sub_overflow(ti, t, &e) | (e > 0x7FFF7FFF)) != 0, 0))
		return (ti >> 31) ^ 0x7FFF;
	return (int16_t)(((uint32_t)e + 0x8000u) >> 16);
}

static inline Word16 fft_sr_add(int32_t ti, int32_t t)
{
	int32_t e;
	if (__builtin_expect((__builtin_add_overflow(ti, t, &e) | (e > 0x7FFF7FFF)) != 0, 0))
		return (ti >> 31) ^ 0x7FFF;
	return (int16_t)(((uint32_t)e + 0x8000u) >> 16);
}

void fft(const Word16 *wr_array, const Word16 *wi_array, Word16 *datam1, Word16 nn, Word16 isign)
{
	int n, mmax, m, j, istep, i;
	Word16 wr, wi, temp1;
	Word16 *data;
	int index, index_step;

	/* Bit-exact raw-integer form.  Data stays 16-bit (each stage's
	   round() output), twiddles are <= 32767 in magnitude, so every
	   16x16 product is exact int32 (|wr*data| < 2^31, no L_mult
	   special case), and only the four final L_add/L_sub per butterfly
	   can saturate.  Loop/index arithmetic is int (all values are
	   small positives, identical to the original Word16 results) so
	   no 16-bit truncation ops appear in address math. */

	/* Saturation+round of ti - t / ti + t in pure 32-bit.  Products
	   fit int32 and ti = data<<15 fits int32, so only the final
	   combine can leave the range - and that is exactly a 32-bit
	   signed overflow.  On overflow the true result is beyond
	   [-2^31, 2^31), which lands in the original's clamp buckets:
	   ti >= 0 means the overflow is positive (e > MAX_32 - 0x8000 ->
	   32767); ti < 0 means negative (e < MIN_32 -> -32768).  Without
	   overflow, e >= MIN_32 always, so only the top clamp can fire,
	   and ((uint32)e + 0x8000) >> 16 reproduces the original 64-bit
	   (e + 0x8000) >> 16 truncated to 16 bits (bit 32 never set).
	   The write-only Overflow flag is skipped, as before. */

	//  Use pointer indexed from 1 instead of 0	
	data = &datam1[-1];

	n = shl(nn,1);
	j = 1;
	for( i = 1; i < n; i+=2 ) 
	{
		if ( j > i) 
		{
			SWAP(data[j],data[i]);    
			SWAP(data[j+1],data[i+1]);   
		}
		m = nn;
		while ( m >= 2 && j > m ) 
		{
			j = (j - m);
			m = (m >> 1);
		}
		j = (j + m);
	}
	mmax = 2;

	// initialize index step 
	index_step = nn;

	while ( n > mmax) 
	{
		istep = shl(mmax,1);  // istep = 2 * mmax 

		index = 0;
		index_step = (index_step >> 1);

		/* Twiddle group m == 1 (wr = ONE_Q15 = 32767, wi = 0), peeled
		   out of the m loop: the wi cross terms vanish exactly
		   (0*x = 0 in integer), leaving tempr/tempi = 32767*re_j /
		   32767*im_j - two multiplies instead of four per
		   butterfly.  (wr is 32767, NOT 1: the Q15 identity
		   twiddle is 1 - 2^-15.) */
		for (i = 1; i <= n; i += istep)
		{
			j = i + mmax;

			int32_t re_j = (int32_t)(int16_t)data[j];
			int32_t im_j = (int32_t)(int16_t)data[j+1];
			int32_t re_i = (int32_t)(int16_t)data[i];
			int32_t im_i = (int32_t)(int16_t)data[i+1];

			int32_t tempr = re_j * 32767;
			int32_t tempi = im_j * 32767;

			int32_t ti = (int32_t)((uint32_t)re_i << 15);
			int32_t tj = (int32_t)((uint32_t)im_i << 15);

			data[j]   = fft_sr_sub(ti, tempr);
			data[i]   = fft_sr_add(ti, tempr);
			data[j+1] = fft_sr_sub(tj, tempi);
			data[i+1] = fft_sr_add(tj, tempi);
		}
		/* twiddles the original loop would have loaded at the end of
		   the m == 1 group */
		index = index_step;

		for (m = 3; m < mmax; m += 2)
		{
			wr = wr_array[index];
			if (isign < 0)
				wi = (Word16)(-wi_array[index]);
			else
				wi = wi_array[index];

			for ( i = m; i <= n; i += istep)
			{
				j = i + mmax;

				// exact products (no L_mult saturation possible):
				// |wr*re_j| <= 32767^2 < 2^31, and the 2-term sums stay
				// within int32 (|tempr| <= 2*32767^2 = 2147352574 < MAX_32)
				int32_t re_j = (int32_t)(int16_t)data[j];
				int32_t im_j = (int32_t)(int16_t)data[j+1];
				int32_t re_i = (int32_t)(int16_t)data[i];
				int32_t im_i = (int32_t)(int16_t)data[i+1];

				int32_t tempr = wr * re_j - wi * im_j;
				int32_t tempi = wr * im_j + wi * re_j;

				int32_t ti = (int32_t)((uint32_t)re_i << 15);
				int32_t tj = (int32_t)((uint32_t)im_i << 15);

				data[j]   = fft_sr_sub(ti, tempr);
				data[i]   = fft_sr_add(ti, tempr);
				data[j+1] = fft_sr_sub(tj, tempi);
				data[i+1] = fft_sr_add(tj, tempi);
			}
			index += index_step;
		}
		mmax = istep;
	}
} 


