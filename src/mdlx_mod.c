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

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/aer.h>
/* include early, to verify it depends only on the headers above */
#include "libmdlx_api.h"
#include "libmdlx.h"
#include "mdlx_mod.h"
#include "mdlx_cdev.h"
#include "version.h"

#define DRV_MODULE_NAME		"mdlx"
#define DRV_MODULE_DESC		"Medium MDLX Reference Driver"
#define DRV_MODULE_RELDATE	"Feb. 2018"

static char version[] =
	DRV_MODULE_DESC " " DRV_MODULE_NAME " v" DRV_MODULE_VERSION "\n";

MODULE_AUTHOR("Medium, Inc.");
MODULE_DESCRIPTION(DRV_MODULE_DESC);
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_LICENSE("Dual BSD/GPL");

/* SECTION: Module global variables */
static int mddev_cnt;

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(0x10ee, 0x1818), },
	{ PCI_DEVICE(0x10ee, 0x0625), },	
	{ PCI_DEVICE(0x10ee, 0x903f), },	
	
	{ PCI_DEVICE(0x10ee, 0x9038), },
	{ PCI_DEVICE(0x10ee, 0x9028), },
	{ PCI_DEVICE(0x10ee, 0x9018), },
	{ PCI_DEVICE(0x10ee, 0x9034), },
	{ PCI_DEVICE(0x10ee, 0x9024), },
	{ PCI_DEVICE(0x10ee, 0x9014), },
	{ PCI_DEVICE(0x10ee, 0x9032), },
	{ PCI_DEVICE(0x10ee, 0x9022), },
	{ PCI_DEVICE(0x10ee, 0x9012), },
	{ PCI_DEVICE(0x10ee, 0x9031), },
	{ PCI_DEVICE(0x10ee, 0x9021), },
	{ PCI_DEVICE(0x10ee, 0x9011), },

	{ PCI_DEVICE(0x10ee, 0x8011), },
	{ PCI_DEVICE(0x10ee, 0x8012), },
	{ PCI_DEVICE(0x10ee, 0x8014), },
	{ PCI_DEVICE(0x10ee, 0x8018), },
	{ PCI_DEVICE(0x10ee, 0x8021), },
	{ PCI_DEVICE(0x10ee, 0x8022), },
	{ PCI_DEVICE(0x10ee, 0x8024), },
	{ PCI_DEVICE(0x10ee, 0x8028), },
	{ PCI_DEVICE(0x10ee, 0x8031), },
	{ PCI_DEVICE(0x10ee, 0x8032), },
	{ PCI_DEVICE(0x10ee, 0x8034), },
	{ PCI_DEVICE(0x10ee, 0x8038), },

	{ PCI_DEVICE(0x10ee, 0x7011), },
	{ PCI_DEVICE(0x10ee, 0x7012), },
	{ PCI_DEVICE(0x10ee, 0x7014), },
	{ PCI_DEVICE(0x10ee, 0x7018), },
	{ PCI_DEVICE(0x10ee, 0x7021), },
	{ PCI_DEVICE(0x10ee, 0x7022), },
	{ PCI_DEVICE(0x10ee, 0x7024), },
	{ PCI_DEVICE(0x10ee, 0x7028), },
	{ PCI_DEVICE(0x10ee, 0x7031), },
	{ PCI_DEVICE(0x10ee, 0x7032), },
	{ PCI_DEVICE(0x10ee, 0x7034), },
	{ PCI_DEVICE(0x10ee, 0x7038), },

	{ PCI_DEVICE(0x10ee, 0x6828), },
	{ PCI_DEVICE(0x10ee, 0x6830), },
	{ PCI_DEVICE(0x10ee, 0x6928), },
	{ PCI_DEVICE(0x10ee, 0x6930), },
	{ PCI_DEVICE(0x10ee, 0x6A28), },
	{ PCI_DEVICE(0x10ee, 0x6A30), },
	{ PCI_DEVICE(0x10ee, 0x6D30), },

	{ PCI_DEVICE(0x10ee, 0x4808), },
	{ PCI_DEVICE(0x10ee, 0x4828), },
	{ PCI_DEVICE(0x10ee, 0x4908), },
	{ PCI_DEVICE(0x10ee, 0x4A28), },
	{ PCI_DEVICE(0x10ee, 0x4B28), },

	{ PCI_DEVICE(0x10ee, 0x2808), },

