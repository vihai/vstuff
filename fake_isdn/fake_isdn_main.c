/*
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <vihai_isdn.h>
#include <lapd_user.h>
#include <lapd.h>

#include "fake_isdn.h"

/******************************************
 * net_device interface functions
 ******************************************/

static int hfc_open(struct net_device *netdev)
{
	struct fake_card *card = netdev->priv;

	spin_lock(&card->lock);
	chan->status = open_framed;
	spin_unlock(&card->lock);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"chan %s opened.\n",
		netdev->name);

	return 0;
}

static int hfc_close(struct net_device *netdev)
{
	struct fake_card *card = netdev->priv;

	spin_lock_irqsave(&card->lock);

	if (chan->status != open_framed) {
		spin_unlock(&card->lock);
		return -EINVAL;
	}

	chan->status = free;
	spin_unlock(&card->lock);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"chan %s closed.\n",
		netdev->name);

	return 0;
}

static int hfc_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct hfc_chan *chan = netdev->priv;
	struct fake_card *card = chan->card;

	netdev->trans_start = jiffies;

	hfc_fifo_put_frame(&chan->tx, skb->data, skb->len);

	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *hfc_get_stats(struct net_device *netdev)
{
	struct hfc_chan *chan = netdev->priv;

	return &chan->net_device_stats;
}

