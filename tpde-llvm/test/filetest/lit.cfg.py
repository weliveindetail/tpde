# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

import os
import lit.formats

config.name = 'TPDE-LLVM FileTests'
config.test_format = lit.formats.ShTest(True)

config.suffixes = ['.ll']

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.tpde_llvm_bin_dir, 'test/filetest');

config.substitutions.append(('llvm-objdump', os.path.join(config.llvm_tools_dir, 'llvm-objdump')))
config.substitutions.append(('FileCheck', os.path.join(config.llvm_tools_dir, 'FileCheck')))
config.substitutions.append(('tpde_llvm', os.path.join(config.tpde_llvm_bin_dir, 'tpde_llvm')))

# TODO(ts): arch config