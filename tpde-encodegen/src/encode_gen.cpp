// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "encode_gen.hpp"


#include <format>
#include <iostream>
#include <string>
#include <unordered_set>

#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/LivePhysRegs.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Target/TargetMachine.h>

#include "encode_gen.hpp"
#include "inst_descs.hpp"

namespace {

struct EncodingTarget {
    llvm::MachineFunction *func;

    explicit EncodingTarget(llvm::MachineFunction *func) : func(func) {}

    virtual ~EncodingTarget() = default;

    virtual unsigned         reg_id_from_mc_reg(llvm::MCRegister)    = 0;
    virtual std::string_view reg_name_lower(unsigned id)             = 0;
    virtual unsigned         reg_bank(unsigned id)                   = 0;
    // some registers, e.g. flags should be ignored for generation purposes
    virtual bool             reg_should_be_ignored(llvm::MCRegister) = 0;
    virtual void             generate_copy(std::string     &buf,
                                           unsigned         indent,
                                           unsigned         bank,
                                           std::string_view dst,
                                           std::string_view src,
                                           unsigned         size)            = 0;
};

struct EncodingTargetX64 : EncodingTarget {
    explicit EncodingTargetX64(llvm::MachineFunction *func)
        : EncodingTarget(func) {}

    bool reg_is_gp(const llvm::Register reg) const {
        const auto *target_reg_info = func->getSubtarget().getRegisterInfo();

        if (!reg.isPhysical()) {
            assert(0);
            std::cerr << "ERROR: encountered non-physical register\n";
            exit(1);
        }
        return target_reg_info->isGeneralPurposeRegister(*func, reg);
    }

    unsigned reg_id_from_mc_reg(const llvm::MCRegister reg) override {
        return func->getSubtarget().getRegisterInfo()->getEncodingValue(reg)
               + (reg_is_gp(reg) ? 0 : 0x20);
    }

    std::string_view reg_name_lower(unsigned id) override {
        std::string_view gp_names[] = {
            "ax",
            "cx",
            "dx",
            "bx",
            "sp",
            "bp",
            "si",
            "di",
            "r8",
            "r9",
            "r10",
            "r11",
            "r12",
            "r13",
            "r14",
            "r15",
        };
        std::string_view xmm_names[] = {
            "xmm0",  "xmm1",  "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",
            "xmm7",  "xmm8",  "xmm9",  "xmm10", "xmm11", "xmm12", "xmm13",
            "xmm14", "xmm15", "xmm16", "xmm17", "xmm18", "xmm19", "xmm20",
            "xmm21", "xmm22", "xmm23", "xmm24", "xmm25", "xmm26", "xmm27",
            "xmm28", "xmm29", "xmm30", "xmm31",
        };

        if (id < 0x10) {
            return gp_names[id];
        } else if (id >= 0x20 && id <= 0x40) {
            return xmm_names[id - 0x20];
        } else {
            assert(0);
            std::cerr << std::format("Unknown register {}\n", id);
            exit(1);
        }
    }

    unsigned reg_bank(const unsigned id) override {
        if (id < 0x10) {
            return 0;
        } else if (id >= 0x20 && id <= 0x40) {
            return 1;
        } else {
            assert(0);
            std::cerr << std::format("Unknown register {}\n", id);
            exit(1);
        }
    }

    bool reg_should_be_ignored(const llvm::MCRegister reg) override {
        const auto name = std::string_view{
            func->getSubtarget().getRegisterInfo()->getName(reg)};
        return (name == "EFLAGS" || name == "MXCSR" || name == "FPCW"
                || name == "SSP");
    }

