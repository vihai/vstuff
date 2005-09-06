/*
 * vISDN
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <linux/types.h>
#include <assert.h>
#include <poll.h>

#include <netinet/in.h>

#include <getopt.h>

#include <lapd.h>

enum working_mode
{
	MODE_GENERATOR,
	MODE_SINK,
	MODE_LOOPBACK,
	MODE_NULL,
};

enum frame_type
{
	FRAME_TYPE_IFRAME,
	FRAME_TYPE_UFRAME,
	FRAME_TYPE_UFRAME_BROADCAST,
};

struct opts
{
	enum working_mode mode;
	enum frame_type frame_type;
	const char *intf_name;

	int frame_size;
	int interval;

	int socket_debug;

	int tei;
};

void send_broadcast(int s, const char *prefix, struct opts *opts)
{
	struct msghdr msg;
	struct cmsghdr cmsg;
	struct sockaddr_lapd sal;
	struct iovec iov;

	__u8 frame[65536];

	iov.iov_base = frame;
	iov.iov_len = opts->frame_size;

	msg.msg_name = &sal;
	msg.msg_namelen = sizeof(sal);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsg;
	msg.msg_controllen = sizeof(cmsg);
	msg.msg_flags = 0;

	memset(frame, 0x5a, sizeof(frame));

	static int frame_seq = 0;

	*(__u32 *)frame = htonl(frame_seq);

	int len;
	len = sendmsg(s, &msg, MSG_OOB);
	if(len < 0) {
		if (errno == ECONNRESET) {
			printf("%sDL-RELEASE-INDICATION\n", prefix);
		} else if (errno == EISCONN) {
			printf("%sDL-ESTABLISH-INDICATION\n", prefix);
		} else {
			printf("%ssendmsg: %s\n", prefix, strerror(errno));
		}

		return;
	}

	int in_size;
	if(ioctl(s, SIOCINQ, &in_size) < 0) {
		printf("%sioctl: %s\n", prefix, strerror(errno));
		exit(1);
	}

	int out_size;
	if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
		printf("%sioctl: %s\n", prefix, strerror(errno));
		exit(1);
	}

	printf("%sO R%7d W:%7d - Broadcast %d\n", prefix, in_size, out_size, len);

	frame_seq++;
}

void start_loopback(int s, const char *prefix, struct opts *opts)
{
	struct msghdr msg;
	struct cmsghdr cmsg;
	struct sockaddr_lapd sal;
	struct iovec iov;

	__u8 frame[65536];

	iov.iov_base = frame;
	iov.iov_len = sizeof(frame);

	msg.msg_name = &sal;
	msg.msg_namelen = sizeof(sal);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsg;
	msg.msg_controllen = sizeof(cmsg);
	msg.msg_flags = 0;

	for (;;) {
		int len;
		len = recvmsg(s, &msg, 0);
		if(len < 0) {
			if (errno == ECONNRESET) {
				printf("%sDL-RELEASE-INDICATION\n", prefix);
				continue;
			} else if (errno == EISCONN) {
				printf("%sDL-ESTABLISH-INDICATION\n", prefix);
				continue;
			} else {
				printf("%srecvmsg: %s\n", prefix, strerror(errno));
				break;
			}
		}

		iov.iov_len = len;

		len = sendmsg(s, &msg, 0);
		if(len < 0) {
			if (errno == ECONNRESET) {
				printf("%sDL-RELEASE-INDICATION\n", prefix);
				continue;
			} else if (errno == EISCONN) {
				printf("%sDL-ESTABLISH-INDICATION\n", prefix);
				continue;
			} else {
				printf("%ssendmsg: %s\n", prefix, strerror(errno));
				break;
			}
		}

		int in_size;
		if(ioctl(s, SIOCINQ, &in_size) < 0) {
			printf("%sioctl: %s\n", prefix, strerror(errno));
			exit(1);
		}

		int out_size;
		if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
			printf("%sioctl: %s\n", prefix, strerror(errno));
			exit(1);
		}

		printf("%sI R%7d W:%7d - Echo %d\n", prefix, in_size, out_size, len);
	}

}

void start_null(int s, const char *prefix, struct opts *opts)
{
	if (opts->frame_type == FRAME_TYPE_IFRAME) {
		printf("%sConnecting...", prefix);
		if (connect(s, NULL, 0) < 0) {
			printf("%sconnect: %s\n", prefix, strerror(errno));
			exit(1);
		}
		printf("OK\n");
	}

	for (;;) {
		int in_size;
		if(ioctl(s, SIOCINQ, &in_size) < 0) {
			printf("%sioctl: %s\n", prefix, strerror(errno));
			exit(1);
		}

		int out_size;
		if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
			printf("%sioctl: %s\n", prefix, strerror(errno));
			exit(1);
		}

		printf("%sR%7d W:%7d\n", prefix, in_size, out_size);

		sleep(1);
	}

}

void start_source(int s, const char *prefix, struct opts *opts)
{
	// Inbound frame

	struct msghdr in_msg;
	struct cmsghdr in_cmsg;
	struct sockaddr_lapd in_sal;
	struct iovec in_iov;
	__u8 in_frame[65536];

	in_iov.iov_base = in_frame;
	in_iov.iov_len = opts->frame_size;

	in_msg.msg_name = &in_sal;
	in_msg.msg_namelen = sizeof(in_sal);
	in_msg.msg_iov = &in_iov;
	in_msg.msg_iovlen = 1;
	in_msg.msg_control = &in_cmsg;
	in_msg.msg_controllen = sizeof(in_cmsg);
	in_msg.msg_flags = 0;

	// Outbound frame

	struct msghdr out_msg;
	struct cmsghdr out_cmsg;
	struct sockaddr_lapd out_sal;
	struct iovec out_iov;
	int out_flags = 0;
	__u8 out_frame[65536];

	out_iov.iov_base = out_frame;
	out_iov.iov_len = opts->frame_size;

	out_msg.msg_name = &out_sal;
	out_msg.msg_namelen = sizeof(out_sal);
	out_msg.msg_iov = &out_iov;
	out_msg.msg_iovlen = 1;
	out_msg.msg_control = &out_cmsg;
	out_msg.msg_controllen = sizeof(out_cmsg);
	out_msg.msg_flags = 0;

	memset(out_frame, 0x5a, sizeof(out_frame));

	if (opts->frame_type == FRAME_TYPE_IFRAME) {
		printf("%sConnecting...", prefix);
		if (connect(s, NULL, 0) < 0) {
			printf("%sconnect: %s\n", prefix, strerror(errno));
			exit(1);
		}
		printf("OK\n");
	} else {
		out_flags |= MSG_OOB;
	}

	struct pollfd polls;

	struct timeval last_tx_tv;
	long long last_tx = 0;

	polls.fd = s;

	int frame_seq;
	for (frame_seq=0;;) {
		struct timeval now_tv;
		gettimeofday(&now_tv, NULL);
		long long now = now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
		long long time_to_wait = opts->interval*1000 - (now - last_tx);

		polls.events = POLLERR;

		if (opts->frame_type != FRAME_TYPE_UFRAME_BROADCAST)
			polls.events |= POLLIN;

		if (time_to_wait < 0) {
			polls.events |= POLLOUT;
			time_to_wait = 0;
		}

		if (poll(&polls, 1, time_to_wait/1000 + 1) < 0) {
			printf("%spoll: %s\n", prefix, strerror(errno));
			exit(1);
		}

		if (polls.revents & POLLHUP)
			break;

		if (polls.revents & POLLIN ||
		    polls.revents & POLLERR) {
			int len;
			len = recvmsg(s, &in_msg, 0);
			if(len < 0) {
				if (errno == ECONNRESET) {
					printf("%sDL-RELEASE-INDICATION\n", prefix);
					break;
				} else if (errno == EISCONN) {
					printf("%sDL-ESTABLISH-INDICATION\n", prefix);
					continue;
				} else {
					printf("%srecvmsg: %s\n", prefix, strerror(errno));
					break;
				}
			}

			int in_size;
			if(ioctl(s, SIOCINQ, &in_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			int out_size;
			if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			gettimeofday(&now_tv, NULL);
			now = now_tv.tv_sec * 1000000LL + now_tv.tv_usec;

			printf("%sI R%7d W:%7d - Sink %d - #%d - %0.3fms",
				prefix,
				in_size, out_size, len,
				ntohl(*(__u32 *)in_frame),
				(now-last_tx)/1000.0);

			if (in_msg.msg_flags & MSG_OOB)
				printf(" U");

			printf("\n");

		}

		if (polls.revents & POLLOUT && time_to_wait <= 0) {
			int len;

			*(__u32 *)out_frame = htonl(frame_seq);

			len = sendmsg(s, &out_msg, out_flags);
			if(len < 0) {
				printf("%ssendmsg: %s\n", prefix, strerror(errno));
				exit(1);
			}

			gettimeofday(&last_tx_tv, NULL);
			last_tx = last_tx_tv.tv_sec * 1000000LL + last_tx_tv.tv_usec;

			int in_size;
			if(ioctl(s, SIOCINQ, &in_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			int out_size;
			if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			printf("%sO R%7d W:%7d - Send %d - #%d\n",
				prefix, in_size, out_size, len, frame_seq);

			frame_seq++;
		}
	}
}

void start_sink(int s, const char *prefix, struct opts *opts)
{
	// Inbound frame

	struct msghdr in_msg;
	struct cmsghdr in_cmsg;
	struct sockaddr_lapd in_sal;
	struct iovec in_iov;
	__u8 in_frame[65536];

	in_iov.iov_base = in_frame;
	in_iov.iov_len = opts->frame_size;

	in_msg.msg_name = &in_sal;
	in_msg.msg_namelen = sizeof(in_sal);
	in_msg.msg_iov = &in_iov;
	in_msg.msg_iovlen = 1;
	in_msg.msg_control = &in_cmsg;
	in_msg.msg_controllen = sizeof(in_cmsg);
	in_msg.msg_flags = 0;

	int i;
	struct pollfd polls;

	polls.fd = s;
	polls.events = POLLIN|POLLERR;

	int expected = -1;
	for (i=0;;i++) {
		if (poll(&polls, 1, -1) < 0) {
			printf("%spoll: %s\n", prefix, strerror(errno));
			exit(1);
		}

		if (polls.revents & POLLHUP)
			break;

		if (polls.revents & POLLIN ||
		    polls.revents & POLLERR) {
			int len;
			len = recvmsg(s, &in_msg, 0);
			if(len < 0) {
				if (errno == ECONNRESET) {
					printf("%sDL-RELEASE-INDICATION\n", prefix);
					continue;
				} else if (errno == EISCONN) {
					printf("%sDL-ESTABLISH-INDICATION\n", prefix);
					continue;
				} else {
					printf("%srecvmsg: %s\n", prefix, strerror(errno));
					break;
				}
			}

			int in_size;
			if(ioctl(s, SIOCINQ, &in_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			int out_size;
			if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
				printf("%sioctl: %s\n", prefix, strerror(errno));
				exit(1);
			}

			if (expected == -1)
				expected = ntohl(*(__u32 *)in_frame);

			printf("%sI R%7d W:%7d - Sink %d - #%d",
				prefix, in_size, out_size, len,
				ntohl(*(__u32 *)in_frame));

			if (expected != ntohl(*(__u32 *)in_frame)) {
				printf(" OUT OF SEQUENCE!");
				expected = ntohl(*(__u32 *)in_frame);
			}

			if (in_msg.msg_flags & MSG_OOB)
				printf(" U");

			printf("\n");

		}

		expected++;
	}

}

void print_usage(const char *progname)
{
	fprintf(stderr,
		"%s: <interface>\n"
		"	--mode (sink|source|loopback)\n"
		"\n"
		"	sink:      Eat frames\n"
		"	source: Generate frames\n"
		"	[-l|--length <length>]      Frame length\n"
		"	[-i|--interval <interval>]  Inter-frame delay interval\n"
		"	[-u|--uframe]               Send U-Frames instead of I-Frames\n"
		"	loopback:  Receive and loopback frames\n"
		"	null:      Doesn't do anything\n"
		"	[-d|--debug]                Enable socket debug mode\n",
		progname);
	exit(1);
}

void start_accept_loop(int accept_socket, struct opts *opts)
{
	listen(accept_socket, 10);

	struct pollfd polls;

	polls.fd = accept_socket;
	polls.events = POLLIN|POLLERR;

	int time_to_wait;

	for (;;) {
		if (opts->frame_type == FRAME_TYPE_UFRAME_BROADCAST) {
			time_to_wait = opts->interval;
			send_broadcast(accept_socket, "", opts);
		} else {
			time_to_wait = -1;
		}

		if (poll(&polls, 1, time_to_wait) < 0) {
			printf("poll: %s\n", strerror(errno));
			exit(1);
		}

		if (polls.revents & POLLHUP ||
		    polls.revents & POLLERR)
			break;

		if (polls.revents & POLLIN) {
			int s;
			s = accept(accept_socket, NULL, 0);

			if (s < 0) {
				printf("accept: %s\n", strerror(errno));
				exit(1);
			}

			int optlen=sizeof(opts->tei);
			if (getsockopt(s, SOL_LAPD, LAPD_TEI,
			    &opts->tei, &optlen)<0) {
				printf("getsockopt: %s\n", strerror(errno));
				exit(1);
			}

			printf("Accepted socket %d\n", opts->tei);

			char prefix[10];
			snprintf(prefix, sizeof(prefix), "%d ", opts->tei);

			int pid;
			pid = fork();

			if (pid > 0) {
				if (opts->mode == MODE_LOOPBACK)
					start_loopback(s, prefix, opts);
				else if (opts->mode == MODE_GENERATOR)
					start_source(s, prefix, opts);
				else if (opts->mode == MODE_SINK)
					start_sink(s, prefix, opts);
				else if (opts->mode == MODE_NULL)
					start_null(s, prefix, opts);
			} else if (pid < 0) {
				printf("fork: %s\n", strerror(errno));
				exit(1);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	struct opts opts;

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	memset(&opts, 0x00, sizeof(opts));
	opts.mode = MODE_SINK;
	opts.frame_type = FRAME_TYPE_IFRAME;
	opts.frame_size = 100;
	opts.interval = 1000;

	struct option options[] = {
		{ "length", required_argument, 0, 0 },
		{ "interval", required_argument, 0, 0 },
		{ "mode", required_argument, 0, 0 },
		{ "uframe", no_argument, 0, 0 },
		{ "bcast", no_argument, 0, 0 },
		{ "debug", no_argument, 0, 0 },
		{ }
	};

	int c;
	int optidx;

	for(;;) {
		c = getopt_long(argc, argv, "l:i:m:ud", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 'l' || (c == 0 &&
		    !strcmp(options[optidx].name, "length"))) {
			opts.frame_size = atoi(optarg);
		} else if (c == 'i' || (c == 0 &&
		    !strcmp(options[optidx].name, "interval"))) {
			opts.interval = atoi(optarg);
		} else if (c == 'm' || (c == 0 &&
		    !strcmp(options[optidx].name, "mode"))) {
			if (!strcasecmp(optarg,"loopback"))
				opts.mode = MODE_LOOPBACK;
			else if (!strcasecmp(optarg,"source"))
				opts.mode = MODE_GENERATOR;
			else if (!strcasecmp(optarg,"sink"))
				opts.mode = MODE_SINK;
			else if (!strcasecmp(optarg,"null"))
				opts.mode = MODE_NULL;
			else {
				fprintf(stderr,"Unknown mode\n");
				print_usage(argv[0]);
			}
		} else if (c == 'd' || (c == 0 &&
		    !strcmp(options[optidx].name, "debug"))) {
			opts.socket_debug = 1;
		} else if (c == 'u' || (c == 0 &&
		    !strcmp(options[optidx].name, "uframe"))) {
			opts.frame_type = FRAME_TYPE_UFRAME;
		} else if (c == 0 &&
		    !strcmp(options[optidx].name, "bcast")) {
			opts.frame_type = FRAME_TYPE_UFRAME_BROADCAST;
		} else {
			fprintf(stderr,"Unknow option %s\n", options[optidx].name);
			print_usage(argv[0]);
			return 1;
		}
	}

	if (optind < argc) {
		opts.intf_name = argv[optind];
	} else {
		fprintf(stderr,"Missing required interface name\n");
		print_usage(argv[0]);
	}

	printf("Opening socket... ");
	int s = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (s < 0) {
		printf("socket: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	if (opts.socket_debug) {
		int on=1;

		printf("Putting socket in debug mode... ");
		if (setsockopt(s, SOL_SOCKET, SO_DEBUG,
			             &on, sizeof(on)) < 0) {
			printf("setsockopt: %s\n", strerror(errno));
			exit(1);
		}
		printf("OK\n");
	}

	printf("Binding to %s... ", opts.intf_name);
	if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
		             opts.intf_name, strlen(opts.intf_name)+1) < 0) {
		printf("setsockopt: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	int role;
	int optlen=sizeof(role);
	if (getsockopt(s, SOL_LAPD, LAPD_ROLE,
	    &role, &optlen)<0) {
		printf("getsockopt: %s\n", strerror(errno));
		exit(1);
	}

	printf("Role... ");
	if (role == LAPD_ROLE_TE) {
		printf("TE\n");

		if (opts.mode == MODE_LOOPBACK)
			start_loopback(s, "", &opts);
		else if (opts.mode == MODE_GENERATOR)
			start_source(s, "", &opts);
		else if (opts.mode == MODE_SINK)
			start_sink(s, "", &opts);
		else if (opts.mode == MODE_NULL)
			start_null(s, "", &opts);
		else
			print_usage(argv[0]);

	} else if (role == LAPD_ROLE_NT) {
		printf("NT\n");
		start_accept_loop(s, &opts);
	} else
		printf("Unknown role %d\n", role);

	return 0;
}
