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
};

enum frame_type
{
	FRAME_TYPE_IFRAME,
	FRAME_TYPE_UFRAME,
};

struct opts
{
	enum working_mode mode;
	enum frame_type frame_type;
	const char *intf_name;

	int frame_size;
	int interval;

	int socket_debug;
};

void start_loopback(int s, struct opts *opts)
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
			printf("recvmsg: %s\n", strerror(errno));
			exit(1);
		}

		iov.iov_len = len;

		len = sendmsg(s, &msg, 0);
		if(len < 0) {
			printf("sendmsg: %s\n", strerror(errno));
			exit(1);
		}

		int in_size;
		if(ioctl(s, SIOCINQ, &in_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		int out_size;
		if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		printf("R%7d W:%7d - Echo %d\n", in_size, out_size, len);
	}

}

void start_generator(int s, struct opts *opts)
{
	struct msghdr msg;
	struct cmsghdr cmsg;
	struct sockaddr_lapd sal;
	struct iovec iov;
	int flags;

	flags = 0;
	if (opts->frame_type == FRAME_TYPE_UFRAME)
		flags |= MSG_OOB;

	printf("Connecting...");
	if (connect(s, NULL, 0) < 0) {
		printf("connect: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

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

	int i;
	for (i=0;;i++) {
		int len;

		*(__u32 *)frame = htonl(i);

		len = sendmsg(s, &msg, flags);
		if(len < 0) {
			printf("sendmsg: %s\n", strerror(errno));
			exit(1);
		}
	
		int in_size;
		if(ioctl(s, SIOCINQ, &in_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		int out_size;
		if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		printf("R%7d W:%7d - Send %d - #%d\n", in_size, out_size, len, i);

		usleep(opts->interval * 1000);
	}
}

void start_sink(int s, struct opts *opts)
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

	int expected = -1;
	for (;;) {
		int len;
		len = recvmsg(s, &msg, 0);
		if(len < 0) {
			printf("recvmsg: %s\n", strerror(errno));
			exit(1);
		}

		iov.iov_len = len;

		int in_size;
		if(ioctl(s, SIOCINQ, &in_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		int out_size;
		if(ioctl(s, SIOCOUTQ, &out_size) < 0) {
			printf("ioctl: %s\n", strerror(errno));
			exit(1);
		}

		if (expected == -1)
			expected = ntohl(*(__u32 *)frame);

		printf("R%7d W:%7d - Sink %d - #%d",
			in_size, out_size, len,
			ntohl(*(__u32 *)frame));

		if (expected != ntohl(*(__u32 *)frame)) {
			printf(" OUT OF SEQUENCE!");
			expected = ntohl(*(__u32 *)frame);
		}

		printf("\n");

		expected++;
	}

}

void print_usage(const char *progname)
{
	fprintf(stderr,
		"%s: <interface>\n"
		"	--mode (sink|generator|loopback)\n"
		"	[-l|--length <length>]\n"
		"	[-i|--interval <interval>]\n"
		"	[-u|--uframe]\n", progname);
	exit(1);
}

void start_accept_loop(int accept_socket, struct opts *opts)
{
	listen(accept_socket, 10);

	struct pollfd polls;

	polls.fd = accept_socket;
	polls.events = POLLIN|POLLERR;

	for (;;) {
		if (poll(&polls, 1, 0) < 0) {
			printf("poll: %s\n", strerror(errno));
			exit(1);
		}

		if (polls.revents & POLLERR)
			break;

		if (polls.revents & POLLIN) {
			int s;
			s = accept(accept_socket, NULL, 0);

			if (s < 0) {
				printf("accept: %s\n", strerror(errno));
				exit(1);
			}

			int pid;
			pid = fork();

			if (pid > 0) {
				if (opts->mode == MODE_LOOPBACK)
					start_loopback(s, opts);
				else if (opts->mode == MODE_GENERATOR)
					start_generator(s, opts);
				else if (opts->mode == MODE_SINK)
					start_sink(s, opts);
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
			else if (!strcasecmp(optarg,"generator"))
				opts.mode = MODE_GENERATOR;
			else if (!strcasecmp(optarg,"sink"))
				opts.mode = MODE_SINK;
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
			start_loopback(s, &opts);
		else if (opts.mode == MODE_GENERATOR)
			start_generator(s, &opts);
		else if (opts.mode == MODE_SINK)
			start_sink(s, &opts);
	else
		print_usage(argv[0]);


	} else if (role == LAPD_ROLE_NT) {
		printf("NT\n");
		start_accept_loop(s, &opts);
	} else
		printf("Unknown role %d\n", role);

	return 0;
}
