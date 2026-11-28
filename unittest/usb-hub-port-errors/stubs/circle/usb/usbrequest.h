// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file usbrequest.h
 * @brief Host replacement for Circle's USB request block, completed by the test.
 */
#ifndef _circle_usb_usbrequest_h
#define _circle_usb_usbrequest_h

#include <circle/usb/usb.h>
#include <circle/usb/usbendpoint.h>
#include <circle/types.h>

class CUSBRequest;

/**
 * @brief Function the request calls when it completes.
 * @param pURB The completed request, which the routine owns from then on.
 * @param pParam First value given to `SetCompletionRoutine()`.
 * @param pContext Second value given to `SetCompletionRoutine()`.
 */
typedef void TURBCompletionRoutine (CUSBRequest *pURB, void *pParam, void *pContext);

/**
 * @brief An asynchronous transfer that `Complete()` finishes the way a host controller would.
 */
class CUSBRequest
{
public:
	/**
	 * @brief Creates a request that receives into the given buffer.
	 * @param pEndpoint The endpoint the request is for.
	 * @param pBuffer The buffer that receives the data, owned by the caller.
	 * @param nBufLen Size of the buffer in bytes.
	 */
	CUSBRequest (CUSBEndpoint *pEndpoint, void *pBuffer, u32 nBufLen);

	/**
	 * @brief Returns whether the transfer succeeded.
	 * @return 1 after a successful `Complete()`, 0 before it.
	 */
	int GetStatus (void) const;

	/**
	 * @brief Returns the number of bytes received.
	 * @return Bytes copied into the buffer by `Complete()`, 0 before it.
	 */
	u32 GetResultLength (void) const;

	/**
	 * @brief Returns the error of a failed transfer.
	 * @return Always `USBErrorUnknown`, because `Complete()` only models success.
	 */
	TUSBError GetUSBError (void) const;

	/**
	 * @brief Sets the function called on completion.
	 * @param pRoutine The function.
	 * @param pParam First value passed to it.
	 * @param pContext Second value passed to it.
	 */
	void SetCompletionRoutine (TURBCompletionRoutine *pRoutine, void *pParam, void *pContext);

	/**
	 * @brief Completes the transfer successfully with the given data.
	 *
	 * Copies as much of the data as the buffer holds and calls the completion routine. The
	 * routine owns the request from then on, so the caller must not touch it afterwards.
	 *
	 * @param pData The data the device sent.
	 * @param nLength Size of the data in bytes.
	 */
	void Complete (const u8 *pData, u32 nLength);

private:
	/// Buffer that receives the data, owned by the creator of the request.
	u8 *m_pBuffer {nullptr};
	/// Size of `m_pBuffer` in bytes.
	u32 m_nBufLen {0};
	/// 1 once completed successfully, 0 before.
	int m_nStatus {0};
	/// Bytes received, 0 before completion.
	u32 m_nResultLength {0};
	/// Function called on completion, null until `SetCompletionRoutine()`.
	TURBCompletionRoutine *m_pCompletionRoutine {nullptr};
	/// First value passed to the completion routine.
	void *m_pCompletionParam {nullptr};
	/// Second value passed to the completion routine.
	void *m_pCompletionContext {nullptr};
};

#endif
