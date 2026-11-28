// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file usbdevice.h
 * @brief Host replacement for Circle's USB device, which logs under its position name.
 */
#ifndef _circle_usb_usbdevice_h
#define _circle_usb_usbdevice_h

#include <circle/logger.h>
#include <circle/types.h>
#include <string>

/**
 * @brief A USB device that initializes and configures without fail.
 */
class CUSBDevice
{
public:
	/**
	 * @brief Creates a device.
	 * @param pName The name it logs under, which Circle derives from its position in the
	 *		device tree, such as `usbdev1` for the device on root port 1.
	 */
	explicit CUSBDevice (const char *pName);

	/**
	 * @brief Destroys the device.
	 */
	virtual ~CUSBDevice (void);

	/**
	 * @brief Reads the device's descriptors.
	 * @return Always `TRUE`.
	 */
	virtual boolean Initialize (void);

	/**
	 * @brief Configures the device.
	 * @return Always `TRUE`.
	 */
	virtual boolean Configure (void);

	/**
	 * @brief Looks for devices behind this one.
	 * @return Always `TRUE`.
	 */
	virtual boolean ReScanDevices (void);

	/**
	 * @brief Tells the host controller that the device is a hub.
	 * @return Always `TRUE`.
	 */
	boolean EnableHubFunction (void);

	/**
	 * @brief Writes a message to the logger with the device's name as its source.
	 * @param Severity Severity of the message.
	 * @param pMessage `printf` format of the message, followed by its arguments.
	 */
	void LogWrite (TLogSeverity Severity, const char *pMessage, ...)
		__attribute__ ((format (printf, 3, 4)));

private:
	/// The name the device logs under.
	std::string m_Name {};
};

#endif
