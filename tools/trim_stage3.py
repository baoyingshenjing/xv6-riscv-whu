#!/usr/bin/env python3
"""Keep only the files needed by the stage 3 acceptance kernel."""
import argparse
import io
from pathlib import Path
import subprocess
import tarfile

REPO = Path(__file__).resolve().parents[1]
KERNEL_FILES = {
    "entry.S", "start.c", "main.c", "printf.c", "uart.c", "spinlock.c",
    "proc.c", "string.c", "kalloc.c", "vm.c", "kernelvec.S", "trap.c",
    "plic.c", "kernel.ld", "types.h", "param.h", "memlayout.h", "riscv.h",
    "defs.h", "spinlock.h", "proc.h",
}
KEEP = {"Makefile", "README", "README.md", "LICENSE", ".gitignore",
        ".gdbinit.tmpl-riscv", ".clang-format", ".editorconfig", ".dir-locals.el"}
KEEP.update(f"kernel/{name}" for name in KERNEL_FILES)

STAGE3_README = """# xv6：第 1–3 阶段验收版

本目录在 stage3 的基础上保留启动、物理内存、内核页表、定时器和串口中断所需文件。
内核源码与对应 stage3 标签一致；Makefile 已移除后续用户程序、文件系统和磁盘的构建规则。

```sh
make qemu CPUS=3
```

预期先看到三个 CPU 的启动提示，之后约每 3 秒出现一个 `T`；键盘输入应立即回显。
退出 QEMU：先按 Ctrl+A，松开后再按 X。

核心文件：`kernel/entry.S`、`start.c`、`kernel.ld`、`main.c`、`kalloc.c`、`vm.c`、
`kernelvec.S`、`trap.c`、`plic.c`、`uart.c`。需要 GDB 时可运行 `make qemu-gdb`，
再在另一终端连接。重新编译可先运行 `make clean`。

上一级验收目录的 README 和追问精讲包含演示步骤及原理讲解。
"""


def trim_stage3(dest):
    dest = Path(dest).resolve()
    archive = subprocess.check_output(["git", "archive", "stage3"], cwd=REPO)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        originals = {member.name: tar.extractfile(member).read()
                     for member in tar.getmembers() if member.isfile()}

    missing = [name for name in KEEP if name not in originals or not (dest / name).is_file()]
    if missing:
        raise RuntimeError(f"stage3 必要文件缺失：{missing}")
    # Check every file we will remove or rewrite before touching anything.
    affected = (set(originals) - KEEP) | {"Makefile", "README.md"}
    changed = [name for name in sorted(affected)
               if not (dest / name).is_file() or
               (dest / name).read_bytes() != originals[name]]
    if changed:
        raise RuntimeError(f"这些文件被修改或缺失，未进行清理：{changed}")

    makefile = (dest / "Makefile").read_text()
    start = makefile.index("ULIB = ")
    end = makefile.index("-include kernel/*.d user/*.d", start)
    makefile = makefile[:start] + makefile[end:]
    makefile = makefile.replace("U=user\n", "")
    makefile = makefile.replace("-include kernel/*.d user/*.d", "-include kernel/*.d")
    start = makefile.index("clean: ")
    end = makefile.index("# try to generate a unique GDB port", start)
    makefile = (makefile[:start] +
                "clean:\n\trm -f $K/*.o $K/*.d $K/*.asm $K/*.sym $K/kernel .gdbinit\n\n" +
                makefile[end:])
    makefile = makefile.replace("$(wildcard kernel/*.[ch] user/*.[ch] mkfs/*.c)",
                                "$(wildcard kernel/*.[ch])")
    if any(part in makefile for part in ("UPROGS", "ULIB", "fs.img", "mkfs/", "$U/")):
        raise RuntimeError("Makefile 中仍有后续阶段构建规则，未进行清理")

    for name in sorted(set(originals) - KEEP):
        (dest / name).unlink()
    # Only remove known, now-empty directories. Never recursively delete extras.
    for name in (".github/workflows", ".github", "user", "mkfs"):
        directory = dest / name
        if directory.is_dir() and not any(directory.iterdir()):
            directory.rmdir()
    (dest / "Makefile").write_text(makefile)
    (dest / "README.md").write_text(STAGE3_README, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage3", type=Path)
    args = parser.parse_args()
    trim_stage3(args.stage3)
    print(f"已裁剪：{args.stage3.resolve()}")
