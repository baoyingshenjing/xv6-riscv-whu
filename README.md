# xv6-riscv-whu：九阶段实验与验收指南

本仓库对应武汉大学《操作系统课程设计》任务书第三部分 **“xv6 的裁剪与拼接”**。
运行环境为 WSL/Linux + QEMU RISC-V。`main` 保存第 9 阶段的最终实现。

**第一次验收（实验 1–3）：** 已提供[单独的演示与讲解指南](docs/acceptance-1-3.md)。
同学记录的真实提问对应的解释见[启动、分页、中断追问精讲](docs/acceptance-1-3-questions.md)。
在本仓库执行 `python3 tools/prepare-acceptance.py`，会生成三个独立源码目录，
随后运行 `bash acceptance-1-3/run.sh 1`（或 `2`、`3`）即可逐项演示，无需切换分支或标签。

## 1. 这九个实验是什么关系？

**它们是按顺序累加功能的九个阶段，不是九个互不相关的程序。**

从只能启动并打印文字的内核开始，逐步加入内存、中断、用户进程、系统调用、
多进程、磁盘、程序装载，最后得到能输入命令的简易操作系统。

```text
1 启动至 S 模式
  ↓ 加入内存管理
2 物理内存与内核页表
  ↓ 加入事件响应
3 内核态中断
  ↓ 加入用户态
4 首个用户进程
  ↓ 加入内核服务与用户堆
5 系统调用机制与 sbrk
  ↓ 加入多个进程及调度
6 多进程调度
  ↓ 加入磁盘块读写
7 virtio 磁盘与缓冲缓存
  ↓ 加入日志、文件组织与程序装载
8 log、inode 与 exec
  ↓ 加入交互入口
9 控制台输入、init 与 shell
```

需要区分两件事：

- **功能关系是累加的。** 例如第 9 阶段运行一个程序，会同时用到前面实现的
  内存分配、页表、进程调度、系统调用、磁盘读取与 exec。
- **每个阶段的保存版本可以单独启动。** Git 标签是某个阶段的代码快照，切换到
  对应标签后可以独立编译、运行，方便老师逐阶段检查。

后期会调整启动流程和测试载体，并非把早期的所有演示输出原样保留。例如：

- 第 3–8 阶段周期性打印 `T` 检查定时器；第 9 阶段为了保持命令行整洁，去掉了这项输出。
- 第 6–8 阶段启动后自动运行测试；第 9 阶段先进入 shell，由用户输入 `test` 启动综合测试。
- 第 7、8 阶段的输出很接近，但第 8 阶段已经能通过文件名查找并装载磁盘上的程序。

因此，**`main` 用于展示最终成果；阶段标签用于展示每一步新增了什么。**

## 2. 每个实验在做什么，验收看什么？

