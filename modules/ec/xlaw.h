/*
 * vISDN A-law/u-law conversion routines
 *
 * Copyright (C) 2005 Daniele Orlandi
 * Copyright (C) 2001 Steve Underwood
 *
 * Authors:
 * Daniele "Vihai" Orlandi <daniele@orlandi.com>
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>

/* N.B. It is tempting to use look-up tables for A-law and u-law conversion.
 *      However, you should consider the cache footprint.
 *
 *      A 64K byte table for linear to x-law and a 512 byte sound like peanuts
 *      these days, and shouldn't an array lookup be real fast? No! When the
 *      cache sloshes as badly as this one will, a tight calculation is better.
 *      The messiest part is normally finding the segment, but a little inline
 *      assembly can fix that on an i386.
 */
 
/*
 * Mu-law is basically as follows:
 *
 *      Biased Linear Input Code        Compressed Code
 *      ------------------------        ---------------
 *      00000001wxyza                   000wxyz
 *      0000001wxyzab                   001wxyz
 *      000001wxyzabc                   010wxyz
 *      00001wxyzabcd                   011wxyz
 *      0001wxyzabcde                   100wxyz
 *      001wxyzabcdef                   101wxyz
 *      01wxyzabcdefg                   110wxyz
 *      1wxyzabcdefgh                   111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */

#define ZEROTRAP                        /* turn on the trap as per the MIL-STD */
#define BIAS             0x84           /* Bias for linear code. */

static inline u8 linear_to_ulaw(s16 linear)
{
    u8 u_val;
    int mask;
    int seg;
    int pcm_val;

    pcm_val = linear;
    /* Get the sign and the magnitude of the value. */
    if (pcm_val < 0)
    {
        pcm_val = BIAS - pcm_val;
        mask = 0x7F;
    }
    else
    {
        pcm_val = BIAS + pcm_val;
        mask = 0xFF;
    }

    seg = fls(pcm_val | 0xFF) - 8;

    /*
     * Combine the sign, segment, quantization bits,
     * and complement the code word.
     */
    if (seg >= 8)
        u_val = 0x7F ^ mask;
    else
        u_val = ((seg << 4) | ((pcm_val >> (seg + 3)) & 0xF)) ^ mask;
#ifdef ZEROTRAP
    /* optional CCITT trap */
    if (u_val == 0)
        u_val = 0x02;
#endif
    return  u_val;
}
/*- End of function --------------------------------------------------------*/

static inline s16 ulaw_to_linear(u8 ulaw)
{
    int t;
    
    /* Complement to obtain normal u-law value. */
    ulaw = ~ulaw;
    /*
     * Extract and bias the quantization bits. Then
     * shift up by the segment number and subtract out the bias.
     */
    t = (((ulaw & 0x0F) << 3) + BIAS) << (((int) ulaw & 0x70) >> 4);
    return  ((ulaw & 0x80)  ?  (BIAS - t) : (t - BIAS));
}
/*- End of function --------------------------------------------------------*/

/*
 * A-law is basically as follows:
 *
 *      Linear Input Code        Compressed Code
 *      -----------------        ---------------
 *      0000000wxyza             000wxyz
 *      0000001wxyza             001wxyz
 *      000001wxyzab             010wxyz
 *      00001wxyzabc             011wxyz
 *      0001wxyzabcd             100wxyz
 *      001wxyzabcde             101wxyz
 *      01wxyzabcdef             110wxyz
 *      1wxyzabcdefg             111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */

#define AMI_MASK        0x55

static inline u8 linear_to_alaw(s16 linear)
{
    int mask;
    int seg;
    int pcm_val;
    
    pcm_val = linear;

    if (pcm_val >= 0)
    {
        /* Sign (7th) bit = 1 */
        mask = AMI_MASK | 0x80;
    }
    else
    {
        /* Sign bit = 0 */
        mask = AMI_MASK;
        pcm_val = -pcm_val;
    }

    /* Convert the scaled magnitude to segment number. */
    seg = fls(pcm_val | 0xFF) - 8;

//printk(KERN_INFO "%04x => %d %d\n", pcm_val, fls(pcm_val), seg);

    /* Combine the sign, segment, and quantization bits. */
    return  ((seg << 4) | ((pcm_val >> ((seg) ?
			(seg + 3) : 4)) & 0x0F)) ^ mask;
}

static inline s16 alaw_to_linear(u8 alaw)
{
    int i;
    int seg;

    alaw ^= AMI_MASK;

    i = (alaw & 0x0F) << 4;

    seg = (alaw & 0x70) >> 4;

    if (seg)
	i = (i + 0x100) << (seg - 1);

    return (alaw & 0x80)  ?  i  :  -i;
}
