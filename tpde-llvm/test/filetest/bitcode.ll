; NOTE: Do not autogenerate
; SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
;
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: llvm-as < %s | tpde-llc --target=x86_64 | %objdump | FileCheck %s -check-prefixes=X64
; RUN: llvm-as < %s | tpde-llc --target=aarch64 | %objdump | FileCheck %s -check-prefixes=ARM64

define void @empty() {
; X64-LABEL: <empty>:
; X64-NEXT: push rbp
; X64-NEXT: mov rbp, rsp
; X64-NEXT: nop word ptr [rax + rax]
; X64-NEXT: sub rsp, 0x30
; X64-NEXT: add rsp, 0x30
; X64-NEXT: pop rbp
; X64-NEXT: ret
; ARM64-LABEL: <empty>:
; ARM64-NEXT:    sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  entry:
    ret void
}
