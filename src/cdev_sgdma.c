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

#include <linux/types.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/aio.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
#include <linux/uio.h>
#endif
#include "libmdlx_api.h"
#include "mdlx_cdev.h"
#include "cdev_sgdma.h"
#include "mdlx_thread.h"

/* Module Parameters */
unsigned int sgdma_timeout = 10;
module_param(sgdma_timeout, uint, 0644);
MODULE_PARM_DESC(sgdma_timeout, "timeout in seconds for sgdma, default is 10 sec.");


extern struct kmem_cache *cdev_cache;
static void char_sgdma_unmap_user_buf(struct mdlx_io_cb *cb, bool write);


static void async_io_handler(unsigned long  cb_hndl, int err)
{
	struct mdlx_cdev *xcdev;
	struct mdlx_engine *engine;
	struct mdlx_dev *mdev;
	struct mdlx_io_cb *cb = (struct mdlx_io_cb *)cb_hndl;
	struct cdev_async_io *caio = (struct cdev_async_io *)cb->private;
	ssize_t numbytes = 0;
	ssize_t res, res2;
	int lock_stat;
	int rv;

	if (NULL == caio) {
		pr_err("Invalid work struct\n");
		return;
	}

	xcdev = (struct mdlx_cdev *)caio->iocb->ki_filp->private_data;

	rv = xcdev_check(__func__, xcdev, 1);
		if (rv < 0)
			return;

	/* Safeguarding for cancel requests */
	lock_stat = spin_trylock(&caio->lock);
	if (!lock_stat) {
		pr_err("caio lock not acquired\n");
		goto skip_dev_lock;
	}

	if (false != caio->cancel) {
		pr_err("skipping aio\n");
		goto skip_tran;
	}


	engine = xcdev->engine;
	mdev = xcdev->mdev;

	if (!err)
		numbytes = mdlx_xfer_completion((void *)cb, mdev, engine->channel, cb->write, cb->ep_addr, &cb->sgt,
				0, sgdma_timeout * 1000);

	char_sgdma_unmap_user_buf(cb, cb->write);

	caio->res2 |= (err < 0) ? err : 0;
	if (caio->res2)
		caio->err_cnt++;

	caio->cmpl_cnt++;
	caio->res += numbytes;


	if (caio->cmpl_cnt == caio->req_cnt)
	{
		res = caio->res;
		res2 = caio->res2;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
		caio->iocb->ki_complete(caio->iocb, res, res2);
#else
		aio_complete(caio->iocb, res, res2);
#endif
skip_tran:
		spin_unlock(&caio->lock);
		kmem_cache_free(cdev_cache, caio);
		kfree(cb);
		return;
	}
	else
	{
		spin_unlock(&caio->lock);
		return;

	}
skip_dev_lock:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	caio->iocb->ki_complete(caio->iocb, numbytes, -EBUSY);
#else
	aio_complete(caio->iocb, numbytes, -EBUSY);
#endif
	kmem_cache_free(cdev_cache, caio);
}


/*
 * character device file operations for SG DMA engine
 */
static loff_t char_sgdma_llseek(struct file *file, loff_t off, int whence)
{
	loff_t newpos = 0;

	switch (whence) {
	case 0: /* SEEK_SET */
		newpos = off;
		break;
	case 1: /* SEEK_CUR */
		newpos = file->f_pos + off;
		break;
	case 2: /* SEEK_END, @TODO should work from end of address space */
		newpos = UINT_MAX + off;
		break;
	default: /* can't happen */
		return -EINVAL;
	}
	if (newpos < 0)
		return -EINVAL;
	file->f_pos = newpos;
	dbg_fops("%s: pos=%lld\n", __func__, (signed long long)newpos);

#if 0
	pr_err("0x%p, off %lld, whence %d -> pos %lld.\n",
		file, (signed long long)off, whence, (signed long long)off);
#endif

	return newpos;
}

