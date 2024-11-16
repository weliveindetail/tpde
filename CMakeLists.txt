# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

# workaround for CLion not supporting multiple cmake projects in one project
cmake_minimum_required(VERSION 3.12)

project(tpde2)

# TODO(ae): Remove workaround when upstream Fadec/Disarm get CMake support
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/deps/disarm-CMakeLists.txt ${CMAKE_CURRENT_SOURCE_DIR}/deps/disarm/CMakeLists.txt ONLY_IF_DIFFERENT)
file(COPY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/deps/fadec-CMakeLists.txt ${CMAKE_CURRENT_SOURCE_DIR}/deps/fadec/CMakeLists.txt ONLY_IF_DIFFERENT)

# *sigh*, why can't CLion just support this workflow...
# add_subdirectory(tpde "${PROJECT_BINARY_DIR}/tpde")
add_subdirectory(tpde-llvm)
