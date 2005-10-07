#ifndef _VECKB_ARITH_H
#define _VECKB_ARITH_H

static inline int CONVOLVE(const int *coeffs, const short *hist, int len)
{
	int x;
	int sum = 0;
	for (x=0;x<len;x++)
		sum += (coeffs[x] >> 16) * hist[x];
	return sum;
}

static inline int CONVOLVE2(const short *coeffs, const short *hist, int len)
{
	int x;
	int sum = 0;
	for (x=0;x<len;x++)
		sum += coeffs[x] * hist[x];
	return sum;
}

static inline void UPDATE(int *taps, const short *history, const int nsuppr, const int ntaps)
{
	int i;
	int correction;
	for (i=0;i<ntaps;i++) {
		correction = history[i] * nsuppr;
		taps[i] += correction;
	}
}

static inline void UPDATE2(int *taps, short *taps_short, const short *history, const int nsuppr, const int ntaps)
{
	int i;
	int correction;
	for (i=0;i<ntaps;i++) {
		correction = history[i] * nsuppr;
		taps[i] += correction;
		taps_short[i] = taps[i] >> 16;
	}
}

static inline short MAX16(const short *y, int len, int *pos)
{
	int k;
	short max = 0;
	int bestpos = 0;
	for (k=0;k<len;k++) {
		if (max < y[k]) {
			bestpos = k;
			max = y[k];
		}
	}
	*pos = (len - 1 - bestpos);
	return max;
}

#endif
