ifneq ($(KERNELRELEASE),)
obj-m := pcie_driver.o
obj-m += mdio_rw.o
else

KDIR=/home/tronlong/rk3568/rk3/rk356x_linux_release_v1.3.1_20221120/kernel
CROSS_COMPILE=/home/tronlong/rk3568/rk3/rk356x_linux_release_v1.3.1_20221120/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

all: modules clean_intermediates

modules:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE)

clean_intermediates:
	rm -f *.o *.mod.o *.mod.c .*.cmd .tmp_versions/* *.symvers Module.symvers modules.order

clean: clean_intermediates
	rm -f *.ko

help:
	@echo "make KDIR=<your kernel path> CROSS_COMPILE=<your CROSS_COMPILE>"
endif