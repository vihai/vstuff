/*
 * vGSM channel driver for Asterisk
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
#include <ctype.h>
#include <sys/termios.h>
#include <sys/signal.h>
#include <ctype.h>

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>

//#include <streamport.h>
//#include <visdn.h>

#include "chan_vgsm.h"

//#include "../config.h"

#define VGSM_DESCRIPTION "VoiSmart VGSM Channel For Asterisk"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"

#define BAUDRATE 38400

/* Up to 10 seconds for an echo to arrive */
#define ECHO_TIMEOUT 1



#define assert(cond)							\
	do {								\
		if (!(cond)) {						\
			ast_log(LOG_ERROR,				\
				"assertion (" #cond ") failed\n");	\
			abort();					\
		}							\
	} while(0)

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static pthread_t vgsm_maint_thread = AST_PTHREADT_NULL;

enum vgsm_interface_status
{
	VGSM_INT_STATUS_UNINITIALIZED,
	VGSM_INT_STATUS_READY,
	VGSM_INT_STATUS_NOT_READY,
	VGSM_INT_STATUS_LOCKED_DOWN,
	VGSM_INT_STATUS_FAILED,
};

enum vgsm_interface_net_status
{
	VGSM_INT_NET_STATUS_NOT_REGISTERED = 1,
	VGSM_INT_NET_STATUS_REGISTERED_HOME = 2,
	VGSM_INT_NET_STATUS_REGISTRATION_DENIED = 3,
	VGSM_INT_NET_STATUS_REGISTERED_ROAMING = 5,
};

enum vgsm_operator_selection
{
	VGSM_OPSEL_AUTOMATIC = 0,
	VGSM_OPSEL_MANUAL_UNLOCKED = 1,
	VGSM_OPSEL_MANUAL_FALLBACK = 4,
	VGSM_OPSEL_MANUAL_LOCKED = 5,
};

struct vgsm_interface
{
	struct list_head ifs_node;

	char name[64];
	char device_filename[PATH_MAX];

	int fd;

	enum vgsm_interface_status status;

	char buf[2048];

	char module_vendor[32];
	char module_model[32];
	char module_version[32];
	char module_revision[32];
	char module_serial_number[32];
	char module_imei[32];

	char sim_imsi[32];
	int sim_remaining_attempts;

	enum vgsm_interface_net_status net_status;
	char net_operator;
	int net_local_area_code;
	int net_cell_id;
	int net_signal_level;
	int net_signal_ber;

	char context[AST_MAX_EXTENSION];

	char pin[16];
	int min_level;
	int rx_gain;
	int tx_gain;
	enum vgsm_operator_selection operator_selection;
	char operator_id;
};

struct vgsm_state
{
	ast_mutex_t lock;

	struct list_head ifs;

	int usecnt;

	int debug;
	

	struct vgsm_interface default_intf;
} vgsm = {
	.usecnt = 0,
#ifdef DEBUG_DEFAULTS
	.debug = TRUE,
#else
	.debug = FALSE,
#endif

	.default_intf = {
		.context = "vgsm",
		.pin = "",
		.min_level = -100,
		.rx_gain = 0,
		.tx_gain = 0,
	}
};

#ifdef DEBUG_CODE
#define vgsm_debug(format, arg...)			\
	if (vgsm.debug)				\
		ast_log(LOG_NOTICE,			\
			format,				\
			## arg)
#else
#define vgsm_debug(format, arg...)		\
	do {} while(0);
#endif

static int do_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return 0;
}

static int do_no_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return 0;
}

static const char *vgsm_intf_status_to_text(enum vgsm_interface_status status)
{
	switch(status) {
	case VGSM_INT_STATUS_UNINITIALIZED:
		return "UNINITIALIZED";
	case VGSM_INT_STATUS_READY:
		return "READY";
	case VGSM_INT_STATUS_NOT_READY:
		return "NOT_READY";
	case VGSM_INT_STATUS_LOCKED_DOWN:
		return "LOCKED_DOWN";
	case VGSM_INT_STATUS_FAILED:
		return "FAILED";
	}

	return "*UNKNOWN*";
}

