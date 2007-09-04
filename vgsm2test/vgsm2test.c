/*
 * vGSM-II tester
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/types.h>

#include <curses.h>
#include <form.h>
#include <menu.h>

#include <longtime.h>

#include "../modules/vgsm2/regs.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define RECV		(0 << 2)
#define XMIT		(0 << 2)
#define DLAB_LSB	(0 << 2)
#define DLAB_MSB	(1 << 2)
#define IER		(1 << 2)
#define IIR		(2 << 2)
#define FCR		(2 << 2)
#define LCR		(3 << 2)
#define MCR		(4 << 2)
#define LSR		(5 << 2)
#define MSR		(6 << 2)

enum test_errors
{
	ERR_NOERR = 0,
	ERR_INTERRUPTED = -1,
	ERR_FAILED = -2,
	ERR_NOT_DONE = -3,
};

struct resource
{
	__u64 begin;
	__u64 end;
	__u64 flags;
};

struct pci_device {
	char *location;
	__u32 vendor_id;
	__u32 device_id;

	int n_resources;
	struct resource resources[8];
};

enum status
{
	STATUS_NOT_TESTED,
	STATUS_TESTING,
	STATUS_OK,
	STATUS_FAILED,
};

struct vgsm_card
{
	struct pci_device *pci_device;

	int memfd;

	void *regs;
	void *fifo;

	unsigned long regs_base;
	unsigned long fifo_base;

	char uartbuf[256];
	int uartbuf_len;

	enum status status;

	__u32 serial;
};

int timed_out = 0;
int has_to_exit = 0;
FILE *report_f = NULL;

static void sigalarm_handler(int signal)
{
	timed_out = 1;
}

static void sigint_handler(int signal)
{
	has_to_exit = 1;
}


/*static void writeb(void *addr, __u8 val)
{
	*(volatile __u8 *)addr = val;
}*/

/*static void writew(void *addr, __u16 val)
{
	*(volatile __u16 *)addr = val;
}*/

/*static void writel(void *addr, __u32 val)
{
	*(volatile __u32 *)addr = val;
}*/

/*static __u8 readb(void *addr)
{
	return *(volatile __u8 *)addr;
}*/

/*static __u16 readw(void *addr)
{
	return *(volatile __u16 *)addr;
}*/

/*static __u32 readl(void *addr)
{
	return *(volatile __u32 *)addr;
}

static __u8 vgsm_inb(struct vgsm_card *c, __u32 addr)
{
	return *(volatile __u8 *)(c->regs + addr);
}

static __u16 vgsm_inw(struct vgsm_card *c, __u32 addr)
{
	return *(volatile __u16 *)(c->regs + addr);
}*/

static __u32 vgsm_inl(struct vgsm_card *c, __u32 addr)
{
	return *(volatile __u32 *)(c->regs + addr);
}

/*static void vgsm_outb(struct vgsm_card *c, __u32 addr, __u8 val)
{
	*(volatile __u8 *)(c->regs + addr) = val;
}

static void vgsm_outw(struct vgsm_card *c, __u32 addr, __u16 val)
{
	*(volatile __u16 *)(c->regs + addr) = val;
}*/

static void vgsm_outl(struct vgsm_card *c, __u32 addr, __u32 val)
{
	*(volatile __u32 *)(c->regs + addr) = val;
}

