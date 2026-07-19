# EDU PCI 驱动实验

这个目录记录 QEMU EDU PCI 设备对应的 Linux 驱动、用户态 ABI 和测试程序。目前已经打通以下链路：

```text
QEMU EDU PCI 设备
        ↓ BAR0 MMIO
Linux PCI 驱动 edu_drv.ko
        ↓ /dev/edu_gpu0 + ioctl
用户态测试程序 edu_usertest
```

## 当前进度

已经完成：

- 匹配 EDU PCI 设备：Vendor ID `0x1234`、Device ID `0x11e8`；
- 启用 PCI 设备并申请 BAR 资源；
- 映射 BAR0 MMIO 空间；
- 注册字符设备 `/dev/edu_gpu0`；
- 实现寄存器读取和写入 ioctl；
- 检查寄存器偏移是否越界并满足 4 字节对齐；
- 使用 mutex 串行化 ioctl 操作；
- 提供 Buildroot 用户态测试程序；
- 验证 identification、liveness 和非法 ioctl 路径；
- 提供共享 fd 和并发 open/close 两种多线程压力测试。

尚未完成：

- `EDU_IOCTL_FACTORIAL` 的驱动实现；
- 中断处理；
- DMA 数据传输；
- 自动化加载驱动和执行测试。

## 目录结构

```text
edu-driver/
├── driver/edu_drv.c       Linux PCI 字符设备驱动
├── include/edu_uapi.h     内核和用户态共用的 ioctl ABI
├── user_test/edu_test.c   用户态测试程序
├── Makefile               编译、安装、打包和启动入口
└── README.md              本文档
```

## 前置条件

先在仓库根目录构建 Linux 内核和 Buildroot rootfs：

```sh
./scripts/build-kernel.sh
./scripts/build-rootfs.sh
```

驱动使用 `linux/build` 中的内核构建目录。用户态程序使用 Buildroot 生成的交叉编译器，因此不依赖宿主机的 libc。

## 编译

以下命令均在仓库根目录执行。

同时编译内核模块和用户态测试程序：

```sh
make -C edu-driver
```

也可以分别编译：

```sh
make -C edu-driver module
make -C edu-driver user_test
```

生成的文件为：

```text
edu-driver/edu_drv.ko
edu-driver/user_test/edu_usertest
```

清理生成文件：

```sh
make -C edu-driver clean
```

## 安装到 initramfs

下面的命令会完成三件事：

1. 编译 `edu_drv.ko` 和 `edu_usertest`；
2. 将它们复制到 `buildroot/build/target/root/`；
3. 重新生成 `buildroot/build/images/rootfs.cpio.gz`。

```sh
make -C edu-driver install
```

安装后的文件位置为：

```text
/root/edu_drv.ko
/root/edu_usertest
```

这里的路径是虚拟机启动后的 rootfs 路径。

## 启动和测试

一条命令完成编译、安装、重新打包和启动 QEMU：

```sh
make -C edu-driver run
```

进入虚拟机后加载驱动：

```sh
insmod /root/edu_drv.ko
```

确认 PCI 设备、驱动和字符设备节点：

```sh
lspci -nn -d 1234:11e8
lsmod | grep edu_drv
ls -l /dev/edu_gpu0
```

运行完整的基础测试：

```sh
/root/edu_usertest all 0x04 0x12345678
```

测试内容包括：

- 打开 `/dev/edu_gpu0`；
- 读取 `0x00` identification 寄存器并校验 `0x010000ed`；
- 发送驱动不支持的 ioctl 并校验返回 `ENOTTY`；
- 向 `0x04` liveness 寄存器写入 `0x12345678`；
- 读回并校验结果为按位取反后的 `0xedcba987`。

成功时可以看到：

```text
[PASS] identification matches QEMU edu device
[PASS] unsupported ioctl returned ENOTTY
[PASS] write ioctl succeeded
[PASS] liveness register returned the complemented value
[PASS] test completed
```

### 多线程压力测试