static int do_show_vgsm_interfaces(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);

	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		ast_cli(fd, "\n------ Interface '%s' ---------\n", intf->name);

		ast_cli(fd,
			"  Device : %s\n"
			"  Context: %s\n"
			"  RX-gain: %d\n"
			"  TX-gain: %d\n"
			"\n"
			"Module:\n"
			"  Status: %s\n"
			"  Model: %s %s\n"
			"  Version: %s\n"
			"  Revision: %s\n"
			"  Serial#: %s\n"
			"  IMEI: %s\n"
			"\n"
			"SIM:\n"
			"  IMSI: %s\n"
			"  PIN remaining attempts: %d\n"
			"\n"
			"Network: \n",
			intf->device_filename,
			intf->context,
			intf->rx_gain,
			intf->tx_gain,
			vgsm_intf_status_to_text(intf->status),
			intf->module_vendor,
			intf->module_model,
			intf->module_version,
			intf->module_revision,
			intf->module_serial_number,
			intf->module_imei,
			intf->sim_imsi,
			intf->sim_remaining_attempts);
	}

	ast_mutex_unlock(&vgsm.lock);

	return 0;
}

static int vgsm_intf_from_var(
	struct vgsm_interface *intf,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "device")) { 
		strncpy(intf->device_filename, var->value,
			sizeof(intf->device_filename));
	} else if (!strcasecmp(var->name, "context")) {
		strncpy(intf->context, var->value,
			sizeof(intf->context));
	} else if (!strcasecmp(var->name, "pin")) {
		strncpy(intf->pin, var->value,
			sizeof(intf->pin));
	} else if (!strcasecmp(var->name, "min_level")) {
		intf->min_level = atoi(var->value);
	} else if (!strcasecmp(var->name, "rx_gain")) {
		intf->rx_gain = atoi(var->value);
	} else if (!strcasecmp(var->name, "tx_gain")) {
		intf->tx_gain = atoi(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void vgsm_copy_interface_config(
	struct vgsm_interface *dst,
	const struct vgsm_interface *src)
{
	strncpy(dst->device_filename, src->device_filename,
		sizeof(dst->device_filename));
	strncpy(dst->context, src->context,
		sizeof(dst->context));
	strncpy(dst->pin, src->pin,
		sizeof(dst->pin));

	dst->min_level = src->min_level;
	dst->rx_gain = src->rx_gain;
	dst->tx_gain = src->tx_gain;
}

static void vgsm_reload_config(void)
{
	struct ast_config *cfg;
	cfg = ast_config_load(VGSM_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s, VGSM disabled\n",
			VGSM_CONFIG_FILE);

		return;
	}
	
	ast_mutex_lock(&vgsm.lock);

	struct ast_variable *var;
	var = ast_variable_browse(cfg, "global");
	while (var) {
		if (vgsm_intf_from_var(&vgsm.default_intf, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	const char *cat;
	for (cat = ast_category_browse(cfg, NULL); cat;
	     cat = ast_category_browse(cfg, (char *)cat)) {

		if (!strcasecmp(cat, "general") ||
		    !strcasecmp(cat, "global"))
			continue;

		int found = FALSE;
		struct vgsm_interface *intf;
		list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
			if (!strcasecmp(intf->name, cat)) {
				found = TRUE;
				break;
			}
		}

		if (!found) {
			intf = malloc(sizeof(*intf));
			memset(intf, 0, sizeof(*intf));

			intf->status = VGSM_INT_STATUS_UNINITIALIZED;

			strncpy(intf->name, cat, sizeof(intf->name));
			vgsm_copy_interface_config(intf, &vgsm.default_intf);

			list_add_tail(&intf->ifs_node, &vgsm.ifs);
		}

		
		var = ast_variable_browse(cfg, (char *)cat);
		while (var) {
			if (vgsm_intf_from_var(intf, var) < 0) {
				ast_log(LOG_WARNING,
					"Unknown configuration variable %s\n",
					var->name);
			}
			
			var = var->next;
		}

		// Move to open_interface
		intf->fd = open(intf->device_filename, O_RDWR);
		if (intf->fd < 0) {
			ast_log(LOG_WARNING, "Unable to open '%s'\n",
				intf->device_filename);
			return;
		}
	}

	ast_mutex_unlock(&vgsm.lock);

	ast_config_destroy(cfg);
}

static int do_vgsm_reload(int fd, int argc, char *argv[])
{
	vgsm_reload_config();

	return 0;
}

static char debug_vgsm_generic_help[] =
	"Usage: debug vgsm generic\n"
	"	Debug generic vGSM events\n";

static struct ast_cli_entry debug_vgsm_generic =
{
	{ "debug", "vgsm", "generic", NULL },
	do_debug_vgsm_generic,
	"Enables generic vGSM debugging",
	debug_vgsm_generic_help,
	NULL
};

static struct ast_cli_entry no_debug_vgsm_generic =
{
	{ "no", "debug", "vgsm", "generic", NULL },
	do_no_debug_vgsm_generic,
	"Disables generic vGSM debugging",
	NULL,
	NULL
};

static char show_vgsm_interfaces_help[] =
	"Usage: vgsm show interfaces\n"
	"	Displays informations on vGSM interfaces\n";

static struct ast_cli_entry show_vgsm_interfaces =
{
	{ "show", "vgsm", "interfaces", NULL },
	do_show_vgsm_interfaces,
	"Displays vGSM interface information",
	show_vgsm_interfaces_help,
	NULL
};

static char vgsm_vgsm_reload_help[] =
	"Usage: vgsm reload\n"
	"	Reloads vGSM config\n";

static struct ast_cli_entry vgsm_reload =
{
	{ "vgsm", "reload", NULL },
	do_vgsm_reload,
	"Reloads vGSM configuration",
	vgsm_vgsm_reload_help,
	NULL
};

static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	//struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	int err;
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

	int found = FALSE;
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcmp(intf->name, intf_name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		ast_log(LOG_WARNING, "Interface %s not found\n", intf_name);
		err = -1;
		goto err_intf_not_found;
	}

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"vgsm_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}

	if (option_debug)
		ast_log(LOG_DEBUG,
			"Calling %s on %s\n",
			dest, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/uniqueid");

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

ast_log(LOG_WARNING, "Mettere qui la sostanza :)\n");

	return 0;

err_channel_not_down:
err_intf_not_found:
err_invalid_format:
err_invalid_destination:

	return err;
}

static int vgsm_answer(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	vgsm_debug("vgsm_answer\n");

	ast_indicate(ast_chan, -1);

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return -1;
	}

	return 0;
}

static int vgsm_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc,
	int timeoutms)
{
	return AST_BRIDGE_FAILED_NOWARN;
}

struct ast_frame *vgsm_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "vgsm_exception\n");

	return NULL;
}

