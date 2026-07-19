#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/../buildroot"

if [ ! -f build/.config ]; then
    make -C src \
        O=../build \
        BR2_DEFCONFIG=../configs/mini_gpu_defconfig \
        defconfig
fi

make -C src \
    O=../build \
    -j"$(nproc)"
