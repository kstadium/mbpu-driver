/*
 * This file is part of the Medium DMA IP Core driver for Linux
 *
 * Copyright (c) 2020-present,  Medium, Inc.
 * All rights reserved.
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#define pr_fmt(fmt)     KBUILD_MODNAME ":%s: " fmt, __func__

#include "mdlx_cdev.h"

static struct class *g_mdlx_class;

struct kmem_cache *cdev_cache;

enum cdev_type {
	CHAR_USER,
	CHAR_CTRL,
	CHAR_XVC,
	CHAR_EVENTS,
	CHAR_MDLX_H2C,
	CHAR_MDLX_C2H,
	CHAR_BYPASS_H2C,
	CHAR_BYPASS_C2H,
	CHAR_BYPASS,
};

static const char * const devnode_names[] = {
	MDLX_NODE_NAME "%d_user",
	MDLX_NODE_NAME "%d_control",
	MDLX_NODE_NAME "%d_xvc",
	MDLX_NODE_NAME "%d_events_%d",
	MDLX_NODE_NAME "%d_h2c_%d",
	MDLX_NODE_NAME "%d_c2h_%d",
	MDLX_NODE_NAME "%d_bypass_h2c_%d",
	MDLX_NODE_NAME "%d_bypass_c2h_%d",
	MDLX_NODE_NAME "%d_bypass",
};

enum mddev_flags_bits {
	XDF_CDEV_USER,
	XDF_CDEV_CTRL,
	XDF_CDEV_XVC,
	XDF_CDEV_EVENT,
	XDF_CDEV_SG,
	XDF_CDEV_BYPASS,
};

static inline void mddev_flag_set(struct mdlx_pci_dev *mddev,
				enum mddev_flags_bits fbit)
{
	mddev->flags |= 1 << fbit;
}

static inline void xcdev_flag_clear(struct mdlx_pci_dev *mddev,
				enum mddev_flags_bits fbit)
{
	mddev->flags &= ~(1 << fbit);
}

static inline int mddev_flag_test(struct mdlx_pci_dev *mddev,
				enum mddev_flags_bits fbit)
{
	return mddev->flags & (1 << fbit);
}

#ifdef __MDLX_SYSFS__
ssize_t mdlx_dev_instance_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct mdlx_pci_dev *mddev =
		(struct mdlx_pci_dev *)dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\t%d\n",
			mddev->major, mddev->mdev->idx);
}

static DEVICE_ATTR_RO(mdlx_dev_instance);
#endif

static int config_kobject(struct mdlx_cdev *xcdev, enum cdev_type type)
{
	int rv = -EINVAL;
	struct mdlx_dev *mdev = xcdev->mdev;
	struct mdlx_engine *engine = xcdev->engine;

	switch (type) {
	case CHAR_MDLX_H2C:
	case CHAR_MDLX_C2H:
	case CHAR_BYPASS_H2C:
	case CHAR_BYPASS_C2H:
		if (!engine) {
			pr_err("Invalid DMA engine\n");
			return rv;
		}
		rv = kobject_set_name(&xcdev->cdev.kobj, devnode_names[type],
			mdev->idx, engine->channel);
		break;
	case CHAR_BYPASS:
	case CHAR_USER:
	case CHAR_CTRL:
	case CHAR_XVC:
		rv = kobject_set_name(&xcdev->cdev.kobj, devnode_names[type],
			mdev->idx);
		break;
	case CHAR_EVENTS:
		rv = kobject_set_name(&xcdev->cdev.kobj, devnode_names[type],
			mdev->idx, xcdev->bar);
		break;
	default:
		pr_warn("%s: UNKNOWN type 0x%x.\n", __func__, type);
		break;
	}

	if (rv)
		pr_err("%s: type 0x%x, failed %d.\n", __func__, type, rv);
	return rv;
}

int xcdev_check(const char *fname, struct mdlx_cdev *xcdev, bool check_engine)
{
	struct mdlx_dev *mdev;

	if (!xcdev || xcdev->magic != MAGIC_CHAR) {
		pr_info("%s, xcdev 0x%p, magic 0x%lx.\n",
			fname, xcdev, xcdev ? xcdev->magic : 0xFFFFFFFF);
		return -EINVAL;
	}

	mdev = xcdev->mdev;
	if (!mdev || mdev->magic != MAGIC_DEVICE) {
		pr_info("%s, mdev 0x%p, magic 0x%lx.\n",
			fname, mdev, mdev ? mdev->magic : 0xFFFFFFFF);
		return -EINVAL;
	}

	if (check_engine) {
		struct mdlx_engine *engine = xcdev->engine;

		if (!engine || engine->magic != MAGIC_ENGINE) {
			pr_info("%s, engine 0x%p, magic 0x%lx.\n", fname,
				engine, engine ? engine->magic : 0xFFFFFFFF);
			return -EINVAL;
		}
	}

	return 0;
}

int char_open(struct inode *inode, struct file *file)
{
	struct mdlx_cdev *xcdev;

	/* pointer to containing structure of the character device inode */
	xcdev = container_of(inode->i_cdev, struct mdlx_cdev, cdev);
	if (xcdev->magic != MAGIC_CHAR) {
		pr_err("xcdev 0x%p inode 0x%lx magic mismatch 0x%lx\n",
			xcdev, inode->i_ino, xcdev->magic);
		return -EINVAL;
	}
	/* create a reference to our char device in the opened file */
	file->private_data = xcdev;

	return 0;
}

