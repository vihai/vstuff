#ifndef _LIBQ931_UTIL_H
#define _LIBQ931_UTIL_H

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

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#endif
