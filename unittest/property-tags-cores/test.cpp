// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using uintptr = unsigned long;
using boolean = bool;
#define TRUE		true
#define FALSE		false
#define PACKED		__attribute__ ((packed))
#define CORES		4
#define TASK_LEVEL	0
#define IRQ_LEVEL	1
#define FIQ_LEVEL	2
#define BCM_MAILBOX_PROP_OUT		8
#define COHERENT_SLOT_PROP_MAILBOX	0
#define BUS_ADDRESS(addr)		((u32) (addr))

enum TLogSeverity {LogPanic, LogError, LogWarning, LogNotice, LogDebug};

static const u32 Temperature = 61000;
static const u32 ArmClock = 2400000000;

// each thread stands for a core, which runs at a level
static thread_local unsigned s_nThisCore = 0;
static thread_local unsigned s_nLevel = TASK_LEVEL;

// called at each compiler barrier and after the lock is taken, to run an
// interrupt handler at that point
static thread_local void (*s_pBarrierHook) () = 0;
static thread_local void (*s_pLockHook) () = 0;

inline unsigned CurrentExecutionLevel () { return s_nLevel; }
inline void EnterCritical (unsigned = IRQ_LEVEL) {}
inline void LeaveCritical () {}
inline void CompilerBarrier ()
{
	std::atomic_signal_fence (std::memory_order_seq_cst);
	if (s_pBarrierHook != 0)
	{
		s_pBarrierHook ();
	}
}
inline void DataSyncBarrier () { std::atomic_thread_fence (std::memory_order_seq_cst); }
inline void DataMemBarrier () { std::atomic_thread_fence (std::memory_order_seq_cst); }
inline void DebugStackTrace (const uintptr *, const char *) {}

struct CMultiCoreSupport
{
	static unsigned ThisCore () { return s_nThisCore; }
};

struct CLogger
{
	static CLogger *Get () { static CLogger Logger; return &Logger; }

	void Write (const char *, TLogSeverity, const char *pMessage, ...)
	{
		assert (s_nLevel < FIQ_LEVEL);		// Circle's logger cannot be used there

		std::lock_guard<std::mutex> Guard (m_Mutex);
		va_list Args;
		va_start (Args, pMessage);
		vsnprintf (m_Last, sizeof m_Last, pMessage, Args);
		va_end (Args);
		m_nWritten++;
	}

	std::mutex m_Mutex;
	unsigned m_nWritten = 0;
	char m_Last[200] = "";
};

// the property buffer, one page that all cores share
alignas (64) static u8 s_Page[4096];

struct CMemorySystem
{
	static void *GetCoherentPage (unsigned) { return s_Page; }
};

// the firmware: WriteRead() can hold the first caller until a second caller
// arrives or waits for a lock, or run an interrupt handler on its thread
static struct
{
	std::mutex Mutex;
	std::condition_variable Arrived;
	unsigned nCallers = 0;
	bool bSecondWaits = false;
	bool bHoldFirst = false;
	bool bRefuseNext = false;
	unsigned nInterruptLevel = TASK_LEVEL;		// TASK_LEVEL for no interrupt
}
s_Firmware;

// a spin lock for TASK_LEVEL with ARM_ALLOW_MULTI_CORE, once the cores run
struct CSpinLock
{
	CSpinLock (unsigned) {}

	void Acquire ()
	{
		if (!m_Mutex.try_lock ())
		{
			{
				std::lock_guard<std::mutex> Guard (s_Firmware.Mutex);
				s_Firmware.bSecondWaits = true;
			}
			s_Firmware.Arrived.notify_all ();
			m_Mutex.lock ();
		}

		if (s_pLockHook != 0)
		{
			s_pLockHook ();
		}
	}

	void Release () { m_Mutex.unlock (); }

	std::mutex m_Mutex;
};

class CBcmMailBox
{
public:
	CBcmMailBox (unsigned, boolean = FALSE) {}
	u32 WriteRead (unsigned nData);
};

#include "header.inc"
#include "source.inc"