/*
 * Called when the device goes from used to unused.
 */
int char_close(struct inode *inode, struct file *file)
{
	struct mdlx_dev *mdev;
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;

	if (!xcdev) {
		pr_err("char device with inode 0x%lx xcdev NULL\n",
			inode->i_ino);
		return -EINVAL;
	}

	if (xcdev->magic != MAGIC_CHAR) {
		pr_err("xcdev 0x%p magic mismatch 0x%lx\n",
				xcdev, xcdev->magic);
		return -EINVAL;
	}

	/* fetch device specific data stored earlier during open */
	mdev = xcdev->mdev;
	if (!mdev) {
		pr_err("char device with inode 0x%lx mdev NULL\n",
			inode->i_ino);
		return -EINVAL;
	}

	if (mdev->magic != MAGIC_DEVICE) {
		pr_err("mdev 0x%p magic mismatch 0x%lx\n", mdev, mdev->magic);
		return -EINVAL;
	}

	return 0;
}

/* create_xcdev() -- create a character device interface to data or control bus
 *
 * If at least one SG DMA engine is specified, the character device interface
 * is coupled to the SG DMA file operations which operate on the data bus. If
 * no engines are specified, the interface is coupled with the control bus.
 */

static int create_sys_device(struct mdlx_cdev *xcdev, enum cdev_type type)
{
	struct mdlx_dev *mdev = xcdev->mdev;
	struct mdlx_engine *engine = xcdev->engine;
	int last_param;

	if (type == CHAR_EVENTS)
		last_param = xcdev->bar;
	else
		last_param = engine ? engine->channel : 0;

	xcdev->sys_device = device_create(g_mdlx_class, &mdev->pdev->dev,
		xcdev->cdevno, NULL, devnode_names[type], mdev->idx,
		last_param);

	if (!xcdev->sys_device) {
		pr_err("device_create(%s) failed\n", devnode_names[type]);
		return -1;
	}

	return 0;
}

static int destroy_xcdev(struct mdlx_cdev *cdev)
{
	if (!cdev) {
		pr_warn("cdev NULL.\n");
		return -EINVAL;
	}
	if (cdev->magic != MAGIC_CHAR) {
		pr_warn("cdev 0x%p magic mismatch 0x%lx\n", cdev, cdev->magic);
		return -EINVAL;
	}

	if (!cdev->mdev) {
		pr_err("mdev NULL\n");
		return -EINVAL;
	}

	if (!g_mdlx_class) {
		pr_err("g_mdlx_class NULL\n");
		return -EINVAL;
	}

	if (!cdev->sys_device) {
		pr_err("cdev sys_device NULL\n");
		return -EINVAL;
	}

	if (cdev->sys_device)
		device_destroy(g_mdlx_class, cdev->cdevno);

	cdev_del(&cdev->cdev);

	return 0;
}

