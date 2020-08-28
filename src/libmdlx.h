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

#ifndef MDLX_LIB_H
#define MDLX_LIB_H

#include <linux/version.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#if	KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
#include <linux/swait.h>
#endif
/*
 *  if the config bar is fixed, the driver does not neeed to search through
 *  all of the bars
 */
//#define MDLX_CONFIG_BAR_NUM	1

//#define __LIBMDLX_DEBUG__

/* Switch debug printing on/off */
#define MDLX_DEBUG 0

/* SECTION: Preprocessor macros/constants */
#define MDLX_BAR_NUM (6)

/* maximum amount of register space to map */
#define MDLX_BAR_SIZE (0x8000UL)

/* Use this definition to poll several times between calls to schedule */
#define NUM_POLLS_PER_SCHED 100

#define MDLX_CHANNEL_NUM_MAX (4)
/*
 * interrupts per engine, rad2_vul.sv:237
 * .REG_IRQ_OUT	(reg_irq_from_ch[(channel*2) +: 2]),
 */
#define MDLX_ENG_IRQ_NUM (1)
#define MAX_EXTRA_ADJ (15)
#define RX_STATUS_EOP (1)

/* Target internal components on MDLX control BAR */
#define MDLX_OFS_INT_CTRL	(0x2000UL)
#define MDLX_OFS_CONFIG		(0x3000UL)

/* maximum number of desc per transfer request */
#define MDLX_TRANSFER_MAX_DESC (2048)

/* maximum size of a single DMA transfer descriptor */
#define MDLX_DESC_BLEN_BITS	28
#define MDLX_DESC_BLEN_MAX	((1 << (MDLX_DESC_BLEN_BITS)) - 1)

/* bits of the SG DMA control register */
#define MDLX_CTRL_RUN_STOP			(1UL << 0)
#define MDLX_CTRL_IE_DESC_STOPPED		(1UL << 1)
#define MDLX_CTRL_IE_DESC_COMPLETED		(1UL << 2)
#define MDLX_CTRL_IE_DESC_ALIGN_MISMATCH	(1UL << 3)
#define MDLX_CTRL_IE_MAGIC_STOPPED		(1UL << 4)
#define MDLX_CTRL_IE_IDLE_STOPPED		(1UL << 6)
#define MDLX_CTRL_IE_READ_ERROR			(0x1FUL << 9)
#define MDLX_CTRL_IE_DESC_ERROR			(0x1FUL << 19)
#define MDLX_CTRL_NON_INCR_ADDR			(1UL << 25)
#define MDLX_CTRL_POLL_MODE_WB			(1UL << 26)
#define MDLX_CTRL_STM_MODE_WB			(1UL << 27)

/* bits of the SG DMA status register */
#define MDLX_STAT_BUSY			(1UL << 0)
#define MDLX_STAT_DESC_STOPPED		(1UL << 1)
#define MDLX_STAT_DESC_COMPLETED	(1UL << 2)
#define MDLX_STAT_ALIGN_MISMATCH	(1UL << 3)
#define MDLX_STAT_MAGIC_STOPPED		(1UL << 4)
#define MDLX_STAT_INVALID_LEN		(1UL << 5)
#define MDLX_STAT_IDLE_STOPPED		(1UL << 6)

#define MDLX_STAT_COMMON_ERR_MASK \
	(MDLX_STAT_ALIGN_MISMATCH | MDLX_STAT_MAGIC_STOPPED | \
	 MDLX_STAT_INVALID_LEN)

/* desc_error, C2H & H2C */
#define MDLX_STAT_DESC_UNSUPP_REQ	(1UL << 19)
#define MDLX_STAT_DESC_COMPL_ABORT	(1UL << 20)
#define MDLX_STAT_DESC_PARITY_ERR	(1UL << 21)
#define MDLX_STAT_DESC_HEADER_EP	(1UL << 22)
#define MDLX_STAT_DESC_UNEXP_COMPL	(1UL << 23)

#define MDLX_STAT_DESC_ERR_MASK	\
	(MDLX_STAT_DESC_UNSUPP_REQ | MDLX_STAT_DESC_COMPL_ABORT | \
	 MDLX_STAT_DESC_PARITY_ERR | MDLX_STAT_DESC_HEADER_EP | \
	 MDLX_STAT_DESC_UNEXP_COMPL)