/* char_sgdma_read_write() -- Read from or write to the device
 *
 * @buf userspace buffer
 * @count number of bytes in the userspace buffer
 * @pos byte-address in device
 * @dir_to_device If !0, a write to the device is performed
 *
 * Iterate over the userspace buffer, taking at most 255 * PAGE_SIZE bytes for
 * each DMA transfer.
 *
 * For each transfer, get the user pages, build a sglist, map, build a
 * descriptor table. submit the transfer. wait for the interrupt handler
 * to wake us on completion.
 */

static int check_transfer_align(struct mdlx_engine *engine,
	const char __user *buf, size_t count, loff_t pos, int sync)
{
	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}

	/* AXI ST or AXI MM non-incremental addressing mode? */
	if (engine->non_incr_addr) {
		int buf_lsb = (int)((uintptr_t)buf) & (engine->addr_align - 1);
		size_t len_lsb = count & ((size_t)engine->len_granularity - 1);
		int pos_lsb = (int)pos & (engine->addr_align - 1);

		dbg_tfr("AXI ST or MM non-incremental\n");
		dbg_tfr("buf_lsb = %d, pos_lsb = %d, len_lsb = %ld\n", buf_lsb,
			pos_lsb, len_lsb);

		if (buf_lsb != 0) {
			dbg_tfr("FAIL: non-aligned buffer address %p\n", buf);
			return -EINVAL;
		}

		if ((pos_lsb != 0) && (sync)) {
			dbg_tfr("FAIL: non-aligned AXI MM FPGA addr 0x%llx\n",
				(unsigned long long)pos);
			return -EINVAL;
		}

		if (len_lsb != 0) {
			dbg_tfr("FAIL: len %d is not a multiple of %d\n",
				(int)count,
				(int)engine->len_granularity);
			return -EINVAL;
		}
		/* AXI MM incremental addressing mode */
	} else {
		int buf_lsb = (int)((uintptr_t)buf) & (engine->addr_align - 1);
		int pos_lsb = (int)pos & (engine->addr_align - 1);

		if (buf_lsb != pos_lsb) {
			dbg_tfr("FAIL: Misalignment error\n");
			dbg_tfr("host addr %p, FPGA addr 0x%llx\n", buf, pos);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Map a user memory range into a scatterlist
 * inspired by vhost_scsi_map_to_sgl()
 * Returns the number of scatterlist entries used or -errno on error.
 */
static inline void mdlx_io_cb_release(struct mdlx_io_cb *cb)
{
	int i;

	for (i = 0; i < cb->pages_nr; i++)
		put_page(cb->pages[i]);

	sg_free_table(&cb->sgt);
	kfree(cb->pages);

	memset(cb, 0, sizeof(*cb));
}

static void char_sgdma_unmap_user_buf(struct mdlx_io_cb *cb, bool write)
{
	int i;

	sg_free_table(&cb->sgt);

	if (!cb->pages || !cb->pages_nr)
		return;

	for (i = 0; i < cb->pages_nr; i++) {
		if (cb->pages[i]) {
			if (!write)
				set_page_dirty_lock(cb->pages[i]);
			put_page(cb->pages[i]);
		} else
			break;
	}

	if (i != cb->pages_nr)
		pr_info("sgl pages %d/%u.\n", i, cb->pages_nr);

	kfree(cb->pages);
	cb->pages = NULL;
}

static int char_sgdma_map_user_buf_to_sgl(struct mdlx_io_cb *cb, bool write)
{
	struct sg_table *sgt = &cb->sgt;
	unsigned long len = cb->len;
	void __user *buf = cb->buf;
	struct scatterlist *sg;
	unsigned int pages_nr = (((unsigned long)buf + len + PAGE_SIZE - 1) -
				 ((unsigned long)buf & PAGE_MASK))
				>> PAGE_SHIFT;
	int i;
	int rv;

	if (pages_nr == 0)
		return -EINVAL;

	if (sg_alloc_table(sgt, pages_nr, GFP_KERNEL)) {
		pr_err("sgl OOM.\n");
		return -ENOMEM;
	}

	// init memory "0"
	cb->pages = kcalloc(pages_nr, sizeof(struct page *), GFP_KERNEL);
	if (!cb->pages) {
		pr_err("pages OOM.\n");
		rv = -ENOMEM;
		goto err_out;
	}

	rv = get_user_pages_fast((unsigned long)buf, pages_nr, 1/* write */,
				cb->pages);
	/* No pages were pinned */
	if (rv < 0) {
		pr_err("unable to pin down %u user pages, %d.\n",
			pages_nr, rv);
		goto err_out;
	}
	/* Less pages pinned than wanted */
	if (rv != pages_nr) {
		pr_err("unable to pin down all %u user pages, %d.\n",
			pages_nr, rv);
		cb->pages_nr = rv;
		rv = -EFAULT;
		goto err_out;
	}

	for (i = 1; i < pages_nr; i++) {
		if (cb->pages[i - 1] == cb->pages[i]) {
			pr_err("duplicate pages, %d, %d.\n",
				i - 1, i);
			rv = -EFAULT;
			cb->pages_nr = pages_nr;
			goto err_out;
		}
	}

	sg = sgt->sgl;
	for (i = 0; i < pages_nr; i++, sg = sg_next(sg)) {
		unsigned int offset = offset_in_page(buf);
		unsigned int nbytes =
			min_t(unsigned int, PAGE_SIZE - offset, len);

		flush_dcache_page(cb->pages[i]);
		sg_set_page(sg, cb->pages[i], nbytes, offset);

		buf += nbytes;
		len -= nbytes;
	}

	if (len) {
		pr_err("Invalid user buffer length. Cannot map to sgl\n");
		return -EINVAL;
	}
	cb->pages_nr = pages_nr;
	return 0;

err_out:
	char_sgdma_unmap_user_buf(cb, write);

	return rv;
}

static ssize_t char_sgdma_read_write(struct file *file, const char __user *buf,
		size_t count, loff_t *pos, bool write)
{
	int rv;
	ssize_t res = 0;
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;
	struct mdlx_dev *mdev;
	struct mdlx_engine *engine;
	struct mdlx_io_cb cb;

	rv = xcdev_check(__func__, xcdev, 1);
	if (rv < 0)
		return rv;
	mdev = xcdev->mdev;
	engine = xcdev->engine;

	dbg_tfr("file 0x%p, priv 0x%p, buf 0x%p,%llu, pos %llu, W %d, %s.\n",
		file, file->private_data, buf, (u64)count, (u64)*pos, write,
		engine->name);

	if ((write && engine->dir != DMA_TO_DEVICE) ||
	    (!write && engine->dir != DMA_FROM_DEVICE)) {
		pr_err("r/w mismatch. W %d, dir %d.\n",
			write, engine->dir);
		return -EINVAL;
	}

	rv = check_transfer_align(engine, buf, count, *pos, 1);
	if (rv) {
		pr_info("Invalid transfer alignment detected\n");
		return rv;
	}

	memset(&cb, 0, sizeof(struct mdlx_io_cb));
	cb.buf = (char __user *)buf;
	cb.len = count;
	cb.ep_addr = (u64)*pos;
	cb.write = write;
	rv = char_sgdma_map_user_buf_to_sgl(&cb, write);		// map
	if (rv < 0)
		return rv;

	res = mdlx_xfer_submit(mdev, engine->channel, write, *pos, &cb.sgt,
				0, sgdma_timeout * 1000);					// transfer

	char_sgdma_unmap_user_buf(&cb, write);					// unmap

	return res;
}


static ssize_t char_sgdma_write(struct file *file, const char __user *buf,
		size_t count, loff_t *pos)
{
	return char_sgdma_read_write(file, buf, count, pos, 1);
}

static ssize_t char_sgdma_read(struct file *file, char __user *buf,
		size_t count, loff_t *pos)
{
	return char_sgdma_read_write(file, buf, count, pos, 0);
}

static ssize_t cdev_aio_write(struct kiocb *iocb, const struct iovec *io,
                              unsigned long count, loff_t pos)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)iocb->ki_filp->private_data;
	struct cdev_async_io *caio;
	struct mdlx_engine *engine;
	struct mdlx_dev *mdev;
	int rv;
	unsigned long i;

	if (!xcdev) {
		pr_info("file 0x%p, xcdev NULL, %llu, pos %llu, W %d.\n",
		        iocb->ki_filp, (u64)count, (u64)pos, 1);
		return -EINVAL;
	}

 	engine = xcdev->engine;
	mdev = xcdev->mdev;

	if (engine->dir != DMA_TO_DEVICE) {
		pr_err("r/w mismatch. WRITE, dir %d.\n",
			engine->dir);
		return -EINVAL;
	}

	caio = kmem_cache_alloc(cdev_cache, GFP_KERNEL);
	memset(caio, 0, sizeof(struct cdev_async_io));

	caio->cb = kzalloc(count * (sizeof(struct mdlx_io_cb)), GFP_KERNEL);

	spin_lock_init(&caio->lock);
	iocb->private = caio;
	caio->iocb = iocb;
	caio->write = true;
	caio->cancel = false;
	caio->req_cnt = count;

	for (i = 0; i < count; i++) {

		memset(&(caio->cb[i]), 0, sizeof(struct mdlx_io_cb));

		caio->cb[i].buf = io[i].iov_base;
		caio->cb[i].len = io[i].iov_len;
		caio->cb[i].ep_addr = (u64)pos;
		caio->cb[i].write = true;
		caio->cb[i].private = caio;
		caio->cb[i].io_done = async_io_handler;
		rv = check_transfer_align(engine, caio->cb[i].buf, caio->cb[i].len, pos, 1);
		if (rv) {
			pr_info("Invalid transfer alignment detected\n");
			kmem_cache_free(cdev_cache, caio);
 			return rv;
		}

		rv = char_sgdma_map_user_buf_to_sgl(&caio->cb[i], true);
		if (rv < 0) {
			return rv;
		}

		rv = mdlx_xfer_submit_nowait((void *)&caio->cb[i], mdev, engine->channel, caio->cb[i].write, caio->cb[i].ep_addr, &caio->cb[i].sgt,
									0, sgdma_timeout * 1000);
 	}

	if (engine->cmplthp)
		mdlx_kthread_wakeup(engine->cmplthp);

	return -EIOCBQUEUED;
}


