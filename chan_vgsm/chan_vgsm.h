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

	char vgsm_chanid[30];
	int channel_fd;

	char calling_number[21];
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

enum vgsm_interface_status
{
	VGSM_INT_STATUS_UNINITIALIZED,
	VGSM_INT_STATUS_INITIALIZING,
	VGSM_INT_STATUS_READY,
	VGSM_INT_STATUS_INCALL,
	VGSM_INT_STATUS_NOT_READY,
	VGSM_INT_STATUS_LOCKED_DOWN,
	VGSM_INT_STATUS_FAILED,
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

	enum vgsm_interface_status status;

	int connect_check_sched_id;

	struct ast_channel *current_call;

	struct vgsm_comm comm;

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

	struct list_head ifs;
	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	int debug;
	

	struct vgsm_interface default_intf;
};

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}