static u32 AskTemperature (unsigned nCore, unsigned nLevel, boolean bEarlyUse)
{
	s_nThisCore = nCore;
	s_nLevel = nLevel;
	CBcmPropertyTags Tags (bEarlyUse);
	TPropertyTagTemperature Tag;
	Tag.nTemperatureId = TEMPERATURE_ID;
	boolean bOK = Tags.GetTag (PROPTAG_GET_TEMPERATURE, &Tag, sizeof Tag, 4);
	s_nLevel = TASK_LEVEL;
	return bOK ? Tag.nValue : 0;
}

static u32 AskArmClock (unsigned nCore, unsigned nLevel, boolean bEarlyUse)
{
	unsigned nLevelBefore = s_nLevel;
	s_nThisCore = nCore;
	s_nLevel = nLevel;
	CBcmPropertyTags Tags (bEarlyUse);
	TPropertyTagClockRate Tag;
	Tag.nClockId = CLOCK_ID_ARM;
	boolean bOK = Tags.GetTag (PROPTAG_GET_CLOCK_RATE, &Tag, sizeof Tag, 4);
	s_nLevel = nLevelBefore;
	return bOK ? Tag.nRate : 0;
}

static u32 s_nInterruptAnswer;			// what the interrupt handler's call returned
static unsigned s_nInterruptMessages;		// what it logged

static void Interrupt (unsigned nLevel)
{
	unsigned nWritten = CLogger::Get ()->m_nWritten;
	s_nInterruptAnswer = AskArmClock (0, nLevel, FALSE);
	s_nInterruptMessages = CLogger::Get ()->m_nWritten - nWritten;
}

u32 CBcmMailBox::WriteRead (unsigned nData)
{
	std::unique_lock<std::mutex> Lock (s_Firmware.Mutex);
	unsigned nCaller = ++s_Firmware.nCallers;
	s_Firmware.Arrived.notify_all ();

	if (nCaller == 1 && s_Firmware.nInterruptLevel != TASK_LEVEL)
	{
		Lock.unlock ();
		Interrupt (s_Firmware.nInterruptLevel);
		Lock.lock ();
	}

	if (nCaller == 1 && s_Firmware.bHoldFirst)
	{
		s_Firmware.Arrived.wait_for (Lock, std::chrono::seconds (1),
					     [] { return s_Firmware.nCallers >= 2 || s_Firmware.bSecondWaits; });
	}

	// answer the request that is in the page now
	TPropertyBuffer *pBuffer = (TPropertyBuffer *) s_Page;
	TPropertyTag *pTag = (TPropertyTag *) pBuffer->Tags;
	u32 *pValue = (u32 *) (pTag + 1);
	pValue[1] = pTag->nTagId == PROPTAG_GET_TEMPERATURE ? Temperature : ArmClock;
	pTag->nValueLength = (u32) VALUE_LENGTH_RESPONSE | (u32) (2 * sizeof (u32));
	pBuffer->nCode = s_Firmware.bRefuseNext ? CODE_RESPONSE_FAILURE : CODE_RESPONSE_SUCCESS;
	s_Firmware.bRefuseNext = false;

	return nData;
}

static void Reset (bool bHoldFirst, bool bRefuseNext, unsigned nInterruptLevel)
{
	std::lock_guard<std::mutex> Guard (s_Firmware.Mutex);
	s_Firmware.nCallers = 0;
	s_Firmware.bSecondWaits = false;
	s_Firmware.bHoldFirst = bHoldFirst;
	s_Firmware.bRefuseNext = bRefuseNext;
	s_Firmware.nInterruptLevel = nInterruptLevel;
}

// a call that does not return within 10 s fails the test, also when it hangs
static u32 Within (std::future<u32> &Call)
{
	bool bReturned = Call.wait_for (std::chrono::seconds (10)) == std::future_status::ready;
	assert (bReturned);
	return Call.get ();
}

// the third compiler barrier of a request comes after it ended on its core;
// a second FIQ arrives there, once, and asks for itself
static thread_local unsigned s_nBarriers;
static void SecondFIQAfterTheRequest ()
{
	if (++s_nBarriers == 3)
	{
		s_pBarrierHook = 0;
		Interrupt (FIQ_LEVEL);
	}
}

// an IRQ arrives right after the request took the lock, once
static void IRQAfterTheLock ()
{
	s_pLockHook = 0;
	Interrupt (IRQ_LEVEL);
}

