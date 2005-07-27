/*
 * FIXME
 */

#ifndef _HFC_FIFO_H
#define _HFC_FIFO_H

#define hfc_SM_D_FIFO_OFF 2
#define hfc_SM_B1_FIFO_OFF 0
#define hfc_SM_B2_FIFO_OFF 1
#define hfc_SM_E_FIFO_OFF 3

#ifdef DEBUG
#define hfc_debug_fifo(dbglevel, fifo, format, arg...)		\
	if (debug_level >= dbglevel)				\
		printk(KERN_DEBUG hfc_DRIVER_PREFIX		\
			"card: %d "				\
			"fifo: %d "				\
			format,					\
			(fifo)->card->id,			\
			(fifo)->hw_index,			\
			## arg)
#else
#define hfc_debug_fifo(dbglevel, chan, format, arg...) do {} while (0)
#endif

#define hfc_msg_fifo(level, fifo, format, arg...)	\
	printk(level hfc_DRIVER_PREFIX			\
		"card: %d "				\
		"fifo: %d "				\
		format,					\
		(fifo)->card->id,			\
		(fifo)->hw_index,			\
		## arg)

union hfc_zgroup
{
	struct { u16 z1,z2; };
	u32 z1z2;
};

union hfc_fgroup
{
	struct { u8 f1,f2; };
	u16 f1f2;
};

struct hfc_fifo_zone_config
{
	u16 z_min;
	u16 z_max;
};

struct hfc_fifo_config
{
	char v_ram_sz;
	char v_fifo_md;
	char v_fifo_sz;

	u8 f_min;
	u8 f_max;

	int zone2_start_id;
	int num_fifos;

	struct hfc_fifo_zone_config zone1;
	struct hfc_fifo_zone_config zone2;
};

struct hfc_fifo
{
	struct hfc_card *card;
	struct hfc_chan_simplex *connected_chan;

	int hw_index;
	enum hfc_direction direction;

	int bit_reversed;

	// Counters cache
	union {
		struct { u16 z1,z2; };
		u32 z1z2;
	};

	union {
		struct { u8 f1,f2; };
		u16 f1f2;
	};

	u16 z_min;
	u16 z_max;
	u16 fifo_size;

	u8 f_min;
	u8 f_max;
	u8 f_num;
};

int hfc_fifo_mem_read(struct hfc_fifo *fifo,
	void *data, int size);
int hfc_fifo_mem_read_to_user(struct hfc_fifo *fifo,
	void __user *data, int size);

#include "card.h"
#include "port.h"
#include "chan.h"

static inline u16 Z_inc(struct hfc_fifo *fifo, u16 z, u16 inc)
{
	// declared as u32 in order to manage overflows
	u32 newz = z + inc;
	if (newz > fifo->z_max)
		newz -= fifo->fifo_size;

	return newz;
}

static inline u8 F_inc(struct hfc_fifo *fifo, u8 f, u8 inc)
{
	// declared as u16 in order to manage overflows
	u16 newf = f + inc;
	if (newf > fifo->f_max)
		newf -= fifo->f_num;

	return newf;
}

static inline u16 hfc_fifo_used_rx(struct hfc_fifo *fifo)
{
	return (fifo->z1 - fifo->z2 + fifo->fifo_size) % fifo->fifo_size;
}

static inline u16 hfc_fifo_get_frame_size(struct hfc_fifo *fifo)
{
 // This +1 is needed because in frame mode the available bytes are Z2-Z1+1
 // while in transparent mode I wouldn't consider the byte pointed by Z2 to
 // be available, otherwise, the FIFO would always contain one byte, even
 // when Z1==Z2

	return hfc_fifo_used_rx(fifo) + 1;
}

/*
static inline u8 hfc_fifo_u8(struct hfc_fifo *fifo, u16 z)
{
	return *((u8 *)(fifo->z_base + z));
}
*/
static inline u16 hfc_fifo_used_tx(struct hfc_fifo *fifo)
{
	return (fifo->z1 - fifo->z2 + fifo->fifo_size) % fifo->fifo_size;
}

static inline u16 hfc_fifo_free_rx(struct hfc_fifo *fifo)
{
	u16 free_bytes=fifo->z2 - fifo->z1;

	if (free_bytes > 0)
		return free_bytes;
	else
		return free_bytes + fifo->fifo_size;
}

static inline u16 hfc_fifo_free_tx(struct hfc_fifo *fifo)
{
	u16 free_bytes=fifo->z2 - fifo->z1;

	if (free_bytes > 0)
		return free_bytes;
	else
		return free_bytes + fifo->fifo_size;
}

static inline int hfc_fifo_has_frames(struct hfc_fifo *fifo)
{
	return fifo->f1 != fifo->f2;
}

static inline u8 hfc_fifo_used_frames(struct hfc_fifo *fifo)
{
	return (fifo->f1 - fifo->f2 + fifo->f_num) % fifo->f_num;
}

static inline u8 hfc_fifo_free_frames(struct hfc_fifo *fifo)
{
	u8 free_frames = fifo->f2 - fifo->f1;
	if (free_frames > 0)
		return free_frames;
	else
		return free_frames + fifo->f_num;
}

static inline void hfc_fifo_refresh_fz_cache(struct hfc_fifo *fifo)
{
	struct hfc_card *card = card = fifo->card;

	// Se hfc-8s-4s.pdf par 4.4.7 for an explanation of this:
	u16 prev_f1f2 = hfc_inw(card, hfc_A_F12);;
	do {
		fifo->f1f2 = hfc_inw(card, hfc_A_F12);
	} while(fifo->f1f2 != prev_f1f2);

	fifo->z1z2 = hfc_inl(card, hfc_A_Z12);

	rmb(); // Is this barrier really needed?
}

// This function and all subsequent accesses to the selected FIFO must be done
// in interrupt handler or inside a spin_lock_irq* protected section
static inline void hfc_fifo_select(struct hfc_fifo *fifo)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	struct hfc_card *card = fifo->card;

	hfc_outb(card, hfc_R_FIFO,
		hfc_R_FIFO_V_FIFO_NUM(fifo->hw_index) |
		(fifo->direction == TX ?
		  hfc_R_FIFO_V_FIFO_DIR_TX :
		  hfc_R_FIFO_V_FIFO_DIR_RX) |
		(fifo->bit_reversed ? hfc_R_FIFO_V_REV : 0));

	mb(); // Be sure actually select the FIFO before waiting

	hfc_wait_busy(card);
	hfc_fifo_refresh_fz_cache(fifo);
}

static inline void hfc_fifo_reset(struct hfc_fifo *fifo)
{
	WARN_ON(!irqs_disabled() && !in_irq());

	struct hfc_card *card = fifo->card;

	hfc_outb(card, hfc_A_INC_RES_FIFO,
		hfc_A_INC_RES_FIFO_V_RES_F);

        hfc_wait_busy(card);
}

void hfc_fifo_clear_rx(struct hfc_fifo *fifo);
void hfc_fifo_clear_tx(struct hfc_fifo *fifo);
int hfc_fifo_get(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_put(struct hfc_fifo *fifo, void *data, int size);
void hfc_fifo_drop(struct hfc_fifo *fifo, int size);
int hfc_fifo_get_frame(struct hfc_fifo *fifo, void *data, int max_size);
void hfc_fifo_drop_frame(struct hfc_fifo *fifo);
void hfc_fifo_put_frame(struct hfc_fifo *fifo, void *data, int size);

#endif
