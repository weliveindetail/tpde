; NOTE: Do not autogenerate
; SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
;
; SPDX-License-Identifier: LicenseRef-Proprietary

; RUN: tpde-llc %s -o %t.o && clang -o %t %t.o && %t

@alias = dso_local unnamed_addr alias void (), ptr @alias_impl

define void @alias_impl() {
entry:
  ret void
}

define i32 @main() {
  entry:
  call void @alias()
  ret i32 0
}
