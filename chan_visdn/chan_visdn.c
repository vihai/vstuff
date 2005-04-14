/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * vISDN Linux Telephony Interface driver
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
#include <stdarg.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <lapd.h>
#include <q931.h>

static char *desc = "VISDN Channel For Asterisk";
static char *type = "VISDN";
static char *tdesc = "VISDN Channel Implementation";

static int usecnt =0;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char context[AST_MAX_EXTENSION] = "default";

AST_MUTEX_DEFINE_STATIC(manager_lock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t q931_thread = AST_PTHREADT_NULL;

/* NBS creates private structures on demand */
   
struct visdn_chan {
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	struct ast_frame fr;			/* "null" frame */

	int inbound;

	struct q931_call *q931_call;
};

struct manager_state
{
	struct q931_lib *libq931;

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


static int visdn_devicestate(void *data)
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

static int visdn_call(struct ast_channel *ast_chan, char *orig_dest, int timeout)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;


	char dest[256];
	strncpy(dest, orig_dest, sizeof(dest));

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"visdn_call called on %s,"
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

	struct q931_call *q931_call;
	q931_call = q931_alloc_call_out(intf);

	if (ast_chan->callerid) {

		char callerid[255];
		const char *name, *number;

		strncpy(callerid, ast_chan->callerid, sizeof(callerid));
		ast_callerid_parse(callerid, &name, &number);

		if (number)
			q931_call_set_calling_number(q931_call, number);
		else
			ast_log(LOG_WARNING,
				"Unable to parse '%s'"
				" into CallerID name & number\n",
				callerid);
	}

	q931_call_set_called_number(q931_call, number);
	q931_call->sending_complete = TRUE;

	q931_call->pvt = ast_chan;

	q931_setup_request(q931_call);

	visdn_chan->q931_call = q931_call;
	visdn_chan->inbound = 0;

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/%s/%d",
		q931_call->intf->name,
		1234); // FIXME

//	if (0) {
//		ast_log(LOG_WARNING, "NBS Connection failed on %s\n", ast_chan->name);
//		ast_queue_control(ast_chan, AST_CONTROL_CONGESTION);
//	} else {
//	}

	return 0;
}

static int visdn_answer(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return -1;
	}

	ast_log(LOG_WARNING, "visdn_answer\n");

	q931_setup_response(visdn_chan->q931_call);

	return 0;
}

static int visdn_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc)
{
	ast_log(LOG_WARNING, "visdn_bridge\n");

	return -1;
}

struct ast_frame *visdn_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "visdn_exception\n");

	return NULL;
}

static int visdn_indicate(struct ast_channel *ast_chan, int condition)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return 1;
	}

	ast_log(LOG_NOTICE, "visdn_indicate %d\n", condition);

	switch(condition) {
	case AST_CONTROL_HANGUP:
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
		return 1;
	break;

	case AST_CONTROL_RINGING:
		if (visdn_chan->inbound)
			q931_alerting_request(visdn_chan->q931_call);
	break;

	case AST_CONTROL_ANSWER:
//		q931_setup_response(visdn_chan->q931_call);
	break;

	case AST_CONTROL_BUSY:
		q931_reject_request(visdn_chan->q931_call,
			Q931_IE_C_CV_USER_BUSY);
	break;

	case AST_CONTROL_CONGESTION:
		q931_reject_request(visdn_chan->q931_call,
			Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER); // Right?
	break;

	case AST_CONTROL_PROGRESS:
		if (visdn_chan->inbound)
			q931_progress_request(visdn_chan->q931_call);
	break;

	case AST_CONTROL_PROCEEDING:
		if (visdn_chan->inbound)
			q931_proceeding_request(visdn_chan->q931_call);
	break;
	}

	return 0;
}

static int visdn_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	ast_log(LOG_WARNING, "visdn_fixup\n");

	return -1;
}

static int visdn_setoption(struct ast_channel *ast_chan, int option, void *data, int datalen)
{
	ast_log(LOG_WARNING, "visdn_setoption\n");

	return -1;
}