static void hfc_set_multicast_list(struct net_device *netdev)
{
	struct fake_chan *chan = netdev->priv;
	struct hfc_port *port = chan->port;
	struct fake_card *card = port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

        if(netdev->flags & IFF_PROMISC && !port->echo_enabled) {
		if (port->nt_mode) {
			printk(KERN_INFO hfc_DRIVER_PREFIX
				"card %d: "
				"port %d: "
				"is in NT mode. Promiscuity is useless\n",
				card->id,
				port->id);

			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		// Only RX FIFO is needed for E channel
		hfc_fifo_select(card, port->chans[E].rx.fifo_id);

		hfc_fifo_reset(card);

		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_SUBCH_CFG,
			hfc_A_SUBCH_CFG_V_BIT_CNT_2);

		hfc_outb(card, hfc_A_IRQ_MSK,
			hfc_A_IRQ_MSK_V_IRQ);

		port->echo_enabled = TRUE;

		printk(KERN_INFO hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"entered in promiscuous mode\n",
			card->id,
			port->id);
        } else if(!(netdev->flags & IFF_PROMISC) && port->echo_enabled) {
		if (!port->echo_enabled) {
			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		hfc_fifo_select(card, port->chans[E].rx.fifo_id);

		hfc_outb(card, hfc_A_CON_HDLC,
			hfc_A_CON_HDCL_V_HDLC_TRP_HDLC|
			hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_DISABLED|
			hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

		hfc_outb(card, hfc_A_IRQ_MSK,
			0);

		port->echo_enabled = FALSE;

		printk(KERN_INFO hfc_DRIVER_PREFIX
			"card %d: "
			"chan %d: "
			"left promiscuous mode.\n",
			card->id,
			port->id);
	}

	spin_unlock_irqrestore(&card->lock, flags);

//	hfc_update_fifo_state(card);
}

/******************************************
 * Interrupt Handler
 ******************************************/

static inline void hfc_handle_timer_interrupt(struct fake_card *card);
static inline void hfc_handle_state_interrupt(struct hfc_port *port);
static void hfc_frame_arrived(struct hfc_chan_simplex *chan);
static inline void hfc_handle_voice(struct fake_card *card);
static inline void hfc_handle_port_fifo_interrupt(struct hfc_port *port);

static irqreturn_t hfc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct fake_card *card = dev_id;

	u8 status = hfc_inb(card, hfc_R_STATUS);
	u8 irq_sci = hfc_inb(card, hfc_R_SCI);

	if (unlikely(
		!(status & (hfc_R_STATUS_V_MISC_IRQSTA |
		            hfc_R_STATUS_V_FR_IRQSTA)) &&
	    !irq_sci)) {
		// probably we are sharing the irq
		return IRQ_NONE;
	}

	if (status & hfc_R_STATUS_V_MISC_IRQSTA) {
		u8 irq_misc = hfc_inb(card, hfc_R_IRQ_MISC);

		if (irq_misc & hfc_R_IRQ_MISC_V_TI_IRQ) {
			hfc_handle_timer_interrupt(card);
		}
	}

	if (status & hfc_R_STATUS_V_FR_IRQSTA) {
		u8 irq_oview = hfc_inb(card, hfc_R_IRQ_OVIEW);

		int i;
		for (i=0; i<card->num_ports; i++) {
			if (irq_oview & (1 << card->ports[i].id)) {
				hfc_handle_port_fifo_interrupt(&card->ports[i]);
			}
		}
	}

	if (irq_sci) {
		int i;
		for (i=0; i<card->num_ports; i++) {
			if (irq_sci & (1 << card->ports[i].id)) {
				hfc_handle_state_interrupt(&card->ports[i]);
			}
		}
	}

	return IRQ_HANDLED;
}

static inline void hfc_handle_fifo_rx_interrupt(struct hfc_chan_simplex *chan)
{
	hfc_frame_arrived(chan);
}

static inline void hfc_handle_fifo_tx_interrupt(struct hfc_chan_simplex *chan)
{
	printk(KERN_DEBUG hfc_DRIVER_PREFIX
		"Received TX interrupt on chan %s\n",chan->chan->name);

	// dev_queue_start if needed
}

static inline void hfc_handle_port_fifo_interrupt(struct hfc_port *port)
{
	struct fake_card *card = port->card;

	u8 fifo_irq = hfc_inb(card,
		hfc_R_IRQ_FIFO_BL0 + port->id);

	if (fifo_irq & (1 << 0))
		hfc_handle_fifo_tx_interrupt(&port->chans[B1].tx);
	if (fifo_irq & (1 << 1))
		hfc_handle_fifo_rx_interrupt(&port->chans[B1].rx);
	if (fifo_irq & (1 << 2))
		hfc_handle_fifo_tx_interrupt(&port->chans[B2].tx);
	if (fifo_irq & (1 << 3))
		hfc_handle_fifo_rx_interrupt(&port->chans[B2].rx);
	if (fifo_irq & (1 << 4))
		hfc_handle_fifo_tx_interrupt(&port->chans[D].tx);
	if (fifo_irq & (1 << 5))
		hfc_handle_fifo_rx_interrupt(&port->chans[D].rx);
//	if (fifo_irq & (1 << 6))
//		hfc_handle_fifo_tx_interrupt(&port->chans[E].tx);
	if (fifo_irq & (1 << 7))
		hfc_handle_fifo_rx_interrupt(&port->chans[E].rx);
}

static inline void hfc_handle_timer_interrupt(struct fake_card *card)
{
	if(card->ignore_first_timer_interrupt) {
		card->ignore_first_timer_interrupt = FALSE;
		return;
	}

/*	if ((port->nt_mode && card->l1_state == 3) ||
		(!card->nt_mode && card->l1_state == 7)) {

		card->regs.ctmt &= ~hfc_CTMT_TIMER_MASK;
		hfc_outb(card, hfc_CTMT, card->regs.ctmt);

		hfc_resume_fifo(card);
	}*/
}

static inline void hfc_handle_state_interrupt(struct hfc_port *port)
{
	struct fake_card *card = port->card;

	hfc_select_port(card, port->id);

	u8 new_state = hfc_A_ST_RD_STA_V_ST_STA(hfc_inb(card, hfc_A_ST_RD_STA));

#ifdef DEBUG
	if (debug_level >= 1) {
		printk(KERN_DEBUG hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"layer 1 state = %c%d\n",
			card->id,
			port->id,
			port->nt_mode?'G':'F',
			new_state);
	}
#endif

	if (port->nt_mode) {
		// NT mode

		if (new_state == 2) {
			// Allows transition from G2 to G3
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
				hfc_A_ST_WR_STA_V_SET_G2_G3);
		} else if (new_state == 3) {
			// fix to G3 state (see specs) ? Why? TODO FIXME
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_SET_STA(3)|
				hfc_A_ST_WR_STA_V_ST_LD_STA);
		}

		if (new_state == 3 && port->l1_state != 3) {
//			hfc_resume_fifo(card);
		}

		if (new_state != 3 && port->l1_state == 3) {
//			hfc_suspend_fifo(card);
		}
	} else {
		if (new_state == 3) {
			if (force_l1_up) {
//				hfc_outb(card, hfc_STATES, hfc_STATES_DO_ACTION |
//						hfc_STATES_ACTIVATE);
			}
		}

		if (new_state == 7 && port->l1_state != 7) {
			// TE is now active, schedule FIFO activation after
			// some time, otherwise the first frames are lost

//			card->regs.ctmt |= hfc_CTMT_TIMER_50 | hfc_CTMT_TIMER_CLEAR;
//			hfc_outb(card, hfc_CTMT, card->regs.ctmt);

			// Activating the timer firest an interrupt immediately, we
			// obviously need to ignore it
		}

		if (new_state != 7 && port->l1_state == 7) {
			// TE has become inactive, disable FIFO
//			hfc_suspend_fifo(card);
		}
	}

	port->l1_state = new_state;
}

