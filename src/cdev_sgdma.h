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


#define IOCTL_MDLX_PERF_V1 (1)
#define MDLX_ADDRMODE_MEMORY (0)
#define MDLX_ADDRMODE_FIXED (1)

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

struct mdlx_performance_ioctl {
	/* IOCTL_MDLX_IOCTL_Vx */
	uint32_t version;
	uint32_t transfer_size;
	/* measurement */
	uint32_t stopped;
	uint32_t iterations;
	uint64_t clock_cycle_count;
	uint64_t data_cycle_count;
	uint64_t pending_count;
};



/* IOCTL codes */

#define IOCTL_MDLX_PERF_START   _IOW('q', 1, struct mdlx_performance_ioctl *)
#define IOCTL_MDLX_PERF_STOP    _IOW('q', 2, struct mdlx_performance_ioctl *)
#define IOCTL_MDLX_PERF_GET     _IOR('q', 3, struct mdlx_performance_ioctl *)
#define IOCTL_MDLX_ADDRMODE_SET _IOW('q', 4, int)
#define IOCTL_MDLX_ADDRMODE_GET _IOR('q', 5, int)
#define IOCTL_MDLX_ALIGN_GET    _IOR('q', 6, int)

#endif /* _MDLX_IOCALLS_POSIX_H_ */
