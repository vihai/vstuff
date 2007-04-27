/*
 * app_pipe application: pipe frames through a exec-ed process
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
#include <asterisk/version.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static char *app = "Pipe";
static char *synopsis = "Test application";

static char *descrip =
"\n";

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

static pid_t spawn_handler(
	struct ast_channel *chan,
	const char *argv[],
	int argc,
	int *rfd,
	int *wfd,
	int *efd)
{
	int r_filedes[2];
	if (pipe(r_filedes) < 0) {
		ast_log(LOG_ERROR, "pipe(r_filedes) error: %s\n",
			strerror(errno));
		return -1;
	}

	int w_filedes[2];
	if (pipe(w_filedes) < 0) {
		ast_log(LOG_ERROR, "pipe(w_filedes) error: %s\n",
			strerror(errno));
		return -1;
	}

	int e_filedes[2];
	if (pipe(e_filedes) < 0) {
		ast_log(LOG_ERROR, "pipe(e_filedes) error: %s\n",
			strerror(errno));
		return -1;
	}

	/* Start by forking */
	pid_t pid = fork();
	if (!pid) {
		close(r_filedes[0]);
		close(w_filedes[1]);
		close(e_filedes[0]);

		if (dup2(w_filedes[0], 0) < 0) {
			ast_log(LOG_ERROR, "dup2(w_filedes) error: %s\n",
				strerror(errno));
			return -1;
		}

		if (dup2(r_filedes[1], 1) < 0) {
			ast_log(LOG_ERROR, "dup2(r_filedes) error: %s\n",
				strerror(errno));
			return -1;
		}

		if (dup2(e_filedes[1], 2) < 0) {
			ast_log(LOG_ERROR, "dup2(e_filedes) error: %s\n",
				strerror(errno));
			return -1;
		}

		int i;
		int max_fds = get_max_fds();
		for (i=STDERR_FILENO + 1; i < max_fds; i++)
			close(i);

		/* Restore original signal handlers */
		for (i=0; i<NSIG; i++)
			signal(i, SIG_DFL);

		execv(argv[0], (char * const *)argv);
		fprintf(stderr, "Failed to exec handler!: %s\n",
					strerror(errno));
		exit(1);
	}

	close(r_filedes[1]);
	close(w_filedes[0]);
	close(e_filedes[1]);

	if (fcntl(r_filedes[0], F_SETFL, O_NONBLOCK) < 0) {
		ast_log(LOG_ERROR, "fcntl(r_filedes, O_NONBLOCK) error: %s\n",
			strerror(errno));
		return -1;
	}

	if (fcntl(w_filedes[1], F_SETFL, O_NONBLOCK) < 0) {
		ast_log(LOG_ERROR, "fcntl(w_filedes, O_NONBLOCK) error: %s\n",
			strerror(errno));
		return -1;
	}

	if (fcntl(e_filedes[0], F_SETFL, O_NONBLOCK) < 0) {
		ast_log(LOG_ERROR, "fcntl(e_filedes, O_NONBLOCK) error: %s\n",
			strerror(errno));
		return -1;
	}

	*rfd = r_filedes[0];
	*wfd = w_filedes[1];
	*efd = e_filedes[0];

	return pid;
}

static int handler_exec(struct ast_channel *chan, void *data)
{
	int res=-1;
	struct ast_frame *f;

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	const char *argv[32] = { };
	int argc = 0;

	char *stringp = strdup(data);
	char *arg;
	while((arg = strsep(&stringp, "|"))) {

		if (!strlen(arg))
			break;

		if (argc >= ARRAY_SIZE(argv) - 4)
			break;

		argv[argc++] = arg;
	}

#if 0
	int i;
	for (i=0;i<argc;i++) {
		ast_log(LOG_NOTICE, "Arg %d: %s\n", i, argv[i]);
	}
#endif

	signal(SIGCHLD, SIG_DFL);

	int rfd, wfd, efd;

	pid_t pid = spawn_handler(chan, argv, argc, &rfd, &wfd, &efd);
	if (pid < 0) {
		ast_log(LOG_WARNING, "Failed to spawn handler\n");
		goto err_spawn_handler;
	}

	while(ast_waitfor(chan, -1) > -1) {

		f = ast_read(chan);
		if (!f) {
			ast_log(LOG_NOTICE,
				"Channel '%s' hungup."
				" Signalling handler at %d to die...\n",
				chan->name, pid);

			kill(pid, SIGTERM);

			break;
		}

		write(wfd, f->data, f->datalen);

		char buf[1024];
		int nread = read(efd, buf, sizeof(buf));
		if (nread < 0) {
			if (errno != EAGAIN) {
				// TODO
			}
		} else {
			ast_log(LOG_NOTICE, "%s", buf);
		}

		nread = read(rfd, buf, sizeof(buf));
		if (nread < 0) {
			if (errno != EAGAIN) {
				// TODO
			}

			ast_frfree(f);
		} else {
			f->data = buf;
			f->datalen = nread;
			f->samples = f->datalen;
			f->offset = 0;
			f->mallocd = 0;
			f->src = "app_pipe";
			f->delivery.tv_sec = 0;
			f->delivery.tv_usec = 0;

			if (ast_write(chan, f) < 0) {
				ast_log(LOG_WARNING,
					"Failed to write frame to '%s': %s\n",
					chan->name, strerror(errno));
				goto err_ast_write;
			}
		}

		int status;
		res = wait4(pid, &status, WNOHANG, NULL);
		if (res < 0) {
			ast_log(LOG_WARNING,
				"wait4 returned %d: %s\n",
				res, strerror(errno));

			break;
		} else if (res > 0) {
			if (option_verbose > 2) {
				if (WIFEXITED(status)) {
					ast_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated with"
						" status %d\n",
						chan->name,
						WEXITSTATUS(status));
				} else if (WIFSIGNALED(status)) {
					ast_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated with"
						" signal %d\n",
						chan->name, WTERMSIG(status));
				} else {
					ast_verbose(VERBOSE_PREFIX_3
						"PPP on %s terminated"
						" weirdly.\n", chan->name);
				}
			}

			break;
		}
	}

	return res;

err_ast_write:
err_spawn_handler:

	return -1;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int load_module(void)
#else
static int app_pipe_load_module(void)
#endif
{
	return ast_register_application(app, handler_exec,
					synopsis, descrip);
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int unload_module(void)
#else
static int app_pipe_unload_module(void)
#endif
{
	return ast_unregister_application(app);
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)

int usecount(void)
{
	return 0;
}

char *description(void)
{
	return "Pipe";
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}

#else

AST_MODULE_INFO(ASTERISK_GPL_KEY,
		AST_MODFLAG_GLOBAL_SYMBOLS,
		"Pipe Application",
		.load = app_pipe_load_module,
		.unload = app_pipe_unload_module,
	);
#endif
