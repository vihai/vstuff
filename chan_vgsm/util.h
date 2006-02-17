#ifndef _VGSM_UTIL_H
#define _VGSM_UTIL_H

extern struct vgsm_state vgsm;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#ifdef DEBUG_CODE
#define vgsm_debug(format, arg...)			\
	if (vgsm.debug)					\
		ast_log(LOG_NOTICE,			\
			format,				\
			## arg)
#else
#define vgsm_debug(format, arg...)			\
	do {} while(0);
#endif

#ifdef DEBUG_CODE
#define vgsm_debug_verb(format, arg...)			\
	if (vgsm.debug)					\
		ast_verbose(VERBOSE_PREFIX_1		\
			format,				\
			## arg)
#else
#define vgsm_debug_verb(format, arg...)			\
	do {} while(0);
#endif

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

#define min(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);		\
	typeof(y) _y = (y);		\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

#if 0
#define ast_mutex_lock(a) \
	do {				\
		ast_verbose("LOCK " # a "  %s:%d\n", __FILE__, __LINE__); \
		ast_mutex_lock(a);	\
	} while(0)

#define ast_mutex_unlock(a) \
	do {				\
		ast_verbose("UNLOCK " # a "  %s:%d\n", __FILE__, __LINE__); \
		ast_mutex_unlock(a);	\
	} while(0)
#endif

#endif
