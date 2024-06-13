// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

// For testing of the analyzer components, we need a small IR that we can
// parse from text files

// Quick specification of the format:
// All identifiers are alphanumeric starting with a letter
// Comments are done using ; at the beginning of a line
//
// Function and block names don't have a prefix when they are defined, value
// names and func/block names when they are used are prefixed with %
//
// clang-format off
// <funcName>(%<valName>, %<valName>, ...) {
// <blockName>:
//     %<valName> = alloca <size>
// ; No operands
//     %<valName> =
//     %<valName> = %<valName>, %<valName>, ...
//     jump %<blockName>, %<blockName>, ...
//
// <blockName>:
//     %<valName> = phi [%<blockName>, %<valName>], [%<blockName>, %<valName>], ...
//     terminate
// }
//
// <funcName>...

// clang-format on
