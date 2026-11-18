# 第一次验收：实验 1–3

本次只验收：**1 启动至 S 模式 → 2 物理内存与内核页表 → 3 内核态中断**。
三者逐步累加。本目录分别保存三个阶段的源码，可直接演示，原仓库保持在 main。
这三个阶段都没有用户进程和 shell，启动后不需要输入 `test`。

同学的真实验收提问对应的详细讲解，见[启动、分页与中断追问精讲](acceptance-1-3-questions.md)。

## 一、现场启动方法

本机已准备的目录：

```sh
cd /home/baoyingshenjing/xv6-riscv-whu/acceptance-1-3
```

如果换电脑，在原仓库 main 上执行 `python3 tools/prepare-acceptance.py`，
即可从阶段标签重新生成这个目录。导出工具会保留已有的同版本目录；
要重新导出干净副本，可用 `--dest` 指定另一个目录。

按顺序演示，每次先退出上一个 QEMU，再运行下一条命令：

```sh
bash run.sh 1
# 观察实验 1，退出 QEMU
bash run.sh 2
# 观察实验 2，退出 QEMU
bash run.sh 3
```

**退出 QEMU：先按 Ctrl+A，松开，再按 X。** 这会回到 Ubuntu，随后就能启动下一阶段。
每个实验默认启动 3 个 CPU，首次运行会自动编译。源码位置分别为 `stage1/`、`stage2/`、`stage3/`。
`stage3/` 已裁剪为前三阶段实际使用的源码与头文件，Makefile 也已移除后续阶段构建规则；
保留的内核源码与 stage3 标签一致。`SOURCE.json` 记录各目录的原始提交，便于核对。

## 二、实验 1：让内核启动到 S 模式

### 给老师看什么

运行 `bash run.sh 1`，应看到三行提示，例如：

```text
cpu 0 is booting!
cpu 1 is booting!
cpu 2 is booting!
```

CPU 1、2 的输出顺序可能交换。之后内核停在循环中，不出现命令行提示符。
本阶段尚未开放中断，因此不要求键盘回显或定时器输出。

### 可以这样介绍

> 我先让各 CPU 在 M 模式完成必要初始化，再通过 mret 切到 S 模式执行 main。
> CPU 0 初始化串口和打印锁，其他 CPU 等初始化完成后再打印，最后各 CPU 进入循环。

### 打开哪些代码

以下文件均位于 `stage1/kernel/`：

| 文件 | 重点 |
| --- | --- |
| `entry.S` | 入口 `_entry`，为每个 hart 设置独立的启动栈，然后调用 start |
| `start.c` | MPP 设为 S、mepc 设为 main、设置 PMP、把 hartid 写入 tp，最后执行 mret |
| `main.c` | CPU 0 初始化；started 的发布/等待；各 CPU 打印和循环 |
| `printf.c`、`uart.c` | printf 通过串口同步输出；等待串口可发送后写入字符 |
| `spinlock.c`、`proc.c` | 打印互斥；根据 tp 找到当前 CPU 的状态 |

### 应能回答

- **为什么设置 mepc？** mret 返回时从 mepc 指定的地址继续执行，这里指定 main。
- **为什么设置 MPP？** mret 根据 MPP 选择返回后的特权模式，这里选择 S 模式。
- **为什么先等 CPU 0？** 串口和打印锁必须先初始化，其他 CPU 才能安全使用。
- **为什么打印需要锁？** 多个 CPU 同时输出字符会发生交错。

## 三、实验 2：管理物理页并启用内核页表

### 给老师看什么

运行 `bash run.sh 2`，仍然看到三个 CPU 的启动提示。
**与实验 1 输出相似是正常的，本阶段新增的是内核内部的内存管理和地址翻译。**
演示输出后，打开下表代码说明新增路径。若老师要求实际观察页表启用，可使用后面的 GDB 检查。

### 可以这样介绍

> 我把内核结束地址到 PHYSTOP 之间的空闲内存按 4KB 页面组织成链表，
> 实现 kalloc/kfree。接着分配内核根页表、建立设备和内存映射，
> 最后让三个 CPU 分别写入 satp，启用同一张 Sv39 内核页表。

### 打开哪些代码

以下文件均位于 `stage2/kernel/`：

| 文件 | 重点 |
| --- | --- |
| `kalloc.c` | kinit/freerange 构建空闲页链表；kalloc 取页；kfree 归还页；锁保护链表 |
| `vm.c` | kvmmake 分配页表；walk 逐级查找/分配页表页；mappages 建立叶子映射 |
| `vm.c` | kvminithart 写入 satp，并在前后执行 sfence.vma |
| `main.c` | CPU 0 执行 kinit/kvminit；每个 CPU 都执行 kvminithart |
| `riscv.h`、`memlayout.h`、`kernel.ld` | 页表位与宏、物理地址布局、etext/end 符号 |

