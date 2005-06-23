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
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <signal.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <linux/rtc.h>

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>

#include <streambus.h>
#include <lapd.h>
#include <q931.h>

#define FRAME_SIZE 160

#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

static char *desc = "VISDN Channel For Asterisk";
static char *type = "VISDN";
static char *tdesc = "VISDN Channel Implementation";

static int usecnt =0;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

// static char context[AST_MAX_EXTENSION] = "default";

static pthread_t visdn_q931_thread = AST_PTHREADT_NULL;

struct visdn_chan {
	struct ast_channel *ast_chan;
	struct q931_call *q931_call;

	int inbound;
	int channel_fd;
};

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

AST_MUTEX_DEFINE_STATIC(q931_lock);
struct q931_state
{
	struct q931_lib *libq931;

	int have_to_exit;

	struct q931_interface *ifs[100];
	int nifs;

	struct q931_dlc dlcs[100];
	int ndlcs;

	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls;
} q931;

static int handle_show_visdn_channels(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&q931_lock);

	int i;
	for (i=0; i<q931.nifs; i++) {
		ast_cli(fd, "Interface: %s\n", q931.ifs[i]->name);

		int j;
		for (j=0; j<q931.ifs[i]->n_channels; j++) {
			ast_cli(fd, "  B%d: %s\n",
				q931.ifs[i]->channels[j].id + 1,
				q931_channel_state_to_text(
					q931.ifs[i]->channels[j].state));
		}
	}

	ast_mutex_unlock(&q931_lock);

	return 0;
}

#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member)	\
		     ;			\
	     &pos->member != (head); 					\
	     pos = list_entry(pos->member.next, typeof(*pos), member)	\
		     )

static int visdn_cli_print_call_list(int fd, struct q931_interface *intf)
{
	int first_call;
	int i;
	for (i=0; i<q931.nifs; i++) {
		struct q931_call *call;
		first_call = TRUE;

		list_for_each_entry(call, &q931.ifs[i]->calls, calls_node) {

			if (!intf || call->intf == intf) {

				if (first_call) {
					ast_cli(fd, "Interface: %s\n", q931.ifs[i]->name);
					ast_cli(fd, "  Ref#    Caller       Called       State\n");
					first_call = FALSE;
				}

				ast_cli(fd, "  %c %5ld %-12s %-12s %s\n",
					(call->direction == Q931_CALL_DIRECTION_INBOUND)
						? 'I' : 'O',
					call->call_reference,
					call->calling_number,
					call->called_number,
					q931_call_state_to_text(call->state));
			}
		}
	}

	return RESULT_SUCCESS;
}

static void visdn_cli_print_call(int fd, struct q931_call *call)
{
	ast_cli(fd, "--------- Call %ld %s\n",
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
			"inbound" : "outbound");

	ast_cli(fd, "Interface       : %s\n", call->intf->name);

	if (call->dlc)
		ast_cli(fd, "DLC (TEI)       : %d\n", call->dlc->tei);

	ast_cli(fd, "State           : %s\n",
		q931_call_state_to_text(call->state));

	ast_cli(fd, "Calling Number  : %s\n", call->calling_number);
	ast_cli(fd, "Called Number   : %s\n", call->called_number);

	ast_cli(fd, "Sending complete: %s\n",
		call->sending_complete ? "Yes" : "No");

	ast_cli(fd, "Broadcast seutp : %s\n",
		call->broadcast_setup ? "Yes" : "No");

	ast_cli(fd, "Tones option    : %s\n",
		call->tones_option ? "Yes" : "No");

	ast_cli(fd, "Active timers   : ");

	if (call->T301.pending) ast_cli(fd, "T301 ");
	if (call->T302.pending) ast_cli(fd, "T302 ");
	if (call->T303.pending) ast_cli(fd, "T303 ");
	if (call->T304.pending) ast_cli(fd, "T304 ");
	if (call->T305.pending) ast_cli(fd, "T305 ");
	if (call->T306.pending) ast_cli(fd, "T306 ");
	if (call->T307.pending) ast_cli(fd, "T307 ");
	if (call->T308.pending) ast_cli(fd, "T308 ");
	if (call->T309.pending) ast_cli(fd, "T309 ");
	if (call->T310.pending) ast_cli(fd, "T310 ");
	if (call->T312.pending) ast_cli(fd, "T312 ");
	if (call->T313.pending) ast_cli(fd, "T313 ");
	if (call->T314.pending) ast_cli(fd, "T314 ");
	if (call->T316.pending) ast_cli(fd, "T316 ");
	if (call->T318.pending) ast_cli(fd, "T318 ");
	if (call->T319.pending) ast_cli(fd, "T319 ");
	if (call->T320.pending) ast_cli(fd, "T320 ");
	if (call->T321.pending) ast_cli(fd, "T321 ");
	if (call->T322.pending) ast_cli(fd, "T322 ");

	ast_cli(fd, "\n");

	ast_cli(fd, "CES:\n");
	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		ast_cli(fd, "%d %s %s ",
			ces->dlc->tei,
			q931_ces_state_to_text(ces->state),
			(ces == call->selected_ces ? "presel" :
			  (ces == call->preselected_ces ? "sel" : "")));

		if (ces->T304.pending) ast_cli(fd, "T304 ");
		if (ces->T308.pending) ast_cli(fd, "T308 ");
		if (ces->T322.pending) ast_cli(fd, "T322 ");

		ast_cli(fd, "\n");
	}

}

