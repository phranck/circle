// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file stubs.cpp
 * @brief Host implementations of the Circle classes the hub driver calls.
 */
#include <circle/usb/usbstandardhub.h>
#include <circle/usb/usbfunction.h>
#include <circle/usb/usbhostcontroller.h>
#include <circle/usb/usbrequest.h>
#include <circle/usb/xhciusbdevice.h>
#include <circle/logger.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

/// Size of the buffer a log message is formatted into, longer messages are cut.
static const size_t LogMessageSize = 256;

/// Bits of `bmRequestType` that name the recipient, USB 2.0 spec table 9-2.
static const u8 RequestRecipientMask = 0x1F;

/// Value of `CUSBRequest::GetStatus()` after a successful transfer.
static const int RequestSucceeded = 1;

/// Endpoint address of the status change endpoint: IN, endpoint 1.
static const u8 StatusChangeEndpointAddress = 0x81;

/// `bmAttributes` of the status change endpoint: interrupt transfers.
static const u8 InterruptEndpointAttributes = 0x03;

/// Maximum packet size of the status change endpoint, one byte for up to 7 ports.
static const u16 StatusChangePacketSize = 1;

/// Polling interval of the status change endpoint, the maximum for high speed.
static const u8 StatusChangeInterval = 12;

/// Return value of a request that failed, as `ControlMessage()` reports a transaction error.
static const int RequestFailed = -1;

/// Bits in a byte, the shift from the low to the high byte of the status change bitmap.
static const unsigned BitsPerByte = 8;

/// Mask of the low byte of the status change bitmap, ports 1 to 7 and the hub.
static const unsigned LowByteMask = 0xFF;

/// Bytes of the status change bitmap the model sends, enough for `USB_HUB_MAX_PORTS`.
static const size_t BitmapBytes = 2;

/**
 * @brief Returns a 16-bit field with the given bits set.
 * @param usField The field, such as `wPortStatus`.
 * @param nMask The bits to set, a combination of the `*__MASK` values.
 * @return The field with the bits set.
 */
static u16 WithBits (u16 usField, unsigned nMask)
{
	return static_cast<u16> (static_cast<unsigned> (usField) | nMask);
}

/**
 * @brief Returns a 16-bit field with the given bits cleared.
 * @param usField The field, such as `wChangeStatus`.
 * @param nMask The bits to clear, a combination of the `*__MASK` values.
 * @return The field with the bits cleared.
 */
static u16 WithoutBits (u16 usField, unsigned nMask)
{
	return static_cast<u16> (static_cast<unsigned> (usField) & ~nMask);
}

/**
 * @brief Formats a log message.
 * @param pMessage `printf` format of the message.
 * @param Args The arguments of the format.
 * @return The message, at most `LogMessageSize` - 1 characters, or a note that the format
 *	   could not be applied.
 */
static std::string FormatMessage (const char *pMessage, va_list Args)
	__attribute__ ((format (printf, 1, 0)));

static std::string FormatMessage (const char *pMessage, va_list Args)
{
	std::array<char, LogMessageSize> Buffer {};
	if (vsnprintf (Buffer.data (), Buffer.size (), pMessage, Args) < 0)
	{
		return std::string ("(format error) ") + pMessage;
	}

	return Buffer.data ();
}

CLogger *CLogger::Get (void)
{
	static CLogger Logger;
	return &Logger;
}

// NOLINTNEXTLINE(cert-dcl50-cpp): Circle's CLogger::Write is variadic.
void CLogger::Write (const char *pSource, TLogSeverity Severity, const char *pMessage, ...)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-init-variables)
	va_list Args;	// Set by va_start, for Circle's variadic interface.
	va_start (Args, pMessage);
	std::string Message = FormatMessage (pMessage, Args);
	va_end (Args);

	m_Entries.push_back ({pSource, Severity, Message});

	if (Severity == LogPanic)
	{
		throw std::runtime_error ("system halted: " + Message);
	}
}

const std::vector<TLogEntry> &CLogger::GetEntries (void) const
{
	return m_Entries;
}

void CLogger::Clear (void)
{
	m_Entries.clear ();
}

CUSBRequest::CUSBRequest (CUSBEndpoint *pEndpoint, void *pBuffer, u32 nBufLen)
:	m_pBuffer (static_cast<u8 *> (pBuffer)),
	m_nBufLen (nBufLen)
{
	(void) pEndpoint;
}