static int map_memory(struct vgsm_card *card)
{
	card->memfd = open("/dev/mem", O_RDWR);
	if (card->memfd < 0) {
		fprintf(stderr, "Cannot open /dev/mem: %s\n", strerror(errno));
		return 1;
	}

	card->regs = mmap(0, 0x10000, PROT_READ|PROT_WRITE, MAP_SHARED,
			card->memfd, card->regs_base);
	if (card->regs == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	card->fifo = mmap(0, 0x10000, PROT_READ|PROT_WRITE, MAP_SHARED,
			card->memfd, card->fifo_base);
	if (card->fifo == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

static WINDOW *infowin;
static WINDOW *outwin;
static WINDOW *funcbar;

enum { DARK_BLUE, DARK_GREEN };
enum { INFO_BG=1, OK_DIALOG_BG, REQ_DIALOG_BG, FAIL_DIALOG_BG };

static void log_info(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwprintw(outwin, fmt, ap);

	if (report_f)
		vfprintf(report_f, fmt, ap);

	va_end(ap);

	wrefresh(outwin);
}

static void update_funcbar(const char *text)
{
	werase(funcbar);
	mvwprintw(funcbar, 0, 0, text);
	wrefresh(funcbar);
}

static int requester(int color, char *fmt, ...)
{
	char *text;
	va_list ap;

	beep();

	va_start(ap, fmt);
	int width = vasprintf(&text, fmt, ap) + 4;
	va_end(ap);

	WINDOW *dgwin = newwin(3, width, LINES/2 - 2, COLS/2 - width/2);
	wbkgd(dgwin, COLOR_PAIR(color) | ' ');
	wborder(dgwin, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwaddstr(dgwin, 1, 1, text);
	wrefresh(dgwin);
	free(text);

	keypad(dgwin, TRUE);

	int c =  wgetch(dgwin);

	delwin(dgwin);

	refresh();

	if (c == KEY_F(10))
		has_to_exit = TRUE;

	return c;
}

static WINDOW *popup(int color, char *fmt, ...)
{
	char *text;
	va_list ap;

	beep();

	va_start(ap, fmt);
	int width = vasprintf(&text, fmt, ap) + 4;
	va_end(ap);

	WINDOW *dgwin = newwin(3, width, LINES/2 - 2, COLS/2 - width/2);
	wbkgd(dgwin, COLOR_PAIR(color) | ' ');
	wborder(dgwin, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwaddstr(dgwin, 1, 1, text);
	wrefresh(dgwin);
	free(text);

	return dgwin;
}

static char *requester_str(int color, char *fmt, ...)
{
	char *text;
	va_list ap;

	beep();

	va_start(ap, fmt);
	int width = vasprintf(&text, fmt, ap) + 4;
	va_end(ap);

	if (width < 20)
		width = 20;

	WINDOW *win = newwin(4, width, LINES/2 - 2, COLS/2 - width/2);
	wbkgd(win, COLOR_PAIR(color) | ' ');
	wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwaddstr(win, 1, 1, text);
	free(text);

	keypad(win, TRUE);
//	nodelay(win, FALSE);

	FIELD *str = new_field(1, width - 4, 0, 1, 0, 0);
	set_field_type(str, TYPE_INTEGER, 0, 0, INT_MAX);
	set_field_buffer(str, 0, "");

	set_field_back(str, A_UNDERLINE | COLOR_PAIR(COLOR_BLACK));
	field_opts_off(str, O_AUTOSKIP); 

	FIELD *fields[] = { str, NULL };
	FORM *form = new_form(fields);

	int rows, cols;
	scale_form(form, &rows, &cols);

	set_current_field(form, str);
	set_form_win(form, win);
	set_form_sub(form, derwin(win, rows, cols, 2, 2));

	post_form(form);

	int done = 0;
	int req = 0;
	int c = 0;
	while(!done) {
		wrefresh(win);
		pos_form_cursor(form);

		c = wgetch(win);
		req = c;

		switch (c) {
		case KEY_UP:
			field_info(current_field(form),
				&c, NULL, NULL, NULL, NULL, NULL);

			if (c == 1)
				req = REQ_PREV_FIELD;
			else
				req = REQ_UP_CHAR;
		break;

		case KEY_DOWN:
			field_info(current_field(form),
				&c, NULL, NULL, NULL, NULL, NULL);

			if (c == 1)
				req = REQ_NEXT_FIELD;
			else
				req = REQ_DOWN_CHAR;
		break;

		case KEY_BACKSPACE: req = REQ_DEL_PREV; break;
		case KEY_DC: req = REQ_DEL_CHAR; break;
		case KEY_LEFT: req = REQ_PREV_CHAR; break;
		case KEY_RIGHT: req = REQ_NEXT_CHAR; break;
		case KEY_HOME: req = REQ_BEG_LINE; break;
		case KEY_END: req = REQ_END_LINE; break;
		case KEY_STAB: req = REQ_NEXT_FIELD; break;
		case KEY_PPAGE: req = REQ_PREV_PAGE; break;
		case KEY_NPAGE: req = REQ_NEXT_PAGE; break;

		case KEY_F(10):
			has_to_exit = TRUE;
			done = 1;
		break;

		case '\r':
			done = 1;
			req = REQ_VALIDATION;
		break;
		}

		form_driver(form, req);
	}

	unpost_form(form);

	char *s;
	if (c == '\r')
		s = strdup(field_buffer(str, 0));
	else
		s = NULL;

	free_form(form);
	free_field(str);
	delwin(win);

	refresh();

	return s;
}

static int card_waitbusy(struct vgsm_card *card)
{
	int i;
	for(i=0; i<1000 &&
		(vgsm_inl(card, VGSM_R_STATUS) & VGSM_R_STATUS_V_BUSY);
		i++)
		usleep(10000);

	if (i==1000)
		return -1;
	else
		return 0;
}


static int asmi_waitbusy(struct vgsm_card *card)
{
	int i;
	for(i=0; i<1000 &&
		(vgsm_inl(card, VGSM_R_ASMI_STA) & VGSM_R_ASMI_STA_V_RUNNING);
		i++)
		usleep(10000);

	if (i==1000)
		return -1;
	else
		return 0;
}

static __u32 serial_read(struct vgsm_card *card)
{
	int i;
	union {
		__u32 serial;
		__u8 octets[4];
	} s;

	for(i=0; i<sizeof(s);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		if (asmi_waitbusy(card) < 0)
			return -1;

		s.octets[i] = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));
	}

	return s.serial;
}


static void update_info(struct vgsm_card *card)
{
	werase(infowin);
	wborder(infowin, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwprintw(infowin, 0, 20, "vGSM-II testing tool");

	if (card) {
		__u32 info = vgsm_inl(card, VGSM_R_INFO);

		mvwprintw(infowin, 1, 2, "MEs: %d - SIMs: %d",
			VGSM_R_INFO_V_ME_CNT(info),
			VGSM_R_INFO_V_SIM_CNT(info));

		__u32 version = vgsm_inl(card, VGSM_R_VERSION);

		mvwprintw(infowin, 2, 2,
			"HW ver: %d.%d.%d",
			(version & 0x00ff0000) >> 16,
			(version & 0x0000ff00) >>  8,
			(version & 0x000000ff) >>  0);

		if (card->serial != 0xffffffff)
			mvwprintw(infowin, 3, 2, "Serial#: %012u",
							card->serial);
		else
			mvwprintw(infowin, 3, 2, "Serial#: NOT SET");

		mvwprintw(infowin, 1, 40, "PCI Location: %s",
			card->pci_device->location);

		mvwprintw(infowin, 2, 40, "Registers: 0x%08x",
			card->regs_base);
		mvwprintw(infowin, 3, 40, "FIFOs: 0x%08x",
			card->fifo_base);
	}

	wrefresh(infowin);
}

#define TEST_FAILED(fmt, arg...) \
	do { \
		log_info("ERROR: " fmt "\n", \
			## arg);\
		return ERR_FAILED; \
	} while(0)


static int t_dai(struct vgsm_card *card, int par)
{
	int i;

	vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(0), 
		VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR |
		VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR);

	__u16 khz_s[] = { 0x0000, 0x5a82, 0x7fff, 0x5a82, 0x0000, 0xa57e, 0xffff, 0xa57e };
//	__u8 khz[] = { 0x34, 0x21, 0x21, 0x34, 0xb4, 0xa1, 0xa1, 0xb4 };


	vgsm_outl(card, VGSM_R_ME_FIFO_TX_IN(0), 0x3);

	for(i=0; i<0x800; i+=2) {
		*(volatile __u16 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i)
			= khz_s[(i/2) % 8];
	}

#if 1

/*	log_info("RX_FIFO: %08x\n", *(volatile __u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0)));
	log_info("RX_FIFO: %04x %04x\n",
		*(volatile __u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0)),
		*(volatile __u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 2));
	log_info("RX_FIFO: %02x %02x %02x %02x\n",
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0)),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 1),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 2),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 3));
	log_info("RX_FIFO2: %08x\n", *(volatile __u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 4));
exit(0);*/



	while(1) {
//		log_info("VGSM_R_ME_FIFO_RX_IN: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(0)));
//		log_info("VGSM_R_ME_FIFO_TX_IN: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_TX_IN(0)));
//		log_info("VGSM_R_ME_FIFO_TX_OUT: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(0)));

		
		struct timeval tv;
		gettimeofday(&tv, NULL);

		log_info("%lu RX_FIFO(%04x) RXIN=%04x TXOUT=%04x:"
			" %04x %04x %04x %04x %04x %04x %04x %04x\n",
			tv.tv_usec, i,
			vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(0)),
			vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(0)),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 2),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 4),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 6),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 8),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 10),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 12),
			*(__u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 14));
//		usleep(1000);

		i+=16;
		if(i>0x800) i=0;
	}
#endif

	return 0;
}

static int t_dai_speed(struct vgsm_card *card, int par)
{
	__s64 start, stop;
	struct timeval tv;
	float speed;
	int k;

#define ITER 10000
	__u32 fifo_size = vgsm_inl(card, VGSM_R_ME_FIFO_SIZE(par));
	__u32 rx_fifo_size = VGSM_R_ME_FIFO_SIZE_V_RX_SIZE(fifo_size);

	log_info("RX fifo size: %d\n", rx_fifo_size);

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;

	log_info("Reading FIFO...");
	for(k=0;k<ITER;k++) {
		int i;
		for(i=0; i<rx_fifo_size; i+=4) {
			*(volatile __u32 *)(card->fifo +
				VGSM_FIFO_RX_BASE(par) + i);
		}
	}

	gettimeofday(&tv, NULL);
	stop = tv.tv_sec * 1000000LL + tv.tv_usec;
	speed = ((float)ITER * sizeof(__u32) * rx_fifo_size) / (stop-start);

	log_info(" %.3f MB/s\n", speed);

	if (speed < 15)
		TEST_FAILED("Read speed not conformant (%.3f < 15 MB/s)\n",
			speed);

	if (speed > 35)
		TEST_FAILED("Read speed not conformant (%.3f > 35 MB/s)\n",
			speed);

	/*-----------------------------------------------*/
	__u32 tx_fifo_size = VGSM_R_ME_FIFO_SIZE_V_TX_SIZE(fifo_size);
	log_info("TX fifo size: %d\n", tx_fifo_size);

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;

	log_info("Writing FIFO...");

	for(k=0;k<ITER;k++) {
		int i;
		for(i=0; i<tx_fifo_size; i+=4)
			*(volatile __u32 *)(card->fifo +
				VGSM_FIFO_TX_BASE(par) + i) = 0x00000000;
	}

	gettimeofday(&tv, NULL);
	stop = tv.tv_sec * 1000000LL + tv.tv_usec;
	speed = ((float)ITER * sizeof(__u32) * tx_fifo_size) / (stop-start);

	log_info(" %.3f MB/s\n", speed);

	if (speed < 25)
		TEST_FAILED("Write speed not conformant (%.3f < 25 MB/s)\n",
			speed);

	if (speed > 70)
		TEST_FAILED("Write speed not conformant (%.3f > 70 MB/s)\n",
			speed);

	return 0;
}

int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}


