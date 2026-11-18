#!/usr/bin/env python3
"""Export the first three lab checkpoints without switching the working branch."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

from trim_stage3 import trim_stage3

REPO = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dest", type=Path, default=REPO / "acceptance-1-3")
    args = parser.parse_args()
    dest = args.dest.expanduser().resolve()
    refs = {
        f"stage{n}": subprocess.check_output(
            ["git", "rev-parse", f"stage{n}^{{commit}}"], cwd=REPO).decode().strip()
        for n in (1, 2, 3)
    }
    if dest.exists():
        manifest = dest / "SOURCE.json"
        if manifest.is_file() and json.loads(manifest.read_text()) == refs:
            print(f"沿用已准备的目录（不覆盖其中的文件）：{dest}")
            print(f"运行：bash {shlex.quote(str(dest / 'run.sh'))} 1（或 2、3）")
            return
        parser.error(f"目录已存在；请用 --dest 指定新的目录：{dest}")

    dest.mkdir(parents=True)
    for name, commit in refs.items():
        stage_dir = dest / name
        stage_dir.mkdir()
        archive = subprocess.check_output(["git", "archive", commit], cwd=REPO)
        subprocess.run(["tar", "-x", "-C", str(stage_dir)], input=archive, check=True)
    trim_stage3(dest / "stage3")
    shutil.copy2(REPO / "tools/acceptance-run.sh", dest / "run.sh")
    shutil.copy2(REPO / "docs/acceptance-1-3.md", dest / "README.md")
    shutil.copy2(REPO / "docs/acceptance-1-3-questions.md",
                 dest / "acceptance-1-3-questions.md")
    (dest / "SOURCE.json").write_text(json.dumps(refs, indent=2) + "\n")

    # Stop each hart after kvminithart(), before the startup message. This
    # provides observable paging evidence despite stages 1/2 looking alike.
    lines = (dest / "stage2/kernel/main.c").read_text().splitlines()
    line = next(i for i, text in enumerate(lines, 1) if 'printf("cpu %d' in text)
    (dest / "check-stage2.gdb").write_text(f'''set pagination off
set confirm off
set print thread-events off
set architecture riscv:rv64
python
import gdb, os
gdb.execute("target remote 127.0.0.1:%d" % (os.getuid() % 5000 + 25000))
end
set $seen = 0
break kernel/main.c:{line}
commands
silent
printf "CPU %d: satp=0x%lx, mode=%lu\\n", (int)$tp, (unsigned long)$satp, ((unsigned long)$satp >> 60)
if ((unsigned long)$satp >> 60) != 8
  printf "FAIL: Sv39 is not enabled.\\n"
  detach
  quit 1
end
if (unsigned long)$satp != (((unsigned long)8 << 60) | ((unsigned long)kernel_pagetable >> 12))
  printf "FAIL: unexpected kernel page table.\\n"
  detach
  quit 1
end
set $seen = $seen | (1 << (int)$tp)
if $seen == 7
  printf "PASS: all three CPUs use the kernel Sv39 page table.\\n"
  detach
  quit
end
continue
end
continue
''')
    print(f"已准备：{dest}")
    for n in (1, 2, 3):
        print(f"实验 {n}：bash {shlex.quote(str(dest / 'run.sh'))} {n}")
    print("目录中 README.md 是本次验收指南，SOURCE.json 记录各阶段的原始提交。")


if __name__ == "__main__":
    main()
