/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>

#include "visdn.h"
#include "chan.h"
#include "cxc.h"
#include "visdn_mod.h"

static void visdn_cxc_delete_rcu(struct rcu_head *head)
{
	struct visdn_cxc_connection *entry =
		container_of(head, struct visdn_cxc_connection, rcu);

	visdn_chan_put(entry->src);
	visdn_chan_put(entry->dst);

	kfree(entry);
}

int visdn_cxc_connect(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	int err;
	struct visdn_cxc_connection *cxc_entry;

	cxc_entry = kmalloc(sizeof(*cxc_entry), GFP_KERNEL);
	if (!cxc_entry) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	INIT_RCU_HEAD(&cxc_entry->rcu);

	spin_lock(&cxc->lock);

	cxc_entry->src = chan1;
	visdn_chan_get(chan1);
	cxc_entry->dst = chan2;
	visdn_chan_get(chan2);

	hlist_add_head_rcu(&cxc_entry->node,
			visdn_cxc_get_hash(cxc, chan1));

	spin_unlock(&cxc->lock);

	if (chan1->ops->connect_to) {
		err = chan1->ops->connect_to(chan1, chan2, 0);
		if (err < 0)
			goto err_connect;
	}

	if (err == VISDN_CONNECT_BRIDGED) {
		// FIXME
	}

	return 0;

err_connect:
err_kmalloc:

	return err;
}

void visdn_cxc_disconnect(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{

retry:
	rcu_read_lock();

	struct visdn_cxc_connection *entry;
	struct hlist_node *tpos;
	hlist_for_each_entry_rcu(entry, tpos,
			visdn_cxc_get_hash(cxc, chan),
			node) {

		if (entry->src == chan) {
			rcu_read_unlock();

			if (chan->ops->disconnect)
				chan->ops->disconnect(chan);

			hlist_del_rcu(&entry->node);

			call_rcu(&entry->rcu, visdn_cxc_delete_rcu);

			goto retry;
		}
	}

	rcu_read_unlock();
}

int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	BUG_ON(!cxc);
	BUG_ON(!chan);

	int err;

	if (!visdn_cxc_get(cxc)) {
		err = -EINVAL;
		goto err_cxc_get;
	}

	down_write(&cxc->subsys.rwsem);
	list_add_tail(&chan->cxc_channels_node, &cxc->channels);
	up_write(&cxc->subsys.rwsem);

	sysfs_create_link(&cxc->subsys.kset.kobj, &chan->kobj, chan->cxc_id);

	return 0;

	visdn_cxc_put(cxc);
err_cxc_get:

	return err;
}

void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	BUG_ON(!cxc);
	BUG_ON(!chan);

	sysfs_remove_link(&cxc->subsys.kset.kobj, chan->cxc_id);

	down_write(&cxc->subsys.rwsem);
	list_del_init(&chan->cxc_channels_node);
	up_write(&cxc->subsys.rwsem);

	visdn_cxc_put(cxc);
}

struct visdn_chan *visdn_cxc_search_chan(
	struct visdn_cxc *cxc,
	const char *chanid)
{
	down_read(&cxc->subsys.rwsem);
	if (!visdn_cxc_get(cxc))
		goto err_cxc_get;

	struct visdn_chan *chan, *pos, *found_chan = NULL;
	list_for_each_entry_safe(chan, pos,
			&cxc->channels, cxc_channels_node) {
		visdn_chan_get(chan);

		if (!strcmp(chan->cxc_id, chanid)) {
			found_chan = chan;
			break;
		}

		visdn_chan_put(chan);
	}

	up_read(&cxc->subsys.rwsem);

	return found_chan;

err_cxc_get:

	up_read(&cxc->subsys.rwsem);

	return NULL;
}
EXPORT_SYMBOL(visdn_cxc_search_chan);

struct visdn_chan *visdn_cxc_get_by_src(
	struct visdn_cxc *cxc,
	struct visdn_chan *chan)
{
	struct visdn_cxc_connection *cxc_entry;

	rcu_read_lock();

	struct hlist_node *tpos;
	hlist_for_each_entry_rcu(cxc_entry, tpos,
			visdn_cxc_get_hash(cxc, chan),
			node) {

		if (cxc_entry->src == chan) {
			visdn_chan_get(cxc_entry->dst);
			rcu_read_unlock();

			return cxc_entry->dst;
		}
	}

	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL(visdn_cxc_get_by_src);

#define to_visdn_cxc(kobj) \
	container_of(kobj, struct visdn_cxc, subsys.kset.kobj)

#define to_visdn_cxc_attr(_attr) \
	container_of(_attr, struct visdn_cxc_attribute, attr)

static ssize_t visdn_cxc_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_cxc_attribute *visdn_cxc_attr =
					to_visdn_cxc_attr(attr);
	struct visdn_cxc *visdn_cxc = to_visdn_cxc(kobj);
	ssize_t err;

