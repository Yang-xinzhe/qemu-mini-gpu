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
- 提供共享 fd 和并发 open/close 两种多线程压力测试；
- 使用 MSI 和 completion 实现异步阶乘 ioctl；
- 验证 `5! = 120` 的 IRQ 唤醒和用户态返回链路；
- 完成 probe 失败回滚以及 remove 时的 IRQ、MSI、BAR0 和 PCI 资源释放。

尚未完成：

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

### 阶乘和 MSI 中断测试

阶乘 ioctl 会使能 EDU 的 factorial IRQ、启动计算，并通过 completion 让调用进程睡眠等待。QEMU 完成计算后发送 MSI，IRQ handler 读取并 ACK 中断，随后唤醒 ioctl 返回结果。

```sh
/root/edu_usertest factorial 5
```

成功输出：

```text
[TEST] ioctl FACTORIAL input=5 timeout=1000 ms
[PASS] 5! = 120
[PASS] test completed
```

输入范围为 0 到 12，因为 `13!` 已经超过 32 位无符号整数。驱动接受的等待时间范围为 1 到 10000 毫秒；设备仍在计算时，新请求返回 `EBUSY`。

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
edu_usertest factorial <input> [device]
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
/root/edu_usertest factorial 5
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
| `EDU_IOCTL_FACTORIAL` | 提交阶乘输入并等待 IRQ 返回结果 | `struct edu_factorial` | 已实现 |

驱动遇到未知 ioctl 时返回 `ENOTTY`。

## 已验证的寄存器

| 偏移 | 名称 | 行为 |
| --- | --- | --- |
| `0x00` | Identification | 只读，预期值为 `0x010000ed` |
| `0x04` | Liveness | 写入一个 32 位值，读回其按位取反结果 |
| `0x08` | Factorial | 写入触发阶乘计算，通过 `EDU_IOCTL_FACTORIAL` 等待完成 |
| `0x20` | Status | bit 0 表示正在计算，bit 7 使能 factorial IRQ |
| `0x24` | IRQ status | bit 0 表示 factorial 完成中断 |
| `0x64` | IRQ acknowledge | 写入对应状态位清除 pending IRQ |

不要把所有寄存器都当作普通内存进行“写入后原值读回”。EDU 的不同寄存器具有不同语义，部分寄存器还会触发中断或 DMA 等副作用。

压力测试选择只读的 identification 寄存器作为并发一致性检查。`0x04` liveness 的一次写入和随后一次读取是两个独立 ioctl，其他线程可以在两者之间写入新值，因此不能用它判断每个线程是否读回了自己的写入结果。

## 本阶段问题与修复记录

### factorial 一直返回 `ETIMEDOUT`

现象是阶乘寄存器已经得到 `5! = 120`，`0x20` 的 IRQFACT 位也已经使能，但 `/proc/interrupts` 中 `edu_drv` 的 MSI 计数始终为 0。

根因是 probe 只调用了 `pci_enable_device()`，没有调用 `pci_set_master()`。MSI 消息没有正常投递到 CPU，因此 IRQ handler 没有进入，completion 也无法唤醒 ioctl。现在驱动会在启用 PCI 设备后调用 `pci_set_master()`，并在失败回滚和 remove 时调用 `pci_clear_master()`。

### 用户态始终打印 factorial 结果为 0

最初测试程序把结构体初始化时使用的局部变量 `result` 打印出来，而 ioctl 真正写回的是 `factorial.result`；同时没有检查 ioctl 返回值，所以 timeout 也会被掩盖。现在测试程序检查 ioctl 错误、读取 `factorial.result`，并自行计算预期结果进行比较。

### probe 失败时资源没有完整回滚

字符设备注册失败后曾经直接返回，导致 IRQ handler、MSI vector、BAR0 映射和 PCI 资源泄漏；错误路径还曾将 BAR 编号 `0` 错当成 `pci_iounmap()` 的映射地址。现在字符设备最后发布，所有失败标签按照资源申请的相反顺序执行：

```text
free_irq
→ pci_free_irq_vectors
→ pci_iounmap
→ pci_release_regions
→ pci_clear_master
→ pci_disable_device
```

### remove 中先解除 BAR0 映射

旧逻辑在 `free_irq()` 之前解除 BAR0 映射，handler 或后续的 `readl()`/`writel()` 可能访问已经失效的 MMIO 地址。现在 remove 会先注销字符设备、关闭并 ACK 设备中断，再释放 IRQ 和 MSI vector，最后才解除 BAR0 映射并关闭 PCI 设备。

### timeout 后的迟到 IRQ

等待超时不等于硬件任务已取消。旧任务完成后产生的迟到 IRQ 可能污染下一次请求。当前驱动在启动任务前清理旧 IRQ/completion 状态，在 timeout 或信号中断时关闭 factorial IRQ、同步 handler、ACK pending 状态，并在新任务开始前检查 `EDU_STATUS_COMPUTING`。

当前驱动面向固定的 QEMU EDU 教学设备和正常模块加载/卸载流程，尚未实现 PCI 热拔插期间仍有用户 fd 打开的完整生命周期管理。

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
