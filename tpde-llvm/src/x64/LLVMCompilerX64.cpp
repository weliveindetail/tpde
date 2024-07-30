// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include <fstream>

#include "LLVMAdaptor.hpp"
#include "LLVMCompilerBase.hpp"
#include "tpde/x64/CompilerX64.hpp"

namespace tpde_llvm::x64 {

struct LLVMCompilerX64
    : tpde::x64::CompilerX64<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase> {
    using Base =
        tpde::x64::CompilerX64<LLVMAdaptor, LLVMCompilerX64, LLVMCompilerBase>;

    std::unique_ptr<LLVMAdaptor> adaptor;

    explicit LLVMCompilerX64(std::unique_ptr<LLVMAdaptor> &&adaptor)
        : Base{adaptor.get()}, adaptor(std::move(adaptor)) {
        static_assert(
            tpde::Compiler<LLVMCompilerX64, tpde::x64::PlatformConfig>);
    }

    [[nodiscard]] static tpde::x64::CallingConv
        cur_calling_convention() noexcept {
        return tpde::x64::CallingConv::SYSV_CC;
    }

    static bool arg_is_int128(IRValueRef) noexcept { return false; }

    bool cur_func_may_emit_calls() const noexcept { return true; }

    u32 val_part_count(IRValueRef) const noexcept { return 1; }

    u32 val_part_size(IRValueRef, u32) const noexcept { return 8; }

    u8 val_part_bank(IRValueRef, u32) const noexcept { return 0; }

    bool try_force_fixed_assignment(IRValueRef) const noexcept { return false; }

    std::optional<ValuePartRef> val_ref_special(ValLocalIdx local_idx,
                                                u32         part) noexcept {
        (void)local_idx;
        (void)part;
        return {};
    }

    void define_func_idx(IRFuncRef func, const u32 idx) noexcept {
        (void)func;
        (void)idx;
        // assert(static_cast<u32>(func) == idx);
    }
};

extern bool compile_llvm(llvm::LLVMContext &ctx,
                         llvm::Module      &mod,
                         const char        *out_path) {
    auto adaptor  = std::make_unique<LLVMAdaptor>(ctx, mod);
    auto compiler = std::make_unique<LLVMCompilerX64>(std::move(adaptor));


    if (!compiler->compile()) {
        return false;
    }

    std::ofstream         out{out_path, std::ios::binary};
    const std::vector<u8> data = compiler->assembler.build_object_file();
    out.write(reinterpret_cast<const char *>(data.data()), data.size());

    return true;
}
} // namespace tpde_llvm::x64
