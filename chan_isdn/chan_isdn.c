/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ISDN Linux Telephony Interface driver
 * 
 * Copyright 2004 Lele Forzani <lele@windmill.it>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <string.h>
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <lapd_user.h>

#include <q931.h>
#include <q931_mt.h>
#include <q931_ie.h>

static char *desc = "ISDN Channel For Asterisk";
static char *type = "ISDN";
static char *tdesc = "ISDN Channel Implementation";

static int usecnt =0;

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char context[AST_MAX_EXTENSION] = "default";

AST_MUTEX_DEFINE_STATIC(manager_lock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t q931_thread = AST_PTHREADT_NULL;

/* NBS creates private structures on demand */
   
struct isdn_chan {
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	struct ast_frame fr;			/* "null" frame */

	struct q931_call *call;
};

struct manager_state
{
	int have_to_exit;

	struct q931_interface *ifs[100];
	int nifs;

	struct q931_dlc dlcs[100];
	int ndlcs;
} manager;

enum poll_info_type
{
	POLL_INFO_TYPE_INTERFACE,
	POLL_INFO_TYPE_DLC,
};

struct poll_info
{
	enum poll_info_type type;
	union
	{
		struct q931_interface *interface;
		struct q931_dlc *dlc;
	};
};


static int isdn_devicestate(void *data)
{
	int res = AST_DEVICE_INVALID;

	// not sure what this should do xxx
	res = AST_DEVICE_UNKNOWN;
	return res;
}

static inline struct ast_channel *callpvt_to_astchan(
	struct q931_call *call)
{
	return (struct ast_channel *)call->pvt;
}

static void isdn_event_alerting(struct q931_call *call)
{
	struct ast_channel *ast_chan = callpvt_to_astchan(call);

	ast_setstate(ast_chan, AST_STATE_RINGING);
}

static void isdn_event_release(struct q931_call *call)
{
	struct ast_channel *ast_chan = callpvt_to_astchan(call);

	ast_chan->_softhangup |= AST_SOFTHANGUP_DEV;
}

static void isdn_event_connect(struct q931_call *call)
{
	struct ast_channel *ast_chan = callpvt_to_astchan(call);

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
}

static int isdn_call(struct ast_channel *ast_chan, char *orig_dest, int timeout)
{
	struct isdn_chan *isdn_chan = ast_chan->pvt->pvt;


	char dest[256];
	strncpy(dest, orig_dest, sizeof(dest));

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"isdn_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		return -1;
	}
	
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	
	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	const char *intf_name;
	const char *number;

	{
	char *stringp = dest;

	intf_name = strsep(&stringp, "/");
	if (!intf_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		return -1;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		return -1;
	}
	}

	struct q931_interface *intf = NULL;

	int i;
	for (i=0; i<manager.nifs; i++) {
		if (!strncmp(manager.ifs[i]->name, intf_name, strlen(intf_name))) {
			intf = manager.ifs[i];
			break;
		}
	}

	if (!intf) {
		ast_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		return -1;
	}

	struct q931_call *call;
	call = q931_alloc_call();

	if (ast_chan->callerid) {

		char callerid[255];
		const char *name, *number;

		strncpy(callerid, ast_chan->callerid, sizeof(callerid));
		ast_callerid_parse(callerid, &name, &number);

		if (number)
			q931_call_set_calling_number(call, number);
		else
			ast_log(LOG_WARNING,
				"Unable to parse '%s'"
				" into CallerID name & number\n",
				callerid);
	}

	q931_call_set_called_number(call, number);

	call->pvt = ast_chan;
	call->alerting_callback = isdn_event_alerting;
	call->release_callback = isdn_event_release;
	call->connect_callback = isdn_event_connect;

	q931_make_call(intf, call);

	isdn_chan->call = call;

//	if (0) {
//		ast_log(LOG_WARNING, "NBS Connection failed on %s\n", ast_chan->name);
//		ast_queue_control(ast_chan, AST_CONTROL_CONGESTION);
//	} else {
//	}

	return 0;
}

static void isdn_destroy(struct isdn_chan *isdn_chan)
{
	free(isdn_chan);
}

static struct isdn_chan *isdn_alloc()
{
	struct isdn_chan *isdn_chan;

	isdn_chan = malloc(sizeof(struct isdn_chan));
	if (!isdn_chan)
		return NULL;

	memset(isdn_chan, 0x00, sizeof(struct isdn_chan));

	return isdn_chan;
}

