/*
 * DSP tester
 *
 * Copyright (C) 2006 Daniele Orlandi
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

#include <longtime.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char BOOL;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define R_HDCR		0x01C1FFF4
#define R_HDCR_WARMRESET		(1 << 0)
#define R_HDCR_DSPINT			(1 << 1)
#define R_HDCR_PCIBOOT			(1 << 2)

#define R_PCIIEN	0x01C0000C
#define R_PCIIEN_PWRMGMT		(1 << 0)
#define R_PCIIEN_PCITARGET		(1 << 1)
#define R_PCIIEN_PCIMASTER		(1 << 2)
#define R_PCIIEN_HOSTSW			(1 << 3)
#define R_PCIIEN_PWRLH			(1 << 4)
#define R_PCIIEN_PWRHL			(1 << 5)
#define R_PCIIEN_MASTEROK		(1 << 6)
#define R_PCIIEN_CFGDONE		(1 << 7)
#define R_PCIIEN_CFGERR			(1 << 8)
#define R_PCIIEN_EERDY			(1 << 9)
#define R_PCIIEN_PRST			(1 << 11)

#define R_CCFG		0x01840000
#define R_CCFG_0KC			(0x0 << 0)
#define R_CCFG_32KC			(0x1 << 0)
#define R_CCFG_64KC			(0x2 << 0)
#define R_CCFG_128KC			(0x3 << 0)
#define R_CCFG_256KC			(0x7 << 0)
#define R_CCFG_ID			(1 << 8)
#define R_CCFG_IP			(1 << 9)
#define R_CCFG_P_URGENT			(0x0 << 29)
#define R_CCFG_P_HIGH			(0x1 << 29)
#define R_CCFG_P_MEDIUM			(0x2 << 29)
#define R_CCFG_P_LOW			(0x3 << 29)

#define R_L1DWIBAR	0x01844030
#define R_L1DWIWC	0x01844034

#define R_GPEN		0x01B00000
#define R_GPDIR		0x01B00004
#define R_GPVAL		0x01B00008
#define R_GPDH		0x01B00010
#define R_GPHM		0x01B00014
#define R_GPDL		0x01B00015
#define R_GPLM		0x01B0001C
#define R_GPGC		0x01B00020
#define R_GPPOL		0x01B00024


#define R_GBLCTL	0x01800000
#define R_GBLCTL_CLK6EN			(1 << 3)
#define R_GBLCTL_CLK4EN			(1 << 4)
#define R_GBLCTL_EK1EN			(1 << 5)
#define R_GBLCTL_EK1HZ			(1 << 6)
#define R_GBLCTL_NOHOLD			(1 << 7)
#define R_GBLCTL_HOLDA			(1 << 8)
#define R_GBLCTL_HOLD			(1 << 9)
#define R_GBLCTL_ARDY			(1 << 10)
#define R_GBLCTL_BUSREQ			(1 << 11)
#define R_GBLCTL_BRMODE			(1 << 13)
#define R_GBLCTL_EK2EN			(1 << 16)
#define R_GBLCTL_EK2HZ			(1 << 17)
#define R_GBLCTL_EK2RATE_FULLCLK	(0x0 << 18)
#define R_GBLCTL_EK2RATE_HALFCLK	(0x1 << 18)
#define R_GBLCTL_EK2RATE_QUARCLK	(0x2 << 18)

#define R_CECTL1	0x01800004
#define R_CECTL0	0x01800008
#define R_CECTL2	0x01800010
#define R_CECTL3	0x01800014
#define R_CECTL_RDHLD(n)		((n) << 0)
#define R_CECTL_WRHLDMSB		(1 << 3)
#define R_CECTL_MTYPE_ASYNC8		(0x0 << 4)
#define R_CECTL_MTYPE_ASYNC16		(0x1 << 4)
#define R_CECTL_MTYPE_ASYNC32		(0x2 << 4)
#define R_CECTL_MTYPE_SDRAM32		(0x3 << 4)
#define R_CECTL_MTYPE_SYNC32		(0x4 << 4)
#define R_CECTL_MTYPE_SDRAM8		(0x8 << 4)
#define R_CECTL_MTYPE_SDRAM16		(0x9 << 4)
#define R_CECTL_MTYPE_SYNC8		(0xa << 4)
#define R_CECTL_MTYPE_SYNC16		(0xb << 4)
#define R_CECTL_MTYPE_ASYNC64		(0xc << 4)
#define R_CECTL_MTYPE_SDRAM64		(0xd << 4)
#define R_CECTL_MTYPE_SYNC64		(0xe << 4)
#define R_CECTL_RDSTRB(n)		((n) << 8)
#define R_CECTL_TA(n)			((n) << 14)
#define R_CECTL_RDSETUP(n)		((n) << 16)
#define R_CECTL_WRHLD(n)		((n) << 20)
#define R_CECTL_WRSTRB(n)		((n) << 22)
#define R_CECTL_WRSETUP(n)		((n) << 28)

#define R_SDCTL		0x01800018
#define R_SDCTL_SLFRFR			(1 << 0)
#define R_SDCTL_TRC(n)			((n) << 12)
#define R_SDCTL_TRP(n)			((n) << 16)
#define R_SDCTL_TRCD(n)			((n) << 20)
#define R_SDCTL_INIT			(1 << 24)
#define R_SDCTL_RFEN			(1 << 25)
#define R_SDCTL_SDCSZ_9COL		(0x0 << 26)
#define R_SDCTL_SDCSZ_8COL		(0x1 << 26)
#define R_SDCTL_SDCSZ_10COL		(0x2 << 26)
#define R_SDCTL_SDRSZ_11ROW		(0x0 << 28)
#define R_SDCTL_SDRSZ_12ROW		(0x1 << 28)
#define R_SDCTL_SDRSZ_13ROW		(0x2 << 28)
#define R_SDCTL_SDBSZ_2BANKS		(0 << 30)
#define R_SDCTL_SDBSZ_4BANKS		(1 << 30)

#define R_SDTIM		0x0180001C
#define R_SDTIM_PERIOD(n)		((n) << 0)
#define R_SDTIM_XRFR(n)			((n) << 24)

#define R_SDEXT		0x01800020
#define R_SDEXT_TCL_2			(0 << 0)
#define R_SDEXT_TCL_3			(1 << 0)
#define R_SDEXT_TRAS(n)			((n) << 1)
#define R_SDEXT_TRRD_2			(0 << 4)
#define R_SDEXT_TRRD_3			(1 << 4)
#define R_SDEXT_TWR(n)			((n) << 5)
#define R_SDEXT_THZP(n)			((n) << 7)
#define R_SDEXT_RD2RD_1			(0 << 9)
#define R_SDEXT_RD2RD_2			(1 << 9)
#define R_SDEXT_RD2DEAC(n)		((n) << 10)
#define R_SDEXT_RD2WR(n)		((n) << 12)
#define R_SDEXT_R2WDQM(n)		((n) << 15)
#define R_SDEXT_WR2WR(n)		((n) << 17)
#define R_SDEXT_WR2DEAC(n)		((n) << 18)
#define R_SDEXT_WR2RD(n)		((n) << 20)

#define R_PDTCTL	0x01800040

#define R_CESEC1	0x01800044
#define R_CESEC0	0x01800048
#define R_CESEC2	0x01800050
#define R_CESEC3	0x01800054
#define R_CESEC_SYNCRL_0CYCLE		(0x0 << 0)
#define R_CESEC_SYNCRL_1CYCLE		(0x1 << 0)
#define R_CESEC_SYNCRL_2CYCLE		(0x2 << 0)
#define R_CESEC_SYNCRL_3CYCLE		(0x3 << 0)
#define R_CESEC_SYNCWL_0CYCLE		(0x0 << 2)
#define R_CESEC_SYNCWL_1CYCLE		(0x1 << 2)
#define R_CESEC_SYNCWL_2CYCLE		(0x2 << 2)
#define R_CESEC_SYNCWL_3CYCLE		(0x3 << 2)
#define R_CESEC_CEEXT			(1 << 4)
#define R_CESEC_RENEN			(1 << 5)
#define R_CESEC_SNCCLK			(1 << 6)


#define R_DEVSTAT	0x01B3F004
#define R_JTAGID	0x01B3F008

#define R_DSPP		0x01C1FFF8

#define R_RSTSRC	0x01C00000
#define R_RSTSRC_RST			(1 << 0)
#define R_RSTSRC_PRST			(1 << 1)
#define R_RSTSRC_WARMRST		(1 << 2)
#define R_RSTSRC_INTREQ			(1 << 3)
#define R_RSTSRC_INTRST			(1 << 4)
#define R_RSTSRC_CFGDONE		(1 << 5)
#define R_RSTSRC_CFGERR			(1 << 6)

#define R_PCIIS		0x01C00008
#define R_PCIIS_PWRMGMT			(1 << 0)
#define R_PCIIS_PCITARGET		(1 << 1)
#define R_PCIIS_PCIMASTER		(1 << 2)
#define R_PCIIS_HOSTSW			(1 << 3)
#define R_PCIIS_PWRLH			(1 << 4)
#define R_PCIIS_PWRHL			(1 << 5)
#define R_PCIIS_MASTEROK		(1 << 6)
#define R_PCIIS_CFGDONE			(1 << 7)
#define R_PCIIS_CFGERR			(1 << 8)
#define R_PCIIS_EERDY			(1 << 9)
#define R_PCIIS_PRST			(1 << 11)
#define R_PCIIS_DMAHALTED		(1 << 12)

#define R_PCIEN		0x01C0000C

#define R_EEADD		0x01C20000
#define R_EEDAT		0x01C20004

#define R_EECTL		0x01C20008
#define R_EECTL_EECNT_EWEN		(0x0 << 0)
#define R_EECTL_EECNT_ERAL		(0x0 << 0)
#define R_EECTL_EECNT_WRAL		(0x0 << 0)
#define R_EECTL_EECNT_EWDS		(0x0 << 0)
#define R_EECTL_EECNT_WRITE		(0x1 << 0)
#define R_EECTL_EECNT_READ		(0x2 << 0)
#define R_EECTL_EECNT_ERASE		(0x3 << 0)
#define R_EECTL_READY			(1 << 2)
#define R_EECTL_EESZ_MASK		(0x7 << 3)
#define R_EECTL_EESZ_NO_EEPROM		(0x0 << 3)
#define R_EECTL_EESZ_1KBIT		(0x1 << 3)
#define R_EECTL_EESZ_2KBIT		(0x2 << 3)
#define R_EECTL_EESZ_4KBIT		(0x3 << 3)
#define R_EECTL_EESZ_16KBIT		(0x4 << 3)
#define R_EECTL_EEAI			(1 << 6)
#define R_EECTL_CFGERR			(1 << 7)
#define R_EECTL_CFGDONE			(1 << 8)


#define EE_VENDOR_ID			0x0
#define EE_DEVICE_ID			0x1
#define EE_CLASS_0			0x2
#define EE_CLASS_1			0x3
#define EE_SUBSYSTEM_VENDOR_ID		0x4
#define EE_SUBSYSTEM_DEVICE_ID		0x5
#define EE_MAX_LATENCY_MIN_GRANT	0x6
#define EE_PC_D1_PC_D0			0x7
#define EE_PC_D3_PC_D2			0x8
#define EE_PD_D1_PD_D0			0x9
#define EE_PD_D3_PD_D2			0xa
#define EE_DATA_SCALE			0xb
#define EE_PMC				0xc
#define EE_CHECKSUM			0xd


struct c6412
{
	int memfd;

	void *regs;
	void *mem;

	unsigned long regs_base;
	unsigned long mem_base;
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

static __u32 reg_readl(struct c6412 *c, __u32 addr)
{
	return *(volatile __u32 *)(c->regs + addr - 0x1800000);
}

static void reg_writel(struct c6412 *c, __u32 addr, __u32 val)
{
	*(volatile __u32 *)(c->regs + addr - 0x1800000) = val;
}

enum access_mode
{
	ACCESS_BYTE,
	ACCESS_WORD,
	ACCESS_DWORD,
};

static int map_memory(struct c6412 *c6412)
{
	c6412->memfd = open("/dev/mem", O_RDWR);
	if (c6412->memfd < 0) {
		fprintf(stderr, "Cannot open /dev/mem: %s\n", strerror(errno));
		return 1;
	}

	c6412->regs = mmap(0, 0x800000, PROT_READ|PROT_WRITE, MAP_SHARED,
			c6412->memfd, c6412->regs_base);
	if (c6412->regs == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	c6412->mem = mmap(0, 0x400000, PROT_READ|PROT_WRITE, MAP_SHARED,
			c6412->memfd, c6412->mem_base);
	if (c6412->mem == MAP_FAILED) {
		fprintf(stderr, "Cannot mmap(): %s\n", strerror(errno));
		return 1;
	}

	return 0;
}

static void eeprom_wait(struct c6412 *c6412)
{
	int i;
	for(i=0; i<200; i++) {
		if (reg_readl(c6412, R_EECTL) & R_EECTL_READY)
			return;

		usleep(10000);
	}

	printf("Timeout waiting for EEPROM!!\n");
}

int eeprom_program(struct c6412 *c6412, __u16 *data, int start, int len)
{

#define R_EEADD_EWEN 0xc0
#define R_EEADD_ERAL 0x80
#define R_EEADD_WRAL 0x40
#define R_EEADD_EWDS 0x00

	/* Enable Write */
	eeprom_wait(c6412);
	reg_writel(c6412, R_EEADD, R_EEADD_EWEN);
	reg_writel(c6412, R_EECTL, R_EECTL_EECNT_EWEN);

	/* Program Space */
	int i;
	for(i = 0; i < len; i++) {
		eeprom_wait(c6412);
		reg_writel(c6412, R_EEADD, i + start);
		reg_writel(c6412, R_EECTL, R_EECTL_EECNT_ERASE);

		eeprom_wait(c6412);
		reg_writel(c6412, R_EEADD, i + start);
		reg_writel(c6412, R_EEDAT, *(data + i));
		reg_writel(c6412, R_EECTL, R_EECTL_EECNT_WRITE);
	}

	/* Disable Write */
	eeprom_wait(c6412);
	reg_writel(c6412, R_EEADD, R_EEADD_EWDS);
	reg_writel(c6412, R_EECTL, R_EECTL_EECNT_EWDS);

	return 0;
}