    void generate_copy(std::string     &buf,
                       unsigned         indent,
                       unsigned         bank,
                       std::string_view dst,
                       std::string_view src,
                       unsigned         size) override {
        if (bank == 0) {
            if (size <= 4) {
                std::format_to(std::back_inserter(buf),
                               "{:>{}}ASMD(MOV32rr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
            } else {
                assert(size <= 8);
                std::format_to(std::back_inserter(buf),
                               "{:>{}}ASMD(MOV64rr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
            }
        } else {
            assert(bank == 1);
            if (size <= 16) {
                std::format_to(std::back_inserter(buf),
                               "{:>{}}if (has_avx()) {{\n",
                               "",
                               indent);
                std::format_to(std::back_inserter(buf),
                               "{:>{}}    ASMD(VMOVUPD128rr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
                std::format_to(
                    std::back_inserter(buf), "{:>{}}else {{\n", "", indent);
                std::format_to(std::back_inserter(buf),
                               "{:>{}}    ASMD(SSE_MOVUPDrr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
                std::format_to(
                    std::back_inserter(buf), "{:>{}}}}\n", "", indent);
            } else if (size <= 32) {
                std::format_to(std::back_inserter(buf),
                               "{:>{}}ASMD(VMOVUPD256rr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
            } else {
                assert(size <= 64);
                std::format_to(std::back_inserter(buf),
                               "{:>{}}ASMD(VMOVUPD512rr, {}, {});\n",
                               "",
                               indent,
                               dst,
                               src);
            }
        }
    }
};

struct ValueInfo {
    enum TYPE {
        SCRATCHREG,
        ASM_OPERAND,
        REG_ALIAS
    };

    TYPE                         ty;
    std::string                  operand_name;
    unsigned                     alias_reg_id;
    // llvm::MachineRegisterInfo::reg_instr_nodbg_iterator cur_def_use_it;
    // registers aliased to this value
    std::unordered_set<unsigned> aliased_regs = {};

    bool is_dead = false;
    // TODO(ts): this has questionable implications when we modify them in if
    // statements as we would need to consolidate their state across the
    // different branches so for now we can simply move this to the runtime
    // scratch register state
    // bool scratch_allocated = false, scratch_destroyed = false;
};

struct GenerationState {
    llvm::MachineFunction              *func;
    EncodingTarget                     *enc_target;
    std::vector<std::string>            param_names{};
    std::unordered_set<unsigned>        used_regs{};
    llvm::DenseMap<unsigned, ValueInfo> value_map{};
    unsigned                            cur_inst_id    = 0;
    int                                 num_ret_regs   = -1;
    bool                                is_first_block = false;
    std::vector<unsigned>               return_regs    = {};

    llvm::DenseMap<unsigned, std::vector<std::string>> fixed_reg_conds{};

    template <typename... T>
    void fmt_line(std::string             &buf,
                  unsigned                 indent,
                  std::format_string<T...> fmt,
                  T &&...args) {
        std::format_to(std::back_inserter(buf), "{:>{}}", "", indent);
        std::format_to(std::back_inserter(buf), fmt, std::forward<T>(args)...);
        buf += '\n';
    }

    void remove_reg_alias(std::string &buf, unsigned reg, unsigned aliased_reg);
    unsigned resolve_reg_alias(std::string &buf, unsigned indent, unsigned);
};

bool handle_move(std::string        &buf,
                 GenerationState    &state,
                 llvm::MachineInstr *inst);
bool handle_terminator(std::string        &buf,
                       GenerationState    &state,
                       llvm::MachineInstr *inst);

bool generate_inst_inner(std::string           &buf,
                         GenerationState       &state,
                         llvm::MachineInstr    *inst,
                         tpde_encgen::InstDesc &desc,
                         std::string_view       if_cond,
                         unsigned               indent);

bool generate_inst(std::string        &buf,
                   GenerationState    &state,
                   llvm::MachineInstr *inst) {
    (void)buf;
    (void)state;

    // check if this is a move
    if (inst->isMoveReg()) {
        if (!inst->hasImplicitDef()) {
            // move without side-effects, e.g. zero-extension
            return handle_move(buf, state, inst);
        }
    }

    if (inst->isTerminator()) {
        return handle_terminator(buf, state, inst);
    }

    if (inst->isPseudo()) {
        if (inst->isKill()) {
            assert(inst->getOperand(0).isReg() && inst->getOperand(1).isReg());
            if (state.enc_target->reg_id_from_mc_reg(
                    inst->getOperand(0).getReg())
                != state.enc_target->reg_id_from_mc_reg(
                    inst->getOperand(1).getReg())) {
                std::cerr << "ERROR: Found KILL instruction with different "
                             "source and destination register\n";
                assert(0);
                return false;
            }
            state.fmt_line(buf, 4, "// KILL is a no-op");
            return true;
        } else {
            std::string              buf{};
            llvm::raw_string_ostream os(buf);
            inst->print(os);
            std::cerr << std::format(
                "ERROR: Encountered unknown instruction '{}'\n", buf);
            assert(0);
            return false;
        }
    }

#if 0
    auto       &tm      = state.func->getTarget();
    const auto &target  = tm.getTarget();
    auto        context = llvm::MCContext{tm.getTargetTriple(),
                                   tm.getMCAsmInfo(),
                                   tm.getMCRegisterInfo(),
                                   tm.getMCSubtargetInfo(),
                                   nullptr,
                                   &tm.Options.MCOptions};
    tm.getObjFileLowering()->Initialize(context, tm);
    auto emitter = state.func->getTarget().getTarget().createMCCodeEmitter(
        *tm.getMCInstrInfo(), context);

    llvm::SmallVector<char, 32> inst_buf = {};

    struct EmitterWrapper : llvm::MCCodeEmitter {
        virtual ~EmitterWrapper() = default;

        llvm::MCCodeEmitter         *emitter;
        llvm::SmallVector<char, 32> *inst_buf;

        EmitterWrapper(llvm::MCCodeEmitter         *emitter,
                       llvm::SmallVector<char, 32> *buf)
            : emitter(emitter), inst_buf(buf) {}

        void reset() override { emitter->reset(); }

        void
            encodeInstruction(const llvm::MCInst                   &Inst,
                              llvm::SmallVectorImpl<char>          &CB,
                              llvm::SmallVectorImpl<llvm::MCFixup> &Fixups,
                              const llvm::MCSubtargetInfo &STI) const override {
            const auto size = CB.size();
            emitter->encodeInstruction(Inst, CB, Fixups, STI);
            const auto enc_len = CB.size() - size;
            std::cerr << std::format("Encoded inst with len {}: ", enc_len);
            for (unsigned i = 0; i < enc_len; ++i) {
                std::cerr << std::format("{:X} ", CB[size + i]);
            }
            std::cerr << "\n";

            inst_buf->append(CB.begin() + size, CB.begin() + CB.size());
        }
    };

    auto obj_streamer =
        std::unique_ptr<llvm::MCStreamer>(target.createMCObjectStreamer(
            tm.getTargetTriple(),
            context,
            nullptr,
            nullptr,
            std::make_unique<EmitterWrapper>(emitter, &inst_buf),
            state.func->getSubtarget(),
            false,
            false,
            true));
    // TOOD(ts): get tm from main? why is is not const?
    auto *asm_printer = target.createAsmPrinter(
        *const_cast<llvm::LLVMTargetMachine *>(&tm), std::move(obj_streamer));
    asm_printer->SetupMachineFunction(*state.func);

    asm_printer->emitInstruction(inst);
    delete asm_printer;
    delete emitter;

    std::format_to(std::back_inserter(buf), "    // encoded as ");
    for (char c : inst_buf) {
        std::format_to(std::back_inserter(buf), "{:X}", c);
    }
    std::format_to(std::back_inserter(buf), "\n");
#endif

    using namespace tpde_encgen;
    // TODO(ts): lookup instruction desc
    tpde_encgen::InstDesc desc;
    if (!tpde_encgen::get_inst_def(*inst, desc)) {
        std::string              buf{};
        llvm::raw_string_ostream os(buf);
        inst->print(os);
        std::cerr << std::format(
            "ERROR: Failed to get instruction desc for {}\n", buf);
        assert(0);
        return false;
    }

// TODO(ts): check preferred encodings
#if 0
    const auto get_use = [&](unsigned idx) {
        for (auto &use : inst->uses()) {
            if (idx != 0) {
                --idx;
                continue;
            }
            return use;
        }
        // should never happen
        assert(0);
        exit(1);
    };
#endif
#if 0
    const auto get_def = [&](unsigned idx) {
        for (auto &def : inst->all_defs()) {
            if (idx != 0) {
                --idx;
                continue;
            }
            return def;
        }
        // should never happen
        assert(0);
        exit(1);
    };
#endif

    bool        ifs_written = false;
    std::string if_conds_inverted{};
    for (auto &pref_enc : desc.preferred_encodings) {
        if (pref_enc.target == InstDesc::PreferredEncoding::TARGET_NONE) {
            // unsupported for now, later used for CPU target checks maybe
            continue;
        }

        assert(pref_enc.target == InstDesc::PreferredEncoding::TARGET_USE);
        const auto &use = inst->getOperand(pref_enc.target_def_use_idx);

        bool break_loop = false;
        switch (pref_enc.cond) {
            using enum InstDesc::PreferredEncoding::COND;
        case COND_IMM64:
        case COND_IMM32:
        case COND_IMM16:
        case COND_IMM8: {
            if (use.isImm()) {
                // we can always encode this
                desc       = *pref_enc.replacement;
                break_loop = true;
                break;
            }
            assert(use.isReg() && use.getReg().isPhysical());
            auto reg_id = state.enc_target->reg_id_from_mc_reg(use.getReg());
            assert(state.value_map.contains(reg_id));
            while (state.value_map[reg_id].ty == ValueInfo::REG_ALIAS) {
                reg_id = state.value_map[reg_id].alias_reg_id;
                assert(state.value_map.contains(reg_id));
            }
            if (state.value_map[reg_id].ty != ValueInfo::ASM_OPERAND) {
                // cannot be an immediate
                continue;
            }

            std::format_to(
                std::back_inserter(buf),
                "    // {} has a preferred encoding as {} if possible\n",
                desc.name_fadec,
                pref_enc.replacement->name_fadec);

            // TODO(ts): this is arch-dependent
            const char *cond_str = nullptr;
            switch (pref_enc.cond) {
            case COND_IMM64: cond_str = "encodeable_as_imm64"; break;
            case COND_IMM32: cond_str = "encodeable_as_imm32_sext"; break;
            case COND_IMM16: cond_str = "encodeable_as_imm16_sext"; break;
            case COND_IMM8: cond_str = "encodeable_as_imm8_sext"; break;
            default: __builtin_unreachable();
            }

            const auto if_cond = std::format(
                "{}.{}()", state.value_map[reg_id].operand_name, cond_str);
            if (ifs_written) {
                buf += " else";
            }
            std::format_to(std::back_inserter(buf),
                           "{:>{}}if ({}) {{\n",
                           "",
                           ifs_written ? 1 : 4,
                           if_cond);
            ifs_written = true;
            generate_inst_inner(
                buf, state, inst, *pref_enc.replacement, if_cond, 8);
            std::format_to(std::back_inserter(buf), "    }}");

            if (!if_conds_inverted.empty()) {
                if_conds_inverted += " && ";
            }
            if_conds_inverted += std::format("(!{})", if_cond);

            // TODO(ts): if the instruction is commutable, we should also check
            // other uses

            continue;
        }
        case COND_MEM: {
            // we can only do this replacement if the asmoperand is a valueref
            // *and* not in a register
            // otherwise we get different semantics
            assert(use.isReg() && use.getReg().isPhysical());
            auto reg_id = state.enc_target->reg_id_from_mc_reg(use.getReg());
            assert(state.value_map.contains(reg_id));
            while (state.value_map[reg_id].ty == ValueInfo::REG_ALIAS) {
                reg_id = state.value_map[reg_id].alias_reg_id;
                assert(state.value_map.contains(reg_id));
            }
            if (state.value_map[reg_id].ty != ValueInfo::ASM_OPERAND) {
                // cannot be a memory operand
                continue;
            }

            std::format_to(
                std::back_inserter(buf),
                "    // {} has a preferred encoding as {} if possible\n",
                desc.name_fadec,
                pref_enc.replacement->name_fadec);

            const auto if_cond =
                std::format("{}.val_ref_prefers_mem_enc()",
                            state.value_map[reg_id].operand_name);
            if (ifs_written) {
                buf += "    else";
            }
            std::format_to(std::back_inserter(buf),
                           "{:>{}}if ({}) {{\n",
                           "",
                           ifs_written ? 1 : 4,
                           if_cond);
            ifs_written = true;
            generate_inst_inner(
                buf, state, inst, *pref_enc.replacement, if_cond, 8);
            std::format_to(std::back_inserter(buf), "    }}");
            continue;
        }
        }

        if (break_loop) {
            break;
        }
    }

    if (ifs_written) {
        buf += " else {\n";
    }

    // TODO(ts): generate operation
    generate_inst_inner(
        buf, state, inst, desc, if_conds_inverted, ifs_written ? 8 : 4);

    if (ifs_written) {
        buf += "    }\n";
    }

    // TODO(ts): check killed operands
    for (auto &use : inst->all_uses()) {
        if (use.isImm() || (use.isReg() && !use.getReg().isValid())) {
            continue;
        }

        assert(use.isReg() && use.getReg().isPhysical());
        const auto reg = use.getReg();

        if (state.enc_target->reg_should_be_ignored(reg)) {
            continue;
        }

        if (!use.isKill()) {
            continue;
        }

        const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
        std::format_to(std::back_inserter(buf),
                       "    // argument {} is killed and marked as dead\n",
                       state.enc_target->reg_name_lower(reg_id));

        assert(state.value_map.contains(reg_id));
        auto &reg_info = state.value_map[reg_id];
        if (reg_info.ty == ValueInfo::REG_ALIAS) {
            state.remove_reg_alias(buf, reg_id, reg_info.alias_reg_id);
        }

        reg_info.ty      = ValueInfo::SCRATCHREG;
        reg_info.is_dead = true;
    }

    for (auto &def : inst->all_defs()) {
        assert(def.isReg() && def.getReg().isPhysical());
        const auto reg = def.getReg();
        if (state.enc_target->reg_should_be_ignored(reg)) {
            continue;
        }

        // after an instruction all registers are in ScratchRegs
        const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
        assert(state.value_map.contains(reg_id));
        auto &reg_info = state.value_map[reg_id];
        if (reg_info.ty == ValueInfo::REG_ALIAS) {
            state.remove_reg_alias(buf, reg_id, reg_info.alias_reg_id);
        }

        reg_info.ty      = ValueInfo::SCRATCHREG;
        reg_info.is_dead = def.isDead();

        state.fmt_line(buf,
                       4,
                       "// result {} is marked as {}",
                       state.enc_target->reg_name_lower(reg_id),
                       reg_info.is_dead ? "dead" : "alive");
    }

    return true;
}

void GenerationState::remove_reg_alias(std::string &buf,
                                       unsigned     reg,
                                       unsigned     aliased_reg) {
    assert(value_map.contains(reg));
    assert(value_map.contains(aliased_reg));
    std::format_to(std::back_inserter(buf),
                   "    // removing alias from {} to {}\n",
                   enc_target->reg_name_lower(reg),
                   enc_target->reg_name_lower(aliased_reg));

    value_map[reg].ty = ValueInfo::SCRATCHREG;
    auto &info        = value_map[aliased_reg];
    info.aliased_regs.erase(reg);

    if (info.ty == ValueInfo::REG_ALIAS && info.aliased_regs.empty()
        && info.is_dead) {
        remove_reg_alias(buf, aliased_reg, info.alias_reg_id);
    }
}

unsigned GenerationState::resolve_reg_alias(std::string &buf,
                                            unsigned     indent,
                                            unsigned     reg) {
    assert(value_map.contains(reg));
    const auto &info = value_map[reg];
    if (info.ty == ValueInfo::REG_ALIAS) {
        fmt_line(buf,
                 indent,
                 "// {} is an alias for {}",
                 enc_target->reg_name_lower(reg),
                 enc_target->reg_name_lower(info.alias_reg_id));
        return resolve_reg_alias(buf, indent, info.alias_reg_id);
    }

    return reg;
}

bool handle_move(std::string        &buf,
                 GenerationState    &state,
                 llvm::MachineInstr *inst) {
    assert(inst->getNumExplicitOperands() == 2);
    const auto *src_op = &*inst->all_uses().begin();
    assert(src_op->getType() == llvm::MachineOperand::MO_Register);
    const auto src_reg = src_op->getReg();
    assert(src_reg.isPhysical());

    assert(inst->getNumExplicitDefs() == 1);
    const auto dst_reg = inst->defs().begin()->getReg();
    assert(dst_reg.isPhysical());

    const auto src_id   = state.enc_target->reg_id_from_mc_reg(src_reg);
    const auto dst_id   = state.enc_target->reg_id_from_mc_reg(dst_reg);
    const auto src_name = state.enc_target->reg_name_lower(src_id);
    const auto dst_name = state.enc_target->reg_name_lower(dst_id);

    assert(state.value_map.contains(src_id));
    if (state.value_map[src_id].is_dead) {
        std::cerr << std::format("ERROR: dead register {} used in move\n",
                                 src_name);
        return false;
    }

    // TODO(ts): src_id == dst_id?
    if (src_reg == dst_reg) {
        // this is a no-op
        buf += std::format("    // no-op\n");
        return true;
    }

    // This is a register move without any intended side-effects,
    // e.g. zero-extension so we can alias it for now
    buf += std::format("    // aliasing {} to {}\n", dst_name, src_name);
    state.value_map[src_id].aliased_regs.insert(dst_id);

    // TODO(ts): cannot do this when we are outside of the entry-block
    if (src_op->isKill()) {
#if 0
                // no need to alias, we can simply move the register
                // TODO(ts): we cannot do this if dst_id is used as a fixed register somewhere
                // as we free up the allocation then

                // TODO(ts): do we want this as the optimizer might not optimize out the move
                buf += std::format("    // source {} is killed so move to {}\n", src_name, dst_name);
                buf += std::format("    scratch_{} = std::move(scratch_{})\n", dst_name, src_name);

                state.value_map[src_id].was_destroyed = true;
                state.value_map[src_id].is_dead = true;
#else
        buf += std::format("    // source {} is killed and marked as "
                           "dead but not yet destroyed\n",
                           src_name);
        state.value_map[src_id].is_dead = true;
#endif
    }

    auto &dst_info        = state.value_map[dst_id];
    dst_info.ty           = ValueInfo::REG_ALIAS;
    dst_info.alias_reg_id = src_id;
    dst_info.is_dead      = false;
    return true;
}

static unsigned reg_size_bytes(llvm::MachineFunction *func,
                               llvm::Register         reg) {
    const auto &target_reg_info = func->getSubtarget().getRegisterInfo();
    const auto &mach_reg_info   = func->getRegInfo();
    return target_reg_info->getRegSizeInBits(reg, mach_reg_info) / 8;
}

bool handle_terminator(std::string        &buf,
                       GenerationState    &state,
                       llvm::MachineInstr *inst) {
    if (inst->isReturn()) {
        // record the number of return regs
        {
            unsigned              num_ret_regs = 0;
            std::vector<unsigned> ret_regs     = {};
            for (const auto &mach_op : inst->explicit_uses()) {
                assert(mach_op.getType() == llvm::MachineOperand::MO_Register);
                const auto reg = mach_op.getReg();
                assert(reg.isPhysical());
                const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
                if (state.num_ret_regs == -1) {
                    ret_regs.push_back(reg_id);
                } else {
                    if (state.return_regs.size() <= num_ret_regs
                        || state.return_regs[num_ret_regs] != reg_id) {
                        std::cerr << std::format(
                            "ERROR: Found mismatching return with register "
                            "{}({})",
                            reg_id,
                            state.enc_target->reg_name_lower(reg_id));
                        return false;
                    }
                }
                ++num_ret_regs;
            }
            if (state.num_ret_regs == -1) {
                state.num_ret_regs = static_cast<int>(num_ret_regs);
                state.return_regs  = std::move(ret_regs);
            }
        }

        // if there is only one block, we can return from the encoding function
        // otherwise we need to buffer the return until the end
        if (state.func->size() == 1) {
            for (int idx = 0; idx < state.num_ret_regs; ++idx) {
                const auto reg = state.return_regs[idx];
                buf += std::format("    // returning reg {} as result_{}\n",
                                   state.enc_target->reg_name_lower(reg),
                                   idx);
                assert(state.value_map.contains(reg));
                auto  cur_reg = reg;
                auto *info    = &state.value_map[reg];
                while (info->ty == ValueInfo::REG_ALIAS) {
                    buf += std::format(
                        "    // {} is an alias for {}\n",
                        state.enc_target->reg_name_lower(cur_reg),
                        state.enc_target->reg_name_lower(info->alias_reg_id));
                    cur_reg = info->alias_reg_id;
                    info    = &state.value_map[cur_reg];
                }

                if (info->ty == ValueInfo::ASM_OPERAND) {
                    buf +=
                        std::format("    // {} is an alias for {}\n",
                                    state.enc_target->reg_name_lower(cur_reg),
                                    info->operand_name);

                    unsigned tmp_idx = idx;
                    for (auto &op : inst->explicit_uses()) {
                        if (tmp_idx > 0) {
                            --tmp_idx;
                            continue;
                        }

                        buf += std::format(
                            "    {}.try_salvage_or_materialize(this, "
                            "result_{}, {}, {});\n",
                            info->operand_name,
                            idx,
                            state.enc_target->reg_bank(cur_reg),
                            reg_size_bytes(state.func, op.getReg()));
                    }
                } else {
                    assert(info->ty == ValueInfo::SCRATCHREG);
                    buf +=
                        std::format("    result_{} = std::move(scratch_{});\n",
                                    idx,
                                    state.enc_target->reg_name_lower(cur_reg));
                    // no more housekeeping needed
                }
            }
            return true;
        } else {
            // we need to move into the result scratch regs (they should be
            // allocated by now)
            return false;
        }
    }

    std::string              tmp;
    llvm::raw_string_ostream os(tmp);
    inst->print(os);

    std::cerr << std::format("ERROR: Encountered unknown terminator {}\n", tmp);
    return false;
}

bool generate_inst_inner(std::string           &buf,
                         GenerationState       &state,
                         llvm::MachineInstr    *inst,
                         tpde_encgen::InstDesc &desc,
                         std::string_view       if_cond,
                         unsigned               indent) {
    // ok so we should just need to walk the operands in the desc, set them up
    // depending on what we get from the MachineInstr and then emit the encoding
    //
    // sounds good...

    (void)if_cond;
    // we need the llvm desc to check for memory operand mismatches
    // as we need different code generated if we deal with a replacement
    // from INSTrr -> INSTrm
    const llvm::MCInstrDesc &llvm_inst_desc =
        state.func->getTarget().getMCInstrInfo()->get(inst->getOpcode());
    (void)llvm_inst_desc;

    // allocate mapping for destinations
    std::vector<bool> defs_allocated{};
    for (const auto &def : inst->all_defs()) {
        assert(def.isReg() && def.getReg().isPhysical());
        const auto reg = def.getReg();
        if (state.enc_target->reg_should_be_ignored(reg)) {
            continue;
        }

        const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
        if (!state.value_map.contains(reg_id)) {
            state.value_map[reg_id] =
                ValueInfo{.ty = ValueInfo::SCRATCHREG, .is_dead = false};
        }
        defs_allocated.push_back(def.isImplicit());
    }

    const auto def_idx = [inst](const llvm::MachineOperand *op) {
        unsigned idx = 0;
        for (const auto &def : inst->all_defs()) {
            if (&def == op) {
                return idx;
            }
            ++idx;
        }
        assert(0);
        exit(1);
    };

    // auto       llvm_uses     = inst->uses().begin();
    // auto       llvm_uses_end = inst->uses().end();
    const auto inst_id = state.cur_inst_id;

    std::vector<std::string> op_names{};

    // sometimes implicit operands will show up in the argument list, e.g.
    // `SHL32rCL` but not always, e.g. `CDQ`
    std::vector<unsigned> implicit_ops_handled{};
    unsigned              implicit_use_count = 0, explicit_use_count = 0;

    for (unsigned op_idx = 0; op_idx < desc.operands.size(); ++op_idx) {
        switch (desc.operands[op_idx].type) {
            using enum tpde_encgen::InstDesc::OP_TYPE;
        case OP_REG: {
            auto &llvm_op = inst->getOperand(desc.operands[op_idx].llvm_idx);
            if (llvm_op.isDef()) {
                continue;
            }

            if (llvm_op.isImplicit()) {
                implicit_ops_handled.push_back(desc.operands[op_idx].llvm_idx);

                if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
                    // should not happen
                    assert(0);
                    exit(1);
                }
                ++implicit_use_count;
            } else {
                if (explicit_use_count >= llvm_inst_desc.NumOperands) {
                    // should not happen
                    assert(0);
                    exit(1);
                }
                ++explicit_use_count;
            }

            assert(llvm_op.isReg() && llvm_op.getReg().isPhysical());
            auto orig_reg_id =
                state.enc_target->reg_id_from_mc_reg(llvm_op.getReg());
            assert(state.value_map.contains(orig_reg_id));

            std::format_to(std::back_inserter(buf),
                           "{:>{}}// operand {} is {}\n",
                           "",
                           indent,
                           op_idx,
                           state.enc_target->reg_name_lower(orig_reg_id));

            auto resolved_reg_id = orig_reg_id;
            while (state.value_map[resolved_reg_id].ty
                   == ValueInfo::REG_ALIAS) {
                std::format_to(
                    std::back_inserter(buf),
                    "{:>{}}// {} is an alias for {}\n",
                    "",
                    indent,
                    state.enc_target->reg_name_lower(resolved_reg_id),
                    state.enc_target->reg_name_lower(
                        state.value_map[resolved_reg_id].alias_reg_id));
                resolved_reg_id = state.value_map[resolved_reg_id].alias_reg_id;
                assert(state.value_map.contains(resolved_reg_id));
            }
            // TODO(ts): need to think about this
            assert(!llvm_op.isEarlyClobber());

            if (llvm_op.isImplicit()) {
                // notify the outer code that we need the register argument as a
                // fixed register state.fmt_line(buf, indent, "// {} is an
                // implicit operand, need to fix it");
                state.fixed_reg_conds[orig_reg_id].emplace_back(if_cond);
            }

            const auto &reg_info = state.value_map[resolved_reg_id];
            if (reg_info.ty == ValueInfo::SCRATCHREG) {
                // if the destination is tied, we want to salvage if possible
                // or allocate the destination if required and do a copy
                // if the destination and the source are the same then we only
                // need to allocate
                //
                // if it is not tied we can simply use the source reg

                if (llvm_op.isTied()) {
                    if (llvm_op.isImplicit()) {
                        std::cerr << "ERROR: Found instruction with tied "
                                     "implicit operand which is unsupported\n";
                        assert(0);
                        return false;
                    }
                    const auto &def_reg = inst->getOperand(
                        inst->findTiedOperandIdx(llvm_op.getOperandNo()));
                    assert(def_reg.isReg() && def_reg.getReg().isPhysical());
                    const auto def_resolved_reg_id =
                        state.enc_target->reg_id_from_mc_reg(def_reg.getReg());
                    assert(state.enc_target->reg_bank(resolved_reg_id)
                           == state.enc_target->reg_bank(def_resolved_reg_id));

                    if (def_reg.isImplicit()) {
                        // TODO(ts): does this make sense?
                        std::cerr << "ERROR: Found instruction which has an "
                                     "operand tied to an implicit definition "
                                     "which is unsupported\n";
                        assert(0);
                        return false;
                    }

                    if (resolved_reg_id == def_resolved_reg_id) {
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}// operand {}({}) is the same as its tied "
                            "destination\n",
                            "",
                            indent,
                            op_idx,
                            state.enc_target->reg_name_lower(resolved_reg_id));
                        // TODO(ts): in this case, the scratch should already be
                        // allocated, no?
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}"
                            "scratch_{}.alloc_from_bank({});\n",
                            "",
                            indent,
                            state.enc_target->reg_name_lower(resolved_reg_id),
                            state.enc_target->reg_bank(resolved_reg_id));
                        defs_allocated[def_idx(&def_reg)] = true;
                    } else if (state.is_first_block && llvm_op.isKill()
                               && (reg_info.aliased_regs.empty()
                                   || (reg_info.is_dead
                                       && reg_info.aliased_regs.size() == 1))) {
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}// operand {}({}) can be salvaged\n",
                            "",
                            indent,
                            op_idx,
                            state.enc_target->reg_name_lower(resolved_reg_id));
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}scratch_{} = std::move(scratch_{});\n",
                            "",
                            indent,
                            state.enc_target->reg_name_lower(
                                def_resolved_reg_id),
                            state.enc_target->reg_name_lower(resolved_reg_id));
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}"
                            "scratch_{}.alloc_from_bank({});\n",
                            "",
                            indent,
                            state.enc_target->reg_name_lower(
                                def_resolved_reg_id),
                            state.enc_target->reg_bank(def_resolved_reg_id));
                        defs_allocated[def_idx(&def_reg)] = true;
                    } else {
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}// operand {}({}) has some references so "
                            "copy it\n",
                            "",
                            indent,
                            op_idx,
                            state.enc_target->reg_name_lower(resolved_reg_id));
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}AsmReg inst_{}_op{} = "
                            "scratch_{}.alloc_from_bank({});\n",
                            "",
                            indent,
                            inst_id,
                            op_idx,
                            state.enc_target->reg_name_lower(
                                def_resolved_reg_id),
                            state.enc_target->reg_bank(def_resolved_reg_id));
                        state.enc_target->generate_copy(
                            buf,
                            indent,
                            state.enc_target->reg_bank(def_resolved_reg_id),
                            std::format("inst_{}_op{}", inst_id, op_idx),
                            std::format("scratch_{}.cur_reg",
                                        state.enc_target->reg_name_lower(
                                            resolved_reg_id)),
                            reg_size_bytes(state.func, llvm_op.getReg()));
                    }
                } else {
                    std::format_to(
                        std::back_inserter(buf),
                        "{:>{}}// operand {}({}) is a simple register\n",
                        "",
                        indent,
                        op_idx,
                        state.enc_target->reg_name_lower(resolved_reg_id));

                    auto use_reg_id = resolved_reg_id;
                    if (llvm_op.isImplicit()
                        && orig_reg_id != resolved_reg_id) {
                        state.fmt_line(
                            buf,
                            indent,
                            "// {} is an implicit operand, need to move "
                            "aliased {} into it",
                            state.enc_target->reg_name_lower(orig_reg_id),
                            state.enc_target->reg_name_lower(resolved_reg_id));
                        use_reg_id = orig_reg_id;
                        state.enc_target->generate_copy(
                            buf,
                            indent,
                            state.enc_target->reg_bank(resolved_reg_id),
                            std::format(
                                "scratch_{}.cur_reg",
                                state.enc_target->reg_name_lower(orig_reg_id)),
                            std::format("scratch_{}.cur_reg",
                                        state.enc_target->reg_name_lower(
                                            resolved_reg_id)),
                            reg_size_bytes(state.func, llvm_op.getReg()));
                    }

                    // TODO(ts): try to salvage the register if possible
                    // register should be allocated if we come into here
                    std::format_to(
                        std::back_inserter(buf),
                        "{:>{}}AsmReg inst_{}_op{} = "
                        "scratch_{}.cur_reg;\n",
                        "",
                        indent,
                        inst_id,
                        op_idx,
                        state.enc_target->reg_name_lower(use_reg_id));
                    op_names.push_back(
                        std::format("inst_{}_op{}", inst_id, op_idx));
                }
            } else {
                assert(reg_info.ty == ValueInfo::ASM_OPERAND);
                std::format_to(
                    std::back_inserter(buf),
                    "{:>{}}// {} is mapped to {}\n",
                    "",
                    indent,
                    state.enc_target->reg_name_lower(resolved_reg_id),
                    reg_info.operand_name);
                if (llvm_op.isImplicit()) {
                    if (llvm_op.isTied()) {
                        std::cerr << "ERROR: Found instruction with tied "
                                     "implicit operand which is unsupported\n";
                        assert(0);
                        return false;
                    }

                    state.fmt_line(
                        buf,
                        indent,
                        "// {} is an implicit operand, cannot salvage",
                        state.enc_target->reg_name_lower(orig_reg_id));
                    state.fmt_line(buf,
                                   indent,
                                   "AsmReg inst{}_op{}_tmp = {}.as_reg(this);",
                                   inst_id,
                                   op_idx,
                                   reg_info.operand_name);
                    state.enc_target->generate_copy(
                        buf,
                        indent,
                        state.enc_target->reg_bank(orig_reg_id),
                        std::format(
                            "scratch_{}.cur_reg",
                            state.enc_target->reg_name_lower(orig_reg_id)),
                        std::format("inst{}_op{}_tmp", inst_id, op_idx),
                        reg_size_bytes(state.func, llvm_op.getReg()));
                    op_names.push_back(std::format(
                        "scratch_{}.cur_reg",
                        state.enc_target->reg_name_lower(orig_reg_id)));
                    break;
                }

                // try to salvage
                if (llvm_op.isTied()) {
                    // TODO(ts): if the destination is used as a fixed reg at
                    // some point we cannot simply salvage into it i think so we
                    // need some extra helpers for that
                    const auto &def_reg = inst->getOperand(
                        inst->findTiedOperandIdx(llvm_op.getOperandNo()));
                    assert(def_reg.isReg() && def_reg.getReg().isPhysical());
                    const auto def_resolved_reg_id =
                        state.enc_target->reg_id_from_mc_reg(def_reg.getReg());
                    assert(state.enc_target->reg_bank(resolved_reg_id)
                           == state.enc_target->reg_bank(def_resolved_reg_id));

                    if (def_reg.isImplicit()) {
                        // TODO(ts): does this make sense?
                        std::cerr << "ERROR: Found instruction which has an "
                                     "operand tied to an implicit definition "
                                     "which is unsupported\n";
                        assert(0);
                        return false;
                    }

                    if (state.is_first_block && llvm_op.isKill()
                        && (reg_info.aliased_regs.empty()
                            || (reg_info.is_dead
                                && reg_info.aliased_regs.size() == 1))) {
                        std::format_to(std::back_inserter(buf),
                                       "{:>{}}// operand {}({}) is tied so try "
                                       "to salvage or materialize\n",
                                       "",
                                       indent,
                                       op_idx,
                                       reg_info.operand_name);
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}{}.try_salvage_or_materialize(this, "
                            "scratch_{}, {}, {});\n",
                            "",
                            indent,
                            reg_info.operand_name,
                            state.enc_target->reg_name_lower(
                                def_resolved_reg_id),
                            state.enc_target->reg_bank(resolved_reg_id),
                            reg_size_bytes(state.func, llvm_op.getReg()));
                        defs_allocated[def_idx(&def_reg)] = true;
                    } else {
                        std::format_to(
                            std::back_inserter(buf),
                            "{:>{}}AsmReg inst_{}_op{} = "
                            "scratch_{}.alloc_from_bank({});\n",
                            "",
                            indent,
                            inst_id,
                            op_idx,
                            state.enc_target->reg_name_lower(
                                def_resolved_reg_id),
                            state.enc_target->reg_bank(def_resolved_reg_id));
                        std::format_to(std::back_inserter(buf),
                                       "{:>{}}AsmReg inst_{}_op{}_tmp = "
                                       "{}.as_reg(this);\n",
                                       "",
                                       indent,
                                       inst_id,
                                       op_idx,
                                       reg_info.operand_name);
                        state.enc_target->generate_copy(
                            buf,
                            indent,
                            state.enc_target->reg_bank(def_resolved_reg_id),
                            std::format("inst_{}_op{}", inst_id, op_idx),
                            std::format("inst_{}_op{}_tmp", inst_id, op_idx),
                            reg_size_bytes(state.func, llvm_op.getReg()));
                        defs_allocated[def_idx(&def_reg)] = true;
                    }
                } else {
                    // TODO(ts): search for dests to salvage to
                    std::format_to(
                        std::back_inserter(buf),
                        "{:>{}}AsmReg inst_{}_op{} = {}.as_reg(this);\n",
                        "",
                        indent,
                        inst_id,
                        op_idx,
                        reg_info.operand_name);
                    op_names.push_back(
                        std::format("inst_{}_op{}", inst_id, op_idx));
                }
            }
            break;
        }
        case OP_MEM: {
            // TODO(ts): factor out for arch-specifica
            state.fmt_line(
                buf, indent, "// operand {} is a memory operand", op_idx);

            const auto llvm_op_idx    = desc.operands[op_idx].llvm_idx;
            auto       is_replacement = false;
            if (llvm_op_idx >= llvm_inst_desc.operands().size()) {
                // this was an implicit (physical) register operand
                is_replacement = true;
                implicit_ops_handled.push_back(llvm_op_idx);

                if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
                    // should not happen
                    assert(0);
                    exit(1);
                }
                ++implicit_use_count;
            } else {
                const auto &op_info = llvm_inst_desc.operands()[llvm_op_idx];
                is_replacement =
                    op_info.OperandType != llvm::MCOI::OPERAND_MEMORY;

                if (inst->getOperand(llvm_op_idx).isImplicit()) {
                    implicit_ops_handled.push_back(llvm_op_idx);
                    assert(is_replacement);
                    if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
                        // should not happen
                        assert(0);
                        exit(1);
                    }
                    ++implicit_use_count;
                } else if (is_replacement) {
                    if (explicit_use_count >= llvm_inst_desc.NumOperands) {
                        // should not happen
                        assert(0);
                        exit(1);
                    }
                    ++explicit_use_count;
                }
            }

            if (is_replacement) {
                // dealing with a replacement so we already know that the
                // operand is an AsmOperand::ValueRef
                // that is on the stack
                assert(inst->getOperand(llvm_op_idx).isReg());
                const auto base_reg_id = state.enc_target->reg_id_from_mc_reg(
                    inst->getOperand(llvm_op_idx).getReg());
                assert(state.value_map.contains(base_reg_id));
                state.fmt_line(buf,
                               indent,
                               "// {} is base for memory operand to use",
                               state.enc_target->reg_name_lower(base_reg_id));

                const auto base_reg_aliased_id =
                    state.resolve_reg_alias(buf, indent, base_reg_id);
                assert(state.value_map.contains(base_reg_aliased_id));
                const auto &base_info = state.value_map[base_reg_aliased_id];
                assert(base_info.ty == ValueInfo::ASM_OPERAND);

                state.fmt_line(
                    buf,
                    indent,
                    "// {} maps to operand {} which is known to be a "
                    "ValuePartRef",
                    state.enc_target->reg_name_lower(base_reg_aliased_id),
                    base_info.operand_name);

                // in x64's case, we know that the stack address is simply
                // rbp-disp
                state.fmt_line(buf,
                               indent,
                               "FeMem inst{}_op{} = FE_MEM(AsmReg::BP, 0, "
                               "FE_NOREG, -{}.val_ref_frame_off());",
                               inst_id,
                               op_idx,
                               base_info.operand_name);

                op_names.push_back(
                    std::format("inst_{}_op{}", inst_id, op_idx));
                break;
            }

            /*
            ** A Memory Operand in MIR consists of five operands:
            ** 1. base register
            ** 2. scale factor
            ** 3. index register
            ** 4. displacement
            ** 5. segment register (uninteresting for us)
            **
            ** We just encountered the first operand, start by getting the rest
            */
            const llvm::MachineOperand &base_reg =
                inst->getOperand(llvm_op_idx);
            const llvm::MachineOperand &scale_factor =
                inst->getOperand(llvm_op_idx + 1);
            const llvm::MachineOperand &index_reg =
                inst->getOperand(llvm_op_idx + 2);
            const llvm::MachineOperand &displacement =
                inst->getOperand(llvm_op_idx + 3);
            assert(scale_factor.isImm() && displacement.isImm());
            assert(base_reg.isReg() && index_reg.isReg());
            assert(base_reg.getReg().isValid()
                   && base_reg.getReg().isPhysical());

            if (explicit_use_count + 4 > llvm_inst_desc.NumOperands) {
                // should not happen
                assert(0);
                exit(1);
            }
            explicit_use_count += 4;

            std::format_to(std::back_inserter(buf),
                           "{:>{}}FeMem inst{}_op{};\n",
                           "",
                           indent,
                           inst_id,
                           op_idx);

            const auto mem_insert_idx = buf.size();

            const auto base_reg_id =
                state.enc_target->reg_id_from_mc_reg(base_reg.getReg());
            assert(state.value_map.contains(base_reg_id));

            std::format_to(std::back_inserter(buf),
                           "{:>{}}// looking at base {}\n",
                           "",
                           indent,
                           state.enc_target->reg_name_lower(base_reg_id));
            const auto base_reg_aliased_id =
                state.resolve_reg_alias(buf, indent, base_reg_id);

            const auto base_is_asm_op = state.value_map[base_reg_aliased_id].ty
                                        == ValueInfo::ASM_OPERAND;
            if (base_is_asm_op) {
                // TODO(ts): add helper to check if this operand is an address
                // *or* a stack reference so we can fold the stack addr into the
                // instruction
                std::format_to(
                    std::back_inserter(buf),
                    "{:>{}}// {} maps to {}, so could be an address\n",
                    "",
                    indent,
                    state.enc_target->reg_name_lower(base_reg_aliased_id),
                    state.value_map[base_reg_aliased_id].operand_name);
                std::format_to(
                    std::back_inserter(buf),
                    "{:>{}}if ({}.is_addr()) {{\n",
                    "",
                    indent,
                    state.value_map[base_reg_aliased_id].operand_name);
                std::format_to(
                    std::back_inserter(buf),
                    "{:>{}}const auto& addr = {}.addr();\n",
                    "",
                    indent + 4,
                    state.value_map[base_reg_aliased_id].operand_name);
                // if there is no displacement and index we can simply take over
                // the address
                if (!index_reg.getReg().isValid()
                    && displacement.getImm() == 0) {
                    state.fmt_line(buf,
                                   indent + 4,
                                   "// no index/disp in LLVM, can simply use "
                                   "the operand address");
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "inst{}_op{} = FE_MEM(addr.base, addr.scale, "
                        "addr.scale ? addr.index : FE_NOREG, addr.disp);",
                        inst_id,
                        op_idx);
                } else if (index_reg.getReg().isValid()) {
                    // TODO(ts): if index_reg is an AsmOperand, check if it is
                    // an imm and fold it into the displacement
                    //
                    // for now, just move the addr into a scratch and then build
                    // a mem op with the llvm index
                    std::format_to(
                        std::inserter(buf, buf.begin() + mem_insert_idx),
                        "{:>{}}ScratchReg inst{}_op{}_scratch{{derived()}};\n",
                        "",
                        indent,
                        inst_id,
                        op_idx);
                    // TODO(ts): don't need to do this if displacement is zero
                    // and addr.scale == 0
                    state.fmt_line(buf,
                                   indent + 4,
                                   "// LLVM memory operand has index, need to "
                                   "materialize the addr");
                    state.fmt_line(buf,
                                   indent + 4,
                                   "AsmReg base_tmp = "
                                   "inst{}_op{}_scratch.alloc_from_bank(0);",
                                   inst_id,
                                   op_idx);
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "ASMD(LEA64rm, base_tmp, FE_MEM(addr.base, addr.scale, "
                        "addr.scale ? addr.index : FE_NOREG, addr.disp));");

                    state.fmt_line(buf,
                                   indent + 4,
                                   "// gather the LLVM memory operand index {}",
                                   state.enc_target->reg_name_lower(
                                       state.enc_target->reg_id_from_mc_reg(
                                           index_reg.getReg())));
                    const auto index_aliased_id = state.resolve_reg_alias(
                        buf,
                        indent,
                        state.enc_target->reg_id_from_mc_reg(
                            index_reg.getReg()));
                    if (state.value_map[index_aliased_id].ty
                        == ValueInfo::ASM_OPERAND) {
                        state.fmt_line(
                            buf,
                            indent + 4,
                            "// {} maps to operand {}",
                            state.enc_target->reg_name_lower(index_aliased_id),
                            state.value_map[index_aliased_id].operand_name);
                        state.fmt_line(
                            buf,
                            indent + 4,
                            "AsmReg index_tmp = {}.as_reg(this);",
                            state.value_map[index_aliased_id].operand_name);
                    } else {
                        state.fmt_line(
                            buf,
                            indent + 4,
                            "AsmReg index_tmp = scratch_{}.cur_reg;",
                            state.enc_target->reg_name_lower(index_aliased_id));
                    }
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "inst{}_op{} = FE_MEM(base_tmp, {}, index_tmp, {});",
                        inst_id,
                        op_idx,
                        scale_factor.getImm(),
                        displacement.getImm());
                } else {
                    // try to check if the displacement is encodeable
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "// LLVM memory operand has displacement, check if it "
                        "is encodeable with the disp from addr");
                    state.fmt_line(buf,
                                   indent + 4,
                                   "if (disp_add_encodeable(addr.disp, {}) {{",
                                   displacement.getImm());
                    state.fmt_line(
                        buf,
                        indent + 8,
                        "inst{}_op{} = FE_MEM(addr.base, addr.scale, "
                        "addr.scale ? addr.index : FE_NOREG, addr.disp + {});",
                        inst_id,
                        op_idx,
                        displacement.getImm());
                    state.fmt_line(buf, indent + 4, "}} else {{");
                    std::format_to(
                        std::inserter(buf, buf.begin() + mem_insert_idx),
                        "{:>{}}ScratchReg inst{}_op{}_scratch{{derived()}};\n",
                        "",
                        indent,
                        inst_id,
                        op_idx);
                    state.fmt_line(
                        buf,
                        indent + 8,
                        "// displacements not encodeable together, need to "
                        "materialize the addr");
                    state.fmt_line(buf,
                                   indent + 8,
                                   "AsmReg base_tmp = "
                                   "inst{}_op{}_scratch.alloc_from_bank(0);",
                                   inst_id,
                                   op_idx);
                    state.fmt_line(
                        buf,
                        indent + 8,
                        "ASMD(LEA64rm, base_tmp, FE_MEM(addr.base, addr.scale, "
                        "addr.scale ? addr.index : FE_NOREG, addr.disp));");
                    state.fmt_line(
                        buf,
                        indent + 8,
                        "inst{}_op{} = FE_MEM(base_tmp, 0, FE_NOREG, {});",
                        inst_id,
                        op_idx,
                        displacement.getImm());
                    state.fmt_line(buf, indent + 4, "}}");
                }


                buf += std::format("{:>{}}}} else {{\n", "", indent);
            }

            if (state.value_map[base_reg_aliased_id].ty
                == ValueInfo::ASM_OPERAND) {
                state.fmt_line(
                    buf,
                    indent + 4,
                    "// {} maps to operand {}",
                    state.enc_target->reg_name_lower(base_reg_aliased_id),
                    state.value_map[base_reg_aliased_id].operand_name);
                state.fmt_line(
                    buf,
                    indent + 4,
                    "AsmReg base = {}.as_reg(this);",
                    state.value_map[base_reg_aliased_id].operand_name);
            } else {
                state.fmt_line(
                    buf,
                    indent + 4,
                    "AsmReg base = scratch_{}.cur_reg;",
                    state.enc_target->reg_name_lower(base_reg_aliased_id));
            }
            if (index_reg.getReg().isValid()) {
                // TODO(ts): if index_reg is an AsmOperand, check if it is
                // an imm and fold it into the displacement
                const auto idx_reg_id =
                    state.enc_target->reg_id_from_mc_reg(index_reg.getReg());
                state.fmt_line(buf,
                               indent + 4,
                               "// LLVM memory operand has index reg {}",
                               state.enc_target->reg_name_lower(idx_reg_id));
                const auto idx_reg_aliased_id =
                    state.resolve_reg_alias(buf, indent + 4, idx_reg_id);
                if (state.value_map[idx_reg_aliased_id].ty
                    == ValueInfo::ASM_OPERAND) {
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "// {} maps to operand {}",
                        state.enc_target->reg_name_lower(idx_reg_aliased_id),
                        state.value_map[idx_reg_aliased_id].operand_name);
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "AsmReg index_tmp = {}.as_reg(this);",
                        state.value_map[idx_reg_aliased_id].operand_name);
                } else {
                    state.fmt_line(
                        buf,
                        indent + 4,
                        "AsmReg index_tmp = scratch_{}.cur_reg;",
                        state.enc_target->reg_name_lower(idx_reg_aliased_id));
                }

                state.fmt_line(buf,
                               indent + 4,
                               "inst{}_op{} = FE_MEM(base, {}, index_tmp, {});",
                               inst_id,
                               op_idx,
                               scale_factor.getImm(),
                               displacement.getImm());
            } else {
                state.fmt_line(buf,
                               indent + 4,
                               "inst{}_op{} = FE_MEM(base, 0, FE_NOREG, {});",
                               inst_id,
                               op_idx,
                               displacement.getImm());
            }

            if (base_is_asm_op) {
                buf += std::format("{:>{}}}}\n", "", indent);
            }

            op_names.push_back(std::format("inst_{}_op{}", inst_id, op_idx));

            break;
        }
        case OP_IMM: {
            state.fmt_line(
                buf, indent, "// operand {} is an immediate operand", op_idx);

            const auto llvm_op_idx    = desc.operands[op_idx].llvm_idx;
            auto       is_replacement = false;
            if (llvm_op_idx >= llvm_inst_desc.operands().size()) {
                // this was an implicit (physical) register operand
                is_replacement = true;
                implicit_ops_handled.push_back(llvm_op_idx);

                if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
                    // should not happen
                    assert(0);
                    exit(1);
                }
                ++implicit_use_count;
            } else {
                const auto &op_info = llvm_inst_desc.operands()[llvm_op_idx];
                is_replacement =
                    op_info.OperandType != llvm::MCOI::OPERAND_IMMEDIATE;

                if (inst->getOperand(llvm_op_idx).isReg()
                    && inst->getOperand(llvm_op_idx).isImplicit()) {
                    implicit_ops_handled.push_back(llvm_op_idx);

                    if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
                        // should not happen
                        assert(0);
                        exit(1);
                    }
                    ++implicit_use_count;
                } else {
                    assert(!inst->getOperand(llvm_op_idx).isReg()
                           || !inst->getOperand(llvm_op_idx).isImplicit());
                    if (explicit_use_count >= llvm_inst_desc.NumOperands) {
                        // should not happen
                        assert(0);
                        exit(1);
                    }
                    ++explicit_use_count;
                }
            }
            const auto &inst_op = inst->getOperand(llvm_op_idx);
            if (is_replacement) {
                // we have an replacement and know that the operand must be an
                // immediate
                assert(inst_op.isReg());
                const auto reg_id =
                    state.enc_target->reg_id_from_mc_reg(inst_op.getReg());
                assert(state.value_map.contains(reg_id));
                const auto resolved_reg_id =
                    state.resolve_reg_alias(buf, indent, reg_id);
                const auto &reg_info = state.value_map[resolved_reg_id];
                assert(reg_info.ty == ValueInfo::ASM_OPERAND);

                state.fmt_line(buf,
                               indent,
                               "const auto& imm = {}.imm();",
                               reg_info.operand_name);
                op_names.emplace_back("imm.const_u64");
            } else {
                assert(inst_op.isImm());
                op_names.push_back(std::format("{}", inst_op.getImm()));
            }

            break;
        }
        case OP_NONE: continue;
        default: {
            assert(0);
            exit(1);
        }
        }
    }

    // TODO(ts): factor this out a bit with the code above
    // need to make sure implicit register uses are also properly moved
    for (auto &op : inst->all_uses()) {
        if (!op.isReg() || !op.isImplicit()) {
            continue;
        }
        if (std::find(implicit_ops_handled.begin(),
                      implicit_ops_handled.end(),
                      op.getOperandNo())
            != implicit_ops_handled.end()) {
            continue;
        }

        assert(op.isReg());
        if (state.enc_target->reg_should_be_ignored(op.getReg())) {
            continue;
        }

        const auto reg_id = state.enc_target->reg_id_from_mc_reg(op.getReg());
        assert(state.value_map.contains(reg_id));

        state.fmt_line(buf,
                       indent,
                       "// Handling implicit operand {}",
                       state.enc_target->reg_name_lower(reg_id));
        if (implicit_use_count >= llvm_inst_desc.NumImplicitUses) {
            state.fmt_line(
                buf,
                indent,
                "// Ignoring since the number of implicit operands on the LLVM "
                "inst exceeds the number in the MCInstrDesc");
            continue;
        }
        ++implicit_use_count;

        const auto resolved_reg_id =
            state.resolve_reg_alias(buf, indent, reg_id);

        const auto &info = state.value_map[resolved_reg_id];
        if (info.ty == ValueInfo::SCRATCHREG && reg_id == resolved_reg_id) {
            state.fmt_line(buf,
                           indent,
                           "// Value is already in register, no need to copy");
            continue;
        }

        if (info.ty == ValueInfo::SCRATCHREG) {
            state.fmt_line(
                buf,
                indent,
                "// Need to break alias from {} to {} and copy the value",
                state.enc_target->reg_name_lower(reg_id),
                state.enc_target->reg_name_lower(resolved_reg_id));
            state.enc_target->generate_copy(
                buf,
                indent,
                state.enc_target->reg_bank(reg_id),
                std::format("scratch_{}.cur_reg",
                            state.enc_target->reg_name_lower(reg_id)),
                std::format("scratch_{}.cur_reg",
                            state.enc_target->reg_name_lower(resolved_reg_id)),
                reg_size_bytes(state.func, op.getReg()));
            assert(state.value_map[reg_id].ty == ValueInfo::REG_ALIAS);
            // TODO(ts): allow this function to bubble up these copies so we can
            // take advantage of them if they happen in all branches
            if (if_cond.empty()) {
                state.remove_reg_alias(
                    buf, reg_id, state.value_map[reg_id].alias_reg_id);
            }
            continue;
        }

        assert(info.ty == ValueInfo::ASM_OPERAND);
        state.fmt_line(
            buf,
            indent,
            "// Need to break alias from {} to operand {} and copy the value",
            state.enc_target->reg_name_lower(reg_id),
            info.operand_name);
        state.fmt_line(buf,
                       indent,
                       "AsmReg inst{}_op{}_tmp = {}.as_reg(this);",
                       inst_id,
                       op.getOperandNo(),
                       info.operand_name);
        state.enc_target->generate_copy(
            buf,
            indent,
            state.enc_target->reg_bank(reg_id),
            std::format("scratch_{}.cur_reg",
                        state.enc_target->reg_name_lower(reg_id)),
            std::format("inst{}_op{}_tmp", inst_id, op.getOperandNo()),
            reg_size_bytes(state.func, op.getReg()));

        // TODO(ts): propagate upwards, see above
        if (if_cond.empty()) {
            if (state.value_map[reg_id].ty == ValueInfo::REG_ALIAS) {
                state.remove_reg_alias(
                    buf, reg_id, state.value_map[reg_id].alias_reg_id);
            } else {
                assert(state.value_map[reg_id].ty == ValueInfo::ASM_OPERAND);
                state.value_map[reg_id].ty = ValueInfo::SCRATCHREG;
            }
        }
    }

    // allocate destinations if it did not yet happen
    // TODO(ts): we rely on the fact that the order when encoding corresponds
    // to llvms ordering of defs/uses which might not always be the case

    buf += "\n";

    std::string operand_str;
    unsigned    implicit_def_count = 0;
    for (const auto &def : inst->all_defs()) {
        assert(def.isReg() && def.getReg().isPhysical());
        const auto reg = def.getReg();
        if (def.isImplicit()) {
            if (implicit_def_count >= llvm_inst_desc.NumImplicitDefs) {
                state.fmt_line(
                    buf,
                    indent,
                    "// Ignoring implicit def {} as it exceeds the number of "
                    "implicit defs in the MCInstrDesc",
                    state.func->getSubtarget().getRegisterInfo()->getName(
                        def.getReg()));
                continue;
            }
            ++implicit_def_count;
        }

        if (state.enc_target->reg_should_be_ignored(reg)) {
            continue;
        }

        const auto reg_id = state.enc_target->reg_id_from_mc_reg(reg);
        if (def.isImplicit()) {
            // notify the outer code that the register is needed as a fixed
            // register
            state.fixed_reg_conds[reg_id].emplace_back(if_cond);
        }

        if (!defs_allocated[def_idx(&def)]) {
            std::format_to(std::back_inserter(buf),
                           "{:>{}}// def {} has not been allocated yet\n",
                           "",
                           indent,
                           state.enc_target->reg_name_lower(reg_id));
            std::format_to(std::back_inserter(buf),
                           "{:>{}}scratch_{}.alloc_from_bank({});\n",
                           "",
                           indent,
                           state.enc_target->reg_name_lower(reg_id),
                           state.enc_target->reg_bank(reg_id));
        }

        if (def.isImplicit()) {
            // implicit defs do not show up in the argument list
            continue;
        }

        operand_str += ", ";
        std::format_to(std::back_inserter(operand_str),
                       "scratch_{}.cur_reg",
                       state.enc_target->reg_name_lower(reg_id));
    }

    for (auto &name : op_names) {
        operand_str += ", ";
        operand_str += name;
    }

    std::format_to(std::back_inserter(buf),
                   "{:>{}}ASMD({}{})\n",
                   "",
                   indent,
                   desc.name_fadec,
                   operand_str);

    return true;
}
} // namespace

