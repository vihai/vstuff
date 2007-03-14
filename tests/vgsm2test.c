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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/types.h>

#include "../modules/vgsm2/regs.h"

#include "longtime.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

struct vgsm_card
{
	int memfd;

	void *regs;
	void *fifo;

	unsigned long regs_base;
	unsigned long fifo_base;
};

/*static void writeb(void *addr, __u8 val)
{
	*(volatile __u8 *)addr = val;
}*/

/*static void writew(void *addr, __u16 val)
{
	*(volatile __u16 *)addr = val;
}*/

static void writel(void *addr, __u32 val)
{
	*(volatile __u32 *)addr = val;
}

static __u8 readb(void *addr)
{
	return *(volatile __u8 *)addr;
}

/*static __u16 readw(void *addr)
{
	return *(volatile __u16 *)addr;
}*/

static __u32 readl(void *addr)
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
}

static __u32 vgsm_inl(struct vgsm_card *c, __u32 addr)
{
	return *(volatile __u32 *)(c->regs + addr);
}

static void vgsm_outb(struct vgsm_card *c, __u32 addr, __u8 val)
{
	*(volatile __u8 *)(c->regs + addr) = val;
}

static void vgsm_outw(struct vgsm_card *c, __u32 addr, __u16 val)
{
	*(volatile __u16 *)(c->regs + addr) = val;
}

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

