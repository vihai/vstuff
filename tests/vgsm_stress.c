/*
 * vGSM serial stress-test
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
#include <fcntl.h>
#include <assert.h>
#include <poll.h>
#include <ctype.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/wait.h>

#define FALSE 0
#define TRUE (!FALSE)

void print_usage(const char *progname)
{
	fprintf(stderr,
"%s: [options]\n"
"\n"
"	devs:	Number of installed interfaces\n",
		progname);

	exit(1);
}

void sighup(int signal)
{
	exit(1);
}

int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}

char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;

	assert(bufsize);

	buf[0] = '\0';

	while(*c) {

		switch(*c) {
		case '\r':
			sanprintf(buf, bufsize, "<cr>");
		break;
		case '\n':
			sanprintf(buf, bufsize, "<lf>");
		break;

		default:
			if (isprint(*c))
				sanprintf(buf, bufsize, "%c", *c);
			else
				sanprintf(buf, bufsize, "<%02x>",
					*(unsigned char *)c);
		}

		c++;
	}

	return buf;
}


void merror(char *device, const char *text)
{
	fprintf(stderr, "\nDevice: %s %s (%s)\n",
		device, text, strerror(errno));
	exit(1);
};

int do_stuff(char *device)
{
	struct pollfd polls;
	char buf[1024] = "";
	int buflen = 0;

	printf("opening %s\n", device);

	polls.fd = open(device, O_RDWR | O_NONBLOCK);
	if (polls.fd < 0)
		merror(device, "Error opening device\n");

	polls.events = POLLERR | POLLIN | POLLHUP; //POLLOUT | ;

	/* Disable all URCs */
	if (write(polls.fd, "AT&F\r", 5) < 0)
		merror(device, "write(AT&F)");

	sleep(1);

	/* Flush buffer */
	if (read(polls.fd, buf, sizeof(buf)) < 0)
		merror(device, "read");

	if (write(polls.fd, "AT&V\r", 5) < 0)
		merror(device, "write");

	int ncycles = 0;

	while(TRUE) {
		int ret;

		ret = poll(&polls, 1, 5000);

		if (ret < 0) {
			merror(device, "poll");
			return 1;
		} else if (ret == 0) {
			char tmpstr[4096];
			fprintf(stderr, "Buf=%d '%s'\n",
				buflen,
				unprintable_escape(buf, tmpstr,
						sizeof(tmpstr)));
			merror(device, "Timeout");
		}

		if (polls.revents & POLLHUP)
			printf("HUP!\n");

		if (polls.revents & POLLIN) {
			int nread = read(polls.fd, buf + buflen,
					sizeof(buf) - buflen);
			if (nread < 0)
				merror(device, "read");

			buflen += nread;
			buf[buflen] = '\0';

			if (buflen >= sizeof(buf) - 1) {
				char tmpstr[4096];
				fprintf(stderr, "Buf=%d '%s'\n",
					buflen,
					unprintable_escape(buf, tmpstr,
							sizeof(tmpstr)));
				merror(device, "overflow");
			}

			if (strstr(buf, "OK\r\n")) {
				buf[0] = '\0';
				buflen = 0;

				//if (write(polls.fd, "AT&V\r", 5) < 0)
				if (write(polls.fd, "AT\r", 3) < 0)
					merror(device, "write");

/*				ncycles++;
				if (ncycles == (getpid() % 50) + 25) {
					ncycles = 0;

					close(polls.fd);
					polls.fd = open(device, O_RDWR);
					if (polls.fd < 0)
						merror(device, "Error opening"
							" device\n");

					printf(".");
				}*/
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	setvbuf(stdout, (char *)NULL, _IONBF, 0);
	signal(SIGHUP, sighup);

	int devices = -1;

	int c;
	int optidx;

	struct option options[] = {
		{ "devs", required_argument, 0, 0 }
		};

	for(;;) {
		c = getopt_long(argc, argv, "", options,
			&optidx);

		if (c == -1)
			break;

		if (c == 0 &&
		    !strcmp(options[optidx].name, "devs")) {
			devices = atoi(optarg);
		} else {
			if (c)
				fprintf(stderr,"Unknow option -%c\n", c);
			else
				fprintf(stderr,
					"Unknow option %s\n",
					options[optidx].name);

			print_usage(argv[0]);
			return 1;
		}
	}

	char *device;

	while((device = argv[optind++])) {
		int ret;

		ret = fork();

		if (ret == 0)
			return do_stuff(device);
		else if (ret < 0)
			perror("fork");
	}

	int status;
	while(TRUE)
		wait(&status);

	return 0;
}
