// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file usbhostcontroller.h
 * @brief Host replacement for Circle's USB host controller, with a hub model behind it.
 */
#ifndef _circle_usb_usbhostcontroller_h
#define _circle_usb_usbhostcontroller_h

#include <circle/usb/usb.h>
#include <circle/usb/usbhub.h>
#include <circle/usb/usbendpoint.h>
#include <circle/usb/usbrequest.h>
#include <circle/types.h>
#include <array>
#include <memory>
#include <vector>

class CUSBStandardHub;

/**
 * @brief A control request that the hub model fails, identified by its setup fields.
 */
struct TFailingRequest
{
	u8	ucRequest;	///< `bRequest`, such as `CLEAR_FEATURE`.
	u16	usValue;	///< `wValue`, the feature selector for feature requests.
	u16	usIndex;	///< `wIndex`, the 1-based port number for port requests.
};

/**
 * @brief Host controller that answers the hub driver's requests from a model of one hub.
 *
 * The model keeps the status and change bits of each port as the USB 2.0 spec section 11.24.2
 * describes them: `SET_FEATURE PORT_RESET` enables the port and sets `C_PORT_RESET`, and
 * `CLEAR_FEATURE` of a change feature clears its bit. A request named in `FailOnce()` fails
 * once with -1, as a transaction error does, and leaves the model unchanged.
 */
class CUSBHostController
{
public:
	/**
	 * @brief Creates a host controller with a hub of the given number of ports behind it.
	 * @param nPorts Ports of the hub, 1 to `USB_HUB_MAX_PORTS`.
	 */
	explicit CUSBHostController (unsigned nPorts);

	/**
	 * @brief Destroys the controller and a status change request still pending.
	 */
	~CUSBHostController (void);

	/**
	 * @brief Reads a descriptor of the hub.
	 * @param pEndpoint The hub's default control endpoint.
	 * @param ucType The descriptor type, of which only `DESCRIPTOR_HUB` is modeled.
	 * @param ucIndex The descriptor index.
	 * @param pBuffer The buffer that receives the descriptor.
	 * @param nBufSize Size of the buffer in bytes.
	 * @param ucRequestType `bmRequestType` of the request.
	 * @param wIndex `wIndex` of the request.
	 * @return Size of the hub descriptor, or -1 for any other descriptor or a buffer too small.
	 */
	int GetDescriptor (CUSBEndpoint *pEndpoint,
			   unsigned char ucType, unsigned char ucIndex,
			   void *pBuffer, unsigned nBufSize,
			   unsigned char ucRequestType = REQUEST_IN,
			   unsigned short wIndex = 0);

	/**
	 * @brief Answers a hub class request from the model.
	 * @param pEndpoint The hub's default control endpoint.
	 * @param ucRequestType `bmRequestType`, `REQUEST_TO_OTHER` for port requests.
	 * @param ucRequest `bRequest`: `GET_STATUS`, `SET_FEATURE` or `CLEAR_FEATURE`.
	 * @param usValue `wValue`, the feature selector.
	 * @param usIndex `wIndex`, the 1-based port number, or 0 for the hub.
	 * @param pData The buffer for the data stage.
	 * @param usDataSize Size of the data stage in bytes.
	 * @return Bytes transferred in the data stage, or -1 for a request named in `FailOnce()`,
	 *	   a port outside the hub, or a request the model does not know.
	 */
	int ControlMessage (CUSBEndpoint *pEndpoint,
			    u8 ucRequestType, u8 ucRequest, u16 usValue, u16 usIndex,
			    void *pData, u16 usDataSize);

	/**
	 * @brief Takes the hub's status change request until `ReportStatusChange()`.
	 * @param pURB The request, owned by the controller from then on.
	 * @return Always `TRUE`.
	 */
	boolean SubmitAsyncRequest (CUSBRequest *pURB);

