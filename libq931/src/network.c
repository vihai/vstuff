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

#include <lapd_user.h>

#include "q931.h"
#include "q931_mt.h"
#include "q931_ie.h"


int main()
{
 q931_init();
 struct q931_interface *interface = q931_open_interface("fakeisdn1d");
 if (!interface)
  {
   printf("q931_open_interface error: %s\n",strerror(errno));
   exit(1);
  }

 listen(interface->nt_socket, 100);

 int npolls = 1;
 struct pollfd polls[100];
 polls[0].fd = interface->nt_socket;
 polls[0].events = POLLIN|POLLERR;

 struct q931_dlc dlcs[100];

 while(1)
  {
   printf("poll()ing...\n");
   poll(polls, npolls, 1000000);

   if (polls[0].revents & POLLIN)
    {
     printf("New DLC accepted...\n");
     polls[npolls].fd = accept(polls[0].fd, NULL, 0);
     polls[npolls].events = POLLIN|POLLERR;
     dlcs[npolls].socket = polls[npolls].fd;
     dlcs[npolls].interface = interface;

     npolls++;

     continue;
    }

   int i;
   for (i=1; i<npolls; i++)
    {
     if (polls[i].revents & POLLIN)
      {
       printf("receiving frame...\n");
       q931_receive(&dlcs[i]);
      }
    }
  }

 q931_close_interface(interface);

 return 0;
}
