SHELL = /bin/bash
ifneq ($(xvc_bar_num),)
	XVC_FLAGS += -D__XVC_BAR_NUM__=$(xvc_bar_num)
endif

ifneq ($(xvc_bar_offset),)
	XVC_FLAGS += -D__XVC_BAR_OFFSET__=$(xvc_bar_offset)
endif

$(warning XVC_FLAGS: $(XVC_FLAGS).)

topdir := $(shell cd $(src) && pwd)

TARGET_MODULE:=mdlx

EXTRA_CFLAGS := -I$(topdir)/include $(XVC_FLAGS)
#EXTRA_CFLAGS += -D__LIBMDLX_DEBUG__
#EXTRA_CFLAGS += -DINTERNAL_TESTING

ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := src/libmdlx.o src/mdlx_cdev.o src/cdev_ctrl.o src/cdev_events.o src/cdev_sgdma.o src/cdev_xvc.o src/cdev_bypass.o src/mdlx_mod.o src/mdlx_thread.o
	obj-m := $(TARGET_MODULE).o
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	PWD:=$(shell pwd)
all :
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean

install: all
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules_install

endif
