#ifndef _VISDN_TIMER_H
#define _VISDN_TIMER_H

#ifdef __KERNEL__

struct visdn_timer;
struct visdn_timer_ops
{
	unsigned int (*poll)(struct visdn_timer *timer, poll_table *wait);
};

struct visdn_timer
{
	char name[64]; // FIXME

	void *priv;

	struct class_device class_dev;

	struct visdn_timer_ops *ops;

	struct file *file;
};
int visdn_timer_modinit(void);
void visdn_timer_modexit(void);

#define to_visdn_timer(class) container_of(class, struct visdn_timer, class_dev)

void visdn_timer_init(
	struct visdn_timer *visdn_timer,
	struct visdn_timer_ops *ops);

struct visdn_timer *visdn_timer_alloc(void);

int visdn_timer_register(
	struct visdn_timer *visdn_timer,
	const char *name);

void visdn_timer_unregister(
	struct visdn_timer *visdn_timer);

#endif

#endif
