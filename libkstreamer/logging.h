/*
 * Userland Kstreamer interface
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */


#ifndef _LIBKSTREAMER_LOGGING_H
#define _LIBKSTREAMER_LOGGING_H

enum ks_log_level
{
	KS_LOG_DEBUG,
	KS_LOG_INFO,
	KS_LOG_NOTICE,
	KS_LOG_WARNING,
	KS_LOG_ERR,
	KS_LOG_CRIT,
	KS_LOG_ALERT,
	KS_LOG_EMERG,
};

#ifdef _LIBKSTREAMER_PRIVATE_

#define LOG_DEBUG	KS_LOG_DEBUG
#define LOG_INFO	KS_LOG_INFO
#define LOG_NOTICE	KS_LOG_NOTICE
#define LOG_WARNING	KS_LOG_WARNING
#define LOG_ERR		KS_LOG_ERR
#define LOG_CRIT	KS_LOG_CRIT
#define LOG_ALERT	KS_LOG_ALERT
#define LOG_EMERG	KS_LOG_EMERG

#endif

#endif
