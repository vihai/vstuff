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

#include "list.h"

#include "q931.h"
#include "q931_mt.h"
#include "q931_ie.h"

void q931_init()
{
 q931_ie_infos_init();
 q931_message_types_init();
}

struct q931_interface *q931_open_interface(const char *name)
{
 struct q931_interface *interface;

 assert(name);

 interface = malloc(sizeof(*interface));
 if (!interface) abort();
 memset(interface, 0x00, sizeof(*interface));

 INIT_LIST_HEAD(&interface->calls);

 interface->next_call_reference = 1;
 interface->call_reference_size = 1; // FIXME should be 1 for BRI, 2 for PRI

 interface->socket = socket(PF_LAPD, SOCK_DGRAM, 0);
 if (interface->socket < 0)
   goto err_socket;

 if (setsockopt(interface->socket, SOL_LAPD, SO_BINDTODEVICE,
                name, strlen(name)+1) < 0)
   goto err_setsockopt;

 int optlen=sizeof(interface->role);
 if (getsockopt(interface->socket, SOL_LAPD, LAPD_ROLE,
		&interface->role, &optlen)<0)
   goto err_getsockopt;

// if (connect(interface->socket, NULL, 0) < 0)
//   goto err_connect;

 return interface;

err_connect:
err_getsockopt:
err_setsockopt:
 close(interface->socket);
err_socket:
 free(interface);

 return NULL;
}

void q931_close_interface(struct q931_interface *interface)
{
 assert(interface);

 assert(interface->socket >= 0);

 shutdown(interface->socket, 0);
 close(interface->socket);

 free(interface);
}

struct q931_call *q931_alloc_call(enum q931_call_direction direction)
{
 struct q931_call *call;

 assert(direction == Q931_CALL_DIRECTION_INBOUND ||
        direction == Q931_CALL_DIRECTION_OUTBOUND);

 call = malloc(sizeof(*call));
 if (!call) abort();
 memset(call, 0x00, sizeof(*call));

 INIT_LIST_HEAD(&call->node);

 return call;
}

void q931_free_call(struct q931_call *call)
{
 assert(call);

 list_del(&call->node);

 free(call);
}

static q931_callref q931_alloc_call_reference(struct q931_interface *interface)
{
 q931_callref call_reference;
 q931_callref first_call_reference;

 first_call_reference = interface->next_call_reference;

try_again:

 call_reference = interface->next_call_reference;

 interface->next_call_reference++;

 if (interface->next_call_reference >=
     (1 << ((interface->call_reference_size * 8) - 1)))
   interface->next_call_reference = 1;

 struct q931_call *call;
 list_for_each_entry(call, &interface->calls, node)
  {
   if (call->direction == Q931_CALL_DIRECTION_OUTBOUND &&
       call->call_reference == call_reference)
    {
     if (call_reference == interface->next_call_reference)
       return -1;
     else
       goto try_again;
    }
  }

 return call_reference;
}

static struct q931_call *q931_find_call_by_reference(
	struct q931_interface *interface,
	enum q931_call_direction direction,
	q931_callref call_reference)
{
 struct q931_call *call;
 list_for_each_entry(call, &interface->calls, node)
  {
   if (call->direction == direction &&
       call->call_reference == call_reference)
    {
     return call;
    }
  }

 return NULL;
}

static int q931_prepare_header(const struct q931_call *call,
	__u8 *frame,
	__u8 message_type)
{
 int size = 0;

 // Header

 struct q931_header *hdr = (struct q931_header *)(frame + size);
 size += sizeof(struct q931_header);

 memset(hdr, 0x00, sizeof(*hdr));

 hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

 assert(call->interface);
 assert(call->interface->call_reference_size >=1 &&
        call->interface->call_reference_size <= 4);
 hdr->call_reference_size = call->interface->call_reference_size;

 // Call reference
 assert(call->call_reference >= 0 &&
        call->call_reference < ((call->interface->call_reference_size * 8) - 1));

 union q931_callref_onwire call_reference;
 call_reference.longval = call->call_reference;
 call_reference.direction = call->direction;

 int i;
 for (i=0 ; i<call->interface->call_reference_size ; i++)
  {
#if __BYTE_ORDER == __BIG_ENDIAN
   *(frame + size++) = call_reference.octets[i];
#elif __BYTE_ORDER == __LITTLE_ENDIAN
   *(frame + size++) =
     call_reference.octets[call->interface->call_reference_size-1-i];
#endif
  }
 
 __u8 *message_type_onwire = (__u8 *)(frame + size);
 size++;
 *message_type_onwire = message_type;

 return size;
}