static void visdn_destroy(struct visdn_chan *visdn_chan)
{
	free(visdn_chan);
}

static struct visdn_chan *visdn_alloc()
{
	struct visdn_chan *visdn_chan;

	visdn_chan = malloc(sizeof(struct visdn_chan));
	if (!visdn_chan)
		return NULL;

	memset(visdn_chan, 0x00, sizeof(struct visdn_chan));

	return visdn_chan;
}

static int visdn_hangup(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	ast_log(LOG_NOTICE, "visdn_hangup\n");

	if (!visdn_chan) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	if (visdn_chan->q931_call->state != N0_NULL_STATE &&
	    visdn_chan->q931_call->state != N1_CALL_INITIATED &&
	    visdn_chan->q931_call->state != N11_DISCONNECT_REQUEST &&
	    visdn_chan->q931_call->state != N12_DISCONNECT_INDICATION &&
	    visdn_chan->q931_call->state != N17_RESUME_REQUEST &&
	    visdn_chan->q931_call->state != N19_RELEASE_REQUEST &&
	    visdn_chan->q931_call->state != N22_CALL_ABORT &&
	    visdn_chan->q931_call->state != U0_NULL_STATE &&
	    visdn_chan->q931_call->state != U6_CALL_PRESENT &&
	    visdn_chan->q931_call->state != U11_DISCONNECT_REQUEST &&
	    visdn_chan->q931_call->state != U12_DISCONNECT_INDICATION &&
	    visdn_chan->q931_call->state != U15_SUSPEND_REQUEST &&
	    visdn_chan->q931_call->state != U17_RESUME_REQUEST &&
	    visdn_chan->q931_call->state != U19_RELEASE_REQUEST)
		q931_disconnect_request(visdn_chan->q931_call,
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);

	visdn_chan->q931_call->pvt = NULL;

	visdn_destroy(visdn_chan);

	ast_chan->pvt->pvt = NULL;
	ast_setstate(ast_chan, AST_STATE_DOWN);

	return 0;
}

static struct ast_frame *visdn_read(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	
	/* Some nice norms */
	visdn_chan->fr.datalen = 0;
	visdn_chan->fr.samples = 0;
	visdn_chan->fr.data =  NULL;
	visdn_chan->fr.src = type;
	visdn_chan->fr.offset = 0;
	visdn_chan->fr.mallocd=0;
	visdn_chan->fr.delivery.tv_sec = 0;
	visdn_chan->fr.delivery.tv_usec = 0;

	ast_log(LOG_DEBUG, "Returning null frame on %s\n", ast_chan->name);

	return &visdn_chan->fr;
}

static int visdn_write(struct ast_channel *ast_chan, struct ast_frame *frame)
{
//	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	/* Write a frame of (presumably voice) data */
	
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING,
				"Don't know what to do with frame type '%d'\n",
				frame->frametype);
		return 0;
	}
	if (!(frame->subclass &
		(AST_FORMAT_ALAW))) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}
	if (ast_chan->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	
	// WRITE THE FRAME
		
	return 0;
}

static struct ast_channel *visdn_new(struct visdn_chan *visdn_chan)
{
	struct ast_channel *ast_chan;

	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel");
		return NULL;
	}

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/null");

        ast_chan->type = type;

	ast_chan->fds[0] = 0;
	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->pvt->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->pvt->rawwriteformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;

	ast_chan->language[0] = '\0';

	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	if (ast_chan->pvt->pvt) {
		ast_log(LOG_ERROR,
			"visdn channel already assigned to ast_channel???");
		return NULL;
	}

	visdn_chan->owner = ast_chan;
	ast_chan->pvt->pvt = visdn_chan;

	ast_chan->pvt->call = visdn_call;
	ast_chan->pvt->hangup = visdn_hangup;
	ast_chan->pvt->answer = visdn_answer;
	ast_chan->pvt->read = visdn_read;
	ast_chan->pvt->write = visdn_write;
	ast_chan->pvt->bridge = visdn_bridge;
	ast_chan->pvt->exception = visdn_exception;
	ast_chan->pvt->indicate = visdn_indicate;
	ast_chan->pvt->fixup = visdn_fixup;
	ast_chan->pvt->setoption = visdn_setoption;

	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	ast_setstate(ast_chan, AST_STATE_DOWN);

	return ast_chan;
}


