// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#ifdef __x86_64__
#include <immintrin.h>

#define TARGET_V1 __attribute__((target("arch=opteron")))
#define TARGET_V2 __attribute__((target("arch=x86-64-v2")))
#define TARGET_V3 __attribute__((target("arch=x86-64-v3")))
#define TARGET_V4 __attribute__((target("arch=x86-64-v4")))
#else
#error "Unsupported architecture"
#endif

#include <stdint.h>
#include <stddef.h>

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

// --------------------------
// arithmetic
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
u128 TARGET_V1 shli128_lt64(u128 a, u64 shift) {
    u64 low = a & 0xFFFFFFFFFFFFFFFF;
    u64 high = a >> 64;

    u64 res_low = low << shift;
    u64 res_high = high << shift;
    res_high |= (low >> (64 - shift));

    u128 res = ((u128)res_high) << 64;
    res |= res_low;
    return res;
}

u128 TARGET_V1 shli128_ge64(u128 a, u64 shift_minus_64) {
    u64 low = a & 0xFFFFFFFFFFFFFFFF;

    return (u128)(low << shift_minus_64);
}

u128 TARGET_V1 shri128_lt64(u128 a, u64 shift) {
    u64 low = a & 0xFFFFFFFFFFFFFFFF;
    u64 high = a >> 64;

    u64 res_low = low >> shift;
    u64 res_high = high >> shift;
    res_low |= (high << (64 - shift));

    u128 res = ((u128)res_high) << 64;
    res |= res_low;
    return res;
}

u128 TARGET_V1 shri128_ge64(u128 a, u64 shift_minus_64) {
    u64 high = a >> 64;

    return (u128)(high >> shift_minus_64);
}

u128 TARGET_V1 ashri128_lt64(u128 a, u64 shift) {
    u64 low = a & 0xFFFFFFFFFFFFFFFF;
    u64 high = a >> 64;

    u64 res_low = low >> shift;
    u64 res_high = (i64)high >> shift;
    res_low |= (high << (64 - shift));

    u128 res = ((u128)res_high) << 64;
    res |= res_low;
    return res;
}

u128 TARGET_V1 ashri128_ge64(i128 a, u64 shift_minus_64) {
    i64 high = a >> 64;

    u128 res = ((u128)(high >> 63)) << 64;
    res |= (uint64_t)(high >> shift_minus_64);
    return res;
}

// --------------------------
// extensions
// --------------------------

i32 TARGET_V1 sext_8_to_32(u8 a) { return (i32)(i8)a; }
i32 TARGET_V1 sext_16_to_32(u16 a) { return (i32)(i16)a; }
i64 TARGET_V1 sext_32_to_64(u32 a) { return (i64)(i32)a; }
i32 TARGET_V1 sext_arbitrary_to_32(u32 a, u32 shift) { return ((i32)(a << shift)) >> shift; }
i64 TARGET_V1 sext_arbitrary_to_64(u64 a, u32 shift) { return ((i64)(a << shift)) >> shift; }

u32 TARGET_V1 zext_8_to_32(i8 a) { return (u32)(u8)a; }
u32 TARGET_V1 zext_16_to_32(i16 a) { return (u32)(u16)a; }
u64 TARGET_V1 zext_32_to_64(i32 a) { return (u64)(u32)a; }
