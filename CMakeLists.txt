# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

# workaround for CLion not supporting multiple cmake projects in one project
cmake_minimum_required(VERSION 3.12)

project(tpde2)

add_subdirectory(tpde)
