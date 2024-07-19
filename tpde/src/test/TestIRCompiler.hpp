// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "TestIR.hpp"
#include "tpde/x64/CompilerX64.hpp"

namespace tpde::test {
struct TestIRCompilerX64 : x64::CompilerX64<TestIRAdaptor, TestIRCompilerX64> {
    using Base = x64::CompilerX64<TestIRAdaptor, TestIRCompilerX64>;

    using IRValueRef   = typename Base::IRValueRef;
    using IRFuncRef    = typename Base::IRFuncRef;
    using ValuePartRef = typename Base::ValuePartRef;
    using ValLocalIdx  = typename Base::ValLocalIdx;
    using ScratchReg   = typename Base::ScratchReg;

    explicit TestIRCompilerX64(TestIRAdaptor *adaptor) : Base{adaptor} {}

    [[nodiscard]] static x64::CallingConv cur_calling_convention() noexcept {
        return x64::CallingConv::SYSV_CC;
    }

    static bool arg_is_int128(IRValueRef) noexcept { return false; }

    std::optional<ValuePartRef> val_ref_special(ValLocalIdx local_idx,
                                                u32         part) noexcept {
        (void)local_idx;
        (void)part;
        return {};
    }

    void define_func_idx(IRFuncRef func, const u32 idx) noexcept {
        assert(static_cast<u32>(func) == idx);
    }

    [[nodiscard]] bool compile_inst(IRValueRef) noexcept;

    TestIR *ir() noexcept { return this->adaptor->ir; }

    bool compile_add(IRValueRef) noexcept;
    bool compile_sub(IRValueRef) noexcept;
};
} // namespace tpde::test