static int handle_show_visdn_calls(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&q931_lock);

	if (argc == 3) {
		visdn_cli_print_call_list(fd, NULL);
	} else if (argc == 4) {
		struct q931_interface *intf = NULL;

		char *callpos = strchr(argv[3], '/');
		if (callpos) {
			*callpos = '\0';
			callpos++;
		}

		int i;
		for (i=0; i<q931.nifs; i++) {
			if (!strcasecmp(q931.ifs[i]->name, argv[3])) {
				intf = q931.ifs[i];
				break;
			}
		}

		if (!intf) {
			ast_cli(fd, "Interface not found\n");
			goto err_intf_not_found;
		}

		if (!callpos) {
			visdn_cli_print_call_list(fd, intf);
		} else {
			struct q931_call *call;

			if (callpos[0] == 'i' || callpos[0] == 'I') {
				call = q931_find_call_by_reference(intf,
					Q931_CALL_DIRECTION_INBOUND,
					atoi(callpos + 1));
			} else if (callpos[0] == 'o' || callpos[0] == 'O') {
				call = q931_find_call_by_reference(intf,
					Q931_CALL_DIRECTION_OUTBOUND,
					atoi(callpos + 1));
			} else {
				ast_cli(fd, "Invalid call reference\n");
				goto err_unknown_direction;
			}

			if (!call) {
				ast_cli(fd, "Call %s not found\n", callpos);
				goto err_call_not_found;
			}

			visdn_cli_print_call(fd, call);
		}
	}

err_call_not_found:
err_unknown_direction:
err_intf_not_found:

	ast_mutex_unlock(&q931_lock);

	return RESULT_SUCCESS;
}

static char handle_show_visdn_channels_help[] =
	"Usage: pri show channels\n"
	"	Displays informations on vISDN channels\n";

static struct ast_cli_entry show_visdn_channels =
{
        { "show", "visdn", "channels", NULL },
	handle_show_visdn_channels,
	"Displays vISDN channel information",
	handle_show_visdn_channels_help,
	NULL
};

static char handle_show_visdn_calls_help[] =
	"Usage: show visdn calls\n"
	"	Lists vISDN calls\n";

static struct ast_cli_entry show_visdn_calls =
{
        { "show", "visdn", "calls", NULL },
	handle_show_visdn_calls,
	"Lists vISDN calls",
	handle_show_visdn_calls_help,
	NULL
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

static int visdn_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	int err = 0;
	char dest[256];

	strncpy(dest, orig_dest, sizeof(dest));

	// Parse destination and obtain interface name + number
	const char *intf_name;
	const char *number;
	char *stringp = dest;

	intf_name = strsep(&stringp, "/");
	if (!intf_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_format;
	}

	ast_mutex_lock(&q931_lock);
	struct q931_interface *intf = NULL;

	int i;
	for (i=0; i<q931.nifs; i++) {
		if (!strcmp(q931.ifs[i]->name, intf_name)) {
			intf = q931.ifs[i];
			break;
		}
	}

	if (!intf) {
		ast_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		err = -1;
                goto err_intf_not_found;
	}

