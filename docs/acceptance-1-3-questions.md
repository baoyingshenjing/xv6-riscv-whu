# 实验 1–3 验收追问精讲：启动、分页、中断

这份说明按同学记录的验收提问整理，依据本仓库 `stage1`、`stage2`、`stage3`
的实际代码。下面的 `kernel/…` 路径，应在相应的阶段目录中打开。
请先理解三条链：**CPU 怎样进入内核、内核怎样启用分页、中断怎样进入处理程序再返回。**

## 1. 先把文件名和函数名说准

- 文件名是 **`kernel.ld`**，`.ld` 是链接脚本，不是 `Kernel.Id`。
- 函数名是 **`kerneltrap()`**。
- 本项目的 **`trapinit()` 只初始化 tickslock**。
- 把中断入口写成 kernelvec 的是 **`trapinithart()`**：

  ```c
  w_stvec((uint64)kernelvec);
  ```

有些资料描述的是其他版本的 xv6。验收时应以自己打开的阶段源码为准，
不要把第 9 阶段的进程、中断调度逻辑搬到第 3 阶段讲。

## 2. 启动链：kernel.ld、entry.S、start()、main() 如何联系？

### 2.1 先区分“构建时安排地址”和“运行时执行指令”

`kernel.ld` 是链接器在构建时读取的布局规则。CPU 不会执行这个文件。

```text
构建时：源码 → 编译/汇编成 .o → ld 按 kernel.ld 链接 → kernel/kernel（ELF）
运行时：QEMU 启动 ROM → _entry → start() → mret → main()
```

链接脚本把内核安排在预期的地址，`ENTRY(_entry)` 把 `_entry` 登记为 ELF 入口。
本机的阶段 3 内核已经核对：

```text
ELF Entry point = 0x80000000
_entry         = 0x80000000
start          = 0x80000054
main           = 0x800000d2
```

后面两个函数地址会随代码、工具链变化；验收应解释地址如何产生，不必背死地址。

### 2.2 entry.S：先准备栈，才能调用 C 函数

入口是 `entry.S` 中的 `_entry`，此时 CPU 处于 M 模式：

```asm
la   sp, stack0
li   a0, 4096
csrr a1, mhartid
addi a1, a1, 1
mul  a0, a0, a1
add  sp, sp, a0
call start
```

这段计算：

```text
sp = stack0 + (hartid + 1) × 4096
```

`stack0` 是 start.c 定义的启动栈数组。每个 CPU 分得 4KB，栈向低地址增长，
所以 sp 初始指向自己那一段的顶部。不同 CPU 不共用同一段栈。
C 函数需要用栈保存返回地址、寄存器和局部数据，因此先设置 sp，再 `call start`。
`call` 会设置返回地址 ra；正常启动通过 mret 进入 main，不会从 start 返回。

### 2.3 start()：设置“返回到什么模式”和“返回到哪里”

最核心的是下面三步：

```c
unsigned long x = r_mstatus();
x &= ~MSTATUS_MPP_MASK;
x |= MSTATUS_MPP_S;
w_mstatus(x);             // mret 后的目标模式：S

w_mepc((uint64)main);     // mret 后的目标 PC：main 的地址

// ……其他准备……
asm volatile("mret");
```

可以把这两个寄存器的作用记成：

| 设置 | 回答的问题 |
| --- | --- |
| `mstatus.MPP = S` | mret 之后以什么特权模式执行？ |
| `mepc = main` | mret 之后从哪个地址继续执行？ |

执行 mret 后，CPU 按上述设置进入 S 模式，并令 PC 指向 main。
所以“进入 S 模式”和“跳到 main”发生在同一条 mret 指令上，
并不是先切换模式，再额外 `call main()`。mret 也不是普通 C 函数的 ret。

start 中其余准备包括：

- `w_satp(0)`：暂不开启分页，先按物理地址运行。
- 配置 `medeleg/mideleg`：将硬件支持委托的异常、中断交给 S 模式处理。
- 配置 `sie`：允许 S 模式外部、定时器中断源；main 后面还要打开全局中断开关。
- 配置 PMP：给 S 模式访问所需物理地址的权限。
- 把 hartid 写入 tp：后续 `cpuid()` 可根据 tp 确定当前 CPU。
- 第 3 阶段还调用 `timerinit()`，设置 S 模式定时器及第一次触发时间。

