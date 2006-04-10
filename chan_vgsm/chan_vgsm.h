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

struct vgsm_chan {
	struct ast_channel *ast_chan;

	struct vgsm_interface *intf;

	int sp_fd;

	int sp_channel_id;
	int module_channel_id;

	int path_id;

	char calling_number[21];

	struct ast_dsp *dsp;
};

struct vgsm_operator_info
{
	struct list_head node;

	char id[8];
	const char *name;
	const char *country;
	const char *date;
	const char *bands;
};

enum vgsm_intf_status
{
	VGSM_INTF_STATUS_UNINITIALIZED,
	VGSM_INTF_STATUS_WAITING_INITIALIZATION,
	VGSM_INTF_STATUS_INITIALIZING,
	VGSM_INTF_STATUS_READY,
	VGSM_INTF_STATUS_INCALL,
	VGSM_INTF_STATUS_WAITING_PIN,
	VGSM_INTF_STATUS_NO_NET,
	VGSM_INTF_STATUS_LOCKED_DOWN,
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

	/* Operative data */

	enum vgsm_intf_status status;

	char *lockdown_reason;

	int call_monitor;

	struct ast_channel *current_call;

	struct vgsm_comm comm;

	pthread_t monitor_thread;

	struct
	{
		char number[30];
		int ton;
		char subaddress[30];
		int subaddress_type;
		char alpha[32];
		int validity;
	} last_cli;

	struct {
		char vendor[32];
		char model[32];
		char version[32];
		char dob_version[32];
		char serial_number[32];
		char imei[32];
	} module;

	struct {
		int inserted;
		char imsi[32];
		int remaining_attempts;
	} sim;

	struct {
		enum vgsm_net_status status;
		char operator_id[6];
		int rxqual;
		int ta;
		int bsic;

		struct {
			int id;
			int lac;
			int arfcn;
			int pwr;
		} cells[16];

		int ncells;
	} net;

};


struct vgsm_state
{
	ast_mutex_t lock;

	struct vgsm_interface default_intf;
	struct list_head ifs;

	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	int router_control_fd;

	int debug;
};

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}