	struct q931_call *q931_call;
	q931_call = q931_alloc_call_out(intf);

	if (ast_chan->callerid) {

		char callerid[255];
		char *name, *number;

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

	q931_call_set_called_number(q931_call, number),
	q931_call->sending_complete = TRUE;

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"visdn_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}
	
	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	q931_call->pvt = ast_chan;

	visdn_chan->q931_call = q931_call;
	visdn_chan->inbound = 0;

	char newname[40];
	snprintf(newname, sizeof(newname), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	ast_mutex_unlock(&ast_chan->lock);

	q931_setup_request(q931_call);

err_channel_not_down:
err_intf_not_found:
	ast_mutex_unlock(&q931_lock);
err_invalid_format:
err_invalid_destination:

	return err;
}

static int visdn_answer(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	ast_log(LOG_NOTICE, "visdn_answer\n");

	ast_indicate(ast_chan, -1);

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return -1;
	}

//	ast_queue_frame(ast_chan, &visdn_chan->fr);

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
		if (visdn_chan->inbound) {
			q931_alerting_request(visdn_chan->q931_call);
			ast_setstate(ast_chan, AST_STATE_RINGING);
		}

		return 1;
	break;

	case AST_CONTROL_ANSWER:
//		q931_setup_response(visdn_chan->q931_call);
	break;

	case AST_CONTROL_BUSY: {
		struct q931_causeset causeset = Q931_CAUSESET_INITC(
			Q931_IE_C_CV_USER_BUSY);

		q931_disconnect_request(visdn_chan->q931_call, &causeset);
	}
	break;

	case AST_CONTROL_CONGESTION: {
		struct q931_causeset causeset = Q931_CAUSESET_INITC(
			Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER); // Right?

		q931_disconnect_request(visdn_chan->q931_call, &causeset);
	}
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

	return 1;
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

	visdn_chan->channel_fd = -1;

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
	    visdn_chan->q931_call->state != U19_RELEASE_REQUEST) {

		struct q931_causeset causeset = Q931_CAUSESET_INITC(
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);

		q931_disconnect_request(visdn_chan->q931_call, &causeset);
	}

	visdn_chan->q931_call->pvt = NULL;

	visdn_destroy(visdn_chan);

	ast_chan->pvt->pvt = NULL;
	ast_setstate(ast_chan, AST_STATE_DOWN);

	return 0;
}

static struct ast_frame *visdn_read(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	static struct ast_frame f;
	static char buf[160 + AST_FRIENDLY_OFFSET];

//printf("R\n");

	if (visdn_chan->channel_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;
		f.src = type;
		f.mallocd = 0;
		f.delivery.tv_sec = 0;
		f.delivery.tv_usec = 0;

		return &f;
	}

	int r = read(visdn_chan->channel_fd, buf, 160);

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = r;
	f.datalen = r;
	f.data = buf;
	f.offset = 0;
	f.src = type;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	return &f;
}

static int visdn_write(struct ast_channel *ast_chan, struct ast_frame *frame)
{
	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

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

	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING,
				"Don't know what to do with frame type '%d'\n",
				frame->frametype);
		return 0;
	}
	if (frame->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return -1;
	}

	if (visdn_chan->channel_fd < 0) {
		ast_log(LOG_WARNING,
			"Attempting to write on unconnected channel\n");
		return -1;
	}

/*printf("W %d %02x%02x%02x%02x%02x%02x%02x%02x %d\n", visdn_chan->channel_fd,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);*/

	write(visdn_chan->channel_fd, frame->data, frame->datalen);

	return 0;
}

static struct ast_channel *visdn_request(char *type, int format, void *data)
{
	struct visdn_chan *visdn_chan;
	char *dest = NULL;

ast_log(LOG_ERROR, "VISDN_REQUEST\n");

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
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		return NULL;
	}

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/null");

        ast_chan->type = type;

	ast_chan->fds[0] = open("/dev/visdn/timer", O_RDONLY);
	if (ast_chan->fds[0] < 0) {
		ast_log(LOG_WARNING, "Unable to open timer %s\n",
			strerror(errno));
		return NULL;
	}

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->pvt->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->pvt->rawwriteformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;

	ast_chan->language[0] = '\0';

	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	visdn_chan->ast_chan = ast_chan;
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

	ast_setstate(ast_chan, AST_STATE_RESERVED);

	return ast_chan;
}

