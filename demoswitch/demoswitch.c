#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <sys/poll.h>
#include <signal.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <lapd.h>

#include "q931.h"
#include "q931_mt.h"
#include "q931_ie.h"

enum poll_info_type
{
	POLL_INFO_TYPE_STDIN,
	POLL_INFO_TYPE_INTERFACE,
	POLL_INFO_TYPE_DLC,
};

struct poll_info
{
	enum poll_info_type type;
	union
	{
		struct q931_interface *interface;
		struct q931_dlc *dlc;
	};
};

struct switch_state
{
	int have_to_exit;

	struct q931_interface *ifs[100];
	int nifs;

	struct q931_dlc dlcs[100];
	int ndlcs;
};

void handle_command(struct switch_state *state, const char *s)
{
	if (!strncasecmp(s, "exit", 4) ||
	    !strncasecmp(s, "quit", 4)) {

		state->have_to_exit = 1;

		printf("Hanging up all calls...\n");

		int i;

		for (i=0; i<state->nifs; i++) {
			struct q931_call *call;
			list_for_each_entry(call, &state->ifs[i]->calls, calls_node)
				q931_hangup_call(call);
		}

	} else if (!strncasecmp(s, "dial", 4)) {

		char *s2 = strdup(s+4);
		char *p = s2;

		char *intname = strtok_r(s2, " ", &p);
		if (!intname) {

			free(s2);
			return;
		}

		struct q931_interface *intf = NULL;
		int i;
		for (i=0; i<state->nifs; i++) {
		printf("intname = '%s' '%s'\n", state->ifs[i]->name, intname);
			if (!strcmp(intname, state->ifs[i]->name)) {
				intf = state->ifs[i];
				break;
			}
		}

		if (intf) {
			struct q931_call *call;
			call = q931_alloc_call();
			q931_make_call(intf, call);
		}

		free(s2);

	} else if (strlen(s) == 1) {
	} else {
		printf("Unknown command\n");
	}
}

void refresh_polls_list(
	struct switch_state *state,
	struct pollfd *polls, struct poll_info *poll_infos, int *npolls)
{
	*npolls = 0;

	// STDIN
	polls[*npolls].fd = 0;
	polls[*npolls].events = POLLIN|POLLERR;
	poll_infos[*npolls].type = POLL_INFO_TYPE_STDIN;
	(*npolls)++;

	int i;
	for(i = 0; i < state->nifs; i++) {
		if (state->ifs[i]->role == LAPD_ROLE_NT) {
			polls[*npolls].fd = state->ifs[i]->nt_socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_INTERFACE;
			poll_infos[*npolls].interface = state->ifs[i];
			(*npolls)++;
		} else {
			polls[*npolls].fd = state->ifs[i]->te_dlc.socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = &state->ifs[i]->te_dlc;
			(*npolls)++;
		}
	}

	for(i = 0; i < state->ndlcs; i++) {
		polls[*npolls].fd = state->dlcs[i].socket;
		polls[*npolls].events = POLLIN|POLLERR;
		poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
		poll_infos[*npolls].dlc = &state->dlcs[i];
		(*npolls)++;
	}
}

struct switch_state *global_switch_state;

void sighandler(int signal)
{
	global_switch_state->have_to_exit = TRUE;
}

void main_loop(struct switch_state *state)
{
	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls = 0;

	npolls = sizeof(*polls)/sizeof(*polls);
	refresh_polls_list(state, polls, poll_infos, &npolls);

	state->have_to_exit = 0;
	int i;
	int active_calls_cnt = 0;
	do {
		if (poll(polls, npolls, 1000000) < 0) {
			printf("poll error: %s\n",strerror(errno));
			exit(1);
		}

		for(i = 0; i < npolls; i++) {
			if (poll_infos[i].type == POLL_INFO_TYPE_STDIN) {
				if (polls[i].revents & POLLERR) {
					printf("Error on stdin\n");
					break;
				}

				if (polls[i].revents & POLLIN) {
					char line_read[100];
					fgets(line_read, sizeof(line_read), stdin);

					if (line_read[strlen(line_read) - 1] == '\n')
						line_read[strlen(line_read) - 1] = '\0';
					if (line_read[strlen(line_read) - 1] == '\r')
						line_read[strlen(line_read) - 1] = '\0';

					handle_command(state, line_read);

					printf("> ");
				}

			} else if (poll_infos[i].type == POLL_INFO_TYPE_INTERFACE) {
				if (polls[i].revents & POLLERR) {
					printf("Error on interface %s poll\n",
						poll_infos[i].interface->name);
				}

				if (polls[i].revents & POLLIN) {
					printf("New DLC accepted...\n");

					state->dlcs[state->ndlcs].socket =
						accept(polls[i].fd, NULL, 0);
					state->dlcs[state->ndlcs].interface =
						poll_infos[i].interface;
					state->ndlcs++;

					refresh_polls_list(state,
						polls,
						poll_infos, &npolls);
				}
			} else if (poll_infos[i].type == POLL_INFO_TYPE_DLC) {
				if (polls[i].revents & POLLIN ||
				    polls[i].revents & POLLERR) {
					printf("receiving frame...\n");
					q931_receive(poll_infos[i].dlc);
				}
			}
		}

		if (state->have_to_exit) {
			active_calls_cnt = 0;

			for (i=0; i<state->nifs; i++) {
				struct q931_call *call;
				list_for_each_entry(call, &state->ifs[i]->calls, calls_node)
					active_calls_cnt++;
			}
		}
	} while(!state->have_to_exit || active_calls_cnt > 0);
}

int main()
{
//	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	struct switch_state state;
	memset(&state, 0x00, sizeof(state));

	global_switch_state = &state;

	q931_init();

	signal(SIGINT, sighandler);

	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		printf("getifaddr: %s\n", strerror(errno));
		exit(1);
	}

printf("-------------------\n");
printf("> ");

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {

		int fd;
		fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

		struct ifreq ifreq;
		memset(&ifreq, 0x00, sizeof(ifreq));
		strncpy(ifreq.ifr_name, ifaddr->ifa_name, sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		close(fd);

		if (ifreq.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		printf("%s: %s %s\n",
			ifaddr->ifa_name,
			(ifaddr->ifa_flags & IFF_UP) ? "UP": "DOWN",
			(ifaddr->ifa_flags & IFF_ALLMULTI) ? "NT": "TE");

		if (!(ifaddr->ifa_flags & IFF_UP))
			continue;

		printf("OK\n");
		state.ifs[state.nifs] = q931_open_interface(ifaddr->ifa_name);
		if (!state.ifs[state.nifs]) {
			printf("q931_open_interface error: %s\n",strerror(errno));
			exit(1);
		}

		listen(state.ifs[state.nifs]->nt_socket, 100);

		state.nifs++;
	}
	freeifaddrs(ifaddrs);


	main_loop(&state);

	int i;
	for (i = 0; i < state.ndlcs; i++) {
		close(state.dlcs[i].socket);
	}

	for (i = 0; i < state.nifs; i++) {
		q931_close_interface(state.ifs[i]);
	}

	return 0;
}
