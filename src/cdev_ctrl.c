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
#include "version.h"
#include "mdlx_cdev.h"
#include "cdev_ctrl.h"

#if KERNEL_VERSION(5, 0, 0) <= LINUX_VERSION_CODE
#define xlx_access_ok(X,Y,Z) access_ok(Y,Z)
#else
#define xlx_access_ok(X,Y,Z) access_ok(X,Y,Z)
#endif

/*
 * character device file operations for control bus (through control bridge)
 */
static ssize_t char_ctrl_read(struct file *fp, char __user *buf, size_t count,
		loff_t *pos)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)fp->private_data;
	struct mdlx_dev *mdev;
	void __iomem *reg;
	u32 w;
	int rv;

	rv = xcdev_check(__func__, xcdev, 0);
	if (rv < 0)
		return rv;
	mdev = xcdev->mdev;

	/* only 32-bit aligned and 32-bit multiples */
	if (*pos & 3)
		return -EPROTO;
	/* first address is BAR base plus file position offset */
	reg = mdev->bar[xcdev->bar] + *pos;
	//w = read_register(reg);
	w = ioread32(reg);
	dbg_sg("%s(@%p, count=%ld, pos=%d) value = 0x%08x\n",
			__func__, reg, (long)count, (int)*pos, w);
	rv = copy_to_user(buf, &w, 4);
	if (rv)
		dbg_sg("Copy to userspace failed but continuing\n");

	*pos += 4;
	return 4;
}

static ssize_t char_ctrl_write(struct file *file, const char __user *buf,
			size_t count, loff_t *pos)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;
	struct mdlx_dev *mdev;
	void __iomem *reg;
	u32 w;
	int rv;

	rv = xcdev_check(__func__, xcdev, 0);
	if (rv < 0)
		return rv;
	mdev = xcdev->mdev;

	/* only 32-bit aligned and 32-bit multiples */
	if (*pos & 3)
		return -EPROTO;

	/* first address is BAR base plus file position offset */
	reg = mdev->bar[xcdev->bar] + *pos;
	rv = copy_from_user(&w, buf, 4);
	if (rv)
		pr_info("copy from user failed %d/4, but continuing.\n", rv);

	dbg_sg("%s(0x%08x @%p, count=%ld, pos=%d)\n",
			__func__, w, reg, (long)count, (int)*pos);
	//write_register(w, reg);
	iowrite32(w, reg);
	*pos += 4;
	return 4;
}

static long version_ioctl(struct mdlx_cdev *xcdev, void __user *arg)
{
	struct mdlx_ioc_info obj;
	struct mdlx_dev *mdev = xcdev->mdev;
	int rv;

	rv = copy_from_user((void *)&obj, arg, sizeof(struct mdlx_ioc_info));
	if (rv) {
		pr_info("copy from user failed %d/%ld.\n",
			rv, sizeof(struct mdlx_ioc_info));
		return -EFAULT;
	}
	memset(&obj, 0, sizeof(obj));
	obj.vendor = mdev->pdev->vendor;
	obj.device = mdev->pdev->device;
	obj.subsystem_vendor = mdev->pdev->subsystem_vendor;
	obj.subsystem_device = mdev->pdev->subsystem_device;
	obj.feature_id = mdev->feature_id;
	obj.driver_version = DRV_MOD_VERSION_NUMBER;
	obj.domain = 0;
	obj.bus = PCI_BUS_NUM(mdev->pdev->devfn);
	obj.dev = PCI_SLOT(mdev->pdev->devfn);
	obj.func = PCI_FUNC(mdev->pdev->devfn);
	if (copy_to_user(arg, &obj, sizeof(struct mdlx_ioc_info)))
		return -EFAULT;
	return 0;
}