int CUSBRequest::GetStatus (void) const
{
	return m_nStatus;
}

u32 CUSBRequest::GetResultLength (void) const
{
	return m_nResultLength;
}

TUSBError CUSBRequest::GetUSBError (void) const
{
	return USBErrorUnknown;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Circle's CUSBRequest interface.
void CUSBRequest::SetCompletionRoutine (TURBCompletionRoutine *pRoutine, void *pParam,
					void *pContext)
{
	m_pCompletionRoutine = pRoutine;
	m_pCompletionParam = pParam;
	m_pCompletionContext = pContext;
}

void CUSBRequest::Complete (const u8 *pData, u32 nLength)
{
	if (m_pCompletionRoutine == nullptr)
	{
		throw std::logic_error ("request completed without a completion routine");
	}

	m_nResultLength = nLength < m_nBufLen ? nLength : m_nBufLen;
	memcpy (m_pBuffer, pData, m_nResultLength);
	m_nStatus = RequestSucceeded;

	// The routine deletes this request, so nothing of it is used afterwards.
	(*m_pCompletionRoutine) (this, m_pCompletionParam, m_pCompletionContext);
}

CUSBDevice::CUSBDevice (const char *pName)
:	m_Name (pName)
{
}

CUSBDevice::~CUSBDevice (void)
{
}

boolean CUSBDevice::Initialize (void)
{
	return TRUE;
}

boolean CUSBDevice::Configure (void)
{
	return TRUE;
}

boolean CUSBDevice::ReScanDevices (void)
{
	return TRUE;
}

boolean CUSBDevice::EnableHubFunction (void)
{
	return TRUE;
}

// NOLINTNEXTLINE(cert-dcl50-cpp): Circle's CUSBDevice::LogWrite is variadic.
void CUSBDevice::LogWrite (TLogSeverity Severity, const char *pMessage, ...)
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,cppcoreguidelines-init-variables)
	va_list Args;	// Set by va_start, for Circle's variadic interface.
	va_start (Args, pMessage);
	std::string Message = FormatMessage (pMessage, Args);
	va_end (Args);

	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Circle's logger interface is variadic.
	CLogger::Get ()->Write (m_Name.c_str (), Severity, "%s", Message.c_str ());
}

unsigned CXHCIUSBDevice::s_nCount = 0;

CXHCIUSBDevice::CXHCIUSBDevice (CXHCIDevice *pHost, TUSBSpeed Speed, CUSBStandardHub *pHub,
				unsigned nHubPortIndex)
:	CUSBDevice (("usbdev1-" + std::to_string (nHubPortIndex + 1)).c_str ())
{
	(void) pHost;
	(void) Speed;
	(void) pHub;
	++s_nCount;
}

CXHCIUSBDevice::~CXHCIUSBDevice (void)
{
	--s_nCount;
}

unsigned CXHCIUSBDevice::GetCount (void)
{
	return s_nCount;
}

CUSBFunction::CUSBFunction (CUSBDevice *pDevice, CUSBHostController *pHost,
			    CUSBEndpoint *pEndpoint0)
:	m_pDevice (pDevice),
	m_pHost (pHost),
	m_pEndpoint0 (pEndpoint0)
{
	m_EndpointDesc.bLength = sizeof m_EndpointDesc;
	m_EndpointDesc.bDescriptorType = DESCRIPTOR_ENDPOINT;
	m_EndpointDesc.bEndpointAddress = StatusChangeEndpointAddress;
	m_EndpointDesc.bmAttributes = InterruptEndpointAttributes;
	m_EndpointDesc.wMaxPacketSize = StatusChangePacketSize;
	m_EndpointDesc.bInterval = StatusChangeInterval;
}

CUSBFunction::CUSBFunction (CUSBFunction *pFunction)
:	m_pDevice (pFunction->m_pDevice),
	m_pHost (pFunction->m_pHost),
	m_pEndpoint0 (pFunction->m_pEndpoint0),
	m_EndpointDesc (pFunction->m_EndpointDesc)
{
}

CUSBFunction::~CUSBFunction (void)
{
}

boolean CUSBFunction::Initialize (void)
{
	return TRUE;
}

boolean CUSBFunction::Configure (void)
{
	return TRUE;
}

boolean CUSBFunction::ReScanDevices (void)
{
	return TRUE;
}