| 实验 | 本阶段新增的能力 | 现场应看到的现象 | 验收版本 |
| --- | --- | --- | --- |
| 1：启动至 S 模式 | CPU 从 M 模式完成初始化，切换到操作系统使用的 S 模式；支持多核打印 | 3 个 CPU 各输出一行 `cpu … is booting!`，然后停在循环中 | `stage1` |
| 2：物理内存与页表 | 把空闲物理页组织起来，提供分配/释放；建立内核页表并让各 CPU 启用分页 | 启动提示正常，外观与实验 1 相似；结合代码解释页分配和页表启用 | `stage2` |
| 3：内核中断 | 处理定时器、串口输入中断 | 约每 3 秒出现一个 `T`；敲键盘能回显 | `stage3` |
| 4：首个用户进程 | 建立首进程的用户地址空间、用户栈和跳板页，进入用户态并返回内核 | 出现两次 `get a syscall from proc 1`；仍能看到 `T` 和键盘回显 | `stage4` |
| 5：系统调用与用户堆 | 建立系统调用分发框架，通过 `sbrk` 扩张/收缩用户堆 | 自动执行堆测试，输出 `sbrk: n=… oldsz=… newsz=…`；有正数增长和负数收缩 | `stage5-complete` |
| 6：多进程调度 | 实现进程状态和调度，以及 fork、sleep、wait、exit、kill 等调用；支持用户态输出 | 自动创建 3 个子进程，显示堆测试、3 次 sleep、wait 回收和被 kill 进程的 `-1` 状态 | `stage6` |
| 7：磁盘块读写 | 加入 virtio 磁盘驱动、睡眠锁和缓冲缓存 | 在实验 6 输出的基础上，显示超级块、`README (1KB)`、`README (modified)` | `stage7` |
| 8：日志、inode 与 exec | 加入日志事务和 inode 层，按文件名找到 ELF 程序，并通过 exec 替换进程程序 | 现象接近实验 7；两个子进程已改为通过 exec 装载磁盘上的 `execchild1/2` | `stage8-complete` |
| 9：简易 shell | 加入行输入、init、shell、cd，把综合测试变为普通用户程序 | 启动出现 `$`；输入 `test` 后完成综合测试、打印 `test: ALL CHECKS PASSED`，再回到 `$` | `stage9-complete` 或 `main` |

`stage1`–`stage9` 是原始阶段标签。第 5、8、9 阶段另有修正版，
**验收时使用上表列出的版本**。`main` 包含最终实现以及最新说明和自动测试。

### 哪些功能不在这九阶段的范围内？

任务书要求的是简易 shell，尚未对用户程序开放完整的文件访问接口。
所以默认系统没有 `ls`，不支持普通文件的 open/read/write、管道、重定向、
后台命令和分号命令列表。这些不作为本仓库九阶段的完成标准。

这里的 `read/write` 系统调用直接连接控制台；`write` 的描述符参数为 3 时，
按任务书约定触发磁盘块测试。**能执行磁盘上的程序，不等于已经开放了完整文件访问接口。**

## 3. 找老师验收之前，先做什么？

以下命令都在 **WSL 的 Ubuntu 终端**执行。你当前的仓库路径是：

```sh
cd /home/baoyingshenjing/xv6-riscv-whu
```

### 第一步：确认环境和代码

本机已经具备所需工具。换到新 Ubuntu 环境时可安装：

```sh
sudo apt update
sudo apt install build-essential gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu qemu-system-misc gdb-multiarch bc xxd
```

也支持 `riscv64-unknown-elf-` 工具链。当前实现使用 S 模式定时器，已在 QEMU 8.2.2 验证。

确认没有未提交的源码修改后，同步代码和标签：

```sh
git status --short
git fetch origin --tags
git switch main
git pull --ff-only
```

本地的课程任务书 PDF、教师压缩包若显示为未跟踪文件，不需要为了验收把它们加入提交。
如果有自己尚未提交的源码修改，请先保存或提交，再切换历史阶段。

### 第二步：在 main 上跑一遍九阶段自动测试

```sh
make test-labs
```

测试会依次验证上表的各阶段版本，并在最后输出：

```text
PASS stage 1: stage1
...
PASS stage 8: stage8-complete
PASS stage 9 (1 CPU): shell, composite test, syscall regressions
PASS stage 9 (3 CPU): shell, composite test, syscall regressions
ALL REQUESTED LAB CHECKS PASSED
```

测试开头会打印临时目录，里面保存各阶段的构建与运行日志。
可以保留末尾通过结果的截图以及日志，作为现场演示的辅助材料。

**总测试在 `main` 上执行。** 早期阶段的历史版本没有 `make test-labs` 目标。

### 第三步：自己完整演示一次最终版

```sh
make clean
make qemu CPUS=3
```

进入 xv6 后，看到 `$` 提示符，再逐条输入下列命令；**不用输入 `$` 本身**：

```text
test
execchild2
cd /
cd README
execchild2
```

预期结果：

