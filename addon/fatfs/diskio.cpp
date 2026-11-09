/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/* Implementation for Circle by R. Stange <rsta2@o2online.de>            */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include <circle/device.h>
#include <circle/devicenameservice.h>
#include <circle/util.h>
#include <circle/types.h>
#include <assert.h>

/*-----------------------------------------------------------------------*/
/* Configuration options                                                 */
/*-----------------------------------------------------------------------*/

// FATFS_READ_AHEAD enables a read-ahead buffer for each drive. FatFs reads
// FAT and directory sectors one at a time. A read of a file reaches the
// device in pieces that end at a cluster boundary, and a partial sector is
// read as a whole sector. With small clusters or small reads, the device
// gets many commands for a few sectors each. With this option, a read of
// less than 8 KByte that starts within or right after the previous read of
// the same drive fills the buffer from its first sector. The first
// read-ahead of such a run reads 8 KByte, and each further one reads twice
// as much, up to 64 KByte. Reads that the buffer holds are served from it.
// The buffer takes 64 KByte of heap for each drive that reads ahead. It is
// allocated at the first read-ahead and kept.
//
// Some access patterns get slower with this option. A run of two small
// reads costs one small read and one read-ahead instead of two small reads,
// and each FAT32 mount reads ahead once after the boot sector. A defective
// sector in the requested range costs two failed device reads, one for the
// read-ahead and one for the request itself. Writes that bypass FatFs do
// not reach the buffer, so a later read can return up to 64 KByte of stale
// data. This option requires FF_MULTI_PARTITION 0. It can be enabled here
// or in Config.mk with "DEFINE += -DFATFS_READ_AHEAD".

//#define FATFS_READ_AHEAD

#if FF_MIN_SS != FF_MAX_SS
	#error FF_MIN_SS != FF_MAX_SS is not supported!
#endif
#define SECTOR_SIZE		FF_MIN_SS

/*-----------------------------------------------------------------------*/
/* Static Data                                                           */
/*-----------------------------------------------------------------------*/

static const char *s_pVolumeName[FF_VOLUMES] =
{
	"emmc1",
	"emmc2",
	"umsd1",
	"umsd2",
	"umsd3",
	"ufd1",
	"nvme1"
};

static CDevice *s_pVolume[FF_VOLUMES] = {0};

static u8 *s_pBuffer = 0;
static unsigned s_nBufferSize = 0;



/*-----------------------------------------------------------------------*/
/* Callbacks                                                             */
/*-----------------------------------------------------------------------*/

static void disk_removed (
	CDevice *pDevice,	/* device removed */
	void *pContext
)
{
	*((CDevice **) pContext) = 0;
}



#ifdef FATFS_READ_AHEAD

/*-----------------------------------------------------------------------*/
/* Read-ahead                                                            */
/*-----------------------------------------------------------------------*/

// With FF_MULTI_PARTITION 1 a drive can hold several volumes, which FatFs
// lets two tasks use at the same time, and these would share the buffer.
#if FF_MULTI_PARTITION
	#error FATFS_READ_AHEAD requires FF_MULTI_PARTITION 0!
#endif

// These sizes in bytes are the buffer of a drive, the first read-ahead of a
// run, and the smallest read that goes to the device, unless the buffer
// holds it.
#define READ_AHEAD_SIZE		0x10000
#define READ_AHEAD_FIRST_SIZE	0x2000
#define READ_AHEAD_DIRECT_SIZE	0x2000

ASSERT_STATIC (READ_AHEAD_SIZE % SECTOR_SIZE == 0);
ASSERT_STATIC (READ_AHEAD_FIRST_SIZE % SECTOR_SIZE == 0);
ASSERT_STATIC (READ_AHEAD_DIRECT_SIZE % SECTOR_SIZE == 0);
ASSERT_STATIC (READ_AHEAD_FIRST_SIZE <= READ_AHEAD_SIZE);
ASSERT_STATIC (READ_AHEAD_DIRECT_SIZE <= READ_AHEAD_SIZE);