static int q931_send_frame(struct q931_interface *interface, void *frame, int size)
{
 struct msghdr msg;
 struct iovec iov;
 msg.msg_name = NULL;
 msg.msg_namelen = 0;
 msg.msg_iov = &iov;
 msg.msg_iovlen = 1;
 msg.msg_control = NULL;
 msg.msg_controllen = 0;
 msg.msg_flags = 0;

 iov.iov_base = frame;
 iov.iov_len = size;

 printf("q931_send_frame\n");

 if (sendmsg(interface->socket, &msg, 0) < 0)
  {
   printf("sendmsg error: %s\n",strerror(errno));
   return errno;
  }

 return 0;
}

static int q931_send_uframe(struct q931_interface *interface, void *frame, int size)
{
 struct msghdr msg;
 struct iovec iov;
 struct sockaddr_lapd sal;

 msg.msg_name = &sal;
 msg.msg_namelen = sizeof(sal);
 msg.msg_iov = &iov;
 msg.msg_iovlen = 1;
 msg.msg_control = NULL;
 msg.msg_controllen = 0;
 msg.msg_flags = 0;

 sal.sal_tei = 127;

 iov.iov_base = frame;
 iov.iov_len = size;

 printf("q931_send_uframe\n");

 if (sendmsg(interface->socket, &msg, MSG_OOB) < 0)
  {
   printf("sendmsg error: %s\n",strerror(errno));
   return errno;
  }

 return 0;
}

static int q931_send_connect_acknowledge(struct q931_call *call)
{
 int size = 0;
 __u8 frame[260]; // FIXME

 size += q931_prepare_header(call, frame, Q931_MT_CONNECT_ACKNOWLEDGE);

 /* IEs:
  *
  * Information Element		Dir.	Type
  * Channel Identification	n->u	O
  * Display			n->u	O
  *
  */

 return q931_send_frame(call->interface, frame, size);
}

static int q931_send_release_complete(struct q931_call *call)
{
 int size = 0;
 __u8 frame[260]; // FIXME

 size += q931_prepare_header(call, frame, Q931_MT_RELEASE_COMPLETE);

 /* IEs:
  *
  * Information Element		Dir.	Type
  * Cause			both	O
  * Facility			both	O
  * Display			n->u	O
  * User-User			u->n	O
  *
  */

// size += q931_append_ie_cause(frame + size,
//           Q931_IE_C_V_ );

 return q931_send_frame(call->interface, frame, size);
}

static int q931_send_setup(struct q931_call *call)
{
 int size = 0;
 __u8 frame[260]; // FIXME

 size += q931_prepare_header(call, frame, Q931_MT_SETUP);

 /* IEs:
  *
  * Information Element		Dir.	Type
  * Sending Complete		both	O
  * Bearer Capability		both	M
  * Channel Identification	both	O
  * Facility			both	O
  * Progress Indicator		both	O
  * Net. Spec. Facilities	both	O
  * Display			n->u	O
  * Keypad Facility		u->n	O
  * Calling Party Number	both	O
  * Calling Party Subaddress	both	O
  * Called Party Number		both	O
  * Called Party Subaddress	both	O
  * Transit Network Selection	u->n	O
  * Low Layer Compatibility	both	O
  * High Layer Compatibility	both	O
  * User-User			both	O
  *
  */

 size += q931_append_ie_bearer_capability_alaw(frame + size);
// size += q931_append_ie_channel_identification_any(frame + size);
 size += q931_append_ie_calling_party_number(frame + size, "5005");
 size += q931_append_ie_called_party_number(frame + size, "5001");
 size += q931_append_ie_sending_complete(frame + size);

// if (call->interface->role == LAPD_ROLE_TE)
//   return q931_send_frame(call->interface, frame, size);
// else
   return q931_send_uframe(call->interface, frame, size);
}

