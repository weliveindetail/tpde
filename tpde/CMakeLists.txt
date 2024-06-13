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
# sources
#------------------------------------------------------------------------------

set(TPDE_HEADERS
    include/tpde/base.hpp
    include/tpde/IRAdaptor.hpp
)

set(TPDE_SOURCES
    # dummy target for autocomplete
    src/main.cpp
    src/test/TestIR.cpp
)

#------------------------------------------------------------------------------
# target tpde
#------------------------------------------------------------------------------

target_include_directories(tpde PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(tpde PRIVATE ${TPDE_SOURCES} ${TPDE_HEADERS})

set_target_properties(tpde PROPERTIES
    VERSION 0.1.0
    PUBLIC_HEADERS "${TPDE_HEADERS}"
    LINKER_LANGUAGE CXX
)

# warnings
if (MSVC)
    target_compile_options(tpde PRIVATE "/W4 /WX")
else ()
    target_compile_options(tpde PRIVATE "-Wall -Wextra -Wpedantic -Werror")
endif ()


if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tpde PUBLIC TPDE_DEBUG)
    target_compile_definitions(tpde PUBLIC TPDE_ASSERTS)
endif ()