#define READ_AHEAD_SECTORS		(READ_AHEAD_SIZE / SECTOR_SIZE)
#define READ_AHEAD_FIRST_SECTORS	(READ_AHEAD_FIRST_SIZE / SECTOR_SIZE)
#define READ_AHEAD_DIRECT_SECTORS	(READ_AHEAD_DIRECT_SIZE / SECTOR_SIZE)

// The read-ahead of one drive. A drive holds one volume. With
// FF_FS_REENTRANT 1 FatFs locks the volume for each access, so only one
// task uses the read-ahead of a drive at a time. With FF_FS_REENTRANT 0
// the application has to ensure this, as it has to for FatFs itself.
class CReadAhead
{
public:
	CReadAhead (void);

	// Returns TRUE if the sectors are in pOut. Returns FALSE if the caller
	// has to read them from the device, because the buffer does not hold
	// them and the read does not start within or right after the previous
	// read, is READ_AHEAD_DIRECT_SECTORS or larger, reaches past the end of
	// the device, or follows a failed read-ahead of the same run.
	boolean Read (u8 *pOut, u64 ullSector, unsigned nCount, CDevice *pDevice);

	// Updates each written sector that the buffer holds.
	void Written (const u8 *pData, u64 ullSector, unsigned nCount);

	// Forgets the buffer and the previous read.
	void Invalidate (void);

private:
	// Returns TRUE if the buffer holds all of the sectors.
	boolean Holds (u64 ullSector, unsigned nCount) const;

	// Reads nCount sectors from ullSector on into the buffer, fewer at the
	// end of the device. Returns FALSE and leaves the buffer empty if the
	// device cannot be read.
	boolean Fill (u64 ullSector, unsigned nCount, CDevice *pDevice);

private:
	u8	*m_pBuffer;		// READ_AHEAD_SIZE bytes, 0 until the first read-ahead
	u64	 m_ullFirst;		// the first sector in the buffer
	unsigned m_nCount;		// the sectors in the buffer, 0 if it is empty
	unsigned m_nWindow;		// the sectors of the last read-ahead

	boolean	 m_bPrevious;		// are m_ullPreviousFirst and m_nPreviousCount valid?
	u64	 m_ullPreviousFirst;	// the first sector of the previous read
	unsigned m_nPreviousCount;	// the sectors of the previous read

	boolean	 m_bFailed;		// has a read-ahead of the current run failed?
};

CReadAhead::CReadAhead (void)
:	m_pBuffer (0),
	m_ullFirst (0),
	m_nCount (0),
	m_nWindow (READ_AHEAD_FIRST_SECTORS),
	m_bPrevious (FALSE),
	m_ullPreviousFirst (0),
	m_nPreviousCount (0),
	m_bFailed (FALSE)
{
}

boolean CReadAhead::Read (u8 *pOut, u64 ullSector, unsigned nCount, CDevice *pDevice)
{
	boolean bFollows =    m_bPrevious
			   && ullSector > m_ullPreviousFirst
			   && ullSector - m_ullPreviousFirst <= m_nPreviousCount;

	m_bPrevious = TRUE;
	m_ullPreviousFirst = ullSector;
	m_nPreviousCount = nCount;

	if (!bFollows)
	{
		// A run that starts elsewhere reads ahead again, after a failure too.
		// A run that has failed reads nothing ahead, so that a defective
		// sector outside the requested ranges is not read again and again.
		m_bFailed = FALSE;
	}

	if (!Holds (ullSector, nCount))
	{
		if (   !bFollows
		    || nCount >= READ_AHEAD_DIRECT_SECTORS
		    || m_bFailed)
		{
			return FALSE;
		}

		// A run that has reached the end of the buffer reads twice as far
		// ahead. Any other run starts again with the first read-ahead.
		if (   m_nCount > 0
		    && ullSector >= m_ullFirst
		    && ullSector - m_ullFirst <= m_nCount)
		{
			m_nWindow *= 2;
			if (m_nWindow > READ_AHEAD_SECTORS)
			{
				m_nWindow = READ_AHEAD_SECTORS;
			}
		}
		else
		{
			m_nWindow = READ_AHEAD_FIRST_SECTORS;
		}

		if (!Fill (ullSector, nCount > m_nWindow ? nCount : m_nWindow, pDevice))
		{
			m_bFailed = TRUE;

			return FALSE;
		}

		if (!Holds (ullSector, nCount))
		{
			return FALSE;
		}
	}

	memcpy (pOut, m_pBuffer + (ullSector - m_ullFirst) * SECTOR_SIZE, nCount * SECTOR_SIZE);

	return TRUE;
}