int eeprom_read(struct c6412 *c6412, __u16 *data, int start, int len)
{
	int i;
	for(i=0; i < len; i++) {
		eeprom_wait(c6412);
		reg_writel(c6412, R_EEADD, i + start);
		reg_writel(c6412, R_EECTL, R_EECTL_EECNT_READ);

		eeprom_wait(c6412);
		*(data + i) = reg_readl(c6412, R_EEDAT) & 0x0000ffff;

	}

	return 0;
}

static void print_info(struct c6412 *c6412)
{
	__u32 devstat = reg_readl(c6412, R_DEVSTAT);

	if ((devstat & 0x00000fff) != 0x00000754) {
		printf("Invalid DEVSTAT: %#08x != %#08x\n",
			devstat, 0x00000754);
		return;
	}

	printf("JTAGID: 0x%08x\n", reg_readl(c6412, R_JTAGID));
	printf("PCIIS: 0x%08x\n", reg_readl(c6412, R_PCIIS));
	printf("EECTL: 0x%08x\n", reg_readl(c6412, R_EECTL));
	printf("RSTSRC: 0x%08x\n", reg_readl(c6412, R_RSTSRC));
};

static void check_eeprom(struct c6412 *c6412)
{
	int i;

	__u32 rstsrc = reg_readl(c6412, R_RSTSRC);
	if (rstsrc & R_RSTSRC_CFGERR) {
		printf("EEPROM configuration invalid, programming required\n");
	}

	__u32 pciis = reg_readl(c6412, R_PCIIS);

	printf("PCIIS:\n");
	printf("  EEPROM status: %sready\n",
		(pciis & R_PCIIS_EERDY) ? "" : "NOT ");

	printf("  EEPROM configuration: %s\n",
		(pciis & R_PCIIS_CFGERR) ? " ERROR" : "ok");

	printf("  EEPROM config-cycle: %s\n",
		(pciis & R_PCIIS_CFGDONE) ? "done" : "NOT done");


	__u32 eectl = reg_readl(c6412, R_EECTL);
	printf("EECTL:\n");
	printf("  EEPROM configuration: %s\n",
		(eectl & R_EECTL_CFGDONE) ? "done" : "NOT done");

	if ((eectl & R_EECTL_EESZ_MASK) == R_EECTL_EESZ_NO_EEPROM) {
		printf("  EEPROM: NOT DETECTED!\n");
		return;
	} else if ((eectl & R_EECTL_EESZ_MASK) == R_EECTL_EESZ_1KBIT)
		printf("  EEPROM: 1-kbit\n");
	else if ((eectl & R_EECTL_EESZ_MASK) == R_EECTL_EESZ_2KBIT)
		printf("  EEPROM: 2-kbit\n");
	else if ((eectl & R_EECTL_EESZ_MASK) == R_EECTL_EESZ_4KBIT)
		printf("  EEPROM: 4-kbit\n");
	else if ((eectl & R_EECTL_EESZ_MASK) == R_EECTL_EESZ_16KBIT)
		printf("  EEPROM: 16-kbit\n");
	else {
		printf("  EEPROM: unknown\n");
		return;
	}

	/* Wait for the EEPROM to be free */
	for(i=0; i<500; i++) {

		if (reg_readl(c6412, R_RSTSRC) & R_RSTSRC_CFGDONE &&
		    reg_readl(c6412, R_PCIIS) & R_PCIIS_EERDY &&
		    reg_readl(c6412, R_EECTL) & R_EECTL_READY)
			goto eeprom_ready;

		usleep(10000);
	}

	printf("EEPROM controller is stuck!!! Attempting reset...\n");

	reg_writel(c6412, R_EEADD, 0x0);
	reg_writel(c6412, R_EECTL, R_EECTL_EECNT_READ);
	reg_readl(c6412, R_EEDAT);

	for(i=0; i<250; i++) {

		if (reg_readl(c6412, R_RSTSRC) & R_RSTSRC_CFGDONE &&
		    reg_readl(c6412, R_PCIIS) & R_PCIIS_EERDY &&
		    reg_readl(c6412, R_EECTL) & R_EECTL_READY)
			goto eeprom_ready;

		usleep(10000);
	}

	printf("EEPROM reset failed... aborting\n");
	return;

eeprom_ready:;

	__u16 eeprom[] = {
		0x104c,	// Vendor ID
		0x9065, // Device ID
		0x0002, // Class Code [7-0] / Revision ID
		0x0b40, // Class Code [23-8]
		0x104c, // Subsystem Vendor ID
		0x9065, // Subsystem Device ID
		0x0000, // Max Latency / Min Grant
		0x0000, // PC_D1/PC_D0
		0x0000, // PC_D3/PC_D2
		0x0000, // PD_D1/PD_D0
		0x0000, // PD_D3/PD_D2
		0x0000, // Data Scale
		0x0000, // 0x00/PMC[14-9]/PCM[5]/PCM[3]
		0x0000, // Checksum
		};
	__u16 eeprom2[ARRAY_SIZE(eeprom)];

	__u16 csum = 0xaaaa;
	for(i=0; i<EE_CHECKSUM; i++)
		csum ^= eeprom[i];

	eeprom[EE_CHECKSUM] = csum;

	if (eeprom_read(c6412, eeprom2, 0, ARRAY_SIZE(eeprom)) < 0) {
		fprintf(stderr, "Cannot read EEPROM\n");
		return;
	}

	BOOL eeprom_needs_programming = FALSE;

	for(i=0; i<ARRAY_SIZE(eeprom2); i++) {
		printf("EEPROM[0x%02x] = 0x%04x\n", i, eeprom2[i]);

		if (eeprom2[i] != eeprom[i])
			eeprom_needs_programming = TRUE;
	}

	if (eeprom_needs_programming) {
		printf("------------ Programming EEPROM --------------\n");

		if (eeprom_program(c6412, eeprom, 0, ARRAY_SIZE(eeprom)) < 0) {
			fprintf(stderr, "Cannot program EEPROM\n");
			return;
		}

		if (eeprom_read(c6412, eeprom2, 0, ARRAY_SIZE(eeprom)) < 0) {
			fprintf(stderr, "Cannot read EEPROM\n");
			return;
		}

		for(i=0; i<ARRAY_SIZE(eeprom); i++) {
			printf("EEPROM[0x%02x] = 0x%04x\n", i, eeprom2[i]);

			if (eeprom2[i] != eeprom[i]) {
				fprintf(stderr,
					"Compare failed at location 0x%02x: "
					"0x%04x != 0x%04x\n",
					i, eeprom2[i], eeprom[i]);

				return;
			}
		}
	}

}