/* We are called with chan->lock'ed */
static int vgsm_indicate(struct ast_channel *ast_chan, int condition)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return 1;
	}

	vgsm_debug("vgsm_indicate %d\n", condition);

	switch(condition) {
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
		return 1;
	break;

	case -1:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_OFFHOOK: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "dial");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_HANGUP: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "congestion");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_RINGING: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "ring");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_ANSWER:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_BUSY: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "busy");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_CONGESTION: {
		const struct tone_zone_sound *tone;
		tone = ast_get_indication_tone(ast_chan->zone, "busy");
		if (tone)
			ast_playtones_start(ast_chan, 0, tone->data, 1);

		return 0;
	}
	break;

	case AST_CONTROL_PROGRESS:
		return 0;
	break;

	case AST_CONTROL_PROCEEDING:
		return 0;
	break;
	}

	return 1;
}

static int vgsm_fixup(
	struct ast_channel *oldchan,
	struct ast_channel *newchan)
{
	struct vgsm_chan *chan = to_vgsm_chan(newchan);

	if (chan->ast_chan != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n",
				oldchan, chan->ast_chan);
		return -1;
	}

	chan->ast_chan = newchan;

	return 0;
}

static int vgsm_setoption(
	struct ast_channel *ast_chan,
	int option,
	void *data,
	int datalen)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int vgsm_transfer(
	struct ast_channel *ast,
	const char *dest)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int vgsm_send_digit(struct ast_channel *ast_chan, char digit)
{
	ast_log(LOG_NOTICE, "%s %c\n", __FUNCTION__, digit);

	return 1;
}