void CReadAhead::Written (const u8 *pData, u64 ullSector, unsigned nCount)
{
	for (unsigned i = 0; i < nCount; i++)
	{
		if (Holds (ullSector + i, 1))
		{
			memcpy (m_pBuffer + (ullSector + i - m_ullFirst) * SECTOR_SIZE,
				pData + i * SECTOR_SIZE, SECTOR_SIZE);
		}
	}
}

void CReadAhead::Invalidate (void)
{
	m_nCount = 0;
	m_bPrevious = FALSE;
	m_bFailed = FALSE;
}

boolean CReadAhead::Holds (u64 ullSector, unsigned nCount) const
{
	return    ullSector >= m_ullFirst
	       && ullSector - m_ullFirst + nCount <= m_nCount;
}

boolean CReadAhead::Fill (u64 ullSector, unsigned nCount, CDevice *pDevice)
{
	m_nCount = 0;

	if (m_pBuffer == 0)
	{
		m_pBuffer = new u8[READ_AHEAD_SIZE];
		assert (m_pBuffer != 0);
	}

	u64 ullSize = pDevice->GetSize ();
	if (ullSize == (u64) -1)
	{
		return FALSE;
	}

	u64 ullSectors = ullSize / SECTOR_SIZE;
	if (ullSector >= ullSectors)
	{
		return FALSE;
	}

	if (nCount > ullSectors - ullSector)
	{
		nCount = (unsigned) (ullSectors - ullSector);
	}

	if (   pDevice->Seek (ullSector * SECTOR_SIZE) == (u64) -1
	    || pDevice->Read (m_pBuffer, nCount * SECTOR_SIZE) != (int) (nCount * SECTOR_SIZE))
	{
		return FALSE;
	}

	m_ullFirst = ullSector;
	m_nCount = nCount;

	return TRUE;
}

static CReadAhead s_ReadAhead[FF_VOLUMES];

#endif



/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (   pdrv < FF_VOLUMES
	    && s_pVolume[pdrv] != 0)
	{
		return 0;
	}

	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv >= FF_VOLUMES)
	{
		return STA_NOINIT;
	}

#ifdef FATFS_READ_AHEAD
	/* The drive can hold another medium than before. */
	s_ReadAhead[pdrv].Invalidate ();
#endif

	s_pVolume[pdrv] = CDeviceNameService::Get ()->GetDevice (s_pVolumeName[pdrv], TRUE);
	if (s_pVolume[pdrv] != 0)
	{
		s_pVolume[pdrv]->RegisterRemovedHandler (disk_removed, &s_pVolume[pdrv]);

		return 0;
	}

	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv >= FF_VOLUMES)
	{
		return RES_PARERR;
	}

	CDevice *pDevice = s_pVolume[pdrv];
	if (pDevice == 0)
	{
		return RES_NOTRDY;
	}

#ifdef FATFS_READ_AHEAD
	if (s_ReadAhead[pdrv].Read (buff, sector, count, pDevice))
	{
		return RES_OK;
	}
