#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/../linux"

if [ ! -f build/.config ]; then
    mkdir -p build
    cp configs/mini_gpu_defconfig build/.config
    make -C src \
        O="$PWD/build" \
        olddefconfig
fi

make -C src \
    O="$PWD/build" \
    -j"$(nproc)"
