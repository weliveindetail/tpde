# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.23)

project(tpde2)

# TODO(ae): use target_compile_features(cxx_std_20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    # practically supported by every compiler
    add_compile_options(-Wno-nested-anon-types)
    # i want these sometimes
    add_compile_options(-Wno-old-style-cast)
    # We often add unreachable() to default cases
    add_compile_options(-Wno-covered-switch-default)

    # for development builds use -Werror and enable sanitizers
    if (PROJECT_IS_TOP_LEVEL)
        add_compile_options(-Werror -Wno-error=deprecated-declarations)
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_compile_options(-fsanitize=address -D_GLIBCXX_ASSERTIONS)
            add_link_options(-fsanitize=address)
        endif ()
    endif ()
endif ()


if (NOT LLVM_FOUND)
    find_package(LLVM 19.1 REQUIRED CONFIG)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
else ()
    message(STATUS "Using LLVM from parent directory")
endif ()

find_program(LIT NAMES ${LLVM_DEFAULT_EXTERNAL_LIT} lit llvm-lit REQUIRED)
message(STATUS "Found lit: ${LIT}")

# *sigh*, why can't CLion just support this workflow...
# add_subdirectory(tpde "${PROJECT_BINARY_DIR}/tpde")
add_subdirectory(tpde-llvm)