**进入 S 模式不等于已经开启分页。** 我们在阶段 3 上用单核 GDB 实测：

```text
mret 前：priv=3，MPP=1，mepc 与 main 地址相同
进入 main：priv=1，satp=0
kvminithart 后：priv=1，satp=0x8000000000087fff，mode=8
```

这里 priv=3 是 M 模式，priv=1 是 S 模式；mode=8 是 Sv39 分页模式。
本机原始记录保存在验收目录 `logs/startup-mode-and-paging.log`。

### 2.4 验收时可以这样连续讲

> kernel.ld 在链接时确定内核布局和入口。QEMU 把执行权交到 `_entry` 后，
> entry.S 先按 hartid 设置每个 CPU 的启动栈，再调用 start。
> start 把 MPP 设成 S，把 main 地址写入 mepc，完成访问权限等准备后执行 mret。
> mret 同时切换到 S 模式并把 PC 设置成 main，所以接下来就在 S 模式执行 main。

## 3. kernel.ld 具体做了什么？为什么要对齐？

### 3.1 按本项目脚本逐项看

| 脚本内容 | 意义 |
| --- | --- |
| `OUTPUT_ARCH("riscv")` | 指定链接输出的目标架构 |
| `ENTRY(_entry)` | 指定 ELF 入口符号；不是一条运行时跳转指令 |
| `SECTIONS { … }` | 规定各输入节如何合并、放到哪些输出节中 |
| `. = 0x80000000` | 把链接地址的当前位置设到内核起始地址；`.` 是位置计数器 |
| `*(.text .text.*)` | 收集目标文件中的代码节 |
| `. = ALIGN(0x1000)` | 把当前位置向上推进到 4KB 边界 |
| `PROVIDE(etext = .)` | 导出代码区域结束边界的符号 |
| `.rodata` | 汇集常量等只读语义的数据节 |
| `.data` | 汇集有初值的可写全局、静态数据 |
| `.bss` | 汇集零初始化的全局、静态数据；加载环境要提供零初值 |
| `. = ALIGN(16)` | 把相应数据位置推进到 16 字节边界 |
| `PROVIDE(end = .)` | 导出内核镜像在内存中的结束位置 |

`etext` 和 `end` 是**表示地址的链接符号**，不是链接器创建的普通 C 数组。
代码写 `extern char end[]`，是为了方便在 C 中取得这个符号的地址。

### 3.2 `_entry` 为什么位于最前面？

本仓库 Makefile 的 OBJS 把 `kernel/entry.o` 放在第一个，
链接脚本再用 `.text` 的收集规则把它的入口代码放到开头，实际 ELF 已验证入口地址。
**只写 `ENTRY(_entry)` 不会自动把入口函数移动到文件或代码区域的最前面。**

有一行容易误读：`kernel/entry.o(_entry)`。GNU ld 的括号里匹配的是**输入节名**，
而本项目 entry.S 实际使用 `.section .text`，`_entry` 是符号名。
所以不要把这行当作“按函数名挑出 `_entry`”来解释。
这里实际的入口排列依靠 Makefile 的输入顺序与 `.text` 收集规则，
`readelf`/`nm` 已核对 `_entry = 0x80000000`。

### 3.3 三种对齐要分清

1. **代码区域末尾的 4KB 对齐**

   页面权限以整页为单位。vm.c 将 `[KERNBASE, etext)` 映射为读/执行，
   将 `[etext, PHYSTOP)` 映射为读/写。etext 对齐后，这两段能在页边界分开。
   本项目的 mappages 也要求映射起点、长度按页对齐。

2. **数据布局的 16 字节对齐**

   用于满足或加强数据的对齐要求，和 4KB 的页边界不是同一个概念。
   另外，启动栈满足 16 字节对齐，直接依靠 start.c 中 stack0 的 `aligned(16)`
   以及每 CPU 的 4096 字节偏移；不能只归因于链接脚本中的 ALIGN(16)。