static ssize_t cdev_aio_read(struct kiocb *iocb, const struct iovec *io,
                             unsigned long count, loff_t pos)
{

	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)iocb->ki_filp->private_data;
	struct cdev_async_io *caio;
	struct mdlx_engine *engine;
	struct mdlx_dev *mdev;
	int rv;
	unsigned long i;

	if (!xcdev) {
		pr_info("file 0x%p, xcdev NULL, %llu, pos %llu, W %d.\n",
		        iocb->ki_filp, (u64)count, (u64)pos, 1);
		return -EINVAL;
	}

	engine = xcdev->engine;
	mdev = xcdev->mdev;

	if (engine->dir != DMA_FROM_DEVICE) {
		pr_err("r/w mismatch. READ, dir %d.\n",
			engine->dir);
		return -EINVAL;
	}

	caio = kmem_cache_alloc(cdev_cache, GFP_KERNEL);
	memset(caio, 0, sizeof(struct cdev_async_io));

	caio->cb = kzalloc(count * (sizeof(struct mdlx_io_cb)), GFP_KERNEL);

	spin_lock_init(&caio->lock);
	iocb->private = caio;
	caio->iocb = iocb;
	caio->write = false;
	caio->cancel = false;
	caio->req_cnt = count;

	for (i = 0; i < count; i++) {

		memset(&(caio->cb[i]), 0, sizeof(struct mdlx_io_cb));

		caio->cb[i].buf = io[i].iov_base;
		caio->cb[i].len = io[i].iov_len;
		caio->cb[i].ep_addr = (u64)pos;
		caio->cb[i].write = false;
		caio->cb[i].private = caio;
		caio->cb[i].io_done = async_io_handler;

		rv = check_transfer_align(engine, caio->cb[i].buf, caio->cb[i].len, pos, 1);
		if (rv) {
			pr_info("Invalid transfer alignment detected\n");
			kmem_cache_free(cdev_cache, caio);
			return rv;
		}

		rv = char_sgdma_map_user_buf_to_sgl(&caio->cb[i], true);
		if (rv < 0) {
			return rv;
		}

		rv = mdlx_xfer_submit_nowait((void *)&caio->cb[i], mdev, engine->channel, caio->cb[i].write, caio->cb[i].ep_addr, &caio->cb[i].sgt,
											0, sgdma_timeout * 1000);
	}

	if (engine->cmplthp)
		mdlx_kthread_wakeup(engine->cmplthp);

	return -EIOCBQUEUED;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static ssize_t cdev_write_iter(struct kiocb *iocb, struct iov_iter *io)
{
	return cdev_aio_write(iocb, io->iov, io->nr_segs, io->iov_offset);
}