static inline void hfc_handle_processing_interrupt(struct fake_card *card)
{
/*	int available_bytes=0;

	// Synchronize with the first enabled channel
	if(card->regs.fifo_en & hfc_FIFOEN_B1RX)
		available_bytes = hfc_fifo_used_rx(&card->chans[B1].rx);
	if(card->regs.fifo_en & hfc_FIFOEN_B2RX)
		available_bytes = hfc_fifo_used_rx(&card->chans[B2].rx);
	else
		available_bytes = -1;

	if ((available_bytes == -1 && card->ticks == 8) ||
		available_bytes >= CHUNKSIZE + hfc_RX_FIFO_PRELOAD) {
		card->ticks = 0;

		if (available_bytes > CHUNKSIZE + hfc_RX_FIFO_PRELOAD) {
			card->late_irqs++;

#if DEBUG
			if (debug_level >= 4) {
				printk(KERN_DEBUG hfc_DRIVER_PREFIX
					"card %d: "
					"late IRQ, %d bytes late\n",
					card->id,
					available_bytes -
						(CHUNKSIZE +
						 hfc_RX_FIFO_PRELOAD));
			}
#endif
		}

		hfc_handle_voice(card);
	}

	card->ticks++;
*/
}

static inline void hfc_handle_voice(struct fake_card *card)
{
/*	if (card->chans[B1].status != open_voice &&
		card->chans[B2].status != open_voice)
		return;
*/
// TODO
}

// This must be called from interrupt handler, no locking is made on FIFOs
static void hfc_frame_arrived(struct hfc_chan_simplex *chan)
{
	struct fake_chan *fdchan = chan->chan;
	struct hfc_port *port = fdchan->port;
	struct fake_card *card = port->card;

	int antiloop = 16; // Copy no more than 16 frames

	WARN_ON(!in_interrupt());

	while(--antiloop) {
		// FIFO selection has to be done for each frame to clear
		// internal buffer (see specs 4.4.4).
		hfc_fifo_select(card, chan->fifo_id);

		hfc_fifo_refresh_fz_cache(chan);

		if (!hfc_fifo_has_frames(chan)) break;

		int frame_size = hfc_fifo_get_frame_size(chan);

		if (frame_size < 3) {
#ifdef DEBUG
			if (debug_level>=2)
				printk(KERN_DEBUG hfc_DRIVER_PREFIX
					"card %d: "
					"port %d: "
					"chan %s: "
					"invalid frame received, just %d bytes\n",
					card->id,
					port->id,
					fdchan->name,
					frame_size);
#endif

			hfc_fifo_drop_frame(chan);

			fdchan->net_device_stats.rx_dropped++;

			continue;
		} else if(frame_size == 3) {
#ifdef DEBUG
			if (debug_level>=2)
				printk(KERN_DEBUG hfc_DRIVER_PREFIX
					"card %d: "
					"port %d: "
					"chan %s: "
					"empty frame received\n",
					card->id,
					port->id,
					fdchan->name);
#endif

			hfc_fifo_drop_frame(chan);

			fdchan->net_device_stats.rx_dropped++;

			continue;
		}

		struct sk_buff *skb =
			dev_alloc_skb(frame_size - 3);

		if (!skb) {
			printk(KERN_ERR hfc_DRIVER_PREFIX
				"card %d: "
				"port %d: "
				"chan %s: "
				"cannot allocate skb: frame dropped\n",
				card->id,
				port->id,
				fdchan->name);

			hfc_fifo_drop_frame(chan);

			fdchan->net_device_stats.rx_dropped++;

			continue;
		}

		// Oh... this is the echo channel... redirect to D
		// channel's netdev
		if (fdchan->id == E) {
			skb->protocol = htons(port->chans[D].protocol);
			skb->dev = port->chans[D].netdev;
			skb->pkt_type = PACKET_OTHERHOST;
		} else {
			skb->protocol = htons(fdchan->protocol);
			skb->dev = fdchan->netdev;
			skb->pkt_type = PACKET_HOST;
		}

		skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (hfc_fifo_get_frame(chan,
			skb_put(skb, frame_size - 3),
			frame_size - 3) == -1) {
			dev_kfree_skb(skb);
			continue;
		}

		fdchan->net_device_stats.rx_packets++;
		fdchan->net_device_stats.rx_bytes += frame_size - 1;

		netif_rx(skb);
	}

	if (!antiloop) 
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"port %d: "
			"chan %s: "
			"Infinite loop detected\n",
			card->id,
			port->id,
			chan->chan->name);
}