static struct ast_channel *visdn_request(char *type, int format, void *data)
{
	struct visdn_chan *visdn_chan;
	char *dest = NULL;
	
	if (!(format & AST_FORMAT_ALAW)) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		return NULL;
	}

	if (data) {
		dest = ast_strdupa((char *)data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}

	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		return NULL;
	}

	visdn_chan->inbound = 0;

	ast_log(LOG_NOTICE,
		"DEST = '%s'\n",
		dest);

	struct ast_channel *ast_chan;
	ast_chan = visdn_new(visdn_chan);
	if (!ast_chan) {
		// FIXME
	}
	
	return ast_chan;
}

void refresh_polls_list(
	struct manager_state *manager,
	struct pollfd *polls, struct poll_info *poll_infos, int *npolls)
{
	*npolls = 0;

	int i;
	for(i = 0; i < manager->nifs; i++) {
		if (manager->ifs[i]->role == LAPD_ROLE_NT) {
			polls[*npolls].fd = manager->ifs[i]->master_socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_INTERFACE;
			poll_infos[*npolls].interface = manager->ifs[i];
			(*npolls)++;
		} else {
			polls[*npolls].fd = manager->ifs[i]->dlc.socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = &manager->ifs[i]->dlc;
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
					manager.dlcs[manager.ndlcs].intf =
						poll_infos[i].interface;
					manager.ndlcs++;

					refresh_polls_list(&manager,
						polls,
						poll_infos, &npolls);
				}
			} else if (poll_infos[i].type == POLL_INFO_TYPE_DLC) {
				if (polls[i].revents & POLLERR ||
				    polls[i].revents & POLLIN) {
					q931_receive(poll_infos[i].dlc);
				}
			}
		}

		if (manager.have_to_exit) {
			active_calls_cnt = 0;

			for (i=0; i<manager.nifs; i++) {
				struct q931_call *call;
				list_for_each_entry(call, &manager.ifs[i]->calls,
								calls_node)
					active_calls_cnt++;
			}
		}
	} while(!manager.have_to_exit || active_calls_cnt > 0);

	return NULL;
}

static int visdn_accept_inbound_call(struct q931_call *q931_call)
{
	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		return FALSE;
	}

	visdn_chan->inbound = 1;
	visdn_chan->q931_call = q931_call;

	struct ast_channel *ast_chan;
	ast_chan = visdn_new(visdn_chan);
	if (!ast_chan)
		return FALSE;

	q931_call->pvt = ast_chan;

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/%s/%d",
		q931_call->intf->name,
		1234); // FIXME

	strncpy(ast_chan->exten,
		q931_call->called_number,
		sizeof(ast_chan->exten)-1);

	strncpy(ast_chan->context,
		"visdn",
		sizeof(ast_chan->context)-1);

	ast_chan->callerid = strdup(q931_call->calling_number);
	ast_setstate(ast_chan, AST_STATE_RING);

	if (ast_pbx_start(ast_chan)) {
		ast_log(LOG_ERROR,
			"Unable to start PBX on %s\n",
			ast_chan->name);
		ast_hangup(ast_chan);
		return FALSE;
	}
}

static void visdn_q931_alerting_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_mutex_lock(&ast_chan->lock);
		ast_setstate(ast_chan, AST_STATE_RINGING);
		ast_mutex_unlock(&ast_chan->lock);
	}
}

static void visdn_q931_connect_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	q931_setup_complete_request(q931_call);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_mutex_lock(&ast_chan->lock);
		ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
		ast_mutex_unlock(&ast_chan->lock);
	}
}