static void print_info(struct vgsm_card *card)
{
	printf("R_INFO: 0x%08x\n", vgsm_inl(card, VGSM_R_INFO));
	printf("R_SERIAL: 0x%08x\n", vgsm_inl(card, VGSM_R_SERIAL));
	printf("R_SERIAL: 0x%08x\n", vgsm_inl(card, VGSM_R_SERIAL));
	printf("R_VERSION: 0x%08x\n", vgsm_inl(card, VGSM_R_VERSION));

	printf("R_INT_STATUS: 0x%08x\n", vgsm_inl(card, VGSM_R_INT_STATUS));
	printf("R_INT_ENABLE: 0x%08x\n", vgsm_inl(card, VGSM_R_INT_ENABLE));

	printf("R_SIM_ROUTER: 0x%08x\n", vgsm_inl(card, VGSM_R_SIM_ROUTER));

	*(__u32 *)(card->fifo + 0x1000) = 0x12345678;
	*(__u32 *)(card->fifo + 0x4) = 0xbebebe32;

	int i;
	for(i=0; i<4; i++) {
		printf("\n");
		printf("R_SIM_SETUP(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_SIM_SETUP(i)));
		printf("R_SIM_STATUS(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_SIM_STATUS(i)));
		printf("R_ME_SETUP(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_ME_SETUP(i)));
		printf("R_ME_STATUS(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_ME_STATUS(i)));
		printf("R_ME_INT_STATUS(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_ME_INT_STATUS(i)));
		printf("R_ME_INT_ENABLE(%d): 0x%08x\n", i,
			vgsm_inl(card, VGSM_R_ME_INT_ENABLE(i)));
	}
/*
	while(1) {
		printf("R_INT_STATUS    : 0x%08x\n", vgsm_inl(card, VGSM_R_INT_STATUS));
		printf("R_INT_ENABLE    : 0x%08x\n\n", vgsm_inl(card, VGSM_R_INT_ENABLE));

		printf("R_ME_INT_STATUS: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_INT_STATUS(0)));
		printf("R_ME_INT_ENABLE: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_INT_ENABLE(0)));
		printf("R_SIM_STATUS    : 0x%08x\n", vgsm_inl(card, VGSM_R_SIM_STATUS(0)));
		printf("R_SIM_INT_STATUS: 0x%08x\n", vgsm_inl(card, VGSM_R_SIM_INT_STATUS(0)));
		printf("R_SIM_INT_ENABLE: 0x%08x\n\n", vgsm_inl(card, VGSM_R_SIM_INT_ENABLE(0)));
		usleep(100000);
	}


*/
}

static void test_dai_speed(struct vgsm_card *card)
{
	__s64 start, stop;
	struct timeval tv;
	float speed;
	int k;

#define ITER 10000
	__u32 fifo_size = vgsm_inl(card, VGSM_R_ME_FIFO_SIZE(0));
	__u32 rx_fifo_size = VGSM_R_ME_FIFO_SIZE_V_RX_SIZE(fifo_size);

	printf("RX fifo size: %d\n", rx_fifo_size);

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000ULL + tv.tv_usec;

	for(k=0;k<ITER;k++) {
		int i;
		for(i=0; i<rx_fifo_size; i+=4) {
			*(volatile __u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i);
		}
	}

	gettimeofday(&tv, NULL);
	stop = tv.tv_sec * 1000000ULL + tv.tv_usec;
	speed = ((float)ITER * sizeof(__u32) * rx_fifo_size) / (stop-start);

	printf("FIFO read speed: %.3f MB/s\n", speed);

	if (speed < 15) {
		fprintf(stderr, "Read speed not conformant (< 180 MB/s)\n");
		exit(1);
	}

	if (speed > 35) {
		fprintf(stderr, "Read speed not conformant (> 280 MB/s)\n");
		exit(1);
	}

	/*-----------------------------------------------*/
	__u32 tx_fifo_size = VGSM_R_ME_FIFO_SIZE_V_TX_SIZE(fifo_size);
	printf("TX fifo size: %d\n", tx_fifo_size);

	gettimeofday(&tv, NULL);
	start = tv.tv_sec * 1000000ULL + tv.tv_usec;

	for(k=0;k<ITER;k++) {
		int i;
		for(i=0; i<tx_fifo_size; i+=4) {
			*(volatile __u32 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i) = 0x00000000;
//		usleep(100);
		}
	}

	gettimeofday(&tv, NULL);
	stop = tv.tv_sec * 1000000ULL + tv.tv_usec;
	speed = ((float)ITER * sizeof(__u32) * tx_fifo_size) / (stop-start);

	printf("FIFO write speed: %.3f MB/s\n", speed);

	if (speed < 50) {
		fprintf(stderr, "Write speed not conformant (< 180 MB/s)\n");
		exit(1);
	}

	if (speed > 70) {
		fprintf(stderr, "Write speed not conformant (> 280 MB/s)\n");
		exit(1);
	}

}

static void test_dai(struct vgsm_card *card)
{
	int i=0;

//	vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(0), 
//		VGSM_R_ME_FIFO_SETUP_V_RX_ALAW |
//		VGSM_R_ME_FIFO_SETUP_V_TX_ALAW);

/*	printf("RX_FIFO: %08x\n", *(volatile __u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0)));
	printf("RX_FIFO: %04x %04x\n",
		*(volatile __u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0)),
		*(volatile __u16 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 2));
	printf("RX_FIFO: %02x %02x %02x %02x\n",
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0)),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 1),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 2),
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 3));
	printf("RX_FIFO2: %08x\n", *(volatile __u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + 4));
exit(0);*/

	vgsm_outl(card, VGSM_R_ME_FIFO_TX_IN(0), 0x124);

	for(i=0; i<0x800; i+=4) {
//		*(volatile __u32 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i) = 0x00000000;
//		*(volatile __u16 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i) = 0x5500;
//		*(volatile __u16 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i + 2) = 0x5500;
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i) = 0x55;
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i + 1) = 0x55;
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i + 2) = 0x55;
		*(volatile __u8 *)(card->fifo + VGSM_FIFO_TX_BASE(0) + i + 3) = 0x55;
	}

	while(1) {
//		printf("VGSM_R_ME_FIFO_RX_IN: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(0)));
//		printf("VGSM_R_ME_FIFO_TX_IN: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_TX_IN(0)));
//		printf("VGSM_R_ME_FIFO_TX_OUT: 0x%08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(0)));

		
		struct timeval tv;
		gettimeofday(&tv, NULL);


		printf("%d RX_FIFO(%04x) RXIN=%04x TXOUT=%04x:"
			" %08x %08x %08x %08x\n", tv.tv_usec, i,
			vgsm_inl(card, VGSM_R_ME_FIFO_RX_IN(0)),
			vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(0)),
			*(__u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i),
			*(__u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 4),
			*(__u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 8),
			*(__u32 *)(card->fifo + VGSM_FIFO_RX_BASE(0) + i + 12));
//		usleep(1000);

		i+=16;
		if(i>0x800) i=0;
	}
}

static void test_asmi(struct vgsm_card *card)
{
	printf("R_ASMI_CTL: 0x%08x\n", vgsm_inl(card, VGSM_R_ASMI_CTL));
	vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x1);
	vgsm_outl(card, VGSM_R_ASMI_CTL, VGSM_R_ASMI_CTL_V_READ_SID);
	usleep(10000);
//	vgsm_outl(card, VGSM_R_ASMI_CTL, 0);
	__u32 asmi_ctl;
	while((asmi_ctl = vgsm_inl(card, VGSM_R_ASMI_CTL)) & VGSM_R_ASMI_CTL_V_BUSY) {
		printf("R_ASMI_CTL: 0x%08x 0x%08x\n", asmi_ctl, vgsm_inl(card, VGSM_R_ASMI_IO));
		usleep(10000);
	}
	printf("R_ASMI_CTL2: 0x%08x 0x%08x\n", asmi_ctl, vgsm_inl(card, VGSM_R_ASMI_IO));
	vgsm_outl(card, VGSM_R_ASMI_CTL, 0x0);

}

