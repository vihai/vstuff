/*
 * Kstreamer helper functions for Asterisk
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
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

//#ifdef DEBUG_DEFAULTS
//BOOL debug = FALSE;
//#else
//BOOL debug = FALSE;
//#endif

static void ks_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case KS_LOG_DEBUG:
		ast_verbose("libks: %s", msg);
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

static int ks_kstreamer_show_features_func(int fd, int argc, char *argv[])
{
	int i;

	ks_conn_topology_rdlock(ks_conn);
	for (i=0; i<ARRAY_SIZE(ks_conn->features_hash); i++) {
		struct ks_feature *feature;
		struct hlist_node *t;

		hlist_for_each_entry(feature, t, &ks_conn->features_hash[i],
								node) {
			ast_cli(fd, "0x%08x: %s\n",
				feature->id,
				feature->name);
		}
	}
	ks_conn_topology_unlock(ks_conn);

	return RESULT_SUCCESS;
}

static char *ks_kstreamer_show_features_complete(
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
		return ks_feature_completion(line, word, state);
	}
	*/

	return NULL;
}

static char ks_kstreamer_show_features_help[] =
"Usage: kstreamer show features\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_show_features =
{
	{ "kstreamer", "show", "features", NULL },
	ks_kstreamer_show_features_func,
	"",
	ks_kstreamer_show_features_help,
	ks_kstreamer_show_features_complete
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_show_nodes_func(int fd, int argc, char *argv[])
{
	int i;

	ks_conn_topology_rdlock(ks_conn);
	for (i=0; i<ARRAY_SIZE(ks_conn->nodes_hash); i++) {
		struct ks_node *node;
		struct hlist_node *t;

		hlist_for_each_entry(node, t, &ks_conn->nodes_hash[i], node) {
			ast_cli(fd, "0x%08x: %s\n",
				node->id,
				node->path);
		}
	}
	ks_conn_topology_unlock(ks_conn);

	return RESULT_SUCCESS;
}

static char *ks_kstreamer_show_nodes_complete(
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

static char ks_kstreamer_show_nodes_help[] =
"Usage: kstreamer show nodes\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_show_nodes =
{
	{ "kstreamer", "show", "nodes", NULL },
	ks_kstreamer_show_nodes_func,
	"",
	ks_kstreamer_show_nodes_help,
	ks_kstreamer_show_nodes_complete
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_show_chans_func(int fd, int argc, char *argv[])
{
	int i;

	ks_conn_topology_rdlock(ks_conn);
	for (i=0; i<ARRAY_SIZE(ks_conn->chans_hash); i++) {
		struct ks_chan *chan;
		struct hlist_node *t;

		hlist_for_each_entry(chan, t, &ks_conn->chans_hash[i], node) {
			ast_cli(fd, "0x%08x: %s\n",
				chan->id,
				chan->path);
		}
	}
	ks_conn_topology_unlock(ks_conn);

	return RESULT_SUCCESS;
}

static char *ks_kstreamer_show_chans_complete(
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

static char ks_kstreamer_show_chans_help[] =
"Usage: kstreamer show chans\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_show_chans =
{
	{ "kstreamer", "show", "chans", NULL },
	ks_kstreamer_show_chans_func,
	"",
	ks_kstreamer_show_chans_help,
	ks_kstreamer_show_chans_complete
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_show_pipelines_func(int fd, int argc, char *argv[])
{
	int i;

	ks_conn_topology_rdlock(ks_conn);
	for (i=0; i<ARRAY_SIZE(ks_conn->pipelines_hash); i++) {
		struct ks_pipeline *pipeline;
		struct hlist_node *t;

		hlist_for_each_entry(pipeline, t, &ks_conn->pipelines_hash[i],
									node) {
			ast_cli(fd, "0x%08x: %s\n",
				pipeline->id,
				pipeline->path);
		}
	}
	ks_conn_topology_unlock(ks_conn);

	return RESULT_SUCCESS;
}

static char *ks_kstreamer_show_pipelines_complete(
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

static char ks_kstreamer_show_pipelines_help[] =
"Usage: kstreamer show pipelines\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_show_pipelines =
{
	{ "kstreamer", "show", "pipelines", NULL },
	ks_kstreamer_show_pipelines_func,
	"",
	ks_kstreamer_show_pipelines_help,
	ks_kstreamer_show_pipelines_complete
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_debug_messages_func(int fd, int argc, char *argv[])
{
	ks_conn->debug_netlink = TRUE;

	return RESULT_SUCCESS;
}

static char  ks_kstreamer_debug_messages_help[] =
"Usage: kstreamer debug messages\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_debug_messages =
{
	{ "kstreamer", "debug", "messages", NULL },
	ks_kstreamer_debug_messages_func,
	"",
	ks_kstreamer_debug_messages_help,
	NULL,
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_no_debug_messages_func(int fd, int argc, char *argv[])
{
	ks_conn->debug_netlink = FALSE;

	return RESULT_SUCCESS;
}

static char ks_kstreamer_no_debug_messages_help[] =
"Usage: no kstreamer debug messages\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_no_debug_messages =
{
	{ "no", "kstreamer", "debug", "messages", NULL },
	ks_kstreamer_no_debug_messages_func,
	"",
	ks_kstreamer_no_debug_messages_help,
	NULL,
};

/*---------------------------------------------------------------------------*/

static int ks_kstreamer_debug_router_func(int fd, int argc, char *argv[])
{
	ks_conn->debug_router = TRUE;

	return RESULT_SUCCESS;
}

static char  ks_kstreamer_debug_router_help[] =
"Usage: kstreamer debug router\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_debug_router =
{
	{ "kstreamer", "debug", "router", NULL },
	ks_kstreamer_debug_router_func,
	"",
	ks_kstreamer_debug_router_help,
	NULL,
};


/*---------------------------------------------------------------------------*/

static int ks_kstreamer_no_debug_router_func(int fd, int argc, char *argv[])
{
	ks_conn->debug_router = FALSE;

	return RESULT_SUCCESS;
}

static char ks_kstreamer_no_debug_router_help[] =
"Usage: no kstreamer debug router\n"
"\n"
"	\n";

static struct ast_cli_entry ks_kstreamer_no_debug_router =
{
	{ "no", "kstreamer", "debug", "router", NULL },
	ks_kstreamer_no_debug_router_func,
	"",
	ks_kstreamer_no_debug_router_help,
	NULL,
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

	ast_cli_register(&ks_kstreamer_show_features);
	ast_cli_register(&ks_kstreamer_show_nodes);
	ast_cli_register(&ks_kstreamer_show_chans);
	ast_cli_register(&ks_kstreamer_show_pipelines);
	ast_cli_register(&ks_kstreamer_debug_messages);
	ast_cli_register(&ks_kstreamer_no_debug_messages);
	ast_cli_register(&ks_kstreamer_debug_router);
	ast_cli_register(&ks_kstreamer_no_debug_router);

	return 0;

	ast_cli_unregister(&ks_kstreamer_no_debug_router);
	ast_cli_unregister(&ks_kstreamer_debug_router);
	ast_cli_unregister(&ks_kstreamer_no_debug_messages);
	ast_cli_unregister(&ks_kstreamer_debug_messages);
	ast_cli_unregister(&ks_kstreamer_show_pipelines);
	ast_cli_unregister(&ks_kstreamer_show_chans);
	ast_cli_unregister(&ks_kstreamer_show_nodes);
	ast_cli_unregister(&ks_kstreamer_show_features);

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
	ast_cli_unregister(&ks_kstreamer_no_debug_router);
	ast_cli_unregister(&ks_kstreamer_debug_router);
	ast_cli_unregister(&ks_kstreamer_no_debug_messages);
	ast_cli_unregister(&ks_kstreamer_debug_messages);
	ast_cli_unregister(&ks_kstreamer_show_pipelines);
	ast_cli_unregister(&ks_kstreamer_show_chans);
	ast_cli_unregister(&ks_kstreamer_show_nodes);
	ast_cli_unregister(&ks_kstreamer_show_features);

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
