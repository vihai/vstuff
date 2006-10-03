/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <asterisk/channel.h>

#include <list.h>

#include "comm.h" 

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#ifndef AST_CONTROL_DISCONNECT
#define AST_CONTROL_DISCONNECT 43
#endif

struct vgsm_chan {
	struct ast_channel *ast_chan;

	struct vgsm_interface *intf;

	int sp_fd;

	int sp_channel_id;
	int module_channel_id;

	int pipeline_id;

	char calling_number[21];

	struct ast_dsp *dsp;
};

struct vgsm_operator_info
{
	struct list_head node;

	char id[8];
	char *name;
	char *country;
	char *date;
	char *bands;
};

enum vgsm_intf_status
{
	VGSM_INTF_STATUS_CLOSED,
	VGSM_INTF_STATUS_OFF,
	VGSM_INTF_STATUS_POWERING_ON,
	VGSM_INTF_STATUS_POWERING_OFF,
	VGSM_INTF_STATUS_WAITING_INITIALIZATION,
	VGSM_INTF_STATUS_INITIALIZING,
	VGSM_INTF_STATUS_READY,
	VGSM_INTF_STATUS_WAITING_SIM,
	VGSM_INTF_STATUS_WAITING_PIN,
	VGSM_INTF_STATUS_FAILED,
};

enum vgsm_net_status
{
	VGSM_NET_STATUS_NOT_SEARCHING,
	VGSM_NET_STATUS_NOT_REGISTERED,
	VGSM_NET_STATUS_REGISTERED_HOME,
	VGSM_NET_STATUS_UNKNOWN,
	VGSM_NET_STATUS_REGISTRATION_DENIED,
	VGSM_NET_STATUS_REGISTERED_ROAMING,
};

enum vgsm_operator_selection
{
	VGSM_OPSEL_AUTOMATIC = 0,
	VGSM_OPSEL_MANUAL_UNLOCKED = 1,
	VGSM_OPSEL_MANUAL_FALLBACK = 4,
	VGSM_OPSEL_MANUAL_LOCKED = 5,
};

enum vgsm_operator_status
{
	VGSM_OPSTAT_UNKNOWN,
	VGSM_OPSTAT_AVAILABLE,
	VGSM_OPSTAT_CURRENT,
	VGSM_OPSTAT_FORBIDDEN,
};

struct vgsm_net_cell
{
	int mcc;
	int mnc;
	int lac;
	int id;
	int bsic;
	int arfcn;
	int rx_lev;
};

struct vgsm_counter
{
	struct list_head node;

	int location;
	int reason;
	int count;
};

enum vgsm_call_direction
{
	VGSM_CALL_DIRECTION_MOBILE_ORIGINATED,
	VGSM_CALL_DIRECTION_MOBILE_TERMINATED
};

enum vgsm_call_state
{
	VGSM_CALL_STATE_UNUSED,
	VGSM_CALL_STATE_ACTIVE,
	VGSM_CALL_STATE_HELD,
	VGSM_CALL_STATE_DIALING,
	VGSM_CALL_STATE_ALERTING,
	VGSM_CALL_STATE_INCOMING,
	VGSM_CALL_STATE_WAITING,
	VGSM_CALL_STATE_TERMINATING,
	VGSM_CALL_STATE_DROPPED,
};

enum vgsm_call_bearer
{
	VGSM_CALL_BEARER_VOICE,
	VGSM_CALL_BEARER_DATA,
	VGSM_CALL_BEARER_FAX,
	VGSM_CALL_BEARER_VOICE_THEN_DATA_VOICE,
	VGSM_CALL_BEARER_VOICE_ALT_DATA_VOICE,
	VGSM_CALL_BEARER_VOICE_ALT_FAX_VOICE,
	VGSM_CALL_BEARER_VOICE_THEN_DATA_DATA,
	VGSM_CALL_BEARER_VOICE_ALT_DATA_DATA,
	VGSM_CALL_BEARER_VOICE_ALT_FAX_FAX,
	VGSM_CALL_BEARER_VOICE_UNKNOWN
};

struct vgsm_call
{
	enum vgsm_call_direction direction;
	enum vgsm_call_state state;
	enum vgsm_call_bearer bearer;
	int multiparty;
	int channel_assigned;

	int updated;
};

struct vgsm_interface
{
	struct list_head ifs_node;

	int refcnt;

	ast_mutex_t lock;

	/* Configuration */
	char name[64];
	char device_filename[PATH_MAX];

	char context[AST_MAX_EXTENSION];

	char pin[16];
	int rx_gain;
	int tx_gain;
	enum vgsm_operator_selection operator_selection;
	char operator_id[8];
	int set_clock;

	int poweroff_on_exit;

	char sms_service_center[32];
	char sms_sender_domain[64];
	char sms_recipient_address[64];

	int dtmf_quelch;
	int dtmf_mutemax;
	int dtmf_relax;

	/* Operative data */

	enum vgsm_intf_status status;
	longtime_t timer_expiration;

	char *lockdown_reason;
	int power_attempts;

	int call_monitor;

	struct vgsm_call calls[4];

	struct ast_channel *active_call;

	int sending_sms;

	struct vgsm_comm comm;

	struct list_head counters;

	pthread_t monitor_thread;

	struct {
		char vendor[32];
		char model[32];
		char version[32];
		char imei[32];
	} module;

	struct {
		int inserted;
		char imsi[32];
		char card_id[32];
		int remaining_attempts;
	} sim;

	struct {
		enum vgsm_net_status status;
		char operator_id[6];

		struct vgsm_net_cell sci;

		struct {
			int rx_lev_full;
			int rx_lev_sub;
			int rx_qual;
			int rx_qual_full;
			int rx_qual_sub;
			int timeslot;
			int ta;
			int rssi;
			int ber;
		} sci2;

		struct vgsm_net_cell nci[10];

		int ncells;
	} net;
};

#define vgsm_intf_put_null(i)	do { vgsm_intf_put(i); i = NULL; } while(0)

struct vgsm_state
{
	ast_mutex_t lock;

	struct vgsm_interface default_intf;
	struct list_head ifs;

	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	int router_control_fd;

	int debug_generic;
	int debug_serial;

	char sms_spooler[32];
	char sms_spooler_pars[32];
};

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

struct vgsm_operator_info *vgsm_search_operator(const char *id);
