// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <variant>

#include "base.hpp"
#include "tpde/x64/CompilerX64.hpp"

// Helper macros for assembling in the compiler
#if defined(ASMD)
    #error Got definition for ASM macros from somewhere else. Maybe you included compilers for multiple architectures?
#endif

#define ASMD(...) ASMC(this->derived(), __VA_ARGS__)

namespace tpde_llvm {

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          typename BaseTy>
struct EncodeCompiler {
    using CompilerX64  = tpde::x64::CompilerX64<Adaptor, Derived, BaseTy>;
    using ScratchReg   = typename CompilerX64::ScratchReg;
    using AsmReg       = typename CompilerX64::AsmReg;
    using ValuePartRef = typename CompilerX64::ValuePartRef;

    struct AsmOperand {
        struct Address {
            AsmReg  base;
            AsmReg  index;
            uint8_t scale;
            int32_t disp;

            explicit Address(AsmReg base, int32_t disp = 0)
                : base(base), scale(0), disp(disp) {}

            [[nodiscard]] bool has_index() const noexcept { return scale != 0; }
        };

        // TODO(ts): evaluate the use of std::variant
        // TODO(ts): I don't like the ValuePartRefs but we also don't want to
        // force all the operands into registers at the start of the encoding...
        std::variant<std::monostate,
                     ScratchReg,
                     ValuePartRef,
                     ValuePartRef *,
                     AsmReg,
                     Address>
            state;

        AsmOperand() = default;

        AsmOperand(AsmOperand &) = delete;

        AsmOperand(AsmOperand &&other) noexcept {
            state       = std::move(other.state);
            other.state = std::monostate{};
        }

        AsmOperand &operator=(const AsmOperand &) noexcept = delete;

        AsmOperand &operator=(AsmOperand &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            state       = std::move(other.state);
            other.state = std::monostate{};
            return *this;
        }

        // ReSharper disable CppNonExplicitConvertingConstructor
        // NOLINTBEGIN(*-explicit-constructor)

        // reg can't be overwritten
        AsmOperand(AsmReg reg) noexcept : state{reg} {}

        // no salvaging
        AsmOperand(const ScratchReg &reg) noexcept {
            assert(!reg.cur_reg.invalid());
            state = reg.cur_reg;
        }

        // salvaging
        AsmOperand(ScratchReg &&reg) noexcept {
            assert(!reg.cur_reg.invalid());
            state = std::move(reg);
        }

        // no salvaging
        AsmOperand(ValuePartRef &ref) noexcept {
            // TODO(ts): check if it is a variable_ref/frame_ptr and then
            // turning it into an Address?
            assert(!ref.is_const);
            state = &ref;
        }

        // salvaging
        AsmOperand(ValuePartRef &&ref) noexcept {
            assert(!ref.is_const);
            state = std::move(ref);
        }

        AsmOperand(Address addr) noexcept { state = addr; }

        // NOLINTEND(*-explicit-constructor)
        // ReSharper restore CppNonExplicitConvertingConstructor

        [[nodiscard]] bool is_addr() const noexcept {
            return std::holds_alternative<Address>(state);
        }

        [[nodiscard]] Address &addr() noexcept {
            return std::get<Address>(state);
        }

        AsmReg as_reg() noexcept;
        void   try_salvage(AsmReg &, ScratchReg &, u8 bank) noexcept;
        void   reset() noexcept;
    };

    CompilerX64 *derived() noexcept {
        return static_cast<CompilerX64 *>(static_cast<Derived *>(this));
    }

    const CompilerX64 *derived() const noexcept {
        return static_cast<const CompilerX64 *>(
            static_cast<const Derived *>(this));
    }

    static bool reg_needs_avx512(AsmReg reg) noexcept {
        if (reg.id() > AsmReg::XMM15) {
            return true;
        }
        return false;
    }

    [[nodiscard]] bool has_avx() const noexcept {
        return derived()->has_cpu_feats(CompilerX64::CPU_AVX);
    }