/******************************************
 * Module initialization and cleanup
 ******************************************/

static void hfc_setup_lapd(struct fake_chan *chan)
{
	chan->netdev->priv = chan;
	chan->netdev->open = hfc_open;
	chan->netdev->stop = hfc_close;
	chan->netdev->hard_start_xmit = hfc_xmit_frame;
	chan->netdev->get_stats = hfc_get_stats;
	chan->netdev->set_multicast_list = hfc_set_multicast_list;
	chan->netdev->features = NETIF_F_NO_CSUM;

	memset(chan->netdev->dev_addr, 0x00, sizeof(chan->netdev->dev_addr));

	SET_MODULE_OWNER(chan->netdev);
}

static int __devinit hfc_probe(struct pci_dev *pci_dev,
	const struct pci_device_id *ent)
{
	static int card_ids_counter = 0;
	int err;

	struct fake_card *card = NULL;
	card = kmalloc(sizeof(struct fake_card), GFP_KERNEL);
	if (!card) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"unable to kmalloc!\n",
			card->id);
		err = -ENOMEM;
		goto err_alloc_hfccard;
	}

	memset(card, 0x00, sizeof(struct fake_card));
	spin_lock_init(&card->lock);
	card->id = card_ids_counter;
	card->pcidev = pci_dev;
	card->chip_type = ent->driver_data;

	if (card->chip_type == HFC_CHIPTYPE_4S) card->num_ports = 4;
	else if (card->chip_type == HFC_CHIPTYPE_8S) card->num_ports = 8;
	else BUG();

	pci_set_drvdata(pci_dev, card);

	if ((err = pci_enable_device(pci_dev))) {
		goto err_pci_enable_device;
	}

	pci_write_config_word(pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	if((err = pci_request_regions(pci_dev, hfc_DRIVER_NAME))) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"cannot request I/O memory region\n",
			card->id);
		goto err_pci_request_regions;
	}

	pci_set_master(pci_dev);

	if (!pci_dev->irq) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"no irq!\n",
			card->id);
		err = -ENODEV;
		goto err_noirq;
	}

	card->io_bus_mem = pci_resource_start(pci_dev,1);
	if (!card->io_bus_mem) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"no iomem!\n",
			card->id);
		err = -ENODEV;
		goto err_noiobase;
	}

	if(!(card->io_mem = ioremap(card->io_bus_mem, hfc_PCI_MEM_SIZE))) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"cannot ioremap I/O memory\n",
			card->id);
		err = -ENODEV;
		goto err_ioremap;
	}

	if ((err = request_irq(card->pcidev->irq, &hfc_interrupt,
		SA_SHIRQ, hfc_DRIVER_NAME, card))) {
		printk(KERN_CRIT hfc_DRIVER_PREFIX
			"card %d: "
			"unable to register irq\n",
			card->id);
		goto err_request_irq;
	}

	int i;
	for (i=0; i<card->num_ports; i++) {
		card->ports[i].card = card;
		card->ports[i].nt_mode = FALSE;
		card->ports[i].id = i;

		struct fake_chan *chan;
//---------------------------------- D

		chan = &card->ports[i].chans[D];

		chan->port = &card->ports[i];
		chan->name = "D";
		chan->status = free;
		chan->id = D;
		chan->protocol = ETH_P_LAPD;
//		spin_lock_init(&chan->lock);

		chan->rx.chan      = chan;
		chan->rx.z_min     = 0x0080;
		chan->rx.z_max     = 0x01FF;
		chan->rx.f_min     = 0x00;
		chan->rx.f_max     = 0x0F;
		chan->rx.fifo_size = chan->rx.z_max - chan->rx.z_min + 1;
		chan->rx.f_num     = chan->rx.f_max - chan->rx.f_min + 1;
		chan->rx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + D_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_RX;

		chan->tx.chan      = chan;
		chan->tx.z_min     = 0x0080;
		chan->tx.z_max     = 0x01FF;
		chan->tx.f_min     = 0x00;
		chan->tx.f_max     = 0x0F;
		chan->tx.fifo_size = chan->tx.z_max - chan->tx.z_min + 1;
		chan->tx.f_num     = chan->tx.f_max - chan->tx.f_min + 1;
		chan->tx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + D_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_TX;

		chan->netdev = alloc_netdev(0, "isdn%dd", setup_lapd);
		if(!chan->netdev) {
			printk(KERN_ERR hfc_DRIVER_PREFIX
				"net_device alloc failed, abort.\n");
			err = -ENOMEM;
			goto err_alloc_netdev_d;
		}

		hfc_setup_lapd(chan);

		chan->netdev->irq = card->pcidev->irq;
		chan->netdev->base_addr = card->io_bus_mem;

		if((err = register_netdev(chan->netdev))) {
			printk(KERN_INFO hfc_DRIVER_PREFIX
				"card %d: "
				"Cannot register net device %s, aborting.\n",
				card->id,
				chan->netdev->name);
			goto err_register_netdev_d;
		}

//---------------------------------- B1
		chan = &card->ports[i].chans[B1];

		chan->port = &card->ports[i];
		chan->name = "B1";
		chan->status = free;
		chan->id = B1;
		chan->protocol = 0;
//		spin_lock_init(&chan->lock);

		chan->rx.chan      = chan;
		chan->rx.z_min     = 0x0080;
		chan->rx.z_max     = 0x01FF;
		chan->rx.f_min     = 0x00;
		chan->rx.f_max     = 0x0F;
		chan->rx.fifo_size = chan->rx.z_max - chan->rx.z_min + 1;
		chan->rx.f_num     = chan->rx.f_max - chan->rx.f_min + 1;
		chan->rx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + B1_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_RX;

		chan->tx.chan      = chan;
		chan->tx.z_min     = 0x0080;
		chan->tx.z_max     = 0x01FF;
		chan->tx.f_min     = 0x00;
		chan->tx.f_max     = 0x0F;
		chan->tx.fifo_size = chan->tx.z_max - chan->tx.z_min + 1;
		chan->tx.f_num     = chan->tx.f_max - chan->tx.f_min + 1;
		chan->tx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + B1_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_TX;

//---------------------------------- B2
		chan = &card->ports[i].chans[B2];

		chan->port = &card->ports[i];
		chan->name = "B2";
		chan->status = free;
		chan->id = B2;
		chan->protocol = 0;
//		spin_lock_init(&chan->lock);

		chan->rx.chan      = chan;
		chan->rx.z_min     = 0x0080;
		chan->rx.z_max     = 0x01FF;
		chan->rx.f_min     = 0x00;
		chan->rx.f_max     = 0x0F;
		chan->rx.fifo_size = chan->rx.z_max - chan->rx.z_min + 1;
		chan->rx.f_num     = chan->rx.f_max - chan->rx.f_min + 1;
		chan->rx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + B2_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_RX;

		chan->tx.chan      = chan;
		chan->tx.z_min     = 0x0080;
		chan->tx.z_max     = 0x01FF;
		chan->tx.f_min     = 0x00;
		chan->tx.f_max     = 0x0F;
		chan->tx.fifo_size = chan->tx.z_max - chan->tx.z_min + 1;
		chan->tx.f_num     = chan->tx.f_max - chan->tx.f_min + 1;
		chan->tx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + B2_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_TX;

//---------------------------------- E
		chan = &card->ports[i].chans[E];

		chan->port = &card->ports[i];
		chan->name = "E";
		chan->status = free;
		chan->id = E;
		chan->protocol = 0;
//		spin_lock_init(&chan->lock);

		chan->rx.chan      = chan;
		chan->rx.z_min     = 0x0080;
		chan->rx.z_max     = 0x01FF;
		chan->rx.f_min     = 0x00;
		chan->rx.f_max     = 0x0F;
		chan->rx.fifo_size = chan->rx.z_max - chan->rx.z_min + 1;
		chan->rx.f_num     = chan->rx.f_max - chan->rx.f_min + 1;
		chan->rx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + E_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_RX;

		chan->tx.chan      = chan;
		chan->tx.z_min     = 0x0080;
		chan->tx.z_max     = 0x01FF;
		chan->tx.f_min     = 0x00;
		chan->tx.f_max     = 0x0F;
		chan->tx.fifo_size = chan->tx.z_max - chan->tx.z_min + 1;
		chan->tx.f_num     = chan->tx.f_max - chan->tx.f_min + 1;
		chan->tx.fifo_id   =
			hfc_R_FIFO_V_FIFO_NUM((i * 4) + E_FIFO_OFF)|
			hfc_R_FIFO_V_FIFO_DIR_TX;
	}


