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
#include "readahead.h"
#include <circle/device.h>
#include <circle/devicenameservice.h>
#include <circle/util.h>
#include <circle/types.h>
#include <assert.h>

#if FF_MIN_SS != FF_MAX_SS
	#error FF_MIN_SS != FF_MAX_SS is not supported!
#endif
#define SECTOR_SIZE		FF_MIN_SS

static_assert (CReadAhead::SectorSize == SECTOR_SIZE, "CReadAhead's sectors are FatFs's");

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

/* The sectors after a read that follows the one before (readahead.h), one
   read-ahead for each of the FF_VOLUMES drives, by drive number. A drive's is
   forgotten as the drive is set up, after a write to it fails, and as the
   drive is ejected. With FF_MULTI_PARTITION a drive can hold several volumes,
   which FatFs lets two tasks use at once, so reads then go to the device
   directly. */
static CReadAhead s_ReadAhead[FF_VOLUMES];
#define READ_AHEAD		(FF_MULTI_PARTITION == 0)



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



/*-----------------------------------------------------------------------*/
/* Read-ahead                                                            */
/*-----------------------------------------------------------------------*/

#if READ_AHEAD

/* Reads the sectors from `ullSector` on for a drive's read-ahead
   (`CReadAhead::Read()`), `nMaxCount` of them and fewer near the device's
   end. Returns how many it read, or `0` if the device's size is unknown,
   `ullSector` lies past its end, or the read fails. */
static unsigned read_ahead (
	void *pContext,		/* the drive's `CDevice` */
	u8 *pBuffer,		/* room for `nMaxCount` sectors, word aligned */
	u64 ullSector,		/* the first sector, any */
	unsigned nMaxCount	/* the most sectors to read in one operation */
)
{
	CDevice *pDevice = (CDevice *) pContext;

	u64 ullSize = pDevice->GetSize ();
	if (ullSize == (u64) -1)
	{
		return 0;
	}

	u64 ullSectors = ullSize / SECTOR_SIZE;
	if (ullSector >= ullSectors)
	{
		return 0;
	}

	unsigned nCount = nMaxCount;
	if (ullSectors - ullSector < nCount)
	{
		nCount = (unsigned) (ullSectors - ullSector);
	}

	if (pDevice->Seek (ullSector * SECTOR_SIZE) == (u64) -1)
	{
		return 0;
	}

	if (pDevice->Read (pBuffer, nCount * SECTOR_SIZE) != (int) (nCount * SECTOR_SIZE))
	{
		return 0;
	}

	return nCount;
}

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

	s_ReadAhead[pdrv].Invalidate ();

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

#if READ_AHEAD
	if (s_ReadAhead[pdrv].Read (buff, sector, count, read_ahead, pDevice))
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
		s_ReadAhead[pdrv].Invalidate ();

		return RES_ERROR;
	}

	s_ReadAhead[pdrv].Written (buff, sector, count);

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
		s_ReadAhead[pdrv].Invalidate ();

		return RES_OK;
	}

	return RES_PARERR;
}