/* read error: H2C */
#define MDLX_STAT_H2C_R_UNSUPP_REQ	(1UL << 9)
#define MDLX_STAT_H2C_R_COMPL_ABORT	(1UL << 10)
#define MDLX_STAT_H2C_R_PARITY_ERR	(1UL << 11)
#define MDLX_STAT_H2C_R_HEADER_EP	(1UL << 12)
#define MDLX_STAT_H2C_R_UNEXP_COMPL	(1UL << 13)

#define MDLX_STAT_H2C_R_ERR_MASK	\
	(MDLX_STAT_H2C_R_UNSUPP_REQ | MDLX_STAT_H2C_R_COMPL_ABORT | \
	 MDLX_STAT_H2C_R_PARITY_ERR | MDLX_STAT_H2C_R_HEADER_EP | \
	 MDLX_STAT_H2C_R_UNEXP_COMPL)

/* write error, H2C only */
#define MDLX_STAT_H2C_W_DECODE_ERR	(1UL << 14)
#define MDLX_STAT_H2C_W_SLAVE_ERR	(1UL << 15)

#define MDLX_STAT_H2C_W_ERR_MASK	\
	(MDLX_STAT_H2C_W_DECODE_ERR | MDLX_STAT_H2C_W_SLAVE_ERR)

/* read error: C2H */
#define MDLX_STAT_C2H_R_DECODE_ERR	(1UL << 9)
#define MDLX_STAT_C2H_R_SLAVE_ERR	(1UL << 10)

#define MDLX_STAT_C2H_R_ERR_MASK	\
	(MDLX_STAT_C2H_R_DECODE_ERR | MDLX_STAT_C2H_R_SLAVE_ERR)

/* all combined */
#define MDLX_STAT_H2C_ERR_MASK	\
	(MDLX_STAT_COMMON_ERR_MASK | MDLX_STAT_DESC_ERR_MASK | \
	 MDLX_STAT_H2C_R_ERR_MASK | MDLX_STAT_H2C_W_ERR_MASK)

#define MDLX_STAT_C2H_ERR_MASK	\
	(MDLX_STAT_COMMON_ERR_MASK | MDLX_STAT_DESC_ERR_MASK | \
	 MDLX_STAT_C2H_R_ERR_MASK)

/* bits of the SGDMA descriptor control field */
#define MDLX_DESC_STOPPED	(1UL << 0)
#define MDLX_DESC_COMPLETED	(1UL << 1)
#define MDLX_DESC_EOP		(1UL << 4)

#define MDLX_PERF_RUN	(1UL << 0)
#define MDLX_PERF_CLEAR	(1UL << 1)
#define MDLX_PERF_AUTO	(1UL << 2)

#define MAGIC_ENGINE	0xEEEEEEEEUL
#define MAGIC_DEVICE	0xDDDDDDDDUL

/* upper 16-bits of engine identifier register */
#define MDLX_ID_H2C 0x1fc0U
#define MDLX_ID_C2H 0x1fc1U

/* for C2H AXI-ST mode */
#define CYCLIC_RX_PAGES_MAX	256

#define LS_BYTE_MASK 0x000000FFUL

#define BLOCK_ID_MASK 0xFFF00000
#define BLOCK_ID_HEAD 0x1FC00000

#define IRQ_BLOCK_ID 0x1fc20000UL
#define CONFIG_BLOCK_ID 0x1fc30000UL

#define WB_COUNT_MASK 0x00ffffffUL
#define WB_ERR_MASK (1UL << 31)
#define POLL_TIMEOUT_SECONDS 10

#define MAX_USER_IRQ 0

#define MAX_DESC_BUS_ADDR (0xffffffffULL)

#define DESC_MAGIC 0xAD4B0000UL

#define C2H_WB 0x52B4UL

#define MAX_NUM_ENGINES (MDLX_CHANNEL_NUM_MAX * 2)
#define H2C_CHANNEL_OFFSET 0x1000
#define SGDMA_OFFSET_FROM_CHANNEL 0x4000
#define CHANNEL_SPACING 0x100
#define TARGET_SPACING 0x1000

#define BYPASS_MODE_SPACING 0x0100

