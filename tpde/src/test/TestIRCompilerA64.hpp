// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "TestIR.hpp"
#include "tpde/base.hpp"

namespace tpde::test {
bool compile_ir_arm64(TestIR            *ir,
                      bool               no_fixed_assignments,
                      const std::string &obj_out_path);
}
