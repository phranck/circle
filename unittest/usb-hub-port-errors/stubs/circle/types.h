// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file types.h
 * @brief Host replacement for Circle's basic types, with the widths they have on the target.
 */
#ifndef _circle_types_h
#define _circle_types_h

#include <cstddef>
#include <cstdint>

/// @brief Unsigned 8-bit integer.
typedef uint8_t u8;
/// @brief Unsigned 16-bit integer.
typedef uint16_t u16;
/// @brief Unsigned 32-bit integer.
typedef uint32_t u32;
/// @brief Unsigned 64-bit integer.
typedef uint64_t u64;
/// @brief Circle's boolean type, either `TRUE` or `FALSE`.
typedef bool boolean;

/// @brief The true value of `boolean`.
#define TRUE	true
/// @brief The false value of `boolean`.
#define FALSE	false

#endif
