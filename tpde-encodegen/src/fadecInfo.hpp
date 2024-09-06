// SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "instInfo.hpp"
#include <array>
#include <llvm/ADT/StringRef.h>
#include <string>
#include <vector>

namespace fadecUtils {

struct FunctionInfo {
    unsigned                numVariants;
    /// The indices to all variants
    std::array<unsigned, 4> variants;
    unsigned                numParams;
    std::array<OpType, 4>   params;
    /// If there are no variants, this is the full name. Otherwise, the base
    /// name
    llvm::StringRef         fadecName;
};

/// A sorted array of all names
extern std::array<llvm::StringRef, 7057> functionNames;
////An array of function information for each Fadec function. The indices match
/// the ones from functionNames
extern std::array<FunctionInfo, 7057>    functionInfos;

} // namespace fadecUtils
