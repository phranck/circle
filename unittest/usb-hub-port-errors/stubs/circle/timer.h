// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file timer.h
 * @brief Host replacement for Circle's timer, whose delays return at once.
 */
#ifndef _circle_timer_h
#define _circle_timer_h

/**
 * @brief Timer whose delays take no time, because the modeled hub needs none.
 */
class CTimer
{
public:
	/**
	 * @brief Returns the one timer of the program.
	 * @return The timer, never null.
	 */
	static CTimer *Get (void)
	{
		static CTimer Timer;
		return &Timer;
	}

	/**
	 * @brief Returns at once.
	 * @param nMilliSeconds The delay the driver asks for, ignored.
	 */
	void MsDelay (unsigned nMilliSeconds)
	{
		(void) nMilliSeconds;
	}
};

#endif
