// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file usbfunction.h
 * @brief Host replacement for Circle's USB function, describing one hub interface.
 */
#ifndef _circle_usb_usbfunction_h
#define _circle_usb_usbfunction_h

#include <circle/usb/usb.h>
#include <circle/usb/usbdevice.h>
#include <circle/usb/usbendpoint.h>
#include <circle/types.h>

class CUSBHostController;

/**
 * @brief The interface of a hub with one interrupt IN endpoint, as the USB 2.0 spec defines it.
 */
class CUSBFunction
{
public:
	/**
	 * @brief Creates the interface the device factory would hand to the hub driver.
	 * @param pDevice The hub device.
	 * @param pHost The host controller the hub is reached through.
	 * @param pEndpoint0 The hub's default control endpoint.
	 */
	CUSBFunction (CUSBDevice *pDevice, CUSBHostController *pHost, CUSBEndpoint *pEndpoint0);

	/**
	 * @brief Creates a function for the same interface, as a driver's constructor does.
	 * @param pFunction The function to take the device, host and endpoint from.
	 */
	explicit CUSBFunction (CUSBFunction *pFunction);

	/**
	 * @brief Destroys the function.
	 */
	virtual ~CUSBFunction (void);

	/**
	 * @brief Initializes the interface.
	 * @return Always `TRUE`.
	 */
	virtual boolean Initialize (void);

	/**
	 * @brief Selects the interface.
	 * @return Always `TRUE`.
	 */
	virtual boolean Configure (void);

	/**
	 * @brief Looks for devices behind the interface.
	 * @return Always `TRUE`.
	 */
	virtual boolean ReScanDevices (void);

	/**
	 * @brief Returns the device the interface belongs to.
	 * @return The hub device.
	 */
	CUSBDevice *GetDevice (void) const;

	/**
	 * @brief Returns the device's default control endpoint.
	 * @return The endpoint.
	 */
	CUSBEndpoint *GetEndpoint0 (void) const;

	/**
	 * @brief Returns the host controller the device is reached through.
	 * @return The host controller.
	 */
	CUSBHostController *GetHost (void) const;

	/**
	 * @brief Returns the number of endpoints of the interface.
	 * @return Always 1, the status change endpoint.
	 */
	u8 GetNumEndpoints (void) const;

	/**
	 * @brief Returns the next descriptor of the given type.
	 * @param ucType The descriptor type.
	 * @return The interrupt IN endpoint descriptor for `DESCRIPTOR_ENDPOINT`, null otherwise.
	 */
	const void *GetDescriptor (u8 ucType) const;

	/**
	 * @brief Reports an interface the driver cannot use, as an error in the log.
	 * @param pSource Name of the driver.
	 */
	void ConfigurationError (const char *pSource) const;

	/**
	 * @brief Returns the interface protocol.
	 * @return Always 0, a hub with a single transaction translator.
	 */
	u8 GetInterfaceProtocol (void) const;

private:
	/// The hub device.
	CUSBDevice *m_pDevice {nullptr};
	/// The host controller the hub is reached through.
	CUSBHostController *m_pHost {nullptr};
	/// The hub's default control endpoint.
	CUSBEndpoint *m_pEndpoint0 {nullptr};
	/// The status change endpoint: interrupt IN, endpoint 1.
	TUSBEndpointDescriptor m_EndpointDesc {};
};

#endif
