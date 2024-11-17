# SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.23)

project(tpde_encodegen)

add_executable(tpde_encodegen)
add_executable(tpde::tpde_encodegen ALIAS tpde_encodegen)

# Configure LLVM
set(TPDE_LINK_LLVM_STATIC FALSE CACHE BOOL "Should LLVM be linked statically?")

target_include_directories(tpde_encodegen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(tpde_encodegen PRIVATE ${LLVM_DEFINITIONS})
if (TPDE_LINK_LLVM_STATIC)
    llvm_map_components_to_libnames(TPDE_ENCODEGEN_LLVM_LIBS
        core X86 AArch64 CodeGen irreader irprinter passes mc support targetparser asmparser asmprinter bitreader bitstreamreader
    )
    target_link_libraries(tpde_encodegen PRIVATE ${TPDE_ENCODEGEN_LLVM_LIBS})
else ()
    target_link_libraries(tpde_encodegen PRIVATE LLVM)
endif ()

target_sources(tpde_encodegen PRIVATE
    src/main.cpp
    src/encode_gen.cpp
    src/arm64/Target.cpp
    src/x64/Target.cpp

    PRIVATE
    FILE_SET HEADERS
    BASE_DIRS src
    FILES
        src/encode_gen.hpp
        src/Target.hpp
        src/arm64/Target.hpp
        src/x64/Target.hpp
)