static void t_reg_access_1(struct vgsm_card *card)
{
	int cnt;

	for(cnt=0; cnt<1000; cnt++) {

		int i;

		for(i=0; i<0xff; i++) {
			__u32 val = i << 24 | i << 16 | i << 8 | i;
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val) {
				fprintf(stderr, "%s:%d failed,"
					" wrote 0x%08x, read 0x%08x\n",
					__FUNCTION__,
					cnt, val, val2);
				exit(1);
			}
		}
	}
}

static void t_reg_access_2(struct vgsm_card *card)
{
	int cnt;

	for(cnt=0; cnt<1000; cnt++) {

		int i;

		for(i=0; i<32; i++) {
			__u32 val = 1 << i;
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val) {
				fprintf(stderr, "%s:%d failed,"
					" wrote 0x%08x, read 0x%08x\n",
					__FUNCTION__,
					cnt, val, val2);
				exit(1);
			}
		}
	}
}

static void t_reg_access_3(struct vgsm_card *card)
{
	int cnt;

	for(cnt=0; cnt<1000; cnt++) {

		int i;

		for(i=0; i<32; i++) {
			__u32 val = 0xffffffff & ~(1 << i);
			__u32 val2;
			vgsm_outl(card, VGSM_R_TEST, val);
			val2 = vgsm_inl(card, VGSM_R_TEST);

			if (val2 != val) {
				fprintf(stderr, "%s:%d failed,"
					" wrote 0x%08x, read 0x%08x\n",
					__FUNCTION__,
					cnt, val, val2);
				exit(1);
			}
		}
	}
}

static void test_reg_access(struct vgsm_card *card)
{
	t_reg_access_1(card);
	t_reg_access_2(card);
	t_reg_access_3(card);
}

static void sim_setup(struct vgsm_card *card)
{
        vgsm_outl(card, VGSM_R_SIM_SETUP(0), VGSM_R_SIM_SETUP_V_VCC | VGSM_R_SIM_SETUP_V_3V);
        vgsm_outl(card, VGSM_R_SIM_SETUP(1), VGSM_R_SIM_SETUP_V_VCC | VGSM_R_SIM_SETUP_V_3V);
        vgsm_outl(card, VGSM_R_SIM_SETUP(2), VGSM_R_SIM_SETUP_V_VCC | VGSM_R_SIM_SETUP_V_3V);
        vgsm_outl(card, VGSM_R_SIM_SETUP(3), VGSM_R_SIM_SETUP_V_VCC | VGSM_R_SIM_SETUP_V_3V);
}

static void test_leds(struct vgsm_card *card)
{
	/* Set LEDs */
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);

	vgsm_outl(card, VGSM_R_LED_USER, 0xffffffff|
		VGSM_R_LED_USER_V_STATUS_G);
}