static void check_emif(struct c6412 *c6412)
{
	reg_writel(c6412, R_GBLCTL,
		R_GBLCTL_EK1EN |
//		R_GBLCTL_EK2EN |
//		R_GBLCTL_EK2RATE_FULLCLK);
		R_GBLCTL_EK2RATE_QUARCLK);

	reg_writel(c6412, R_CECTL0, R_CECTL_MTYPE_SDRAM32);
	reg_writel(c6412, R_CESEC0, 0);

	reg_writel(c6412, R_SDTIM, R_SDTIM_PERIOD(0x5d));

	reg_writel(c6412, R_SDEXT, 
		R_SDEXT_TCL_2 |
		R_SDEXT_TRAS(6) |
		R_SDEXT_TRRD_2 |
		R_SDEXT_TWR(1) |
		R_SDEXT_THZP(5) |
		R_SDEXT_RD2RD_2 |	// CHECKME
		R_SDEXT_RD2DEAC(3) |	// CHECKME
		R_SDEXT_RD2WR(5) |	// CHECKME
		R_SDEXT_R2WDQM(2) |	// CHECKME
		R_SDEXT_WR2WR(1) |	// CHECKME
		R_SDEXT_WR2DEAC(1) |	// CHECKME
		R_SDEXT_WR2RD(1));	// CHECKME

	reg_writel(c6412, R_SDCTL,
		R_SDCTL_TRC(9) |
		R_SDCTL_TRP(2) |
		R_SDCTL_TRCD(2) |
		R_SDCTL_INIT |
		R_SDCTL_RFEN |
		R_SDCTL_SDCSZ_8COL |
		R_SDCTL_SDRSZ_11ROW |
		R_SDCTL_SDBSZ_4BANKS);
usleep(100000);

#if 1

	int b=0;

	int val;
	for(val=0; val<=0xff; val++) {
		reg_writel(c6412, R_DSPP, 0x200); 
		memset(c6412->mem, val, 0x400000);

		reg_writel(c6412, R_DSPP, 0x201); 
		memset(c6412->mem, val, 0x400000);

		__u32 val2 = val << 24 | val << 16 | val << 8 | val;

		reg_writel(c6412, R_DSPP, 0x200);

		int i;
		for (i=0; i<0x400000; i+=4) {
			__u32 a = readl(c6412->mem + i);

			if (a != val2) {
				b++;
				printf("[%08x] %08x != %08x\n", i, val2, a);
			}
		}

		reg_writel(c6412, R_DSPP, 0x201); 
		for (i=0; i<0x400000; i+=4) {
			__u32 a = readl(c6412->mem + i);

			if (a != val2) {
				b++;
				printf("[%08x] %08x != %08x\n", i, val2, a);
			}
		}

		printf("%02x B=%d\n", val, b);
	}

#endif
}

