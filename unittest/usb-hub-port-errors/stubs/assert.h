// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file assert.h
 * @brief Host replacement for Circle's assert, which fails the running test case.
 */
#pragma once
#include <stdexcept>

/**
 * @brief Throws `std::runtime_error` with the failed expression as its message.
 *
 * Circle's own `assert()` halts the system, so a failed assertion in the driver fails the
 * test case that triggered it.
 *
 * @param expr The condition that must hold.
 */
#define assert(expr) ((expr) ? (void) 0 : throw std::runtime_error (#expr))