void refresh_polls_list(
	struct q931_state *q931,
	struct pollfd *polls, struct poll_info *poll_infos, int *npolls)
{
	*npolls = 0;

	int i;
	for(i = 0; i < q931->nifs; i++) {
		if (q931->ifs[i]->role == LAPD_ROLE_NT) {
			polls[*npolls].fd = q931->ifs[i]->master_socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_INTERFACE;
			poll_infos[*npolls].interface = q931->ifs[i];
			(*npolls)++;
		} else {
			polls[*npolls].fd = q931->ifs[i]->dlc.socket;
			polls[*npolls].events = POLLIN|POLLERR;
			poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
			poll_infos[*npolls].dlc = &q931->ifs[i]->dlc;
			(*npolls)++;
		}
	}

	for(i = 0; i < q931->ndlcs; i++) {
		polls[*npolls].fd = q931->dlcs[i].socket;
		polls[*npolls].events = POLLIN|POLLERR;
		poll_infos[*npolls].type = POLL_INFO_TYPE_DLC;
		poll_infos[*npolls].dlc = &q931->dlcs[i];
		(*npolls)++;
	}
}

static void visdn_accept(struct q931_state *q931,
	struct q931_interface *intf, int accept_socket)
{
	q931->dlcs[q931->ndlcs].socket =
		accept(accept_socket, NULL, 0);
	q931->dlcs[q931->ndlcs].intf =
		intf;

	int optlen=sizeof(q931->dlcs[q931->ndlcs].tei);
	if (getsockopt(q931->dlcs[q931->ndlcs].socket, SOL_LAPD, LAPD_TEI,
		&q931->dlcs[q931->ndlcs].tei, &optlen)<0) {
			printf("getsockopt: %s\n", strerror(errno));
			return;
	}

	ast_log(LOG_NOTICE,
		"New DLC (TEI=%d) accepted...\n",
		q931->dlcs[q931->ndlcs].tei);

	q931->ndlcs++;

	refresh_polls_list(q931,
		q931->polls,
		q931->poll_infos,
		&q931->npolls);
}

static void *visdn_q931_thread_main(void *data)
{

	q931.npolls = 0;
	refresh_polls_list(&q931,
		q931.polls,
		q931.poll_infos,
		&q931.npolls);

	q931.have_to_exit = 0;
	int i;
	int active_calls_cnt = 0;
	do {
		longtime_t usec_to_wait = q931_run_timers(q931.libq931);
		int msec_to_wait;

		if (usec_to_wait < 0)
			msec_to_wait = -1;
		else
			msec_to_wait = usec_to_wait / 1000 + 1;

		printf("TimeToWait = %lld\n", usec_to_wait);

		if (poll(q931.polls, q931.npolls, msec_to_wait) < 0) {
			if (errno == EINTR)
				continue;

			printf("poll error: %s\n",strerror(errno));
			exit(1);
		}

		for(i = 0; i < q931.npolls; i++) {
			if (q931.poll_infos[i].type == POLL_INFO_TYPE_INTERFACE) {
				if (q931.polls[i].revents & POLLERR) {
					printf("Error on interface %s poll\n",
						q931.poll_infos[i].interface->name);
				}

				if (q931.polls[i].revents & POLLIN) {
					ast_mutex_lock(&q931_lock);
					visdn_accept(&q931,
						q931.poll_infos[i].interface,
						q931.polls[i].fd);
					ast_mutex_unlock(&q931_lock);
				}
			} else if (q931.poll_infos[i].type == POLL_INFO_TYPE_DLC) {
				if (q931.polls[i].revents & POLLERR ||
				    q931.polls[i].revents & POLLIN) {
					ast_mutex_lock(&q931_lock);
					q931_receive(q931.poll_infos[i].dlc);
					ast_mutex_unlock(&q931_lock);
				}
			}
		}

		if (q931.have_to_exit) {
			active_calls_cnt = 0;

			for (i=0; i<q931.nifs; i++) {
				struct q931_call *call;
				list_for_each_entry(call, &q931.ifs[i]->calls,
								calls_node)
					active_calls_cnt++;
			}
		}
	} while(!q931.have_to_exit || active_calls_cnt > 0);

	return NULL;
}

