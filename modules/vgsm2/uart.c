/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/version.h>
#include <linux/ctype.h>

#include <asm/io.h>

#include "uart.h"

#define VGSM_UART_CLOCK 33330000 /* 33.33 MHz */

/*
 * Debugging.
 */

#if 0
#define DEBUG_INTR(fmt...)	printk(fmt)
#else
#define DEBUG_INTR(fmt...)	do { } while (0)
#endif

static u32 uart_in(struct vgsm_uart *up, int offset)
{
	u32 r = readl(up->port.membase + (offset << 2));
//printk(KERN_DEBUG "IN 0x%08x = %08x\n", up->port.membase + (offset << 2), r);

	return r;
}

static void
uart_out(struct vgsm_uart *up, int offset, u32 value)
{
	writel(value, up->port.membase + (offset << 2));

//printk(KERN_DEBUG "OUT 0x%08x = %08x\n", up->port.membase + (offset << 2), value);
}

/* Uart divisor latch write */
static inline void vgsm_uart_dl_write(struct vgsm_uart *up, u16 value)
{
	uart_out(up, UART_DLM, value >> 8 & 0xff);
	uart_out(up, UART_DLL, value & 0xff);
}

/*
 * FIFO support.
 */
static inline void vgsm_uart_clear_fifos(struct vgsm_uart *up)
{
	uart_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	uart_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
		       UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
}

static void vgsm_uart_stop_tx(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		uart_out(up, UART_IER, up->ier);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
static void vgsm_uart_stop_tx_compat(
	struct uart_port *port, unsigned int tty_stop)
{
	vgsm_uart_stop_tx(port);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
static void vgsm_uart_start_tx(struct uart_port *port, unsigned int tty_start)
#else
static void vgsm_uart_start_tx(struct uart_port *port)
#endif
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		uart_out(up, UART_IER, up->ier);
	}
}

static void vgsm_uart_stop_rx(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	uart_out(up, UART_IER, up->ier);
}

static void vgsm_uart_enable_ms(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	up->ier |= UART_IER_MSI;
	uart_out(up, UART_IER, up->ier);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
static inline void
uart_insert_char(struct uart_port *port, unsigned int status,
                 unsigned int overrun, unsigned int ch, unsigned int flag)
{
	struct tty_struct *tty = port->info->tty;
	
	if ((status & port->ignore_status_mask & ~overrun) == 0)
		tty_insert_flip_char(tty, ch, flag);
	
	/*
	 * Overrun is special.  Since it's reported immediately,
	 * it doesn't affect the current character.
	 */
	if (status & ~port->ignore_status_mask & overrun)
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);
}
#endif

