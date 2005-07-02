#include <stdio.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <errno.h>

int main()
{
	int fd;
	fd = open("visdntimer", O_RDONLY);
	if (!fd) {
		printf("open; %s\n", strerror(errno));
		return 1;
	}

	struct pollfd pollfd = { fd, POLLIN, 0 };

	printf("%d\n", time(NULL));

	int i;
	for (i=0; i<1000; i++) {
		if (poll(&pollfd, 1, -1) < 0) {
			printf("open; %s\n", strerror(errno));
			return 1;
		}
	}

	printf("%d\n", time(NULL));
}