1. `test` 创建 3 个子进程，完成磁盘、堆、延时、kill、wait 测试。
   通常需要约 6 秒，最后显示 `test: ALL CHECKS PASSED`，重新出现 `$`。
2. `execchild2` 单独执行堆分配/释放测试，输出 `sbrk test over!` 后返回 shell。
3. `cd /` 成功，继续显示提示符。
4. `cd README` 输出 `cannot cd README`，因为 README 是普通文件。
5. 再次执行 `execchild2` 应正常运行，说明失败的 cd 没有破坏当前工作目录。

**不要单独运行 `execchild1`。** 它在磁盘测试后进入忙循环，应由 `test` 创建，
再由另一个子进程通过 kill 结束。

完成演示后，先按 **Ctrl+A**，松开，再按 **X**，退出 QEMU 回到 Ubuntu。
早期阶段同样使用这个退出方法。

## 4. 现场如何逐阶段给老师演示？

可以先这样介绍项目：

> 这九个实验把 xv6 按功能层逐步拼起来。我保留了各阶段的代码快照，
> 可以展示最终 shell，也可以切换到任意阶段，演示当时新增的功能。

先展示最终版；如果老师要求逐项验收，就按第 2 节表格切换标签。
**第 1–8 阶段没有 shell，启动后自动展示相应现象，不需要输入 `test`。**

### 切换一个阶段的统一步骤

先退出正在运行的 QEMU，再在 Ubuntu 执行，例如验收实验 5：

```sh
git switch --detach stage5-complete
make clean
make qemu CPUS=3
```

看到 `sbrk` 的正数和负数调用记录后，说明增长、收缩路径都已执行。
老师若继续检查实验 6，先退出 QEMU，再执行：

```sh
git switch --detach stage6
make clean
make qemu CPUS=3
```

其余阶段把标签换成第 2 节表格中的对应值即可。
`--detach` 表示查看固定历史版本，出现“脱离分支”提示是正常的。
**每次切换阶段后都执行 `make clean`，避免复用上一阶段的构建产物，并重建测试磁盘。**

### 哪些阶段需要额外解释？

- **实验 2：** 屏幕与实验 1 相似。打开 `kernel/kalloc.c`、`kernel/vm.c`、
  `kernel/main.c`，说明空闲页链表、内核映射和每个 CPU 启用页表的位置。
  若老师要求观察寄存器，可用 GDB 检查 `satp`。
- **实验 6：** 等到 `init process has no child now, over!` 再结束展示。
  三次 sleep 分别等待 10、20、30 个 tick，是依次执行的，累计约 6 秒。
- **实验 7：** 首次启动可比较修改前后的 README 内容。
  测试会改写 `fs.img`，重复演示时先 `make clean` 再启动，才能看到完整的前后变化。
- **实验 8：** 输出与实验 7 接近，重点展示 `execchild1/2` 已成为独立磁盘程序，
  以及 exec 如何读取 ELF、建立新地址空间、传入参数。日志恢复的自动验证见第 6 节。
- **实验 9：** 展示键盘行输入、普通命令执行、cd，以及 `test` 结束后重新出现提示符。
  不再打印 `T` 是本阶段按任务书做出的调整。

### 验收结束后恢复最终版

```sh
git switch main
make clean
make qemu CPUS=3
```

如果老师只收最终成果，交 `main` 的源码和 README；如果要求九个阶段的过程记录，
同时提供上表的阶段标签与运行记录。自动测试结果可以辅助说明，现场仍应能讲清新增功能和代码位置。

## 5. 老师问实现原理时，可以从哪里看代码？