static void vgsm_uart_receive_chars(struct vgsm_uart *up, u8 *ext_lsr)
{
	struct tty_struct *tty = up->port.info->tty;
	u8 ch, lsr = *ext_lsr;
	int max_count = 256;
	char flag;

	do {
		ch = uart_in(up, UART_RX);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

#if 0
		{
		u32 spec = uart_in(up, 8);
		struct tty_buffer *tb = tty->buf.tail;
		if (isprint(ch))
			printk(KERN_DEBUG "A %d %02x %04x '%c'\n",
				max_count, lsr, spec, ch);
		else
			printk(KERN_DEBUG "A %d %02x %04x '<%02x>'\n",
				max_count, lsr, spec, ch);
		}
#endif

		if (unlikely(lsr & (UART_LSR_BI | UART_LSR_PE |
				    UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */
			if (lsr & UART_LSR_BI) {
				lsr &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
			} else if (lsr & UART_LSR_PE)
				up->port.icount.parity++;
			else if (lsr & UART_LSR_FE)
				up->port.icount.frame++;

			if (lsr & UART_LSR_OE)
				up->port.icount.overrun++;

			/*
			 * Mask off conditions which should be ignored.
			 */
			lsr &= up->port.read_status_mask;

			if (lsr & UART_LSR_BI) {
				DEBUG_INTR("handling break....");
				flag = TTY_BREAK;
			} else if (lsr & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (lsr & UART_LSR_FE)
				flag = TTY_FRAME;
		}

		uart_insert_char(&up->port, lsr, UART_LSR_OE, ch, flag);

		lsr = uart_in(up, UART_LSR);

	} while ((lsr & UART_LSR_DR) && (max_count-- > 0));

	tty_flip_buffer_push(tty);

	*ext_lsr = lsr;
}

static void transmit_chars(struct vgsm_uart *up)
{
	struct circ_buf *xmit = &up->port.info->xmit;
	int count;

	if (up->port.x_char) {
		uart_out(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (uart_tx_stopped(&up->port)) {
		vgsm_uart_stop_tx(&up->port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		vgsm_uart_stop_tx(&up->port);
		return;
	}

	count = 16;
	do {
		uart_out(up, UART_TX, xmit->buf[xmit->tail]);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);

		up->port.icount.tx++;

		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	DEBUG_INTR("THRE...");

// DELAY HELPS
	if (uart_circ_empty(xmit))
		vgsm_uart_stop_tx(&up->port);
}

static u8 check_modem_status(struct vgsm_uart *up)
{
	u8 msr = uart_in(up, UART_MSR);

	if (msr & UART_MSR_ANY_DELTA && up->ier & UART_IER_MSI) {
		if (msr & UART_MSR_TERI)
			up->port.icount.rng++;
		if (msr & UART_MSR_DDSR)
			up->port.icount.dsr++;

		if (msr & UART_MSR_DDCD)
			uart_handle_dcd_change(&up->port,
					msr & UART_MSR_DCD);
		if (msr & UART_MSR_DCTS)
			uart_handle_cts_change(&up->port,
					msr & UART_MSR_CTS);

		wake_up_interruptible(&up->port.info->delta_msr_wait);
	}

	return msr;
}

void vgsm_uart_interrupt(struct vgsm_uart *up)
{
	u8 iir, lsr;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);

	iir = uart_in(up, UART_IIR);
	if (iir & UART_IIR_NO_INT) {
		spin_unlock(&up->port.lock);
		return;
	}

	lsr = uart_in(up, UART_LSR);

	if (lsr & UART_LSR_DR)
		vgsm_uart_receive_chars(up, &lsr);

	check_modem_status(up);

	if (lsr & UART_LSR_THRE)
		transmit_chars(up);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static unsigned int vgsm_uart_tx_empty(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = uart_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int vgsm_uart_get_mctrl(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);
	unsigned int status;
	unsigned int ret;

	status = check_modem_status(up);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;

	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;

	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;

	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;

	return ret;
}

static void vgsm_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);
	u8 mcr = 0;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	uart_out(up, UART_MCR, mcr);
}

static void vgsm_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	uart_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int vgsm_uart_startup(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	up->mcr = 0;

	uart_out(up, UART_LCR, up->lcr | UART_LCR_DLAB);
	if (up->sim_mode)
		vgsm_uart_dl_write(up, VGSM_UART_CLOCK / (8636 * 16));
	else
		vgsm_uart_dl_write(up, VGSM_UART_CLOCK / (9600 * 16));
	uart_out(up, UART_LCR, up->lcr);

	/*
	 * Now, initialize the UART
	 */
	uart_out(up, UART_LCR, UART_LCR_WLEN8);

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	vgsm_uart_clear_fifos(up);

	/*
	 * Clear the interrupt registers.
	 */
	uart_in(up, UART_LSR);
	uart_in(up, UART_RX);
	uart_in(up, UART_IIR);
	uart_in(up, UART_MSR);

	/*
	 * At this point, there's no way the LSR could still be 0xff;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (uart_in(up, UART_LSR) == 0xff) {
		printk(KERN_ERR
			"ttyS%d: LSR safety check engaged!\n",
			up->port.line);
		return -ENODEV;
	}

	/*
	 * Most PC uarts need OUT2 raised to enable interrupts.
	 */
	up->port.mctrl |= TIOCM_OUT2;
	vgsm_uart_set_mctrl(&up->port, up->port.mctrl);

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI | UART_IER_RDI;
	uart_out(up, UART_IER, up->ier);

	return 0;
}

static void vgsm_uart_shutdown(struct uart_port *port)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);
	unsigned long flags;

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	uart_out(up, UART_IER, 0);

	spin_lock_irqsave(&up->port.lock, flags);
	up->port.mctrl &= ~TIOCM_OUT2;
	vgsm_uart_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Disable break condition and FIFOs
	 */
	uart_out(up, UART_LCR, uart_in(up, UART_LCR) & ~UART_LCR_SBC);
	vgsm_uart_clear_fifos(up);

	/*
	 * Read data port to reset things, and then unlink from
	 * the IRQ chain.
	 */
	uart_in(up, UART_RX);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vgsm_uart_set_termios(
	struct uart_port *port,
	struct termios *termios,
	struct termios *old)
#else
static void vgsm_uart_set_termios(
	struct uart_port *port,
	struct ktermios *termios,
	struct ktermios *old)
#endif
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);
	u8 cval, fcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	switch (termios->c_cflag & CSIZE) {
	case CS5: cval = UART_LCR_WLEN5; break;
	case CS6: cval = UART_LCR_WLEN6; break;
	case CS7: cval = UART_LCR_WLEN7; break;
	default:
	case CS8: cval = UART_LCR_WLEN8; break;
	}

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;

	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;

	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (termios->c_cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old,
			(VGSM_UART_CLOCK / 0xffff) / 16, VGSM_UART_CLOCK/16); 
	quot = uart_get_divisor(port, baud);

	fcr = UART_FCR_ENABLE_FIFO | 0x80;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;

	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;

	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;

	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;

	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;

		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */

		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */

	up->ier &= ~UART_IER_MSI;

	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;

	uart_out(up, UART_IER, up->ier);

	if (!up->sim_mode) {
		uart_out(up, UART_LCR, cval | UART_LCR_DLAB);
		vgsm_uart_dl_write(up, quot);
		uart_out(up, UART_LCR, cval);
	}

	/* Save LCR */
	up->lcr = cval;

	if (fcr & UART_FCR_ENABLE_FIFO) {
		/* emulated UARTs (Lucent Venus 167x) need two steps */
		uart_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	}

	/* set fcr */
	uart_out(up, UART_FCR, fcr);

	vgsm_uart_set_mctrl(&up->port, up->port.mctrl);

	spin_unlock_irqrestore(&up->port.lock, flags);
}


static void vgsm_uart_release_port(struct uart_port *port)
{
}

static int vgsm_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void vgsm_uart_config_port(struct uart_port *port, int flags)
{
}

static int vgsm_uart_verify_port(
	struct uart_port *port,
	struct serial_struct *ser)
{
	return 0;
}

static const char *vgsm_uart_type(struct uart_port *port)
{
	return "VGSM16550";
}

static int vgsm_uart_ioctl(
	struct uart_port *port,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_uart *up =
		container_of(port, struct vgsm_uart, port);

	if (up->ioctl)
		return up->ioctl(up, cmd, arg);
	else
		return -ENOIOCTLCMD;
}

static struct uart_ops vgsm_uart_ops = {
	.tx_empty	= vgsm_uart_tx_empty,
	.set_mctrl	= vgsm_uart_set_mctrl,
	.get_mctrl	= vgsm_uart_get_mctrl,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14)
	.stop_tx	= vgsm_uart_stop_tx_compat,
#else
	.stop_tx	= vgsm_uart_stop_tx,
#endif
	.start_tx	= vgsm_uart_start_tx,
	.stop_rx	= vgsm_uart_stop_rx,
	.enable_ms	= vgsm_uart_enable_ms,
	.break_ctl	= vgsm_uart_break_ctl,
	.startup	= vgsm_uart_startup,
	.shutdown	= vgsm_uart_shutdown,
	.set_termios	= vgsm_uart_set_termios,
	.type		= vgsm_uart_type,
	.release_port	= vgsm_uart_release_port,
	.request_port	= vgsm_uart_request_port,
	.config_port	= vgsm_uart_config_port,
	.verify_port	= vgsm_uart_verify_port,
	.ioctl		= vgsm_uart_ioctl,
};

