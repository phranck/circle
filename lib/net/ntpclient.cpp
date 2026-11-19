//
// ntpclient.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2016  R. Stange <rsta2@o2online.de>
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
#include <circle/net/ntpclient.h>
#include <circle/net/socket.h>
#include <circle/net/in.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <circle/types.h>
#include <assert.h>

#define NTP_PACKET_SIZE		48

#define NTP_MODE_SERVER		4	// the mode of a server's reply to a client
#define NTP_LEAP_NOSYNC		3	// leap indicator: clock never synchronized
#define NTP_STRATUM_KISS	0	// stratum of a kiss-o'-death reply
#define NTP_MAXSTRAT		16	// stratum of an unsynchronized server
#define NTP_REFID_OFFSET	12	// reference identifier, the kiss code at stratum 0

#define SEVENTY_YEARS		2208988800U

static const char FromNTPClient[] = "ntp";

// a kiss code is ASCII, any other byte is logged as '?'
static char KissCodeChar (u8 uchByte)
{
	return ' ' <= uchByte && uchByte <= '~' ? (char) uchByte : '?';
}

CNTPClient::CNTPClient (CNetSubSystem *pNetSubSystem)
:	m_pNetSubSystem (pNetSubSystem)
{
	assert (m_pNetSubSystem != 0);
}

CNTPClient::~CNTPClient (void)
{
	m_pNetSubSystem = 0;
}

unsigned CNTPClient::GetTime (CIPAddress &rServerIP)
{
	assert (m_pNetSubSystem != 0);
	CSocket Socket (m_pNetSubSystem, IPPROTO_UDP);
	if (Socket.Connect (rServerIP, 123) != 0)
	{
		return 0;
	}

	u8 NTPPacket[NTP_PACKET_SIZE];
	memset (NTPPacket, 0, sizeof NTPPacket);
	NTPPacket[0] = 0xE3;			// leap indicator: unknown, version: 4, mode: client
	NTPPacket[1] = 0;			// stratum: unspecified
	NTPPacket[2] = 10;			// poll: 1024 seconds
	NTPPacket[3] = 0;			// precision: 1 second
	memcpy (NTPPacket+12, "XCIR", 4);

	unsigned char RecvPacket[NTP_PACKET_SIZE];

	unsigned nTry;
	for (nTry = 1; nTry <= 3; nTry++)
	{
		if (Socket.Send (NTPPacket, sizeof NTPPacket, 0) != sizeof NTPPacket)
		{
			CLogger::Get ()->Write (FromNTPClient, LogError, "Send failed");

			return 0;
		}

		CScheduler::Get ()->MsSleep (1000);

		int nResult = Socket.Receive (RecvPacket, sizeof RecvPacket, MSG_DONTWAIT);
		if (nResult < 0)
		{
			CLogger::Get ()->Write (FromNTPClient, LogError, "Receive failed");

			return 0;
		}

		if (nResult >= NTP_PACKET_SIZE)		// the Transmit Timestamp ends the packet
		{
			break;
		}
	}

	if (nTry > 3)
	{
		CLogger::Get ()->Write (FromNTPClient, LogError, "Invalid or no response");

		return 0;
	}

	// Every 32-bit value of the seconds maps to a Unix time below, so the
	// checks that follow keep a reply that holds no synchronized server's
	// time from setting the clock. RFC 4330 section 5, as corrected by erratum 2263,
	// discards a reply whose version or stratum is 0 or whose mode is not the
	// server's. A reply of stratum 0 carries a kiss code (RFC 5905 section
	// 7.4), such as "INIT" from a server that has not synchronized yet. RFC 5905
	// does not use the time of a server that has never been synchronized
	// (leap indicator 3) or whose stratum is MAXSTRAT or more (appendix
	// A.5.5.3, accept()).
	const u8 *pServer = rServerIP.Get ();
	unsigned nLeap = RecvPacket[0] >> 6;
	unsigned nVersion = (RecvPacket[0] >> 3) & 7;
	unsigned nMode = RecvPacket[0] & 7;
	unsigned nStratum = RecvPacket[1];

	if (   nVersion == 0
	    || nMode != NTP_MODE_SERVER)
	{
		CLogger::Get ()->Write (FromNTPClient, LogError,
					"No server reply from %u.%u.%u.%u (version %u, mode %u)",
					pServer[0], pServer[1], pServer[2], pServer[3],
					nVersion, nMode);

		return 0;
	}

	if (nStratum == NTP_STRATUM_KISS)
	{
		const u8 *pKissCode = RecvPacket + NTP_REFID_OFFSET;
		CLogger::Get ()->Write (FromNTPClient, LogError,
					"Kiss code %c%c%c%c from %u.%u.%u.%u",
					KissCodeChar (pKissCode[0]), KissCodeChar (pKissCode[1]),
					KissCodeChar (pKissCode[2]), KissCodeChar (pKissCode[3]),
					pServer[0], pServer[1], pServer[2], pServer[3]);

		return 0;
	}

	if (   nLeap == NTP_LEAP_NOSYNC
	    || nStratum >= NTP_MAXSTRAT)
	{
		CLogger::Get ()->Write (FromNTPClient, LogError,
					"%u.%u.%u.%u is not synchronized (leap indicator %u, stratum %u)",
					pServer[0], pServer[1], pServer[2], pServer[3],
					nLeap, nStratum);

		return 0;
	}

	// RFC 4330 section 5: a reply with a Transmit Timestamp of 0 is to be discarded;
	// seconds of 0 with a fraction are a valid time at the start of the 2036 era
	static const u8 NoTimestamp[8] = {0};
	if (memcmp (RecvPacket+40, NoTimestamp, sizeof NoTimestamp) == 0)
	{
		CLogger::Get ()->Write (FromNTPClient, LogError, "No transmit timestamp");

		return 0;
	}

	unsigned nSecondsSince1900;
	nSecondsSince1900  =   (unsigned) RecvPacket[40] << 24
			     | (unsigned) RecvPacket[41] << 16
			     | (unsigned) RecvPacket[42] << 8
			     | (unsigned) RecvPacket[43];

	// The NTP seconds wrap at 2036. Unsigned subtraction maps era 0 from 1970
	// on and era 1 to Unix time through 2106; era 0 from 1968 to 1970, which
	// has no Unix time, falls on 2104 to 2106.
	unsigned nTime = nSecondsSince1900 - SEVENTY_YEARS;
	
	return nTime;
}
