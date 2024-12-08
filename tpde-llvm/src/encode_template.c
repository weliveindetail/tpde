// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#if defined(__x86_64__)
    #include <immintrin.h>

    #define TARGET_V1 __attribute__((target("arch=opteron")))
    #define TARGET_V2 __attribute__((target("arch=x86-64-v2")))
    #define TARGET_V3 __attribute__((target("arch=x86-64-v3")))
    #define TARGET_V4 __attribute__((target("arch=x86-64-v4")))
#elif defined(__aarch64__)
    #include <arm_neon.h>

    // ARMv8.0 lacks atomic instructions (would generate libcalls)
    #define TARGET_V1 __attribute__((target("arch=armv8.1-a")))
#else
#error "Unsupported architecture"
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef __int128_t i128;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef __uint128_t u128;

// --------------------------
// loads
// --------------------------

u32 TARGET_V1 loadi8(u8* ptr) { return *ptr; }
u32 TARGET_V1 loadi16(u16* ptr) { return *ptr; }
u32 TARGET_V1 loadi32(u32* ptr) { return *ptr; }
u64 TARGET_V1 loadi64(u64* ptr) { return *ptr; }

struct i24 { i8 data[3]; };
struct i40 { i8 data[5]; };
struct i48 { i8 data[6]; };
struct i56 { i8 data[7]; };

struct i24 TARGET_V1 loadi24(struct i24* ptr) { return *ptr; }
struct i40 TARGET_V1 loadi40(struct i40* ptr) { return *ptr; }
struct i48 TARGET_V1 loadi48(struct i48* ptr) { return *ptr; }
struct i56 TARGET_V1 loadi56(struct i56* ptr) { return *ptr; }

__uint128_t TARGET_V1 loadi128(__uint128_t* ptr) { return *ptr; }

float TARGET_V1 loadf32(float* ptr) { return *ptr; }
double TARGET_V1 loadf64(double* ptr) { return *ptr; }

#ifdef __x86_64__
__m128 TARGET_V1 loadv128(__m128* ptr) { return *ptr; }
__m256 TARGET_V3 loadv256(__m256* ptr) { return *ptr; }
__m512 TARGET_V4 loadv512(__m512* ptr) { return *ptr; }
#endif

#ifdef __aarch64__
uint64x2_t TARGET_V1 loadv128(uint64x2_t *ptr) { return *ptr; }
#endif

// --------------------------
// stores
// --------------------------

void TARGET_V1 storei8(u8* ptr, u8 value) { *ptr = value; }
void TARGET_V1 storei16(u16* ptr, u16 value) { *ptr = value; }
void TARGET_V1 storei32(u32* ptr, u32 value) { *ptr = value; }
void TARGET_V1 storei64(u64* ptr, u64 value) { *ptr = value; }

void TARGET_V1 storei24(struct i24* ptr, struct i24 value) { *ptr = value; }
void TARGET_V1 storei40(struct i40* ptr, struct i40 value) { *ptr = value; }
void TARGET_V1 storei48(struct i48* ptr, struct i48 value) { *ptr = value; }
void TARGET_V1 storei56(struct i56* ptr, struct i56 value) { *ptr = value; }

void TARGET_V1 storei128(__uint128_t* ptr, __uint128_t value) { *ptr = value; }

void TARGET_V1 storef32(float* ptr, float value) { *ptr = value; }
void TARGET_V1 storef64(double* ptr, double value) { *ptr = value; }

#ifdef __x86_64__
void TARGET_V1 storev128(__m128* ptr, __m128 value) { *ptr = value; }
void TARGET_V3 storev256(__m256* ptr, __m256 value) { *ptr = value; }
void TARGET_V4 storev512(__m512* ptr, __m512 value) { *ptr = value; }
#endif

#ifdef __aarch64__
void TARGET_V1 storev128(uint64x2_t *ptr, uint64x2_t value) { *ptr = value; }
#endif

// --------------------------
// integer arithmetic
// --------------------------