long char_ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)filp->private_data;
	struct mdlx_dev *mdev;
	struct mdlx_ioc_base ioctl_obj;
	long result = 0;
	int rv;

	rv = xcdev_check(__func__, xcdev, 0);
	if (rv < 0)
		return rv;

	mdev = xcdev->mdev;
	if (!mdev) {
		pr_info("cmd %u, mdev NULL.\n", cmd);
		return -EINVAL;
	}
	pr_info("cmd 0x%x, mdev 0x%p, pdev 0x%p.\n", cmd, mdev, mdev->pdev);

	if (_IOC_TYPE(cmd) != MDLX_IOC_MAGIC) {
		pr_err("cmd %u, bad magic 0x%x/0x%x.\n",
			 cmd, _IOC_TYPE(cmd), MDLX_IOC_MAGIC);
		return -ENOTTY;
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		result = !xlx_access_ok(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		result =  !xlx_access_ok(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd));

	if (result) {
		pr_err("bad access %ld.\n", result);
		return -EFAULT;
	}

	switch (cmd) {
	case MDLX_IOCINFO:
		if (copy_from_user((void *)&ioctl_obj, (void __user *) arg,
			 sizeof(struct mdlx_ioc_base))) {
			pr_err("copy_from_user failed.\n");
			return -EFAULT;
		}

		if (ioctl_obj.magic != MDLX_XCL_MAGIC) {
			pr_err("magic 0x%x !=  MDLX_XCL_MAGIC (0x%x).\n",
				ioctl_obj.magic, MDLX_XCL_MAGIC);
			return -ENOTTY;
		}
		return version_ioctl(xcdev, (void __user *)arg);
	case MDLX_IOCOFFLINE:
		mdlx_device_offline(mdev->pdev, mdev);
		break;
	case MDLX_IOCONLINE:
		mdlx_device_online(mdev->pdev, mdev);
		break;
	default:
		pr_err("UNKNOWN ioctl cmd 0x%x.\n", cmd);
		return -ENOTTY;
	}
	return 0;
}

/* maps the PCIe BAR into user space for memory-like access using mmap() */
int bridge_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct mdlx_dev *mdev;
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;
	unsigned long off;
	unsigned long phys;
	unsigned long vsize;
	unsigned long psize;
	int rv;

	rv = xcdev_check(__func__, xcdev, 0);
	if (rv < 0)
		return rv;
	mdev = xcdev->mdev;

	off = vma->vm_pgoff << PAGE_SHIFT;
	/* BAR physical address */
	phys = pci_resource_start(mdev->pdev, xcdev->bar) + off;
	vsize = vma->vm_end - vma->vm_start;
	/* complete resource */
	psize = pci_resource_end(mdev->pdev, xcdev->bar) -
		pci_resource_start(mdev->pdev, xcdev->bar) + 1 - off;

	dbg_sg("mmap(): xcdev = 0x%08lx\n", (unsigned long)xcdev);
	dbg_sg("mmap(): cdev->bar = %d\n", xcdev->bar);
	dbg_sg("mmap(): mdev = 0x%p\n", mdev);
	dbg_sg("mmap(): pci_dev = 0x%08lx\n", (unsigned long)mdev->pdev);

	dbg_sg("off = 0x%lx\n", off);
	dbg_sg("start = 0x%llx\n",
		(unsigned long long)pci_resource_start(mdev->pdev,
		xcdev->bar));
	dbg_sg("phys = 0x%lx\n", phys);

	if (vsize > psize)
		return -EINVAL;
	/*
	 * pages must not be cached as this would result in cache line sized
	 * accesses to the end point
	 */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	/*
	 * prevent touching the pages (byte access) for swap-in,
	 * and prevent the pages from being swapped out
	 */
	vma->vm_flags |= VMEM_FLAGS;
	/* make MMIO accessible to user space */
	rv = io_remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
			vsize, vma->vm_page_prot);
	dbg_sg("vma=0x%p, vma->vm_start=0x%lx, phys=0x%lx, size=%lu = %d\n",
		vma, vma->vm_start, phys >> PAGE_SHIFT, vsize, rv);

	if (rv)
		return -EAGAIN;
	return 0;
}

/*
 * character device file operations for control bus (through control bridge)
 */
static const struct file_operations ctrl_fops = {
	.owner = THIS_MODULE,
	.open = char_open,
	.release = char_close,
	.read = char_ctrl_read,
	.write = char_ctrl_write,
	.mmap = bridge_mmap,
	.unlocked_ioctl = char_ctrl_ioctl,
};

void cdev_ctrl_init(struct mdlx_cdev *xcdev)
{
	cdev_init(&xcdev->cdev, &ctrl_fops);
}