static u32 AskWithHooks (void (*pBarrierHook) (), void (*pLockHook) ())
{
	s_nBarriers = 0;
	s_pBarrierHook = pBarrierHook;
	s_pLockHook = pLockHook;
	u32 nResult = AskTemperature (0, TASK_LEVEL, FALSE);
	s_pBarrierHook = 0;
	s_pLockHook = 0;
	return nResult;
}

int main ()
{
#ifdef ARM_ALLOW_MULTI_CORE
	// two cores ask at the same time, with objects for normal and for early use
	for (boolean bEarlyUse : {FALSE, TRUE})
	{
		Reset (true, false, TASK_LEVEL);
		auto First = std::async (std::launch::async, AskTemperature, 0U, (unsigned) TASK_LEVEL, bEarlyUse);
		{
			std::unique_lock<std::mutex> Lock (s_Firmware.Mutex);
			s_Firmware.Arrived.wait_for (Lock, std::chrono::seconds (10),
						     [] { return s_Firmware.nCallers >= 1; });
		}
		auto Second = std::async (std::launch::async, AskArmClock, 1U, (unsigned) TASK_LEVEL, bEarlyUse);
		assert (Within (First) == Temperature);
		assert (Within (Second) == ArmClock);
	}
#endif

	// a request the firmware refuses leaves the buffer to the next caller
	Reset (false, true, TASK_LEVEL);
	auto Refused = std::async (std::launch::async, AskTemperature, 0U, (unsigned) TASK_LEVEL, FALSE);
	assert (Within (Refused) == 0);
	auto Next = std::async (std::launch::async, AskArmClock, 0U, (unsigned) TASK_LEVEL, FALSE);
	assert (Within (Next) == ArmClock);

	// a call at IRQ_LEVEL on a core in no request is answered and logs nothing
	Reset (false, false, TASK_LEVEL);
	unsigned nBefore = CLogger::Get ()->m_nWritten;
	auto AtIRQ = std::async (std::launch::async, AskTemperature, 0U, (unsigned) IRQ_LEVEL, FALSE);
	assert (Within (AtIRQ) == Temperature);
	assert (CLogger::Get ()->m_nWritten == nBefore);

	// an interrupt handler that asks while its core waits for an answer is
	// refused, and logged once: by its call at IRQ_LEVEL, and at FIQ_LEVEL
	// by the interrupted request once it is done
	for (unsigned nLevel : {IRQ_LEVEL, FIQ_LEVEL})
	{
		Reset (false, false, nLevel);
		nBefore = CLogger::Get ()->m_nWritten;
		auto Interrupted = std::async (std::launch::async, AskTemperature, 0U, (unsigned) TASK_LEVEL, FALSE);
		assert (Within (Interrupted) == Temperature);
		assert (s_nInterruptAnswer == 0);
		assert (s_Firmware.nCallers == 1);
		assert (s_nInterruptMessages == (nLevel == IRQ_LEVEL ? 1U : 0U));
		assert (CLogger::Get ()->m_nWritten == nBefore + 1);
		assert (strstr (CLogger::Get ()->m_Last, "refused") != 0);
	}

	// a second FIQ right after the refused one's request ended is answered,
	// and the refusal is still logged once, below FIQ_LEVEL
	Reset (false, false, FIQ_LEVEL);
	nBefore = CLogger::Get ()->m_nWritten;
	auto TwoFIQs = std::async (std::launch::async, AskWithHooks, SecondFIQAfterTheRequest, nullptr);
	assert (Within (TwoFIQs) == Temperature);
	assert (s_nInterruptAnswer == ArmClock);
	assert (CLogger::Get ()->m_nWritten == nBefore + 1);
	assert (strstr (CLogger::Get ()->m_Last, "FIQ_LEVEL") != 0);

	// an IRQ right after the request took the lock is refused, not left
	// waiting for that lock
	Reset (false, false, TASK_LEVEL);
	nBefore = CLogger::Get ()->m_nWritten;
	auto AfterLock = std::async (std::launch::async, AskWithHooks, nullptr, IRQAfterTheLock);
	assert (Within (AfterLock) == Temperature);
	assert (s_nInterruptAnswer == 0);
	assert (CLogger::Get ()->m_nWritten == nBefore + 1);

	printf ("Property tags passed\n");
}