char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;

	buf[0] = '\0';

	while(*c) {

		switch(*c) {
		case '\r':
			sanprintf(buf, bufsize, "<cr>");
		break;
		case '\n':
			sanprintf(buf, bufsize, "<lf>");
		break;

		default:
			if (isprint(*c))
				sanprintf(buf, bufsize, "%c", *c);
			else
				sanprintf(buf, bufsize, "<%02x>",
					*(unsigned char *)c);
		}

		c++;
	}

	return buf;
}

static int uart_readbuf(
	struct vgsm_card *card, unsigned long base)
{
	if (vgsm_inl(card, base + 20) & 0x01) {
		unsigned char c = vgsm_inl(card, base + 0);

		card->uartbuf[card->uartbuf_len] = c;
		card->uartbuf[card->uartbuf_len + 1] = '\0';
		card->uartbuf_len++;
	}

	return 0;
}

static int uart_write(
	struct vgsm_card *card, unsigned long base,
	char *buf)
{
	int pos = 0;
	int len = strlen(buf);

	char tmpstr[200];
	log_info("  TX: '%s'", unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	while(pos < len) {
		int i = 0;

		while(!(vgsm_inl(card, base + 20) & 0x20)) {
			uart_readbuf(card, base);

			i++;
			if (i > 1000)
				TEST_FAILED("Timeout waiting for"
					" FIFO space in UART");
		}

		vgsm_outl(card, base + 0, buf[pos]);
		pos++;

		uart_readbuf(card, base);
	}

	log_info("\n");

	return 0;
}

static char uart_pop(struct vgsm_card *card)
{
	char c = card->uartbuf[card->uartbuf_len-1];

	if (card->uartbuf_len > 1)
		memmove(card->uartbuf + 1, card->uartbuf,
			card->uartbuf_len);

	card->uartbuf_len--;

	return c;
}

static int uart_read(
	struct vgsm_card *card, unsigned long base,
	char *echo,
	char *buf)
{
	int pos = 0;
	int i = 0;

	__s64 start, now;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;

	while(pos < strlen(echo)) {
		uart_readbuf(card, base);

		if (card->uartbuf_len) {

			char c = uart_pop(card);

			if (c != echo[pos]) {
				gettimeofday(&tv, NULL);
				start = tv.tv_sec * 1000000LL + tv.tv_usec;

				do {
					uart_readbuf(card, base);
					gettimeofday(&tv, NULL);

					now = tv.tv_sec * 1000000LL + tv.tv_usec;
				} while (now - start < 400000LL);

				TEST_FAILED(
					"UART Echo corrupted, expected '%s',"
					" received '%s' at char %d",
					echo, card->uartbuf, pos);
			}
			pos++;
		}

		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000000LL + tv.tv_usec;

		if (now - start > 4000000LL)
			TEST_FAILED("UART Timeout waiting for echo");
	}

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;

	pos = 0;
	while(strcmp(buf + pos - 6, "\r\nOK\r\n")) {

		uart_readbuf(card, base);

		if (card->uartbuf_len) {
			buf[pos++] = uart_pop(card);
			buf[pos] = '\0';
		}

		i++;

		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000000LL + tv.tv_usec;

		if (now - start > 4000000LL)
			TEST_FAILED("Timeout waiting for UART response");
	}

	char tmpstr[200];
	log_info("  RX: '%s'\n",
		unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	buf[pos - 6] = '\0';

	return pos - 6;
}

static void uart_flush(struct vgsm_card *card, unsigned long base)
{
	vgsm_outl(card, base + FCR, 0x87); // Flush FIFOs
	card->uartbuf_len = 0;
}


static int program_serial(struct vgsm_card *card, __u32 serial)
{
	int i;

	log_info("Erasing sector 0x700000...");
	vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000);
	vgsm_outl(card, VGSM_R_ASMI_CTL,
		VGSM_R_ASMI_CTL_V_WREN |
		VGSM_R_ASMI_CTL_V_SECTOR_ERASE |
		VGSM_R_ASMI_CTL_V_START);
	if (asmi_waitbusy(card) < 0)
		TEST_FAILED("Timeout waiting ASMI during read");
	log_info("OK\n");

	union {
		__u32 serial;
		__u8 octets[4];
	} s;

#if 0
	for(i=0; i<sizeof(serial);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		if (asmi_waitbusy(card) < 0)
			TEST_FAILED("Timeout waiting ASMI during read (%d)", i);

		s.octets[i] = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));

	}

	if (s.serial != 0xffffffff)
		TEST_FAILED("Serial number is already programmed as 0x%08x",
			s.serial);
#endif

	s.serial = serial;

	log_info("Writing serial number...");
	for(i=0; i<sizeof(serial); i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_WRITE |
				VGSM_R_ASMI_CTL_V_START |
				VGSM_R_ASMI_CTL_V_DATAIN(s.octets[i]));

		if (asmi_waitbusy(card) < 0)
			TEST_FAILED("Timeout waiting for ASMI (%d)", i);
	}
	log_info("OK\n");

	log_info("Comparing serial number...");
	for(i=0; i<sizeof(serial);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		if (asmi_waitbusy(card) < 0)
			TEST_FAILED("Timeout waiting ASMI during read (%d)", i);

		__u8 c = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));

		if (c != s.octets[i])
			TEST_FAILED("Octet %d: Wrote %02x, read %02x!",
				i, s.octets[i], c);
	}
	log_info("OK\n");

	return 0;
}

static int t_asmi(struct vgsm_card *card, int par)
{

/*	log_info("Resetting card...");
	vgsm_outl(card, VGSM_R_SERVICE, VGSM_R_SERVICE_V_RESET);
	usleep(10000);
	vgsm_outl(card, VGSM_R_SERVICE, 0);
	if (card_waitbusy(card) < 0)
		TEST_FAILED("Timeout waiting card to be initialized");
	log_info("OK\n");*/

	log_info("Waiting for ASMI to be ready...");
	if (asmi_waitbusy(card) < 0)
		TEST_FAILED("Timeout waiting ASMI pre-write");
	log_info("OK\n");

	__u8 buf[] = "Copyright (C) 2007 Daniele Orlandi, All Rights Reserved";

	log_info("Erasing sector 6...");
	vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x60000);
	vgsm_outl(card, VGSM_R_ASMI_CTL,
		VGSM_R_ASMI_CTL_V_WREN |
		VGSM_R_ASMI_CTL_V_SECTOR_ERASE |
		VGSM_R_ASMI_CTL_V_START);
	if (asmi_waitbusy(card) < 0)
		TEST_FAILED("Timeout waiting ASMI during read");
	log_info("OK\n");

	log_info("Writing string at 0x60000...");
	int i;
	for(i=0; i<sizeof(buf);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x60000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_WRITE |
				VGSM_R_ASMI_CTL_V_START |
				VGSM_R_ASMI_CTL_V_DATAIN(buf[i]));
		if (asmi_waitbusy(card) < 0)
			TEST_FAILED("Timeout waiting ASMI during write");
	}
	log_info("OK\n");

	log_info("Comparing string at 0x60000...");
	for(i=0; i<sizeof(buf);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x60000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		if (asmi_waitbusy(card) < 0)
			TEST_FAILED("Timeout waiting ASMI during read");

		__u8 c = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));

		if (c != buf[i])
			TEST_FAILED("Octet %d: Wrote %02x, read %02x!",
				i, buf[i], c);
	}
	log_info("OK\n");

	return 0;
}

static int t_card_valid(struct vgsm_card *card, int par)
{
	if (vgsm_inl(card, VGSM_R_VERSION) == 0xffffffff)
		TEST_FAILED("Got 0xffffffff reading version, card is probably"
			" disabled");

	return 0;
}

static int t_version(struct vgsm_card *card, int par)
{
	__u32 ver = vgsm_inl(card, VGSM_R_VERSION);

	if ((ver & 0x00ffff00) != 0x00020400 &&
	    (ver & 0x00ffff00) != 0x00020500 &&
	    (ver & 0x00ffff00) != 0x00020600)
		TEST_FAILED("This testing program does"
			" support only 2.4.x to 2.6.x trains");

	return 0;
}

