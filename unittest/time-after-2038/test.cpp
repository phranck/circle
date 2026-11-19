// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using boolean = bool;
#define TRUE		true
#define FALSE		false
#define HZ		100
#define IPPROTO_UDP	17
#define MSG_DONTWAIT	0x40

enum TLogSeverity {LogPanic, LogError, LogWarning, LogNotice, LogDebug};

static char s_LastLog[200];

struct CLogger
{
	static CLogger *Get () { static CLogger Logger; return &Logger; }

	void Write (const char *, TLogSeverity, const char *pMessage, ...)
	{
		va_list Args;
		va_start (Args, pMessage);
		vsnprintf (s_LastLog, sizeof s_LastLog, pMessage, Args);
		va_end (Args);
	}
};

struct CSpinLock
{
	void Acquire () {}
	void Release () {}
};

typedef boolean TUpdateTimeHandler (unsigned nNewTime, unsigned nOldTime);

class CTimer
{
public:
	boolean SetTimeZone (int nMinutesDiff);
	int GetTimeZone (void) const;
	boolean SetTime (unsigned nTime, boolean bLocal = TRUE);
	unsigned GetTicks (void) const;
	unsigned GetUptime (void) const;
	boolean GetUptime (unsigned *pSeconds, unsigned *pMicroSeconds);
	unsigned GetTime (void) const;
	boolean GetLocalTime (unsigned *pSeconds, unsigned *pMicroSeconds);
	unsigned GetUniversalTime (void) const;
	boolean GetUniversalTime (unsigned *pSeconds, unsigned *pMicroSeconds);

private:
	TUpdateTimeHandler *m_pUpdateTimeHandler = 0;
	volatile unsigned m_nTicks = 0;
	volatile unsigned m_nUptime = 0;
	volatile unsigned m_nTime = 0;
	CSpinLock m_TimeSpinLock;
	int m_nMinutesDiff = 0;
};

#include "timer.inc"

struct CNetSubSystem {};

class CIPAddress
{
public:
	CIPAddress (const u8 *pAddress) { memcpy (m_Address, pAddress, sizeof m_Address); }
	const u8 *Get (void) const { return m_Address; }

private:
	u8 m_Address[4];
};

struct CScheduler
{
	static CScheduler *Get () { static CScheduler Scheduler; return &Scheduler; }
	void MsSleep (unsigned) {}
};

// the server's reply, as each case sets it
static u8 s_Reply[48];
static int s_nReplyLength;

class CSocket
{
public:
	CSocket (CNetSubSystem *, int) {}
	int Connect (CIPAddress &, u16) { return 0; }
	int Send (const void *, unsigned nLength, int) { return nLength; }
	int Receive (void *pBuffer, unsigned, int) { memcpy (pBuffer, s_Reply, s_nReplyLength); return s_nReplyLength; }
};

#include "ntpclient.h.inc"
#include "ntpclient.inc"

static const unsigned After2038 = 0x80000000U;
static const unsigned Era0Date = 3976214400U;		// 2026-01-01 in NTP seconds
static const unsigned Era0DateUnix = 1767225600U;

static unsigned Ask (unsigned nSeconds, unsigned nFraction = 0x80000000U, unsigned nLeap = 0,
		     unsigned nStratum = 2, unsigned nMode = 4, const char *pRefID = "\0\0\0\0",
		     int nLength = 48, unsigned nVersion = 4)
{
	memset (s_Reply, 0, sizeof s_Reply);
	s_Reply[0] = nLeap << 6 | nVersion << 3 | nMode;
	s_Reply[1] = nStratum;
	memcpy (s_Reply + 12, pRefID, 4);
	for (unsigned i = 0; i < 4; i++)
	{
		s_Reply[40 + i] = nSeconds >> (24 - 8 * i);
		s_Reply[44 + i] = nFraction >> (24 - 8 * i);
	}
	s_nReplyLength = nLength;
	s_LastLog[0] = '\0';

	CNetSubSystem Net;
	CNTPClient Client (&Net);
	static const u8 Server[4] = {192, 0, 2, 123};
	CIPAddress ServerIP (Server);
	return Client.GetTime (ServerIP);
}

int main ()
{
	CTimer Timer;
	unsigned nSeconds, nMicroSeconds;

	// UTC after 2038 and up to 2106
	assert (Timer.SetTime (After2038, FALSE));
	assert (Timer.GetUniversalTime () == After2038);
	assert (Timer.GetUniversalTime (&nSeconds, &nMicroSeconds) && nSeconds == After2038);
	assert (Timer.SetTime (0xFFFFFFFFU, FALSE));
	assert (Timer.GetUniversalTime () == 0xFFFFFFFFU);

	// one hour east of UTC
	assert (Timer.SetTimeZone (60));
	assert (Timer.SetTime (After2038, FALSE));
	assert (Timer.GetTime () == After2038 + 3600 && Timer.GetUniversalTime () == After2038);
	assert (Timer.SetTime (0xFFFFFFFFU - 3600, FALSE));
	assert (!Timer.SetTime (0xFFFFFFFFU - 3599, FALSE));		// local time would pass 2106
	assert (Timer.GetUniversalTime () == 0xFFFFFFFFU - 3600);

	// one hour west of UTC
	assert (Timer.SetTimeZone (-60));
	assert (Timer.SetTime (After2038, FALSE));
	assert (Timer.GetTime () == After2038 - 3600 && Timer.GetUniversalTime () == After2038);
	assert (!Timer.SetTime (1000, FALSE));				// local time would lie before 1970

	// NTP era 0, its last second, the rollover in 2036, and 2038
	assert (Ask (Era0Date) == Era0DateUnix);
	assert (Ask (0xFFFFFFFFU) == 2085978495U);
	assert (Ask (0, 1) == 2085978496U);
	assert (Ask (61505152U) == After2038);

	// replies without a usable time are refused
	assert (Ask (0, 0) == 0);					// Transmit Timestamp 0
	assert (Ask (Era0Date, 0x80000000U, 0, 2, 4, "\0\0\0\0", 44) == 0);	// no fraction
	assert (Ask (Era0Date, 0x80000000U, 0, 2, 4, "\0\0\0\0", 48, 0) == 0);	// version 0
	assert (strcmp (s_LastLog, "No server reply from 192.0.2.123 (version 0, mode 4)") == 0);
	assert (Ask (Era0Date, 0x80000000U, 0, 2, 3) == 0);		// a client's mode
	assert (Ask (Era0Date, 0x80000000U, 0, 0, 4, "RATE") == 0);	// kiss-o'-death
	assert (strcmp (s_LastLog, "Kiss code RATE from 192.0.2.123") == 0);
	assert (Ask (1000, 0x80000000U, 3) == 0);			// never synchronized, 1900
	assert (strcmp (s_LastLog, "192.0.2.123 is not synchronized (leap indicator 3, stratum 2)") == 0);
	assert (Ask (Era0Date, 0x80000000U, 0, 16) == 0);		// stratum 16
	assert (Ask (Era0Date, 0x80000000U, 0, 15) == Era0DateUnix);	// stratum 15 is synchronized
	assert (Ask (Era0Date, 0x80000000U, 1) == Era0DateUnix);	// a leap second ahead

	printf ("Time after 2038 passed\n");
}
