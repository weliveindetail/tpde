// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include <llvm/CodeGen/MachineFunction.h>

namespace tpde_encgen {
bool create_encode_function(llvm::MachineFunction *,
                            std::string_view name,
                            std::string     &decl_lines,
                            std::string     &impl_lines);
}
