#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include <lapd_user.h>

#include "q931.h"
#include "q931_mt.h"
#include "q931_ie.h"


int main()
{
 q931_init();
 struct q931_interface *interface = q931_open_interface("isdn0d");

 struct q931_call *call;

 call = q931_alloc_call(Q931_CALL_DIRECTION_OUTBOUND);
 q931_make_call(interface, call);

 while(1)
  {
   fd_set read_fds;
   struct timeval tv;
   int retval;

   FD_ZERO(&read_fds);
   FD_SET(interface->socket, &read_fds);

   tv.tv_sec = 0;
   tv.tv_usec = 0;

/*   tv.tv_sec = 0;
   tv.tv_usec = 500000;

   printf("queue =");

   if(ioctl(s, TIOCINQ, &size)<0)
    {
     printf("%s\n",strerror(errno));
     exit(1);
    }
   printf(" %d",size);

   if(ioctl(s, TIOCOUTQ, &size)<0)
    {
     printf("%s\n",strerror(errno));
     exit(1);
    }
   printf(" %d\n",size);*/

   if((retval = select(interface->socket+1, &read_fds, NULL, NULL, &tv))<0)
    {
     printf("select error: %s\n",strerror(errno));
     exit(1);
    }
   else if(retval)
    {
     if (FD_ISSET(interface->socket, &read_fds)) q931_receive(interface);
    }
  }

 q931_free_call(call);
 q931_close_interface(interface);

/*
 if(ioctl(s, TIOCOUTQ, &size)<0)
  {
   printf("ioctl error: %s\n",strerror(errno));
   exit(1);
  }
 printf("ioctl(TIOCOUTQ) success: %d\n",size);

 if(ioctl(s, TIOCINQ, &size)<0)
  {
   printf("ioctl error: %d %s\n",errno,strerror(errno));
   exit(1);
  }
 printf("ioctl(TIOCINQ) success: %d\n",size);


 printf("shutdown... ");
 if(shutdown(s, 0) < 0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("ok\n");

 close(s);
*/

/*
 printf("getsockopt(SO_BINDTODEVICE)... ");
 socklen_t devnamesize = sizeof(devname);
 if(getsockopt(s, SOL_LAPD, SO_BINDTODEVICE, devname, &devnamesize)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("%s\n",devname);

 int optval,optlen;

 printf("getsockopt(LAPD_ROLE)... ");
 optlen=sizeof(optval);
 if(getsockopt(s, SOL_LAPD, LAPD_ROLE, &optval, &optlen)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 if(optlen != sizeof(int)) { printf("uh?\n"); exit(1); }
 printf("%d\n", optval);

 printf("getsockopt(LAPD_TE_STATUS)... ");
 optlen=sizeof(optval);
 if(getsockopt(s, SOL_LAPD, LAPD_TE_STATUS, &optval, &optlen)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 if(optlen != sizeof(int)) { printf("uh?\n"); exit(1); }
 printf("%d\n", optval);

 printf("getsockopt(LAPD_TE_TEI)... ");
 optlen=sizeof(optval);
 if(getsockopt(s, SOL_LAPD, LAPD_TE_TEI, &optval, &optlen)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 if(optlen != sizeof(int)) { printf("uh?\n"); exit(1); }
 printf("%d\n", optval);

 printf("ioctl(TIOCOUTQ)... ");
 int size;
 if(ioctl(s, TIOCOUTQ, &size)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("%d\n",size);

 printf("ioctl(TIOCINQ)... ");
 if(ioctl(s, TIOCINQ, &size)<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("%d\n",size);

 printf("connect... ");
 if (connect(s, NULL, 0) < 0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("ok\n");

 int len;
 __u8 frame[] = { 0x08, 0x01, 0x03, 0x05, 0x04, 0x03, 0x80, 0x90, 0xa3, 0x18,
		  0x01, 0x83, 0x6c, 0x0b, 0x21, 0x81, 0x33, 0x36, 0x32, 0x35,
		  0x31, 0x36, 0x31, 0x33, 0x38, 0x70, 0x05, 0x80, 0x35, 0x30,
		  0x30, 0x31, 0xa1 };

 printf("send... ");
 if((len=send(s, frame, sizeof(frame), 0))<0)
  {
   printf("%s\n",strerror(errno));
   exit(1);
  }
 printf("ok (%d)\n",len);
*/

 return 0;
}