#ifdef INTERNAL_TESTING
	{ PCI_DEVICE(0x1d0f, 0x1042), 0},
#endif
	{0,}
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static void mddev_free(struct mdlx_pci_dev *mddev)
{
	struct mdlx_dev *mdev = mddev->mdev;

	pr_info("mddev 0x%p, destroy_interfaces, mdev 0x%p.\n", mddev, mdev);
	mddev_destroy_interfaces(mddev);
	mddev->mdev = NULL;
	pr_info("mddev 0x%p, mdev 0x%p mdlx_device_close.\n", mddev, mdev);
	mdlx_device_close(mddev->pdev, mdev);
	mddev_cnt--;

	kfree(mddev);
}

static struct mdlx_pci_dev *mddev_alloc(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev = kmalloc(sizeof(*mddev), GFP_KERNEL);

	if (!mddev)
		return NULL;
	memset(mddev, 0, sizeof(*mddev));

	mddev->magic = MAGIC_DEVICE;
	mddev->pdev = pdev;
	mddev->user_max = MAX_USER_IRQ;
	mddev->h2c_channel_max = MDLX_CHANNEL_NUM_MAX;
	mddev->c2h_channel_max = MDLX_CHANNEL_NUM_MAX;

	mddev_cnt++;
	return mddev;
}

static int probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int rv = 0;
	struct mdlx_pci_dev *mddev = NULL;
	struct mdlx_dev *mdev;
	void *hndl;

	//pr_info(">>>>> PCI Device probe one\n");
	mddev = mddev_alloc(pdev);
	if (!mddev) {
    	pr_info("mddev_alloc failed\n");
    	return -ENOMEM;
   	}
  	//else pr_info("mddev_alloc succeed\n");

	hndl = mdlx_device_open(DRV_MODULE_NAME, pdev, &mddev->user_max,
			&mddev->h2c_channel_max, &mddev->c2h_channel_max);
	
	if (!hndl) {
		rv = -EINVAL;
		goto err_out;
	}

	if (mddev->user_max > MAX_USER_IRQ) {
		pr_err("Maximum users limit reached\n");
		rv = -EINVAL;
		goto err_out;
	}

	if (mddev->h2c_channel_max > MDLX_CHANNEL_NUM_MAX) {
		pr_err("Maximun H2C channel limit reached\n");
		rv = -EINVAL;
		goto err_out;
	}

	if (mddev->c2h_channel_max > MDLX_CHANNEL_NUM_MAX) {
		pr_err("Maximun C2H channel limit reached\n");
		rv = -EINVAL;
		goto err_out;
	}

	if (!mddev->h2c_channel_max && !mddev->c2h_channel_max)
		pr_warn("NO engine found!\n");

	if (mddev->user_max) {
		u32 mask = (1 << (mddev->user_max + 1)) - 1;

		rv = mdlx_user_isr_enable(hndl, mask);
		if (rv)
			goto err_out;
	}

	/* make sure no duplicate */
	mdev = mdev_find_by_pdev(pdev);
	if (!mdev) {
		pr_warn("NO mdev found!\n");
		rv =  -EINVAL;
		goto err_out;
	}

	if (hndl != mdev) {
		pr_err("mdev handle mismatch\n");
		rv =  -EINVAL;
		goto err_out;
	}

	pr_info("%s mdlx%d, pdev 0x%p, mdev 0x%p, 0x%p, usr %d, ch %d,%d.\n",
		dev_name(&pdev->dev), mdev->idx, pdev, mddev, mdev,
		mddev->user_max, mddev->h2c_channel_max,
		mddev->c2h_channel_max);

	mddev->mdev = hndl;

	rv = mddev_create_interfaces(mddev);
	if (rv)
		goto err_out;

	dev_set_drvdata(&pdev->dev, mddev);

	return 0;

