// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once

#include "IRAdaptor.hpp"

namespace tpde {

template <IRAdaptor Adaptor>
struct Analyzer {
    // TODO(ts): maybe rename this to ValueTracker or smth since it not only
    // does the analysis but also manages and stores the value assignments and
    // block order

    void reset() {}
};

} // namespace tpde