/* obtain the 32 most significant (high) bits of a 32-bit or 64-bit address */
#define PCI_DMA_H(addr) ((addr >> 16) >> 16)
/* obtain the 32 least significant (low) bits of a 32-bit or 64-bit address */
#define PCI_DMA_L(addr) (addr & 0xffffffffUL)

#ifndef VM_RESERVED
	#define VMEM_FLAGS (VM_IO | VM_DONTEXPAND | VM_DONTDUMP)
#else
	#define VMEM_FLAGS (VM_IO | VM_RESERVED)
#endif

#ifdef __LIBMDLX_DEBUG__
#define dbg_io		pr_err
#define dbg_fops	pr_err
#define dbg_perf	pr_err
#define dbg_sg		pr_err
#define dbg_tfr		pr_err
#define dbg_irq		pr_err
#define dbg_init	pr_err
#define dbg_desc	pr_err
#else
/* disable debugging */
#define dbg_io(...)
#define dbg_fops(...)
#define dbg_perf(...)
#define dbg_sg(...)
#define dbg_tfr(...)
#define dbg_irq(...)
#define dbg_init(...)
#define dbg_desc(...)
#endif

/* SECTION: Enum definitions */
enum transfer_state {
	TRANSFER_STATE_NEW = 0,
	TRANSFER_STATE_SUBMITTED,
	TRANSFER_STATE_COMPLETED,
	TRANSFER_STATE_FAILED,
	TRANSFER_STATE_ABORTED
};

enum shutdown_state {
	ENGINE_SHUTDOWN_NONE = 0,	/* No shutdown in progress */
	ENGINE_SHUTDOWN_REQUEST = 1,	/* engine requested to shutdown */
	ENGINE_SHUTDOWN_IDLE = 2	/* engine has shutdown and is idle */
};

enum dev_capabilities {
	CAP_64BIT_DMA = 2,
	CAP_64BIT_DESC = 4,
	CAP_ENGINE_WRITE = 8,
	CAP_ENGINE_READ = 16
};

/* SECTION: Structure definitions */

struct mdlx_io_cb {
	void __user *buf;
	size_t len;
	void *private;
	unsigned int pages_nr;
	struct sg_table sgt;
	struct page **pages;
	/** total data size */
	unsigned int count;
	/** MM only, DDR/BRAM memory addr */
	u64 ep_addr;
	/** write: if write to the device */
	struct mdlx_request_cb *req;
	u8 write:1;
	void (*io_done)(unsigned long cb_hndl, int err);
};

struct config_regs {
	u32 identifier;
	u32 reserved_1[4];
	u32 msi_enable;
};

/**
 * SG DMA Controller status and control registers
 *
 * These registers make the control interface for DMA transfers.
 *
 * It sits in End Point (FPGA) memory BAR[0] for 32-bit or BAR[0:1] for 64-bit.
 * It references the first descriptor which exists in Root Complex (PC) memory.
 *
 * @note The registers must be accessed using 32-bit (PCI DWORD) read/writes,
 * and their values are in little-endian byte ordering.
 */
struct engine_regs {
	u32 identifier;
	u32 control;
	u32 control_w1s;
	u32 control_w1c;
	u32 reserved_1[12];	/* padding */

	u32 status;
	u32 status_rc;
	u32 completed_desc_count;
	u32 alignments;
	u32 reserved_2[14];	/* padding */

	u32 poll_mode_wb_lo;
	u32 poll_mode_wb_hi;
	u32 interrupt_enable_mask;
	u32 interrupt_enable_mask_w1s;
	u32 interrupt_enable_mask_w1c;
	u32 reserved_3[9];	/* padding */

	u32 perf_ctrl;
	u32 perf_cyc_lo;
	u32 perf_cyc_hi;
	u32 perf_dat_lo;
	u32 perf_dat_hi;
	u32 perf_pnd_lo;
	u32 perf_pnd_hi;
} __packed;

struct engine_sgdma_regs {
	u32 identifier;
	u32 reserved_1[31];	/* padding */

	/* bus address to first descriptor in Root Complex Memory */
	u32 first_desc_lo;
	u32 first_desc_hi;
	/* number of adjacent descriptors at first_desc */
	u32 first_desc_adjacent;
	u32 credits;
} __packed;

