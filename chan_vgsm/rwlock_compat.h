/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#ifndef _VGSM_RWLOCK_COMPAT_H
#define _VGSM_RWLOCK_COMPAT_H

typedef pthread_rwlock_t ast_rwlock_t;

static inline int ast_rwlock_init(ast_rwlock_t *prwlock)
{
	pthread_rwlockattr_t attr;

	pthread_rwlockattr_init(&attr);

#ifdef HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NP
	pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
#endif

	return pthread_rwlock_init(prwlock, &attr);
}

static inline int ast_rwlock_destroy(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_destroy(prwlock);
}

#ifdef DEBUG_THREADS
#define ast_rwlock_unlock(a) \
	_ast_rwlock_unlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_unlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
	res = pthread_rwlock_unlock(lock);
	ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_rdlock(a) \
	_ast_rwlock_rdlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_rdlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
	ast_store_lock_info(AST_RDLOCK, file, line, func, name, lock);
	res = pthread_rwlock_rdlock(lock);
	if (!res)
		ast_mark_lock_acquired();
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_wrlock(a) \
	_ast_rwlock_wrlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_wrlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
	ast_store_lock_info(AST_WRLOCK, file, line, func, name, lock);
	res = pthread_rwlock_wrlock(lock);
	if (!res)
		ast_mark_lock_acquired();
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_tryrdlock(a) \
	_ast_rwlock_tryrdlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_tryrdlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
	ast_store_lock_info(AST_RDLOCK, file, line, func, name, lock);
	res = pthread_rwlock_tryrdlock(lock);
	if (!res)
		ast_mark_lock_acquired();
	else
		ast_remove_lock_info(lock);
	return res;
}

#define ast_rwlock_trywrlock(a) \
	_ast_rwlock_trywrlock(a, # a, __FILE__, __LINE__, __PRETTY_FUNCTION__)

static inline int _ast_rwlock_trywrlock(ast_rwlock_t *lock, const char *name,
	const char *file, int line, const char *func)
{
	int res;
	ast_store_lock_info(AST_WRLOCK, file, line, func, name, lock);
	res = pthread_rwlock_trywrlock(lock);
	if (!res)
		ast_mark_lock_acquired();
	else
		ast_remove_lock_info(lock);
	return res;
}

#else

static inline int ast_rwlock_unlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_unlock(prwlock);
}

static inline int ast_rwlock_rdlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_rdlock(prwlock);
}

static inline int ast_rwlock_tryrdlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_tryrdlock(prwlock);
}

static inline int ast_rwlock_wrlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_wrlock(prwlock);
}

static inline int ast_rwlock_trywrlock(ast_rwlock_t *prwlock)
{
	return pthread_rwlock_trywrlock(prwlock);
}
#endif /* DEBUG_THREADS */

/* Statically declared read/write locks */

#ifndef HAVE_PTHREAD_RWLOCK_INITIALIZER
#define __AST_RWLOCK_DEFINE(scope, rwlock) \
        scope ast_rwlock_t rwlock; \
static void  __attribute__ ((constructor)) init_##rwlock(void) \
{ \
        ast_rwlock_init(&rwlock); \
} \
static void  __attribute__ ((destructor)) fini_##rwlock(void) \
{ \
        ast_rwlock_destroy(&rwlock); \
}
#else
#define AST_RWLOCK_INIT_VALUE PTHREAD_RWLOCK_INITIALIZER
#define __AST_RWLOCK_DEFINE(scope, rwlock) \
        scope ast_rwlock_t rwlock = AST_RWLOCK_INIT_VALUE
#endif

#define AST_RWLOCK_DEFINE_STATIC(rwlock) __AST_RWLOCK_DEFINE(static, rwlock)

#endif
