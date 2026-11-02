ifneq ($(KERNELRELEASE),)
obj-m := dot_accel_drv.o
ccflags-y += -I$(src)/../include
else
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KDIR ?= /home/projects/gerstl/ece382m/linux-xlnx
.PHONY: all check
all: check
	$(MAKE) -C "$(KDIR)" M="$(CURDIR)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
check:
	@test -n "$(KDIR)" -a -f "$(KDIR)/Makefile" || { echo 'Set KDIR to the matching prepared ARM Linux kernel build tree.'; exit 1; }
endif
