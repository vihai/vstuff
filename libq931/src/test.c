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

#include <lapd_user.h>

void main()
{
 int s = socket(PF_LAPD, SOCK_DGRAM, 0);

 if (s<0)
  {
   printf("socket: %s\n",strerror(errno));
   exit(1);
  }

 char name[] = "fakeisdn0d";
 if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
                name, strlen(name)+1) < 0)
  {
   printf("setsockopt: %s\n",strerror(errno));
   exit(1);
  }

 sleep(5);
}
