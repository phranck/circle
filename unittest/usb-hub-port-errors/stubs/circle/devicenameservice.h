// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file devicenameservice.h
 * @brief Host replacement for Circle's device name service, which keeps no names.
 */
#ifndef _circle_devicenameservice_h
#define _circle_devicenameservice_h

#include <circle/types.h>

/**
 * @brief Accepts device registrations and forgets them, because no test looks a name up.
 */
class CDeviceNameService
{
public:
	/**
	 * @brief Returns the one name service of the program.
	 * @return The name service, never null.
	 */
	static CDeviceNameService *Get (void)
	{
		static CDeviceNameService Service;
		return &Service;
	}

	/**
	 * @brief Accepts a device registration.
	 * @param pPrefix Prefix of the device name, such as `uhub`.
	 * @param nIndex Number after the prefix.
	 * @param pDevice The device object.
	 * @param bBlockDevice Whether it is a block device.
	 */
	void AddDevice (const char *pPrefix, unsigned nIndex, void *pDevice, boolean bBlockDevice)
	{
		(void) pPrefix;
		(void) nIndex;
		(void) pDevice;
		(void) bBlockDevice;
	}

	/**
	 * @brief Accepts the removal of a registration.
	 * @param pPrefix Prefix of the device name.
	 * @param nIndex Number after the prefix.
	 * @param bBlockDevice Whether it is a block device.
	 */
	void RemoveDevice (const char *pPrefix, unsigned nIndex, boolean bBlockDevice)
	{
		(void) pPrefix;
		(void) nIndex;
		(void) bBlockDevice;
	}
};

#endif
