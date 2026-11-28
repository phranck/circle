// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file usbendpoint.h
 * @brief Host replacement for Circle's USB endpoint, which only stands for an endpoint.
 */
#ifndef _circle_usb_usbendpoint_h
#define _circle_usb_usbendpoint_h

#include <circle/usb/usb.h>

class CUSBDevice;

/**
 * @brief An endpoint that the modeled host controller accepts without looking at it.
 */
class CUSBEndpoint
{
public:
	/**
	 * @brief Creates the default control endpoint of a device.
	 */
	CUSBEndpoint (void)
	{
	}

	/**
	 * @brief Creates an endpoint from its descriptor.
	 * @param pDevice The device the endpoint belongs to.
	 * @param pDesc The endpoint descriptor of the interface.
	 */
	CUSBEndpoint (CUSBDevice *pDevice, const TUSBEndpointDescriptor *pDesc)
	{
		(void) pDevice;
		(void) pDesc;
	}
};

#endif