static void visdn_q931_disconnect_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_mutex_lock(&ast_chan->lock);
		ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
		ast_mutex_unlock(&ast_chan->lock);
	}

	q931_release_request(q931_call);
}

static void visdn_q931_error_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_info_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	if (!ast_canmatch_extension(NULL, "visdn", q931_call->called_number,
			1, q931_call->calling_number)) {
		q931_reject_request(q931_call,
			Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);

		return;
	}

	if (!ast_exists_extension(NULL, "visdn", q931_call->called_number,
			1, q931_call->calling_number)) {

		return;
	}

	ast_log(LOG_NOTICE, "Extension matches!\n");

	if (!visdn_accept_inbound_call(q931_call))
		q931_reject_request(q931_call,
			Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);
	else
		q931_proceeding_request(q931_call);
}

static void visdn_q931_more_info_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_notify_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_proceeding_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_indicate(ast_chan, AST_CONTROL_PROCEEDING);
	}
}

static void visdn_q931_progress_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_indicate(ast_chan, AST_CONTROL_PROGRESS);
	}
}

static void visdn_q931_reject_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_mutex_lock(&ast_chan->lock);
		ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
		ast_mutex_unlock(&ast_chan->lock);
	}
}

static void visdn_q931_release_confirm(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_release_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan) {
		ast_mutex_lock(&ast_chan->lock);
		ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
		ast_mutex_unlock(&ast_chan->lock);
	}
}

static void visdn_q931_resume_confirm(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_resume_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_setup_complete_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_setup_confirm(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_setup_indication(struct q931_call *q931_call)
{
	printf("*** %s %s\n", __FUNCTION__, q931_call->called_number);

	if (!ast_canmatch_extension(NULL, "visdn", q931_call->called_number,
			1, q931_call->calling_number)) {
		q931_reject_request(q931_call,
			Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);

		return;
	}

	if (q931_call->sending_complete) {
		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			if (!visdn_accept_inbound_call(q931_call))
				q931_reject_request(q931_call,
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);
			else
				q931_proceeding_request(q931_call);
		} else {
			ast_log(LOG_NOTICE,
				"No extension %s in context '%s',"
				" ignoring call\n",
				q931_call->called_number,
				"visdn");

			q931_reject_request(q931_call,
				Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);
		}

	} else {
		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			if (!visdn_accept_inbound_call(q931_call))
				q931_reject_request(q931_call,
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);
			else
				q931_proceeding_request(q931_call);

			q931_proceeding_request(q931_call);
		} else {
			q931_more_info_request(q931_call);
		}
	}

}

static void visdn_q931_status_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_suspend_confirm(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_suspend_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_timeout_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_connect_channel(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);
}

static void visdn_q931_disconnect_channel(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);
}

static void visdn_q931_start_tone(struct q931_channel *channel,
	enum q931_tone_type tone)
{
	printf("*** %s B%d %d\n", __FUNCTION__, channel->id+1, tone);
}

static void visdn_q931_stop_tone(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);
}

static void visdn_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case Q931_LOG_DEBUG: ast_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_INFO: ast_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_NOTICE: ast_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_WARNING: ast_log(__LOG_WARNING, "libq931", 0, "", "%s", msg); break;
	case Q931_LOG_ERR:
	case Q931_LOG_CRIT:
	case Q931_LOG_ALERT:
	case Q931_LOG_EMERG: ast_log(__LOG_ERROR, "libq931", 0, "", "%s", msg); break;
	}
}

