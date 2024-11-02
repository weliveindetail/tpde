; NOTE: Do not autogenerate
; SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
;
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: rm -rf %t
; RUN: mkdir %t

; RUN: llvm-as -o %t/out.bc %s
; RUN: tpde_llvm %t/out.bc | llvm-objdump -d -r --no-show-raw-insn --symbolize-operands --no-addresses --x86-asm-syntax=intel - | FileCheck %s -check-prefixes=X64,CHECK --enable-var-scope --dump-input always
; RUN: tpde_llvm --target=aarch64 %t/out.bc | llvm-objdump -d -r --no-show-raw-insn --symbolize-operands --no-addresses - | FileCheck %s -check-prefixes=ARM64,CHECK --enable-var-scope --dump-input always

define void @empty() {
; CHECK-LABEL: empty>:
; X64:    push rbp
; X64:    mov rbp, rsp
; X64:    nop word ptr [rax + rax]
; X64:    sub rsp, 0x30
; X64:    add rsp, 0x30
; X64:    pop rbp
; X64:    ret
; X64:     ...
; ARM64:    sub sp, sp, #0xa0
; ARM64:    stp x29, x30, [sp]
; ARM64:    mov x29, sp
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    nop
; ARM64:    ldp x29, x30, [sp]
; ARM64:    add sp, sp, #0xa0
; ARM64:    ret
; ARM64:     ...
  entry:
    ret void
}
