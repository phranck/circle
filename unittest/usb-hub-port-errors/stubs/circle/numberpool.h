// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file numberpool.h
 * @brief Host replacement for Circle's number pool, which counts up.
 */
#ifndef _circle_numberpool_h
#define _circle_numberpool_h

#include <circle/types.h>

/**
 * @brief Hands out device numbers in increasing order and never reuses them.
 */
class CNumberPool
{
public:
	/**
	 * @brief Creates a pool that starts at the given number.
	 * @param nMin The first number handed out.
	 */
	explicit CNumberPool (unsigned nMin)
	:	m_nNext (nMin)
	{
	}

	/**
	 * @brief Returns the next number.
	 * @param bMustSucceed Whether Circle would halt when the pool is empty, ignored here.
	 * @param pFrom Name of the caller for Circle's log message, ignored here.
	 * @return The next number of the pool.
	 */
	unsigned AllocateNumber (boolean bMustSucceed, const char *pFrom)
	{
		(void) bMustSucceed;
		(void) pFrom;
		return m_nNext++;
	}

	/**
	 * @brief Takes a number back without reusing it.
	 * @param nNumber The number handed out before.
	 */
	void FreeNumber (unsigned nNumber)
	{
		(void) nNumber;
	}

private:
	/// The number the next `AllocateNumber()` returns.
	unsigned m_nNext {0};
};

#endif