struct vgsm_uart *vgsm_uart_create(
	struct vgsm_uart *uart,
	struct uart_driver *uart_driver,
	unsigned long mapbase,
	void *membase,
	int irq,
	struct device *dev,
	int line,
	int sim_mode,
	int (*ioctl_f)(struct vgsm_uart *uart,
		unsigned int cmd, unsigned long arg))
	
{
	BUG_ON(!uart); /* Only static creation supported */

	uart->driver = uart_driver;

	uart->port.iobase = 0;
	uart->port.mapbase = mapbase;
	uart->port.membase = membase;
	uart->port.irq = irq;
	uart->port.fifosize = 16;
	uart->port.iotype = UPIO_MEM;
	uart->port.flags = UPF_SHARE_IRQ;
	uart->port.dev = dev;
	uart->port.ops = &vgsm_uart_ops;
	uart->port.line = line;
	uart->port.type = PORT_16550A;
	uart->port.uartclk = VGSM_UART_CLOCK;

	uart->sim_mode = sim_mode;

	uart->ioctl = ioctl_f;

	return uart;
}

void vgsm_uart_destroy(struct vgsm_uart *uart)
{
}

int vgsm_uart_register(struct vgsm_uart *uart)
{
	int err;

	err = uart_add_one_port(uart->driver, &uart->port);

	return err;
}

void vgsm_uart_unregister(struct vgsm_uart *uart)
{
	uart_remove_one_port(uart->driver, &uart->port);
}