#endif

	/* Ensure that the transfer buffer is word aligned */
	BYTE *pBuffer = buff;
	unsigned nSize = count * SECTOR_SIZE;
	if (((uintptr) pBuffer & 3) != 0)
	{
		if (s_nBufferSize < nSize)
		{
			delete [] s_pBuffer;

			s_nBufferSize = nSize;

			s_pBuffer = new u8[s_nBufferSize];
			assert (s_pBuffer != 0);
		}

		pBuffer = s_pBuffer;
	}

	QWORD offset = sector;
	offset *= SECTOR_SIZE;
	pDevice->Seek (offset);

	if (pDevice->Read (pBuffer, nSize) < 0)
	{
		return RES_ERROR;
	}

	if (pBuffer != buff)
	{
		memcpy (buff, pBuffer, nSize);
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (pdrv >= FF_VOLUMES)
	{
		return RES_PARERR;
	}

	CDevice *pDevice = s_pVolume[pdrv];
	if (pDevice == 0)
	{
		return RES_NOTRDY;
	}

	/* Ensure that the transfer buffer is word aligned */
	const BYTE *pBuffer = buff;
	unsigned nSize = count * SECTOR_SIZE;
	if (((uintptr) pBuffer & 3) != 0)
	{
		if (s_nBufferSize < nSize)
		{
			delete [] s_pBuffer;

			s_nBufferSize = nSize;

			s_pBuffer = new u8[s_nBufferSize];
			assert (s_pBuffer != 0);
		}

		memcpy (s_pBuffer, buff, nSize);

		pBuffer = s_pBuffer;
	}

	QWORD offset = sector;
	offset *= SECTOR_SIZE;
	pDevice->Seek (offset);

	if (pDevice->Write (pBuffer, nSize) < 0)
	{
#ifdef FATFS_READ_AHEAD
		/* The device can hold some of the written sectors or a part of them. */
		s_ReadAhead[pdrv].Invalidate ();
#endif

		return RES_ERROR;
	}

#ifdef FATFS_READ_AHEAD
	s_ReadAhead[pdrv].Written (buff, sector, count);
#endif

	return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	switch (cmd)
	{
	case GET_SECTOR_COUNT:
		{
			if (pdrv >= FF_VOLUMES)
			{
				return RES_PARERR;
			}

			CDevice *pDevice =
				CDeviceNameService::Get ()->GetDevice (s_pVolumeName[pdrv], TRUE);
			if (pDevice != 0)
			{
				u64 ullSize = pDevice->GetSize ();
				if (ullSize == (u64) -1)
				{
					return RES_PARERR;
				}

				*(LBA_t *) buff = (LBA_t) (ullSize / SECTOR_SIZE);
			}
			else
			{
				return RES_NOTRDY;
			}
		}
		return RES_OK;

	case CTRL_SYNC:
		{
			if (pdrv >= FF_VOLUMES)
			{
				return RES_PARERR;
			}

			CDevice *pDevice =
				CDeviceNameService::Get ()->GetDevice (s_pVolumeName[pdrv], TRUE);
			if (pDevice != 0)
			{
				/* This fails, if unsupported, so ignore eventual errors. */
				pDevice->IOCtl (DEVICE_IOCTL_SYNC, 0);
			}
			else
			{
				return RES_NOTRDY;
			}
		}
		return RES_OK;

	case GET_SECTOR_SIZE:
		assert (buff != 0);
		*(WORD *) buff = SECTOR_SIZE;
		return RES_OK;

	case CTRL_EJECT:
		if (pdrv >= FF_VOLUMES)
		{
			return RES_PARERR;
		}

		if (s_pVolume[pdrv] == 0)
		{
			s_pVolume[pdrv] = CDeviceNameService::Get ()->GetDevice (s_pVolumeName[pdrv], TRUE);
			if (s_pVolume[pdrv] == 0)
			{
				return RES_NOTRDY;
			}
		}

		if (!s_pVolume[pdrv]->RemoveDevice ())
		{
			return RES_ERROR;
		}

		s_pVolume[pdrv] = 0;

		return RES_OK;
	}

	return RES_PARERR;
}