u32 TARGET_V1 addi32(u32 a, u32 b) { return (a + b); }
u32 TARGET_V1 subi32(u32 a, u32 b) { return (a - b); }
u32 TARGET_V1 muli32(u32 a, u32 b) { return (a * b); }
u32 TARGET_V1 udivi32(u32 a, u32 b) { return (a / b); }
i32 TARGET_V1 sdivi32(i32 a, i32 b) { return (a / b); }
u32 TARGET_V1 uremi32(u32 a, u32 b) { return (a % b); }
i32 TARGET_V1 sremi32(i32 a, i32 b) { return (a % b); }
u32 TARGET_V1 landi32(u32 a, u32 b) { return (a & b); }
u32 TARGET_V1 lori32(u32 a, u32 b) { return (a | b); }
u32 TARGET_V1 lxori32(u32 a, u32 b) { return (a ^ b); }
u32 TARGET_V1 shli32(u32 a, u32 b) { return (a << b); }
u32 TARGET_V1 shri32(u32 a, u32 b) { return (a >> b); }
i32 TARGET_V1 ashri32(i32 a, i32 b) { return (a >> b); }
i32 TARGET_V1 absi32(i32 a) { return (a < 0) ? -a : a; }

u64 TARGET_V1 addi64(u64 a, u64 b) { return (a + b); }
u64 TARGET_V1 subi64(u64 a, u64 b) { return (a - b); }
u64 TARGET_V1 muli64(u64 a, u64 b) { return (a * b); }
u64 TARGET_V1 udivi64(u64 a, u64 b) { return (a / b); }
i64 TARGET_V1 sdivi64(i64 a, i64 b) { return (a / b); }
u64 TARGET_V1 uremi64(u64 a, u64 b) { return (a % b); }
i64 TARGET_V1 sremi64(i64 a, i64 b) { return (a % b); }
u64 TARGET_V1 landi64(u64 a, u64 b) { return (a & b); }
u64 TARGET_V1 lori64(u64 a, u64 b) { return (a | b); }
u64 TARGET_V1 lxori64(u64 a, u64 b) { return (a ^ b); }
u64 TARGET_V1 shli64(u64 a, u64 b) { return (a << b); }
u64 TARGET_V1 shri64(u64 a, u64 b) { return (a >> b); }
i64 TARGET_V1 ashri64(i64 a, i64 b) { return (a >> b); }
i64 TARGET_V1 absi64(i64 a) { return (a < 0) ? -a : a; }

u128 TARGET_V1 addi128(u128 a, u128 b) { return (a + b); }
u128 TARGET_V1 subi128(u128 a, u128 b) { return (a - b); }
u128 TARGET_V1 muli128(u128 a, u128 b) { return (a * b); }
//u128 TARGET_V1 udivi128(u128 a, u128 b) { return (a / b); }
//i128 TARGET_V1 sdivi128(i128 a, i128 b) { return (a / b); }
//u128 TARGET_V1 uremi128(u128 a, u128 b) { return (a % b); }
//i128 TARGET_V1 sremi128(i128 a, i128 b) { return (a % b); }
u128 TARGET_V1 landi128(u128 a, u128 b) { return (a & b); }
u128 TARGET_V1 lori128(u128 a, u128 b) { return (a | b); }
u128 TARGET_V1 lxori128(u128 a, u128 b) { return (a ^ b); }
u128 TARGET_V1 shli128(u128 a, u128 b) { return (a << b); }
u128 TARGET_V1 shri128(u128 a, u128 b) { return (a >> b); }
i128 TARGET_V1 ashri128(i128 a, i128 b) { return (a >> b); }

// For better codegen when shifting by immediates
u128 TARGET_V1 shli128_lt64(u128 a, u64 amt, u64 iamt) {
    u64 lo = (u64)a << amt;
    u128 hi0 = (u64)a >> iamt; // iamt = 64-amt
    u128 hi1 = (u64)(a >> 64) << amt;
    return (hi0 | hi1) << 64 | lo;
}

u128 TARGET_V1 shli128_ge64(u128 a, u64 amt) {
    return a << (64 + (amt % 64));
}

u128 TARGET_V1 shri128_lt64(u128 a, u64 amt, u64 iamt) {
    u64 lo0 = (u64)a >> amt;
    u128 lo1 = (u64)(a >> 64) << iamt; // iamt = 64-amt
    u128 hi = (u64)(a >> 64) >> amt;
    return hi << 64 | lo0 | lo1;
}

