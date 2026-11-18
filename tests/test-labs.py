#!/usr/bin/env python3
"""Run the nine course checkpoints in isolated source/disk copies (Linux/WSL)."""
import argparse
import os
from pathlib import Path
import re
import select
import shutil
import struct
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
REFS = {n: f"stage{n}" for n in range(1, 9)}
REFS.update({5: "stage5-complete", 8: "stage8-complete"})
ROOT = Path(tempfile.mkdtemp(prefix="xv6-labs-"))


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def run(argv, cwd, log):
    result = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=90)
    (ROOT / log).write_bytes(result.stdout)
    require(result.returncode == 0,
            f"{' '.join(argv)} failed; see {ROOT / log}")


def snapshot(stage):
    dest = ROOT / f"stage{stage}"
    dest.mkdir()
    if stage == 9:
        # Include local source edits and new test files, excluding build outputs
        # through gitignore. The supplied course PDF/tarballs are not build inputs.
        files = subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
            cwd=REPO).decode().split("\0")
        for name in files:
            src = REPO / name
            if not name or not src.is_file() or src.suffix in (".gz", ".pdf"):
                continue
            target = dest / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, target)
    else:
        archive = subprocess.check_output(["git", "archive", REFS[stage]], cwd=REPO)
        subprocess.run(["tar", "-x", "-C", str(dest)], input=archive, check=True)
    goals = ["kernel/kernel"] + (["fs.img"] if stage >= 7 else [])
    run(["make", "-j4"] + goals, dest, f"stage{stage}-build.log")
    return dest


class Qemu:
    def __init__(self, cwd, log, cpus=3, image=None):
        cmd = ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
               "-kernel", "kernel/kernel", "-m", "128M", "-smp", str(cpus),
               "-nographic"]
        if image:
            cmd += ["-global", "virtio-mmio.force-legacy=false", "-drive",
                    f"file={image},if=none,format=raw,id=x0", "-device",
                    "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]
        self.proc = subprocess.Popen(cmd, cwd=cwd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.output = ""
        self.log = ROOT / log

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.proc.terminate()
        try:
            tail, _ = self.proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            tail, _ = self.proc.communicate()
        self.output += tail.decode(errors="replace")
        self.log.write_text(self.output)

    def expect(self, pattern, start=0, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            require("panic:" not in self.output, f"kernel panic; see {self.log}")
            match = re.search(pattern, self.output[start:])
            if match:
                return self.output[start:]
            if select.select([self.proc.stdout], [], [], 0.1)[0]:
                data = os.read(self.proc.stdout.fileno(), 65536)
                require(bool(data), f"QEMU exited; see {self.log}")
                self.output += data.decode(errors="replace")
        raise AssertionError(f"timeout waiting for {pattern!r}; see {self.log}")

    def send(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def command(self, text, timeout=15):
        start = len(self.output)
        self.send(text + "\n")
        return self.expect(r"\$ ", start, timeout)


def boot(q, cpus=3):
    for cpu in range(cpus):
        q.expect(f"cpu {cpu} is booting!")


def disk_check(dest):
    block = (dest / "fs.img").read_bytes()[47 * 1024:48 * 1024]
    paragraph = (dest / "README").read_bytes().split(b"\n\n")[0]
    require(block == paragraph.ljust(1024, b"\0"), "README disk write mismatch")


def early_stage(stage, dest):
    with Qemu(dest, f"stage{stage}-run.log", image="fs.img" if stage >= 7 else None) as q:
        boot(q)
        if stage <= 2:
            return
        q.send("LAB-KEYBOARD\n")
        q.expect("LAB-KEYBOARD")
        q.expect(r"(?m)^T")
        if stage == 4:
            require(q.output.count("get a syscall from proc 1") == 2,
                    "stage 4 must execute two system calls")
        if stage == 5:
            q.expect(r"sbrk: n=-\d+")
            q.expect(r"sbrk: n=\d+")
        if stage >= 6:
            q.expect("init process has no child now, over!")
            for marker in ["sleep test - 1s", "sleep test - 2s",
                           "sleep test - 3s", "exit code: -1"]:
                require(marker in q.output, f"missing {marker}")
            if stage == 6:
                require("sbrk test over!" in q.output, "heap test missing")
                require(len(re.findall(r"wait - pid: \d+ - exit code:", q.output)) == 3,
                        "must reap all three children")
        if stage >= 7:
            require("README (modified):" in q.output, "block test missing")
    if stage >= 7:
        disk_check(dest)


def recovery(dest):
    image = dest / "recovery.img"
    shutil.copy2(dest / "fs.img", image)
    marker = b"LAB-LOG-RECOVERY\n".ljust(1024, b"\0")
    with image.open("r+b") as f:
        f.seek(2 * 1024)
        f.write(struct.pack("<II", 1, 47).ljust(1024, b"\0"))
        f.seek(3 * 1024)
        f.write(marker)
    with Qemu(dest, "stage8-recovery.log", image=image.name) as q:
        q.expect("recovering tail 0 dst 47")
        q.expect("README \\(modified\\):")
    data = image.read_bytes()
    require(struct.unpack_from("<I", data, 2 * 1024)[0] == 0, "log not cleared")
    require(data[47 * 1024:48 * 1024] == marker, "log replay mismatch")


def shell_stage(dest, cpus):
    # Additional probes are installed only in this temporary test image.
    shutil.copy2(REPO / "tests/labprobe.c", dest / "user/labprobe.c")
    run(["make", "user/_labprobe"], dest, f"stage9-probe-build-{cpus}.log")
    run(["mkfs/mkfs", "probe.img", "README", "user/_init", "user/_sh", "user/_test",
         "user/_execchild1", "user/_execchild2", "user/_labprobe"], dest,
        f"stage9-mkfs-{cpus}.log")
    with Qemu(dest, f"stage9-run-{cpus}cpu.log", cpus, "probe.img") as q:
        boot(q, cpus)
        q.expect(r"\$ ")
        out = q.command("labprobe alpha beta")
        require("labcheck: ALL CHECKS PASSED" in out, "syscall regression failed")
        require("cannot cd README" in q.command("cd README"), "cd accepted a file")
        require("cannot cd missing" in q.command("cd missing"), "cd accepted missing path")
        q.command("cd /")
        out = q.command("discard this line\x15test", timeout=20)
        for marker in ["Super Block info:", "README (modified):",
                       "sleep test - 1s", "sleep test - 2s", "sleep test - 3s",
                       "exit code: -1", "test: ALL CHECKS PASSED"]:
            require(marker in out, f"comprehensive test missing {marker}")
        require("sbrk test over!" in q.command("execchildx\x082"), "backspace input failed")
        require("exec missing failed" in q.command("missing"), "unknown command handling")
        require("sbrk test over!" in q.command("execchild2"), "shell did not recover")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stages", nargs="+", type=int, choices=range(1, 10),
                        default=list(range(1, 10)))
    args = parser.parse_args()
    print(f"Logs and isolated builds: {ROOT}", flush=True)
    try:
        for stage in args.stages:
            dest = snapshot(stage)
            if stage == 9:
                for cpus in [1, 3]:
                    shell_stage(dest, cpus)
                    print(f"PASS stage 9 ({cpus} CPU): shell, composite test, syscall regressions",
                          flush=True)
            else:
                early_stage(stage, dest)
                if stage == 8:
                    recovery(dest)
                print(f"PASS stage {stage}: {REFS[stage]}", flush=True)
    except (AssertionError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", flush=True)
        raise SystemExit(1)
    print("ALL REQUESTED LAB CHECKS PASSED", flush=True)


if __name__ == "__main__":
    main()
