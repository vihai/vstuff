#ifndef _UTIL_H
#define _UTIL_H

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

static inline int q931_intcmp(int a, int b)
{
 if (a==b) return 0;
 else if (a>b) return 1;
 else return -1;
}

#endif
