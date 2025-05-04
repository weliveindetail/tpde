# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.23)

project(tpde2)

# TODO(ae): use target_compile_features(cxx_std_20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TPDE_INCLUDE_TESTS "enable test" ON)
option(TPDE_ENABLE_COVERAGE "enable coverage instrumentation" OFF)

# warnings
if (MSVC)
    add_compile_options(/W4 /WX)
    # disable RTTI and exceptions
    add_compile_options(/GR- /EHsc /D_HAS_EXCEPTIONS=0)
else ()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # disable RTTI and exceptions
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti;-fno-exceptions>")

    # this is annoying
    add_compile_options(-Wno-missing-field-initializers)

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # practically supported by every compiler
        add_compile_options(-Wno-nested-anon-types)
        # i want these sometimes
        add_compile_options(-Wno-old-style-cast)
        # We often add unreachable() to default cases
        add_compile_options(-Wno-covered-switch-default)
        add_compile_options(-Wsign-compare)
    endif()
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-changes-meaning>)
    endif()

    # for development builds use -Werror and enable sanitizers
    if (PROJECT_IS_TOP_LEVEL)
        add_compile_options(-Werror -Wno-error=deprecated-declarations)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_compile_options(-fsanitize=address -D_GLIBCXX_ASSERTIONS)
            add_link_options(-fsanitize=address)
        endif ()
    endif ()
endif ()

if (TPDE_ENABLE_COVERAGE)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        error("coverage build only supported with Clang")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

if (NOT LLVM_FOUND)
    find_package(LLVM 19.1 CONFIG)
endif()
if (NOT LLVM_FOUND)
    find_package(LLVM 20.1 CONFIG)
    message(WARNING "LLVM version compatible, but tests are likely to fail")
    message(NOTICE "Tests are written against LLVM 19.1")
endif()
if (NOT LLVM_FOUND)
    message(FATAL_ERROR "Unable to find suitable LLVM (need 19.1 or 20.1)")
endif()
message(STATUS "Found LLVM ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
if (NOT "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}" STREQUAL "19.1")
    message(WARNING "LLVM version ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR} "
        "is supported, but tests will fail. Tests currently rely on LLVM 19.1; "
        "test failures will likely be caused by different code generation.")
endif()

if (TPDE_INCLUDE_TESTS)
    find_program(LIT NAMES ${LLVM_DEFAULT_EXTERNAL_LIT} lit llvm-lit REQUIRED)
    message(STATUS "Found lit: ${LIT}")
endif()

function(add_tpde_lit_target target)
    cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})
    add_custom_target(${target}
        COMMAND ${LIT} -sv ${ARG_UNPARSED_ARGUMENTS}
        USES_TERMINAL)
    if (ARG_DEPENDS)
        add_dependencies(${target} ${ARG_DEPENDS})
    endif()
endfunction()

function(add_tpde_lit_testsuite target)
    cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})
    add_tpde_lit_target(${target} ${ARG_UNPARSED_ARGUMENTS} DEPENDS ${ARG_DEPENDS})
    set_property(GLOBAL APPEND PROPERTY TPDE_LIT_TESTSUITES ${ARG_UNPARSED_ARGUMENTS})
    set_property(GLOBAL APPEND PROPERTY TPDE_LIT_DEPENDS ${ARG_DEPENDS})
endfunction()

# *sigh*, why can't CLion just support this workflow...
# add_subdirectory(tpde "${PROJECT_BINARY_DIR}/tpde")
add_subdirectory(tpde-llvm)

if (TPDE_INCLUDE_TESTS)
    get_property(TPDE_LIT_TESTSUITES GLOBAL PROPERTY TPDE_LIT_TESTSUITES)
    get_property(TPDE_LIT_DEPENDS GLOBAL PROPERTY TPDE_LIT_DEPENDS)
    add_tpde_lit_target(check-tpde ${TPDE_LIT_TESTSUITES} DEPENDS ${TPDE_LIT_DEPENDS})
endif()
