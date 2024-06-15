# SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
#
# SPDX-License-Identifier: LicenseRef-Proprietary

import lit.formats
import os

config.name = 'TPDE FileTests'
config.test_format = lit.formats.ShTest(True)

config.suffixes = ['.tir']

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.tpde_obj_root, 'test/filetest');

config.substitutions.append(("%tpde_test", os.path.join(config.tpde_obj_root, "tpde_test")))