static int vgsm_sendtext(struct ast_channel *ast, const char *text)
{
	ast_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void vgsm_destroy(struct vgsm_chan *vgsm_chan)
{
	free(vgsm_chan);
}

static struct vgsm_chan *vgsm_alloc()
{
	struct vgsm_chan *vgsm_chan;

	vgsm_chan = malloc(sizeof(*vgsm_chan));
	if (!vgsm_chan)
		return NULL;

	memset(vgsm_chan, 0, sizeof(*vgsm_chan));

	vgsm_chan->channel_fd = -1;

	return vgsm_chan;
}

static int vgsm_hangup(struct ast_channel *ast_chan)
{
	vgsm_debug("vgsm_hangup %s\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	ast_mutex_lock(&ast_chan->lock);

	close(ast_chan->fds[0]);

	if (vgsm_chan) {
/*		if (vgsm_chan->channel_fd >= 0) {
			// Disconnect the softport since we cannot rely on
			// libq931 (see above)
			if (ioctl(vgsm_chan->channel_fd,
					VISDN_IOC_DISCONNECT, NULL) < 0) {
				ast_log(LOG_ERROR,
					"ioctl(VISDN_IOC_DISCONNECT): %s\n",
					strerror(errno));
			}

			if (close(vgsm_chan->channel_fd) < 0) {
				ast_log(LOG_ERROR,
					"close(vgsm_chan->channel_fd): %s\n",
					strerror(errno));
			}

			vgsm_chan->channel_fd = -1;
		}

		vgsm_destroy(vgsm_chan);

	ast_chan->tech_pvt = NULL;
*/
	}

	ast_setstate(ast_chan, AST_STATE_DOWN);

	ast_mutex_unlock(&ast_chan->lock);

	vgsm_debug("vgsm_hangup complete\n");

	return 0;
}

static struct ast_frame *vgsm_read(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	static struct ast_frame f;
	char buf[512];

	read(ast_chan->fds[0], buf, 1);

	f.src = VGSM_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	if (vgsm_chan->channel_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;

		return &f;
	}

	int nread = read(vgsm_chan->channel_fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return &f;
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "R %.3f %d\n",
	t/1000000.0,
	vgsm_chan->channel_fd);
#endif

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = nread;
	f.datalen = nread;
	f.data = buf;
	f.offset = 0;

	return &f;
}

static int vgsm_write(
	struct ast_channel *ast_chan,
	struct ast_frame *frame)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING,
			"Don't know what to do with frame type '%d'\n",
			frame->frametype);

		return 0;
	}

	if (frame->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (vgsm_chan->channel_fd < 0) {
//		ast_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

#if 0
ast_verbose(VERBOSE_PREFIX_3 "W %d %02x%02x%02x%02x%02x%02x%02x%02x %d\n", vgsm_chan->channel_fd,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);
#endif

	write(vgsm_chan->channel_fd, frame->data, frame->datalen);

	return 0;
}

static const struct ast_channel_tech vgsm_tech;

static struct ast_channel *vgsm_new(
	struct vgsm_chan *vgsm_chan,
	int state)
{
	struct ast_channel *ast_chan;
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		return NULL;
	}

	ast_chan->type = VGSM_CHAN_TYPE;

	ast_chan->fds[0] = open("/dev/visdn/timer", O_RDONLY);
	if (ast_chan->fds[0] < 0) {
		ast_log(LOG_ERROR, "Unable to open timer: %s\n",
			strerror(errno));
		return NULL;
	}

	if (state == AST_STATE_RING)
		ast_chan->rings = 1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;
	ast_chan->rawwriteformat = AST_FORMAT_ALAW;

//	ast_chan->language[0] = '\0';
//	ast_set_flag(ast_chan, AST_FLAG_DIGITAL);

	vgsm_chan->ast_chan = ast_chan;
	ast_chan->tech_pvt = vgsm_chan;

	ast_chan->tech = &vgsm_tech;

	ast_setstate(ast_chan, state);

	return ast_chan;
}

static struct ast_channel *vgsm_request(
	const char *type, int format, void *data, int *cause)
{
	struct vgsm_chan *vgsm_chan;
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

	vgsm_chan = vgsm_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		return NULL;
	}

	struct ast_channel *ast_chan;
	ast_chan = vgsm_new(vgsm_chan, AST_STATE_DOWN);

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VGSM/null");

	ast_mutex_lock(&usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	return ast_chan;
}