static int t_reg_values(struct vgsm_card *card, int par)
{
	log_info("Testing R_VERSION...");
	__u32 version = vgsm_inl(card, VGSM_R_VERSION);
	if (version & 0xff000000)
		TEST_FAILED("R_VERSION 0x%08x & 0xff000000 != 0", version);
	log_info("OK\n");

	log_info("Testing R_INFO...");
	__u32 info = vgsm_inl(card, VGSM_R_INFO);
	if (info & 0xffffff00)
		TEST_FAILED("R_INFO 0x%08x & 0xffffff00 != 0", info);
	log_info("OK\n");

	log_info("Testing R_SERVICE...");
	__u32 service = vgsm_inl(card, VGSM_R_SERVICE);
	if (service & 0xffffff00)
		TEST_FAILED("R_SERVICE 0x%08x & 0xffffff00 != 0", service);
	log_info("OK\n");

	log_info("Testing R_STATUS...");
	__u32 status = vgsm_inl(card, VGSM_R_STATUS);
	if (status != 0x00000002)
		TEST_FAILED("R_STATUS 0x%08x != 0x00000002", status);
	log_info("OK\n");

	return 0;
}

static int t_serial_number(struct vgsm_card *card, int par)
{
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0xffffffff);

	char *str = NULL;

	log_info("Reading serial number...");
	card->serial = serial_read(card);
	log_info("%012u\n", card->serial);

	update_info(card);

	if (card->serial == 0xffffffff) {
		str = requester_str(REQ_DIALOG_BG,
				"Please enter serial #:");
	} else {
		int c = requester(REQ_DIALOG_BG,
			"Serial number already set to %012u."
			" SKIP programming? (y/n)", card->serial);

		if (has_to_exit)
			return ERR_INTERRUPTED;
		else if (c == 'n' || c == 'N')
			str = requester_str(REQ_DIALOG_BG,
				"Please enter serial #:");
	}

	if (has_to_exit)
		return ERR_INTERRUPTED;

	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0x00000000);

	if (str) {
		if (strlen(str)) {

			program_serial(card, atoi(str));

			log_info("Reading again serial number...");
			card->serial = serial_read(card);
			log_info("%012u\n", card->serial);

			update_info(card);
		}

		free(str);
	}

	return 0;
}

static int t_reg_access_1(struct vgsm_card *card, int par)
{
	int cnt;

	log_info("Reading/writing patterns to R_TEST...");

	for(cnt=0; cnt<10000; cnt++) {

		int i;

		for(i=0; i<0xff; i++) {
			__u32 val = i << 24 | i << 16 | i << 8 | i;
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val)
				TEST_FAILED(
					" iter=%d, wrote 0x%08x, read 0x%08x\n",
					cnt, val, val2);
		}
	}

	log_info("OK\n");

	return 0;
}

static int t_reg_access_2(struct vgsm_card *card, int par)
{
	int cnt;

	log_info("Reading/writing patterns to R_TEST...");

	for(cnt=0; cnt<10000; cnt++) {

		int i;

		for(i=0; i<32; i++) {
			__u32 val = 1 << i;
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val)
				TEST_FAILED(
					" iter=%d, wrote 0x%08x, read 0x%08x\n",
					cnt, val, val2);
		}
	}

	log_info("OK\n");

	return 0;
}

static int t_reg_access_3(struct vgsm_card *card, int par)
{
	int cnt;

	log_info("Reading/writing patterns to R_TEST...");

	for(cnt=0; cnt<10000; cnt++) {

		int i;

		for(i=0; i<32; i++) {
			__u32 val = 0xffffffff & ~(1 << i);
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val)
				TEST_FAILED(
					" iter=%d, wrote 0x%08x, read 0x%08x\n",
					cnt, val, val2);
		}
	}

	log_info("OK\n");

	return 0;
}

static void sim_holder_remove(struct vgsm_card *card, int sim)
{
	if ((vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
				VGSM_R_SIM_STATUS_V_CCIN)) {

retry:;
		WINDOW *win = popup(REQ_DIALOG_BG,
				"Please remove SIM holder #%d", sim);
		while((vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
				VGSM_R_SIM_STATUS_V_CCIN)) {
			usleep(10000);

			if (has_to_exit)
				return;
		}

		delwin(win);
		refresh();

		__s64 start, now;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		start = tv.tv_sec * 1000000LL + tv.tv_usec;

		do {
			if (vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
					VGSM_R_SIM_STATUS_V_CCIN)
				goto retry;
			usleep(1000);

			gettimeofday(&tv, NULL);
			now = tv.tv_sec * 1000000LL + tv.tv_usec;
		} while(now - start < 1000000LL);
	}
}

static void sim_holder_insert(struct vgsm_card *card, int sim)
{
	if (!(vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
				VGSM_R_SIM_STATUS_V_CCIN)) {

retry:;
		WINDOW *win = popup(REQ_DIALOG_BG,
				"Please insert SIM holder "
				"with a valid SIM #%d", sim);
		while(!(vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
				VGSM_R_SIM_STATUS_V_CCIN)) {
			usleep(10000);

			if (has_to_exit)
				return;
		}

		delwin(win);
		refresh();

		__s64 start, now;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		start = tv.tv_sec * 1000000LL + tv.tv_usec;

		do {
			if (!vgsm_inl(card, VGSM_R_SIM_STATUS(sim)) &
					VGSM_R_SIM_STATUS_V_CCIN)
				goto retry;
			usleep(1000);

			gettimeofday(&tv, NULL);
			now = tv.tv_sec * 1000000LL + tv.tv_usec;
		} while(now - start < 1000000LL);
	}
}

static int t_sim_holder(struct vgsm_card *card, int par)
{
	log_info("Checking that the SIM holder is removed...");
	sim_holder_remove(card, par);
	log_info("OK\n");

	if (has_to_exit)
		return ERR_INTERRUPTED;

	log_info("Checking that the SIM holder is inserted...");
	sim_holder_insert(card, par);
	log_info("OK\n");

	if (has_to_exit)
		return ERR_INTERRUPTED;

	return 0;
}