| 实验 | 建议打开的核心文件 | 应能解释的问题 |
| --- | --- | --- |
| 1 | `kernel/entry.S`、`start.c`、`main.c`、`printf.c` | 启动入口在哪里？如何从 M 模式进入 S 模式？多核输出怎样互斥？ |
| 2 | `kernel/kalloc.c`、`vm.c`、`riscv.h` | 物理页如何分配和释放？页表映射了什么？satp 的作用是什么？ |
| 3 | `kernel/trap.c`、`kernelvec.S`、`plic.c`、`uart.c` | 如何区分中断原因？定时器怎样重置？键盘字符如何被取出？ |
| 4 | `kernel/proc.c`、`proc.h`、`trampoline.S`、`swtch.S` | 用户栈和地址空间如何准备？trapframe 与 context 分别保存什么？ |
| 5 | `kernel/syscall.c`、`sysproc.c`、`vm.c` | 系统调用号和参数如何传递？sbrk 如何改变进程大小与页面映射？ |
| 6 | `kernel/proc.c`、`trap.c`、`uart.c` | 何时调度？sleep/wakeup 如何避免丢失唤醒？僵尸进程由谁回收？ |
| 7 | `kernel/virtio_disk.c`、`bio.c`、`sleeplock.c`、`mkfs/mkfs.c` | 驱动如何提交请求？完成中断如何唤醒进程？缓冲和缓存如何复用？ |
| 8 | `kernel/log.c`、`fs.c`、`exec.c`、`sysfile.c` | 日志如何提交和恢复？inode 如何定位程序？exec 与 fork 有什么区别？ |
| 9 | `kernel/console.c`、`user/initcode.S`、`init.c`、`sh.c`、`test.c` | 回车如何唤醒读取者？init 如何启动 shell？命令怎样 fork/exec/wait？ |

上表路径对应各阶段自己的源码；切换标签后，看到的是那个阶段的实现。

## 6. 自动验证具体覆盖什么？

```sh
# 在 main 上验证九阶段
make test-labs

# 仅验证当前第 9 阶段源码
python3 tests/test-labs.py --stages 9

# 仅验证第 8 阶段快照，包括已提交日志的启动恢复
python3 tests/test-labs.py --stages 8
```

测试只依赖 Python 3 标准库和前述编译/运行工具。
历史阶段从标签创建临时源码副本；第 9 阶段复制当前工作区源码。
所有构建、磁盘改写和额外测试程序都在临时目录中运行，不会改写当前工作区的磁盘镜像。

覆盖内容包括：

- 九阶段的编译、启动和对应运行现象。
- 定时器/键盘中断、三个子进程的运行与回收、盘块写回。
- 构造已提交但尚未写回目标盘块的日志，检查启动恢复后的数据和日志头。
- 最终版在 1/3 CPU 下的综合 test、shell 和系统调用回归。
- exec 参数及 argv[0]、参数数量边界、无效指针、过长参数。
- cd 目录类型检查、跨页堆读写与收缩、退出状态、睡眠唤醒、退格和 Ctrl+U。

额外测试程序 `labprobe` 仅加入临时测试磁盘，默认课程磁盘中仍只有
`README`、`init`、`sh`、`test`、`execchild1`、`execchild2`。

GitHub 上以 **main 的 `Course labs` 工作流**作为九阶段自动验证入口。
历史阶段标签仍可能携带旧的上游 CI 配置；那套配置中的完整文件系统测试不适用于本课程裁剪版本。
上游 `test-xv6.py` 保留供参考，不作为这九阶段的验收命令。

## 7. 其他常用操作

### 重置当前阶段的磁盘

退出 QEMU 后执行：

```sh
make clean
make qemu
```

这会重新编译并重建 `fs.img`。仅重建磁盘也可执行 `touch README` 后再运行 `make fs.img`。

### GDB 调试

一个 WSL 终端执行：

```sh
make qemu-gdb
```

另一个 WSL 终端进入同一仓库目录后执行：

```sh
gdb-multiarch kernel/kernel
```

按项目生成的 `.gdbinit` 连接 QEMU。如 GDB 提示禁止自动加载该文件，
按提示为当前仓库添加对应的安全路径，再重新启动 GDB。

上游 xv6 的介绍与许可证分别见 `README` 和 `LICENSE`。
