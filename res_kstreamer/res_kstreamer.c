/*
 * Kstreamer helper functions for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>
#include <asterisk/version.h>
#include <asterisk/app.h>

/* FUCK YOU ASTERSISK */
#undef pthread_mutex_t
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_trylock
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_cond_t
#undef pthread_cond_init
#undef pthread_cond_destroy
#undef pthread_cond_signal
#undef pthread_cond_broadcast
#undef pthread_cond_wait
#undef pthread_cond_timedwait

#include <libkstreamer.h>

#include "res_kstreamer.h"

#include "util.h"

struct ks_conn *ks_conn;

#ifdef DEBUG_DEFAULTS
BOOL debug = FALSE;
#else
BOOL debug = FALSE;
#endif

static void ks_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case KS_LOG_DEBUG:
		if (debug)
			ast_verbose("ks %s", msg);
	break;

	case KS_LOG_INFO:
		ast_verbose(VERBOSE_PREFIX_2  "%s", msg);
	break;

	case KS_LOG_NOTICE:
		ast_log(__LOG_NOTICE, "ks", 0, "", "%s", msg);
	break;

	case KS_LOG_WARNING:
		ast_log(__LOG_WARNING, "ks", 0, "", "%s", msg);
	break;

	case KS_LOG_ERR:
	case KS_LOG_CRIT:
	case KS_LOG_ALERT:
	case KS_LOG_EMERG:
		ast_log(__LOG_ERROR, "ks", 0, "", "%s", msg);
	break;
	}
}

/*---------------------------------------------------------------------------*/

static int ks_show_kstreamer_dynattrs_func(int fd, int argc, char *argv[])
{
	int i;

	for (i=0; i<ARRAY_SIZE(ks_conn->dynattrs_hash); i++) {
		struct ks_dynattr *dynattr;
		struct hlist_node *t;

		pthread_mutex_lock(&ks_conn->topology_lock);
		hlist_for_each_entry(dynattr, t, &ks_conn->dynattrs_hash[i],
								node) {
			ast_cli(fd, "0x%08x: %s\n",
				dynattr->id,
				dynattr->name);
		}
		pthread_mutex_unlock(&ks_conn->topology_lock);
	}

	return RESULT_SUCCESS;
}

static char *ks_show_kstreamer_dynattrs_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	/*
	int i;

	switch(pos) {
	case 2:
		return ks_dynattr_completion(line, word, state);
	}
	*/

	return NULL;
}

static char ks_show_kstreamer_dynattrs_help[] =
"Usage: show kstreamer dynattrs\n"
"\n"
"	\n";

static struct ast_cli_entry ks_show_kstreamer_dynattrs =
{
	{ "show", "kstreamer", "dynattrs", NULL },
	ks_show_kstreamer_dynattrs_func,
	"",
	ks_show_kstreamer_dynattrs_help,
	ks_show_kstreamer_dynattrs_complete
};

/*---------------------------------------------------------------------------*/

static int ks_show_kstreamer_nodes_func(int fd, int argc, char *argv[])
{
	int i;

	for (i=0; i<ARRAY_SIZE(ks_conn->nodes_hash); i++) {
		struct ks_node *node;
		struct hlist_node *t;

		pthread_mutex_lock(&ks_conn->topology_lock);
		hlist_for_each_entry(node, t, &ks_conn->nodes_hash[i], node) {
			ast_cli(fd, "0x%08x: %s\n",
				node->id,
				node->path);
		}
		pthread_mutex_unlock(&ks_conn->topology_lock);
	}

	return RESULT_SUCCESS;
}

static char *ks_show_kstreamer_nodes_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	/*
	int i;

	switch(pos) {
	case 2:
		return ks_node_completion(line, word, state);
	}
	*/

	return NULL;
}

static char ks_show_kstreamer_nodes_help[] =
"Usage: show kstreamer nodes\n"
"\n"
"	\n";

static struct ast_cli_entry ks_show_kstreamer_nodes =
{
	{ "show", "kstreamer", "nodes", NULL },
	ks_show_kstreamer_nodes_func,
	"",
	ks_show_kstreamer_nodes_help,
	ks_show_kstreamer_nodes_complete
};

/*---------------------------------------------------------------------------*/