static int isdn_hangup(struct ast_channel *ast_chan)
{
	struct isdn_chan *isdn_chan = ast_chan->pvt->pvt;

	if (option_debug)
		ast_log(LOG_DEBUG, "isdn_hangup(%s)\n", ast_chan->name);

	if (!isdn_chan) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	q931_hangup_call(isdn_chan->call);

	isdn_chan->call->pvt = NULL;

	isdn_destroy(isdn_chan);

	ast_chan->pvt->pvt = NULL;
	ast_setstate(ast_chan, AST_STATE_DOWN);

	return 0;
}

static struct ast_frame *isdn_read(struct ast_channel *ast_chan)
{
	struct isdn_chan *isdn_chan = ast_chan->pvt->pvt;
	
	/* Some nice norms */
	isdn_chan->fr.datalen = 0;
	isdn_chan->fr.samples = 0;
	isdn_chan->fr.data =  NULL;
	isdn_chan->fr.src = type;
	isdn_chan->fr.offset = 0;
	isdn_chan->fr.mallocd=0;
	isdn_chan->fr.delivery.tv_sec = 0;
	isdn_chan->fr.delivery.tv_usec = 0;

	ast_log(LOG_DEBUG, "Returning null frame on %s\n", ast_chan->name);

	return &isdn_chan->fr;
}

static int isdn_write(struct ast_channel *ast_chan, struct ast_frame *frame)
{
	struct isdn_chan *isdn_chan = ast_chan->pvt->pvt;
	/* Write a frame of (presumably voice) data */
	
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with  frame type '%d'\n", frame->frametype);
		return 0;
	}
	if (!(frame->subclass &
		(AST_FORMAT_SLINEAR))) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return 0;
	}
	if (ast_chan->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	
	// WRITE THE FRAME
		
	return 0;
}

static struct ast_channel *isdn_new(struct isdn_chan *isdn_chan, int state)
{
	struct ast_channel *ast_chan;

	ast_chan = ast_channel_alloc(1);
	if (ast_chan) {
		snprintf(ast_chan->name, sizeof(ast_chan->name), "ISDN/null");
		ast_chan->type = type;
		ast_chan->fds[0] = 0;

		ast_chan->nativeformats = prefformat;
		ast_chan->pvt->rawreadformat = prefformat;
		ast_chan->pvt->rawwriteformat = prefformat;
		ast_chan->writeformat = prefformat;
		ast_chan->readformat = prefformat;

		ast_setstate(ast_chan, state);

		if (state == AST_STATE_RING)
			ast_chan->rings = 1;

		ast_chan->pvt->pvt = isdn_chan;
		ast_chan->pvt->call = isdn_call;
		ast_chan->pvt->hangup = isdn_hangup;
		ast_chan->pvt->read = isdn_read;
		ast_chan->pvt->write = isdn_write;

		strncpy(ast_chan->context, context, sizeof(ast_chan->context)-1);
		strncpy(ast_chan->exten, "s",  sizeof(ast_chan->exten) - 1);

		ast_chan->language[0] = '\0';

		isdn_chan->owner = ast_chan;

		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);

		ast_update_use_count();

		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);
			}
		}
	} else {
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	}

	return ast_chan;
}


static struct ast_channel *isdn_request(char *type, int format, void *data)
{
	int oldformat;
	struct isdn_chan *isdn_chan;
	char *dest = NULL;
	