3. **空闲内存起点向上取整**

   链接脚本的 end 不一定是 4KB 对齐。freerange 用 `PGROUNDUP(end)` 找到第一张完整空闲页，
   防止把仍有内核数据的末尾页面放进空闲链表。

补充两点：第 1–3 阶段还没有 trampoline，不能用后面阶段的跳板页对齐来代替这里的解释。
`.rodata` 的名字也不直接决定运行时 PTE 权限：本阶段实际将 etext 后的区域统一映射为读/写。

## 4. kinit、kvminit、kvminithart 各干什么？

记忆顺序：**先准备可以分配的物理页 → 用这些页造页表 → 让当前 CPU 使用页表。**

| 函数 | 实际动作 | 调用范围 | 是否立即开启分页 |
| --- | --- | --- | --- |
| `kinit()` | 初始化 kmem 锁，将完整空闲物理页加入 freelist | CPU 0 一次 | 否 |
| `kvminit()` | 调用 kvmmake，分配并填充内核页表，保存根指针 kernel_pagetable | CPU 0 一次 | 否 |
| `kvminithart()` | 将根页号和 Sv39 模式写入当前 CPU 的 satp，并执行 sfence.vma | 每个 CPU | 是 |

### 4.1 kinit 为什么必须在 kvminit 前面？

kvmmake/walk 要调用 kalloc 分配根页表和中间页表的物理页。
没有先构建空闲链表，kalloc 就没有页可分配。

freerange 处理的范围是 `PGROUNDUP(end)` 到 `PHYSTOP` 之间的完整页面。
kfree 把页挂到链表头，kalloc 从头部取页，锁保护并发修改。
kalloc 填充调试字节，并不保证返回全零页面，所以创建页表时还要 memset 清零。

### 4.2 kvminit 建立哪些映射？

第 2、3 阶段映射 UART、PLIC、内核代码、内核数据和剩余物理内存。
内核代码读/执行；设备寄存器、数据和空闲内存读/写。
大量映射是 VA=PA 的恒等映射，但分页和权限检查仍然生效。

walk 通过三级索引找到 PTE，需要时分配中间页表；
mappages 把物理页号、权限和有效位写入叶子 PTE。

### 4.3 kvminithart 为什么要每个 CPU 都执行？

```c
sfence_vma();
w_satp(MAKE_SATP(kernel_pagetable));
sfence_vma();
```

多个 CPU 可以共享内存中的同一张内核页表，但每个 CPU 都有自己的 satp，
需要分别装入根页号。sfence.vma 用来同步分页相关状态，避免继续使用不适用的旧翻译缓存。

> kinit 管理可分配的物理页，kvminit 创建内核页表，kvminithart 让当前 CPU 使用这张页表。
> 前两个由 CPU 0 执行一次，最后一个需要所有 CPU 分别执行。

## 5. 页表几级、多少位？

**本项目运行在 RV64 上，使用 Sv39 三级页表，普通页大小为 4KB。**

```text
有效虚拟地址的低 39 位：

38             30 29             21 20             12 11              0
+----------------+-----------------+-----------------+-----------------+
| VPN[2]：9 位   | VPN[1]：9 位    | VPN[0]：9 位    | 页内偏移：12 位 |
+----------------+-----------------+-----------------+-----------------+
       根级              中间级              叶级
```

- 页内偏移 12 位，所以一页是 `2^12 = 4096` 字节。
- 每级索引 9 位，所以每张页表有 `2^9 = 512` 项。
- 每项 PTE 8 字节，因此一张页表恰好是 `512 × 8 = 4096` 字节。
- 三个索引共 27 位，加 12 位页内偏移，共 39 位。
- 地址和寄存器仍用 64 位保存，合法 Sv39 地址的高 25 位应等于第 38 位的符号扩展。
- 本 xv6 为简化处理只使用低半区，因此 `MAXVA = 1L << 38`；这不意味着只有两级或 38 位的 Sv39。
- 普通 4KB 页的翻译，是找到叶子 PTE 的物理页基址，再加原来的 12 位页内偏移。

