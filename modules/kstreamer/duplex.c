/*
 * Kstreamer kernel infrastructure core
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <kernel_config.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "duplex.h"

static struct attribute *ks_duplex_default_attrs[] =
{
	NULL,
};

void ks_duplex_init(
	struct ks_duplex *duplex,
	struct ks_duplex_ops *ops,
	const char *name,
	struct kobject *parent)
{
	BUG_ON(!duplex);
	BUG_ON(!ops);
	BUG_ON(!ops->owner);
	BUG_ON(!name);
	BUG_ON(!parent);

	memset(duplex, 0, sizeof(*duplex));

	kobject_init(&duplex->kobj);
	kobject_set_name(&duplex->kobj, "%s", name);
	kobj_set_kset_s(duplex, ks_duplexes_subsys);
	duplex->kobj.parent = parent;

	duplex->ops = ops;
}
EXPORT_SYMBOL(ks_duplex_init);

int ks_duplex_register(struct ks_duplex *duplex)
{
	int err;

	BUG_ON(!duplex);

	err = kobject_add(&duplex->kobj);
	if (err < 0)
		goto err_kobject_add;

	return 0;

	kobject_del(&duplex->kobj);
err_kobject_add:

	return err;
}
EXPORT_SYMBOL(ks_duplex_register);

void ks_duplex_unregister(
	struct ks_duplex *duplex)
{
	kobject_del(&duplex->kobj);
}
EXPORT_SYMBOL(ks_duplex_unregister);

#define to_ks_duplex_attr(_attr) \
	container_of(_attr, struct ks_duplex_attribute, attr)

static ssize_t ks_duplex_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct ks_duplex_attribute *ks_duplex_attr =
					to_ks_duplex_attr(attr);
	struct ks_duplex *ks_duplex = to_ks_duplex(kobj);
	ssize_t err;

	if (ks_duplex_attr->show)
		err = ks_duplex_attr->show(ks_duplex, ks_duplex_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t ks_duplex_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct ks_duplex_attribute *ks_duplex_attr =
					to_ks_duplex_attr(attr);
	struct ks_duplex *ks_duplex = to_ks_duplex(kobj);
	ssize_t err;

	if (ks_duplex_attr->store)
		err = ks_duplex_attr->store(ks_duplex, ks_duplex_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops ks_duplex_sysfs_ops = {
	.show   = ks_duplex_attr_show,
	.store  = ks_duplex_attr_store,
};

static void ks_duplex_release(struct kobject *kobj)
{
	struct ks_duplex *duplex = to_ks_duplex(kobj);

	ks_debug(3, "ks_duplex_release()\n");

	if (duplex->ops->release)
		duplex->ops->release(duplex);
	else {
		ks_msg(KERN_ERR, "Duplex '%s' does not have a"
			" release() function, it is broken and must be"
			" fixed.\n",
			duplex->kobj.name);
		WARN_ON(1);
	}
}

static struct kobj_type ks_duplex_ktype = {
	.release	= ks_duplex_release,
	.sysfs_ops	= &ks_duplex_sysfs_ops,
	.default_attrs	= ks_duplex_default_attrs,
};

decl_subsys_name(ks_duplexes, duplexes, &ks_duplex_ktype, NULL);

int ks_duplex_create_file(
	struct ks_duplex *duplex,
	struct ks_duplex_attribute *attr)
{
	int err = 0;

	if (ks_duplex_get(duplex)) {
		err = sysfs_create_file(&duplex->kobj, &attr->attr);
		ks_duplex_put(duplex);
	}

	return err;
}
EXPORT_SYMBOL(ks_duplex_create_file);

void ks_duplex_remove_file(
	struct ks_duplex *duplex,
	struct ks_duplex_attribute *attr)
{
	if (ks_duplex_get(duplex)) {
		sysfs_remove_file(&duplex->kobj, &attr->attr);
		ks_duplex_put(duplex);
	}
}
EXPORT_SYMBOL(ks_duplex_remove_file);

int ks_duplex_modinit(void)
{
	int err;

	ks_duplexes_subsys.kset.kobj.parent = &kstreamer_subsys.kset.kobj;

	err = subsystem_register(&ks_duplexes_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&ks_duplexes_subsys);
err_subsystem_register:

	return err;
}

void ks_duplex_modexit(void)
{
	subsystem_unregister(&ks_duplexes_subsys);
}