static int t_sim_atr(struct vgsm_card *card, int par)
{
	log_info("Checking that the SIM holder is inserted...");
	sim_holder_insert(card, par);
	log_info("OK\n");

	if (has_to_exit)
		return ERR_INTERRUPTED;

	log_info("Setting UART parameters...");
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + FCR, 0x87); // Flush FIFOs
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + IER, 0x0);
	vgsm_inl(card, VGSM_SIM_UART_BASE(par) + IIR);
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + LCR, 0x80); //Enable Latch
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + DLAB_MSB, 0);
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + DLAB_LSB, 217);
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + LCR, 0x03); //Disable Latch
	log_info("DONE\n");

	log_info("Setting SIM routing...");
	vgsm_outl(card, VGSM_R_SIM_ROUTER,
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(0) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(1) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(2) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(3));
	log_info("OK\n");

	log_info("Activating SIM reset...");
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + MCR, 0x01);
	log_info("OK\n");

	log_info("Powering-on SIM...");

	vgsm_outl(card, VGSM_R_SIM_SETUP(par),
			VGSM_R_SIM_SETUP_V_3V |
			VGSM_R_SIM_SETUP_V_CLOCK_OFF);
	usleep(30000);
	vgsm_outl(card, VGSM_R_SIM_SETUP(par),
			VGSM_R_SIM_SETUP_V_VCC |
			VGSM_R_SIM_SETUP_V_3V |
			VGSM_R_SIM_SETUP_V_CLOCK_OFF);
	usleep(30000);
	log_info("OK\n");

	log_info("Activating 3.571 MHz SIM clock...");
	vgsm_outl(card, VGSM_R_SIM_SETUP(par),
			VGSM_R_SIM_SETUP_V_VCC |
			VGSM_R_SIM_SETUP_V_3V |
			VGSM_R_SIM_SETUP_V_CLOCK_3_5);
	usleep(30000);
	log_info("OK\n");

	log_info("Flushing ATR buffer...");
	usleep(300000);
	uart_flush(card, VGSM_SIM_UART_BASE(par));
	log_info("OK\n");

	log_info("Removing SIM reset...");
	vgsm_outl(card, VGSM_SIM_UART_BASE(par) + MCR, 0x03);
	log_info("OK\n");

	log_info("Reading ATR...");

	__u8 atr[128];
	int atr_pos = 0;

	__s64 start, now;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;

	do {
		uart_readbuf(card, VGSM_SIM_UART_BASE(par));

		if (card->uartbuf_len) {
			atr[atr_pos] = uart_pop(card);
			atr_pos++;
			if (atr_pos == sizeof(atr))
				TEST_FAILED("ATR too big (%d > %d)",
					atr_pos+1, sizeof(atr));
		}

		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000000LL + tv.tv_usec;

	} while(now - start < 1000000);

	while(!atr_pos) {
		gettimeofday(&tv, NULL);
		now = tv.tv_sec * 1000000LL + tv.tv_usec;

		if (now - start > 2000000)
			TEST_FAILED("Timed out waiting for ATR from SIM");
	}

	int i;
	for(i=0; i<atr_pos; i++) {
		log_info("%02x ", atr[i]);
	}
	log_info("\n");

	if (atr[0] != 0x3b)
		TEST_FAILED("ATR not starting with 0x3b != 0x%02x", atr[0]);


	return 0;
}

static int t_leds_off(struct vgsm_card *card, int par)
{
	log_info("Turning OFF all LEDs...");
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0x00000000);
	log_info("DONE\n");

	log_info("Visual inspection...");

askagain:;
	int c = requester(REQ_DIALOG_BG, "Are LEDs all off [y/n] ?");
	if (has_to_exit)
		return ERR_INTERRUPTED;
	else if (c == 'y' || c == 'Y')
		;
	else if (c == 'n' || c == 'N')
		TEST_FAILED("One LED is lit while it should be off");
	else
		goto askagain;

	log_info("OK\n");

	vgsm_outl(card, VGSM_R_LED_SRC, 0);

	return 0;
}

static int t_leds_red(struct vgsm_card *card, int par)
{
	log_info("Turning ON all RED LEDs...");
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0x55555555);
	log_info("DONE\n");

	log_info("Visual inspection...");

askagain:;
	int c = requester(REQ_DIALOG_BG,
			"Are LEDs all red [y/n] ? ");

	if (has_to_exit)
		return ERR_INTERRUPTED;
	else if (c == 'y' || c == 'Y')
		;
	else if (c == 'n' || c == 'N')
		TEST_FAILED("One LED is off or not red while it should be red");
	else
		goto askagain;

	log_info("OK\n");

	log_info("Turning OFF all LEDs...");
	vgsm_outl(card, VGSM_R_LED_USER, 0);
	log_info("DONE\n");

	return 0;
}

static int t_leds_green(struct vgsm_card *card, int par)
{
	log_info("Turning ON all GREEN LEDs...");
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0xaaaaaaaa);
	log_info("OK\n");

	log_info("Visual inspection...");

askagain:;
	int c = requester(REQ_DIALOG_BG,
			"Are LEDs all green [y/n] ? ");

	if (has_to_exit)
		return ERR_INTERRUPTED;
	else if (c == 'y' || c == 'Y')
		;
	else if (c == 'n' || c == 'N')
		TEST_FAILED("One LED is off or not green while it should"
				" be green");
	else
		goto askagain;

	log_info("OK\n");

	log_info("Turning OFF all LEDs...");
	vgsm_outl(card, VGSM_R_LED_USER, 0);
	log_info("DONE\n");

	return 0;
}

static int t_leds_orange(struct vgsm_card *card, int par)
{
	log_info("Turning ON all RED+GREEN LEDs...");
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);
	vgsm_outl(card, VGSM_R_LED_USER, 0xffffffff);
	log_info("DONE\n");

	log_info("Visual inspection...");

askagain:;
	int c = requester(REQ_DIALOG_BG,
			"Are LEDs all orange [y/n] ? ");

	if (has_to_exit)
		return ERR_INTERRUPTED;
	else if (c == 'y' || c == 'Y')
		;
	else if (c == 'n' || c == 'N')
		TEST_FAILED("One LED is off or not orange while it should"
				" be orange");
	else
		goto askagain;

	log_info("Turning OFF all LEDs...");
	vgsm_outl(card, VGSM_R_LED_USER, 0);
	log_info("DONE\n");

	return 0;
}

static int t_leds_flashing(struct vgsm_card *card, int par)
{
	log_info("Flashing LEDs...");
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);

retry:;
	int i;
	for(i=0; i<15; i++) {
		vgsm_outl(card, VGSM_R_LED_USER, 0x55555555);
		usleep(100000);
		vgsm_outl(card, VGSM_R_LED_USER, 0xaaaaaaaa);
		usleep(100000);
		vgsm_outl(card, VGSM_R_LED_USER, 0x00000000);
		usleep(100000);
	}
	log_info("DONE\n");

	log_info("Visual inspection...");

askagain:;
	int c = requester(REQ_DIALOG_BG,
			"Did all LEDs flash [y/n/(r)etry] ? ");
	if (has_to_exit)
		return ERR_INTERRUPTED;
	else if (c == 'y' || c == 'Y')
		;
	else if (c == 'r' || c == 'R')
		goto retry;
	else if (c == 'n' || c == 'N')
		TEST_FAILED("One LED is not flashing or not orange while it "
				" should be flashing");
	else
		goto askagain;

	log_info("OK\n");

	log_info("Turning OFF all LEDs...");
	vgsm_outl(card, VGSM_R_LED_USER, 0);
	log_info("DONE\n");

	return 0;
}

static int t_module_vddlp(struct vgsm_card *card, int par)
{
	__u32 status;

	log_info("Checking VddLp...");
	status = vgsm_inl(card, VGSM_R_ME_STATUS(par));
	if ((!status & VGSM_R_ME_STATUS_V_VDDLP))
		TEST_FAILED("VddLp not present\n", par);

	log_info("OK\n");

	return 0;
}

static int t_poweron(struct vgsm_card *card, int par)
{
	__u32 status;

	vgsm_outl(card, VGSM_R_ME_SETUP(par), 0);

	status = vgsm_inl(card, VGSM_R_ME_STATUS(par));
	if (status & VGSM_R_ME_STATUS_V_VDD) {
		log_info("Module %d is powered ON, powering off...", par);
		vgsm_outl(card, VGSM_R_ME_SETUP(par),
			VGSM_R_ME_SETUP_V_EMERG_OFF);

		usleep(3600000);
		vgsm_outl(card, VGSM_R_ME_SETUP(par), 0);
		log_info("OK\n");
	}

	log_info("Checking off signals...");
	if (vgsm_inl(card, VGSM_R_ME_STATUS(par) & VGSM_R_ME_STATUS_V_VDD))
		TEST_FAILED("ME%d poweroff failed, VDD still present", par);

	if ((vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x20))
		TEST_FAILED("ME%d CTS should not be asserted", par);
	log_info("OK\n");

	log_info("Setting SIM routing and power...");
	vgsm_outl(card, VGSM_R_SIM_ROUTER,
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(0) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(1) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(2) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE_UART(3));
	log_info("DONE\n");

	__s64 start, stop;
	__s64 cts_start, cts_stop;
	struct timeval tv;

	log_info("Powering on ME%d...", par);

	vgsm_outl(card, VGSM_R_ME_SETUP(par), VGSM_R_ME_SETUP_V_ON);

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;
	do {
		gettimeofday(&tv, NULL);
		stop = tv.tv_sec * 1000000LL + tv.tv_usec;

		if (stop - start > 120000LL)
			TEST_FAILED("ME%d VDD failed to come"
				" up in 120 milliseconds", par);

	} while(!(vgsm_inl(card, VGSM_R_ME_STATUS(par)) &
				VGSM_R_ME_STATUS_V_VDD));

	usleep(120000 - (stop - start));
	vgsm_outl(card, VGSM_R_ME_SETUP(par), 0);

	gettimeofday(&tv, NULL);
	cts_start = tv.tv_sec * 1000000LL + tv.tv_usec;

	do {
		gettimeofday(&tv, NULL);
		cts_stop = tv.tv_sec * 1000000LL + tv.tv_usec;

		if (cts_stop - cts_start > 5000000LL)
			TEST_FAILED("ME%d CTS failed to come"
				" up in 5 seconds", par);
	} while(!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x10));

	log_info("OK (VDD %d ms, CTS in %d ms)\n",
		(int)((stop - start)/1000),
		(int)((cts_stop - cts_start)/1000));


	sleep(1);

	return 0;
}