若继续追问物理地址：Sv39 标准 PTE 提供 44 位物理页号，加 12 位偏移，
格式可描述最多 56 位物理地址；这不代表 QEMU 真有这么多 RAM。
当前 QEMU 参数配置的是 128MB RAM。

“三级”指一次地址翻译最多按三个层次查找，不是整个系统总共只有三张页表页。
本项目 walk/mappages 使用最底层的普通 4KB 叶子映射。

## 6. 为什么发生中断后会到 kernelvec？

需要先完成初始化，再由硬件在中断发生时跳转。

```text
准备阶段：
  trapinit()       → 初始化 tickslock
  trapinithart()   → stvec = kernelvec
  plicinit()      → UART 外部中断的全局配置
  plicinithart()  → 当前 CPU 的 PLIC 使能与阈值
  intr_on()      → 设置 sstatus.SIE，打开当前 CPU 的全局中断开关

运行阶段：
  中断满足触发条件
    → 硬件保存中断现场的关键控制信息，PC 转向 stvec
    → kernelvec 保存通用寄存器
    → call kerneltrap
    → devintr 分发设备/定时器处理
    → 返回 kerneltrap，再返回 kernelvec
    → 恢复寄存器，sret 返回被中断位置
```

`main()` 没有普通地 `call kernelvec()`。
main 设置好 stvec 后，**CPU 硬件**在合适的中断到来时把控制权交给它。
本代码将 kernelvec 的对齐地址直接写入 stvec，使用 Direct 模式。
stvec 的低两位保存模式，因此入口地址至少需要 4 字节对齐；
本项目 kernelvec.S 中的 `.align 4` 按该汇编器语法表示按 16 字节对齐，也满足这个要求。

S 模式 trap 发生时，硬件会记录 sepc、scause 等信息，
在 sstatus 中记录之前的模式和中断开关，并清除 SIE。
硬件不会替软件把所有通用寄存器保存到栈上，这正是汇编入口需要完成的工作。

## 7. kernelvec、kerneltrap、devintr 分别负责什么？

### 7.1 kernelvec：汇编层的现场保护和返回

它位于 `kernelvec.S`，先将 sp 减去 256，为保存寄存器留空间，
保存 C 调用可能破坏的寄存器，再 `call kerneltrap`。
C 调用约定要求被调用函数保护的寄存器由该约定覆盖；
不要把这份源码描述成“汇编无条件保存了全部 32 个寄存器”。

kerneltrap 返回后，kernelvec 恢复保存的寄存器、恢复 sp，再执行 sret。
这三个阶段只运行内核，因此 sret 返回到原来的 S 模式内核执行位置。

### 7.2 kerneltrap：检查这次陷入是否合理，交给分发器处理

按源码顺序：

1. 保存 sepc 和 sstatus。
2. 检查 SPP：这次是否从 S 模式进入？
3. 检查 SIE：硬件进入处理时应该已关中断。
4. 调用 devintr；如果没有识别并处理这次 trap，打印 scause/sepc/stval 并 panic。
5. 恢复 sepc 和 sstatus，然后返回汇编入口。

它负责内核 trap 的通用处理框架，不直接根据 UART 寄存器逐字节收输入。
第 3 阶段没有用户进程，所以这里不做 yield 或进程调度。

### 7.3 devintr：识别中断种类并分发

第 3 阶段的两个分支是：

```c
if (scause == 0x8000000000000009L) {
  int irq = plic_claim();
  if (irq == UART0_IRQ)
    uartintr();
  // ……其他非零 IRQ 的诊断输出……
  if (irq)
    plic_complete(irq);
  return 1;
}
if (scause == 0x8000000000000005L) {
  clockintr();
  return 1;
}
return 0;
```

本阶段返回 1 表示已识别处理，0 表示没有识别。
后续阶段可能用不同返回值区分定时器以决定调度，不要混用。

## 8. scause 和 irq 到底差在哪？

一句话：**scause 告诉你“哪类 trap”，外部中断的 irq 再告诉你“哪个设备”。**