static void turn_on_modules(struct vgsm_card *card)
{
	vgsm_outl(card, VGSM_R_ME_SETUP(0), VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_outl(card, VGSM_R_ME_SETUP(1), VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_outl(card, VGSM_R_ME_SETUP(2), VGSM_R_ME_SETUP_V_EMERG_OFF);
	vgsm_outl(card, VGSM_R_ME_SETUP(3), VGSM_R_ME_SETUP_V_EMERG_OFF);
	usleep(3200000);

	vgsm_outl(card, VGSM_R_ME_SETUP(0), VGSM_R_ME_SETUP_V_ON);
	vgsm_outl(card, VGSM_R_ME_SETUP(1), VGSM_R_ME_SETUP_V_ON);
	vgsm_outl(card, VGSM_R_ME_SETUP(2), VGSM_R_ME_SETUP_V_ON);
	vgsm_outl(card, VGSM_R_ME_SETUP(3), VGSM_R_ME_SETUP_V_ON);
	usleep(120000);
	vgsm_outl(card, VGSM_R_ME_SETUP(0), 0);
	vgsm_outl(card, VGSM_R_ME_SETUP(1), 0);
	vgsm_outl(card, VGSM_R_ME_SETUP(2), 0);
	vgsm_outl(card, VGSM_R_ME_SETUP(3), 0);
}

static void test_serial(struct vgsm_card *card)
{
	int i;
	int me = 1;

#define RECV		(VGSM_ME_ASC0_BASE(me) + (0 << 2))
#define XMIT		(VGSM_ME_ASC0_BASE(me) + (0 << 2))
#define DLAB_LSB	(VGSM_ME_ASC0_BASE(me) + (0 << 2))
#define DLAB_MSB	(VGSM_ME_ASC0_BASE(me) + (1 << 2))
#define IER		(VGSM_ME_ASC0_BASE(me) + (1 << 2))
#define IIR		(VGSM_ME_ASC0_BASE(me) + (2 << 2))
#define FCR		(VGSM_ME_ASC0_BASE(me) + (2 << 2))
#define LCR		(VGSM_ME_ASC0_BASE(me) + (3 << 2))
#define MCR		(VGSM_ME_ASC0_BASE(me) + (4 << 2))
#define LSR		(VGSM_ME_ASC0_BASE(me) + (5 << 2))
#define MSR		(VGSM_ME_ASC0_BASE(me) + (6 << 2))

	vgsm_outl(card, LCR, 0x80); //Enable Latch
	vgsm_outl(card, DLAB_MSB, 0);
	vgsm_outl(card, DLAB_LSB, 0x9);
	printf("DLAB_LSB %08x\n", vgsm_inl(card, DLAB_LSB));
	printf("DLAB_MSB %02x\n", vgsm_inl(card, DLAB_MSB));
	vgsm_outl(card, LCR, 0x03); //Disable Latch
	vgsm_outl(card, FCR, 0x87);
	vgsm_outl(card, MCR, 0x0b);

#if 1
	while(1) {
//		printf("%02x\n", vgsm_inl(card, RECV));
//		printf("R_INFO: 0x%08x\n", vgsm_inl(card, VGSM_R_INFO));

	vgsm_outl(card, LCR, 0x80); //Enable Latch
	printf("DLAB_LSB %08x\n", vgsm_inl(card, DLAB_LSB));
	printf("DLAB_MSB %02x\n", vgsm_inl(card, DLAB_MSB));
	vgsm_outl(card, LCR, 0x03); //Disable Latch
		printf("MSR %02x LSR %02x IIR %02x IER %02x\n", vgsm_inl(card, MSR), vgsm_inl(card, LSR), vgsm_inl(card, IIR), vgsm_inl(card, IER));
		vgsm_outl(card, XMIT, 'A');
		vgsm_outl(card, MCR, 0x03030303);
		usleep(300000);
		vgsm_outl(card, MCR, 0x00);
		usleep(100000);
	}
#endif

}

static void test_audio(struct vgsm_card *card)
{
	int i;

	while(1) {
		printf("RX_0_OUT: %08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_RX_OUT(0)));
		printf("TX_0_OUT: %08x\n", vgsm_inl(card, VGSM_R_ME_FIFO_TX_OUT(0)));
		usleep(200000);
	}
}

int main(int argc, char *argv[])
{
	int c;
	int optidx;
	struct vgsm_card card;

	struct option options[] = {
		{ "--byte", no_argument, 0, 0 },
		{ "--word", no_argument, 0, 0 },
		{ "--dword", no_argument, 0, 0 },
		{ }
	};

	for(;;) {
		struct option no_opt ={ "", no_argument, 0, 0 };
		struct option *opt;

		c = getopt_long(argc, argv, "bwd", options, &optidx);

		if (c == -1)
			break;

		opt = c ? &no_opt : &options[optidx];

		if (c == 'b' || !strcmp(opt->name, "dummy")) {
		} else {
			if (c) {
				fprintf(stderr, "Unknow option '%c'\n", c);
			} else {
				fprintf(stderr, "Unknow option %s\n",
					options[optidx].name);
			}

			return 1;
		}
	}

	if (argc == 1) {
		fprintf(stderr,
			"Usage: %s <base>\n",
			argv[0]);
		return 1;
	}

	if (argc < optind + 1) {
		fprintf(stderr, "Base addresses not specified\n");
		return 1;
	}

	if (sscanf(argv[optind], "%lx", &card.regs_base) < 1) {
		fprintf(stderr, "Invalid base address\n");
		return 1;
	}
	printf("Registers base address = 0x%08lx\n", card.regs_base);

	if (sscanf(argv[optind+1], "%lx", &card.fifo_base) < 1) {
		fprintf(stderr, "Invalid fifo base address\n");
		return 1;
	}
	printf("FIFO base address = 0x%08lx\n", card.fifo_base);

	if (map_memory(&card))
		return 1;

	print_info(&card);	
//	test_reg_access(&card);
//	turn_on_modules(&card);
//	test_dai_speed(&card);
	test_dai(&card);
//	sim_setup(&card);
//	test_leds(&card);
//	test_serial(&card);
//	test_audio(&card);

	return 0;
}