// -------------------------------------------------------

	snprintf(card->proc_dir_name,
			sizeof(card->proc_dir_name),
			"%d", card->id);
	card->proc_dir = proc_mkdir(card->proc_dir_name, hfc_proc_hfc_dir);
	card->proc_dir->owner = THIS_MODULE;

	card->proc_info = create_proc_read_entry(
			"info", 0444, card->proc_dir,
			hfc_proc_read_info, card);
	card->proc_info->owner = THIS_MODULE;

	card->proc_fifos = create_proc_read_entry(
			"fifos", 0400, card->proc_dir,
			hfc_proc_read_fifos, card);
	card->proc_fifos->owner = THIS_MODULE;

	hfc_reset_card(card);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d configured at mem %#lx (0x%p) IRQ %u\n",
		card->id,
		card->io_bus_mem,
		card->io_mem,
		card->pcidev->irq); 

	card_ids_counter++;

	return 0;

	//FIXME  (all ports)
//	unregister_netdev(card->chans[D].netdev);
err_register_netdev_d:
//	free_netdev(card->chans[D].netdev);
err_alloc_netdev_d:
	free_irq(pci_dev->irq, card);
err_request_irq:
	iounmap(card->io_mem);
err_ioremap:
err_noiobase:
err_noirq:
	pci_release_regions(pci_dev);
