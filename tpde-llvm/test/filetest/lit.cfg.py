# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

import os
import lit.formats

from lit.llvm import llvm_config

config.name = 'TPDE-LLVM FileTests'
config.test_format = lit.formats.ShTest(True)

config.suffixes = ['.ll', '.cpp']

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.tpde_llvm_bin_dir, 'test/filetest');

# Tweak the PATH to include the tools dir and TPDE binaries.
llvm_config.with_environment('PATH', config.llvm_tools_dir, append_path=True)
llvm_config.with_environment('PATH', config.tpde_llvm_bin_dir, append_path=True)

# TODO(ts): arch config
