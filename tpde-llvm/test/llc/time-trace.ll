; NOTE: Do not autogenerate
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: tpde-llc -o /dev/null --time-trace=%t.json
; RUN: FileCheck --input-file=%t.json %s

; CHECK: "traceEvents"

define void @func() {
  ret void
}
