ifneq ($(KERNELRELEASE),)

obj-m += edu_drv.o
edu_drv-y := driver/edu_drv.o
ccflags-y += -I$(src)/include

else

PROJECT_ROOT := $(abspath $(CURDIR)/..)
KDIR := $(PROJECT_ROOT)/linux/build
ROOTFS := $(PROJECT_ROOT)/buildroot/build/target
BUILDROOT := $(PROJECT_ROOT)/buildroot
RUN_QEMU := $(PROJECT_ROOT)/scripts/run-edu.sh
TARGET_CC := $(BUILDROOT)/build/host/bin/x86_64-buildroot-linux-gnu-gcc
USER_TEST := user_test/edu_usertest

.PHONY: all module user_test install run clean

all: module user_test

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

user_test: $(USER_TEST)

$(USER_TEST): user_test/edu_test.c include/edu_uapi.h
	@test -x $(TARGET_CC) || \
		(echo "error: run ./scripts/build-rootfs.sh first"; exit 1)
	$(TARGET_CC) \
		-Wall -Wextra -O2 \
		-I$(CURDIR)/include \
		-o $@ $<

install: all
	@test -d $(ROOTFS)/root || \
		(echo "error: run ./scripts/build-rootfs.sh first"; exit 1)
	install -m 0644 edu_drv.ko $(ROOTFS)/root/edu_drv.ko
	install -m 0755 $(USER_TEST) $(ROOTFS)/root/edu_usertest
	$(MAKE) -C $(BUILDROOT)/src O=../build rootfs-cpio

run: install
	$(RUN_QEMU)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f $(USER_TEST)

endif
