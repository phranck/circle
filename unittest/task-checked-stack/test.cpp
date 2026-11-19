// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

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
#define STDLIB_SUPPORT	1		// the heap halts the system, when it takes its reserve
#define DATA_CACHE_LINE_LENGTH_MAX	64
#define ASSERT_STATIC(expr)		static_assert (expr, #expr)
#define HEAP_BLOCK_BUCKET_SIZES		0x40,0x400,0x1000,0x4000,0x10000,0x40000,0x80000
#define HEAP_LOW			0
#define HEAP_DEFAULT_NEW		HEAP_LOW
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

struct CSpinLock
{
	void Acquire () {}
	void Release () {}
};

#include "heapallocator.h.inc"
#include "heapallocator.cpp.inc"

// the heap the tasks take their stacks from
static CHeapAllocator *s_pHeap = 0;

struct CMemorySystem
{
	static void *HeapAllocate (size_t nSize, int nType, boolean bMayFail = FALSE)
	{
		assert (nType == HEAP_DEFAULT_NEW);
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
	assert (Panics ([] { s_pHeap->Allocate (TASK_STACK_SIZE); }));
	assert (strstr (pLogger->m_Last, "Out of memory") != 0);

	// a checked task without room for its stack is invalid, stays out of the scheduler,
	// and logs nothing, also where the heap keeps a reserve
	SetupHeap (Reserve);
	Fill ();
	nWritten = pLogger->m_nWritten;
	unsigned nAdded = pScheduler->m_nAdded;
	CCheckedTask *pChecked = 0;
	assert (!Panics ([&] { pChecked = new CCheckedTask; }));
	assert (!pChecked->IsValid ());
	assert (pScheduler->m_nAdded == nAdded);
	assert (pLogger->m_nWritten == nWritten);
	delete pChecked;

	// a checked task with room is valid, joins the scheduler, and gives its stack back
	SetupHeap (Reserve);
	pChecked = new CCheckedTask;
	assert (pChecked->IsValid ());
	assert (pScheduler->m_nAdded == nAdded + 1);
	void *pStack = (void *) (pChecked->GetStack ().Top - TASK_STACK_SIZE);
	CScheduler::Finish (pChecked);
	delete pChecked;
	assert (s_pHeap->Allocate (TASK_STACK_SIZE) == pStack);

	// a task of the other constructor without room for its stack is a panic,
	// which names the caller, also where the heap keeps no reserve
	SetupHeap (0);
	Fill ();
	assert (Panics ([] { delete new CPlainTask; }));
	assert (strstr (pLogger->m_Last, "No stack for new task (caller 0x") != 0);

	// with a reserve, the heap halts the system first, as before
	SetupHeap (Reserve);
	Fill ();
	assert (Panics ([] { delete new CPlainTask; }));
	assert (strstr (pLogger->m_Last, "Out of memory") != 0);

	delete s_pHeap;

	printf ("Task checked stack passed\n");
}
