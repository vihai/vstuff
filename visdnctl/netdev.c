/*
 * vISDN - Controlling program
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <ifaddrs.h>

#include <linux/sockios.h>

#include <linux/visdn/netdev.h>
#include <linux/visdn/lapd.h>

#include <list.h>

#include "visdnctl.h"
#include "netdev.h"

static int netdev_do_create(
	const char *devname,
	int protocol)
{
	int fd;
	fd = open(NETDEV_CONTROL, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Cannot open " NETDEV_CONTROL ": %s\n",
			strerror(errno));

		return 1;
	}

	struct vnd_create create;

	create.protocol = protocol;
	strncpy(create.devname, devname, sizeof(create.devname));

	if (ioctl(fd, VND_IOC_CREATE, &create) < 0) {
		fprintf(stderr, "ioctl(IOC_CREATE): %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	printf("D: %s\n", create.d_chan);
	printf("E: %s\n", create.e_chan);

	return 0;
}

static int netdev_do_destroy(
	const char *devname)
{
	int fd;
	fd = open(NETDEV_CONTROL, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Cannot open " NETDEV_CONTROL ": %s\n",
			strerror(errno));

		return 1;
	}

	struct vnd_create create;

	strncpy(create.devname, devname, sizeof(create.devname));

	if (ioctl(fd, VND_IOC_DESTROY, &create) < 0) {
		fprintf(stderr, "ioctl(IOC_DESTROY): %s\n",
			strerror(errno));

		return 1;
	}

	close(fd);

	return 0;
}

static int handle_netdev_create(int argc, char *argv[], int optind)
{
	if (argc <= optind + 2) {
		print_usage("Missing netdev's device name\n");
		return 1;
	}

	const char *devname = argv[optind + 2];

	if (argc <= optind + 3) {
		print_usage("Missing netdev's protocol\n");
		return 1;
	}

	const char *protocol_name = argv[optind + 3];

	int protocol = 0;
	if (!strcasecmp(protocol_name, "lapd"))
		protocol = ARPHRD_LAPD;
//	else if (!strcasecmp(protocol_name, "mtp2"))
//		protocol = ARPHRD_MTP2;
	else {
		print_usage("Unknown netdev's protocol '%s'\n",
			protocol_name);

		return 1;
	}

	return netdev_do_create(devname, protocol);
}

static int handle_netdev_destroy(int argc, char *argv[], int optind)
{
	if (argc <= optind + 2) {
		print_usage("Missing netdev's device name\n");
		return 1;
	}

	const char *devname = argv[optind + 2];

	return netdev_do_destroy(devname);
}

static int set_flags(const char *devname, int newflags, int mask)
{
	int fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		fprintf(stderr, "Cannot create socket: %s\n",
			strerror(errno));
		return 1;
	}

	struct ifreq ifr;
	strncpy(ifr.ifr_name, devname, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		close(fd);
		fprintf(stderr, "ioctl(SIOCGIFFLAGS): %s\n",
			strerror(errno));
	}

	if ((ifr.ifr_flags ^ newflags) & mask) {
		ifr.ifr_flags &= ~mask;
		ifr.ifr_flags |= mask & newflags;

		if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
			close(fd);
			fprintf(stderr, "ioctl(SIOCSIFFLAGS): %s\n",
				strerror(errno));
		}
	}

	close(fd);

	return 0;
}

static int do_set_type(const char *devname, int pri)
{
	if (pri)
		return set_flags(devname, IFF_PORTSEL, IFF_PORTSEL);
	else
		return set_flags(devname, 0, IFF_PORTSEL);
}

static int handle_netdev_set_type(
	int argc, char *argv[], int optind,
	const char *devname)
{
	if (argc <= optind + 4) {
		print_usage("Missing netdev's parameter value\n");
		return 1;
	}

	const char *value = argv[optind + 4];

	if (!strcasecmp(value, "bri"))
		return do_set_type(devname, 0);
	else if (!strcasecmp(value, "pri"))
		return do_set_type(devname, 1);
	else {
		print_usage("Unknown type '%s'\n",
			value);
		return 1;
	}
}

static int do_set_role(const char *devname, int nt_mode)
{
	if (nt_mode)
		return set_flags(devname, IFF_ALLMULTI, IFF_ALLMULTI);
	else
		return set_flags(devname, 0, IFF_ALLMULTI);
}

static int handle_netdev_set_role(
	int argc, char *argv[], int optind,
	const char *devname)
{
	if (argc <= optind + 4) {
		print_usage("Missing netdev's parameter value\n");
		return 1;
	}

	const char *value = argv[optind + 4];

	if (!strcasecmp(value, "nt"))
		return do_set_role(devname, 1);
	else if (!strcasecmp(value, "te"))
		return do_set_role(devname, 0);
	else {
		print_usage("Unknown role '%s'\n",
			value);
		return 1;
	}
}

static int do_set_mode(const char *devname, int p2p)
{
	if (p2p)
		return set_flags(devname, IFF_NOARP, IFF_NOARP);
	else
		return set_flags(devname, 0, IFF_NOARP);
}

static int handle_netdev_set_mode(
	int argc, char *argv[], int optind,
	const char *devname)
{
	if (argc <= optind + 4) {
		print_usage("Missing netdev's parameter value\n");
		return 1;
	}

	const char *value = argv[optind + 4];

	if (!strcasecmp(value, "p2p"))
		return do_set_mode(devname, 1);
	else if (!strcasecmp(value, "p2mp"))
		return do_set_mode(devname, 0);
	else {
		print_usage("Unknown mode '%s'\n",
			value);
		return 1;
	}
}

static int handle_netdev_set(int argc, char *argv[], int optind)
{
	if (argc <= optind + 2) {
		print_usage("Missing netdev's device name\n");
		return 1;
	}

	const char *devname = argv[optind + 2];

	if (argc <= optind + 3) {
		print_usage("Missing netdev's parameter name\n");
		return 1;
	}

	const char *parameter = argv[optind + 3];

	if (!strcasecmp(parameter, "up")) {
		return set_flags(devname, IFF_UP, IFF_UP);
	} else if (!strcasecmp(parameter, "down")) {
		return set_flags(devname, 0, IFF_UP);
	} else if (!strcasecmp(parameter, "type")) {
		return handle_netdev_set_type(argc, argv, optind, devname);
	} else if (!strcasecmp(parameter, "role")) {
		return handle_netdev_set_role(argc, argv, optind, devname);
	} else if (!strcasecmp(parameter, "mode")) {
		return handle_netdev_set_mode(argc, argv, optind, devname);
	} else {
		print_usage("Unknown parameter '%s'\n",
			parameter);
		return 1;
	}
}

static int show_any()
{
	int fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		print_usage("Cannot create socket: %s\n",
			strerror(errno));
		return 1;
	}

	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		print_usage("Cannot getifaddrs: %s\n",
			strerror(errno));
		return 1;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifr;
		strncpy(ifr.ifr_name, ifaddr->ifa_name,
			sizeof(ifr.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
			close(fd);
			fprintf(stderr, "ioctl(SIOCSIFHWBROADCAST): %s\n",
				strerror(errno));
			break;
		}

		if (ifr.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		printf("%s: %s %s %s\n",
			ifr.ifr_name,
			(ifaddr->ifa_flags & IFF_UP) ? "UP" : "DOWN",
			(ifaddr->ifa_flags & IFF_ALLMULTI) ? "NT" : "TE",
			(ifaddr->ifa_flags & IFF_NOARP) ? "PtP" : "PtMP");
	}

	close(fd);

	return 0;
}

static int handle_netdev_show(int argc, char *argv[], int optind)
{
	if (argc <= optind + 2) {
		return show_any();
	}

	const char *devname = argv[optind + 2];

	int fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		print_usage("Cannot create socket: %s\n",
			strerror(errno));
		return 1;
	}

	struct ifreq ifr;
	strncpy(ifr.ifr_name, devname, IFNAMSIZ);

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		close(fd);
		fprintf(stderr, "ioctl(SIOCGIFFLAGS): %s\n",
			strerror(errno));
	}

	printf("%s: %s %s\n",
		ifr.ifr_name,
		(ifr.ifr_flags & IFF_UP) ? "UP" : "DOWN",
		(ifr.ifr_flags & IFF_ALLMULTI) ? "NT" : "TE");

	return 0;
}

static int handle_netdev(int argc, char *argv[], int optind)
{
	const char *subcommand;

	if (argc <= optind + 1) {
		print_usage("Missing netdev's subcommand\n");
		return 1;
	}

	subcommand = argv[optind + 1];

	if (!strcmp(subcommand, "create"))
		return handle_netdev_create(argc, argv, optind);
	else if (!strcmp(subcommand, "destroy"))
		return handle_netdev_destroy(argc, argv, optind);
	else if (!strcmp(subcommand, "set"))
		return handle_netdev_set(argc, argv, optind);
	else if (!strcmp(subcommand, "show"))
		return handle_netdev_show(argc, argv, optind);
	else {
		print_usage("Unknown netdev's subcommand '%s'\n",
			subcommand);
		return 1;
	}
}

static void usage(int argc, char *argv[])
{
	fprintf(stderr,
		"  netdev <subcommand>\n"
		"\n"
		"    Controls netdev interfaces.\n"
		"\n"
		"    create\n"
		"    destroy\n"
		"    set\n"
		"    show\n");
}

struct module module_netdev =
{
	.cmd	= "netdev",
	.do_it	= handle_netdev,
	.usage	= usage,
};
