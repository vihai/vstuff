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
#include <fcntl.h>
#include <sys/poll.h>
#include <errno.h>

int main()
{
	int fd;
	fd = open("/dev/visdn/timer", O_RDONLY);
	if (!fd) {
		printf("open; %s\n", strerror(errno));
		return 1;
	}

	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	struct pollfd pollfd = { fd, POLLIN, 0 };

	printf("%d\n", time(NULL));

	int i;
	for (i=0; i<1000; i++) {
		if (poll(&pollfd, 1, -1) < 0) {
			printf("poll; %s\n", strerror(errno));
			return 1;
		}

//		printf("%02x\n", pollfd.revents);

//		printf("%d ", i);
	}

	printf("%d\n", time(NULL));
}
