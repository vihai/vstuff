#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <signal.h>

#include <lapd.h>

#define Q931_PRIVATE

#include <libq931/msgtype.h>
#include <libq931/ie.h>

static int shutting_down = FALSE;
static int shutdown_acked;

void sighandler(int signal)
{
	shutting_down = TRUE;
	shutdown_acked = FALSE;
}

int main()
{
	signal(SIGINT, sighandler);

	q931_init();

	struct q931_interface *interface = q931_open_interface("fakeisdn0d");
	if (!interface) {
		printf("q931_open_interface error: %s\n",strerror(errno));
		exit(1);
	}

	struct q931_dlc *dlc = &interface->te_dlc;
	struct q931_call *call;

	call = q931_alloc_call();

	printf("Interface opened\n");

	printf("Sleeping...");
	sleep(1);
	printf("OK\n");

	printf("Making call...\n");
	q931_make_call(interface, call);

	int active_calls_cnt = 0;
	do {
		fd_set read_fds;
		struct timeval tv;
		int retval;

		FD_ZERO(&read_fds);
		FD_SET(dlc->socket, &read_fds);

		tv.tv_sec = 0;
		tv.tv_usec = 0;

		if((retval = select(dlc->socket+1, &read_fds, NULL, NULL, &tv))<0) {
			printf("select error: %s\n",strerror(errno));
			exit(1);
		} else if(retval) {
			if (FD_ISSET(interface->te_dlc.socket, &read_fds))
				q931_receive(dlc);
		}

		if (shutting_down && !shutdown_acked) {
	 		struct q931_call *call;
			list_for_each_entry(call, &interface->calls, node)
				q931_hangup_call(call);

			shutdown_acked = TRUE;

			continue;
		}

 		struct q931_call *call;
		active_calls_cnt = 0;
		list_for_each_entry(call, &interface->calls, node)
			active_calls_cnt++;

	} while(active_calls_cnt > 0);

	q931_free_call(call);
	q931_close_interface(interface);

	return 0;
}