u128 TARGET_V1 shri128_ge64(u128 a, u64 amt) {
    return a >> (64 + (amt % 64));
}

u128 TARGET_V1 ashri128_lt64(u128 a, u64 amt, u64 iamt) {
    u64 lo0 = (u64)a >> amt;
    u128 lo1 = (u64)(a >> 64) << iamt; // iamt = 64-amt
    u128 hi = (i64)(a >> 64) >> amt;
    return hi << 64 | lo0 | lo1;
}

u128 TARGET_V1 ashri128_ge64(i128 a, u64 amt) {
    return a >> (64 + (amt % 64));
}

u16 TARGET_V1 bswapi16(u16 a) { return __builtin_bswap16(a); }
u32 TARGET_V1 bswapi32(u32 a) { return __builtin_bswap32(a); }
u64 TARGET_V1 bswapi48(u64 a) { return __builtin_bswap64(a) >> 16; }
u64 TARGET_V1 bswapi64(u64 a) { return __builtin_bswap64(a); }

u32 TARGET_V1 cttzi32_zero_poison(u32 a) { return __builtin_ctz(a); }
u64 TARGET_V1 cttzi64_zero_poison(u64 a) { return __builtin_ctzll(a); }

u32 TARGET_V1 cttzi8(i8 a) { if ((u8)a == 0) { return 8; } else { return __builtin_ctz(a); }}
u32 TARGET_V1 cttzi16(i16 a) { if ((u16)a == 0) { return 16; } else { return __builtin_ctz(a); }}
u32 TARGET_V1 cttzi32(u32 a) { if (a == 0) { return 32; } else { return __builtin_ctz(a); }}
u64 TARGET_V1 cttzi64(u64 a) { if (a == 0) { return 64; } else { return __builtin_ctzll(a); }}


u32 TARGET_V1 ctlzi8_zero_poison(i8 a) { return __builtin_clz((u32)(u8)a) - 24; }
u32 TARGET_V1 ctlzi16_zero_poison(i16 a) { return __builtin_clz((u32)(u16)a) - 16; }
u32 TARGET_V1 ctlzi32_zero_poison(u32 a) { return __builtin_clz(a); }
u64 TARGET_V1 ctlzi64_zero_poison(u64 a) { return __builtin_clzll(a); }

u32 TARGET_V1 ctlzi8(i8 a) { if ((u8)a == 0) { return 8; } else { return __builtin_clz((u32)(u8)a) - 24; }}
u32 TARGET_V1 ctlzi16(i16 a) { if ((u16)a == 0) { return 16; } else { return __builtin_clz((u32)(u16)a) - 16; }}
u32 TARGET_V1 ctlzi32(u32 a) { if (a == 0) { return 32; } else { return __builtin_clz(a); }}
u64 TARGET_V1 ctlzi64(u64 a) { if (a == 0) { return 64; } else { return __builtin_clzll(a); }}

// --------------------------
// integer overflow
// --------------------------

#define RES_STRUCT(ty) struct res_##ty { ty val; u64 of; };
RES_STRUCT(i8)
RES_STRUCT(u8)
RES_STRUCT(i16)
RES_STRUCT(u16)
RES_STRUCT(i32)
RES_STRUCT(u32)
RES_STRUCT(i64)
RES_STRUCT(u64)
RES_STRUCT(i128)
RES_STRUCT(u128)
#undef RES_STRUCT

// Use regcall on x86-64 to return 128 bit integers + overflow flag in registers
#if defined(__x86_64__)
    #define OF_OP_CC __regcall
#else
    #define OF_OP_CC
#endif
#define OF_OP(ty, inv_ty, op)                                                  \
    OF_OP_CC struct res_##ty TARGET_V1 of_##op##_##ty(inv_ty a, inv_ty b) {    \
        ty    res;                                                             \
        _Bool of = __builtin_##op##_overflow((ty)a, (ty)b, &res);              \
        return (struct res_##ty){res, of};                                     \
    }

