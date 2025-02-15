; NOTE: Do not autogenerate
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: tpde-llc --target=x86_64 < %s | llvm-readelf -s - | FileCheck %s
; RUN: tpde-llc --target=aarch64 < %s | llvm-readelf -s - | FileCheck %s

; CHECK: Symbol table '.symtab'
; CHECK-NOT: llvm.donothing

declare void @llvm.donothing()

define void @use() {
  call void @llvm.donothing()
  ret void
}