| 内容 | 从哪里来 | 本项目例子 |
| --- | --- | --- |
| scause | CPU 的 CSR，由硬件记录 | 最高位为 1 且原因码为 9：S 外部中断 |
| irq | 软件读取 PLIC 的 claim 寄存器得到 | 10：UART0；0：没有认领到请求 |

RV64 的 scause 最高位是 Interrupt 位，其余位是原因码：

```text
0x8000000000000009 = (1 << 63) | 9 → S 模式外部中断
0x8000000000000005 = (1 << 63) | 5 → S 模式定时器中断
```

**scause 的 9 与 UART 的 IRQ 10 属于不同编号体系。**
不能说“scause=10 就是键盘中断”，也不能把上面完整数值简写成 scause=9 而不说明最高位。
定时器分支不通过 PLIC，因此不需要再读取 irq。

## 9. 两条具体中断链怎样讲？

### 9.1 定时器为什么能打印 TTT？

```text
start.timerinit 设置首次 stimecmp
→ time 达到比较值，产生 S 定时器中断
→ 硬件进入 kernelvec
→ kerneltrap → devintr，识别原因码 5
→ clockintr
→ CPU 0 给 ticks 加 1，每满 30 次打印 T
→ 每个 CPU 重设自己的 stimecmp = time + 1000000
→ 返回并通过 sret 继续原内核代码
```

在当前 QEMU 时间基准下，1,000,000 个 time 单位约为 0.1 秒，
30 次约为 3 秒。只让 CPU 0 累计全局 ticks，避免三个 CPU 重复计数。
`T` 没有自带换行，所以屏幕可能连续显示 `TTT`。
本版本使用 S 模式定时器，不需要经过旧版本的 M 模式 timervec/软件中断转发路径。

### 9.2 键盘输入为什么能回显？

```text
在 QEMU 终端输入字符
→ 字符进入虚拟 UART 的接收寄存器
→ UART 经 PLIC 产生外部中断
→ kernelvec → kerneltrap → devintr，识别原因码 9
→ plic_claim 得到 UART0_IRQ=10
→ uartintr → uartgetc 取出字符
→ uartputc_sync 等待可发送，再写入 UART 输出寄存器
→ plic_complete，返回并通过 sret 继续内核代码
```

所以这里常说的“键盘中断”，在这块 QEMU 虚拟板上实际由 UART 接收中断承载，
不是另一个独立的键盘驱动。第 3 阶段也不经过第 9 阶段才引入的 console 行缓冲和 shell。

## 10. 验收前的口述练习

不要逐行背整份源码。分别练习以下三段，每段先给出主线，再指向关键代码：

1. **启动，约一分钟：** 链接布局与入口 → 每 CPU 启动栈 → MPP/mepc → mret → S 模式 main。
2. **分页，约一分钟：** kinit 管物理页 → kvminit 建内核映射 → 每 CPU 的 kvminithart 写 satp；
   三级、39 位、9+9+9+12、4KB、512 个 8 字节 PTE。
3. **中断，约一分钟：** trapinithart 配 stvec → 硬件进入 kernelvec → kerneltrap 检查 →
   devintr 按 scause 分发；外部中断再查 PLIC irq → 定时器/串口处理 → sret 返回。

现场按 `run.sh 1`、`run.sh 2`、`run.sh 3` 展示；实验 2 可配合现有的 `check-stage2.gdb`。
实验 3 等到至少两个 T，再输入 `hello xv6` 展示回显。

## 11. 核对依据

函数调用、常量和输出以本仓库三个阶段的源码及本地 ELF/GDB 核对为准。
体系结构与链接脚本语义另外参考官方资料：

- [RISC-V Machine-Level ISA：MPP、mepc 与 mret](https://docs.riscv.org/reference/isa/priv/machine.html)
- [RISC-V Supervisor-Level ISA：Sv39、stvec、scause 与 sret](https://docs.riscv.org/reference/isa/priv/supervisor.html)
- [GNU ld：输入节匹配规则](https://sourceware.org/binutils/docs/ld/Input-Section-Basics.html)
- [GNU ld：ALIGN 等内建函数](https://sourceware.org/binutils/docs/ld/Builtin-Functions.html)