static int t_uart_asc0(struct vgsm_card *card, int par)
{
	log_info("Setting UART parameters...");
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + FCR, 0x87); // Flush FIFOs
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + IER, 0x0);
	vgsm_inl(card,  VGSM_ME_ASC0_BASE(par) + IIR);
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + LCR, 0x80); //Enable Latch
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + DLAB_MSB, 0);
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + DLAB_LSB, 0x9);
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + LCR, 0x03); //Disable Latch
	vgsm_outl(card, VGSM_ME_ASC0_BASE(par) + MCR, 0x0b);
	log_info("DONE\n");

	log_info("Flushing UART...");
	uart_flush(card, VGSM_ME_ASC0_BASE(par));
	log_info("DONE\n");

	char buf[128] = "";

	log_info("Setting factory defaults:\n");
	uart_write(card, VGSM_ME_ASC0_BASE(par), "AT&F\r");
	sleep(1);
	uart_flush(card, VGSM_ME_ASC0_BASE(par));
	log_info("Setting factory defaults: OK\n");

	log_info("Pinging:\n");
	int i;
	for(i=0; i<20; i++) {
		uart_write(card, VGSM_ME_ASC0_BASE(par), "AT\r");
		if (uart_read(card, VGSM_ME_ASC0_BASE(par), "AT\r", buf) < 0)
			TEST_FAILED("Error reading from UART");
		if (strlen(buf))
			TEST_FAILED("Unexpected response: '%s'", buf);
	}
	log_info("Pinging OK\n");

	log_info("Clear To Send (CTS)...\n");

	/* CTS (checked off when par off) */
	if (!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x10))
		TEST_FAILED("CTS should be asserted");

	log_info("Clear To Send (CTS): OK\n");

	/* DSR */
	log_info("Data Set Ready (DSR)...\n");

	if (!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x20))
		TEST_FAILED("DSR should be asserted");
	uart_write(card, VGSM_ME_ASC0_BASE(par), "AT&S1\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par), "AT&S1\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	usleep(10000);
	if (vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x20)
		TEST_FAILED("DSR should not be asserted");
	uart_write(card, VGSM_ME_ASC0_BASE(par), "AT&S0\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par), "AT&S0\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	usleep(10000);
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	if (!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x20))
		TEST_FAILED("DSR should be asserted again");

	log_info("Data Set Ready (DSR): OK\n");

	/* DCD */
	log_info("Data Carrier Detect (DCD)...\n");

	if (vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x80)
		TEST_FAILED("DCD should not be asserted");
	uart_write(card, VGSM_ME_ASC0_BASE(par), "AT&C0\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par), "AT&C0\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	usleep(10000);
	if (!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x80))
		TEST_FAILED("DCD should be asserted");
	uart_write(card, VGSM_ME_ASC0_BASE(par), "AT&C1\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par), "AT&C1\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	usleep(10000);
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	if (vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x80)
		TEST_FAILED("DCD should not be asserted here");

	log_info("Data Carrier Detect (DCD): OK\n");

	/* RI */

	log_info("Ring indicator (RI)...\n");

	if (vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x40)
		TEST_FAILED("Ring indicator (RI) is"
			" asserted while it shouldn't");

	uart_write(card, VGSM_ME_ASC0_BASE(par),
		"AT+CCLK=\"02/01/01,00:00:00\"\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par),
			"AT+CCLK=\"02/01/01,00:00:00\"\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	usleep(10000);

	uart_write(card, VGSM_ME_ASC0_BASE(par),
		"AT+CALA=\"02/01/01,00:00:01\"\r");
	if (uart_read(card, VGSM_ME_ASC0_BASE(par),
			"AT+CALA=\"02/01/01,00:00:01\"\r", buf) < 0)
		TEST_FAILED("Error reading from UART");
	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);

	__s64 start, ri_start = 0, ri_stop;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000LL + tv.tv_usec;
	ri_stop = start;

	while(ri_stop - start < 5000000LL) {

		if (ri_start) {
			if (!(vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x40)) {
				gettimeofday(&tv, NULL);
				ri_stop = tv.tv_sec * 1000000LL + tv.tv_usec;
				break;
			}
		} else {
			if (vgsm_inl(card, VGSM_ME_ASC0_BASE(par) + MSR) & 0x40) {
				gettimeofday(&tv, NULL);
				ri_start = tv.tv_sec * 1000000LL + tv.tv_usec;
			}
		}

		gettimeofday(&tv, NULL);
		ri_stop = tv.tv_sec * 1000000LL + tv.tv_usec;
	}

	if (ri_start == 0)
		TEST_FAILED("Ring indicator (RI) signal not detected");
	else if (ri_stop - ri_start > 2400000)
		TEST_FAILED("Ring indicator (RI) signal asserted for too much"
			" (%d ms)", (int)(ri_stop - ri_start)/1000);

	log_info("Ring indicator (RI): OK (dur=%d ms, del=%d ms)\n",
		(int)(ri_stop - ri_start)/1000,
		(int)(ri_start - start)/1000);


	return 0;
}

static int t_uart_asc1(struct vgsm_card *card, int par)
{
	int speed = 9;

retry:
	log_info("Setting UART parameters...");
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + FCR, 0x87); // Flush FIFOs
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + IER, 0x0);
	vgsm_inl(card,  VGSM_ME_ASC1_BASE(par) + IIR);
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + LCR, 0x80); //Enable Latch
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + DLAB_MSB, 0);
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + DLAB_LSB, speed);
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + LCR, 0x03); //Disable Latch
	vgsm_outl(card, VGSM_ME_ASC1_BASE(par) + MCR, 0x0b);
	log_info("DONE\n");

	log_info("Flushing UART...");
	uart_flush(card, VGSM_ME_ASC1_BASE(par));
	log_info("DONE\n");

	char buf[128] = "";

	log_info("Setting factory defaults:");
	uart_write(card, VGSM_ME_ASC1_BASE(par), "AT&F\r");
	sleep(1);
	uart_flush(card, VGSM_ME_ASC1_BASE(par));
	log_info("Setting factory defaults: OK\n");

	log_info("Resetting:");
	uart_write(card, VGSM_ME_ASC1_BASE(par), "ATZ\r");
	if (uart_read(card, VGSM_ME_ASC1_BASE(par), "ATZ\r", buf) < 0) {

		if (speed == 9) {
			speed = 36;
			goto retry;
		}

		TEST_FAILED("Error reading from UART");
	}
	log_info("Resetting: OK\n");

	log_info("Pinging:\n");
	int i;
	for(i=0; i<20; i++) {
		uart_write(card, VGSM_ME_ASC1_BASE(par), "AT\r");
		if (uart_read(card, VGSM_ME_ASC1_BASE(par), "AT\r", buf) < 0)
			TEST_FAILED("Error reading from UART");
		if (strlen(buf))
			TEST_FAILED("Unexpected response: '%s'", buf);
	}
	log_info("Pinging OK\n");

	if (strlen(buf))
		TEST_FAILED("Unexpected response: '%s'", buf);
	log_info("OK\n");

	return 0;
}

