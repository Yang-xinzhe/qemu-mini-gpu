obj-m += edu_drv.o

PROJECT_ROOT := $(abspath $(CURDIR)/..)
KDIR := $(PROJECT_ROOT)/linux/build
ROOTFS := $(PROJECT_ROOT)/buildroot/build/target
BUILDROOT := $(PROJECT_ROOT)/buildroot
RUN_QEMU := $(PROJECT_ROOT)/scripts/run-edu.sh

.PHONY: all install run clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

install: all
	@test -d $(ROOTFS)/root || \
			(echo "error: run ./scripts/build-rootfs.sh first"; exit 1)
	install -m 0644 edu_drv.ko $(ROOTFS)/root/edu_drv.ko
	$(MAKE) -C $(BUILDROOT)/src O=../build rootfs-cpio

run: install
	$(RUN_QEMU)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean