//
// netconfig.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2026  R. Stange <rsta2@gmx.net>
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
#include <circle/net/netconfig.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

static const char FromNetConfig[] = "netcfg";

CNetConfig::CNetConfig (void)
:	m_bUseDHCP (TRUE)
{
	Reset ();
}

CNetConfig::~CNetConfig (void)
{
}

void CNetConfig::Reset (void)
{
	static const u8 NullAddress[] = {0, 0, 0, 0};

	m_IPAddress.Set (NullAddress);
	m_NetMask.Set (NullAddress);
	m_DefaultGateway.Set (NullAddress);
	m_DNSServer.Set (NullAddress);
	m_NTPServer.Set (NullAddress);
	m_DomainName[0] = '\0';
	UpdateBroadcastAddress ();
}

void CNetConfig::SetDHCP (boolean bUsed)
{
	m_bUseDHCP = bUsed;
}

void CNetConfig::SetIPAddress (u32 nAddress)
{
	m_IPAddress.Set (nAddress);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetNetMask (u32 nNetMask)
{
	m_NetMask.Set (nNetMask);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetDefaultGateway (u32 nAddress)
{
	m_DefaultGateway.Set (nAddress);
}

void CNetConfig::SetDNSServer (u32 nAddress)
{
	m_DNSServer.Set (nAddress);
}

void CNetConfig::SetNTPServer (u32 nAddress)
{
	m_NTPServer.Set (nAddress);
}

boolean CNetConfig::SetDomainName (const char *pDomainName)
{
	assert (pDomainName != 0);

	// one character more than a name may have shows a name that is too long
	unsigned nLength = strnlen (pDomainName, NET_DOMAIN_NAME_MAX+1);
	if (   nLength > 0
	    && !IsDomainName (pDomainName, nLength))
	{
		m_DomainName[0] = '\0';

		CLogger::Get ()->Write (FromNetConfig, LogWarning,
					"Domain name refused: not a valid name (caller 0x%lX)",
					(unsigned long) (uintptr) __builtin_return_address (0));

		return FALSE;
	}

	// pDomainName may point into m_DomainName, as GetDomainName() returns it
	memmove (m_DomainName, pDomainName, nLength);
	m_DomainName[nLength] = '\0';

	return TRUE;
}

boolean CNetConfig::IsDomainName (const char *pName, unsigned nLength)
{
	assert (pName != 0);

	if (nLength == 0 || nLength > NET_DOMAIN_NAME_MAX)
	{
		return FALSE;
	}

	unsigned nLabelLength = 0;
	for (unsigned i = 0; i < nLength; i++)
	{
		char chChar = pName[i];
		if (chChar == '.')
		{
			if (nLabelLength == 0 || pName[i-1] == '-')
			{
				return FALSE;
			}

			nLabelLength = 0;

			continue;
		}

		boolean bLetterOrDigit =    (chChar >= 'a' && chChar <= 'z')
					 || (chChar >= 'A' && chChar <= 'Z')
					 || (chChar >= '0' && chChar <= '9');
		if (   !bLetterOrDigit
		    && !(chChar == '-' && nLabelLength > 0))
		{
			return FALSE;
		}

		if (++nLabelLength > NET_DOMAIN_LABEL_MAX)
		{
			return FALSE;
		}
	}

	// the last label must not be empty (a dot at the end) or end with a hyphen
	return    nLabelLength > 0
	       && pName[nLength-1] != '-';
}

void CNetConfig::SetIPAddress (const u8 *pAddress)
{
	m_IPAddress.Set (pAddress);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetNetMask (const u8 *pNetMask)
{
	m_NetMask.Set (pNetMask);

	UpdateBroadcastAddress ();
}

void CNetConfig::SetDefaultGateway (const u8 *pAddress)
{
	m_DefaultGateway.Set (pAddress);
}

void CNetConfig::SetDNSServer (const u8 *pAddress)
{
	m_DNSServer.Set (pAddress);
}

void CNetConfig::SetNTPServer (const u8 *pAddress)
{
	m_NTPServer.Set (pAddress);
}

const CIPAddress *CNetConfig::GetIPAddress (void) const
{
	return &m_IPAddress;
}

boolean CNetConfig::IsDHCPUsed (void) const
{
	return m_bUseDHCP;
}

const u8 *CNetConfig::GetNetMask (void) const
{
	return m_NetMask.Get ();
}

const CIPAddress *CNetConfig::GetDefaultGateway (void) const
{
	return &m_DefaultGateway;
}

const CIPAddress *CNetConfig::GetDNSServer (void) const
{
	return &m_DNSServer;
}

const CIPAddress *CNetConfig::GetBroadcastAddress (void) const
{
	return &m_BroadcastAddress;
}

const CIPAddress *CNetConfig::GetNTPServer (void) const
{
	return &m_NTPServer;
}

const char *CNetConfig::GetDomainName (void) const
{
	return m_DomainName;
}

void CNetConfig::UpdateBroadcastAddress (void)
{
	u32 nIPAddress;
	m_IPAddress.CopyTo ((u8 *) &nIPAddress);

	u32 nNetMask;
	m_NetMask.CopyTo ((u8 *) &nNetMask);

	m_BroadcastAddress.Set (nIPAddress | ~nNetMask);
}