CUSBDevice *CUSBFunction::GetDevice (void) const
{
	return m_pDevice;
}

CUSBEndpoint *CUSBFunction::GetEndpoint0 (void) const
{
	return m_pEndpoint0;
}

CUSBHostController *CUSBFunction::GetHost (void) const
{
	return m_pHost;
}

u8 CUSBFunction::GetNumEndpoints (void) const
{
	return 1;
}

const void *CUSBFunction::GetDescriptor (u8 ucType) const
{
	return ucType == DESCRIPTOR_ENDPOINT ? &m_EndpointDesc : nullptr;
}

void CUSBFunction::ConfigurationError (const char *pSource) const
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Circle's logger interface is variadic.
	CLogger::Get ()->Write (pSource, LogError, "Invalid configuration");
}

u8 CUSBFunction::GetInterfaceProtocol (void) const
{
	return 0;
}

CUSBHostController::CUSBHostController (unsigned nPorts)
:	m_nPorts (nPorts)
{
	if (nPorts == 0 || nPorts > USB_HUB_MAX_PORTS)
	{
		throw std::invalid_argument ("hub model needs 1 to USB_HUB_MAX_PORTS ports");
	}
}

CUSBHostController::~CUSBHostController (void)
{
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): Circle's interface.
int CUSBHostController::GetDescriptor (CUSBEndpoint *pEndpoint,
				       unsigned char ucType, unsigned char ucIndex,
				       void *pBuffer, unsigned nBufSize,
				       unsigned char ucRequestType, unsigned short wIndex)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
	(void) pEndpoint;
	(void) ucIndex;
	(void) ucRequestType;
	(void) wIndex;

	TUSBHubDescriptor Desc {};
	if (ucType != DESCRIPTOR_HUB || nBufSize < sizeof Desc)
	{
		return RequestFailed;
	}

	Desc.bDescLength = sizeof Desc;
	Desc.bDescriptorType = DESCRIPTOR_HUB;
	Desc.bNbrPorts = static_cast<u8> (m_nPorts);
	memcpy (pBuffer, &Desc, sizeof Desc);

	return static_cast<int> (sizeof Desc);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): Circle's interface.
int CUSBHostController::ControlMessage (CUSBEndpoint *pEndpoint,
					u8 ucRequestType, u8 ucRequest, u16 usValue, u16 usIndex,
					void *pData, u16 usDataSize)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
	(void) pEndpoint;

	for (auto Failing = m_Failing.begin (); Failing != m_Failing.end (); ++Failing)
	{
		if (   Failing->ucRequest == ucRequest
		    && Failing->usValue == usValue
		    && Failing->usIndex == usIndex)
		{
			m_Failing.erase (Failing);

			return RequestFailed;
		}
	}

	if ((ucRequestType & RequestRecipientMask) != REQUEST_TO_OTHER)
	{
		// The only hub request the driver sends is GET_STATUS of the hub, which reports
		// neither local power loss nor over-current.
		TUSBHubStatus Status {};
		if (ucRequest != GET_STATUS || usDataSize < sizeof Status)
		{
			return RequestFailed;
		}

		memcpy (pData, &Status, sizeof Status);

		return static_cast<int> (sizeof Status);
	}

	if (usIndex == 0 || usIndex > m_nPorts)
	{
		return RequestFailed;
	}

	TUSBPortStatus &Port = GetPort (usIndex);

	if (ucRequest == GET_STATUS)
	{
		if (usDataSize < sizeof Port)
		{
			return RequestFailed;
		}

		memcpy (pData, &Port, sizeof Port);

		return static_cast<int> (sizeof Port);
	}

	if (ucRequest == SET_FEATURE)
	{
		return SetFeature (Port, usValue);
	}

	if (ucRequest == CLEAR_FEATURE)
	{
		return ClearFeature (Port, usValue);
	}

	return RequestFailed;
}

int CUSBHostController::SetFeature (TUSBPortStatus &Port, u16 usFeature)
{
	switch (usFeature)
	{
	case PORT_POWER:
		Port.wPortStatus = WithBits (Port.wPortStatus, PORT_POWER__MASK);
		return 0;

	case PORT_RESET:
		Port.wPortStatus = WithBits (Port.wPortStatus, PORT_ENABLE__MASK);
		Port.wChangeStatus = WithBits (Port.wChangeStatus, C_PORT_RESET__MASK);
		return 0;

	default:
		return RequestFailed;
	}
}

