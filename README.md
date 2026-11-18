# xv6-riscv-whu

武汉大学《操作系统课程设计》任务书第三部分“xv6 的裁剪与拼接”的九阶段实现。
`main` 是第 9 阶段最终版，运行于 WSL/Linux + QEMU RISC-V。

## 环境与运行

Ubuntu 22.04 / WSL 可安装：

```sh
sudo apt update
sudo apt install build-essential gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu qemu-system-misc gdb-multiarch bc xxd
make qemu
```

也支持 `riscv64-unknown-elf-` 工具链。当前版本使用 S 模式定时器；已在 QEMU 8.2.2 验证。
默认 3 个 CPU，可用 `make qemu CPUS=1` 切换为单核。
QEMU 中先按 Ctrl+A，松开后按 X 退出。
调试使用 `make qemu-gdb`，在另一终端用 `gdb-multiarch kernel/kernel` 连接。

进入 shell 后可运行：

```text
$ test
... 超级块、README 盘块读写、堆分配、延时、kill、wait 的输出 ...
test: ALL CHECKS PASSED
$ execchild2
... sbrk test over! ...
$ cd /
```

`test` 会创建三个子进程：通过 exec 启动磁盘测试和堆测试，再由延时子进程
结束忙循环子进程；父进程核对三个退出状态，完成后返回 shell。
`execchild1` 包含忙循环，应通过 `test` 启动。
磁盘块测试按任务书约定改写虚拟磁盘中 README 的首块，保留第一段。
要恢复初始磁盘，可运行 `touch README` 后重新 `make fs.img`。

课程范围仅包含简易 shell 和直接控制台 I/O，磁盘镜像中默认包含
`README`、`init`、`sh`、`test`、`execchild1`、`execchild2`。
文件访问系统调用、ls、管道、重定向、后台命令和命令列表不属于这九阶段任务。

## 九阶段验收

原始 `stage1` 至 `stage9` 标签保留；修复过的阶段使用新的 `-complete` 标签。

| 阶段 | 验收版本 | 主要验收内容 |
| --- | --- | --- |
| 1 | `stage1` | M 模式初始化、进入 S 模式、多核提示输出 |
| 2 | `stage2` | 物理页分配与释放、内核页表、所有 CPU 开启分页 |
| 3 | `stage3` | 定时器中断、每 30 次输出 T、键盘回显 |
| 4 | `stage4` | 首个用户进程、两次系统调用、跳板页和用户栈 |
| 5 | `stage5-complete` | 系统调用分发、sbrk 扩张/收缩及调用记录输出 |
| 6 | `stage6` | 多进程调度、fork/sleep/wait/exit/kill、异步串口输出 |
| 7 | `stage7` | virtio 磁盘、缓冲缓存、超级块和 README 盘块读写 |
| 8 | `stage8-complete` | inode 初始化、日志事务与恢复、完整 exec 参数传递 |
| 9 | `stage9-complete` / `main` | 用户态引导 init、控制台输入、shell、cd、综合 test |

需要检查某一阶段时，在工作区没有未提交修改的情况下执行，例如：

```sh
git switch --detach stage5-complete
make clean
make qemu
# 检查结束后返回最终版
git switch main
make clean
make qemu
```

阶段 9 中用户态引导程序 `user/initcode.S` 被编译为字节数组，由 `userinit()`
加载；它发起 exec 读取 init，init 再 fork/exec 启动 shell。
系统调用按 xv6 的模块组织：进程调用位于 `sysproc.c`，exec、chdir 和
直接控制台 read/write 位于 `sysfile.c`。

## 自动验证

```sh
make test-labs
# 只验证当前第 9 阶段源码
python3 tests/test-labs.py --stages 9
```

测试只依赖 Python 3 标准库。历史阶段从上表标签创建临时源码副本；第 9 阶段
复制当前工作区源码。所有构建、测试盘块改写和额外测试程序均在临时目录进行，
不会改写当前工作区的磁盘镜像。目录及日志位置会在测试开始时输出。

覆盖九阶段编译与运行、键盘/定时器中断、三子进程完整生命周期、盘块写回、
已提交日志的启动恢复，以及最终版在 1/3 CPU 下的 shell 和系统调用回归。
回归包含 exec 参数及 argv[0]、参数数量边界、无效指针、过长参数、
cd 类型检查、跨页堆读写与收缩、退出状态、睡眠唤醒、退格及 Ctrl+U。
额外的 `labprobe` 仅写入临时测试磁盘，不进入默认课程磁盘镜像。
GitHub Actions 运行同一套课程测试。上游通用 `test-xv6.py` 保留供参考，
其中依赖完整文件系统的 usertests/crash 测试超出本项目课程范围。

上游 xv6 的介绍与许可证分别见 `README` 和 `LICENSE`。
