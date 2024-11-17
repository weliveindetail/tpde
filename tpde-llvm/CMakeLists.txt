# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.23)

project(tpde_llvm)


# tpde_llvm library

add_library(tpde_llvm)
add_library(tpde::tpde_llvm ALIAS tpde_llvm)
set_target_properties(tpde_llvm PROPERTIES
    VERSION 0.1.0
    LINKER_LANGUAGE CXX
)

add_subdirectory(../tpde ${PROJECT_BINARY_DIR}/tpde)
add_subdirectory(../tpde-encodegen ${PROJECT_BINARY_DIR}/tpde-encodegen)

target_link_libraries(tpde_llvm PRIVATE tpde)

# hopscotch-map (disable warnings)
target_include_directories(tpde_llvm SYSTEM PRIVATE ../deps/hopscotch-map/include)

# Configure LLVM
set(TPDE_LINK_LLVM_STATIC FALSE CACHE BOOL "Should LLVM be linked statically?")

target_include_directories(tpde_llvm SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_definitions(tpde_llvm PUBLIC ${LLVM_DEFINITIONS})
if (TPDE_LINK_LLVM_STATIC)
    llvm_map_components_to_libnames(TPDE_LLVM_LIBS
        core irreader irprinter passes support bitreader bitstreamreader targetparser
    )
    target_link_libraries(tpde_llvm PUBLIC ${TPDE_LLVM_LIBS})
else ()
    target_link_libraries(tpde_llvm PUBLIC LLVM)
endif ()

target_sources(tpde_llvm PRIVATE
    PRIVATE
    FILE_SET priv_headers TYPE HEADERS
    BASE_DIRS src
    FILES
        src/base.hpp
        src/LLVMAdaptor.hpp
        src/LLVMCompilerBase.hpp
)

if(TPDE_X64)
    target_compile_definitions(tpde_llvm PRIVATE TPDE_LLVM_X64)
    target_sources(tpde_llvm PRIVATE
        src/x64/LLVMCompilerX64.cpp

        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            src/x64/LLVMCompilerX64.hpp
    )
endif()

if(TPDE_A64)
    target_compile_definitions(tpde_llvm PRIVATE TPDE_LLVM_ARM64)
    target_sources(tpde_llvm PRIVATE
        src/arm64/LLVMCompilerArm64.cpp

        PUBLIC
        FILE_SET HEADERS
        BASE_DIRS src
        FILES
            src/arm64/LLVMCompilerArm64.hpp
    )
endif()


# tpde_llc binary

add_executable(tpde-llc tools/tpde-llc.cpp)
target_link_libraries(tpde-llc PRIVATE tpde_llvm)

# general deps directory (for args)
target_include_directories(tpde-llc PRIVATE ../deps/)


# Tests

# configure lit.site.cfg.py
configure_file(test/filetest/lit.site.cfg.py.in test/filetest/lit.site.cfg.py @ONLY)

# TODO(ts): make this configurable
add_custom_target(tpde_llvm_filetest
    COMMAND ${LIT} -v "${CMAKE_CURRENT_BINARY_DIR}/test/filetest"
    DEPENDS tpde-llc
    USES_TERMINAL)
