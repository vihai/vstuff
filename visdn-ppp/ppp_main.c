/*
 * vISDN gateway between vISDN's crossconnector and Linux's ppp subsystem
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */


static int hfc_ppp_start_xmit(
	struct ppp_channel *ppp_chan,
	struct sk_buff *skb)
{
	return -EINVAL;
}

static int hfc_ppp_ioctl(
	struct ppp_channel *ppp_chan,
	unsigned int cmd,
	unsigned long arg)
{
	return -EINVAL;
}

static struct ppp_channel_ops hfc_ppp_ops =
{
	start_xmit: hfc_ppp_start_xmit,
	ioctl: hfc_ppp_ioctl,
};


static void visdn_frame_input_error(struct visdn_chan *chan, int code)
{
	if(0) { //PPP
		ppp_input_error(&chan->ppp_chan, code);
	}
}

