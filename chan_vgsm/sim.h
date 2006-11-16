/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_SIM_H
#define _VGSM_SIM_H

enum vgsm_sim_type_of_file
{
	VGSM_SIM_TOF_RFU	= 0x00,
	VGSM_SIM_TOF_MF		= 0x01,
	VGSM_SIM_TOF_DF		= 0x02,
	VGSM_SIM_TOF_EF		= 0x04,
};

enum vgsm_sim_dcs_language
{
	VGSM_SIM_DCS_LANG_GERMAN	= 0x0,
	VGSM_SIM_DCS_LANG_ENGLISH	= 0x1,
	VGSM_SIM_DCS_LANG_ITALIAN	= 0x2,
	VGSM_SIM_DCS_LANG_FRENCH	= 0x3,
	VGSM_SIM_DCS_LANG_SPANISH	= 0x4,
	VGSM_SIM_DCS_LANG_DUTCH		= 0x5,
	VGSM_SIM_DCS_LANG_SWEDISH	= 0x6,
	VGSM_SIM_DCS_LANG_DANISH	= 0x7,
	VGSM_SIM_DCS_LANG_PORTUGUESE	= 0x8,
	VGSM_SIM_DCS_LANG_FINNISH	= 0x9,
	VGSM_SIM_DCS_LANG_NORWEGIAN	= 0xa,
	VGSM_SIM_DCS_LANG_GREEK		= 0xb,
	VGSM_SIM_DCS_LANG_TURKISH	= 0xc,
	VGSM_SIM_DCS_LANG_HUNGARIAN	= 0xd,
	VGSM_SIM_DCS_LANG_POLISH	= 0xe,
	VGSM_SIM_DCS_LANG_UNSPECIFIED	= 0xf
};

struct vgsm_sim_file_stats {
	__u16 pad1;
	__u16 length;
	__u16 file_id;
	__u8 type_of_file;
	__u8 pad2[5];
	__u8 gsm_data_len;

	union {
	__u8 gsm_specific_data[21];
	struct {
	__u8 characteristics;
	__u8 num_df_childs;
	__u8 num_ef_childs;
	__u8 num_chv_codes;
	__u8 pad3;
	__u8 chv1_status;
	__u8 unblock_chv1_status;
	__u8 chv2_status;
	__u8 unblock_chv2_status;
	};
	};
} __attribute__ ((__packed__));

struct vgsm_sim
{
	int refcnt;

	struct vgsm_comm *comm;
};

struct vgsm_sim_file
{
	int refcnt;

	struct vgsm_sim *sim;

	__u16 id;

	enum vgsm_sim_type_of_file type;

	__u8 length;
};

struct vgsm_sim *vgsm_sim_alloc(struct vgsm_comm *comm);
struct vgsm_sim *vgsm_sim_get(struct vgsm_sim *sim);
void _vgsm_sim_put(struct vgsm_sim *sim);
#define vgsm_sim_put(sim) \
	do { _vgsm_sim_put(sim); (sim) = NULL; } while(0)

struct vgsm_sim_file *vgsm_sim_file_alloc(struct vgsm_sim *sim);
struct vgsm_sim_file *vgsm_sim_file_get(
	struct vgsm_sim_file *sim_file);
void _vgsm_sim_file_put(struct vgsm_sim_file *sim_file);
#define vgsm_sim_file_put(sim_file) \
	do { _vgsm_sim_file_put(sim_file); (sim_file) = NULL; } while(0)
int vgsm_sim_file_open(struct vgsm_sim_file *sim_file, __u16 file_id);
int vgsm_sim_file_read(
	struct vgsm_sim_file *sim_file,
	void *data, __u16 offset, __u8 len);
void vgsm_sim_file_stats_dump(struct vgsm_sim_file *sim_file);

const char *vgsm_sim_language_to_text(enum vgsm_sim_dcs_language value);

#endif