struct msix_vec_table_entry {
	u32 msi_vec_addr_lo;
	u32 msi_vec_addr_hi;
	u32 msi_vec_data_lo;
	u32 msi_vec_data_hi;
} __packed;

struct msix_vec_table {
	struct msix_vec_table_entry entry_list[32];
} __packed;

struct interrupt_regs {
	u32 identifier;
	u32 user_int_enable;
	u32 user_int_enable_w1s;
	u32 user_int_enable_w1c;
	u32 channel_int_enable;
	u32 channel_int_enable_w1s;
	u32 channel_int_enable_w1c;
	u32 reserved_1[9];	/* padding */

	u32 user_int_request;
	u32 channel_int_request;
	u32 user_int_pending;
	u32 channel_int_pending;
	u32 reserved_2[12];	/* padding */

	u32 user_msi_vector[8];
	u32 channel_msi_vector[8];
} __packed;

struct sgdma_common_regs {
	u32 padding[8];
	u32 credit_mode_enable;
	u32 credit_mode_enable_w1s;
	u32 credit_mode_enable_w1c;
} __packed;


/* Structure for polled mode descriptor writeback */
struct mdlx_poll_wb {
	u32 completed_desc_count;
	u32 reserved_1[7];
} __packed;


/**
 * Descriptor for a single contiguous memory block transfer.
 *
 * Multiple descriptors are linked by means of the next pointer. An additional
 * extra adjacent number gives the amount of extra contiguous descriptors.
 *
 * The descriptors are in root complex memory, and the bytes in the 32-bit
 * words must be in little-endian byte ordering.
 */
struct mdlx_desc {
	u32 control;
	u32 bytes;		/* transfer length in bytes */
	u32 src_addr_lo;	/* source address (low 32-bit) */
	u32 src_addr_hi;	/* source address (high 32-bit) */
	u32 dst_addr_lo;	/* destination address (low 32-bit) */
	u32 dst_addr_hi;	/* destination address (high 32-bit) */
	/*
	 * next descriptor in the single-linked list of descriptors;
	 * this is the PCIe (bus) address of the next descriptor in the
	 * root complex memory
	 */
	u32 next_lo;		/* next desc address (low 32-bit) */
	u32 next_hi;		/* next desc address (high 32-bit) */
} __packed;

/* 32 bytes (four 32-bit words) or 64 bytes (eight 32-bit words) */
struct mdlx_result {
	u32 status;
	u32 length;
	u32 reserved_1[6];	/* padding */
} __packed;

struct sw_desc {
	dma_addr_t addr;
	unsigned int len;
};

/* Describes a (SG DMA) single transfer for the engine */
struct mdlx_transfer {
	struct list_head entry;		/* queue of non-completed transfers */
	struct mdlx_desc *desc_virt;	/* virt addr of the 1st descriptor */
	struct mdlx_result *res_virt; /* virt addr of result for c2h streaming */
	dma_addr_t res_bus;			  /* bus addr for result descriptors */
	dma_addr_t desc_bus;		/* bus addr of the first descriptor */
	int desc_adjacent;		/* adjacent descriptors at desc_bus */
	int desc_num;			/* number of descriptors in transfer */
	int desc_index;			/* index for first descriptor in transfer */
	enum dma_data_direction dir;
#if	KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	struct swait_queue_head wq;
#else
	wait_queue_head_t wq;		/* wait queue for transfer completion */
#endif

	enum transfer_state state;	/* state of the transfer */
	unsigned int flags;
#define XFER_FLAG_NEED_UNMAP	0x1
	int cyclic;			/* flag if transfer is cyclic */
	int last_in_request;		/* flag if last within request */
	unsigned int len;
	struct sg_table *sgt;
	struct mdlx_io_cb *cb;
};

struct mdlx_request_cb {
	struct sg_table *sgt;
	unsigned int total_len;
	u64 ep_addr;

	struct mdlx_transfer tfer[2]; /* Use two transfers in case single request needs to be split */
	struct mdlx_io_cb *cb;

	unsigned int sw_desc_idx;
	unsigned int sw_desc_cnt;
	struct sw_desc sdesc[0];
};

