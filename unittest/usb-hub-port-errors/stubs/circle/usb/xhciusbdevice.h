// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file xhciusbdevice.h
 * @brief Host replacement for Circle's xHCI USB device, which counts the devices that exist.
 */
#ifndef _circle_usb_xhciusbdevice_h
#define _circle_usb_xhciusbdevice_h

#include <circle/usb/usb.h>
#include <circle/usb/usbdevice.h>

class CXHCIDevice;
class CUSBStandardHub;

/**
 * @brief A device the hub creates for a connected port.
 */
class CXHCIUSBDevice : public CUSBDevice
{
public:
	/**
	 * @brief Creates the device found on a hub port.
	 * @param pHost The host controller, not used by the stub.
	 * @param Speed The speed the port reports.
	 * @param pHub The hub the device is connected to.
	 * @param nHubPortIndex The 0-based index of the hub port.
	 */
	CXHCIUSBDevice (CXHCIDevice *pHost, TUSBSpeed Speed, CUSBStandardHub *pHub,
			unsigned nHubPortIndex);

	/**
	 * @brief Destroys the device.
	 */
	~CXHCIUSBDevice (void) override;

	/// Copying would count one device twice.
	CXHCIUSBDevice (const CXHCIUSBDevice &) = delete;
	/// Copying would count one device twice.
	CXHCIUSBDevice &operator= (const CXHCIUSBDevice &) = delete;
	/// Moving would count one device twice.
	CXHCIUSBDevice (CXHCIUSBDevice &&) = delete;
	/// Moving would count one device twice.
	CXHCIUSBDevice &operator= (CXHCIUSBDevice &&) = delete;

	/**
	 * @brief Returns how many devices exist at the moment.
	 * @return Devices created and not yet destroyed.
	 */
	static unsigned GetCount (void);

private:
	/// Devices created and not yet destroyed.
	static unsigned s_nCount;
};

#endif
