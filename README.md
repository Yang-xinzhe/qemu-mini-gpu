# qemu-mini-gpu

一个用于学习 QEMU PCI 设备、Linux 内核驱动和用户态程序交互的最小 GPU 实验环境。

仓库使用以下组件：

- QEMU 10.0.3：运行 x86_64 虚拟机和承载模拟设备。
- Linux 7.2-rc3：独立构建实验内核。
- Buildroot 2025.02.x：生成基于 BusyBox 的 initramfs。

QEMU、Linux 和 Buildroot 源码以 Git submodule 固定版本，编译产物不会提交到仓库。

## 1. 安装依赖

下面的命令适用于 Debian、Ubuntu 和 WSL2 Ubuntu：

```sh
sudo apt update
sudo apt install -y \
    bc \
    bison \
    build-essential \
    ca-certificates \
    cpio \
    file \
    flex \
    git \
    libelf-dev \
    libglib2.0-dev \
    libncurses-dev \
    libpixman-1-dev \
    libssl-dev \
    ninja-build \
    pkg-config \
    python3 \
    python3-venv \
    rsync \
    unzip \
    wget
```

如果使用其他 Linux 发行版，请安装对应的软件包。当前构建和运行脚本面向 Linux 主机。

## 2. 获取源码

新 clone 仓库时，推荐同时拉取所有 submodule：

```sh
git clone --recurse-submodules https://github.com/Yang-xinzhe/qemu-mini-gpu.git
cd qemu-mini-gpu
```

如果已经 clone，但 `qemu/src`、`linux/src` 或 `buildroot/src` 是空目录，运行：

```sh
git submodule update --init --recursive
```

更新父仓库后，也建议再次执行该命令，确保 submodule 切换到父仓库记录的版本。

## 3. 构建

所有命令都在仓库根目录执行。

### 构建 QEMU

```sh
./scripts/build-qemu.sh
```

输出文件：

```text
qemu/build/qemu-system-x86_64
```

该脚本每次都会清理旧的 QEMU 构建产物，然后重新配置和编译。

### 构建 Linux 内核

```sh
./scripts/build-kernel.sh
```

输出文件：

```text
linux/build/arch/x86/boot/bzImage
```

第一次构建时，脚本会从 `linux/configs/mini_gpu_defconfig` 自动生成 `linux/build/.config`；后续执行为增量构建。

### 构建 initramfs

```sh
./scripts/build-rootfs.sh
```

输出文件：

```text
buildroot/build/images/rootfs.cpio.gz
```

第一次构建时，脚本会从 `buildroot/configs/mini_gpu_defconfig` 自动生成 Buildroot 配置。该配置关闭了 Buildroot 自带的 Linux 内核和 host-qemu，只生成实验所需的用户空间及 rootfs。

三个构建步骤互不依赖，可以分别执行；启动虚拟机前需要确保三个输出文件都已生成。

## 4. 启动虚拟机

```sh
./scripts/run-edu.sh
```

虚拟机使用：

- Q35 机器类型；
- 2 个虚拟 CPU；
- 1 GiB 内存；
- 独立构建的 Linux 内核；
- Buildroot 生成的 gzip 压缩 initramfs；
- 串口终端，无图形窗口。

退出 QEMU：

```text
Ctrl+A，然后按 X
```

## 5. 修改配置

### 修改 Linux 内核配置

```sh
make -C linux/src \
    O="$PWD/linux/build" \
    menuconfig
```

修改完成后，将精简配置保存回仓库：

```sh
make -C linux/src \
    O="$PWD/linux/build" \
    savedefconfig

cp linux/build/defconfig linux/configs/mini_gpu_defconfig
```

### 修改 Buildroot 配置

```sh
make -C buildroot/src \
    O=../build \
    menuconfig
```

修改完成后，将精简配置保存回仓库：

```sh
make -C buildroot/src \
    O=../build \
    BR2_DEFCONFIG="$PWD/buildroot/configs/mini_gpu_defconfig" \
    savedefconfig
```

不要提交 `linux/build/.config` 或 `buildroot/build/.config`；应当提交对应的 `mini_gpu_defconfig`。

## 6. 常见问题

### `qemu/src`、`linux/src` 或 `buildroot/src` 中没有源码

```sh
git submodule update --init --recursive
```

### 启动时提示找不到 QEMU、内核或 rootfs

确认三个构建步骤都已成功完成：

```sh
test -x qemu/build/qemu-system-x86_64
test -f linux/build/arch/x86/boot/bzImage
test -f buildroot/build/images/rootfs.cpio.gz
```

这些命令全部返回成功后，再运行：

```sh
./scripts/run-edu.sh
```

### 想重新进行完整构建

QEMU 构建脚本本身会清理旧产物。Linux 和 Buildroot 使用增量构建；需要彻底重建时，可以先删除对应的 build 目录，再重新执行脚本：

```sh
rm -rf linux/build
rm -rf buildroot/build

./scripts/build-kernel.sh
./scripts/build-rootfs.sh
```

构建目录中的内容都是生成产物，已由 `.gitignore` 排除。

## 目录说明

```text
qemu/src/              QEMU submodule
qemu/build/            QEMU 构建产物
linux/src/             Linux submodule
linux/configs/         可复现的内核配置
linux/build/           Linux 内核构建产物
buildroot/src/         Buildroot submodule
buildroot/configs/     可复现的 Buildroot 配置
buildroot/build/       Buildroot 构建产物
scripts/               构建和运行脚本
mini-gpu/              Mini GPU 设备、驱动、用户态程序和文档
```
