#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <poll.h>

#include <getopt.h>

#include <lapd.h>

void start_loopback(int s)
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

		printf("R%4d W:%4d - Echo %d\n", in_size, out_size, len);
	}

}

void start_generator(int s)
{
	struct msghdr msg;
	struct cmsghdr cmsg;
	struct sockaddr_lapd sal;
	struct iovec iov;

	if (connect(s, NULL, 0) < 0) {
		printf("connect: %s\n", strerror(errno));
		exit(1);
	}

	__u8 frame[65536];

	iov.iov_base = frame;
	iov.iov_len = 2000;

	msg.msg_name = &sal;
	msg.msg_namelen = sizeof(sal);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsg;
	msg.msg_controllen = sizeof(cmsg);
	msg.msg_flags = 0;

	memset(frame, 0x5a, sizeof(frame));

	for (;;) {
		int len;

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

		printf("R%4d W:%4d - Send %d\n", in_size, out_size, len);

		usleep(1000000);
	}
}

void print_usage(const char *progname)
{
	printf("%s: (--generator|-g|--loopback|-l)\n", progname);
	exit(1);
}

void start_accept_loop(int accept_socket)
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
				start_loopback(s);
			} else if (pid < 0) {
				printf("fork: %s\n", strerror(errno));
				exit(1);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	const char *intf_name = NULL;
	int loopback_mode = 0;
	int generator_mode = 0;

	struct option options[] = {
		{ "interface", required_argument, 0, 0 },
		{ "loopback", no_argument, 0, 0 },
		{ "generator", no_argument, 0, 0 },
		{ }
	};

	int c;
	int optidx;

	for(;;) {
		c = getopt_long(argc, argv, "i:slg", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 'i' || (c == 0 &&
		    !strcmp(options[optidx].name, "interface"))) {
			intf_name = optarg;
		} else if (c == 'l' || (c == 0 &&
		    !strcmp(options[optidx].name, "loopback"))) {
			loopback_mode = 1;
		} else if (c == 'g' || (c == 0 &&
		    !strcmp(options[optidx].name, "generator"))) {
			generator_mode = 1;
		} else {
			printf("Unknow option %s\n", options[optidx].name);
			return 1;
		}
	}

	if (optind < argc) {
		printf ("non-option ARGV-elements: ");

		while (optind < argc)
			printf ("%s ", argv[optind++]);

		printf ("\n");
	}

	if (!intf_name)
		print_usage(argv[0]);

	printf("Opening socket... ");
	int s = socket(PF_LAPD, SOCK_DGRAM, 0);
	if (s < 0) {
		printf("socket: %s\n", strerror(errno));
		exit(1);
	}
	printf("OK\n");

	printf("Binding to %s... ", intf_name);
	if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
		             intf_name, strlen(intf_name)+1) < 0) {
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
	} else if (role == LAPD_ROLE_NT) {
		printf("NT\n");
		start_accept_loop(s);
	} else
		printf("Unknown role %d\n", role);

	if (loopback_mode)
		start_loopback(s);
	else if(generator_mode)
		start_generator(s);
	else
		print_usage(argv[0]);

	return 0;
}