static void check_gpio(struct c6412 *c6412)
{
	reg_writel(c6412, R_GPEN, 0x000000f0);
	reg_writel(c6412, R_GPDIR, 0x000000f0);
	reg_writel(c6412, R_GPVAL, 0x00000008);	

/*	for(i=0; i<10000; i++) {
		reg_writel(c6412, R_GPVAL, 0x00000008 | ((i & 0x3) << 6));	
		usleep(100000);
	}*/
}

static void upload_firmware(struct c6412 *c6412)
{
	int fd = open("dsptest.fw", O_RDONLY);
	if (fd < 0) {
		perror("open(dsptest.fw)\n");
		return;
	}
	
	reg_writel(c6412, R_DSPP, 0x0); 

	read(fd, c6412->mem, 10000);

	__u8 fw[] = {
		0x2A, 0x04, 0x71, 0x02, 
		0xEB, 0x00, 0x00, 0x02, 
		0x28, 0x00, 0x90, 0x01, 
		0x76, 0x02, 0x0C, 0x02, 
		0x00, 0x00, 0x00, 0x00, 
		0x20, 0xA1, 0x02, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
		0x00, 0x00, 0x00, 0x00, 
	};
/*	__u8 fw[] = {
		0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};*/
	memset(c6412->mem, 0, 0x40000);
	memcpy(c6412->mem, fw, sizeof(fw));

//	longtime_t start = longtime_now();
//	printf("3- %d\n", longtime_now() - start);
//	printf("4- %d\n", longtime_now() - start);

	int i;
	for(i=0; i<0x2000; i++) {
		__u8 a;
		a = readb(c6412->mem + i);

		if (!(i % 0x20))
			printf("\n%04x: ", i);

		printf("%02x", a);
	}
	printf("\n");

	printf("HDCR: 0x%08x\n", reg_readl(c6412, R_HDCR));
	printf("PCIIEN: 0x%08x\n", reg_readl(c6412, R_PCIIEN));

	printf("0x00002000 = 0x%08x\n", readl(c6412->mem + 0x00002000));
	printf("0x00003000 = 0x%08x\n", readl(c6412->mem + 0x00003000));
	writel(c6412->mem + 0x00002000, 0xdeadbeef);
	writel(c6412->mem + 0x00003000, 0xdeadbeef);

	//reg_writel(c6412, R_PCIIEN, 0);

	reg_writel(c6412, R_HDCR, 0);
	reg_writel(c6412, R_HDCR, R_HDCR_DSPINT);
	//reg_writel(c6412, R_HDCR, 0);

//	reg_writel(c6412, R_PCIIS, 0x8);

	usleep(200000);

////	reg_writel(c6412, R_CCFG, R_CCFG_0KC | R_CCFG_IP | R_CCFG_ID);
//	reg_writel(c6412, R_L1DWIBAR, 0x00000000);
//	reg_writel(c6412, R_L1DWIWC, 0x00002000);
//	printf("WC = 0x%08x\n", reg_readl(c6412, R_L1DWIWC));
//	usleep(100000);
//	printf("WC = 0x%08x\n", reg_readl(c6412, R_L1DWIWC));

	printf("0x00002000 = 0x%08x\n", readl(c6412->mem + 0x00002000));
	printf("0x00003000 = 0x%08x\n", readl(c6412->mem + 0x00003000));
}