struct test
{
	__u32 id;
	char *name;
	int (*doit)(struct vgsm_card *card, int par);
	int par;
};

struct test tests[] =
{
#if 1
	{ 0x00000000, "Card valid", t_card_valid },
	{ 0x00000010, "Card version", t_version },
	{ 0x00000020, "Register values", t_reg_values },
	{ 0x00005000, "ASMI basic", t_asmi },
	{ 0x00000030, "Serial number", t_serial_number },
	{ 0x00000040, "Register access (pattern 1)", t_reg_access_1 },
	{ 0x00000050, "Register access (pattern 2)", t_reg_access_2 },
	{ 0x00000060, "Register access (pattern 3)", t_reg_access_3 },
	{ 0x00001010, "LEDs off", t_leds_off },
	{ 0x00001020, "LEDs red", t_leds_red },
	{ 0x00001020, "LEDs green", t_leds_green },
	{ 0x00001020, "LEDs orange", t_leds_orange },
//	{ 0x00001030, "LEDs flashing", t_leds_flashing },
#endif

#if 1
	{ 0x00003000, "SIM0 holder", t_sim_holder, 0 },
	{ 0x00003010, "SIM0 ATR", t_sim_atr, 0 },
	{ 0x00003010, "ME0 VDDlp", t_module_vddlp, 0 },
	{ 0x00003010, "ME0 poweron", t_poweron, 0 },
	{ 0x00003020, "ME0 ASC0 UART", t_uart_asc0, 0 },
	{ 0x00003020, "ME0 ASC1 UART", t_uart_asc1, 0 },
//	{ 0x00004000, "DAI0 basic", t_dai, 0 },
	{ 0x00004010, "DAI0 access speed", t_dai_speed, 0 },
#endif

#if 1
	{ 0x00003000, "SIM1 holder", t_sim_holder, 1 },
	{ 0x00003010, "SIM1 ATR", t_sim_atr, 1 },
	{ 0x00003010, "ME1 VDDlp", t_module_vddlp, 1 },
	{ 0x00003010, "ME1 poweron", t_poweron, 1 },
	{ 0x00003020, "ME1 ASC0 UART", t_uart_asc0, 1 },
	{ 0x00003020, "ME1 ASC1 UART", t_uart_asc1, 1 },
//	{ 0x00004000, "DAI1 basic", t_dai, 1 },
	{ 0x00004010, "DAI1 access speed", t_dai_speed, 1 },
#endif

#if 1
	{ 0x00003000, "SIM2 holder", t_sim_holder, 2 },
	{ 0x00003010, "SIM2 ATR", t_sim_atr, 2 },
	{ 0x00003010, "ME2 VDDlp", t_module_vddlp, 2 },
	{ 0x00003010, "ME2 poweron", t_poweron, 2 },
	{ 0x00003020, "ME2 ASC0 UART", t_uart_asc0, 2 },
	{ 0x00003020, "ME2 ASC1 UART", t_uart_asc1, 2 },
//	{ 0x00004000, "DAI2 basic", t_dai, 2 },
	{ 0x00004010, "DAI2 access speed", t_dai_speed, 2 },

#endif

#if 1
	{ 0x00003000, "SIM3 holder", t_sim_holder, 3 },
	{ 0x00003010, "SIM3 ATR", t_sim_atr, 3 },
	{ 0x00003010, "ME3 VDDlp", t_module_vddlp, 3 },
	{ 0x00003010, "ME3 poweron", t_poweron, 3 },
	{ 0x00003020, "ME3 ASC0 UART", t_uart_asc0, 3 },
	{ 0x00003020, "ME3 ASC1 UART", t_uart_asc1, 3 },
//	{ 0x00004000, "DAI3 basic", t_dai, 3 },
	{ 0x00004010, "DAI3 access speed", t_dai_speed, 3 },
#endif
};

static struct pci_device *enumerate_pci(void)
{
	struct pci_device *devices = NULL;
	int n_devices = 0;

	const char *devices_dir = "/sys/bus/pci/devices/";

	DIR *dir = opendir(devices_dir);
	assert(dir);

	struct dirent *dirent;
	while((dirent = readdir(dir))) {

		if (!strcmp(dirent->d_name, ".") ||
		    !strcmp(dirent->d_name, ".."))
			continue;

		devices = realloc(devices, sizeof(*devices) * (n_devices + 2));

		struct pci_device *device = &devices[n_devices];

		memset(device, 0, sizeof(*device));
		device->location = strdup(dirent->d_name);

		{
		char *filename;
		asprintf(&filename, "%s/%s/vendor",
			devices_dir, dirent->d_name);

		int fd = open(filename, O_RDONLY);
		if (fd < 0) {
			perror("open()");
			abort();
		}
		free(filename);

		char buf[32];
		if (read(fd, buf, sizeof(buf)) < 0) {
			perror("read()");
			abort();
		}

		close(fd);

		sscanf(buf, "0x%04x", &device->vendor_id);
		}

		{
		char *filename;
		asprintf(&filename, "%s/%s/device",
			devices_dir, dirent->d_name);

		int fd = open(filename, O_RDONLY);
		if (fd < 0) {
			perror("open()");
			abort();
		}
		free(filename);

		char buf[32];
		if (read(fd, buf, sizeof(buf)) < 0) {
			perror("read()");
			abort();
		}

		close(fd);

		sscanf(buf, "0x%04x", &device->device_id);
		}

		{
		char *filename;
		asprintf(&filename, "%s/%s/resource",
			devices_dir, dirent->d_name);

		FILE *f = fopen(filename, "r");
		if (!f) {
			perror("fopen()");
			abort();
		}
		free(filename);

		char buf[80];
		while (fgets(buf, sizeof(buf), f)) {

			sscanf(buf, "0x%16llx 0x%16llx 0x%16llx",
				&device->resources[device->n_resources].begin,
				&device->resources[device->n_resources].end,
				&device->resources[device->n_resources].flags);

			if (device->resources[device->n_resources].flags)
				device->n_resources++;
		}

		fclose(f);
		}

		n_devices++;
		devices[n_devices].location = NULL;
	}

	closedir(dir);

	return devices;
}

const char *status_to_text(enum status status)
{
	switch(status) {
	case STATUS_NOT_TESTED:
		return "Not tested";
	case STATUS_TESTING:
		return "Test in progress";
	case STATUS_OK:
		return "Tested OK";
	case STATUS_FAILED:
		return "Test FAILED!!!";
	}

	return "*UNKNOWN*";
}

static struct vgsm_card *select_card(
	struct vgsm_card *cards, int n_cards, int cur_card)
{
	beep();

	WINDOW *win = newwin(LINES - 6, 0, 5, 0);
	wbkgd(win, COLOR_PAIR(REQ_DIALOG_BG) | ' ');
	wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);
	mvwaddstr(win, 1, 1, "Select one card:");

	update_funcbar("ENTER - Select | F2 - Halt | F3 - Reboot | F10 - Exit");

	keypad(win, TRUE);