多个线程共享同一个 fd，并发读取 identification 寄存器，同时定期检查非法 ioctl 是否稳定返回 `ENOTTY`：

```sh
/root/edu_usertest stress 8 10000
```

每个线程独立、反复地执行 `open`、读取 identification 和 `close`：

```sh
/root/edu_usertest open-stress 8 1000
```

两个数字参数依次为线程数和每个线程的循环次数。线程数范围为 1 到 256，循环次数必须大于 0。压力循环本身不会逐条打印 ioctl，以免串口日志影响测试速度；结束时会统一输出：

```text
[RESULT] successful ioctls:   ...
[RESULT] expected failures:   ...
[RESULT] unexpected failures: 0
[RESULT] invalid values:      0
```

只有 `unexpected failures` 和 `invalid values` 都是 0，测试才会返回成功。`expected failures` 来自故意发送的非法 ioctl，不代表测试失败。

驱动的逐次 ioctl 日志使用 `dev_dbg()`，默认不会刷入控制台。需要排查单次请求时，可以通过 Linux dynamic debug 临时启用 `edu_drv.c` 的调试日志。

卸载驱动：

```sh
rmmod edu_drv
```

退出 QEMU：先按 `Ctrl+A`，松开后再按 `X`。

## edu_usertest 命令

默认设备节点是 `/dev/edu_gpu0`，也可以在每条命令末尾指定其他设备节点。

```text
edu_usertest open [device]
edu_usertest read <offset> [device]
edu_usertest write <offset> <value> [device]
edu_usertest invalid [device]
edu_usertest all <offset> <value> [device]
edu_usertest stress <threads> <iterations> [device]
edu_usertest open-stress <threads> <iterations> [device]
```

示例：

```sh
/root/edu_usertest open
/root/edu_usertest read 0x00
/root/edu_usertest write 0x04 0x12345678
/root/edu_usertest invalid
/root/edu_usertest all 0x04 0x12345678
/root/edu_usertest stress 8 10000
/root/edu_usertest open-stress 8 1000
```

数字参数支持十进制和以 `0x` 开头的十六进制格式。

## 当前 ioctl ABI

公共定义位于 `include/edu_uapi.h`：

| ioctl | 方向 | 参数 | 当前状态 |
| --- | --- | --- | --- |
| `EDU_IOCTL_REG_READ` | 内核读 BAR0，结果返回用户态 | `struct edu_reg_io` | 已实现 |
| `EDU_IOCTL_REG_WRITE` | 用户态写 BAR0 | `struct edu_reg_io` | 已实现 |
| `EDU_IOCTL_FACTORIAL` | 提交阶乘输入并获取结果 | `struct edu_factorial` | 仅定义，尚未实现 |

驱动遇到未知 ioctl 或尚未实现的 ioctl 时返回 `ENOTTY`。

## 已验证的寄存器

| 偏移 | 名称 | 行为 |
| --- | --- | --- |
| `0x00` | Identification | 只读，预期值为 `0x010000ed` |
| `0x04` | Liveness | 写入一个 32 位值，读回其按位取反结果 |
| `0x08` | Factorial | 写入会触发阶乘计算，目前还没有对应的高级 ioctl 流程 |

不要把所有寄存器都当作普通内存进行“写入后原值读回”。EDU 的不同寄存器具有不同语义，部分寄存器还会触发中断或 DMA 等副作用。

压力测试选择只读的 identification 寄存器作为并发一致性检查。`0x04` liveness 的一次写入和随后一次读取是两个独立 ioctl，其他线程可以在两者之间写入新值，因此不能用它判断每个线程是否读回了自己的写入结果。

## 串口日志交错

用户程序的标准输出和内核 `dev_info()` 日志都会写入同一个串口控制台，因此偶尔会出现两段输出交错。这不代表 ioctl 执行失败，应以程序最后的 `[PASS]` 或 `[FAIL]` 结果为准。

需要临时减少内核控制台日志时，可以在虚拟机中执行：

```sh
dmesg -n 4
```

恢复较详细的日志：

```sh
dmesg -n 7
```