#define OF_OPS(width) \
    OF_OP(i##width, u##width, add) \
    OF_OP(i##width, u##width, sub) \
    OF_OP(i##width, u##width, mul) \
    OF_OP(u##width, i##width, add) \
    OF_OP(u##width, i##width, sub) \
    OF_OP(u##width, i##width, mul) \

OF_OPS(8)
OF_OPS(16)
OF_OPS(32)
OF_OPS(64)

// 128-bit mul-overflow is inlined on x86-64, but not on AArch64. Furthermore,
// on AArch64, there is no calling convention to return more than two registers
// (LLVM supports this, but Clang doesn't, because it follows the AAPCS ABI).
// Therefore, code these manually for AArch64.
#if defined(__x86_64__)
OF_OPS(128)
#endif

#undef OF_OPS
#undef OF_OP
#undef OF_OP_CC

// --------------------------
// float arithmetic
// --------------------------

float TARGET_V1 addf32(float a, float b) { return (a + b); }
float TARGET_V1 subf32(float a, float b) { return (a - b); }
float TARGET_V1 mulf32(float a, float b) { return (a * b); }
float TARGET_V1 divf32(float a, float b) { return (a / b); }
//float TARGET_V1 remf32(float a, float b) { return __builtin_fmodf(a, b); }

double TARGET_V1 addf64(double a, double b) { return (a + b); }
double TARGET_V1 subf64(double a, double b) { return (a - b); }
double TARGET_V1 mulf64(double a, double b) { return (a * b); }
double TARGET_V1 divf64(double a, double b) { return (a / b); }
//double TARGET_V1 remf64(double a, double b) { return __builtin_fmod(a, b); }

float TARGET_V1 fnegf32(float a) { return (-a); }
double TARGET_V1 fnegf64(double a) { return (-a); }

float TARGET_V1 fabsf32(float a) { return __builtin_fabsf(a); }
double TARGET_V1 fabsf64(double a) { return __builtin_fabs(a); }

float TARGET_V1 fmaf32(float a, float b, float c) { return a * b + c; }
double TARGET_V1 fmaf64(double a, double b, double c) { return a * b + c; }

// --------------------------
// float conversions
// --------------------------

float TARGET_V1 f64tof32(double a) { return (float)(a); }
double TARGET_V1 f32tof64(float a) { return (double)(a); }

i32 TARGET_V1 f32toi32(float a) { return (i32)a; }
u32 TARGET_V1 f32tou32(float a) { return (u32)a; }
i64 TARGET_V1 f32toi64(float a) { return (i64)a; }
u64 TARGET_V1 f32tou64(float a) { return (u64)a; }
i32 TARGET_V1 f64toi32(double a) { return (i32)a; }
u32 TARGET_V1 f64tou32(double a) { return (u32)a; }
i64 TARGET_V1 f64toi64(double a) { return (i64)a; }
u64 TARGET_V1 f64tou64(double a) { return (u64)a; }

float TARGET_V1 i8tof32(u8 a) { return (float)(i8)a; }
float TARGET_V1 i16tof32(u16 a) { return (float)(i16)a; }
float TARGET_V1 i32tof32(u32 a) { return (float)(i32)a; }
float TARGET_V1 i64tof32(u64 a) { return (float)(i64)a; }
float TARGET_V1 u8tof32(i8 a) { return (float)(u8)a; }
float TARGET_V1 u16tof32(i16 a) { return (float)(u16)a; }
float TARGET_V1 u32tof32(i32 a) { return (float)(u32)a; }
float TARGET_V1 u64tof32(i64 a) { return (float)(u64)a; }

double TARGET_V1 i8tof64(u8 a) { return (double)(i8)a; }
double TARGET_V1 i16tof64(u16 a) { return (double)(i16)a; }
double TARGET_V1 i32tof64(u32 a) { return (double)(i32)a; }
double TARGET_V1 i64tof64(u64 a) { return (double)(i64)a; }
double TARGET_V1 u8tof64(i8 a) { return (double)(u8)a; }
double TARGET_V1 u16tof64(i16 a) { return (double)(u16)a; }
double TARGET_V1 u32tof64(i32 a) { return (double)(u32)a; }
double TARGET_V1 u64tof64(i64 a) { return (double)(u64)a; }

// --------------------------
// extensions
// --------------------------

i32 TARGET_V1 sext_8_to_32(u8 a) { return (i32)(i8)a; }
i64 TARGET_V1 sext_8_to_64(u8 a) { return (i64)(i8)a; }
i32 TARGET_V1 sext_16_to_32(u16 a) { return (i32)(i16)a; }
i64 TARGET_V1 sext_16_to_64(u16 a) { return (i64)(i16)a; }
i64 TARGET_V1 sext_32_to_64(u32 a) { return (i64)(i32)a; }
i32 TARGET_V1 sext_arbitrary_to_32(u32 a, u32 shift) { return ((i32)(a << shift)) >> shift; }
i64 TARGET_V1 sext_arbitrary_to_64(u64 a, u32 shift) { return ((i64)(a << shift)) >> shift; }

i64 TARGET_V1 fill_with_sign64(i64 a) { return (a >> 63); }

u32 TARGET_V1 zext_8_to_32(i8 a) { return (u32)(u8)a; }
u32 TARGET_V1 zext_16_to_32(i16 a) { return (u32)(u16)a; }
u64 TARGET_V1 zext_32_to_64(i32 a) { return (u64)(u32)a; }

// --------------------------
// atomics
// --------------------------

typedef struct CmpXchgRes { u64 orig; bool success; } CmpXchgRes;

CmpXchgRes TARGET_V1 cmpxchg_u64_monotonic_monotonic(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return (CmpXchgRes){cmp, res};
}

CmpXchgRes TARGET_V1 cmpxchg_u64_acquire_monotonic(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    return (CmpXchgRes){cmp, res};
}
CmpXchgRes TARGET_V1 cmpxchg_u64_acquire_acquire(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    return (CmpXchgRes){cmp, res};
}

CmpXchgRes TARGET_V1 cmpxchg_u64_release_monotonic(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return (CmpXchgRes){cmp, res};
}
CmpXchgRes TARGET_V1 cmpxchg_u64_release_acquire(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
    return (CmpXchgRes){cmp, res};
}

CmpXchgRes TARGET_V1 cmpxchg_u64_acqrel_monotonic(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
    return (CmpXchgRes){cmp, res};
}
CmpXchgRes TARGET_V1 cmpxchg_u64_acqrel_acquire(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return (CmpXchgRes){cmp, res};
}

CmpXchgRes TARGET_V1 cmpxchg_u64_seqcst_monotonic(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
    return (CmpXchgRes){cmp, res};
}
CmpXchgRes TARGET_V1 cmpxchg_u64_seqcst_acquire(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
    return (CmpXchgRes){cmp, res};
}
CmpXchgRes TARGET_V1 cmpxchg_u64_seqcst_seqcst(u64* ptr, u64 cmp, u64 new_val) {
    bool res = __atomic_compare_exchange_n(ptr, &cmp, new_val, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return (CmpXchgRes){cmp, res};
}

u32 TARGET_V1 atomic_load_u8_mono(u8* ptr) { return __atomic_load_n(ptr, __ATOMIC_RELAXED); }
u32 TARGET_V1 atomic_load_u16_mono(u16* ptr) { return __atomic_load_n(ptr, __ATOMIC_RELAXED); }
u32 TARGET_V1 atomic_load_u32_mono(u32* ptr) { return __atomic_load_n(ptr, __ATOMIC_RELAXED); }
u64 TARGET_V1 atomic_load_u64_mono(u64* ptr) { return __atomic_load_n(ptr, __ATOMIC_RELAXED); }
u32 TARGET_V1 atomic_load_u8_acq(u8* ptr) { return __atomic_load_n(ptr, __ATOMIC_ACQUIRE); }
u32 TARGET_V1 atomic_load_u16_acq(u16* ptr) { return __atomic_load_n(ptr, __ATOMIC_ACQUIRE); }
u32 TARGET_V1 atomic_load_u32_acq(u32* ptr) { return __atomic_load_n(ptr, __ATOMIC_ACQUIRE); }
u64 TARGET_V1 atomic_load_u64_acq(u64* ptr) { return __atomic_load_n(ptr, __ATOMIC_ACQUIRE); }
u32 TARGET_V1 atomic_load_u8_seqcst(u8* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
u32 TARGET_V1 atomic_load_u16_seqcst(u16* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
u32 TARGET_V1 atomic_load_u32_seqcst(u32* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }
u64 TARGET_V1 atomic_load_u64_seqcst(u64* ptr) { return __atomic_load_n(ptr, __ATOMIC_SEQ_CST); }

// --------------------------
// select
// --------------------------

i32 TARGET_V1 select_i32(u8 cond, i32 val1, i32 val2) { return ((cond & 1) ? val1 : val2); }
i64 TARGET_V1 select_i64(u8 cond, i64 val1, i64 val2) { return ((cond & 1) ? val1 : val2); }
i128 TARGET_V1 select_i128(u8 cond, i128 val1, i128 val2) { return ((cond & 1) ? val1 : val2); }
float TARGET_V1 select_f32(u8 cond, float val1, float val2) { return ((cond & 1) ? val1 : val2); }
double TARGET_V1 select_f64(u8 cond, double val1, double val2) { return ((cond & 1) ? val1 : val2); }

// --------------------------
// float comparisons
// --------------------------

#define FOP_ORD(ty, name, op) u32 TARGET_V1 fcmp_##name##_##ty(ty a, ty b) { return !__builtin_isunordered(a, b) && (a op b); }
#define FOP_UNRD(ty, name, op) u32 TARGET_V1 fcmp_##name##_##ty(ty a, ty b) { return __builtin_isunordered(a, b) || (a op b); }
#define FOPS(ty) FOP_ORD(ty, oeq, ==) \
    FOP_ORD(ty, ogt, >) \
    FOP_ORD(ty, oge, >=) \
    FOP_ORD(ty, olt, <) \
    FOP_ORD(ty, ole, <=) \
    FOP_ORD(ty, one, !=) \
    u32 TARGET_V1 fcmp_ord_##ty(ty a, ty b) { return !__builtin_isunordered(a, b); } \
    FOP_UNRD(ty, ueq, ==) \
    FOP_UNRD(ty, ugt, >) \
    FOP_UNRD(ty, uge, >=) \
    FOP_UNRD(ty, ult, <) \
    FOP_UNRD(ty, ule, <=) \
    FOP_UNRD(ty, une, !=) \
    u32 TARGET_V1 fcmp_uno_##ty(ty a, ty b) { return __builtin_isunordered(a, b); }

FOPS(float)
FOPS(double)


#undef FOP_ORD
#undef FOP_UNORD
#undef FOPS

// --------------------------
// is_fpclass
// --------------------------

#define FOP(ty, name, num) u8 TARGET_V1 is_fpclass_##name##_##ty(u8 c, ty a) { return c | __builtin_isfpclass(a, (num)); }
#define FOPS(ty) \
    FOP(ty, snan, 1<<0) \
    FOP(ty, qnan, 1<<1) \
    FOP(ty, ninf, 1<<2) \
    FOP(ty, nnorm, 1<<3) \
    FOP(ty, nsnorm, 1<<4) \
    FOP(ty, nzero, 1<<5) \
    FOP(ty, pzero, 1<<6) \
    FOP(ty, psnorm, 1<<7) \
    FOP(ty, pnorm, 1<<8) \
    FOP(ty, pinf, 1<<9) \
    FOP(ty, nan, (1<<0)|(1<<1)) \
    FOP(ty, inf, (1<<2)|(1<<9)) \
    FOP(ty, norm, (1<<3)|(1<<8)) \
    FOP(ty, finite, (1<<3)|(1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<8))

FOPS(float)
FOPS(double)

#undef FOPS
#undef FOP

// --------------------------
// prefetch
// --------------------------

void prefetch_rl0(void* addr) { __builtin_prefetch(addr, 0, 0); }
void prefetch_rl1(void* addr) { __builtin_prefetch(addr, 0, 1); }
void prefetch_rl2(void* addr) { __builtin_prefetch(addr, 0, 2); }
void prefetch_rl3(void* addr) { __builtin_prefetch(addr, 0, 3); }

void prefetch_wl0(void* addr) { __builtin_prefetch(addr, 1, 0); }
void prefetch_wl1(void* addr) { __builtin_prefetch(addr, 1, 1); }
void prefetch_wl2(void* addr) { __builtin_prefetch(addr, 1, 2); }
void prefetch_wl3(void* addr) { __builtin_prefetch(addr, 1, 3); }
