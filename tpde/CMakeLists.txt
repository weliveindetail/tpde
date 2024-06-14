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

#------------------------------------------------------------------------------
# dependencies
#------------------------------------------------------------------------------

# spdlog
if (((TPDE_LOGGING STREQUAL "DebugOnly") AND (CMAKE_BUILD_TYPE STREQUAL "Debug")) OR (TPDE_LOGGING STREQUAL "ON"))
    set(TPDE_INCLUDE_SPDLOG TRUE)

    set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "TPDE compiles without exceptions")
    add_subdirectory(../tp/spdlog ${CMAKE_CURRENT_BINARY_DIR}/tp/spdlog)
else ()
    set(TPDE_INCLUDE_SPDLOG FALSE)
endif ()


#------------------------------------------------------------------------------
# sources
#------------------------------------------------------------------------------

set(TPDE_HEADERS
    include/tpde/base.hpp
    include/tpde/CompilerBase.hpp
    include/tpde/CompilerBase.hpp
    include/tpde/IRAdaptor.hpp
    include/tpde/Analyzer.hpp
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
    target_compile_options(tpde PRIVATE /W4 /WX)
    # disable RTTI and exceptions
    target_compile_options(tpde PRIVATE /GR- /EHsc /D_HAS_EXCEPTIONS=0)
else ()
    target_compile_options(tpde PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # disable RTTI and exceptions
    target_compile_options(tpde PRIVATE -fno-rtti -fno-exceptions)
endif ()


if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tpde PUBLIC TPDE_DEBUG)
    target_compile_definitions(tpde PUBLIC TPDE_ASSERTS)
endif ()

if (${TPDE_INCLUDE_SPDLOG})
    target_compile_definitions(tpde PUBLIC TPDE_LOGGING)
    target_link_libraries(tpde PUBLIC spdlog::spdlog)
endif ()