static ssize_t cdev_read_iter(struct kiocb *iocb, struct iov_iter *io)
{
	return cdev_aio_read(iocb, io->iov, io->nr_segs, io->iov_offset);
}
#endif

static int ioctl_do_perf_start(struct mdlx_engine *engine, unsigned long arg)
{
	int rv;
	struct mdlx_dev *mdev;

	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}

	mdev = engine->mdev;
	if (!mdev) {
		pr_err("Invalid mdev\n");
		return -EINVAL;
	}

	/* performance measurement already running on this engine? */
	if (engine->mdlx_perf) {
		dbg_perf("IOCTL_MDLX_PERF_START failed!\n");
		dbg_perf("Perf measurement already seems to be running!\n");
		return -EBUSY;
	}
	engine->mdlx_perf = kzalloc(sizeof(struct mdlx_performance_ioctl),
		GFP_KERNEL);

	if (!engine->mdlx_perf)
		return -ENOMEM;

	rv = copy_from_user(engine->mdlx_perf,
		(struct mdlx_performance_ioctl __user *)arg,
		sizeof(struct mdlx_performance_ioctl));

	if (rv < 0) {
		dbg_perf("Failed to copy from user space 0x%lx\n", arg);
		return -EINVAL;
	}
	if (engine->mdlx_perf->version != IOCTL_MDLX_PERF_V1) {
		dbg_perf("Unsupported IOCTL version %d\n",
			engine->mdlx_perf->version);
		return -EINVAL;
	}

	enable_perf(engine);
	dbg_perf("transfer_size = %d\n", engine->mdlx_perf->transfer_size);
	/* initialize wait queue */