static const struct ast_channel_tech vgsm_tech = {
	.type		= VGSM_CHAN_TYPE,
	.description	= VGSM_DESCRIPTION,
	.capabilities	= AST_FORMAT_ALAW,
	.requester	= vgsm_request,
	.call		= vgsm_call,
	.hangup		= vgsm_hangup,
	.answer		= vgsm_answer,
	.read		= vgsm_read,
	.write		= vgsm_write,
	.indicate	= vgsm_indicate,
	.transfer	= vgsm_transfer,
	.fixup		= vgsm_fixup,
	.send_digit	= vgsm_send_digit,
	.bridge		= vgsm_bridge,
	.send_text	= vgsm_sendtext,
	.setoption	= vgsm_setoption,
};

// Must be called with vgsm.lock acquired
static void vgsm_add_interface(const char *name)
{
	int found = FALSE;
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcasecmp(intf->name, name)) {
			found = TRUE;
			break;
		}
	}

	if (!found) {
		intf = malloc(sizeof(*intf));

		strncpy(intf->name, name, sizeof(intf->name));
		vgsm_copy_interface_config(intf, &vgsm.default_intf);

		list_add_tail(&intf->ifs_node, &vgsm.ifs);
	}
}

// Must be called with vgsm.lock acquired
static void vgsm_rem_interface(const char *name)
{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		if (!strcmp(intf->name, name)) {

			break;
		}
	}
}

/*
static void vgsm_connect_channel(
	struct q931_channel *channel)
{
	FUNC_DEBUG();

	assert(channel->call);
	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);
	assert(ast_chan->pvt);
	assert(ast_chan->tech_pvt);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	char path[100], dest[100];
	snprintf(path, sizeof(path),
		"/sys/class/net/%s/vgsm_channel/connected/../B%d",
		channel->intf->name,
		channel->id+1);

	memset(dest, 0, sizeof(dest));
	if (readlink(path, dest, sizeof(dest) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", path, strerror(errno));
		goto err_readlink;
	}

	char *chanid = strrchr(dest, '/');
	if (!chanid || !strlen(chanid + 1)) {
		ast_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest);
		goto err_invalid_chanid;
	}

	strncpy(vgsm_chan->vgsm_chanid, chanid + 1,
		sizeof(vgsm_chan->vgsm_chanid));

	if (vgsm_chan->is_voice) {
		vgsm_debug("Connecting streamport to chan %s\n",
				vgsm_chan->vgsm_chanid);

		vgsm_chan->channel_fd = open("/dev/vgsm/streamport", O_RDWR);
		if (vgsm_chan->channel_fd < 0) {
			ast_log(LOG_ERROR,
				"Cannot open streamport: %s\n",
				strerror(errno));
			goto err_open;
		}

		struct vgsm_connect vc;
		strcpy(vc.src_chanid, "");
		snprintf(vc.dst_chanid, sizeof(vc.dst_chanid), "%s",
			vgsm_chan->vgsm_chanid);
		vc.flags = 0;

		if (ioctl(vgsm_chan->channel_fd, VGSM_IOC_CONNECT,
		    (caddr_t) &vc) < 0) {
			ast_log(LOG_ERROR,
				"ioctl(VGSM_CONNECT): %s\n",
				strerror(errno));
			goto err_ioctl;
		}
	}

	ast_mutex_unlock(&ast_chan->lock);

	return;

err_ioctl:
err_open:
err_invalid_chanid:
err_readlink:

	ast_mutex_unlock(&ast_chan->lock);
}

static void vgsm_q931_disconnect_channel(
	struct q931_channel *channel)
{
	FUNC_DEBUG();

	struct ast_channel *ast_chan = callpvt_to_astchan(channel->call);

	if (!ast_chan)
		return;

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	ast_mutex_lock(&ast_chan->lock);

	if (vgsm_chan->channel_fd >= 0) {
		if (ioctl(vgsm_chan->channel_fd,
				VGSM_IOC_DISCONNECT, NULL) < 0) {
			ast_log(LOG_ERROR,
				"ioctl(VGSM_IOC_DISCONNECT): %s\n",
				strerror(errno));
		}

		if (close(vgsm_chan->channel_fd) < 0) {
			ast_log(LOG_ERROR,
				"close(vgsm_chan->channel_fd): %s\n",
				strerror(errno));
		}

		vgsm_chan->channel_fd = -1;
	}

	ast_mutex_unlock(&ast_chan->lock);
}
*/






