static int ks_show_kstreamer_chans_func(int fd, int argc, char *argv[])
{
	int i;

	for (i=0; i<ARRAY_SIZE(ks_conn->chans_hash); i++) {
		struct ks_chan *chan;
		struct hlist_node *t;

		pthread_mutex_lock(&ks_conn->topology_lock);
		hlist_for_each_entry(chan, t, &ks_conn->chans_hash[i], node) {
			ast_cli(fd, "0x%08x: %s\n",
				chan->id,
				chan->path);
		}
		pthread_mutex_unlock(&ks_conn->topology_lock);
	}

	return RESULT_SUCCESS;
}

static char *ks_show_kstreamer_chans_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	/*
	int i;

	switch(pos) {
	case 2:
		return ks_chan_completion(line, word, state);
	}
	*/

	return NULL;
}

static char ks_show_kstreamer_chans_help[] =
"Usage: show kstreamer chans\n"
"\n"
"	\n";

static struct ast_cli_entry ks_show_kstreamer_chans =
{
	{ "show", "kstreamer", "chans", NULL },
	ks_show_kstreamer_chans_func,
	"",
	ks_show_kstreamer_chans_help,
	ks_show_kstreamer_chans_complete
};

/*---------------------------------------------------------------------------*/

static int ks_show_kstreamer_pipelines_func(int fd, int argc, char *argv[])
{
	int i;

	for (i=0; i<ARRAY_SIZE(ks_conn->pipelines_hash); i++) {
		struct ks_pipeline *pipeline;
		struct hlist_node *t;

		pthread_mutex_lock(&ks_conn->topology_lock);
		hlist_for_each_entry(pipeline, t, &ks_conn->pipelines_hash[i],
									node) {
			ast_cli(fd, "0x%08x: %s\n",
				pipeline->id,
				pipeline->path);
		}
		pthread_mutex_unlock(&ks_conn->topology_lock);
	}

	return RESULT_SUCCESS;
}

static char *ks_show_kstreamer_pipelines_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	/*
	int i;

	switch(pos) {
	case 2:
		return ks_pipeline_completion(line, word, state);
	}
	*/

	return NULL;
}

static char ks_show_kstreamer_pipelines_help[] =
"Usage: show kstreamer pipelines\n"
"\n"
"	\n";

static struct ast_cli_entry ks_show_kstreamer_pipelines =
{
	{ "show", "kstreamer", "pipelines", NULL },
	ks_show_kstreamer_pipelines_func,
	"",
	ks_show_kstreamer_pipelines_help,
	ks_show_kstreamer_pipelines_complete
};

/*---------------------------------------------------------------------------*/

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int load_module(void)
#else
static int ks_load_module(void)
#endif
{
	int err;

	ks_conn = ks_conn_create();
	if (!ks_conn) {
		ast_log(LOG_ERROR, "Unable to create kstreamer connection\n");
		goto err_ks_conn_create;
	}

	ks_conn->report_func = ks_logger;

	err = ks_conn_establish(ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Unable to establish kstreamer connection\n");
		goto err_ks_conn_establish;
	}

	ks_update_topology(ks_conn);

	ast_cli_register(&ks_show_kstreamer_dynattrs);
	ast_cli_register(&ks_show_kstreamer_nodes);
	ast_cli_register(&ks_show_kstreamer_chans);
	ast_cli_register(&ks_show_kstreamer_pipelines);

	return 0;

	ast_cli_unregister(&ks_show_kstreamer_pipelines);
	ast_cli_unregister(&ks_show_kstreamer_chans);
	ast_cli_unregister(&ks_show_kstreamer_nodes);
	ast_cli_unregister(&ks_show_kstreamer_dynattrs);

	// Disconnect?
err_ks_conn_establish:
	ks_conn_destroy(ks_conn);
err_ks_conn_create:

	return -1;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int unload_module(void)
#else
static int ks_unload_module(void)
#endif
{
	ast_cli_unregister(&ks_show_kstreamer_pipelines);
	ast_cli_unregister(&ks_show_kstreamer_chans);
	ast_cli_unregister(&ks_show_kstreamer_nodes);
	ast_cli_unregister(&ks_show_kstreamer_dynattrs);

	ks_conn_destroy(ks_conn);

	return 0;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)

char *description(void)
{
	return "Kstreamer protocol handler";
}

int usecount(void)
{
	/* We should never be unloaded */
	return 1;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}

#else

AST_MODULE_INFO(ASTERISK_GPL_KEY,
		AST_MODFLAG_GLOBAL_SYMBOLS,
		"Kstreamer handler",
		.load = ks_load_module,
		.unload = ks_unload_module,
	);
#endif