err_out:
	pr_err("pdev 0x%p, err %d.\n", pdev, rv);
	mddev_free(mddev);
	return rv;
}

static void remove_one(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev;

	if (!pdev)
		return;

	mddev = dev_get_drvdata(&pdev->dev);
	if (!mddev)
		return;

	pr_info("pdev 0x%p, mdev 0x%p, 0x%p.\n",
		pdev, mddev, mddev->mdev);
	mddev_free(mddev);

	dev_set_drvdata(&pdev->dev, NULL);
}

static pci_ers_result_t mdlx_error_detected(struct pci_dev *pdev,
					pci_channel_state_t state)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		pr_warn("dev 0x%p,0x%p, frozen state error, reset controller\n",
			pdev, mddev);
		mdlx_device_offline(pdev, mddev->mdev);
		pci_disable_device(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		pr_warn("dev 0x%p,0x%p, failure state error, req. disconnect\n",
			pdev, mddev);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t mdlx_slot_reset(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	pr_info("0x%p restart after slot reset\n", mddev);
	if (pci_enable_device_mem(pdev)) {
		pr_info("0x%p failed to renable after slot reset\n", mddev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	pci_restore_state(pdev);
	pci_save_state(pdev);
	mdlx_device_online(pdev, mddev->mdev);

	return PCI_ERS_RESULT_RECOVERED;
}

static void mdlx_error_resume(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, mddev);
	pci_cleanup_aer_uncorrect_error_status(pdev);
}

#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
static void mdlx_reset_prepare(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, mddev);
	mdlx_device_offline(pdev, mddev->mdev);
}

static void mdlx_reset_done(struct pci_dev *pdev)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p.\n", pdev, mddev);
	mdlx_device_online(pdev, mddev->mdev);
}

#elif KERNEL_VERSION(3, 16, 0) <= LINUX_VERSION_CODE
static void mdlx_reset_notify(struct pci_dev *pdev, bool prepare)
{
	struct mdlx_pci_dev *mddev = dev_get_drvdata(&pdev->dev);

	pr_info("dev 0x%p,0x%p, prepare %d.\n", pdev, mddev, prepare);

	if (prepare)
		mdlx_device_offline(pdev, mddev->mdev);
	else
		mdlx_device_online(pdev, mddev->mdev);
}
#endif

static const struct pci_error_handlers mdlx_err_handler = {
	.error_detected	= mdlx_error_detected,
	.slot_reset	= mdlx_slot_reset,
	.resume		= mdlx_error_resume,
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
	.reset_prepare	= mdlx_reset_prepare,
	.reset_done	= mdlx_reset_done,
#elif KERNEL_VERSION(3, 16, 0) <= LINUX_VERSION_CODE
	.reset_notify	= mdlx_reset_notify,
#endif
};

static struct pci_driver pci_driver = {
	.name = DRV_MODULE_NAME,
	.id_table = pci_ids,
	.probe = probe_one,
	.remove = remove_one,
	.err_handler = &mdlx_err_handler,
};

static int __init mdlx_mod_init(void)
{
	int rv;

	pr_info("Medium Distributed Ledger Driver Module Init");
	pr_info("%s", version);

	if (desc_blen_max > MDLX_DESC_BLEN_MAX)
		desc_blen_max = MDLX_DESC_BLEN_MAX;
	
	pr_info("desc_blen_max: 0x%x/%u, sgdma_timeout: %u sec.\n",
		desc_blen_max, desc_blen_max, sgdma_timeout);

	rv = mdlx_cdev_init();
 
  	pr_info("mdlx_cdev_init / finished\n");
	if (rv < 0)
		return rv;

	return pci_register_driver(&pci_driver);
}

static void __exit mdlx_mod_exit(void)
{
	/* unregister this driver from the PCI bus driver */
	dbg_init("pci_unregister_driver.\n");
	pci_unregister_driver(&pci_driver);
	mdlx_cdev_cleanup();
}

module_init(mdlx_mod_init);
module_exit(mdlx_mod_exit);
