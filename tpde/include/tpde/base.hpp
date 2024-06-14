// SPDX-FileCopyrightText: 2024 Tobias Schwarz <tobias.schwarz@tum.de>
//
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <cstdint>

// TODO: basic stuff like asserts etc...

#ifdef TPDE_ASSERTS
    // make sure this always works even if NDEBUG is set
    #ifdef NDEBUG
        #undef NDEBUG
        #include <cassert>
        #define NDEBUG
    #else
        #include <cassert>
    #endif
#else
    #define assert(x)
#endif

#ifdef TPDE_LOGGING
    #include <spdlog/spdlog.h>
    #define TPDE_LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
    #define TPDE_LOG_DBG(...)   spdlog::debug(__VA_ARGS__)
    #define TPDE_LOG_INFO(...)  spdlog::info(__VA_ARGS__)
    #define TPDE_LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
    #define TPDE_LOG_ERR(...)   spdlog::error(__VA_ARGS__)
#else
    #define TPDE_LOG_TRACE(...)
    #define TPDE_LOG_DBG(...)
    #define TPDE_LOG_INFO(...)
    #define TPDE_LOG_WARN(...)
    #define TPDE_LOG_ERR(...)
#endif


namespace tpde {
// NOTE(ts): someone's gonna hate me...
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
} // namespace tpde