    // TODO(ts): also generate overloads with IRValueRef params/results
    // that will automagically do part management for us?
    void encode_load8(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load16(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load24(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load32(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load40(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load48(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load56(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load64(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_load128(AsmOperand  ptr,
                        ScratchReg &result0,
                        ScratchReg &result1) noexcept;
    void encode_loadf32(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_loadf64(AsmOperand ptr, ScratchReg &result) noexcept;
    void encode_loadv128(AsmOperand ptr, ScratchReg &result) noexcept;
    [[nodiscard]] bool encode_loadv256(AsmOperand  ptr,
                                       ScratchReg &result) noexcept;
    [[nodiscard]] bool encode_loadv512(AsmOperand  ptr,
                                       ScratchReg &result) noexcept;
};

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
typename EncodeCompiler<Adaptor, Derived, BaseTy>::AsmReg
    EncodeCompiler<Adaptor, Derived, BaseTy>::AsmOperand::as_reg() noexcept {
    if (std::holds_alternative<ScratchReg>(state)) {
        return std::get<ScratchReg>(state).cur_reg;
    }
    if (std::holds_alternative<ValuePartRef>(state)) {
        return std::get<ValuePartRef>(state).alloc_reg();
    }
    if (std::holds_alternative<ValuePartRef *>(state)) {
        return std::get<ValuePartRef *>(state)->alloc_reg();
    }
    if (std::holds_alternative<AsmReg>(state)) {
        return std::get<AsmReg>(state);
    }
    // TODO(ts): allow mem operands with scratchreg param?
    assert(0);
    exit(1);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::AsmOperand::try_salvage(
    AsmReg &dst, ScratchReg &dst_scratch, const u8 bank) noexcept {
    if (std::holds_alternative<ScratchReg>(state)) {
        assert(std::get<ScratchReg>(state).compiler->register_file.reg_bank(
                   std::get<ScratchReg>(state).cur_reg)
               == bank);
        dst_scratch = std::move(std::get<ScratchReg>(state));
        dst         = dst_scratch.cur_reg;
        return;
    }
    if (std::holds_alternative<ValuePartRef>(state)) {
        auto &ref = std::get<ValuePartRef>(state);
        assert(ref.bank() == bank);
        if (ref.can_salvage()) {
            auto reg = ref.salvage();
            dst_scratch.alloc_specific(reg);
            dst = reg;
            return;
        }
        // dst = std::get<ValuePartRef>(state).alloc_reg();
        // return;
    }

    dst = dst_scratch.alloc_from_bank(bank);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::AsmOperand::reset() noexcept {
    state = std::monostate{};
}

// these are mostly derived from what tpde-asmgen currently generates
// with some ideas on how it could be improved though I'm not sure what is
// technically possible
template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load8(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
        dst_reg          = dst_reg_scratch.alloc_from_bank(0);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
        ptr.try_salvage(dst_reg, dst_reg_scratch, 0);
    }
    assert(!dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m8, dst_reg, mem_op);
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load16(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
        dst_reg          = dst_reg_scratch.alloc_from_bank(0);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
        ptr.try_salvage(dst_reg, dst_reg_scratch, 0);
    }
    assert(!dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m16, dst_reg, mem_op);
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load24(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // bb.0.entry:
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:gr32 = MOVZX32rm16 %0:gr64, 1, $noreg, 0, $noreg :: (load (s16) from
    // %ir.num, align 1); example.c:7:12 %2:gr32 = MOVZX32rm8 %0:gr64, 1,
    // $noreg, 2, $noreg :: (load (s8) from %ir.num + 2); example.c:7:12 %3:gr32
    // = SHL32ri %2:gr32(tied-def 0), 16, implicit-def dead $eflags;
    // example.c:7:12 %4:gr32 = ADD32rr_DB %1:gr32(tied-def 0), killed %3:gr32,
    // implicit-def dead $eflags; example.c:7:12 $eax = COPY %4:gr32;
    // example.c:7:5 RET 0, $eax; example.c:7:5


    // %1:gr32 = MOVZX32rm16 %0:gr64, 1, $noreg, 0, $noreg :: (load (s16) from
    // %ir.num, align 1); example.c:7:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }

    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(0);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m16, op0_dst_reg, op0_mem_op);


    // %2:gr32 = MOVZX32rm8 %0:gr64, 1, $noreg, 2, $noreg :: (load (s8) from
    // %ir.num + 2); example.c:7:12
    AsmReg     op1_dst_reg;
    ScratchReg op1_dst_reg_scratch{derived()};

    FeMem op1_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 2 > 0);
        op1_mem_op  = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 2);
        op1_dst_reg = op1_dst_reg_scratch.alloc_from_bank(0);
    } else {
        op1_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 2);
        ptr.try_salvage(op1_dst_reg, op1_dst_reg_scratch, 0);
    }

    assert(!op1_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m8, op1_dst_reg, op1_mem_op);

    // ptr dead after this point
    ptr.reset();


    // %3:gr32 = SHL32ri %2:gr32(tied-def 0), 16, implicit-def dead $eflags;
    // example.c:7:12
    AsmReg     op2_dst_reg;
    ScratchReg op2_dst_reg_scratch{derived()};

    // can salvage op1
    op2_dst_reg_scratch = std::move(op1_dst_reg_scratch);
    // op1_dst_reg_scratch dead after this point
    op2_dst_reg         = op2_dst_reg_scratch.cur_reg;
    assert(!op2_dst_reg_scratch.cur_reg.invalid());

    ASMD(SHL32ri, op2_dst_reg, 16);


    // %4:gr32 = ADD32rr_DB %1:gr32(tied-def 0), killed %3:gr32, implicit-def
    // dead $eflags; example.c:7:12
    AsmReg     op3_dst_reg;
    ScratchReg op3_dst_reg_scratch{derived()};

    // can salvage op0
    op3_dst_reg_scratch = std::move(op0_dst_reg_scratch);
    // op0_dst_reg_scratch dead after this point
    op3_dst_reg         = op3_dst_reg_scratch.cur_reg;
    assert(!op3_dst_reg_scratch.cur_reg.invalid());

    ASMD(ADD32rr, op3_dst_reg, op2_dst_reg);

    // op2 dead after this point
    op2_dst_reg_scratch.reset();


    // $eax = COPY %4:gr32; example.c:7:5
    // RET 0, $eax; example.c:7:5

    // result comes from op3_dst_reg_scratch
    result = std::move(op3_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load32(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
        dst_reg          = dst_reg_scratch.alloc_from_bank(0);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
        ptr.try_salvage(dst_reg, dst_reg_scratch, 0);
    }
    assert(!dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV32rm, dst_reg, mem_op);
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load40(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from %ir.num, align 1); example.c:7:12
    // %2:gr64 = SUBREG_TO_REG 0, killed %1:gr32, %subreg.sub_32bit; example.c:7:12
    // %3:gr32 = MOVZX32rm8 %0:gr64,1, $noreg, 4, $noreg :: (load (s8) from %ir.num + 4); example.c:7:12
    // %4:gr64 = SUBREG_TO_REG 0, killed %3:gr32, %subreg.sub_32bit; example.c:7:12
    // %5:gr64 = SHL64ri %4:gr64(tied-def 0), 32, implicit-def dead $eflags; example.c:7:12
    // %6:gr64 = ADD64rr_DB %2:gr64(tied-def 0), killed %5:gr64, implicit-def dead $eflags; example.c:7:12
    // $rax = COPY %6:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5
    // clang-format on


    // %1:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from
    // %ir.num, align 1); example.c:7:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }

    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(0);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV32rm, op0_dst_reg, op0_mem_op);

    // %2:gr64 = SUBREG_TO_REG 0, killed %1:gr32, %subreg.sub_32bit;
    // example.c:7:12
    AsmReg     op1_dst_reg;
    ScratchReg op1_dst_reg_scratch{derived()};

    // pseudo-op
    // can salvage op0
    op1_dst_reg         = op0_dst_reg;
    op1_dst_reg_scratch = std::move(op0_dst_reg_scratch);
    assert(!op1_dst_reg_scratch.cur_reg.invalid());


    // %3:gr32 = MOVZX32rm8 %0:gr64, 1, $noreg, 4, $noreg :: (load (s8) from
    // %ir.num + 4); example.c:7:12
    AsmReg     op2_dst_reg;
    ScratchReg op2_dst_reg_scratch{derived()};

    // ptr is killed here
    FeMem op2_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 4 > 0);
        op2_mem_op  = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 4);
        op2_dst_reg = op2_dst_reg_scratch.alloc_from_bank(0);
    } else {
        op2_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 4);
        ptr.try_salvage(op2_dst_reg, op2_dst_reg_scratch, 0);
    }

    assert(!op2_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m8, op2_dst_reg, op2_mem_op);

    // ptr dead after this point
    ptr.reset();


    // %4:gr64 = SUBREG_TO_REG 0, killed %3:gr32, %subreg.sub_32bit;
    // example.c:7:12
    AsmReg     op3_dst_reg;
    ScratchReg op3_dst_reg_scratch{derived()};

    // pseudo-op
    // can salvage op2
    op3_dst_reg         = op2_dst_reg;
    op3_dst_reg_scratch = std::move(op2_dst_reg_scratch);
    assert(!op3_dst_reg_scratch.cur_reg.invalid());


    // %5:gr64 = SHL64ri %4:gr64(tied-def 0), 32, implicit-def dead $eflags;
    // example.c:7:12
    AsmReg     op4_dst_reg;
    ScratchReg op4_dst_reg_scratch{derived()};

    // can salvage op3
    op4_dst_reg_scratch = std::move(op3_dst_reg_scratch);
    // op1_dst_reg_scratch dead after this point
    op4_dst_reg         = op4_dst_reg_scratch.cur_reg;
    assert(!op4_dst_reg_scratch.cur_reg.invalid());

    ASMD(SHL64ri, op4_dst_reg, 32);


    // %6:gr64 = ADD64rr_DB %2:gr64(tied-def 0), killed %5:gr64, implicit-def
    // dead $eflags; example.c:7:12
    AsmReg     op5_dst_reg;
    ScratchReg op5_dst_reg_scratch{derived()};

    // can salvage op1
    op5_dst_reg_scratch = std::move(op1_dst_reg_scratch);
    // op0_dst_reg_scratch dead after this point
    op5_dst_reg         = op5_dst_reg_scratch.cur_reg;
    assert(!op5_dst_reg_scratch.cur_reg.invalid());

    ASMD(ADD64rr, op5_dst_reg, op4_dst_reg);

    // op4 dead after this point
    op4_dst_reg_scratch.reset();


    // $rax = COPY %6:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5

    // result comes from op5_dst_reg_scratch
    result = std::move(op5_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load48(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from %ir.num, align 1); example.c:7:12
    // %2:gr64 = SUBREG_TO_REG 0, killed %1:gr32, %subreg.sub_32bit; example.c:7:12
    // %3:gr32 = MOVZX32rm16 %0:gr64,1, $noreg, 4, $noreg :: (load (s8) from %ir.num + 4); example.c:7:12
    // %4:gr64 = SUBREG_TO_REG 0, killed %3:gr32, %subreg.sub_32bit; example.c:7:12
    // %5:gr64 = SHL64ri %4:gr64(tied-def 0), 32, implicit-def dead $eflags; example.c:7:12
    // %6:gr64 = ADD64rr_DB %2:gr64(tied-def 0), killed %5:gr64, implicit-def dead $eflags; example.c:7:12
    // $rax = COPY %6:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5
    // clang-format on


    // %1:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from
    // %ir.num, align 1); example.c:7:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }

    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(0);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV32rm, op0_dst_reg, op0_mem_op);

    // %2:gr64 = SUBREG_TO_REG 0, killed %1:gr32, %subreg.sub_32bit;
    // example.c:7:12
    AsmReg     op1_dst_reg;
    ScratchReg op1_dst_reg_scratch{derived()};

    // pseudo-op
    // can salvage op0
    op1_dst_reg         = op0_dst_reg;
    op1_dst_reg_scratch = std::move(op0_dst_reg_scratch);
    assert(!op1_dst_reg_scratch.cur_reg.invalid());


    // %3:gr32 = MOVZX32rm16 %0:gr64, 1, $noreg, 4, $noreg :: (load (s8) from
    // %ir.num + 4); example.c:7:12
    AsmReg     op2_dst_reg;
    ScratchReg op2_dst_reg_scratch{derived()};

    // ptr is killed here
    FeMem op2_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 4 > 0);
        op2_mem_op  = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 4);
        op2_dst_reg = op2_dst_reg_scratch.alloc_from_bank(0);
    } else {
        op2_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 4);
        ptr.try_salvage(op2_dst_reg, op2_dst_reg_scratch, 0);
    }

