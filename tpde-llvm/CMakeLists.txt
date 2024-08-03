# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.13)

#------------------------------------------------------------------------------
# general
#------------------------------------------------------------------------------

project(tpde_llvm)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tpde_llvm)
add_executable(tpde::tpde_llvm ALIAS tpde_llvm)

#------------------------------------------------------------------------------
# options
#------------------------------------------------------------------------------

option(TPDE_LLVM_X64 "enable x86-64 support" ON)


#------------------------------------------------------------------------------
# dependencies
#------------------------------------------------------------------------------

# we want to overwrite the options of the subdirs
# see https://stackoverflow.com/a/75700631
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# tpde config
if (TPDE_LLVM_X64)
    set(TPDE_X64 ON)
else ()
    set(TPDE_X64 OFF)
endif ()

# tpde
add_subdirectory(../tpde ${PROJECT_BINARY_DIR}/tpde)

# hopscotch-map (disable warnings)
target_include_directories(tpde_llvm SYSTEM PRIVATE ../deps/hopscotch-map/include)

# general deps directory (for args)
target_include_directories(tpde_llvm PRIVATE ../deps/)

# Configure LLVM
set(TPDE_LINK_LLVM_STATIC FALSE CACHE BOOL "Should LLVM be linked statically?")
if (TPDE_ASSERTS)
    set(LLVM_ENABLE_ASSERTIONS "ON")
endif ()
# set(LLVM_ABI_BREAKING_CHECKS "FORCE_OFF")

find_package(LLVM 19 REQUIRED CONFIG)
if (TPDE_LINK_LLVM_STATIC)
    llvm_map_components_to_libnames(llvm_libs core irreader irprinter passes support bitreader bitstreamreader)
else ()
    set(llvm_libs LLVM)
endif ()
add_library(tpdeLLVM INTERFACE)
target_include_directories(tpdeLLVM SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(tpdeLLVM INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(tpdeLLVM INTERFACE ${llvm_libs})

#------------------------------------------------------------------------------
# sources
#------------------------------------------------------------------------------

set(TPDE_LLVM_HEADERS
    src/base.hpp
    src/LLVMAdaptor.hpp
    src/LLVMCompilerBase.hpp
)

set(TPDE_LLVM_HEADERS_PUBLIC
)

set(TPDE_LLVM_SOURCES
    src/main.cpp
)

set(TPDE_LLVM_X64_HEADERS
    src/x64/LLVMCompilerX64.hpp
)

set(TPDE_LLVM_X64_SOURCES
    src/x64/LLVMCompilerX64.cpp
)


#------------------------------------------------------------------------------
# target tpde_llvm
#------------------------------------------------------------------------------

target_include_directories(tpde_llvm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(tpde_llvm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_sources(tpde_llvm PRIVATE ${TPDE_LLVM_SOURCES} ${TPDE_LLVM_HEADERS})

if (TPDE_LLVM_X64)
    target_sources(tpde_llvm PRIVATE ${TPDE_LLVM_X64_SOURCES} ${TPDE_LLVM_X64_HEADERS})
endif ()

set_target_properties(tpde_llvm PROPERTIES
    VERSION 0.1.0
    PUBLIC_HEADERS "${TPDE_LLVM_HEADERS_PUBLIC}"
    LINKER_LANGUAGE CXX
)

# warnings
if (MSVC)
    target_compile_options(tpde_llvm PRIVATE /W4 /WX)
    # disable RTTI and exceptions
    target_compile_options(tpde_llvm PRIVATE /GR- /EHsc /D_HAS_EXCEPTIONS=0)
else ()
    target_compile_options(tpde_llvm PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # disable RTTI and exceptions
    target_compile_options(tpde_llvm PRIVATE -fno-rtti -fno-exceptions)

    # this is annoying
    target_compile_options(tpde_llvm PRIVATE -Wno-missing-field-initializers)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tpde_llvm PRIVATE -fsanitize=address)
    target_link_options(tpde_llvm PRIVATE -fsanitize=address)
endif ()

# link tpde
target_link_libraries(tpde_llvm PUBLIC tpde::tpde)
# link llvm
target_link_libraries(tpde_llvm PUBLIC tpdeLLVM)

#[[
# TODO(ts): create test target which sets TPDE_TESTING so that we can say,
# we only want to run until the early stages of the loop analysis or w/e
#------------------------------------------------------------------------------
# target tpde_test
#------------------------------------------------------------------------------

add_executable(tpde_test)

target_link_libraries(tpde_test PUBLIC tpde)

target_include_directories(tpde_test PRIVATE ../deps/)

set(TPDE_TEST_SOURCES
    src/test/TestIR.cpp
    src/test/TestIR.hpp
    src/test/TestIRCompiler.cpp
    src/test/TestIRCompiler.hpp
    src/test/test_main.cpp
)

target_sources(tpde_test PRIVATE ${TPDE_TEST_SOURCES})

set_target_properties(tpde_test PROPERTIES
    VERSION 0.1.0
    LINKER_LANGUAGE CXX
)

# warnings
if (MSVC)
    target_compile_options(tpde_test PRIVATE /W4 /WX)
else ()
    target_compile_options(tpde_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

    # this is annoying
    target_compile_options(tpde_test PRIVATE -Wno-missing-field-initializers)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tpde_test PRIVATE -fsanitize=address)
    target_link_options(tpde_test PRIVATE -fsanitize=address)
endif ()

target_compile_options(tpde_test PRIVATE -ferror-limit=1000)

target_compile_definitions(tpde_test PUBLIC TPDE_TESTING)
]]
#

#------------------------------------------------------------------------------
# target tpde_llvm_filetest
#------------------------------------------------------------------------------

# configure lit.site.cfg.py
configure_file(test/filetest/lit.site.cfg.py.in test/filetest/lit.site.cfg.py @ONLY)

# TODO(ts): make this configurable
add_custom_target(tpde_llvm_filetest
    COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-lit -v "${CMAKE_CURRENT_BINARY_DIR}/test/filetest"
    DEPENDS tpde_llvm)
