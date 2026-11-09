//
// readahead.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2026  R. Stange <rsta2@gmx.net>
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
#ifndef _fatfs_readahead_h
#define _fatfs_readahead_h

#include <circle/types.h>
#include <circle/util.h>

// Read-ahead for the small reads FatFs makes in diskio.cpp. A read of one or
// two sectors pays the device's command overhead for each sector, and FatFs
// reads FAT sectors one at a time while it follows a cluster chain. A run
// starts with a read that begins within or right after the previous one.
// Its first read-ahead is short and doubles while the run goes on, up to the
// buffer's size. A read of DirectSectors or more that the buffer does not
// hold goes to the device directly. A read-ahead that fails is not tried
// again until a run starts elsewhere, so a bad sector nobody asked for costs
// one failed read rather than one for each read of the run. Writes update the
// sectors the buffer holds, so a read always sees the device's data.
//
// One CReadAhead serves one physical drive. diskio.cpp uses read-ahead only
// with FF_MULTI_PARTITION 0, where a drive holds one volume. FatFs with
// FF_FS_REENTRANT 1 locks a volume for each access. With FF_FS_REENTRANT 0
// FatFs itself is not safe against two tasks using one volume at once, so the
// application has to prevent that, and the same then protects the read-ahead.
class CReadAhead
{
public:
	/// The buffer's size and the most sectors read ahead in one go: 128,
	/// 64 KB, which a run reaches with its fourth read-ahead.
	static const unsigned Sectors = 128;
	/// The sectors the first read-ahead of a run reads: 16, 8 KB. This
	/// bounds the extra latency and data of a short run.
	static const unsigned FirstSectors = 16;
	/// The smallest read that the buffer does not hold and that goes to
	/// the device directly: 16. Copying such a read through the buffer
	/// costs more than its command overhead saves.
	static const unsigned DirectSectors = 16;
	/// Bytes in a sector: 512, FatFs's FF_MIN_SS, which diskio.cpp checks.
	static const unsigned SectorSize = 512;

	/// \param pContext The context handed to Read ()
	/// \param pBuffer Where the sectors go, room for nMaxCount sectors
	/// \param ullSector The first sector, any
	/// \param nMaxCount The sectors wanted: FirstSectors to Sectors
	/// \return The sectors read, fewer near the device's end, 0 on failure;
	///	    more than nMaxCount counts as a failure too
	typedef unsigned TReadFunction (void *pContext, u8 *pBuffer, u64 ullSector,
					unsigned nMaxCount);

	CReadAhead (void)
	:	m_pBuffer (0),
		m_ullFirst (0),
		m_nCount (0),
		m_nWindow (FirstSectors),
		m_ullLastFirst (0),
		m_nLastCount (0),
		m_bLast (FALSE),
		m_bFailed (FALSE)
	{
	}

	~CReadAhead (void)
	{
		delete [] m_pBuffer;
	}

	CReadAhead (const CReadAhead &) = delete;
	CReadAhead &operator= (const CReadAhead &) = delete;

	/// \brief A read: served from the buffer if it holds the sectors, or
	///	   after reading ahead from the first of them if the read is
	///	   smaller than DirectSectors and starts within or right after the
	///	   last read
	/// \param pOut Where the sectors go, nCount * SectorSize bytes
	/// \param ullSector The first sector, any
	/// \param nCount The sectors, 1 or more
	/// \param pRead Reads the sectors ahead: read_ahead () in diskio.cpp
	/// \param pContext Handed to pRead: the drive's CDevice
	/// \return TRUE if the sectors are in pOut; FALSE if the caller has to
	///	    read them, because the buffer does not hold them and the read
	///	    does not follow the last one, is DirectSectors or larger, or
	///	    reading ahead failed in this run or stopped short at the
	///	    device's end
	boolean Read (u8 *pOut, u64 ullSector, unsigned nCount, TReadFunction *pRead,
		      void *pContext)
	{
		boolean bFollows =    m_bLast
				   && ullSector > m_ullLastFirst
				   && ullSector - m_ullLastFirst <= m_nLastCount;
		m_ullLastFirst = ullSector;
		m_nLastCount = nCount;
		m_bLast = TRUE;

		if (!bFollows)
		{
			m_bFailed = FALSE;	// a run that starts elsewhere tries again
		}

		if (!Holds (ullSector, nCount))
		{
			if (   !bFollows
			    || nCount >= DirectSectors
			    || m_bFailed)
			{
				return FALSE;
			}

			// A run that reached the buffer's end reads twice as far
			// ahead; any other starts again with the first read-ahead.
			m_nWindow = ReachesEnd (ullSector) ? Smaller (2 * m_nWindow, Sectors)
							   : FirstSectors;
			if (!Fill (ullSector, Larger (m_nWindow, nCount), pRead, pContext))
			{
				m_bFailed = TRUE;

				return FALSE;
			}

			if (!Holds (ullSector, nCount))
			{
				return FALSE;
			}
		}

		memcpy (pOut, m_pBuffer + (ullSector - m_ullFirst) * SectorSize,
			nCount * SectorSize);

		return TRUE;
	}

