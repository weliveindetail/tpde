// SPDX-License-Identifier: LicenseRef-Proprietary

#include "tpde/base.hpp"

#include <cstdlib>

namespace tpde {

[[noreturn]] void fatal_error([[maybe_unused]] const char *msg) noexcept {
  TPDE_LOG_ERR("TPDE FATAL ERROR: {}", msg);
  abort();
}

} // end namespace tpde