static int create_xcdev(struct mdlx_pci_dev *mddev, struct mdlx_cdev *xcdev,
			int bar, struct mdlx_engine *engine,
			enum cdev_type type)
{
	int rv;
	int minor;
	struct mdlx_dev *mdev = mddev->mdev;
	dev_t dev;

	spin_lock_init(&xcdev->lock);
	/* new instance? */
	if (!mddev->major) {
		/* allocate a dynamically allocated char device node */
		int rv = alloc_chrdev_region(&dev, MDLX_MINOR_BASE,
					MDLX_MINOR_COUNT, MDLX_NODE_NAME);

		if (rv) {
			pr_err("unable to allocate cdev region %d.\n", rv);
			return rv;
		}
		mddev->major = MAJOR(dev);
	}

	/*
	 * do not register yet, create kobjects and name them,
	 */
	xcdev->magic = MAGIC_CHAR;
	xcdev->cdev.owner = THIS_MODULE;
	xcdev->mddev = mddev;
	xcdev->mdev = mdev;
	xcdev->engine = engine;
	xcdev->bar = bar;

	rv = config_kobject(xcdev, type);
	if (rv < 0)
		return rv;

	switch (type) {
	case CHAR_USER:
	case CHAR_CTRL:
		/* minor number is type index for non-SGDMA interfaces */
		minor = type;
		cdev_ctrl_init(xcdev);
		break;
	case CHAR_XVC:
		/* minor number is type index for non-SGDMA interfaces */
		minor = type;
		cdev_xvc_init(xcdev);
		break;
	case CHAR_MDLX_H2C:
		minor = 32 + engine->channel;
		cdev_sgdma_init(xcdev);
		break;
	case CHAR_MDLX_C2H:
		minor = 36 + engine->channel;
		cdev_sgdma_init(xcdev);
		break;
	case CHAR_EVENTS:
		minor = 10 + bar;
		cdev_event_init(xcdev);
		break;
	case CHAR_BYPASS_H2C:
		minor = 64 + engine->channel;
		cdev_bypass_init(xcdev);
		break;
	case CHAR_BYPASS_C2H:
		minor = 68 + engine->channel;
		cdev_bypass_init(xcdev);
		break;
	case CHAR_BYPASS:
		minor = 100;
		cdev_bypass_init(xcdev);
		break;
	default:
		pr_info("type 0x%x NOT supported.\n", type);
		return -EINVAL;
	}
	xcdev->cdevno = MKDEV(mddev->major, minor);

	/* bring character device live */
	rv = cdev_add(&xcdev->cdev, xcdev->cdevno, 1);
	if (rv < 0) {
		pr_err("cdev_add failed %d, type 0x%x.\n", rv, type);
		goto unregister_region;
	}

	dbg_init("xcdev 0x%p, %u:%u, %s, type 0x%x.\n",
		xcdev, mddev->major, minor, xcdev->cdev.kobj.name, type);

	/* create device on our class */
	if (g_mdlx_class) {
		rv = create_sys_device(xcdev, type);
		if (rv < 0)
			goto del_cdev;
	}

	return 0;

del_cdev:
	cdev_del(&xcdev->cdev);
unregister_region:
	unregister_chrdev_region(xcdev->cdevno, MDLX_MINOR_COUNT);
	return rv;
}

