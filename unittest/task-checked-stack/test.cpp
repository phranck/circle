// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <new>

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;
using uintptr = unsigned long;
using boolean = bool;
#define TRUE		true
#define FALSE		false
#define PACKED		__attribute__ ((packed))
#define AARCH		64
// STDLIB_SUPPORT comes from run.py, which builds the test with 1 and with 3
#define DATA_CACHE_LINE_LENGTH_MAX	64
#define ASSERT_STATIC(expr)		static_assert (expr, #expr)
#define HEAP_BLOCK_BUCKET_SIZES		0x40,0x400,0x1000,0x4000,0x10000,0x40000,0x80000
#define HEAP_LOW			0
#define HEAP_ANY			2
#define HEAP_DEFAULT_NEW		HEAP_LOW
#define HEAP_DEFAULT_MALLOC		HEAP_ANY	// differs from HEAP_DEFAULT_NEW, so the test tells them apart
#define TASK_STACK_SIZE			0x8000
#define MEM_KERNEL_STACK		0x80000
#define KERNEL_STACK_SIZE		0x20000

enum TLogSeverity {LogPanic, LogError, LogWarning, LogNotice, LogDebug};

// a panic halts the system; here it ends the call that logged it
struct TPanic {};

struct CLogger
{
	static CLogger *Get () { static CLogger Logger; return &Logger; }

	void Write (const char *, TLogSeverity Severity, const char *pMessage, ...)
	{
		va_list Args;
		va_start (Args, pMessage);
		vsnprintf (m_Last, sizeof m_Last, pMessage, Args);
		va_end (Args);
		m_nWritten++;

		if (Severity == LogPanic)
		{
			throw TPanic ();
		}
	}

	void WriteNoAlloc (const char *pSource, TLogSeverity Severity, const char *pMessage)
	{
		Write (pSource, Severity, "%s", pMessage);
	}

	unsigned m_nWritten = 0;
	char m_Last[200] = "";
};

#define LOGMODULE(name)	static const char From[] = name
#define LOGPANIC(...)	CLogger::Get ()->Write (From, LogPanic, __VA_ARGS__)
#define LOGWARN(...)	CLogger::Get ()->Write (From, LogWarning, __VA_ARGS__)

struct CSpinLock
{
	void Acquire () {}
	void Release () {}
};

#include "heapallocator.h.inc"
#include "heapallocator.cpp.inc"

// the heap the tasks take their stacks from, whichever heap they ask for
static CHeapAllocator *s_pHeap = 0;

// the heap the last task asked for
static int s_nLastType = -1;

struct CMemorySystem
{
	static void *HeapAllocate (size_t nSize, int nType, boolean bMayFail = FALSE)
	{
		s_nLastType = nType;
		return s_pHeap->Allocate (nSize, bMayFail);
	}

	static void HeapFree (void *pBlock) { s_pHeap->Free (pBlock); }
};

struct TTaskRegisters {};
struct TStackInfo { uintptr Top; size_t Size; };
struct CSynchronizationEvent { void Set () {} void Wait () {} };

class CString
{
public:
	void Format (const char *, ...) {}
	CString &operator= (const char *) { return *this; }
	operator const char * () const { return ""; }
};

class CTask;

class CScheduler
{
public:
	static CScheduler *Get () { static CScheduler Scheduler; return &Scheduler; }
	void AddTask (CTask *) { m_nAdded++; }
	void Yield () {}
	boolean IsValidTask (CTask *) { return TRUE; }
	static void Finish (CTask *pTask);	// as a task's end does before it is deleted

	unsigned m_nAdded = 0;
};

#include "task.h.inc"
#include "task.cpp.inc"

void CTask::InitializeRegs (void) {}

void CScheduler::Finish (CTask *pTask)
{
	pTask->SetState (TaskStateTerminated);
}

class CCheckedTask : public CTask
{
public:
	CCheckedTask () : CTask (TASK_STACK_SIZE, FALSE, TaskCreationChecked) {}
};

class CPlainTask : public CTask
{
public:
	CPlainTask () : CTask (TASK_STACK_SIZE) {}
};

// the block a stack of TASK_STACK_SIZE takes from the bucket 0x10000
static const size_t BlockBytes = sizeof (THeapBlockHeader) + 0x10000;
static const size_t Reserve = 0x8000;
alignas (DATA_CACHE_LINE_LENGTH_MAX) static u8 s_Memory[2 * BlockBytes + Reserve];