int main(int argc, char *argv[])
{
	int c;
	int optidx;
	struct c6412 c6412;
	enum access_mode access_mode;

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

		if (c == 'b' || !strcmp(opt->name, "byte"))
			access_mode = ACCESS_BYTE;
		else if (c == 'w' || !strcmp(opt->name, "word"))
			access_mode = ACCESS_WORD;
		else if (c == 'd' || !strcmp(opt->name, "dword"))
			access_mode = ACCESS_DWORD;
		else {
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
			"Usage: %s <regs_base> <mem_base>\n",
			argv[0]);
		return 1;
	}

	if (argc < optind + 1) {
		fprintf(stderr, "Base address not specified\n");
		return 1;
	}

	if (sscanf(argv[optind], "%lx", &c6412.regs_base) < 1) {
		fprintf(stderr, "Invalid base address\n");
		return 1;
	}
	printf("Registers base address = 0x%08lx\n", c6412.regs_base);

	if (sscanf(argv[optind + 1], "%lx", &c6412.mem_base) < 1) {
		fprintf(stderr, "Invalid base address\n");
		return 1;
	}
	printf("Memory base address = 0x%08lx\n", c6412.mem_base);

	if (argc < optind + 1) {
		fprintf(stderr, "Command not specified\n");
		return 1;
	}

#if 0
	if (!strchr(argv[optind + 1], '=')) {
		unsigned long reg;
		unsigned long value;

		sscanf(argv[optind + 1], "%lx=%lx", &reg, &value);

		write_memory(base, access_mode, reg, value);

	} else {
		unsigned long reg;

		sscanf(argv[optind + 1], "%lx", &reg);

		read_memory(base, access_mode, reg);
	}
#endif

	if (map_memory(&c6412))
		return 1;

	print_info(&c6412);
	check_gpio(&c6412);
//	check_eeprom(&c6412);
	upload_firmware(&c6412);
//	check_emif(&c6412);

	return 0;
}
