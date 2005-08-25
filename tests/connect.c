#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#include <visdn.h>

int main()
{
	int fd;
	fd = open("/dev/visdn/control", O_RDWR);
	if (fd < 0) {
		printf("open: %s\n", strerror(errno));
		return 1;
	}

	struct visdn_connect conn;
	strcpy(conn.src_chanid, "1.0");
	strcpy(conn.dst_chanid, "2.B1");

	if (ioctl(fd, VISDN_IOC_CONNECT, &conn) < 0) {
		printf("ioctl: %s\n", strerror(errno));
		return 1;
	}
}