	if (visdn_cxc_attr->show)
		err = visdn_cxc_attr->show(visdn_cxc, visdn_cxc_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_cxc_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_cxc_attribute *visdn_cxc_attr =
					to_visdn_cxc_attr(attr);
	struct visdn_cxc *visdn_cxc = to_visdn_cxc(kobj);
	ssize_t err;

	if (visdn_cxc_attr->store)
		err = visdn_cxc_attr->store(visdn_cxc, visdn_cxc_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_cxc_sysfs_ops = {
	.show   = visdn_cxc_attr_show,
	.store  = visdn_cxc_attr_store,
};

static void visdn_cxc_release(struct kobject *kobj)
{
	visdn_debug(3, "visdn_cxc_release()\n");

	struct visdn_cxc *cxc = to_visdn_cxc(kobj);

	if (cxc->ops->release)
		cxc->ops->release(cxc);
	else {
		visdn_msg(KERN_ERR, "vISDN cxc '%s' does not have a release()"
			" function, it is broken and must be fixed.\n",
			cxc->name);
		WARN_ON(1);
	}
}

static ssize_t visdn_cxc_connect_store(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr,
	const char *buf,
	size_t count)
{
	int err;
	char locbuf[100];

	char *locbuf_p = locbuf;

	strncpy(locbuf, buf, sizeof(locbuf));
	char *chan1_id = strsep(&locbuf_p, ",\n\r");
	if (!chan1_id)
		return -EINVAL;

	char *chan2_id = strsep(&locbuf_p, ",\n\r");
	if (!chan2_id)
		return -EINVAL;

	struct visdn_chan *chan1 = visdn_cxc_search_chan(cxc, chan1_id);
	if (!chan1) {
		err = -ENODEV;
		goto err_search_src;
	}

	struct visdn_chan *chan2 = visdn_cxc_search_chan(cxc, chan2_id);
	if (!chan2) {
		err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_connect_self;
	}

	err = visdn_connect(chan1, chan2, 0);
	if (err < 0)
		goto err_connect;

	// Release references returned by visdn_search_chan()
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

        return count;

	// visdn_disconnect
err_connect:
err_connect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return err;
}

VISDN_CXC_ATTR(connect, S_IWUSR, NULL, visdn_cxc_connect_store);

static struct attribute *visdn_cxc_default_attrs[] =
{
        &visdn_cxc_attr_connect.attr,
        NULL,
};

static struct kobj_type ktype_visdn_cxc = {
	.release	= visdn_cxc_release,
	.sysfs_ops	= &visdn_cxc_sysfs_ops,
	.default_attrs	= visdn_cxc_default_attrs,
};

void visdn_cxc_init(struct visdn_cxc *cxc)
{
	memset(cxc, 0, sizeof(*cxc));

	spin_lock_init(&cxc->lock);

	INIT_LIST_HEAD(&cxc->channels);

	int i;
 	for (i=0; i<ARRAY_SIZE(cxc->connections_hash); i++)
		INIT_HLIST_HEAD(&cxc->connections_hash[i]);
}
EXPORT_SYMBOL(visdn_cxc_init);

int visdn_cxc_register(struct visdn_cxc *cxc)
{
	int err;

	err = kobject_set_name(&cxc->subsys.kset.kobj, "%s", cxc->name);
	if (err < 0)
		goto err_kobject_set_name;

	subsys_set_kset(cxc, visdn_tdm_subsys);

	err = subsystem_register(&cxc->subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&cxc->subsys);
err_subsystem_register:
err_kobject_set_name:

	return err;
}
EXPORT_SYMBOL(visdn_cxc_register);

int visdn_cxc_create_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr)
{
	int err = 0;

	if (visdn_cxc_get(cxc)) {
		err = sysfs_create_file(&cxc->subsys.kset.kobj, &attr->attr);
		visdn_cxc_put(cxc);
	}

	return err;
}
EXPORT_SYMBOL(visdn_cxc_create_file);

void visdn_cxc_remove_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr)
{
	if (visdn_cxc_get(cxc)) {
		sysfs_remove_file(&cxc->subsys.kset.kobj, &attr->attr);
		visdn_cxc_put(cxc);
	}
}
EXPORT_SYMBOL(visdn_cxc_remove_file);

void visdn_cxc_unregister(struct visdn_cxc *cxc)
{
	subsystem_unregister(&cxc->subsys);
}
EXPORT_SYMBOL(visdn_cxc_unregister);

decl_subsys(visdn_tdm, &ktype_visdn_cxc, NULL);

int visdn_cxc_modinit()
{
	int err;

	err = subsystem_register(&visdn_tdm_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&visdn_tdm_subsys);
err_subsystem_register:

	return err;
}

void visdn_cxc_modexit()
{
	subsystem_unregister(&visdn_tdm_subsys);
}
