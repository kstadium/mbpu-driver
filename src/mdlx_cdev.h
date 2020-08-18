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

#ifndef __MDLX_CHRDEV_H__
#define __MDLX_CHRDEV_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include "mdlx_mod.h"

#define MDLX_NODE_NAME	"mdlx"
#define MDLX_MINOR_BASE (0)
#define MDLX_MINOR_COUNT (255)

void mdlx_cdev_cleanup(void);
int mdlx_cdev_init(void);

int char_open(struct inode *inode, struct file *file);
int char_close(struct inode *inode, struct file *file);
int xcdev_check(const char *fname, struct mdlx_cdev *xcdev, bool check_engine);
void cdev_ctrl_init(struct mdlx_cdev *xcdev);
void cdev_xvc_init(struct mdlx_cdev *xcdev);
void cdev_event_init(struct mdlx_cdev *xcdev);
void cdev_sgdma_init(struct mdlx_cdev *xcdev);
void cdev_bypass_init(struct mdlx_cdev *xcdev);
long char_ctrl_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

void mddev_destroy_interfaces(struct mdlx_pci_dev *mddev);
int mddev_create_interfaces(struct mdlx_pci_dev *mddev);

int bridge_mmap(struct file *file, struct vm_area_struct *vma);

#endif /* __MDLX_CHRDEV_H__ */
