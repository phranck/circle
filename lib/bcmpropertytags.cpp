//
// bcmpropertytags.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2019  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
#include <circle/bcmpropertytags.h>
#include <circle/util.h>
#include <circle/synchronize.h>
#include <circle/bcm2835.h>
#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/logger.h>
#include <circle/debug.h>
#include <circle/macros.h>
#include <assert.h>

struct TPropertyBuffer
{
	u32	nBufferSize;			// bytes
	u32	nCode;
	#define CODE_REQUEST		0x00000000
	#define CODE_RESPONSE_SUCCESS	0x80000000
	#define CODE_RESPONSE_FAILURE	0x80000001
	u8	Tags[0];
	// end tag follows
}
PACKED;

static const char FromPropertyTags[] = "proptags";

CSpinLock CBcmPropertyTags::s_SpinLock (TASK_LEVEL);

// s_bInRequest[CurrentCore ()] is TRUE while GetTags() works on a request on
// this core, from before it takes s_SpinLock until it has let it go.
// s_pRefusedFromFIQ[CurrentCore ()] is the return address of the first call
// from FIQ_LEVEL that GetTags() refused during that request, or 0.

#ifdef ARM_ALLOW_MULTI_CORE

static volatile boolean s_bInRequest[CORES] = {FALSE};
static const void * volatile s_pRefusedFromFIQ[CORES] = {0};

static unsigned CurrentCore (void)
{
	return CMultiCoreSupport::ThisCore ();
}

#else

static volatile boolean s_bInRequest[1] = {FALSE};
static const void * volatile s_pRefusedFromFIQ[1] = {0};

static unsigned CurrentCore (void)
{
	return 0;
}

#endif

CBcmPropertyTags::CBcmPropertyTags (boolean bEarlyUse)
:	m_MailBox (BCM_MAILBOX_PROP_OUT)
{
}

CBcmPropertyTags::~CBcmPropertyTags (void)
{
}

boolean CBcmPropertyTags::GetTag (u32 nTagId, void *pTag, unsigned nTagSize, unsigned nRequestParmSize)
{
	assert (pTag != 0);
	assert (nTagSize >= sizeof (TPropertyTagSimple));

	TPropertyTag *pHeader = (TPropertyTag *) pTag;
	pHeader->nTagId = nTagId;
	pHeader->nValueBufSize = nTagSize - sizeof (TPropertyTag);
	pHeader->nValueLength = nRequestParmSize & ~VALUE_LENGTH_RESPONSE;

	if (!GetTags (pTag, nTagSize, __builtin_return_address (0)))
	{
		return FALSE;
	}

	pHeader->nValueLength &= ~VALUE_LENGTH_RESPONSE;
	if (pHeader->nValueLength == 0)
	{
		return FALSE;
	}

	return TRUE;
}

boolean CBcmPropertyTags::GetTags (void *pTags, unsigned nTagsSize)
{
	return GetTags (pTags, nTagsSize, __builtin_return_address (0));
}

boolean CBcmPropertyTags::GetTags (void *pTags, unsigned nTagsSize, const void *pCaller)
{
	assert (pTags != 0);
	assert (nTagsSize >= sizeof (TPropertyTagSimple));
	unsigned nBufferSize = sizeof (TPropertyBuffer) + nTagsSize + sizeof (u32);
	assert ((nBufferSize & 3) == 0);

	// A call from an interrupt handler on a core that is in a request is
	// refused. Once that request holds s_SpinLock, the call would wait for ever
	// for it, and without ARM_ALLOW_MULTI_CORE it would overwrite the request
	// it interrupted. The note is set before the lock is taken, because a call
	// from an interrupt between taking the lock and setting the note would wait
	// for ever, so a call is also refused while its core waits for the lock.
	// The logger takes its spin lock at IRQ_LEVEL, so it cannot be used at
	// FIQ_LEVEL. A call refused there is noted, and the request it interrupted
	// logs it once it is done.
	unsigned nCore = CurrentCore ();
	if (s_bInRequest[nCore])
	{
		if (CurrentExecutionLevel () < FIQ_LEVEL)
		{
			CLogger::Get ()->Write (FromPropertyTags, LogError,
						"Call interrupting a request on core %u refused (return address 0x%lX)",
						nCore, (unsigned long) (uintptr) pCaller);

			DebugStackTrace ((const uintptr *) __builtin_frame_address (0), FromPropertyTags);
		}
		else if (s_pRefusedFromFIQ[nCore] == 0)
		{
			s_pRefusedFromFIQ[nCore] = pCaller;
		}

		return FALSE;
	}

	s_bInRequest[nCore] = TRUE;
	CompilerBarrier ();

	TPropertyBuffer *pBuffer =
		(TPropertyBuffer *) CMemorySystem::GetCoherentPage (COHERENT_SLOT_PROP_MAILBOX);

	// All cores share the buffer, so it is held from the request until the
	// response is copied. A spin lock for TASK_LEVEL does nothing before
	// CSpinLock::Enable(), which CMultiCoreSupport::Initialize() calls before it
	// starts the other cores, so the early calls of CMemorySystem take it too.
	// They come before the static constructors, when s_SpinLock is still zeroed
	// memory, which reads as a free lock for TASK_LEVEL.
	s_SpinLock.Acquire ();

	pBuffer->nBufferSize = nBufferSize;
	pBuffer->nCode = CODE_REQUEST;
	memcpy (pBuffer->Tags, pTags, nTagsSize);

	u32 *pEndTag = (u32 *) (pBuffer->Tags + nTagsSize);
	*pEndTag = PROPTAG_END;

	DataSyncBarrier ();

	boolean bResult = FALSE;

	u32 nBufferAddress = BUS_ADDRESS ((uintptr) pBuffer);
	if (m_MailBox.WriteRead (nBufferAddress) == nBufferAddress)
	{
		DataMemBarrier ();

		if (pBuffer->nCode == CODE_RESPONSE_SUCCESS)
		{
			memcpy (pTags, pBuffer->Tags, nTagsSize);

			bResult = TRUE;
		}
	}

	s_SpinLock.Release ();

	CompilerBarrier ();
	s_bInRequest[nCore] = FALSE;
	CompilerBarrier ();

	// No call is refused on this core from here on. A call refused at
	// FIQ_LEVEL is logged below FIQ_LEVEL, by the request it interrupted, which
	// runs there, or by a call that comes between that request's end and this
	// point. The note is taken with IRQs and FIQs disabled, so only one of them
	// logs it.
	if (CurrentExecutionLevel () < FIQ_LEVEL)
	{
		EnterCritical (FIQ_LEVEL);
		const void *pRefused = s_pRefusedFromFIQ[nCore];
		s_pRefusedFromFIQ[nCore] = 0;
		LeaveCritical ();

		if (pRefused != 0)
		{
			CLogger::Get ()->Write (FromPropertyTags, LogError,
						"Call from FIQ_LEVEL interrupting a request on core %u refused"
						" (return address 0x%lX)", nCore, (unsigned long) (uintptr) pRefused);
		}
	}

	return bResult;
}