	/// \brief Sectors were written to the device: the buffer takes over
	///	   those it holds
	/// \param pData The sectors written, nCount * SectorSize bytes
	/// \param ullSector The first sector written, any
	/// \param nCount The sectors written, any
	void Written (const u8 *pData, u64 ullSector, unsigned nCount)
	{
		for (unsigned i = 0; i < nCount; i++)
		{
			if (Holds (ullSector + i, 1))
			{
				memcpy (m_pBuffer + (ullSector + i - m_ullFirst) * SectorSize,
					pData + (u64) i * SectorSize, SectorSize);
			}
		}
	}

	/// \brief Forgets what was read: the device changed, or a write to it failed
	void Invalidate (void)
	{
		m_nCount = 0;
		m_nWindow = FirstSectors;
		m_bLast = FALSE;
		m_bFailed = FALSE;
	}

private:
	/// \param ullSector The first sector, any
	/// \param nCount The sectors, 1 or more
	/// \return TRUE if the buffer holds all of them, FALSE otherwise
	boolean Holds (u64 ullSector, unsigned nCount) const
	{
		return    ullSector >= m_ullFirst
		       && ullSector - m_ullFirst + nCount <= m_nCount;
	}

	/// \param ullSector The first sector of a read the buffer does not hold, any
	/// \return TRUE if it lies in the buffer or right after it, so the run
	///	    reached the buffer's end; FALSE otherwise, and while the buffer
	///	    is empty
	boolean ReachesEnd (u64 ullSector) const
	{
		return    m_nCount > 0
		       && ullSector >= m_ullFirst
		       && ullSector - m_ullFirst <= m_nCount;
	}

	/// \return The smaller of nFirst and nSecond
	static unsigned Smaller (unsigned nFirst, unsigned nSecond)
	{
		return nFirst < nSecond ? nFirst : nSecond;
	}

	/// \return The larger of nFirst and nSecond
	static unsigned Larger (unsigned nFirst, unsigned nSecond)
	{
		return nFirst > nSecond ? nFirst : nSecond;
	}

	/// \brief Reads the sectors from ullSector on into the buffer, which is
	///	   allocated at the first read-ahead and empty if that or the read
	///	   fails
	/// \param ullSector The first sector, any
	/// \param nMaxCount The sectors wanted, 1 to Sectors
	/// \param pRead Reads them
	/// \param pContext Handed to pRead
	/// \return TRUE if the buffer holds at least ullSector, FALSE otherwise
	boolean Fill (u64 ullSector, unsigned nMaxCount, TReadFunction *pRead, void *pContext)
	{
		m_nCount = 0;

		if (m_pBuffer == 0)
		{
			m_pBuffer = new u8[Sectors * SectorSize];
			if (m_pBuffer == 0)
			{
				return FALSE;
			}
		}

		unsigned nCount = (*pRead) (pContext, m_pBuffer, ullSector, nMaxCount);
		if (   nCount == 0
		    || nCount > nMaxCount)
		{
			return FALSE;
		}

		m_ullFirst = ullSector;
		m_nCount = nCount;

		return TRUE;
	}

	u8	*m_pBuffer;		// Sectors * SectorSize bytes; 0 until the first read-ahead
	u64	 m_ullFirst;		// the first sector in m_pBuffer; 0 at the start
	unsigned m_nCount;		// the sectors in m_pBuffer, 0 to Sectors; 0 at the start
	unsigned m_nWindow;		// the sectors of the last read-ahead, FirstSectors to
					// Sectors; FirstSectors at the start and after
					// Invalidate ()
	u64	 m_ullLastFirst;	// the first sector of the last read; 0 at the start
	unsigned m_nLastCount;		// the sectors of the last read; 0 at the start
	boolean	 m_bLast;		// whether m_ullLastFirst and m_nLastCount tell the
					// last read; FALSE at the start and after Invalidate ()
	boolean	 m_bFailed;		// whether a read-ahead of the current run failed, so the
					// run reads nothing ahead; FALSE at the start, after
					// Invalidate () and when a run starts elsewhere
};

#endif