static void visdn_q931_alerting_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	ast_setstate(ast_chan, AST_STATE_RINGING);
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_connect_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	q931_setup_complete_request(q931_call);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

//	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	ast_mutex_lock(&ast_chan->lock);
	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
	ast_mutex_unlock(&ast_chan->lock);
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
	printf("*** %s %s\n", __FUNCTION__, q931_call->called_number);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	if (q931_call->sending_complete) {
		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			strncpy(ast_chan->exten,
				q931_call->called_number,
				sizeof(ast_chan->exten)-1);

			ast_chan->callerid = strdup(q931_call->calling_number);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

				struct q931_causeset causeset = Q931_CAUSESET_INITC(
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);

				q931_disconnect_request(q931_call, &causeset);
			} else {
				q931_proceeding_request(q931_call);
				ast_setstate(ast_chan, AST_STATE_RING);
			}
		} else {
			struct q931_causeset causeset = Q931_CAUSESET_INITC(
				Q931_IE_C_CV_UNALLOCATED_NUMBER);

			q931_disconnect_request(q931_call, &causeset);
		}
	} else {
		if (!ast_canmatch_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {
			struct q931_causeset causeset = Q931_CAUSESET_INITC(
				Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);

			q931_disconnect_request(q931_call, &causeset);

			return;
		}

		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			strncpy(ast_chan->exten,
				q931_call->called_number,
				sizeof(ast_chan->exten)-1);

			ast_chan->callerid = strdup(q931_call->calling_number);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

				struct q931_causeset causeset = Q931_CAUSESET_INITC(
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);

				q931_disconnect_request(q931_call, &causeset);
			} else {
				q931_proceeding_request(q931_call);
				ast_setstate(ast_chan, AST_STATE_RING);
			}
		}
	}
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

	if (!ast_chan)
		return;

	ast_indicate(ast_chan, AST_CONTROL_PROCEEDING);
}

static void visdn_q931_progress_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_indicate(ast_chan, AST_CONTROL_PROGRESS);
}

static void visdn_q931_reject_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_release_confirm(
	struct q931_call *q931_call,
	enum q931_release_confirm_status status)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (ast_chan)
		ast_hangup(ast_chan);
}

static void visdn_q931_release_indication(
	struct q931_call *q931_call,
	const struct q931_causeset *causeset)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	ast_softhangup(ast_chan, AST_SOFTHANGUP_DEV);
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_resume_confirm(
	struct q931_call *q931_call,
	enum q931_resume_confirm_status status)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_resume_indication(
	struct q931_call *q931_call,
	__u8 *call_identity,
	int call_identity_len)
{
	printf("*** %s\n", __FUNCTION__);

	q931_resume_response(q931_call);
}

static void visdn_q931_setup_complete_indication(
	struct q931_call *q931_call,
	enum q931_setup_complete_indication_status status)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_setup_confirm(
	struct q931_call *q931_call,
	enum q931_setup_confirm_status status)
{
	printf("*** %s\n", __FUNCTION__);