    assert(!op2_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m16, op2_dst_reg, op2_mem_op);

    // ptr dead after this point
    ptr.reset();


    // %4:gr64 = SUBREG_TO_REG 0, killed %3:gr32, %subreg.sub_32bit;
    // example.c:7:12
    AsmReg     op3_dst_reg;
    ScratchReg op3_dst_reg_scratch{derived()};

    // pseudo-op
    // can salvage op2
    op3_dst_reg         = op2_dst_reg;
    op3_dst_reg_scratch = std::move(op2_dst_reg_scratch);
    assert(!op3_dst_reg_scratch.cur_reg.invalid());


    // %5:gr64 = SHL64ri %4:gr64(tied-def 0), 32, implicit-def dead $eflags;
    // example.c:7:12
    AsmReg     op4_dst_reg;
    ScratchReg op4_dst_reg_scratch{derived()};

    // can salvage op3
    op4_dst_reg_scratch = std::move(op3_dst_reg_scratch);
    // op1_dst_reg_scratch dead after this point
    op4_dst_reg         = op4_dst_reg_scratch.cur_reg;
    assert(!op4_dst_reg_scratch.cur_reg.invalid());

    ASMD(SHL64ri, op4_dst_reg, 32);


    // %6:gr64 = ADD64rr_DB %2:gr64(tied-def 0), killed %5:gr64, implicit-def
    // dead $eflags; example.c:7:12
    AsmReg     op5_dst_reg;
    ScratchReg op5_dst_reg_scratch{derived()};