void mddev_destroy_interfaces(struct mdlx_pci_dev *mddev)
{
	int i = 0;
	int rv;
#ifdef __MDLX_SYSFS__
	device_remove_file(&mddev->pdev->dev, &dev_attr_mdlx_dev_instance);
#endif

	if (mddev_flag_test(mddev, XDF_CDEV_SG)) {
		/* iterate over channels */
		for (i = 0; i < mddev->h2c_channel_max; i++) {
			/* remove SG DMA character device */
			rv = destroy_xcdev(&mddev->sgdma_h2c_cdev[i]);
			if (rv < 0)
				pr_err("Failed to destroy h2c xcdev %d error :0x%x\n",
						i, rv);
		}
		for (i = 0; i < mddev->c2h_channel_max; i++) {
			rv = destroy_xcdev(&mddev->sgdma_c2h_cdev[i]);
			if (rv < 0)
				pr_err("Failed to destroy c2h xcdev %d error 0x%x\n",
						i, rv);
		}
	}

	if (mddev_flag_test(mddev, XDF_CDEV_EVENT)) {
		for (i = 0; i < mddev->user_max; i++) {
			rv = destroy_xcdev(&mddev->events_cdev[i]);
			if (rv < 0)
				pr_err("Failed to destroy cdev event %d error 0x%x\n",
					i, rv);
		}
	}

	/* remove control character device */
	if (mddev_flag_test(mddev, XDF_CDEV_CTRL)) {
		rv = destroy_xcdev(&mddev->ctrl_cdev);
		if (rv < 0)
			pr_err("Failed to destroy cdev ctrl event %d error 0x%x\n",
				i, rv);
	}

	/* remove user character device */
	if (mddev_flag_test(mddev, XDF_CDEV_USER)) {
		rv = destroy_xcdev(&mddev->user_cdev);
		if (rv < 0)
			pr_err("Failed to destroy user cdev %d error 0x%x\n",
				i, rv);
	}

	if (mddev_flag_test(mddev, XDF_CDEV_XVC)) {
		rv = destroy_xcdev(&mddev->xvc_cdev);
		if (rv < 0)
			pr_err("Failed to destroy xvc cdev %d error 0x%x\n",
				i, rv);
	}

	if (mddev_flag_test(mddev, XDF_CDEV_BYPASS)) {
		/* iterate over channels */
		for (i = 0; i < mddev->h2c_channel_max; i++) {
			/* remove DMA Bypass character device */
			rv = destroy_xcdev(&mddev->bypass_h2c_cdev[i]);
			if (rv < 0)
				pr_err("Failed to destroy bypass h2c cdev %d error 0x%x\n",
					i, rv);
		}
		for (i = 0; i < mddev->c2h_channel_max; i++) {
			rv = destroy_xcdev(&mddev->bypass_c2h_cdev[i]);
			if (rv < 0)
				pr_err("Failed to destroy bypass c2h %d error 0x%x\n",
					i, rv);
		}
		rv = destroy_xcdev(&mddev->bypass_cdev_base);
		if (rv < 0)
			pr_err("Failed to destroy base cdev\n");
	}

	if (mddev->major)
		unregister_chrdev_region(
				MKDEV(mddev->major, MDLX_MINOR_BASE),
				MDLX_MINOR_COUNT);
}