static char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;
	int len = 0;

	while(*c) {
		switch(*c) {
		case '\r': len += snprintf(buf + len, bufsize - len, "<cr>"); break;
		case '\n': len += snprintf(buf + len, bufsize - len, "<lf>"); break;
		default:
			if (isprint(*c)) {
				len += snprintf(buf + len, bufsize - len, "%c", *c);
			} else
				len += snprintf(buf + len, bufsize - len, ".");
		}

		c++;
	}

	return buf;
}

#if 0
static int vgsm_read_response(struct vgsm_interface *intf, char *resp, int resp_size, int timeout)
{
}

static int vgsm_module_escape(struct vgsm_interface *intf)
{
	sleep(1);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING, "write to module failed: %s\n", strerror(errno));
		return -1;
	}

	usleep(100000);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING, "write to module failed: %s\n", strerror(errno));
		return -1;
	}

	usleep(100000);

	if (write(intf->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING, "write to module failed: %s\n", strerror(errno));
		return -1;
	}

	sleep(1);

	char junk;
	while(read(intf->fd, &junk, 1) > 0);

	return 0;
}

static int vgsm_expect_ok(struct vgsm_interface *intf, int timeout)
{
	char resp[64];

	if (vgsm_read_response(intf, resp, sizeof(resp), timeout) < 0)
		return -1;

	return strcmp(resp, "OK") ? -1 : 0;
}

