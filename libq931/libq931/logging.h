/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#ifndef _LIBQ931_LOGGING_H
#define _LIBQ931_LOGGING_H

enum q931_log_level
{
	Q931_LOG_DEBUG,
	Q931_LOG_INFO,
	Q931_LOG_NOTICE,
	Q931_LOG_WARNING,
	Q931_LOG_ERR,
	Q931_LOG_CRIT,
	Q931_LOG_ALERT,
	Q931_LOG_EMERG,
};

#ifdef Q931_PRIVATE

#define LOG_DEBUG	Q931_LOG_DEBUG
#define LOG_INFO	Q931_LOG_INFO
#define LOG_NOTICE	Q931_LOG_NOTICE
#define LOG_WARNING	Q931_LOG_WARNING
#define LOG_ERR		Q931_LOG_ERR
#define LOG_CRIT	Q931_LOG_CRIT
#define LOG_ALERT	Q931_LOG_ALERT
#define LOG_EMERG	Q931_LOG_EMERG

#endif

#endif