int mddev_create_interfaces(struct mdlx_pci_dev *mddev)
{
	struct mdlx_dev *mdev = mddev->mdev;
	struct mdlx_engine *engine;
	int i;
	int rv = 0;

	/* initialize control character device */		// Control
	rv = create_xcdev(mddev, &mddev->ctrl_cdev, mdev->config_bar_idx,
			NULL, CHAR_CTRL);
	if (rv < 0) {
		pr_err("create_char(ctrl_cdev) failed\n");
		goto fail;
	}
	//else pr_info("create_char(ctrl_cdev) succeed\n");
	mddev_flag_set(mddev, XDF_CDEV_CTRL);

	/* initialize events character device */		// Event
	for (i = 0; i < mddev->user_max; i++) {
		rv = create_xcdev(mddev, &mddev->events_cdev[i], i, NULL,
			CHAR_EVENTS);
		if (rv < 0) {
			pr_err("create char event %d failed, %d.\n", i, rv);
			goto fail;
		}
		//else pr_info("create char event %d succeed, %d.\n", i, rv);
	}
	mddev_flag_set(mddev, XDF_CDEV_EVENT);

	/* iterate over channels */						// AXI4 port MDLX
	for (i = 0; i < mddev->h2c_channel_max; i++) {
		engine = &mdev->engine_h2c[i];

		if (engine->magic != MAGIC_ENGINE)
			continue;

		rv = create_xcdev(mddev, &mddev->sgdma_h2c_cdev[i], i, engine,
				 CHAR_MDLX_H2C);
		if (rv < 0) {
			pr_err("create char h2c %d failed, %d.\n", i, rv);
			goto fail;
		}
		else pr_info("create char h2c %d succeed, %d.\n", i, rv);
	}

	for (i = 0; i < mddev->c2h_channel_max; i++) {
		engine = &mdev->engine_c2h[i];

		if (engine->magic != MAGIC_ENGINE)
			continue;

		rv = create_xcdev(mddev, &mddev->sgdma_c2h_cdev[i], i, engine,
				 CHAR_MDLX_C2H);
		if (rv < 0) {
			pr_err("create char c2h %d failed, %d.\n", i, rv);
			goto fail;
		}
		else pr_info("create char c2h %d succeed, %d.\n", i, rv);
	}
	mddev_flag_set(mddev, XDF_CDEV_SG);

	/* ??? Bypass */
	/* Initialize Bypass Character Device */		// Bypass
	if (mdev->bypass_bar_idx > 0) {
		for (i = 0; i < mddev->h2c_channel_max; i++) {
			engine = &mdev->engine_h2c[i];

			if (engine->magic != MAGIC_ENGINE)
				continue;

			rv = create_xcdev(mddev, &mddev->bypass_h2c_cdev[i], i,
					engine, CHAR_BYPASS_H2C);
			if (rv < 0) {
				pr_err("create h2c %d bypass I/F failed, %d.\n",
					i, rv);
				goto fail;
			}
		}

		for (i = 0; i < mddev->c2h_channel_max; i++) {
			engine = &mdev->engine_c2h[i];

			if (engine->magic != MAGIC_ENGINE)
				continue;

			rv = create_xcdev(mddev, &mddev->bypass_c2h_cdev[i], i,
					engine, CHAR_BYPASS_C2H);
			if (rv < 0) {
				pr_err("create c2h %d bypass I/F failed, %d.\n",
					i, rv);
				goto fail;
			}
		}

		rv = create_xcdev(mddev, &mddev->bypass_cdev_base,
				mdev->bypass_bar_idx, NULL, CHAR_BYPASS);
		if (rv < 0) {
			pr_err("create bypass failed %d.\n", rv);
			goto fail;
		}
		mddev_flag_set(mddev, XDF_CDEV_BYPASS);
	}

	/* initialize user character device */		// AXI4-Lite
	if (mdev->user_bar_idx >= 0) {
		rv = create_xcdev(mddev, &mddev->user_cdev, mdev->user_bar_idx,
			NULL, CHAR_USER);
		if (rv < 0) {
			pr_err("create_char(user_cdev) failed\n");
			goto fail;
		}
		mddev_flag_set(mddev, XDF_CDEV_USER);

		/* xvc */								// xvc(Medium Virtual Cable)
		rv = create_xcdev(mddev, &mddev->xvc_cdev, mdev->user_bar_idx,
				 NULL, CHAR_XVC);
		if (rv < 0) {
			pr_err("create xvc failed, %d.\n", rv);
			goto fail;
		}
		mddev_flag_set(mddev, XDF_CDEV_XVC);
	}

#ifdef __MDLX_SYSFS__
	/* sys file */
	rv = device_create_file(&mddev->pdev->dev,
				&dev_attr_mdlx_dev_instance);
	if (rv) {
		pr_err("Failed to create device file\n");
		goto fail;
	}
#endif
	pr_info("mddev_create_interfaces finished\n");

	return 0;

fail:
	rv = -1;
	mddev_destroy_interfaces(mddev);
	return rv;
}

int mdlx_cdev_init(void)
{
	g_mdlx_class = class_create(THIS_MODULE, MDLX_NODE_NAME);
	if (IS_ERR(g_mdlx_class)) {
		dbg_init(MDLX_NODE_NAME ": failed to create class");
		return -1;
	}

    /* using kmem_cache_create to enable sequential cleanup */
    cdev_cache = kmem_cache_create("cdev_cache",
                                   sizeof(struct cdev_async_io),
                                   0,
                                   SLAB_HWCACHE_ALIGN,
                                   NULL);
    if (!cdev_cache) {
    	pr_info("memory allocation for cdev_cache failed. OOM\n");
    	return -ENOMEM;
    }

   	mdlx_threads_create(8);

	return 0;
}

void mdlx_cdev_cleanup(void)
{
	if (cdev_cache)
		kmem_cache_destroy(cdev_cache);

	if (g_mdlx_class)
		class_destroy(g_mdlx_class);

	mdlx_threads_destroy();
}
