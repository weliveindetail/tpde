; NOTE: Assertions have been autogenerated by test/update_tpde_llc_test_checks.py UTC_ARGS: --version 5
; SPDX-FileCopyrightText: 2025 Contributors to TPDE <https://tpde.org>
; SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

; RUN: tpde-llc --target=x86_64 %s | %objdump | FileCheck %s -check-prefixes=X64
; RUN: tpde-llc --target=aarch64 %s | %objdump | FileCheck %s -check-prefixes=ARM64

%struct.ptr_i32 = type { ptr, i32 }
define i32 @fn_i32_byval_ptr_i32_i32(ptr byval(%struct.ptr_i32) align 8 %0, i32 %1) {
; X64-LABEL: <fn_i32_byval_ptr_i32_i32>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    mov eax, dword ptr [rbp + 0x18]
; X64-NEXT:    lea eax, [rax + rdi]
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <fn_i32_byval_ptr_i32_i32>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    add x17, sp, #0xa0
; ARM64-NEXT:    add x9, x17, #0x0
; ARM64-NEXT:    ldr w1, [x9, #0x8]
; ARM64-NEXT:    add w0, w0, w1
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
entry:
  %addr = getelementptr %struct.ptr_i32, ptr %0, i64 0, i32 1
  %val = load i32, ptr %addr
  %res = add i32 %val, %1
  ret i32 %res
}

define i32 @call_byval(i32 %0) {
; X64-LABEL: <call_byval>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x40
; X64-NEXT:    lea rax, [rbp - 0x40]
; X64-NEXT:    sub rsp, 0x10
; X64-NEXT:    mov rcx, qword ptr [rax]
; X64-NEXT:    mov qword ptr [rsp], rcx
; X64-NEXT:    mov rcx, qword ptr [rax + 0x8]
; X64-NEXT:    mov qword ptr [rsp + 0x8], rcx
; X64-NEXT:  <L0>:
; X64-NEXT:    call <L0>
; X64-NEXT:     R_X86_64_PLT32 fn_i32_byval_ptr_i32_i32-0x4
; X64-NEXT:    add rsp, 0x10
; X64-NEXT:    add rsp, 0x40
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <call_byval>:
; ARM64:         sub sp, sp, #0xb0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    add x1, x29, #0xa0
; ARM64-NEXT:    sub sp, sp, #0x10
; ARM64-NEXT:    ldr x16, [x1]
; ARM64-NEXT:    str x16, [sp]
; ARM64-NEXT:    ldr x16, [x1, #0x8]
; ARM64-NEXT:    str x16, [sp, #0x8]
; ARM64-NEXT:    bl 0x98 <call_byval+0x28>
; ARM64-NEXT:     R_AARCH64_CALL26 fn_i32_byval_ptr_i32_i32
; ARM64-NEXT:    add sp, sp, #0x10
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xb0
; ARM64-NEXT:    ret
entry:
  %val = alloca %struct.ptr_i32, align 8
  %1 = call i32 @fn_i32_byval_ptr_i32_i32(ptr byval(%struct.ptr_i32) align 8 %val, i32 %0)
  ret i32 %1
}

define i128 @fn_byval2(ptr byval({ptr, ptr}) %a, ptr byval(i64) %b, ptr byval(i128) %c) {
; X64-LABEL: <fn_byval2>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    mov rax, qword ptr [rbp + 0x20]
; X64-NEXT:    mov rcx, qword ptr [rbp + 0x10]
; X64-NEXT:    mov qword ptr [rcx], rax
; X64-NEXT:    mov qword ptr [rbp + 0x18], rcx
; X64-NEXT:    mov rax, qword ptr [rbp + 0x30]
; X64-NEXT:    mov rcx, qword ptr [rbp + 0x38]
; X64-NEXT:    mov rdx, rcx
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <fn_byval2>:
; ARM64:         sub sp, sp, #0xb0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    add x17, sp, #0xb0
; ARM64-NEXT:    add x9, x17, #0x0
; ARM64-NEXT:    add x10, x17, #0x10
; ARM64-NEXT:    add x11, x17, #0x20
; ARM64-NEXT:    ldr x10, [x10]
; ARM64-NEXT:    ldr x0, [x9]
; ARM64-NEXT:    str x10, [x0]
; ARM64-NEXT:    str x0, [x9, #0x8]
; ARM64-NEXT:    ldp x11, x0, [x11]
; ARM64-NEXT:    str x0, [x29, #0xa8]
; ARM64-NEXT:    mov x0, x11
; ARM64-NEXT:    ldr x1, [x29, #0xa8]
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xb0
; ARM64-NEXT:    ret
  %l1 = load i64, ptr %b
  %l2 = load ptr, ptr %a
  store i64 %l1, ptr %l2
  %a3 = getelementptr {ptr, ptr, ptr}, ptr %a, i64 0, i32 1
  store ptr %l2, ptr %a3
  %r = load i128, ptr %c
  ret i128 %r
}

define void @call_byval2(ptr %a, ptr %b, ptr %c) {
; X64-LABEL: <call_byval2>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    mov rax, qword ptr [rdi]
; X64-NEXT:    mov qword ptr [rsp], rax
; X64-NEXT:    mov rax, qword ptr [rdi + 0x8]
; X64-NEXT:    mov qword ptr [rsp + 0x8], rax
; X64-NEXT:    mov rax, qword ptr [rsi]
; X64-NEXT:    mov qword ptr [rsp + 0x10], rax
; X64-NEXT:    mov rax, qword ptr [rdx]
; X64-NEXT:    mov qword ptr [rsp + 0x20], rax
; X64-NEXT:    mov rax, qword ptr [rdx + 0x8]
; X64-NEXT:    mov qword ptr [rsp + 0x28], rax
; X64-NEXT:  <L0>:
; X64-NEXT:    call <L0>
; X64-NEXT:     R_X86_64_PLT32 fn_byval2-0x4
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <call_byval2>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    sub sp, sp, #0x30
; ARM64-NEXT:    ldr x16, [x0]
; ARM64-NEXT:    str x16, [sp]
; ARM64-NEXT:    ldr x16, [x0, #0x8]
; ARM64-NEXT:    str x16, [sp, #0x8]
; ARM64-NEXT:    ldr x16, [x1]
; ARM64-NEXT:    str x16, [sp, #0x10]
; ARM64-NEXT:    ldr x16, [x2]
; ARM64-NEXT:    str x16, [sp, #0x20]
; ARM64-NEXT:    ldr x16, [x2, #0x8]
; ARM64-NEXT:    str x16, [sp, #0x28]
; ARM64-NEXT:    bl 0x17c <call_byval2+0x3c>
; ARM64-NEXT:     R_AARCH64_CALL26 fn_byval2
; ARM64-NEXT:    add sp, sp, #0x30
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  %r = call i128 @fn_byval2(ptr %a, ptr %b, ptr %c)
  ret void
}

; byval's are always at least 8-byte-aligned on x86-64/AArch64
define void @fn_byval3(ptr byval(i8) align 1 %a, ptr byval(i32) align 2 %b, ptr byval(i8) %c, ptr %d) {
; X64-LABEL: <fn_byval3>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    movzx eax, byte ptr [rbp + 0x10]
; X64-NEXT:    mov byte ptr [rdi], al
; X64-NEXT:    mov eax, dword ptr [rbp + 0x18]
; X64-NEXT:    mov dword ptr [rdi], eax
; X64-NEXT:    movzx eax, byte ptr [rbp + 0x20]
; X64-NEXT:    mov byte ptr [rdi], al
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <fn_byval3>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    add x17, sp, #0xa0
; ARM64-NEXT:    add x9, x17, #0x0
; ARM64-NEXT:    add x10, x17, #0x8
; ARM64-NEXT:    add x11, x17, #0x10
; ARM64-NEXT:    ldrb w9, [x9]
; ARM64-NEXT:    strb w9, [x0]
; ARM64-NEXT:    ldr w10, [x10]
; ARM64-NEXT:    str w10, [x0]
; ARM64-NEXT:    ldrb w11, [x11]
; ARM64-NEXT:    strb w11, [x0]
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  %la = load i8, ptr %a
  store volatile i8 %la, ptr %d
  %lb = load i32, ptr %b
  store volatile i32 %lb, ptr %d
  %lc = load i8, ptr %c
  store volatile i8 %lc, ptr %d
  ret void
}

define void @call_byval3(ptr %a, ptr %b, ptr %c, ptr %d) {
; X64-LABEL: <call_byval3>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:    sub rsp, 0x20
; X64-NEXT:    movzx eax, byte ptr [rdi]
; X64-NEXT:    mov byte ptr [rsp], al
; X64-NEXT:    mov eax, dword ptr [rsi]
; X64-NEXT:    mov dword ptr [rsp + 0x8], eax
; X64-NEXT:    movzx eax, byte ptr [rdx]
; X64-NEXT:    mov byte ptr [rsp + 0x10], al
; X64-NEXT:    mov rdi, rcx
; X64-NEXT:  <L0>:
; X64-NEXT:    call <L0>
; X64-NEXT:     R_X86_64_PLT32 fn_byval3-0x4
; X64-NEXT:    add rsp, 0x20
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <call_byval3>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    sub sp, sp, #0x20
; ARM64-NEXT:    ldrb w16, [x0]
; ARM64-NEXT:    strb w16, [sp]
; ARM64-NEXT:    ldr w16, [x1]
; ARM64-NEXT:    str w16, [sp, #0x8]
; ARM64-NEXT:    ldrb w16, [x2]
; ARM64-NEXT:    strb w16, [sp, #0x10]
; ARM64-NEXT:    mov x0, x3
; ARM64-NEXT:    bl 0x250 <call_byval3+0x30>
; ARM64-NEXT:     R_AARCH64_CALL26 fn_byval3
; ARM64-NEXT:    add sp, sp, #0x20
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  call void @fn_byval3(ptr %a, ptr %b, ptr %c, ptr %d)
  ret void
}

define void @fn_byval4(ptr byval({ i64, i64 }) %a, ...) {
; X64-LABEL: <fn_byval4>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0xe0
; X64-NEXT:    mov qword ptr [rbp - 0xd8], rdi
; X64-NEXT:    mov qword ptr [rbp - 0xd0], rsi
; X64-NEXT:    mov qword ptr [rbp - 0xc8], rdx
; X64-NEXT:    mov qword ptr [rbp - 0xc0], rcx
; X64-NEXT:    mov qword ptr [rbp - 0xb8], r8
; X64-NEXT:    mov qword ptr [rbp - 0xb0], r9
; X64-NEXT:    test al, al
; X64-NEXT:    je <L0>
; X64-NEXT:    movdqu xmmword ptr [rbp - 0xa8], xmm0
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x98], xmm1
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x88], xmm2
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x78], xmm3
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x68], xmm4
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x58], xmm5
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x48], xmm6
; X64-NEXT:    movdqu xmmword ptr [rbp - 0x38], xmm7
; X64-NEXT:  <L0>:
; X64-NEXT:    add rsp, 0xe0
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <fn_byval4>:
; ARM64:         sub sp, sp, #0x170
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    stp x0, x1, [sp, #0xa0]
; ARM64-NEXT:    stp x2, x3, [sp, #0xb0]
; ARM64-NEXT:    stp x4, x5, [sp, #0xc0]
; ARM64-NEXT:    stp x6, x7, [sp, #0xd0]
; ARM64-NEXT:    stp q0, q1, [sp, #0xe0]
; ARM64-NEXT:    stp q2, q3, [sp, #0x100]
; ARM64-NEXT:    stp q4, q5, [sp, #0x120]
; ARM64-NEXT:    stp q6, q7, [sp, #0x140]
; ARM64-NEXT:    add x17, sp, #0x170
; ARM64-NEXT:    add x9, x17, #0x0
; ARM64-NEXT:    add x17, x17, #0x10
; ARM64-NEXT:    str x17, [x29, #0x160]
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0x170
; ARM64-NEXT:    ret
  ret void
}

define void @call_byval4(ptr %a, ptr %b) {
; X64-LABEL: <call_byval4>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    nop word ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x30
; X64-NEXT:  <L0>:
; X64-NEXT:    call <L0>
; X64-NEXT:     R_X86_64_PLT32 fn_byval4-0x4
; X64-NEXT:    add rsp, 0x30
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <call_byval4>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    nop
; ARM64-NEXT:    bl 0x310 <call_byval4+0x10>
; ARM64-NEXT:     R_AARCH64_CALL26 fn_byval4
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  call void @fn_byval4(ptr %a, ptr %b)
  ret void
}

define i64 @fn_byval5(ptr byval(i64) %a) {
; X64-LABEL: <fn_byval5>:
; X64:         push rbp
; X64-NEXT:    mov rbp, rsp
; X64-NEXT:    push rbx
; X64-NEXT:    nop dword ptr [rax + rax]
; X64-NEXT:    sub rsp, 0x28
; X64-NEXT:    mov rax, qword ptr [rbp + 0x10]
; X64-NEXT:    add rsp, 0x28
; X64-NEXT:    pop rbx
; X64-NEXT:    pop rbp
; X64-NEXT:    ret
;
; ARM64-LABEL: <fn_byval5>:
; ARM64:         sub sp, sp, #0xa0
; ARM64-NEXT:    stp x29, x30, [sp]
; ARM64-NEXT:    mov x29, sp
; ARM64-NEXT:    str x19, [sp, #0x10]
; ARM64-NEXT:    add x17, sp, #0xa0
; ARM64-NEXT:    add x19, x17, #0x0
; ARM64-NEXT:    ldr x19, [x19]
; ARM64-NEXT:    mov x0, x19
; ARM64-NEXT:    ldp x29, x30, [sp]
; ARM64-NEXT:    ldr x19, [sp, #0x10]
; ARM64-NEXT:    add sp, sp, #0xa0
; ARM64-NEXT:    ret
  br label %bb

bb:
  %r = load i64, ptr %a
  ret i64 %r
}