err_pci_request_regions:
err_pci_enable_device:
	kfree(card);
err_alloc_hfccard:
	return err;
}

static void __devexit hfc_remove(struct pci_dev *pci_dev)
{
	struct fake_card *card = pci_get_drvdata(pci_dev);

//	unregister_netdev(card->chans[B2].netdev);
//	unregister_netdev(card->chans[B1].netdev);

	int i;
	for (i=0; i<card->num_ports; i++) {
		unregister_netdev(card->ports[i].chans[D].netdev);
	}

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d: "
		"shutting down card at %p.\n",
		card->id,
		card->io_mem);

	unsigned long flags;
	spin_lock_irqsave(&card->lock,flags);

	// softreset clears all pending interrupts
	hfc_softreset(card);

	spin_unlock_irqrestore(&card->lock,flags);

	// There should be no interrupt from here on

	pci_write_config_word(pci_dev, PCI_COMMAND, 0);

	remove_proc_entry("fifos", card->proc_dir);
	remove_proc_entry("info", card->proc_dir);
	remove_proc_entry(card->proc_dir_name, hfc_proc_hfc_dir);

	free_irq(pci_dev->irq, card);

	iounmap(card->io_mem);

	pci_release_regions(pci_dev);

	pci_disable_device(pci_dev);

	for (i=0; i<card->num_ports; i++) {
		free_netdev(card->ports[i].chans[D].netdev);
	}

	kfree(card);
}

/******************************************
 * Module stuff
 ******************************************/

static int __init hfc_init_module(void)
{
	int ret;

	printk(KERN_INFO hfc_DRIVER_PREFIX
		hfc_DRIVER_DESCR " loading\n");

	hfc_proc_hfc_dir = proc_mkdir(hfc_DRIVER_NAME, proc_root_driver);

	ret = pci_module_init(&hfc_driver);
	return ret;
}

module_init(hfc_init_module);

static void __exit hfc_module_exit(void)
{
	pci_unregister_driver(&hfc_driver);

	remove_proc_entry(hfc_DRIVER_NAME, proc_root_driver);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		hfc_DRIVER_DESCR " unloaded\n");
}

module_exit(hfc_module_exit);

#endif

MODULE_DESCRIPTION(hfc_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#ifdef LINUX26

module_param(force_l1_up, int, 0444);
#ifdef DEBUG
module_param(debug_level, int, 0444);
#endif

#else

MODULE_PARM(force_l1_up,"i");
#ifdef DEBUG
MODULE_PARM(debug_level,"i");
#endif

#endif // LINUX26

MODULE_PARM_DESC(nt_mode, "Comma-separated list of card IDs to configure in NT mode");
MODULE_PARM_DESC(force_l1_up, "Don't allow L1 to go down");

#ifdef DEBUG
MODULE_PARM_DESC(debug_level, "List of card IDs to configure in NT mode");
#endif


