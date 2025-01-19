// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/base.hpp"

namespace tpde {

[[noreturn]] void fatal_error(const char *msg) noexcept {
  TPDE_LOG_ERR("TPDE FATAL ERROR: {}", msg);
  abort();
}

} // end namespace tpde