static int vgsm_module_init(struct vgsm_interface *intf)
{
	char resp[64];

	ast_log(LOG_NOTICE, "Initializing module '%s'\n", intf->name);

//	vgsm_module_escape(intf);

	vgsm_send_message(intf, "ATZ");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

/* CHIEDERE A VANNICOLA:
 *
 * Usare XON/XOFF o i micro riescono a gestire RTS/CTS ?
 * 

Guardate ATX1234
*/

	vgsm_send_message(intf, "AT &F E0 V1 Q0 &K4");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	vgsm_send_message(intf, "AT+IPR=38400");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	vgsm_send_message(intf, "AT+CMEE=1");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	vgsm_send_message(intf, "AT#PCT");
	vgsm_read_response(intf,
			intf->sim_remaining_attempts,
			sizeof(intf->sim_remaining_attempts), 500);

	vgsm_send_message(intf, "AT+CPIN");
	if (vgsm_read_response(intf, resp, sizeof(resp), 20 * 1000) < 0)
		return -1;

	if (!strcmp(resp, "+CPIN: READY")) {
		ast_verbose(VERBOSE_PREFIX_1 "SIM ready\n");
	} else if (!strcmp(resp, "+CPIN: SIM PIN")) {

		ast_verbose(VERBOSE_PREFIX_1 "SIM requires PIN\n");

#if 0
		/* Only do ONE attempt automatically, further attempts must
		 * be explicitly required by the user */

		vgsm_send_message(intf, "AT+CPIN=%s", intf->pin);
		if (vgsm_expect_ok(intf, 20 * 1000) < 0) {
			ast_log(LOG_WARNING,
				"SIM PIN refused, aborting module initialization\n");

			return -1;
		}
#else
		return -1;
#endif
	} else if (!strcmp(resp, "+CPIN: SIM PIN2")) {
		ast_log(LOG_WARNING,
			"SIM requires PIN2, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CPIN: SIM PUK")) {
		ast_log(LOG_WARNING,
			"SIM requires PUK, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CPIN: SIM PUK2")) {
		ast_log(LOG_WARNING,
			"SIM requires PUK2, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CME ERROR: 10")) {
		ast_log(LOG_NOTICE,
			"SIM not present, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CME ERROR: 13")) {
		ast_log(LOG_WARNING,
			"SIM defective, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CME ERROR: 14")) {
		ast_log(LOG_WARNING,
			"SIM busy, aborting module initialization\n");

		return -1;
	} else if (!strcmp(resp, "+CME ERROR: 15")) {
		ast_log(LOG_WARNING,
			"Wrong type of SIM, aborting module initialization\n");

		return -1;
	} else {
		ast_log(LOG_WARNING,
			"Unknown response '%s'"
			", aborting module initialization\n", resp);

		return -1;
	}

	/* Enable extended cellular result codes */
	vgsm_send_message(intf, "AT+CRC=1");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Enable Calling Line Presentation */
	vgsm_send_message(intf, "AT+CLIP=1");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Enable unsolicited advice of charge notifications */
	vgsm_send_message(intf, "AT+CAOC=2");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Sets current time on module */
	if (intf->set_clock) {
		vgsm_send_message(intf, "AT+CCLK=%s", timestamp);
		if (vgsm_expect_ok(intf, 500) < 0)
			return -1;
	}

	/* Enable unsolicited new message indications */
	vgsm_send_message(intf, "AT+CNMI=2,1,2,1,0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Select handsfree audio path */
	vgsm_send_message(intf, "AT#CAP=1");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Disable tones */
	vgsm_send_message(intf, "AT#STM=0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Set ringer auto directed to GPIO7 */
	vgsm_send_message(intf, "AT#SRP=3");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

// SET #SGPO
	/* Enable unsolicited SIM status reporting */
	vgsm_send_message(intf, "AT#QSS=1");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Disable handsfree echo canceller */
	vgsm_send_message(intf, "AT#SHFEC=0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Set MIC audio gain to +0dB */
	vgsm_send_message(intf, "AT#HFMICG=0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Disable sidetone generation */
	vgsm_send_message(intf, "AT#SHFSD=0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Set GSM1800 band */
	vgsm_send_message(intf, "AT#BND=0");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;

	/* Enable Jammer detector */
	vgsm_send_message(intf, "AT#JDR=2");
	if (vgsm_expect_ok(intf, 500) < 0)
		return -1;


// Read info
	vgsm_send_message(intf, "AT+CGMI");
	vgsm_read_response(intf,
			intf->module_vendor,
			sizeof(intf->module_vendor), 5 * 1000);

	vgsm_send_message(intf, "AT+CGMM");
	vgsm_read_response(intf,
			intf->module_model,
			sizeof(intf->module_model), 5 * 1000);

	vgsm_send_message(intf, "ATI5");
	vgsm_read_response(intf,
			intf->module_version,
			sizeof(intf->module_version), 5 * 1000);

	vgsm_send_message(intf, "AT+CGMR");
	vgsm_read_response(intf,
			intf->module_revision,
			sizeof(intf->module_revision), 5 * 1000);

	vgsm_send_message(intf, "AT+GSN");
	vgsm_read_response(intf,
			intf->module_serial_number,
			sizeof(intf->module_serial_number), 5 * 1000);

	vgsm_send_message(intf, "AT+CGSN");
	vgsm_read_response(intf,
			intf->module_imei,
			sizeof(intf->module_imei), 5 * 1000);

	vgsm_send_message(intf, "AT+CIMI");
	vgsm_read_response(intf,
			intf->sim_imsi,
			sizeof(intf->sim_imsi), 5 * 1000);

/* */
/*	vgsm_send_message(intf, "AT+CSQ"); // Read signal quality
	AT#MONI
	AT#CSURV

*/

	if (vgsm_send(p, "ATI3", 0)) {
	if (vgsm_send(p, "ATI4", 0)) {
	if (vgsm_send(p, "AT+CPIN=4911", 0) ||
	if (vgsm_send(p, "AT+FCLASS=8", 0) ||
	if (vgsm_send(p, "AT&K3", 0) ||

	return 0;
}
#endif

static int vgsm_send_message(struct vgsm_interface *intf, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

static int vgsm_send_message(struct vgsm_interface *intf, const char *fmt, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, fmt);

	if (vsnprintf(buf, sizeof(buf), fmt, ap) >= sizeof(buf) - 1)
		return -1;

	strcat(buf, "\r");

	char tmpstr[80];
	ast_verbose("=> '%s'\n", unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	return write(intf->fd, buf, strlen(buf));
}

static int vgsm_handle_message(struct vgsm_interface *intf, const char *msg)
{
	char tmpstr[80];
	ast_verbose("<= '%s'\n", unprintable_escape(msg, tmpstr, sizeof(tmpstr)));

	return 0;
}

static int vgsm_receive(struct vgsm_interface *intf)
{
ast_verbose("rec1\n");
	int buflen = strlen(intf->buf);
	int nread = read(intf->fd, intf->buf + buflen, sizeof(intf->buf) - buflen - 1);
	if (nread < 0) {
		ast_log(LOG_WARNING, "Error reading from serial: %s\n",
			strerror(errno));

		return -1;
	}

ast_verbose("rec2\n");
	intf->buf[buflen + nread] = '\0';

	while(1) {
		const char *begincr, *endlf;


char tmpstr[80];
ast_log(LOG_WARNING, "BUF='%s'\n", unprintable_escape(intf->buf, tmpstr, sizeof(tmpstr)));

ast_verbose("rec3\n");
		begincr = strstr(intf->buf, "\r\n");
		if (!begincr)
			break;

ast_verbose("rec4\n");
		if (begincr > intf->buf) {
			char *dropped = malloc(begincr - intf->buf + 1);
			char tmpstr[80];

			memcpy(dropped, intf->buf, begincr - intf->buf);
			dropped[begincr - intf->buf] = '\0';

			memmove(intf->buf, begincr + 1, strlen(begincr + 1) + 1);

			ast_log(LOG_WARNING,
				"Dropped spurious bytes '%s' from serial\n",
				unprintable_escape(dropped, tmpstr, sizeof(tmpstr)));

			continue;
		}

ast_verbose("rec5\n");
		endlf = strstr(begincr + 2, "\r\n");
		if (!endlf)
			break;

ast_verbose("rec6\n");
		endlf++;

		int chars_copied = (endlf - begincr + 1) - 4;

		assert(chars_copied >= 0);


		char msg[80];
		if (chars_copied >= sizeof(msg)) {
			ast_log(LOG_WARNING, "Response too long\n");
		} else {
			strncpy(msg, begincr + 2, chars_copied);
			msg[chars_copied] = '\0';

			vgsm_handle_message(intf, msg);
		}

		memmove(intf->buf, endlf + 1, strlen(endlf + 1) + 1);
	}

	return 0;
}

static int vgsm_maint_thread_do_stuff()
{
	struct pollfd polls[64];
	struct vgsm_interface *ifs[64];
	int npolls = 0;

	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
		polls[npolls].fd = intf->fd;
		polls[npolls].events = POLLERR | POLLIN;

		ifs[npolls] = intf;

		npolls++;
	}


	for(;;) {
ast_verbose("poll1\n");
		int res = poll(polls, npolls, -1);
		if (res < 0) {
			ast_log(LOG_WARNING, "Error polling serial: %s\n",
				strerror(errno));

			return -1;
		} else if (res == 0) {
			ast_log(LOG_WARNING, "Timeout polling serials\n");

			return -1;
		}
ast_verbose("poll2\n");

		int i;
		for (i=0; i<npolls; i++) {
			if (polls[i].revents & POLLIN)
				vgsm_receive(ifs[i]);
		}

ast_verbose("poll3\n");
	}

	return 0;
}

static void *vgsm_maint_thread_main(void *data)
{
	sleep(2);

	ast_mutex_lock(&vgsm.lock);

	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {
//		vgsm_module_init(intf);
		vgsm_send_message(intf, "ATZ");
	}

	ast_mutex_unlock(&vgsm.lock);

	while(vgsm_maint_thread_do_stuff());

	return NULL;
}

int load_module()
{
	int res = 0;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	ast_mutex_init(&vgsm.lock);

	INIT_LIST_HEAD(&vgsm.ifs);

	vgsm_reload_config();

	if (ast_channel_register(&vgsm_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VGSM_CHAN_TYPE);
		return -1;
	}

	ast_cli_register(&debug_vgsm_generic);
	ast_cli_register(&no_debug_vgsm_generic);
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&show_vgsm_interfaces);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&vgsm_maint_thread, &attr,
					vgsm_maint_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start maintainance thread.\n");
		return -1;
	}

	return res;
}

int unload_module(void)
{
	ast_cli_unregister(&show_vgsm_interfaces);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	return 0;
}

int reload(void)
{
	vgsm_reload_config();

	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = vgsm.usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return VGSM_DESCRIPTION;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
