/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Generic Linux Telephony Interface driver
 * 
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
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>

static char *desc = "ISDN Channel For Asterisk";
static char *type = "ISDN";
static char *tdesc = "ISDN Channel Implementation";

static int usecnt =0;

/* Only linear is allowed */
static int prefformat = AST_FORMAT_SLINEAR;

AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static char context[AST_MAX_EXTENSION] = "default";

/* NBS creates private structures on demand */
   
struct isdn_pvt {
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char app[16];					/* Our app */
	struct ast_frame fr;			/* "null" frame */
};

static int isdn_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct isdn_pvt *p;

	p = ast->pvt->pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "isdn_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}
	
	/* When we call, it just works, really, there's no destination...  Just
	   ring the phone and wait for someone to answer */
	
	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast->name);

	/* If we can't connect, return congestion */
	
	if (0) {
		ast_log(LOG_WARNING, "NBS Connection failed on %s\n", ast->name);
		ast_queue_control(ast, AST_CONTROL_CONGESTION);
	} else {
		ast_setstate(ast, AST_STATE_RINGING);
		ast_queue_control(ast, AST_CONTROL_ANSWER);
	}

	return 0;
}

static void isdn_destroy(struct isdn_pvt *p)
{
	free(p);
}

static struct isdn_pvt *isdn_alloc(void *data)
{
	struct isdn_pvt *p;

		
	p = malloc(sizeof(struct isdn_pvt));
	
	if (p) {
		memset(p, 0, sizeof(struct isdn_pvt));

		
	}
	return p;
}

static int isdn_hangup(struct ast_channel *ast)
{
	struct isdn_pvt *p;
	p = ast->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "isdn_hangup(%s)\n", ast->name);
	if (!ast->pvt->pvt) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}
	isdn_destroy(p);
	ast->pvt->pvt = NULL;
	ast_setstate(ast, AST_STATE_DOWN);
	return 0;
}

static struct ast_frame  *isdn_read(struct ast_channel *ast)
{
	struct isdn_pvt *p = ast->pvt->pvt;
	

	/* Some nice norms */
	p->fr.datalen = 0;
	p->fr.samples = 0;
	p->fr.data =  NULL;
	p->fr.src = type;
	p->fr.offset = 0;
	p->fr.mallocd=0;
	p->fr.delivery.tv_sec = 0;
	p->fr.delivery.tv_usec = 0;

	ast_log(LOG_DEBUG, "Returning null frame on %s\n", ast->name);

	return &p->fr;
}

static int isdn_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct isdn_pvt *p = ast->pvt->pvt;
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
	if (ast->_state != AST_STATE_UP) {
		/* Don't try tos end audio on-hook */
		return 0;
	}
	
	// WRITE THE FRAME
		
	return 0;
}

static struct ast_channel *isdn_new(struct isdn_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc(1);
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "ISDN/null");
		tmp->type = type;
		tmp->fds[0] = 0;
		tmp->nativeformats = prefformat;
		tmp->pvt->rawreadformat = prefformat;
		tmp->pvt->rawwriteformat = prefformat;
		tmp->writeformat = prefformat;
		tmp->readformat = prefformat;
		ast_setstate(tmp, state);
		if (state == AST_STATE_RING)
			tmp->rings = 1;
		tmp->pvt->pvt = i;
		tmp->pvt->call = isdn_call;
		tmp->pvt->hangup = isdn_hangup;
		tmp->pvt->read = isdn_read;
		tmp->pvt->write = isdn_write;
		strncpy(tmp->context, context, sizeof(tmp->context)-1);
		strncpy(tmp->exten, "s",  sizeof(tmp->exten) - 1);
		tmp->language[0] = '\0';
		i->owner = tmp;
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return tmp;
}


static struct ast_channel *isdn_request(const char *type, int format, void *data, int *cause)
{
	int oldformat;
	struct isdn_pvt *p;
	struct ast_channel *tmp = NULL;
	
	oldformat = format;
	format &= (AST_FORMAT_SLINEAR);
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", oldformat);
		return NULL;
	}
	return tmp;
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

int load_module()
{
	/* Make sure we can register our Adtranphone channel type */
	if (ast_channel_register(type, tdesc, 
			 AST_FORMAT_SLINEAR, isdn_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		__unload_module();
		return -1;
	}
	return 0;
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