	/**
	 * @brief Returns whether devices can be attached and removed while the system runs.
	 * @return Always `TRUE`.
	 */
	boolean IsPlugAndPlay (void) const;

	/**
	 * @brief Lets each hub that reported a status change handle it, as Circle's task level
	 *	  does.
	 * @return Always `TRUE`.
	 */
	boolean UpdatePlugAndPlay (void);

	/**
	 * @brief Notes that a hub reported a status change.
	 * @param pHub The hub.
	 */
	void PortStatusChanged (CUSBStandardHub *pHub);

	/**
	 * @brief Connects a device to a port, which sets the connection and its change bit.
	 * @param nPort The 1-based port number.
	 */
	void Connect (unsigned nPort);

	/**
	 * @brief Disconnects the device from a port, which disables it and sets its change bit.
	 * @param nPort The 1-based port number.
	 */
	void Disconnect (unsigned nPort);

	/**
	 * @brief Replaces the change bits of a port.
	 * @param nPort The 1-based port number.
	 * @param usChangeStatus The new `wChangeStatus`, a combination of `C_PORT_*__MASK` bits.
	 */
	void SetChange (unsigned nPort, u16 usChangeStatus);

	/**
	 * @brief Returns the change bits of a port.
	 * @param nPort The 1-based port number.
	 * @return The port's `wChangeStatus`.
	 */
	u16 GetChange (unsigned nPort) const;

	/**
	 * @brief Makes the next matching control request fail.
	 * @param ucRequest `bRequest` of the request.
	 * @param usValue `wValue` of the request.
	 * @param usIndex `wIndex` of the request.
	 */
	void FailOnce (u8 ucRequest, u16 usValue, u16 usIndex);

	/**
	 * @brief Completes the pending status change request with the given bitmap.
	 * @param usBitmap Bit 0 for the hub, bit n for port n.
	 */
	void ReportStatusChange (u16 usBitmap);

	/**
	 * @brief Returns whether a status change request is pending.
	 * @return `TRUE` while the hub waits for the next status change.
	 */
	boolean HasPendingRequest (void) const;

private:
	/**
	 * @brief Returns the model of a port.
	 * @param nPort The 1-based port number, 1 to `m_nPorts`.
	 * @return The port's status and change bits.
	 */
	TUSBPortStatus &GetPort (unsigned nPort);

	/**
	 * @brief Throws `std::out_of_range` for a port the hub does not have.
	 * @param nPort The 1-based port number.
	 */
	void CheckPort (unsigned nPort) const;

	/**
	 * @brief Answers `SET_FEATURE` for a port.
	 * @param Port The port's model.
	 * @param usFeature The feature selector, `PORT_POWER` or `PORT_RESET`.
	 * @return 0 for a modeled feature, -1 otherwise.
	 */
	static int SetFeature (TUSBPortStatus &Port, u16 usFeature);

	/**
	 * @brief Answers `CLEAR_FEATURE` for a port.
	 * @param Port The port's model.
	 * @param usFeature The feature selector: `PORT_ENABLE`, `PORT_POWER`,
	 *		    `C_PORT_CONNECTION`, `C_PORT_ENABLE` or `C_PORT_RESET`.
	 * @return 0 for a modeled feature, -1 otherwise.
	 */
	static int ClearFeature (TUSBPortStatus &Port, u16 usFeature);

private:
	/// Ports of the hub, 1 to `USB_HUB_MAX_PORTS`.
	unsigned m_nPorts {0};
	/// Status and change bits of each port; index 0 is port 1.
	std::array<TUSBPortStatus, USB_HUB_MAX_PORTS> m_Port {};
	/// Requests that fail once, in the order they were named.
	std::vector<TFailingRequest> m_Failing {};
	/// The hub's status change request, null when none is pending.
	std::unique_ptr<CUSBRequest> m_pPendingURB {};
	/// Hubs that reported a status change and have not handled it yet.
	std::vector<CUSBStandardHub *> m_ChangedHubs {};
};

#endif