	struct ast_channel *ast_chan = callpvt_to_astchan(q931_call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	ast_setstate(ast_chan, AST_STATE_UP);
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_setup_indication(struct q931_call *q931_call)
{
	printf("*** %s %s\n", __FUNCTION__, q931_call->called_number);


	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		goto err_visdn_alloc;
	}

	visdn_chan->inbound = 1;
	visdn_chan->q931_call = q931_call;

	struct ast_channel *ast_chan;
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_visdn_new;
	}

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VISDN/%s/%c%ld",
		q931_call->intf->name,
		q931_call->direction == Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O',
		q931_call->call_reference);


        ast_chan->type = type;

	ast_chan->fds[0] = open("/dev/visdn/timer", O_RDONLY);
	if (ast_chan->fds[0] < 0) {
		ast_log(LOG_WARNING, "Unable to open timer %s\n",
			strerror(errno));
		goto err_visdn_new;
	}

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->pvt->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->pvt->rawwriteformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;

	ast_chan->language[0] = '\0';

	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	visdn_chan->ast_chan = ast_chan;
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

	strncpy(ast_chan->context,
		"visdn",
		sizeof(ast_chan->context)-1);

	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	ast_setstate(ast_chan, AST_STATE_RING);




	q931_call->pvt = ast_chan;

	if (q931_call->sending_complete) {
		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			strncpy(ast_chan->exten,
				q931_call->called_number,
				sizeof(ast_chan->exten)-1);

			ast_set_callerid(ast_chan, q931_call->calling_number, 0);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

				struct q931_causeset causeset = Q931_CAUSESET_INITC(
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);

				q931_reject_request(q931_call, &causeset);
			} else {
				q931_setup_response(q931_call);

				// FIXME: Do this with Siemens PBX which are buggy
				// q931_proceeding_request(q931_call);

				ast_setstate(ast_chan, AST_STATE_RING);
			}
		} else {
			ast_log(LOG_NOTICE,
				"No extension %s in context '%s',"
				" ignoring call\n",
				q931_call->called_number,
				"visdn");

			struct q931_causeset causeset = Q931_CAUSESET_INITC(
				Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);

			q931_reject_request(q931_call, &causeset);
		}

	} else {
		if (!ast_canmatch_extension(NULL, "visdn", q931_call->called_number,
				1, q931_call->calling_number)) {

			struct q931_causeset causeset = Q931_CAUSESET_INITC(
				Q931_IE_C_CV_NO_ROUTE_TO_DESTINATION);

			q931_reject_request(q931_call, &causeset);

			ast_hangup(ast_chan);

			return;
		}

		if (ast_exists_extension(NULL, "visdn",
				q931_call->called_number, 1,
				q931_call->calling_number)) {

			strncpy(ast_chan->exten,
				q931_call->called_number,
				sizeof(ast_chan->exten)-1);

			ast_chan->callerid = strdup(q931_call->calling_number);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_hangup(ast_chan);

				struct q931_causeset causeset = Q931_CAUSESET_INITC(
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER);

				q931_reject_request(q931_call, &causeset);
			} else {
				q931_proceeding_request(q931_call);
				ast_setstate(ast_chan, AST_STATE_RING);
			}
		} else {
			q931_more_info_request(q931_call);
		}
	}

	return;

err_visdn_alloc:
	// Free visdn_chan
err_visdn_new:
;
}

static void visdn_q931_status_indication(
	struct q931_call *q931_call,
	enum q931_status_indication_status status)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_suspend_confirm(
	struct q931_call *q931_call,
	enum q931_suspend_confirm_status status)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_suspend_indication(
	struct q931_call *q931_call,
	__u8 *call_identity,
	int call_identity_len)
{
	printf("*** %s\n", __FUNCTION__);

	q931_suspend_response(q931_call);
}

static void visdn_q931_timeout_indication(struct q931_call *q931_call)
{
	printf("*** %s\n", __FUNCTION__);
}

enum sb_bearertype
{
        VISDN_BT_VOICE  = 1,
        VISDN_BT_PPP    = 2,
};

struct sb_setbearer
{
        int sb_index;
        enum sb_bearertype sb_bearertype;
};

#define VISDN_SET_BEARER        SIOCDEVPRIVATE
#define VISDN_PPP_GET_CHAN      (SIOCDEVPRIVATE+1)
#define VISDN_PPP_GET_UNIT      (SIOCDEVPRIVATE+2)

static void visdn_q931_connect_channel(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);

	assert(channel);
	assert(channel->call);

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	assert(ast_chan);
	assert(ast_chan->pvt);
	assert(ast_chan->pvt->pvt);

	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;

	struct ifreq ifr;
	strncpy(ifr.ifr_name, channel->call->intf->name, sizeof(ifr.ifr_name));

	struct sb_setbearer bt;
	bt.sb_index = channel->id;
	bt.sb_bearertype = VISDN_BT_VOICE;
	ifr.ifr_data = (void *)&bt;

	if (ioctl(channel->call->dlc->socket, VISDN_SET_BEARER,
	    (caddr_t) &ifr) < 0) {
		ast_log(LOG_ERROR, "ioctl(VISDN_SET_BEARER_PPP): %s\n", strerror(errno));
		return;
	}

	visdn_chan->channel_fd = open("pippo", O_RDWR);
	if (visdn_chan->channel_fd < 0) {
		ast_log(LOG_ERROR, "Cannot open channel\n");
	}
}

static void visdn_q931_disconnect_channel(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan) 
		return;

	struct visdn_chan *visdn_chan = ast_chan->pvt->pvt;
	close(visdn_chan->channel_fd);
}

