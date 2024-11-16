# SPDX-FileCopyrightText: 2024 Tobias Kamm <tobias.kamm@tum.de>
# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

cmake_minimum_required(VERSION 3.13)

#------------------------------------------------------------------------------
# general
#------------------------------------------------------------------------------

project(tpde_encodegen)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_link_options("-fuse-ld=lld")

add_executable(tpde_encodegen)
add_executable(tpde::tpde_encodegen ALIAS tpde_encodegen)

#------------------------------------------------------------------------------
# options
#------------------------------------------------------------------------------

# option(TPDE_ENCGEN_X64 "enable x86-64 support" ON)


#------------------------------------------------------------------------------
# dependencies
#------------------------------------------------------------------------------

# hopscotch-map (disable warnings)
target_include_directories(tpde_encodegen SYSTEM PRIVATE ../deps/hopscotch-map/include)

# general deps directory (for args)
target_include_directories(tpde_encodegen PRIVATE ../deps/)

# Configure LLVM
set(TPDE_LINK_LLVM_STATIC FALSE CACHE BOOL "Should LLVM be linked statically?")
if (TPDE_ASSERTS)
    set(LLVM_ENABLE_ASSERTIONS "ON")
endif ()

if (TPDE_LINK_LLVM_STATIC)
    llvm_map_components_to_libnames(llvm_libs core irreader irprinter passes support bitreader bitstreamreader)
else ()
    set(llvm_libs LLVM)
endif ()

add_library(tpdeEncLLVM INTERFACE)
target_include_directories(tpdeEncLLVM SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(tpdeEncLLVM INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(tpdeEncLLVM INTERFACE ${llvm_libs})

#------------------------------------------------------------------------------
# sources
#------------------------------------------------------------------------------

set(TPDE_ENCGEN_HEADERS
)

set(TPDE_ENCGEN_SOURCES
    src/main.cpp
    src/encode_gen.cpp
    src/arm64/Target.cpp
    src/x64/Target.cpp
)

# set(TPDE_ENCGEN_X64_HEADERS
# )
#
# set(TPDE_ENCGEN_X64_SOURCES
# )


#------------------------------------------------------------------------------
# target tpde_encodegen
#------------------------------------------------------------------------------

target_include_directories(tpde_encodegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(tpde_encodegen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_sources(tpde_encodegen PRIVATE ${TPDE_ENCGEN_SOURCES} ${TPDE_ENCGEN_HEADERS})

# if (TPDE_ENCGEN_X64)
#     target_sources(tpde_encodegen PRIVATE ${TPDE_ENCGEN_X64_SOURCES} ${TPDE_ENCGEN_X64_HEADERS})
# endif ()

set_target_properties(tpde_encodegen PROPERTIES
    VERSION 0.1.0
    LINKER_LANGUAGE CXX
)

# warnings
if (MSVC)
    target_compile_options(tpde_encodegen PRIVATE /W4 /WX)
    # disable RTTI and exceptions
    target_compile_options(tpde_encodegen PRIVATE /GR- /EHsc /D_HAS_EXCEPTIONS=0)
else ()
    target_compile_options(tpde_encodegen PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # disable RTTI and exceptions
    target_compile_options(tpde_encodegen PRIVATE -fno-rtti -fno-exceptions)

    # this is annoying
    target_compile_options(tpde_encodegen PRIVATE -Wno-missing-field-initializers)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tpde_encodegen PRIVATE -fsanitize=address)
    target_link_options(tpde_encodegen PRIVATE -fsanitize=address)

    target_compile_definitions(tpde_encodegen PUBLIC TPDE_TESTING)
endif ()

# link llvm
target_link_libraries(tpde_encodegen PUBLIC tpdeEncLLVM)
