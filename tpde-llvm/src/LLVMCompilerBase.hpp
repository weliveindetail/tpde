// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "tpde/CompilerBase.hpp"

#include "LLVMAdaptor.hpp"

namespace tpde_llvm {

template <typename Adaptor, typename Derived, typename Config>
struct LLVMCompilerBase : tpde::CompilerBase<LLVMAdaptor, Derived, Config> {
    // TODO
    using Base = tpde::CompilerBase<LLVMAdaptor, Derived, Config>;

    using IRValueRef = typename Base::IRValueRef;

    LLVMCompilerBase(LLVMAdaptor *adaptor, const bool generate_obj)
        : Base{adaptor, generate_obj} {
        static_assert(tpde::Compiler<Derived, Config>);
        static_assert(std::is_same_v<Adaptor, LLVMAdaptor>);
    }

    Derived *derived() noexcept { return static_cast<Derived *>(this); }

    const Derived *derived() const noexcept {
        return static_cast<Derived *>(this);
    }

    // TODO(ts): check if it helps to check this
    static bool cur_func_may_emit_calls() noexcept { return true; }

    static bool try_force_fixed_assignment(IRValueRef) noexcept {
        return false;
    }

    IRValueRef llvm_val_idx(llvm::Value *) const noexcept;
    IRValueRef llvm_val_idx(llvm::Instruction *) const noexcept;

    bool compile_inst(IRValueRef) noexcept;

    bool compile_ret(IRValueRef, llvm::Instruction *) noexcept;
};

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        llvm::Value *val) const noexcept {
    return this->adaptor->val_lookup_idx(val);
}

template <typename Adaptor, typename Derived, typename Config>
typename LLVMCompilerBase<Adaptor, Derived, Config>::IRValueRef
    LLVMCompilerBase<Adaptor, Derived, Config>::llvm_val_idx(
        llvm::Instruction *inst) const noexcept {
    return this->adaptor->inst_lookup_idx(inst);
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_inst(
    IRValueRef val_idx) noexcept {
    auto *i =
        llvm::dyn_cast<llvm::Instruction>(this->adaptor->values[val_idx].val);
    const auto opcode = i->getOpcode();
    switch (opcode) {
    case llvm::Instruction::Ret: return compile_ret(val_idx, i);
    default: {
        TPDE_LOG_ERR("Encountered unknown instruction opcode {}: {}",
                     opcode,
                     i->getOpcodeName());
        assert(0);
        exit(1);
    }
    }
}

template <typename Adaptor, typename Derived, typename Config>
bool LLVMCompilerBase<Adaptor, Derived, Config>::compile_ret(
    IRValueRef, llvm::Instruction *ret) noexcept {
    assert(llvm::isa<llvm::ReturnInst>(ret));

    if (ret->getNumOperands() != 0) {
        assert(ret->getNumOperands() == 1);
        derived()->move_val_to_ret_regs(ret->getOperand(0));
    }

    derived()->gen_func_epilog();
    this->release_regs_after_return();
    return true;
}
} // namespace tpde_llvm