static pthread_t visdn_generator_thread = AST_PTHREADT_NULL;
AST_MUTEX_DEFINE_STATIC(gen_chans_lock);
static struct ast_channel *gen_chans[256];
static int gen_chans_num = 0;

static void *visdn_generator_thread_main(void *aaa)
{
	struct ast_frame f;
	__u8 buf[256];

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = 80;
	f.datalen = 80;
	f.data = buf;
//	f.offset = AST_FRIENDLY_OFFSET;
	f.offset = 0;
	f.src = type;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	int ms = 500;

	while (gen_chans_num) {
		struct ast_channel *chan;

		chan = ast_waitfor_n(gen_chans, gen_chans_num, &ms);
		if (chan) {
			void *tmp;
			int res;
                	int (*generate)(struct ast_channel *chan, void *tmp,
			int datalen, int samples);

			tmp = chan->generatordata;
			chan->generatordata = NULL;
			generate = chan->generator->generate;
			res = generate(chan, tmp, f.datalen, f.samples);
			chan->generatordata = tmp;
			if (res) {
			        ast_log(LOG_DEBUG, "Auto-deactivating generator\n");
			        ast_deactivate_generator(chan);
			}
		}
	}

	visdn_generator_thread = AST_PTHREADT_NULL;

	return NULL;
}

int visdn_gendial_start(struct ast_channel *chan)
{
	int res = -1;

	ast_mutex_lock(&gen_chans_lock);

	if (gen_chans_num > sizeof(gen_chans)) {
		ast_log(LOG_WARNING, "MAX 256 chans in dialtone generation\n");
		goto err_too_many_channels;
	}

	gen_chans[gen_chans_num] = chan;
	gen_chans_num++;

	if (visdn_generator_thread == AST_PTHREADT_NULL) {
		if (ast_pthread_create(&visdn_generator_thread, NULL,
				visdn_generator_thread_main, NULL)) {
			ast_log(LOG_WARNING, "Unable to create autoservice thread :(\n");
		} else
			pthread_kill(visdn_generator_thread, SIGURG);
	}

err_too_many_channels:

	ast_mutex_unlock(&gen_chans_lock);
	return res;
}

int visdn_gendial_stop(struct ast_channel *chan)
{
	ast_mutex_lock(&gen_chans_lock);

	int i;
	for (i=0; i<gen_chans_num; i++) {
		if (gen_chans[i] == chan) {
			int j;
			for (j=i; j<gen_chans_num-1; j++)
				gen_chans[j] = gen_chans[j+1];

			break;
		}
	}

	if (i==gen_chans_num) {
		ast_log(LOG_WARNING, "Channel not found trying to stop generator\n");
		goto err_chan_not_found;
	}

	gen_chans_num--;


	if (gen_chans_num == 0 &&
	    visdn_generator_thread != AST_PTHREADT_NULL) {
		pthread_kill(visdn_generator_thread, SIGURG);
	}

	ast_mutex_unlock(&gen_chans_lock);

	/* Wait for it to un-block */
	while(chan->blocking)
		usleep(1000);

	return 0;

err_chan_not_found:

	ast_mutex_unlock(&gen_chans_lock);

	return 0;
}




static void visdn_q931_start_tone(struct q931_channel *channel,
	enum q931_tone_type tone)
{
	printf("*** %s B%d %d\n", __FUNCTION__, channel->id+1, tone);

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	// Unfortunately, after ast_hangup the channel is not valid
	// anymore and we cannot generate further tones thought we should
	if (!ast_chan)
		return;

	switch (tone) {
	case Q931_TONE_DIAL: {
		struct tone_zone_sound *ts;
		ts = ast_get_indication_tone(ast_chan->zone, "dial");

		if (ts) {
			if (ast_playtones_start(ast_chan, 0, ts->data, 0))
				ast_log(LOG_NOTICE,"Unable to start playtones\n");
		} else {
			ast_tonepair_start(ast_chan, 350, 440, 0, 0);
		}

		visdn_gendial_start(ast_chan);
	}
	break;

/*
	case Q931_TONE_BUSY:
		ast_indicate(ast_chan, AST_CONTROL_BUSY);
	break;

	case Q931_TONE_HANGUP:
	case Q931_TONE_FAILURE:
		ast_indicate(ast_chan, AST_CONTROL_CONGESTION);
	break;*/
	default:;
	}
}



