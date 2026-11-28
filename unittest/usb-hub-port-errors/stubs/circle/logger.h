// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file logger.h
 * @brief Host replacement for Circle's logger, which records every message for the test.
 */
#ifndef _circle_logger_h
#define _circle_logger_h

#include <string>
#include <vector>

/**
 * @brief Severity of a log message, in the order of Circle's own `TLogSeverity`.
 */
enum TLogSeverity
{
	LogPanic,	///< Circle halts the system after writing the message.
	LogError,	///< An operation failed and the system goes on.
	LogWarning,	///< Something unexpected happened that the system can handle.
	LogNotice,	///< Normal operation worth noting.
	LogDebug	///< Detail for debugging.
};

/**
 * @brief One message the driver wrote.
 */
struct TLogEntry
{
	std::string	Source;		///< Name of the module or device that wrote it.
	TLogSeverity	Severity;	///< Its severity, `LogPanic` to `LogDebug`.
	std::string	Message;	///< The formatted text.
};

/**
 * @brief Records the driver's log messages and models the halt that follows `LogPanic`.
 */
class CLogger
{
public:
	/**
	 * @brief Returns the one logger of the program.
	 * @return The logger, never null.
	 */
	static CLogger *Get (void);

	/**
	 * @brief Records a message.
	 *
	 * With `LogPanic`, Circle's logger halts every core after writing the message. This one
	 * throws `std::runtime_error` instead, with "system halted: " before the message, so the
	 * running test case fails.
	 *
	 * @param pSource Name of the module or device writing the message.
	 * @param Severity Severity of the message.
	 * @param pMessage `printf` format of the message, followed by its arguments.
	 */
	void Write (const char *pSource, TLogSeverity Severity, const char *pMessage, ...)
		__attribute__ ((format (printf, 4, 5)));

	/**
	 * @brief Returns the messages recorded since the last `Clear()`.
	 * @return The messages in the order they were written.
	 */
	const std::vector<TLogEntry> &GetEntries (void) const;

	/**
	 * @brief Forgets every recorded message.
	 */
	void Clear (void);

private:
	/// Messages in the order they were written, empty after `Clear()`.
	std::vector<TLogEntry> m_Entries {};
};

#endif