// a new heap with room for two stacks, and a reserve behind them
static void SetupHeap (size_t nReserve)
{
	delete s_pHeap;
	s_pHeap = new CHeapAllocator ("test");
	s_pHeap->Setup ((uintptr) s_Memory, 2 * BlockBytes + nReserve, nReserve);
}

static void Fill (void)
{
	for (unsigned i = 0; i < 2; i++)
	{
		void *pBlock = s_pHeap->Allocate (TASK_STACK_SIZE);
		assert (pBlock != 0);
	}
}

template <typename TCall>
static bool Panics (TCall Call)
{
	try
	{
		Call ();
	}
	catch (const TPanic &)
	{
		return true;
	}

	return false;
}

// whether a call fails as a task without a stack fails: with a panic below
// STDLIB_SUPPORT 3, with std::bad_alloc, which a caller may catch, from 3 on
template <typename TCall>
static bool FailsWithoutStack (TCall Call)
{
	try
	{
		Call ();
	}
	catch (const TPanic &)
	{
		return STDLIB_SUPPORT < 3;
	}
	catch (const std::bad_alloc &)
	{
		return STDLIB_SUPPORT >= 3;
	}

	return false;
}

// whether a request, which cannot handle 0, takes the reserve and reports "Out of memory":
// below STDLIB_SUPPORT 3 the heap halts the system, from 3 on it logs a warning
static bool TakesReserve (void)
{
	CLogger *pLogger = CLogger::Get ();
	unsigned nWritten = pLogger->m_nWritten;
	bool bPanic = Panics ([] { s_pHeap->Allocate (TASK_STACK_SIZE); });

	return    bPanic == (STDLIB_SUPPORT < 3)
	       && pLogger->m_nWritten == nWritten + 1
	       && strstr (pLogger->m_Last, "Out of memory") != 0;
}

// the heap a task's stack comes from: the one operator new takes its blocks from
static const int StackHeap = STDLIB_SUPPORT >= 3 ? HEAP_DEFAULT_MALLOC : HEAP_DEFAULT_NEW;

int main ()
{
	CLogger *pLogger = CLogger::Get ();
	CScheduler *pScheduler = CScheduler::Get ();

	// a request that may fail returns 0 and leaves the reserve to a caller, who cannot handle 0
	SetupHeap (Reserve);
	Fill ();
	unsigned nWritten = pLogger->m_nWritten;
	assert (!Panics ([] { assert (s_pHeap->Allocate (TASK_STACK_SIZE, TRUE) == 0); }));
	assert (pLogger->m_nWritten == nWritten);
	assert (TakesReserve ());

	// a checked task without room for its stack is invalid, stays out of the scheduler,
	// logs nothing, and leaves the reserve
	SetupHeap (Reserve);
	Fill ();
	nWritten = pLogger->m_nWritten;
	unsigned nAdded = pScheduler->m_nAdded;
	CCheckedTask *pChecked = 0;
	assert (!Panics ([&] { pChecked = new CCheckedTask; }));
	assert (s_nLastType == StackHeap);
	assert (!pChecked->IsValid ());
	assert (pScheduler->m_nAdded == nAdded);
	assert (pLogger->m_nWritten == nWritten);
	delete pChecked;
	assert (TakesReserve ());

	// a checked task with room is valid, joins the scheduler, and gives its stack back
	SetupHeap (Reserve);
	pChecked = new CCheckedTask;
	assert (pChecked->IsValid ());
	assert (pScheduler->m_nAdded == nAdded + 1);
	void *pStack = (void *) (pChecked->GetStack ().Top - TASK_STACK_SIZE);
	CScheduler::Finish (pChecked);
	delete pChecked;
	assert (s_pHeap->Allocate (TASK_STACK_SIZE) == pStack);

	// a task of the other constructor without room for its stack names its caller, stays
	// out of the scheduler, and leaves the reserve, where the heap keeps one
	static const size_t Reserves[] = {0, Reserve};
	for (size_t nReserve : Reserves)
	{
		SetupHeap (nReserve);
		Fill ();
		nAdded = pScheduler->m_nAdded;
		s_nLastType = -1;
		assert (FailsWithoutStack ([] { delete new CPlainTask; }));
		assert (s_nLastType == StackHeap);
		assert (strstr (pLogger->m_Last, "No stack for new task (caller 0x") != 0);
		assert (strstr (pLogger->m_Last, "(caller 0x0)") == 0);
		assert (pScheduler->m_nAdded == nAdded);
		assert (nReserve == 0 || TakesReserve ());
	}

	delete s_pHeap;

	printf ("Task checked stack passed with STDLIB_SUPPORT %d\n", STDLIB_SUPPORT);
}