static void visdn_q931_stop_tone(struct q931_channel *channel)
{
	printf("*** %s B%d\n", __FUNCTION__, channel->id+1);

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	visdn_gendial_stop(ast_chan);
}

static void visdn_q931_management_restart_confirm(
	struct q931_global_call *gc,
	const struct q931_chanset *chanset)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_timeout_management_indication(
	struct q931_global_call *gc)
{
	printf("*** %s\n", __FUNCTION__);
}

static void visdn_q931_status_management_indication(
	struct q931_global_call *gc)
{
	printf("*** %s\n", __FUNCTION__);
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
	ast_cli_unregister(&show_visdn_calls);
	ast_cli_unregister(&show_visdn_channels);

	ast_channel_unregister(type);

	if (q931.libq931)
		q931_leave(q931.libq931);

	return 0;
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	q931.libq931 = q931_init();
	q931_set_logger_func(q931.libq931, visdn_logger);

	// Setup all callbacks for libq931 primitives
	q931.libq931->alerting_indication =
		visdn_q931_alerting_indication;
	q931.libq931->connect_indication =
		visdn_q931_connect_indication;
	q931.libq931->disconnect_indication =
		visdn_q931_disconnect_indication;
	q931.libq931->error_indication =
		visdn_q931_error_indication;
	q931.libq931->info_indication =
		visdn_q931_info_indication;
	q931.libq931->more_info_indication =
		visdn_q931_more_info_indication;
	q931.libq931->notify_indication =
		visdn_q931_notify_indication;
	q931.libq931->proceeding_indication =
		visdn_q931_proceeding_indication;
	q931.libq931->progress_indication =
		visdn_q931_progress_indication;
	q931.libq931->reject_indication =
		visdn_q931_reject_indication;
	q931.libq931->release_confirm =
		visdn_q931_release_confirm;
	q931.libq931->release_indication =
		visdn_q931_release_indication;
	q931.libq931->resume_confirm =
		visdn_q931_resume_confirm;
	q931.libq931->resume_indication =
		visdn_q931_resume_indication;
	q931.libq931->setup_complete_indication =
		visdn_q931_setup_complete_indication;
	q931.libq931->setup_confirm =
		visdn_q931_setup_confirm;
	q931.libq931->setup_indication =
		visdn_q931_setup_indication;
	q931.libq931->status_indication =
		visdn_q931_status_indication;
	q931.libq931->suspend_confirm =
		visdn_q931_suspend_confirm;
	q931.libq931->suspend_indication =
		visdn_q931_suspend_indication;
	q931.libq931->timeout_indication =
		visdn_q931_timeout_indication;

	q931.libq931->connect_channel =
		visdn_q931_connect_channel;
	q931.libq931->disconnect_channel =
		visdn_q931_disconnect_channel;
	q931.libq931->start_tone =
		visdn_q931_start_tone;
	q931.libq931->stop_tone =
		visdn_q931_stop_tone;

	q931.libq931->management_restart_confirm =
		visdn_q931_management_restart_confirm;
	q931.libq931->timeout_management_indication =
		visdn_q931_timeout_management_indication;
	q931.libq931->status_management_indication =
		visdn_q931_status_management_indication;


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

		struct q931_interface *intf =
			q931_open_interface(q931.libq931,
				ifaddr->ifa_name);

		if (!intf) {
			ast_log(LOG_WARNING,
				"Cannot open interface %s, skipping\n",
				ifaddr->ifa_name);

			continue;
		}

		intf->tones_option = TRUE;

		if (intf->role == LAPD_ROLE_NT) {
			if (listen(intf->master_socket, 100) < 0) {
				ast_log(LOG_ERROR,
					"cannot listen on master socket: %s\n",
					strerror(errno));

					return -1;
			}
		}

		q931.ifs[q931.nifs] = intf;
		q931.nifs++;
	}
	close(fd);
	freeifaddrs(ifaddrs);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&visdn_q931_thread, &attr,
					visdn_q931_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start q931 thread.\n");
		return -1;
	}
	
	if (ast_channel_register_ex(type, tdesc, 
			 AST_FORMAT_ALAW,
			 visdn_request, visdn_devicestate)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	
	ast_cli_register(&show_visdn_channels);
	ast_cli_register(&show_visdn_calls);
	
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
