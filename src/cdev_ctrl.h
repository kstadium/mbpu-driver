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

#ifndef _MDLX_IOCALLS_POSIX_H_
#define _MDLX_IOCALLS_POSIX_H_

#include <linux/ioctl.h>

/* Use 'x' as magic number */
#define MDLX_IOC_MAGIC	'x'
/* XL OpenCL X->58(ASCII), L->6C(ASCII), O->0 C->C L->6C(ASCII); */
#define MDLX_XCL_MAGIC 0X586C0C6C

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 *
 * _IO(type,nr)		    no arguments
 * _IOR(type,nr,datatype)   read data from driver
 * _IOW(type,nr.datatype)   write data to driver
 * _IORW(type,nr,datatype)  read/write data
 *
 * _IOC_DIR(nr)		    returns direction
 * _IOC_TYPE(nr)	    returns magic
 * _IOC_NR(nr)		    returns number
 * _IOC_SIZE(nr)	    returns size
 */

enum MDLX_IOC_TYPES {
	MDLX_IOC_NOP,
	MDLX_IOC_INFO,
	MDLX_IOC_OFFLINE,
	MDLX_IOC_ONLINE,
	MDLX_IOC_MAX
};

struct mdlx_ioc_base {
	unsigned int magic;
	unsigned int command;
};

struct mdlx_ioc_info {
	struct mdlx_ioc_base	base;
	unsigned short		vendor;
	unsigned short		device;
	unsigned short		subsystem_vendor;
	unsigned short		subsystem_device;
	unsigned int		dma_engine_version;
	unsigned int		driver_version;
	unsigned long long	feature_id;
	unsigned short		domain;
	unsigned char		bus;
	unsigned char		dev;
	unsigned char		func;
};

/* IOCTL codes */
#define MDLX_IOCINFO		_IOWR(MDLX_IOC_MAGIC, MDLX_IOC_INFO, \
					struct mdlx_ioc_info)
#define MDLX_IOCOFFLINE		_IO(MDLX_IOC_MAGIC, MDLX_IOC_OFFLINE)
#define MDLX_IOCONLINE		_IO(MDLX_IOC_MAGIC, MDLX_IOC_ONLINE)

#define IOCTL_MDLX_ADDRMODE_SET	_IOW('q', 4, int)
#define IOCTL_MDLX_ADDRMODE_GET	_IOR('q', 5, int)
#define IOCTL_MDLX_ALIGN_GET	_IOR('q', 6, int)

#endif /* _MDLX_IOCALLS_POSIX_H_ */
