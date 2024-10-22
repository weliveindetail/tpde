# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.12)

#------------------------------------------------------------------------------
# general
#------------------------------------------------------------------------------

project(tpde)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tpde)
add_library(tpde::tpde ALIAS tpde)

#------------------------------------------------------------------------------
# options
#------------------------------------------------------------------------------

set(TPDE_LOGGING "DebugOnly" CACHE STRING "Enable logging with spdlog (DebugOnly(default)/ON/OFF)")
set_property(CACHE TPDE_LOGGING PROPERTY STRINGS DebugOnly ON OFF)

option(TPDE_X64 "enable x86-64 support" ON)
option(TPDE_A64 "enable AArch64 support" ON)

#------------------------------------------------------------------------------
# dependencies
#------------------------------------------------------------------------------

# spdlog
if (((TPDE_LOGGING STREQUAL "DebugOnly") AND (CMAKE_BUILD_TYPE STREQUAL "Debug")) OR (TPDE_LOGGING STREQUAL "ON"))
    set(TPDE_INCLUDE_SPDLOG TRUE)

    set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "TPDE compiles without exceptions")
    add_subdirectory(../deps/spdlog ${CMAKE_CURRENT_BINARY_DIR}/deps/spdlog)
else ()
    set(TPDE_INCLUDE_SPDLOG FALSE)
endif ()

# gharveymn/small_vector
add_subdirectory(../deps/small_vector ${CMAKE_CURRENT_BINARY_DIR}/deps/small_vector)

# fadec
if (TPDE_X64)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(FADEC_DECODE ON CACHE BOOL "fadec decode for asserts")
    else ()
        set(FADEC_DECODE OFF CACHE BOOL "no fadec decode")
    endif ()
    set(FADEC_ENCODE OFF CACHE BOOL "no fadec encode")
    set(FADEC_ENCODE2 ON CACHE BOOL "fadec encode2 support")
    add_subdirectory(../deps/fadec ${CMAKE_CURRENT_BINARY_DIR}/deps/fadec)
endif ()

# disarm
if (TPDE_A64)
    add_subdirectory(../deps/disarm ${CMAKE_CURRENT_BINARY_DIR}/deps/disarm)
endif ()

#------------------------------------------------------------------------------
# sources
#------------------------------------------------------------------------------

set(TPDE_HEADERS
    include/tpde/Analyzer.hpp
    include/tpde/Assembler.hpp
    include/tpde/base.hpp
    include/tpde/Compiler.hpp
    include/tpde/CompilerBase.hpp
    include/tpde/AssemblerElf.hpp
    include/tpde/IRAdaptor.hpp
    include/tpde/RegisterFile.hpp
    include/tpde/ScratchReg.hpp
    include/tpde/AssignmentPartRef.hpp
    include/tpde/ValuePartRef.hpp
    include/tpde/util/SmallBitSet.hpp
    include/tpde/util/SmallVector.hpp
    include/tpde/util/misc.hpp
)

set(TPDE_SOURCES
    # dummy target for autocomplete
    src/main.cpp
)

set(TPDE_X64_HEADERS
    include/tpde/x64/AssemblerElfX64.hpp
    include/tpde/x64/CompilerX64.hpp
)

set(TPDE_A64_HEADERS
    include/tpde/arm64/AssemblerElfA64.hpp
    include/tpde/arm64/CompilerA64.hpp
)

#------------------------------------------------------------------------------
# target tpde
#------------------------------------------------------------------------------

target_include_directories(tpde PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(tpde PRIVATE ${TPDE_SOURCES} ${TPDE_HEADERS})

if (TPDE_X64)
    target_sources(tpde PRIVATE ${TPDE_X64_HEADERS})
    target_link_libraries(tpde PUBLIC fadec::fadec)
endif ()

if (TPDE_A64)
    target_sources(tpde PRIVATE ${TPDE_A64_HEADERS})
    target_link_libraries(tpde PUBLIC disarm64::disarm64)
endif ()

set_target_properties(tpde PROPERTIES
    VERSION 0.1.0
    PUBLIC_HEADERS "${TPDE_HEADERS}"
    LINKER_LANGUAGE CXX
)

# warnings
if (MSVC)
    target_compile_options(tpde PRIVATE /W4 /WX)
    # disable RTTI and exceptions
    target_compile_options(tpde PRIVATE /GR- /EHsc /D_HAS_EXCEPTIONS=0)
else ()
    target_compile_options(tpde PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # disable RTTI and exceptions
    target_compile_options(tpde PRIVATE -fno-rtti -fno-exceptions)

    # this is annoying
    target_compile_options(tpde PRIVATE -Wno-missing-field-initializers)
    # practically supported by every compiler
    target_compile_options(tpde PRIVATE -Wno-nested-anon-types)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tpde PUBLIC TPDE_DEBUG)
    target_compile_definitions(tpde PUBLIC TPDE_ASSERTS)
    set(TPDE_ASSERTS TRUE)

    target_compile_options(tpde PRIVATE -fsanitize=address)
    target_link_options(tpde PRIVATE -fsanitize=address)
endif ()

# depend on spdlog if logging is enabled
if (${TPDE_INCLUDE_SPDLOG})
    target_compile_definitions(tpde PUBLIC TPDE_LOGGING)
    target_link_libraries(tpde PUBLIC spdlog::spdlog)
endif ()

# link small_vector
target_link_libraries(tpde PUBLIC gch::small_vector)
# we don't use exceptions
target_compile_definitions(tpde PUBLIC GCH_NO_STRONG_EXCEPTION_GUARANTEES)


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
    src/test/TestIRCompilerA64.hpp
    src/test/TestIRCompilerA64.cpp
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
    # practically supported by every compiler
    target_compile_options(tpde_test PRIVATE -Wno-nested-anon-types)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tpde_test PRIVATE -fsanitize=address)
    target_link_options(tpde_test PRIVATE -fsanitize=address)
endif ()

#target_compile_options(tpde_test PRIVATE -ferror-limit=1000)

target_compile_definitions(tpde_test PUBLIC TPDE_TESTING)

#------------------------------------------------------------------------------
# target tpde_filetest
#------------------------------------------------------------------------------

# configure lit.site.cfg.py
configure_file(test/filetest/lit.site.cfg.py.in test/filetest/lit.site.cfg.py @ONLY)

# TODO(ts): make this configurable
add_custom_target(tpde_filetest
    COMMAND lit -v "${CMAKE_CURRENT_BINARY_DIR}/test/filetest"
    DEPENDS tpde_test)