namespace tpde_encgen {
bool create_encode_function(llvm::MachineFunction *func,
                            std::string_view       name,
                            std::string           &decl_lines,
                            std::string           &impl_lines) {
    (void)func;
    (void)name;
    (void)decl_lines;
    (void)impl_lines;

    std::string write_buf{};

    // update dead/kill flags since they might not be very accurate anymore
    // NOTE: we assume that the MBB liveins are accurate though
    for (auto &MBB : *func) {
        llvm::recomputeLivenessFlags(MBB);
    }

    // write MachineIR as comment at the start of the function
    {
        std::string              tmp;
        llvm::raw_string_ostream os(tmp);
        func->print(os);

        auto view = std::string_view{tmp};
        while (!view.empty()) {
            write_buf += "    // ";

            const auto pos = view.find('\n');
            if (pos == std::string_view::npos) {
                write_buf += view;
            } else {
                write_buf += view.substr(0, pos);
                view       = view.substr(pos + 1);
            }
            write_buf += '\n';
        }
        write_buf += '\n';
    }

    // TODO(ts): parameterize
    auto            enc_target = std::make_unique<EncodingTargetX64>(func);
    GenerationState state{
        .func = func, .enc_target = enc_target.get(), .is_first_block = true};

// TODO(ts): this can't work if a IR value corresponds to multiple registers
// so we should parse debug info but that does not always exist
#if 0
    // try to get the names for the IR arguments
    auto &llvm_func = func->getFunction();
    for (llvm::Argument &arg : llvm_func.args()) {
        if (arg.hasName() && !arg.getName().empty()) {
            if (std::find(state.param_names.begin(),
                          state.param_names.end(),
                          arg.getName())
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    arg.getName().str(),
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(arg.getName().str());
        } else {
            auto name = std::format("param_{}", state.param_names.size());
            if (std::find(
                    state.param_names.begin(), state.param_names.end(), name)
                != state.param_names.end()) {
                std::cerr << std::format(
                    "ERROR: argument '{}' already exists in function '{}'!\n",
                    name,
                    llvm_func.getName().str());
                return false;
            }
            state.param_names.push_back(std::move(name));
        }
    }
#endif


    auto &mach_reg_info = func->getRegInfo();
    // const auto &target_reg_info = func->getSubtarget().getRegisterInfo();


    // map inputs
    {
        unsigned idx = 0;
        for (auto it  = mach_reg_info.livein_begin(),
                  end = mach_reg_info.livein_end();
             it != end;
             ++it) {
            auto name = std::format("param_{}", state.param_names.size());
            state.param_names.push_back(std::move(name));

            const auto reg_id = enc_target->reg_id_from_mc_reg(it->first);
            assert(!state.used_regs.contains(reg_id));
            state.used_regs.insert(reg_id);
            state.value_map[reg_id] = ValueInfo{
                .ty           = ValueInfo::ASM_OPERAND,
                .operand_name = state.param_names[idx++],
                // .cur_def_use_it =
                //    mach_reg_info.reg_instr_nodbg_begin(it->first)
            };

            std::format_to(std::back_inserter(write_buf),
                           "    // Mapping {} to {}\n",
                           enc_target->reg_name_lower(reg_id),
                           state.value_map[reg_id].operand_name);
        }
    }

    std::string write_buf_inner{};
    {
        for (auto bb_it = func->begin(); bb_it != func->end(); ++bb_it) {
            for (auto inst_it = bb_it->begin(); inst_it != bb_it->end();
                 ++inst_it) {
                llvm::MachineInstr *inst = &(*inst_it);
                if (inst->isDebugInstr()) {
                    continue;
                }


                std::string              tmp;
                llvm::raw_string_ostream os(tmp);
                inst->print(os, true, false, true, true);
                write_buf_inner += "\n\n    // ";
                write_buf_inner += tmp;
                write_buf_inner += "\n";

                if (!generate_inst(write_buf_inner, state, inst)) {
                    return false;
                }
                ++state.cur_inst_id;
            }
            state.is_first_block = false;
        }
    }

    // create ScratchRegs
    for (const auto &[reg, info] : state.value_map) {
        std::format_to(std::back_inserter(write_buf),
                       "    ScratchReg scratch_{}{{derived()}};\n",
                       state.enc_target->reg_name_lower(reg));
    }

    // handle fixed registers
    for (const auto &[reg, fix_conds] : state.fixed_reg_conds) {
        const auto reg_name_lower = state.enc_target->reg_name_lower(reg);
        auto       reg_name_upper = std::string{reg_name_lower};
        std::transform(reg_name_upper.begin(),
                       reg_name_upper.end(),
                       reg_name_upper.begin(),
                       toupper);
        auto if_written = false;
        if (!std::any_of(fix_conds.begin(), fix_conds.end(), [](const auto &e) {
                return e.empty();
            })) {
            // no use is unconditional, so we emit an if to not always fix the
            // register
            if_written    = true;
            auto if_conds = std::string{};
            for (auto i = 0u; i < fix_conds.size(); ++i) {
                if (std::find(
                        fix_conds.begin(), fix_conds.begin() + i, fix_conds[i])
                    != fix_conds.begin() + i) {
                    continue;
                }
                if (!if_conds.empty()) {
                    if_conds += " || ";
                }
                if_conds += '(';
                if_conds += fix_conds[i];
                if_conds += ')';
            }
            write_buf += "    if (";
            write_buf += if_conds;
            write_buf += ") {\n";
        }

        // TODO(ts): need more sophisticated fixed alloc helper
        std::format_to(std::back_inserter(write_buf),
                       "{:>{}}scratch_{}.alloc_specific(AsmReg::{});\n",
                       "",
                       if_written ? 8 : 4,
                       reg_name_lower,
                       reg_name_upper);
        if (if_written) {
            write_buf += "    }\n";
        }
    }


    // finish up function header
    {
        std::format_to(
            std::back_inserter(impl_lines),
            "template <typename Adaptor,\n"
            "          typename Derived,\n"
            "          template <typename, typename, typename>\n"
            "          class BaseTy>\n"
            "void EncodeCompiler<Adaptor, Derived, BaseTy>::encode_{}(",
            name);
    }

    auto first = true;
    for (auto &param : state.param_names) {
        if (!first) {
            std::format_to(std::back_inserter(impl_lines), ", ");
        } else {
            first = false;
        }
        std::format_to(std::back_inserter(impl_lines), "AsmOperand {}", param);
    }

    if (state.num_ret_regs == -1) {
        // TODO(ts): might mean no return
        std::cerr
            << "ERROR: number of return registers not set at end of function\n";
        return false;
    }

    for (int i = 0; i < state.num_ret_regs; ++i) {
        if (!first) {
            std::format_to(std::back_inserter(impl_lines), ", ");
        } else {
            first = false;
        }
        std::format_to(
            std::back_inserter(impl_lines), "ScratchReg &result_{}", i);
    }

    impl_lines += ") {\n";

    impl_lines += write_buf;
    impl_lines += write_buf_inner;

    impl_lines += "\n}\n\n";

    return true;
}
} // namespace tpde_encgen
