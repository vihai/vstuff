/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Echo application -- play back what you hear to evaluate latency
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/options.h>
#include <asterisk/logger.h>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <chan_visdn.h>

static char *tdesc = "vISDN";
static char *app = "vISDNppp";
static char *synopsis = "Runs pppd and connects channel to visdn-ppp gateway";

static char *descrip = 
"  ZapRAS(args): Executes a RAS server using pppd on the given channel.\n"
"The channel must be a clear channel (i.e. PRI source) and a Zaptel\n"
"channel to be able to use this function (No modem emulation is included).\n"
"Your pppd must be patched to be zaptel aware. Arguments should be\n"
"separated by | characters.  Always returns -1.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

#define PPP_MAX_ARGS	32
#define PPP_EXEC	"/usr/sbin/pppd"

static int get_max_fds(void)
{
#ifdef OPEN_MAX
	return OPEN_MAX;
#else
	int max;

	max = sysconf(_SC_OPEN_MAX);
	if (max <= 0)
		return 1024;

	return max;
#endif
}

static pid_t spawn_ppp(
	struct ast_channel *chan,
	const char *argv[],
	int argc)
{
	/* Start by forking */
	pid_t pid = fork();
	if (pid)
		return pid;

	dup2(chan->fds[0], STDIN_FILENO);

	int i;
	int max_fds = get_max_fds();
	for (i=STDERR_FILENO + 1; i < max_fds; i++) 
		close(i);

	/* Restore original signal handlers */
	for (i=0; i<NSIG; i++)
		signal(i, SIG_DFL);

	/* Finally launch PPP */
	execv(PPP_EXEC, (char * const *)argv);
	fprintf(stderr, "Failed to exec pppd!: %s\n", strerror(errno));
	exit(1);
}


static int visdn_ppp_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct localuser *u;
	struct ast_frame *f;
	LOCAL_USER_ADD(u);

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	if (strcmp(chan->type, "VISDN")) {
		ast_log(LOG_WARNING,
			"Only VISDN channels may be connected to"
			" this application\n");

		return -1;
	}

	struct visdn_chan *visdn_chan = chan->pvt->pvt;
	if (!strlen(visdn_chan->visdn_chanid)) {
		ast_log(LOG_WARNING,
			"vISDN crossconnector channel ID not present\n");
		return -1;
	}

	const char *argv[PPP_MAX_ARGS] = { };
	int argc = 0;

	argv[argc++] = PPP_EXEC;
	argv[argc++] = "nodetach";

	char *stringp = strdup(data);
	char *arg;
	while(arg = strsep(&stringp, "|")) {

		if (!strlen(arg))
			break;

		if (argc >= PPP_MAX_ARGS - 4)
			break;

		argv[argc++] = arg;
	}

	argv[argc++] = "plugin";
	argv[argc++] = "visdn.so";
	argv[argc++] = visdn_chan->visdn_chanid;

/*	int i;
	for (i=0;i<argc;i++) {
		ast_log(LOG_NOTICE, "Arg %d: %s\n", i, argv[i]);
	}*/

	pid_t pid = spawn_ppp(chan, argv, argc);
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to spawn pppd\n");
		return -1;
	}

	int status;
	int signalled;
	for (;;) {
		while(ast_waitfor(chan, -1) > -1) {
			res = wait4(pid, &status, WNOHANG, NULL);
			if (!res) {
				/* Check for hangup */
				if (chan->_softhangup && !signalled) {
					ast_log(LOG_NOTICE,
						"Channel '%s' hungup."
						" Signalling PPP at %d to die...\n",
						chan->name, pid);

					kill(pid, SIGTERM);
					signalled = 1;
				}

				/* Try again */
				sleep(1);
				continue;
			}

			f = ast_read(chan);
			if (!f)
				break;

			ast_frfree(f);
		}


		if (res < 0) {
			ast_log(LOG_WARNING,
				"wait4 returned %d: %s\n",
				res, strerror(errno));
		}

		if (option_verbose > 2) {
			if (WIFEXITED(status)) {
				ast_verbose(VERBOSE_PREFIX_3
					"PPP on %s terminated with status %d\n",
					chan->name, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				ast_verbose(VERBOSE_PREFIX_3
					"PPP on %s terminated with signal %d\n", 
					chan->name, WTERMSIG(status));
			} else {
				ast_verbose(VERBOSE_PREFIX_3
					"PPP on %s terminated weirdly.\n", chan->name);
			}
		}

		break;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, visdn_ppp_exec, synopsis, descrip);
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