//	nodelay(win, FALSE);


	ITEM **items = NULL;

	int n_items = 0;
	MENU *menu;
	int i;

	for(i=0; i<n_cards; i++) {

		struct vgsm_card *card = &cards[i];

		char *descr;
		asprintf(&descr, "%s",
			status_to_text(card->status));

		items = realloc(items, sizeof(*items) * (n_items + 2));

		items[n_items] = new_item(card->pci_device->location, descr);
	
		n_items++;
		items[n_items] = NULL;
	}
	
	menu = new_menu(items);

	int rows, cols;
	scale_menu(menu, &rows, &cols);
	set_menu_win(menu, win);
	set_menu_sub(menu, derwin(win, rows, cols, 2, 2));

	post_menu(menu);

	int done = 0;
	int c = 0;
	struct vgsm_card *card = NULL;

	while(!done)
	{
		wrefresh(win);

		c = wgetch(win);

		switch(c) {
		case KEY_DOWN:
			menu_driver(menu, REQ_DOWN_ITEM);
		break;

		case KEY_UP:
			menu_driver(menu, REQ_UP_ITEM);
		break;

		case '\r':
			card = &cards[item_index(current_item(menu))];
			goto out;
		break;

		case KEY_F(2):
			system("/sbin/halt");
			goto out;
		break;

		case KEY_F(3):
			system("/sbin/reboot");
			goto out;
		break;

		case KEY_F(10):
			goto out;
		break;
		}
	}	

out:;

	unpost_menu(menu);

	for(i=0; i<n_items; i++)
		free_item(items[i]);

	free_menu(menu);

	return card;
}

void card_test(struct vgsm_card *card)
{
	/* Make sure the card is out of reset */
	vgsm_outl(card, VGSM_R_SERVICE, 0);

	update_info(card);
	update_funcbar(" F10 - Exit");

	char tmpstr[64];
	time_t now = time(NULL);
	strftime(tmpstr, sizeof(tmpstr),
		"%a, %d  %b  %Y  %H:%M:%S  %z",
		gmtime(&now));
	log_info("\n=========== Test session started %s ========\n\n",
		tmpstr);

	card->status = STATUS_TESTING;

	int i;
	for(i=0; i<ARRAY_SIZE(tests); i++) {

retry:;
		log_info("----------- %s ----------\n",
			tests[i].name);

		int err = tests[i].doit(card, tests[i].par);

		if (err == ERR_FAILED) {
			card->status = STATUS_FAILED;

			log_info("#### TEST FAILED!!!\n\n");

askagain:;
			int c = requester(FAIL_DIALOG_BG,
				"Test failed: (r)etry? (i)gnore? (q)uit?...");
			if (c == 'r' || c == 'R') {
				goto retry;
			} if (c == 'i' || c == 'I') {
				log_info("#### TEST IGNORED\n\n");
				continue;
			} else if (c == 'q' || c == 'Q') {
				break;
			} else
				goto askagain;

		} else if (err == ERR_INTERRUPTED) {

			card->status = STATUS_NOT_TESTED;

			log_info("#### TEST INTERRUPTED\n\n");

			break;
		} else {
			log_info("#### OK\n\n");
		}
	}

	now = time(NULL);
	strftime(tmpstr, sizeof(tmpstr),
		"%a, %d  %b  %Y  %H:%M:%S  %z",
		gmtime(&now));

	if (card->status == STATUS_TESTING) {

		card->status = STATUS_OK;

		log_info("\n=========== Test session successfully "
			"completed on %s ========\n\n",
			tmpstr);

	} else if (card->status == STATUS_NOT_TESTED) {
		log_info("\n=========== Test session interrupted"
			" %s ========\n\n",
			tmpstr);
	} else {
		log_info("\n=========== Test session FAILED"
			" %s ========\n\n",
			tmpstr);

		card->status = STATUS_FAILED;
	}
}

void print_usage(int argc, char *argv[])
{
	fprintf(stderr,
		"%s: [options]\n"
		"\n"
		"Options:"
		"    -l --logdir: Create test reports in specified directory\n"
		"\n", argv[0]);

	exit(1);
}

int main(int argc, char *argv[])
{
	const char *logdir = NULL;
	int c;
	int optidx;

	struct option options[] = {
		{ "logdir", required_argument, 0, 0 },
		{ }
	};

	for(;;) {
		struct option no_opt ={ "", no_argument, 0, 0 };
		struct option *opt;

		c = getopt_long(argc, argv, "v", options, &optidx);

		if (c == -1)
			break;

		opt = c ? &no_opt : &options[optidx];

		if (c == 'l' || !strcmp(opt->name, "logdir"))
			logdir = optarg;
		else {
			print_usage(argc, argv);
			return 1;
		}
	}

	signal(SIGALRM, sigalarm_handler);
	signal(SIGINT, sigint_handler);

	initscr();      /* initialize the curses library */
	nonl();         /* tell curses not to do NL->CR/NL on output */
	cbreak();       /* take input chars one at a time, no wait for \n */
	noecho();       /* don't echo input */
	keypad(stdscr, TRUE);  /* enable keyboard mapping */

	infowin = derwin(stdscr, 5, 0, 0, 0);
	syncok(infowin, TRUE);

	outwin = derwin(stdscr, LINES - 6, 0, 5, 0);
	scrollok(outwin, TRUE);
	syncok(outwin, TRUE);

	funcbar = derwin(stdscr, 1, 0, LINES - 1, 0);
	syncok(funcbar, TRUE);

	if (has_colors()) {
		start_color();

		init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLUE);
		init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLUE);
		init_pair(COLOR_RED, COLOR_RED, COLOR_BLUE);
		init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLUE);
		init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLUE);
		init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLUE);
		init_pair(COLOR_BLUE, COLOR_WHITE, COLOR_BLUE);
		init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLUE);

		init_color(DARK_GREEN, 0, 64, 0);
		init_pair(INFO_BG, COLOR_WHITE, COLOR_GREEN);
		init_pair(OK_DIALOG_BG, COLOR_WHITE, COLOR_GREEN);
		init_pair(REQ_DIALOG_BG, COLOR_WHITE, COLOR_YELLOW);
		init_pair(FAIL_DIALOG_BG, COLOR_WHITE, COLOR_RED);

		wbkgd(infowin, COLOR_PAIR(INFO_BG) | ' ');
		wbkgd(outwin, COLOR_PAIR(COLOR_WHITE) | ' ');
	}

	struct vgsm_card *cards = NULL;
	int n_cards = 0;

	struct pci_device *devices = enumerate_pci();
	struct pci_device *device = devices;

	for(; device->location ; device++) {

		if (device->vendor_id != 0x1ab9 ||
		    device->device_id != 0x0001)
			continue;

		assert(device->n_resources == 2);

		cards = realloc(cards, sizeof(*cards) * (n_cards + 2));
		memset(&cards[n_cards], 0, sizeof(cards[0]));

		cards[n_cards].status = STATUS_NOT_TESTED;
		cards[n_cards].pci_device = device;
		cards[n_cards].regs_base = device->resources[0].begin;
		cards[n_cards].fifo_base = device->resources[1].begin;

		if (map_memory(&cards[n_cards]))

			return 1;

		n_cards++;
	}

	int cur_card = 0;

	while(1) {
		struct vgsm_card *card;

		update_info(NULL);

		card = select_card(cards, n_cards, cur_card);
		if (!card)
			break;

		char date_str[64];
		time_t now = time(NULL);
		strftime(date_str, sizeof(date_str),
			"%Y%m%d-%H%M%S",
			gmtime(&now));

		char *filename = NULL;

		if (logdir) {
			asprintf(&filename, "%s/XXXXXXXXXXXXXX-%s", logdir,
				date_str);

			report_f = fopen(filename, "w");
			if (!report_f) {
				perror("fopen()");
				abort();
			}
		}

		card_test(card);

		if (logdir) {
			char *filename2;
			asprintf(&filename2, "%s/%012u-%s", logdir,
				card->serial, date_str);

			if (rename(filename, filename2) < 0) {
				perror("rename()");
				abort();
			}
			free(filename2);
			free(filename);

			fclose(report_f);
		}

		has_to_exit = 0;
	}

	endwin();

	return 0;
}