页表映射：UART、PLIC、内核代码、内核数据和剩余物理内存。
内核代码使用读/执行权限，内核数据和空闲内存使用读/写权限。

### 可选：用 GDB 证明三个 CPU 都启用了分页

先退出普通运行的 QEMU。终端 A 在本目录运行：

```sh
bash run.sh 2 --gdb
```

QEMU 会暂停等待调试器。终端 B 执行：

```sh
cd /home/baoyingshenjing/xv6-riscv-whu/acceptance-1-3/stage2
gdb-multiarch -nx -batch -x ../check-stage2.gdb kernel/kernel
```

检查脚本会在每个 CPU 启用页表后读取 satp，输出三个 CPU 的寄存器值，最后显示：

```text
CPU 0: satp=0x8000..., mode=8
CPU 1: satp=0x8000..., mode=8
CPU 2: satp=0x8000..., mode=8
PASS: all three CPUs use the kernel Sv39 page table.
```

输出顺序可以不同，完整 satp 值以实际运行为准。mode=8 表示 Sv39，
脚本还会核对其页表根地址。检查结束后调试器自动退出，终端 A 的 QEMU 继续运行。
此检查按默认 3 CPU 配置准备。

### 应能回答

- **kalloc 返回的是什么？** 一个完整的 4KB 物理页的内核可用地址。
- **空闲链表存在哪里？** 每个空闲页开头保存 next 指针，kmem.freelist 指向表头。
- **Sv39 是什么？** 39 位虚拟地址的分页方式，通常以三级页表完成翻译。
- **为什么三个 CPU 都要写 satp？** satp 属于每个 CPU 自己的寄存器。
- **地址看起来没变，为什么还要页表？** 此处大量采用恒等映射，地址数值可以相等，
  但翻译和权限检查仍由已启用的页表完成。

## 四、实验 3：处理定时器和键盘中断

### 给老师看什么

运行 `bash run.sh 3`：

1. 先看到三行 CPU 启动提示。
2. 等待约 3 秒，应出现一个 `T`；继续等待应周期性出现。
3. 输入 `hello xv6`，屏幕应回显相同字符；按回车可换行。

可等待 6–7 秒，展示两次定时器输出，再输入几个字符。
这里的输入由串口中断直接回显，不是 shell 在执行命令。

### 可以这样介绍

> 我在前两个阶段的基础上设置内核中断入口并开放中断。
> 定时器每约 0.1 秒触发一次，CPU 0 每累计 30 次打印 T。
> 键盘输入经 UART 和 PLIC 产生外部中断，由驱动读出字符并同步回显。

### 打开哪些代码

以下文件均位于 `stage3/kernel/`：

| 文件 | 重点 |
| --- | --- |
| `start.c` | 委托中断，开启 S 模式定时器相关配置，设置第一次 stimecmp |
| `main.c` | trap/PLIC 的初始化和每 CPU 配置；末尾 intr_on 开放中断 |
| `kernelvec.S` | 保存寄存器、调用 kerneltrap、恢复寄存器并通过 sret 返回 |
| `trap.c` | kerneltrap 进入处理；devintr 根据 scause 分发；clockintr 计时并更新 stimecmp |
| `plic.c` | 配置串口 IRQ；claim 认领外部中断；complete 通知处理结束 |
| `uart.c` | uartintr 读取输入字符，并通过 uartputc_sync 回显 |

### 应能回答

- **怎样区分中断？** 读取 scause；本实现处理 S 模式定时器中断和外部中断。
- **为什么需要 PLIC？** 外部中断还需要确定设备来源、完成仲裁和认领；UART 的 IRQ 为 10。
- **定时器是否经过 PLIC？** 不经过；它是 CPU 的定时器中断。
- **为什么要重新设置 stimecmp？** 为下一次定时器中断安排新的触发时间。
- **为什么只有 CPU 0 累计 ticks？** 避免多个 CPU 对同一全局时间重复计数。
- **为什么此时没有进程切换？** 第 3 阶段尚未引入用户进程和调度器，中断后返回原内核执行位置。

## 五、验收前最后检查

- 三个启动命令各运行一次，确认能用 Ctrl+A、X 退出。
- 实验 1：三核启动提示正常。
- 实验 2：能说明 kalloc/kfree、页表映射、每 CPU 的 satp；需要时展示 GDB 检查。
- 实验 3：`T` 周期输出，键盘即时回显。
- 能从本目录直接打开对应 stage 的代码。

在原仓库 main 上还可运行 `python3 tests/test-labs.py --stages 1 2 3`，
作为第一次验收范围的自动检查。自动检查通过可以辅助展示，仍需能解释上面的代码与现象。
