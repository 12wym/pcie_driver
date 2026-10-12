# 中断下半部 demo

示例以 Linux 5.15 普通内核（非 PREEMPT_RT）为基准。当前目录里的两个模块使用定时器每秒产生 10 个计数事件，无需 PCIe 硬件。定时器回调本身运行在 TIMER_SOFTIRQ；它用于演示从不能睡眠的上下文提交下半部，不是对真实硬中断的模拟。

原驱动和应用层接口未改动。示例未在目标 Linux 内核编译或加载；当前 workspace 的驱动 Makefile 指向另一台机器上的 RK3568 内核和交叉工具链，不能据此确认运行内核版本。

## tasklet 和工作队列模块

```bash
make
sudo insmod tasklet_demo.ko
sudo dmesg -w
# 另一个终端，结束示例
sudo rmmod tasklet_demo

sudo insmod workqueue_demo.ko
# 观察 dmesg，再结束示例
sudo rmmod workqueue_demo
```

使用指定的内核构建目录：

```bash
make KDIR=/path/to/configured/kernel/build
```

ARM64 交叉编译时应使用目标内核的构建配置和工具链：

```bash
make KDIR=/path/to/configured/kernel/build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

tasklet_demo 的回调通过 tasklet_setup() 注册；旧内核若没有该 API，可改为 tasklet_init()，回调签名相应改为 void callback(unsigned long data)。不要只改调用而不改回调签名。

工作队列示例使用 alloc_ordered_workqueue()，展示整个队列的串行执行。只有一个 work_struct 时没有必要专门使用 ordered 队列；真实驱动根据不同工作项是否需要全局顺序选择队列。普通队列可用 alloc_workqueue("name", 0, 0)，简单工作也可用 schedule_work() 提交到系统队列。

预期日志包含 processed 10 events；工作队列日志还包含 kworker 线程名。合并时具体每次计数可能不同。工作队列中的 msleep(20) 仅用于展示可睡眠上下文，不应为了延时而加到真实采集路径。

## 直接 softirq demo：需要内核源码改动

普通外部模块不能直接注册新 softirq：槽位在内核编译时固定，Linux 5.15 的 open_softirq() 没有导出给模块，也没有通用的注销接口。禁止覆盖 TIMER_SOFTIRQ、TASKLET_SOFTIRQ 等现有处理函数。

在一个用于实验的 Linux 5.15 内核树中：

1. 修改 include/linux/interrupt.h 的 softirq 枚举，在 RCU_SOFTIRQ 前插入 DEMO_SOFTIRQ，保持 RCU 最后：

```c
    HRTIMER_SOFTIRQ,
    DEMO_SOFTIRQ,
    RCU_SOFTIRQ,
    NR_SOFTIRQS
```

2. 修改 kernel/softirq.c 的名称表，顺序与枚举一致：

```c
const char * const softirq_to_name[NR_SOFTIRQS] = {
    "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "IRQ_POLL",
    "TASKLET", "SCHED", "HRTIMER", "DEMO", "RCU"
};
```

3. 将 softirq_builtin_demo.c 复制到内核树 kernel/ 目录，在 kernel/Makefile 加入：

```make
obj-y += softirq_builtin_demo.o
```

4. 按目标平台流程构建并启动该实验内核。启动日志应出现 softirq_demo，/proc/softirqs 包含 DEMO 行。本例只在启动时提交 10 个事件，使用 per-CPU 原子计数器保存事件数。

直接增加 softirq 仅用于学习机制；新 PCIe 外部模块通常选择 threaded IRQ、普通工作队列或目标内核支持的 BH workqueue。

## 共同的实现规则

- 调度操作是通知机制：pending 位可合并多次触发。实际事件数放在计数器里，实际数据放在 FIFO、ring 或 DMA 描述符里；原子计数器不会保存 ADC 样本。
- softirq/tasklet 不能 msleep、mutex_lock、down、wait_event，也不能使用可能睡眠的 GFP_KERNEL 分配或 copy_to_user。预分配优先，必要的临时分配用 GFP_ATOMIC 并处理失败。
- 普通 workqueue 可以睡眠，但持有自旋锁时仍不能睡眠。worker 不能假设 current 是打开设备的用户进程，也不能直接访问该进程的用户指针。
- softirq 同类型可跨 CPU 并发；同一个 tasklet 实例自身串行，不代表它与硬中断或 ioctl 自动互斥；同一个 work 在不重新初始化、不换队列、不改变回调的条件下自身不并发，不代表多个 work 共享的数据安全。
- 硬中断与下半部共享状态时，短临界区用 spin_lock_irqsave；只有进程上下文与 softirq/tasklet 共享时可用 spin_lock_bh；只有可睡眠线程共享时可用 mutex。
- 停止/卸载：先停止硬件、定时器、ioctl 等所有生产者，再等待 IRQ 完成，再 tasklet_kill 或 cancel_work_sync，最后释放队列、MMIO 和 DMA 内存。不能在回调所需的锁内同步取消回调，也不能从回调同步取消自身。
- flush_workqueue 等待工作处理；cancel_work_sync 取消待处理工作并等待正在执行的工作。因此取消完成不意味着所有已提交数据都处理完了。还活着的生产者可以再次排队，必须先停生产者。
- 回调时间要有界。批次过大应分批继续调度；内核 softirq 的处理轮次限制不会强行中止你自己的长回调。普通 worker 同样不应长期占用 CPU 或永久等待。

## 接入当前 PCIe 驱动

真正的 ISR 应先按 FPGA 协议检查、确认或屏蔽中断源，再保存描述符/数据或可追溯的事件，并提交 tasklet/work。下半部完成数据入队后 wake_up_interruptible()，应用通过 poll 或阻塞 read/ioctl 等待。

必须确认 FPGA 数据在下半部执行前仍可读取。当前驱动的 +320 读取语义无法单凭源码确定；如果这个读取会释放当前批次，必须在释放前保存批次数据，或由硬件 FIFO/DMA 描述符保证所有权，不能先确认后延迟读取一个会被覆盖的寄存器窗口。

Linux 6.9 的 BH workqueue 提供 WQ_BH 标志；它在 softirq 上下文执行，仍不能睡眠，不应与普通 worker 线程队列混淆。旧的 4.x/5.x 厂商内核不能假设有该 API。
