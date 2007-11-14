/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _MODULE_H
#define _MODULE_H

#include <asterisk/channel.h>

#include <list.h>

#include "longtime.h"
#include "timer.h"
#include "comm.h"
#include "mesim.h"
#include "number.h"
#include "sim.h"

#define VGSM_ME_PREFIX "me:"
#define VGSM_ME_GLOBAL "me:*"

#ifdef DEBUG_CODE
#define vgsm_debug_state(module, format, arg...)	\
	if ((module)->debug_state)			\
		ast_verbose("vgsm: %s: "		\
			format,				\
			(module)->name,			\
			## arg)
#define vgsm_debug_call(module, format, arg...)		\
	if ((module)->debug_call)			\
		ast_verbose("vgsm: %s: "		\
			format,				\
			(module)->name,			\
			## arg)
#else
#define vgsm_debug_state(module, format, arg...)	\
	do {} while(0);
#define vgsm_debug_call(module, format, arg...)		\
	do {} while(0);
#endif

enum vgsm_codec
{
	VGSM_CODEC_GSM_EFR,
	VGSM_CODEC_GSM_FR,
	VGSM_CODEC_GSM_HR,
	VGSM_CODEC_AMR_HR,
	VGSM_CODEC_AMR_FR,
};

enum vgsm_module_status
{
	VGSM_MODULE_STATUS_CLOSED,
	VGSM_MODULE_STATUS_OFF,
	VGSM_MODULE_STATUS_POWERING_ON,
	VGSM_MODULE_STATUS_POWERING_OFF,
	VGSM_MODULE_STATUS_RESETTING,
	VGSM_MODULE_STATUS_WAITING_INITIALIZATION,
	VGSM_MODULE_STATUS_INITIALIZING,
	VGSM_MODULE_STATUS_READY,
	VGSM_MODULE_STATUS_WAITING_SIM,
	VGSM_MODULE_STATUS_WAITING_PIN,
	VGSM_MODULE_STATUS_FAILED,
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
	VGSM_OPSEL_MANUAL = 1,
	VGSM_OPSEL_DEREGISTERED = 2,
	VGSM_OPSEL_MANUAL_FALLBACK = 4,
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
	__u16 mcc;
	__u16 mnc;
	__u16 lac;
	__u16 id;
	__u16 bsic;
	__u16 arfcn;
	__u16 rx_lev;
};

struct vgsm_counter
{
	struct list_head node;

	__u16 location;
	__u16 reason;
	__u32 count;
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

//	struct vgsm_chan *vgsm_chan;
};

struct vgsm_cbm_recorded
{
	struct list_head node;

	wchar_t *text;
};

enum vgsm_flow_control
{
	VGSM_FLOW_AUTO,
	VGSM_FLOW_NONE,
	VGSM_FLOW_SW,
	VGSM_FLOW_HW
};

#define VGSM_SIM_ROUTE_EXTERNAL -1
#define VGSM_SIM_ROUTE_DEFAULT -2

struct vgsm_module;
struct vgsm_module_config
{
	int refcnt;

	struct vgsm_module *module;

	char device_filename[PATH_MAX];
	char mesim_device_filename[PATH_MAX];

	enum vgsm_flow_control flow_control;

	char context[AST_MAX_EXTENSION];

	char pin[16];
	__u8 rx_gain;
	__u8 tx_gain;

	enum vgsm_mesim_proto sim_proto;
	char sim_local_device_filename[PATH_MAX];
	struct sockaddr_in sim_impl_simclient_addr;

	BOOL set_clock;
	BOOL poweroff_on_exit;

	enum vgsm_operator_selection operator_selection;
	__s16 operator_mcc;
	__s16 operator_mnc;

	struct vgsm_number smcc_address;

	char sms_sender_domain[64];
	char sms_recipient_address[64];

	BOOL dtmf_quelch;
	BOOL dtmf_mutemax;
	BOOL dtmf_relax;

	BOOL amr_enabled;
	BOOL gsm_fr_enabled;
	BOOL gsm_hr_enabled;
	enum vgsm_codec gsm_preferred;

	int rx_calibrate;
	int tx_calibrate;

	int jitbuf_low;
	int jitbuf_high;
};

struct vgsm_module
{
	struct list_head ifs_node;

	int refcnt;

	ast_mutex_t lock;

	struct vgsm_module_config *current_config;

	char name[64];

	enum vgsm_module_status status;

	struct vgsm_timerset timerset;
	struct vgsm_timer timer;

	char *status_reason;
	int power_attempts;
	int failure_count;
	int failure_attempts;

	struct vgsm_call calls[4];

	struct vgsm_chan *incoming_call;
	struct vgsm_chan *vgsm_chan;
	BOOL call_present;

	BOOL sending_sms;

	int me_fd;
	struct vgsm_comm comm;
	enum vgsm_flow_control flow_control;

	int mesim_fd;
	struct vgsm_mesim mesim;

	int interface_version;

	pthread_t monitor_thread;
//	pthread_t mesim_monitor_thread;

	struct {
		__u8 ver_maj;
		__u8 ver_min;
		__u8 ver_ser;

		__u32 serial;
	} card;

	struct {
		char vendor[32];
		char model[32];
		char version[32];
		char imei[32];
	} module;

	struct {
		BOOL inserted;
		char imsi[32];
		char card_id[32];
		int remaining_attempts;
		struct vgsm_number smcc_address;
	} sim;

	struct {
		enum vgsm_net_status status;
		__u16 mcc;
		__u16 mnc;

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

	struct {
		int inbound;
		int outbound;
		struct list_head inbound_counters;
		struct list_head outbound_counters;

	} stats;

	BOOL debug_state;
	BOOL debug_call;
	BOOL debug_sms;
	BOOL debug_cbm;
	BOOL debug_jitbuf;
	BOOL debug_frames;
};

extern struct vgsm_urc_class vgsm_module_urcs[];

struct vgsm_module_config *vgsm_module_config_alloc(void);
struct vgsm_module_config *vgsm_module_config_get(
	struct vgsm_module_config *module_config);
void _vgsm_module_config_put(struct vgsm_module_config *module_config);
#define vgsm_module_config_put(module_config) \
	do {						\
		_vgsm_module_config_put(module_config);	\
		(module_config) = NULL;			\
	} while(0)

void vgsm_module_config_default(struct vgsm_module_config *mc);

struct vgsm_module *vgsm_module_alloc(void);
struct vgsm_module *vgsm_module_get(struct vgsm_module *module);
void _vgsm_module_put(struct vgsm_module *module);
#define vgsm_module_put(module) \
	do { _vgsm_module_put(module); (module) = NULL; } while(0)

struct vgsm_module *_vgsm_module_get_by_name(const char *name);
struct vgsm_module *vgsm_module_get_by_name(const char *name);

void vgsm_module_set_status(
	struct vgsm_module *module,
	enum vgsm_module_status status,
	longtime_t timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 4, 5)));

void vgsm_module_reload(struct ast_config *cfg);

void vgsm_module_chup_complete(struct vgsm_req *req, void *data);

void vgsm_module_counter_inc(
	struct vgsm_module *module,
	BOOL outbound,
	int location,
	int reason);
void vgsm_module_failed(struct vgsm_module *module, int err);

const char *vgsm_module_error_to_text(int code);

void vgsm_module_shutdown_all(void);

char *vgsm_module_completion(const char *line, const char *word, int state);

int vgsm_module_module_load(void);
int vgsm_module_module_unload(void);

#endif