    // can salvage op1
    op5_dst_reg_scratch = std::move(op1_dst_reg_scratch);
    // op0_dst_reg_scratch dead after this point
    op5_dst_reg         = op5_dst_reg_scratch.cur_reg;
    assert(!op5_dst_reg_scratch.cur_reg.invalid());

    ASMD(ADD64rr, op5_dst_reg, op4_dst_reg);

    // op4 dead after this point
    op4_dst_reg_scratch.reset();


    // $rax = COPY %6:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5

    // result comes from op5_dst_reg_scratch
    result = std::move(op5_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load56(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    // bb.0.entry:
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:gr32 = MOVZX32rm16 %0:gr64, 1, $noreg, 4, $noreg :: (load (s16) from %ir.num + 4, align 1); example.c:7:12
    // %2:gr32 = MOVZX32rm8 %0:gr64, 1, $noreg, 6, $noreg :: (load (s8) from %ir.num + 6); example.c:7:12
    // %3:gr32 = SHL32ri %2:gr32(tied-def 0), 16, implicit-def dead $eflags; example.c:7:12
    // %4:gr32 = ADD32rr_DB %1:gr32(tied-def 0), killed %3:gr32, implicit-def dead $eflags; example.c:7:12
    // %6:gr64 = IMPLICIT_DEF; example.c:7:12
    // %5:gr64 = INSERT_SUBREG %6:gr64(tied-def 0), killed %4:gr32, %subreg.sub_32bit; example.c:7:12
    // %7:gr64 = SHL64ri %5:gr64(tied-def 0), 32, implicit-def dead $eflags; example.c:7:12
    // %8:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from %ir.num, align 1); example.c:7:12
    // %9:gr64 = SUBREG_TO_REG 0, killed %8:gr32, %subreg.sub_32bit; example.c:7:12
    // %10:gr64 = ADD64rr_DB %9:gr64(tied-def 0), killed %7:gr64, implicit-def dead $eflags; example.c:7:12
    // $rax = COPY %10:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5
    // clang-format on


    // %1:gr32 = MOVZX32rm16 %0:gr64, 1, $noreg, 4, $noreg :: (load (s16) from
    // %ir.num + 4, align 1); example.c:7:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 4 > 0);
        op0_mem_op = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 4);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 4);
    }

    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(0);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m16, op0_dst_reg, op0_mem_op);


    // %2:gr32 = MOVZX32rm8 %0:gr64, 1, $noreg, 6, $noreg :: (load (s8) from
    // %ir.num + 6); example.c:7:12
    AsmReg     op1_dst_reg;
    ScratchReg op1_dst_reg_scratch{derived()};

    FeMem op1_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 6 > 0);
        op1_mem_op = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 6);
    } else {
        op1_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 6);
    }

    op1_dst_reg = op1_dst_reg_scratch.alloc_from_bank(0);
    assert(!op1_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOVZXr32m8, op1_dst_reg, op1_mem_op);


    // %3:gr32 = SHL32ri %2:gr32(tied-def 0), 16, implicit-def dead $eflags;
    // example.c:7:12
    AsmReg     op2_dst_reg;
    ScratchReg op2_dst_reg_scratch{derived()};

    // can salvage op1
    op2_dst_reg_scratch = std::move(op1_dst_reg_scratch);
    op2_dst_reg         = op2_dst_reg_scratch.cur_reg;

    ASMD(SHL32ri, op2_dst_reg, 16);


    // %4:gr32 = ADD32rr_DB %1:gr32(tied-def 0), killed %3:gr32, implicit-def
    // dead $eflags; example.c:7:12
    AsmReg     op3_dst_reg;
    ScratchReg op3_dst_reg_scratch{derived()};

    // can salvage op0
    op3_dst_reg_scratch = std::move(op0_dst_reg_scratch);
    op3_dst_reg         = op3_dst_reg_scratch.cur_reg;

    ASMD(ADD32rr, op3_dst_reg, op2_dst_reg);

    // op2 is dead after this point
    op2_dst_reg_scratch.reset();


    // %6:gr64 = IMPLICIT_DEF; example.c:7:12
    // pseudo-op, no register allocated


    // %5:gr64 = INSERT_SUBREG %6:gr64(tied-def 0), killed %4:gr32,
    // %subreg.sub_32bit; example.c:7:12
    AsmReg     op4_dst_reg;
    ScratchReg op4_dst_reg_scratch{derived()};

    // can salvage from op3
    op4_dst_reg_scratch = std::move(op3_dst_reg_scratch);
    op4_dst_reg         = op4_dst_reg_scratch.cur_reg;

    // INSERT_SUBREG with IMPLICIT_DEF as first operand is a no-op
    // TODO(ts): llvm cannot optimize away the destructor for the op3
    // ScratchReg, so an autogenerator needs to special-case no-ops


    // %7:gr64 = SHL64ri %5:gr64(tied-def 0), 32, implicit-def dead $eflags;
    // example.c:7:12
    AsmReg     op6_dst_reg;
    ScratchReg op6_dst_reg_scratch{derived()};

    // can salvage op4
    op6_dst_reg_scratch = std::move(op4_dst_reg_scratch);
    op6_dst_reg         = op6_dst_reg_scratch.cur_reg;

    ASMD(SHL64ri, op6_dst_reg, 32);


    // %8:gr32 = MOV32rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s32) from
    // %ir.num, align 1); example.c:7:12
    AsmReg     op7_dst_reg;
    ScratchReg op7_dst_reg_scratch{derived()};

    // ptr can be salvaged
    FeMem op7_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op7_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
        op7_dst_reg      = op7_dst_reg_scratch.alloc_from_bank(0);
    } else {
        op7_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
        ptr.try_salvage(op7_dst_reg, op7_dst_reg_scratch, 0);
    }
    assert(!op7_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV32rm, op7_dst_reg, op7_mem_op);

    // ptr is dead after this
    ptr.reset();


    // %9:gr64 = SUBREG_TO_REG 0, killed %8:gr32, %subreg.sub_32bit;
    // example.c:7:12
    AsmReg     op8_dst_reg;
    ScratchReg op8_dst_reg_scratch{derived()};

    // can salvage op7
    op8_dst_reg_scratch = std::move(op7_dst_reg_scratch);
    op8_dst_reg         = op8_dst_reg_scratch.cur_reg;

    // SUBREG_TO_REG is a no-op


    // %10:gr64 = ADD64rr_DB %9:gr64(tied-def 0), killed %7:gr64, implicit-def
    // dead $eflags; example.c:7:12
    AsmReg     op9_dst_reg;
    ScratchReg op9_dst_reg_scratch{derived()};

    // can salvage op8
    op9_dst_reg_scratch = std::move(op8_dst_reg_scratch);
    op9_dst_reg         = op9_dst_reg_scratch.cur_reg;

    ASMD(ADD64rr, op9_dst_reg, op6_dst_reg);

    // op6 is dead after this point
    op6_dst_reg_scratch.reset();


    // $rax = COPY %10:gr64; example.c:7:5
    // RET 0, $rax; example.c:7:5

    // result comes from op9_dst_scratch
    result = std::move(op9_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load64(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
        dst_reg          = dst_reg_scratch.alloc_from_bank(0);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
        ptr.try_salvage(dst_reg, dst_reg_scratch, 0);
    }
    assert(!dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV64rm, dst_reg, mem_op);
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_load128(
    AsmOperand ptr, ScratchReg &result0, ScratchReg &result1) noexcept {
    // clang-format off
    // liveins: $rax
    // %0:gr64 = COPY killed $rax
    // %1:gr64 = MOV64rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s64) from %ir .0, align 16, !tbaa !12)
    // %2:gr64 = MOV64rm killed %0:gr64, 1, $noreg, 8, $noreg :: (load (s64) from %ir .0 + 8, basealign 16, !tbaa !12)
    // $rax = COPY killed %1:gr64
    // $rcx = COPY killed %2:gr64
    // RET 0, killed $rax, killed $rcx
    // clang-format on


    // %1:gr64 = MOV64rm %0:gr64, 1, $noreg, 0, $noreg :: (load (s64) from %ir
    // .0, align 16, !tbaa !12)
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    FeMem op0_mem_op;
    // cannot salvage ptr
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(0);

    ASMD(MOV64rm, op0_dst_reg, op0_mem_op);


    // %2:gr64 = MOV64rm killed %0:gr64, 1, $noreg, 8, $noreg :: (load (s64)
    // from %ir .0 + 8, basealign 16, !tbaa !12)
    AsmReg     op1_dst_reg;
    ScratchReg op1_dst_reg_scratch{derived()};

    // can salvage ptr
    FeMem op1_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        assert(addr.disp + 8 > 0);
        op1_mem_op  = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp + 8);
        op1_dst_reg = op1_dst_reg_scratch.alloc_from_bank(0);
    } else {
        op1_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 8);
        ptr.try_salvage(op1_dst_reg, op1_dst_reg_scratch, 0);
    }
    assert(!op1_dst_reg_scratch.cur_reg.invalid());

    ASMD(MOV64rm, op1_dst_reg, op1_mem_op);

    // ptr is dead after this point
    ptr.reset();


    // $rax = COPY killed %1:gr64
    // $rcx = COPY killed %2:gr64
    // RET 0, killed $rax, killed $rcx
    result0 = std::move(op0_dst_reg_scratch);
    result1 = std::move(op1_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_loadf32(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    dst_reg = dst_reg_scratch.alloc_from_bank(1);
    assert(!dst_reg_scratch.cur_reg.invalid());

    if (has_avx()) {
        ASMD(VMOVSSrm, dst_reg, mem_op);
    } else {
        ASMD(SSE_MOVSSrm, dst_reg, mem_op);
    }
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_loadf64(
    AsmOperand ptr, ScratchReg &result) noexcept {
    AsmReg     dst_reg;
    ScratchReg dst_reg_scratch{derived()};

    FeMem mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        mem_op           = FE_MEM(addr.base,
                        addr.scale,
                        addr.scale ? addr.index : FE_NOREG,
                        addr.disp);
    } else {
        mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    dst_reg = dst_reg_scratch.alloc_from_bank(1);
    assert(!dst_reg_scratch.cur_reg.invalid());

    if (has_avx()) {
        ASMD(VMOVSDrm, dst_reg, mem_op);
    } else {
        ASMD(SSE_MOVSDrm, dst_reg, mem_op);
    }
    ptr.reset();

    result = std::move(dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_loadv128(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    //   liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:vr128 = MOVAPSrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s128) from %ir.ptr); example.c:13:12
    // $xmm0 = COPY %1:vr128; example.c:13:5
    // RET 0, $xmm0; example.c:13:5
    // clang-format on


    // %1:vr128 = MOVAPSrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s128) from
    // %ir.ptr); example.c:13:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    // ptr has different bank -> no salvaging
    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(1);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    if (has_avx()) {
        ASMD(VMOVAPS128rm, op0_dst_reg, op0_mem_op);
    } else {
        ASMD(SSE_MOVAPSrm, op0_dst_reg, op0_mem_op);
    }

    // ptr is dead after this point
    ptr.reset();

    // $xmm0 = COPY %1:vr128; example.c:13:5
    // RET 0, $xmm0; example.c:13:5
    result = std::move(op0_dst_reg_scratch);
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
bool EncodeCompiler<Adaptor, Derived, BaseTy>::encode_loadv256(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:vr256 = VMOVAPSYrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s256) from %ir.ptr); example.c:13:12
    // $ymm0 = COPY %1:vr256; example.c:13:5
    // RET 0, $ymm0; example.c:13:5
    // clang-format on

    // this function requires AVX because of VMOVAPSY
    if (!derived()->has_cpu_feats(CompilerX64::CPU_AVX)) {
        return false;
    }


    // %1:vr256 = VMOVAPSYrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s256) from
    // %ir.ptr); example.c:13:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    // ptr has different bank -> no salvaging
    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(1);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    if (reg_needs_avx512(op0_dst_reg)) {
        assert(derived()->has_cpu_feats(CompilerX64::CPU_AVX512F));
    }
    ASMD(VMOVAPS256rm, op0_dst_reg, op0_mem_op);

    // ptr is dead after this point
    ptr.reset();

    // $ymm0 = COPY %1:vr256; example.c:13:5
    // RET 0, $ymm0; example.c:13:5
    result = std::move(op0_dst_reg_scratch);
    return true;
}

template <typename Adaptor,
          typename Derived,
          template <typename, typename, typename>
          class BaseTy>
bool EncodeCompiler<Adaptor, Derived, BaseTy>::encode_loadv512(
    AsmOperand ptr, ScratchReg &result) noexcept {
    // clang-format off
    // liveins: $rdi
    // %0:gr64 = COPY $rdi
    // %1:vr512 = VMOVAPSZrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s512) from %ir.ptr); example.c:13:12
    // $zmm0 = COPY %1:vr512; example.c:13:5
    // RET 0, $zmm0; example.c:13:5
    // clang-format on

    // this function requires AVX512F because of VMOVAPSZ
    // TODO(ts): or because of the use of zmm registers
    if (!derived()->has_cpu_feats(CompilerX64::CPU_AVX512F)) {
        return false;
    }


    // %1:vr512 = VMOVAPSZrm %0:gr64, 1, $noreg, 0, $noreg :: (load (s512) from
    // %ir.ptr); example.c:13:12
    AsmReg     op0_dst_reg;
    ScratchReg op0_dst_reg_scratch{derived()};

    // ptr has different bank -> no salvaging
    FeMem op0_mem_op;
    if (ptr.is_addr()) {
        const auto &addr = ptr.addr();
        op0_mem_op       = FE_MEM(addr.base,
                            addr.scale,
                            addr.scale ? addr.index : FE_NOREG,
                            addr.disp);
    } else {
        op0_mem_op = FE_MEM(ptr.as_reg(), 0, FE_NOREG, 0);
    }
    op0_dst_reg = op0_dst_reg_scratch.alloc_from_bank(1);
    assert(!op0_dst_reg_scratch.cur_reg.invalid());

    if (reg_needs_avx512(op0_dst_reg)) {
        assert(derived()->has_cpu_feats(CompilerX64::CPU_AVX512F));
    }
    ASMD(VMOVAPS512rm, op0_dst_reg, op0_mem_op);

    // ptr is dead after this point
    ptr.reset();

    // $zmm0 = COPY %1:vr512; example.c:13:5
    // RET 0, $zmm0; example.c:13:5
    result = std::move(op0_dst_reg_scratch);
    return true;
}


} // namespace tpde_llvm

#undef ASMD
