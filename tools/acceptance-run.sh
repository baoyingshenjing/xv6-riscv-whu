#!/usr/bin/env bash
set -euo pipefail

stage="${1:-}"
case "$stage" in
  1|2|3) ;;
  *) echo "用法：bash run.sh 1|2|3 [--gdb]" >&2; exit 2 ;;
esac

target=qemu
if [[ "${2:-}" == "--gdb" && $# == 2 ]]; then
  target=qemu-gdb
elif [[ $# != 1 ]]; then
  echo "用法：bash run.sh 1|2|3 [--gdb]" >&2
  exit 2
fi

task_root="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$task_root/stage$stage"
exec make "$target" CPUS="${CPUS:-3}"
