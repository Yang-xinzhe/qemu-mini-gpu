#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/../qemu"

mkdir -p build
find build -mindepth 1 ! -name README.md -delete
cd build

../src/configure \
    --target-list=x86_64-softmmu \
    --enable-debug \
    --enable-debug-info \
    --disable-werror

ninja -j"$(nproc)"