int q931_make_call(struct q931_interface *interface, struct q931_call *call)
{
 assert(!call->interface);
 call->interface = interface;

 call->direction = Q931_CALL_DIRECTION_OUTBOUND;

 call->call_reference = q931_alloc_call_reference(interface);

 if (call->call_reference < 0)
  {
   printf("All call references are used!!!\n");
   return -1;
  }

 printf("Call reference allocated (%ld)\n", call->call_reference);

 list_add_tail(&call->node, &interface->calls);

 q931_send_setup(call);
 call->user_status = U1_CALL_INITIATED;

 // If we know all channels are busy we should not send SETUP (5.1)


 return 0;
}

void q931_receive(struct q931_interface *interface)
{
 struct msghdr msg;
 struct sockaddr_lapd sal;
 struct cmsghdr cmsg;
 struct iovec iov;

 q931_init();

 __u8 frame[512];

 iov.iov_base = frame;
 iov.iov_len = sizeof(frame);

 msg.msg_name = &sal;
 msg.msg_namelen = sizeof(sal);
 msg.msg_iov = &iov;
 msg.msg_iovlen = 1;
 msg.msg_control = &cmsg;
 msg.msg_controllen = sizeof(cmsg);
 msg.msg_flags = 0;

 int len;
 len = recvmsg(interface->socket, &msg, 0);
 if(len < 0)
  {
   printf("recvmsg: %d %s\n",errno,strerror(errno));
   exit(1);
  }

 printf("recv ok (len=%d): ", len);
 int i;
 for(i=0; i<len; i++)
   printf("%02x",frame[i]);
 printf("\n");

 struct q931_header *hdr = (struct q931_header *)frame;

 if (hdr->call_reference_size>3)
  {
   // TODO error
   printf("Call reference length > 3 ????\n");
   return;
  }

 union q931_callref_onwire call_reference;

 call_reference.longval = 0;

 for(i=0; i<hdr->call_reference_size; i++)
  {
   // LITTLE ENDIAN
   call_reference.octets[i] = hdr->call_reference[i];

   // BIG ENDIAN
//   call_reference.octets[i] = hdr->data[call_reference_size - 1 - i];

   if (i==0 && call_reference.octets[i] & 0x80)
    {
     call_reference.octets[i] &= 0x7f;
    }
  }

 printf("  protocol descriptor = %u\n", hdr->protocol_discriminator);
 printf("  call reference = %u %lu %c\n",
   hdr->call_reference_size,
   call_reference.longval,
   call_reference.direction?'O':'I');

 struct q931_call *call =
   q931_find_call_by_reference(
     interface,
     call_reference.direction,
     call_reference.longval);

 __u8 message_type = *(__u8 *)(frame + sizeof(struct q931_header) + hdr->call_reference_size);

 printf("  message_type = %s (%u)\n",
   q931_get_message_type_name(message_type),
   message_type);

 int curpos= sizeof(struct q931_header) + hdr->call_reference_size + 1;
 int codeset = 0;
 int codeset_locked = FALSE;

 i=0;
 while(curpos < len)
  {
   __u8 first_octet = *(__u8 *)(frame + curpos);
   curpos++;

   if (q931_is_so_ie(first_octet))
    {
     // Single octet IE

     if (q931_get_so_ie_id(first_octet) == Q931_IE_SHIFT)
      {
       if (q931_get_so_ie_type2_value(first_octet) & 0x08)
        {
         // Locking shift

         printf("Locked Switch from codeset %u to codeset %u",
           codeset, q931_get_so_ie_type2_value(first_octet) & 0x07);

         codeset = q931_get_so_ie_type2_value(first_octet) & 0x07;
         codeset_locked = FALSE;
         continue;
        }
       else
        {
         // Non-Locking shift

         printf("Non-Locked Switch from codeset %u to codeset %u",
           codeset, q931_get_so_ie_type2_value(first_octet));

         codeset = q931_get_so_ie_type2_value(first_octet);
         codeset_locked = TRUE;
         continue;
        }
      }

     if (codeset == 0)
      {
       const struct q931_ie_info *ie_info =
                q931_get_ie_info(first_octet);

       if (ie_info)
        {
         printf("SO IE %d ===> %u (%s)\n", i,
           first_octet,
           ie_info->name);

//       q931_add_ie(packet);
        }
       else
        {
         printf("SO IE %d ===> %u (unknown)\n", i,
           first_octet);
        }
      }
    }
   else
    {
     // Variable Length IE

     __u8 ie_len = *(__u8 *)(frame + curpos);
     curpos++;

     if (codeset == 0)
      {
       const struct q931_ie_info *ie_info =
                q931_get_ie_info(first_octet);

       if (ie_info)
        {
         printf("VS IE %d ===> %u (%s) -- length %u\n", i,
           first_octet,
           ie_info->name,
           ie_len);

         if (first_octet == Q931_IE_PROGRESS_INDICATOR)
          {
           struct q931_ie_progress_indicator_onwire_3_4 *ie =
             (struct q931_ie_progress_indicator_onwire_3_4 *)(frame + curpos);

           printf("Progress indicator:\n");
           printf("  Coding standard = %u\n", ie->coding_standard);
           printf("  Location        = %u\n", ie->location);
           printf("  Progress descr. = %u\n", ie->progress_description);
          }
        }
       else
        {
         printf("VS IE %d ===> %u (unknown) -- length %u\n", i,
           first_octet,
           ie_len);
        }
      }

     // q931_add_ie(packet);

     curpos += ie_len;

     if(curpos > len) 
      {
       printf("MALFORMED FRAME\n");
       break;
      }
    }

   if (!codeset_locked) codeset = 0;

   i++;
  }

 switch (message_type)
  {
   case Q931_MT_ALERTING:
     
   break;

   case Q931_MT_CALL_PROCEEDING:
     call->user_status = U3_OUTGOING_CALL_PROCEEDING;
   break;

   case Q931_MT_CONNECT:
     call->user_status = U10_ACTIVE;
     q931_send_connect_acknowledge(call);
   break;

   case Q931_MT_CONNECT_ACKNOWLEDGE:
   break;

   case Q931_MT_PROGRESS:
   break;

   case Q931_MT_SETUP:
   break;

   case Q931_MT_SETUP_ACKNOWLEDGE:
     call->user_status = U2_OVERLAP_SENDING;
   break;

   case Q931_MT_DISCONNECT:
   break;

   case Q931_MT_RELEASE:
     q931_send_release_complete(call);
     // Release B chans
     // Relase call reference
     call->user_status = U0_NULL_STATE;
   break;

   case Q931_MT_RELEASE_COMPLETE:
   break;

   case Q931_MT_RESTART:
   break;

   case Q931_MT_RESTART_ACKNOWLEDGE:
   break;


   case Q931_MT_STATUS:
   break;

   case Q931_MT_STATUS_ENQUIRY:
   break;

   case Q931_MT_USER_INFORMATION:
   break;

   case Q931_MT_SEGMENT:
   break;

   case Q931_MT_CONGESTION_CONTROL:
   break;

   case Q931_MT_INFORMATION:
   break;

   case Q931_MT_FACILITY:
   break;

   case Q931_MT_NOTIFY:
   break;


   case Q931_MT_HOLD:
   break;

   case Q931_MT_HOLD_ACKNOWLEDGE:
   break;

   case Q931_MT_HOLD_REJECT:
   break;

   case Q931_MT_RETRIEVE:
   break;

   case Q931_MT_RETRIEVE_ACKNOWLEDGE:
   break;

   case Q931_MT_RETRIEVE_REJECT:
   break;

   case Q931_MT_RESUME:
   break;

   case Q931_MT_RESUME_ACKNOWLEDGE:
   break;

   case Q931_MT_RESUME_REJECT:
   break;

   case Q931_MT_SUSPEND:
   break;

   case Q931_MT_SUSPEND_ACKNOWLEDGE:
   break;

   case Q931_MT_SUSPEND_REJECT:
   break;

  }
}