	/* We do signed linear */
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR | AST_FORMAT_ULAW | AST_FORMAT_ALAW);

	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}

	if (data) {
		dest = ast_strdupa((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}

	isdn_chan = isdn_alloc();
	if (!isdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate isdn_chan\n");
		return NULL;
	}

	struct ast_channel *ast_chan;
	ast_chan = isdn_new(isdn_chan, AST_STATE_DOWN);
	
	return ast_chan;
}

static int __unload_module(void)
{
	/* First, take us out of the channel loop */
	ast_channel_unregister(type);
	return 0;
}

int unload_module(void)
{
	return __unload_module();
}

/*
void logger(const char *msg)
{
	ast_log(LOG_NOTICE, msg);
}*/

void refresh_polls_list(
	struct manager_state *manager,
	struct pollfd *polls, struct poll_info *poll_infos, int *npolls)
{
	*npolls = 0;

	int i;
	for(i = 0; i < manager->nifs; i++) {
		if (manager->ifs[i]->role == LAPD_ROLE_NT) {
			polls[*npolls].fd = manager->ifs[i]->nt_socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_INTERFACE;
			poll_infos[*npolls].interface = manager->ifs[i];
			(*npolls)++;
		} else {
			polls[*npolls].fd = manager->ifs[i]->te_dlc.socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = &manager->ifs[i]->te_dlc;
			(*npolls)++;
		}
	}

	for(i = 0; i < manager->ndlcs; i++) {
		polls[*npolls].fd = manager->dlcs[i].socket;
		polls[*npolls].events = POLLIN|POLLERR;
		poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
		poll_infos[*npolls].dlc = &manager->dlcs[i];
		(*npolls)++;
	}
}

static void *q931_thread_main(void *data)
{
	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls = 0;

	npolls = sizeof(*polls)/sizeof(*polls);
	refresh_polls_list(&manager, polls, poll_infos, &npolls);

	manager.have_to_exit = 0;
	int i;
	int active_calls_cnt = 0;
	do {
		if (poll(polls, npolls, 1000000) < 0) {
			printf("poll error: %s\n",strerror(errno));
			exit(1);
		}

		for(i = 0; i < npolls; i++) {
			if (poll_infos[i].type == POLL_INFO_TYPE_INTERFACE) {
				if (polls[i].revents & POLLERR) {
					printf("Error on interface %s poll\n",
						poll_infos[i].interface->name);
				}

				if (polls[i].revents & POLLIN) {
					printf("New DLC accepted...\n");

					manager.dlcs[manager.ndlcs].socket =
						accept(polls[i].fd, NULL, 0);
					manager.dlcs[manager.ndlcs].interface =
						poll_infos[i].interface;
					manager.ndlcs++;

					refresh_polls_list(&manager,
						polls,
						poll_infos, &npolls);
				}
			} else if (poll_infos[i].type == POLL_INFO_TYPE_DLC) {
				if (polls[i].revents & POLLERR) {
					printf("Error on DLC %d\n", polls[i].fd);
				}

				if (polls[i].revents & POLLIN) {
					printf("receiving frame...\n");
					q931_receive(poll_infos[i].dlc);
				}
			}
		}

		if (manager.have_to_exit) {
			active_calls_cnt = 0;

			for (i=0; i<manager.nifs; i++) {
				struct q931_call *call;
				list_for_each_entry(call, &manager.ifs[i]->calls, node)
					active_calls_cnt++;
			}
		}
	} while(!manager.have_to_exit || active_calls_cnt > 0);

	return NULL;
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	q931_init();
//	q931_register_logger_func(logger);

	// Enum interfaces and open them
	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		ast_log(LOG_ERROR, "getifaddr: %s\n", strerror(errno));
		return -1;
	}

	int fd;
	fd = socket(PF_LAPD, SOCK_DGRAM, htons(ETH_P_ALL));
	if (fd < 0) {
		ast_log(LOG_ERROR, "socket: %s\n", strerror(errno));
		return -1;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifreq;
		memset(&ifreq, 0x00, sizeof(ifreq));
		strncpy(ifreq.ifr_name, ifaddr->ifa_name, sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			ast_log(LOG_ERROR, "ioctl (%s): %s\n", ifaddr->ifa_name, strerror(errno));
			return -1;
		}

		if (ifreq.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		ast_log(LOG_NOTICE, "%s: %s %s\n",
			ifaddr->ifa_name,
			(ifaddr->ifa_flags & IFF_UP) ? "UP": "DOWN",
			(ifaddr->ifa_flags & IFF_ALLMULTI) ? "NT": "TE");

		if (!(ifaddr->ifa_flags & IFF_UP))
			continue;

		manager.ifs[manager.nifs] = q931_open_interface(ifaddr->ifa_name);
		if (!manager.ifs[manager.nifs]) {
			ast_log(LOG_ERROR, "q931_open_interface error: %s\n", strerror(errno));
			return -1;
		}

		listen(manager.ifs[manager.nifs]->nt_socket, 100);

		manager.nifs++;
	}
	close(fd);
	freeifaddrs(ifaddrs);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&q931_thread, &attr, q931_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start q931 thread.\n");
		return -1;
	}
	
	if (ast_channel_register_ex(type, tdesc, 
			 AST_FORMAT_SLINEAR |  AST_FORMAT_ULAW | AST_FORMAT_ALAW,
			 isdn_request, isdn_devicestate)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		__unload_module();
		return -1;
	}
	
	// register CLI extensions
	
	return res;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