#if KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	init_swait_queue_head(&engine->mdlx_perf_wq);
#else
	init_waitqueue_head(&engine->mdlx_perf_wq);
#endif
	rv = mdlx_performance_submit(mdev, engine);
	if (rv < 0)
		pr_err("Failed to submit dma performance\n");
	return rv;
}

static int ioctl_do_perf_stop(struct mdlx_engine *engine, unsigned long arg)
{
	struct mdlx_transfer *transfer = NULL;
	int rv;

	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}

	dbg_perf("IOCTL_MDLX_PERF_STOP\n");

	/* no performance measurement running on this engine? */
	if (!engine->mdlx_perf) {
		dbg_perf("No measurement in progress\n");
		return -EINVAL;
	}

	/* stop measurement */
	transfer = engine_cyclic_stop(engine);
	if (!transfer) {
		pr_err("Failed to stop cyclic transfer\n");
		return -EINVAL;
	}
	dbg_perf("Waiting for measurement to stop\n");

	get_perf_stats(engine);

	rv = copy_to_user((void __user *)arg, engine->mdlx_perf,
			sizeof(struct mdlx_performance_ioctl));
	if (rv) {
		dbg_perf("Error copying result to user\n");
		return rv;
	}

	kfree(transfer);

	kfree(engine->mdlx_perf);
	engine->mdlx_perf = NULL;

	return 0;
}

static int ioctl_do_perf_get(struct mdlx_engine *engine, unsigned long arg)
{
	int rc;

	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}

	dbg_perf("IOCTL_MDLX_PERF_GET\n");

	if (engine->mdlx_perf) {
		get_perf_stats(engine);

		rc = copy_to_user((void __user *)arg, engine->mdlx_perf,
			sizeof(struct mdlx_performance_ioctl));
		if (rc) {
			dbg_perf("Error copying result to user\n");
			return rc;
		}
	} else {
		dbg_perf("engine->mdlx_perf == NULL?\n");
		return -EPROTO;
	}

	return 0;
}