int CUSBHostController::ClearFeature (TUSBPortStatus &Port, u16 usFeature)
{
	switch (usFeature)
	{
	case PORT_ENABLE:
		Port.wPortStatus = WithoutBits (Port.wPortStatus, PORT_ENABLE__MASK);
		return 0;

	case PORT_POWER:
		Port.wPortStatus = WithoutBits (Port.wPortStatus, PORT_POWER__MASK);
		return 0;

	case C_PORT_CONNECTION:
		Port.wChangeStatus = WithoutBits (Port.wChangeStatus, C_PORT_CONNECTION__MASK);
		return 0;

	case C_PORT_ENABLE:
		Port.wChangeStatus = WithoutBits (Port.wChangeStatus, C_PORT_ENABLE__MASK);
		return 0;

	case C_PORT_RESET:
		Port.wChangeStatus = WithoutBits (Port.wChangeStatus, C_PORT_RESET__MASK);
		return 0;

	default:
		return RequestFailed;
	}
}

boolean CUSBHostController::SubmitAsyncRequest (CUSBRequest *pURB)
{
	std::unique_ptr<CUSBRequest> pRequest (pURB);
	if (m_pPendingURB != nullptr)
	{
		throw std::logic_error ("a second status change request was submitted");
	}

	m_pPendingURB = std::move (pRequest);

	return TRUE;
}

boolean CUSBHostController::IsPlugAndPlay (void) const
{
	return TRUE;
}

boolean CUSBHostController::UpdatePlugAndPlay (void)
{
	while (!m_ChangedHubs.empty ())
	{
		CUSBStandardHub *pHub = m_ChangedHubs.front ();
		m_ChangedHubs.erase (m_ChangedHubs.begin ());

		pHub->HandlePortStatusChange ();
	}

	return TRUE;
}

void CUSBHostController::PortStatusChanged (CUSBStandardHub *pHub)
{
	m_ChangedHubs.push_back (pHub);
}

void CUSBHostController::Connect (unsigned nPort)
{
	TUSBPortStatus &Port = GetPort (nPort);
	Port.wPortStatus = WithBits (Port.wPortStatus, PORT_CONNECTION__MASK);
	Port.wChangeStatus = WithBits (Port.wChangeStatus, C_PORT_CONNECTION__MASK);
}

void CUSBHostController::Disconnect (unsigned nPort)
{
	TUSBPortStatus &Port = GetPort (nPort);
	Port.wPortStatus = WithoutBits (Port.wPortStatus, PORT_CONNECTION__MASK);
	Port.wPortStatus = WithoutBits (Port.wPortStatus, PORT_ENABLE__MASK);
	Port.wChangeStatus = WithBits (Port.wChangeStatus, C_PORT_CONNECTION__MASK);
}

void CUSBHostController::SetChange (unsigned nPort, u16 usChangeStatus)
{
	GetPort (nPort).wChangeStatus = usChangeStatus;
}

u16 CUSBHostController::GetChange (unsigned nPort) const
{
	CheckPort (nPort);

	return m_Port.at (nPort - 1).wChangeStatus;
}

void CUSBHostController::FailOnce (u8 ucRequest, u16 usValue, u16 usIndex)
{
	m_Failing.push_back ({ucRequest, usValue, usIndex});
}

void CUSBHostController::ReportStatusChange (u16 usBitmap)
{
	if (m_pPendingURB == nullptr)
	{
		throw std::logic_error ("the hub has no status change request pending");
	}

	const std::array<u8, BitmapBytes> Bitmap {static_cast<u8> (usBitmap & LowByteMask),
						  static_cast<u8> (usBitmap >> BitsPerByte)};

	// The hub's completion routine deletes the request.
	CUSBRequest *pURB = m_pPendingURB.release ();
	pURB->Complete (Bitmap.data (), BitmapBytes);
}

boolean CUSBHostController::HasPendingRequest (void) const
{
	return m_pPendingURB != nullptr;
}

TUSBPortStatus &CUSBHostController::GetPort (unsigned nPort)
{
	CheckPort (nPort);

	return m_Port.at (nPort - 1);
}

void CUSBHostController::CheckPort (unsigned nPort) const
{
	if (nPort == 0 || nPort > m_nPorts)
	{
		throw std::out_of_range ("port outside the hub model");
	}
}
