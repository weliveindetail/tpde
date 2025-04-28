; NOTE: Do not autogenerate
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: tpde-lli %s
; RUN: tpde-lli --orc %s

define i32 @main() {
  ret i32 0
}