static int ioctl_do_addrmode_set(struct mdlx_engine *engine, unsigned long arg)
{
	return engine_addrmode_set(engine, arg);
}

static int ioctl_do_addrmode_get(struct mdlx_engine *engine, unsigned long arg)
{
	int rv;
	unsigned long src;

	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}
	src = !!engine->non_incr_addr;

	dbg_perf("IOCTL_MDLX_ADDRMODE_GET\n");
	rv = put_user(src, (int __user *)arg);

	return rv;
}

static int ioctl_do_align_get(struct mdlx_engine *engine, unsigned long arg)
{
	if (!engine) {
		pr_err("Invalid DMA engine\n");
		return -EINVAL;
	}

	dbg_perf("IOCTL_MDLX_ALIGN_GET\n");
	return put_user(engine->addr_align, (int __user *)arg);
}

static long char_sgdma_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;
	struct mdlx_dev *mdev;
	struct mdlx_engine *engine;

	int rv = 0;

	rv = xcdev_check(__func__, xcdev, 1);
	if (rv < 0)
		return rv;

	mdev = xcdev->mdev;
	engine = xcdev->engine;

	switch (cmd) {
	case IOCTL_MDLX_PERF_START:
		rv = ioctl_do_perf_start(engine, arg);
		break;
	case IOCTL_MDLX_PERF_STOP:
		rv = ioctl_do_perf_stop(engine, arg);
		break;
	case IOCTL_MDLX_PERF_GET:
		rv = ioctl_do_perf_get(engine, arg);
		break;
	case IOCTL_MDLX_ADDRMODE_SET:
		rv = ioctl_do_addrmode_set(engine, arg);
		break;
	case IOCTL_MDLX_ADDRMODE_GET:
		rv = ioctl_do_addrmode_get(engine, arg);
		break;
	case IOCTL_MDLX_ALIGN_GET:
		rv = ioctl_do_align_get(engine, arg);
		break;
	default:
		dbg_perf("Unsupported operation\n");
		rv = -EINVAL;
		break;
	}

	return rv;
}

static int char_sgdma_open(struct inode *inode, struct file *file)
{
	struct mdlx_cdev *xcdev;
	struct mdlx_engine *engine;

	char_open(inode, file);

	xcdev = (struct mdlx_cdev *)file->private_data;
	engine = xcdev->engine;

	if (engine->streaming && engine->dir == DMA_FROM_DEVICE) {
		if (engine->device_open == 1)
			return -EBUSY;
		engine->device_open = 1;
	}

	return 0;
}

static int char_sgdma_close(struct inode *inode, struct file *file)
{
	struct mdlx_cdev *xcdev = (struct mdlx_cdev *)file->private_data;
	struct mdlx_engine *engine;
	int rv;

	rv = xcdev_check(__func__, xcdev, 1);
	if (rv < 0)
		return rv;

	engine = xcdev->engine;

	if (engine->streaming && engine->dir == DMA_FROM_DEVICE) {
		engine->device_open = 0;
		if (engine->cyclic_req)
			return mdlx_cyclic_transfer_teardown(engine);
	}

	return 0;
}
static const struct file_operations sgdma_fops = {
	.owner = THIS_MODULE,
	.open = char_sgdma_open,
	.release = char_sgdma_close,
	.write = char_sgdma_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	.write_iter = cdev_write_iter,
#else
	.aio_write = cdev_aio_write,
#endif
	.read = char_sgdma_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	.read_iter = cdev_read_iter,
#else
	.aio_read = cdev_aio_read,
#endif
	.unlocked_ioctl = char_sgdma_ioctl,
	.llseek = char_sgdma_llseek,
};

void cdev_sgdma_init(struct mdlx_cdev *xcdev)
{
	cdev_init(&xcdev->cdev, &sgdma_fops);
}
