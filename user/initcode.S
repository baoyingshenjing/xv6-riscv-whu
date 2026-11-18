#include "kernel/syscall.h"

# First user-mode instructions: replace this bootstrap with init from disk.
.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall

# A failed exec must never return into the embedded strings.
failed:
        li a0, 1
        li a7, SYS_exit
        ecall
        j failed

init:
        .string "init"
.p2align 3
argv:
        .dword init
        .dword 0
