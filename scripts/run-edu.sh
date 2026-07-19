#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

qemu/build/qemu-system-x86_64 \
    -machine q35 \
    -cpu max \
    -m 1024M \
    -smp 2 \
    -kernel linux/build/arch/x86/boot/bzImage \
    -initrd buildroot/build/images/rootfs.cpio.gz \
    -append "console=ttyS0 earlyprintk=serial nokaslr panic=-1" \
    -nographic \
    -no-reboot
