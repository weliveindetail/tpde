; SPDX-License-Identifier: LicenseRef-Proprietary

; --------------------------
; Saturating float conversions
; --------------------------

; Clang exposes these only under -fno-strict-float-cast-overflow, which would
; inhibit better code generation for non-saturating conversions on x86-64.
define i32 @f32toi32_sat(float %p) { %r = call i32 @llvm.fptosi.sat(float %p) ret i32 %r }
define i32 @f32tou32_sat(float %p) { %r = call i32 @llvm.fptoui.sat(float %p) ret i32 %r }
define i64 @f32toi64_sat(float %p) { %r = call i64 @llvm.fptosi.sat(float %p) ret i64 %r }
define i64 @f32tou64_sat(float %p) { %r = call i64 @llvm.fptoui.sat(float %p) ret i64 %r }
define i32 @f64toi32_sat(double %p) { %r = call i32 @llvm.fptosi.sat(double %p) ret i32 %r }
define i32 @f64tou32_sat(double %p) { %r = call i32 @llvm.fptoui.sat(double %p) ret i32 %r }
define i64 @f64toi64_sat(double %p) { %r = call i64 @llvm.fptosi.sat(double %p) ret i64 %r }
define i64 @f64tou64_sat(double %p) { %r = call i64 @llvm.fptoui.sat(double %p) ret i64 %r }
