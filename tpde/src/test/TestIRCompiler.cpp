// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "TestIRCompiler.hpp"

namespace tpde::test {
bool TestIRCompilerX64::compile_inst(IRValueRef val_idx) noexcept {
    const TestIR::Value &value =
        this->analyzer.adaptor->ir->values[static_cast<u32>(val_idx)];

    switch (value.type) {
        using enum TestIR::Value::Type;
    case ret: {
        assert(value.op_count == 0);
        this->gen_func_epilog();
        return true;
    }
    default: assert(0); __builtin_unreachable();
    }

    return false;
}
} // namespace tpde::test