struct mdlx_engine {
	unsigned long magic;	/* structure ID for sanity checks */
	struct mdlx_dev *mdev;	/* parent device */
	char name[5];		/* name of this engine */
	int version;		/* version of this engine */
	//dev_t cdevno;		/* character device major:minor */
	//struct cdev cdev;	/* character device (embedded struct) */

	/* HW register address offsets */
	struct engine_regs *regs;		/* Control reg BAR offset */
	struct engine_sgdma_regs *sgdma_regs;	/* SGDAM reg BAR offset */
	u32 bypass_offset;			/* Bypass mode BAR offset */

	/* Engine state, configuration and flags */
	enum shutdown_state shutdown;	/* engine shutdown mode */
	enum dma_data_direction dir;
	int device_open;	/* flag if engine node open, ST mode only */
	int running;		/* flag if the driver started engine */
	int non_incr_addr;	/* flag if non-incremental addressing used */
	int streaming;
	int addr_align;		/* source/dest alignment in bytes */
	int len_granularity;	/* transfer length multiple */
	int addr_bits;		/* HW datapath address width */
	int channel;		/* engine indices */
	int max_extra_adj;	/* descriptor prefetch capability */
	int desc_dequeued;	/* num descriptors of completed transfers */
	u32 status;		/* last known status of device */
	/* only used for MSIX mode to store per-engine interrupt mask value */
	u32 interrupt_enable_mask_value;

	/* Transfer list management */
	struct list_head transfer_list;	/* queue of transfers */

	/* Members applicable to AXI-ST C2H (cyclic) transfers */
	struct mdlx_result *cyclic_result;
	dma_addr_t cyclic_result_bus;	/* bus addr for transfer */
	struct mdlx_request_cb *cyclic_req;
	struct sg_table cyclic_sgt;
    u8 *perf_buf_virt;
    dma_addr_t perf_buf_bus; /* bus address */
	u8 eop_found; /* used only for cyclic(rx:c2h) */
	int eop_count;
	int rx_tail;	/* follows the HW */
	int rx_head;	/* where the SW reads from */
	int rx_overrun;	/* flag if overrun occured */

	/* for copy from cyclic buffer to user buffer */
	unsigned int user_buffer_index;

	/* Members associated with polled mode support */
	u8 *poll_mode_addr_virt;	/* virt addr for descriptor writeback */
	dma_addr_t poll_mode_bus;	/* bus addr for descriptor writeback */

	/* Members associated with interrupt mode support */
#if	KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	struct swait_queue_head shutdown_wq;
#else
	wait_queue_head_t shutdown_wq;	/* wait queue for shutdown sync */
#endif
	spinlock_t lock;		/* protects concurrent access */
	int prev_cpu;			/* remember CPU# of (last) locker */
	int msix_irq_line;		/* MSI-X vector for this engine */
	u32 irq_bitmask;		/* IRQ bit mask for this engine */
	struct work_struct work;	/* Work queue for interrupt handling */

	struct mutex desc_lock;		/* protects concurrent access */
	dma_addr_t desc_bus;
	struct mdlx_desc *desc;
	int desc_idx;			/* current descriptor index */
	int desc_used;			/* total descriptors used */

	/* for performance test support */
	struct mdlx_performance_ioctl *mdlx_perf;	/* perf test control */
#if	KERNEL_VERSION(4, 6, 0) <= LINUX_VERSION_CODE
	struct swait_queue_head mdlx_perf_wq;
#else
	wait_queue_head_t mdlx_perf_wq;	/* Perf test sync */
#endif

	struct mdlx_kthread *cmplthp;
	/* completion status thread list for the queue */
	struct list_head cmplthp_list;
	/* pending work thread list */
	/* cpu attached to intr_work */
	unsigned int intr_work_cpu;
};

struct mdlx_user_irq {
	struct mdlx_dev *mdev;		/* parent device */
	u8 user_idx;			/* 0 ~ 15 */
	u8 events_irq;			/* accumulated IRQs */
	spinlock_t events_lock;		/* lock to safely update events_irq */
	wait_queue_head_t events_wq;	/* wait queue to sync waiting threads */
	irq_handler_t handler;

	void *dev;
};

/* MDLX PCIe device specific book-keeping */
#define MDEV_FLAG_OFFLINE	0x1
struct mdlx_dev {
	struct list_head list_head;
	struct list_head rcu_node;