int unload_module(void)
{
	ast_channel_unregister(type);

	if (manager.libq931)
		q931_leave(manager.libq931);

	return 0;
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	manager.libq931 = q931_init();
	q931_set_logger_func(manager.libq931, visdn_logger);

	// Enum interfaces and open them
	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		ast_log(LOG_ERROR, "getifaddr: %s\n", strerror(errno));
		return -1;
	}

	int fd;
	fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		ast_log(LOG_ERROR, "socket: %s\n", strerror(errno));
		return -1;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifreq;
		memset(&ifreq, 0x00, sizeof(ifreq));
		strncpy(ifreq.ifr_name, ifaddr->ifa_name, sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			ast_log(LOG_ERROR, "ioctl (%s): %s\n",
				ifaddr->ifa_name, strerror(errno));
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

		manager.ifs[manager.nifs] =
			q931_open_interface(manager.libq931,
				ifaddr->ifa_name);

		if (!manager.ifs[manager.nifs]) {
			ast_log(LOG_WARNING,
				"Cannot open interface %s, skipping\n",
				ifaddr->ifa_name);

			continue;
		}
		if (!manager.ifs[manager.nifs]) {
			ast_log(LOG_ERROR,
				"q931_open_interface error: %s\n",
				strerror(errno));

			return -1;
		}

		// Setup all callbacks for libq931 primitives
		manager.ifs[manager.nifs]->alerting_indication =
			visdn_q931_alerting_indication;
		manager.ifs[manager.nifs]->connect_indication =
			visdn_q931_connect_indication;
		manager.ifs[manager.nifs]->disconnect_indication =
			visdn_q931_disconnect_indication;
		manager.ifs[manager.nifs]->error_indication =
			visdn_q931_error_indication;
		manager.ifs[manager.nifs]->info_indication =
			visdn_q931_info_indication;
		manager.ifs[manager.nifs]->more_info_indication =
			visdn_q931_more_info_indication;
		manager.ifs[manager.nifs]->notify_indication =
			visdn_q931_notify_indication;
		manager.ifs[manager.nifs]->proceeding_indication =
			visdn_q931_proceeding_indication;
		manager.ifs[manager.nifs]->progress_indication =
			visdn_q931_progress_indication;
		manager.ifs[manager.nifs]->reject_indication =
			visdn_q931_reject_indication;
		manager.ifs[manager.nifs]->release_confirm =
			visdn_q931_release_confirm;
		manager.ifs[manager.nifs]->release_indication =
			visdn_q931_release_indication;
		manager.ifs[manager.nifs]->resume_confirm =
			visdn_q931_resume_confirm;
		manager.ifs[manager.nifs]->resume_indication =
			visdn_q931_resume_indication;
		manager.ifs[manager.nifs]->setup_complete_indication =
			visdn_q931_setup_complete_indication;
		manager.ifs[manager.nifs]->setup_confirm =
			visdn_q931_setup_confirm;
		manager.ifs[manager.nifs]->setup_indication =
			visdn_q931_setup_indication;
		manager.ifs[manager.nifs]->status_indication =
			visdn_q931_status_indication;
		manager.ifs[manager.nifs]->suspend_confirm =
			visdn_q931_suspend_confirm;
		manager.ifs[manager.nifs]->suspend_indication =
			visdn_q931_suspend_indication;
		manager.ifs[manager.nifs]->timeout_indication =
			visdn_q931_timeout_indication;

		manager.ifs[manager.nifs]->connect_channel =
			visdn_q931_connect_channel;
		manager.ifs[manager.nifs]->disconnect_channel =
			visdn_q931_disconnect_channel;
		manager.ifs[manager.nifs]->start_tone =
			visdn_q931_start_tone;
		manager.ifs[manager.nifs]->stop_tone =
			visdn_q931_stop_tone;

		if (manager.ifs[manager.nifs]->role == LAPD_ROLE_NT) {
			if (listen(manager.ifs[manager.nifs]->master_socket, 100) < 0) {
				ast_log(LOG_ERROR,
					"cannot listen on master socket: %s\n",
					strerror(errno));

					return -1;
			}
		}

		manager.nifs++;
	}
	close(fd);
	freeifaddrs(ifaddrs);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&q931_thread, &attr,
					q931_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start q931 thread.\n");
		return -1;
	}
	
	if (ast_channel_register_ex(type, tdesc, 
			 AST_FORMAT_ALAW,
			 visdn_request, visdn_devicestate)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
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