	unsigned long magic;		/* structure ID for sanity checks */
	struct pci_dev *pdev;	/* pci device struct from probe() */
	int idx;		/* dev index */

	const char *mod_name;		/* name of module owning the dev */

	spinlock_t lock;		/* protects concurrent access */
	unsigned int flags;

	/* PCIe BAR management */
	void __iomem *bar[MDLX_BAR_NUM];	/* addresses for mapped BARs */
	int user_bar_idx;	/* BAR index of user logic */
	int config_bar_idx;	/* BAR index of MDLX config logic */
	int bypass_bar_idx;	/* BAR index of MDLX bypass logic */
	int regions_in_use;	/* flag if dev was in use during probe() */
	int got_regions;	/* flag if probe() obtained the regions */

	int user_max;
	int c2h_channel_max;
	int h2c_channel_max;

	/* Interrupt management */
	int irq_count;		/* interrupt counter */
	int irq_line;		/* flag if irq allocated successfully */
	int msi_enabled;	/* flag if msi was enabled for the device */
	int msix_enabled;	/* flag if msi-x was enabled for the device */
#if KERNEL_VERSION(4, 12, 0) > LINUX_VERSION_CODE
	struct msix_entry entry[32];	/* msi-x vector/entry table */
#endif
	struct mdlx_user_irq user_irq[16];	/* user IRQ management */
	unsigned int mask_irq_user;

	/* MDLX engine management */
	int engines_num;	/* Total engine count */
	u32 mask_irq_h2c;
	u32 mask_irq_c2h;
	struct mdlx_engine engine_h2c[MDLX_CHANNEL_NUM_MAX];
	struct mdlx_engine engine_c2h[MDLX_CHANNEL_NUM_MAX];

	/* SD_Accel specific */
	enum dev_capabilities capabilities;
	u64 feature_id;
};

static inline int mdlx_device_flag_check(struct mdlx_dev *mdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->lock, flags);
	if (mdev->flags & f) {
		spin_unlock_irqrestore(&mdev->lock, flags);
		return 1;
	}
	spin_unlock_irqrestore(&mdev->lock, flags);
	return 0;
}

static inline int mdlx_device_flag_test_n_set(struct mdlx_dev *mdev,
					 unsigned int f)
{
	unsigned long flags;
	int rv = 0;

	spin_lock_irqsave(&mdev->lock, flags);
	if (mdev->flags & f) {
		spin_unlock_irqrestore(&mdev->lock, flags);
		rv = 1;
	} else
		mdev->flags |= f;
	spin_unlock_irqrestore(&mdev->lock, flags);
	return rv;
}

static inline void mdlx_device_flag_set(struct mdlx_dev *mdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->lock, flags);
	mdev->flags |= f;
	spin_unlock_irqrestore(&mdev->lock, flags);
}

static inline void mdlx_device_flag_clear(struct mdlx_dev *mdev, unsigned int f)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->lock, flags);
	mdev->flags &= ~f;
	spin_unlock_irqrestore(&mdev->lock, flags);
}

void write_register(u32 value, void *iomem);
u32 read_register(void *iomem);

struct mdlx_dev *mdev_find_by_pdev(struct pci_dev *pdev);

void mdlx_device_offline(struct pci_dev *pdev, void *dev_handle);
void mdlx_device_online(struct pci_dev *pdev, void *dev_handle);

int mdlx_performance_submit(struct mdlx_dev *mdev, struct mdlx_engine *engine);
struct mdlx_transfer *engine_cyclic_stop(struct mdlx_engine *engine);
void enable_perf(struct mdlx_engine *engine);
void get_perf_stats(struct mdlx_engine *engine);

int mdlx_cyclic_transfer_setup(struct mdlx_engine *engine);
int mdlx_cyclic_transfer_teardown(struct mdlx_engine *engine);
ssize_t mdlx_engine_read_cyclic(struct mdlx_engine *engine,
		char __user *buf, size_t count, int timeout_ms);
int engine_addrmode_set(struct mdlx_engine *engine, unsigned long arg);
int engine_service_poll(struct mdlx_engine *engine, u32 expected_desc_count);
#endif /* MDLX_LIB_H */
