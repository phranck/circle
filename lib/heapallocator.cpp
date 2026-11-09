//
// heapallocator.cpp
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
#include <circle/heapallocator.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

u32 CHeapAllocator::s_nBucketSize[] = { HEAP_BLOCK_BUCKET_SIZES };

CHeapAllocator::CHeapAllocator (const char *pHeapName)
:	m_pHeapName (pHeapName),
	m_pNext (0),
	m_pLimit (0),
	m_nReserve (0),
	m_pLargeFreeList (0)
{
	memset (m_Bucket, 0, sizeof m_Bucket);

	unsigned nBuckets = sizeof s_nBucketSize / sizeof s_nBucketSize[0];
	if (nBuckets > HEAP_BLOCK_MAX_BUCKETS)
	{
		nBuckets = HEAP_BLOCK_MAX_BUCKETS;
	}

	for (unsigned i = 0; i < nBuckets; i++)
	{
		m_Bucket[i].nSize = s_nBucketSize[i];
	}
}

CHeapAllocator::~CHeapAllocator (void)
{
}

void CHeapAllocator::Setup (uintptr nBase, size_t nSize, size_t nReserve)
{
	m_pNext = (u8 *) nBase;
	m_pLimit = (u8 *) (nBase + nSize);
	m_nReserve = nReserve;
}

size_t CHeapAllocator::GetFreeSpace (void) const
{
	return m_pLimit - m_pNext;
}

void *CHeapAllocator::Allocate (size_t nSize)
{
#ifdef KASAN_SUPPORTED
	return KasanAllocateHook (*this, nSize);
#else
	return DoAllocate (nSize);
#endif
}

void *CHeapAllocator::DoAllocate (size_t nSize)
{
	if (m_pNext == 0)
	{
		return 0;
	}

	m_SpinLock.Acquire ();

	THeapBlockBucket *pBucket;
	for (pBucket = m_Bucket; pBucket->nSize > 0; pBucket++)
	{
		if (nSize <= pBucket->nSize)
		{
			nSize = pBucket->nSize;

#ifdef HEAP_DEBUG
			if (++pBucket->nCount > pBucket->nMaxCount)
			{
				pBucket->nMaxCount = pBucket->nCount;
			}
#endif

			break;
		}
	}

	THeapBlockHeader *pBlockHeader;
	if (   pBucket->nSize > 0
	    && (pBlockHeader = pBucket->pFreeList) != 0)
	{
		assert (pBlockHeader->nMagic == HEAP_BLOCK_FREE_MAGIC);
		pBucket->pFreeList = pBlockHeader->pNext;
	}
	else if (   pBucket->nSize > 0
		 || (pBlockHeader = TakeLargeBlock (nSize)) == 0)
	{
		pBlockHeader = (THeapBlockHeader *) m_pNext;

		u8 *pNextBlock = m_pNext;
		pNextBlock += (sizeof (THeapBlockHeader) + nSize + HEAP_BLOCK_ALIGN-1) & ~HEAP_ALIGN_MASK;

		if (   pNextBlock <= m_pNext			// may have wrapped
		    || pNextBlock > m_pLimit-m_nReserve)
		{
			if (m_nReserve == 0)
			{
				m_SpinLock.Release ();

				return 0;
			}

			m_nReserve = 0;

			m_SpinLock.Release ();

#ifdef HEAP_DEBUG
			DumpStatus ();
#endif
#if STDLIB_SUPPORT >= 3
			// C++ exception should be thrown after returning 0
			CLogger::Get ()->WriteNoAlloc (m_pHeapName, LogWarning, "Out of memory");
#else
			CLogger::Get ()->Write (m_pHeapName, LogPanic, "Out of memory");
#endif

			return 0;
		}

		m_pNext = pNextBlock;

		// A bucket block keeps its bucket's size, which DoFree() matches exactly.
		// A large block keeps all the room it takes, so TakeLargeBlock() can
		// give it to any later request up to that size.
		pBlockHeader->nSize = pBucket->nSize > 0 ? (u32) nSize
							 : (u32) (pNextBlock - pBlockHeader->Data);
	}

	m_SpinLock.Release ();

	pBlockHeader->nMagic = HEAP_BLOCK_ALLOC_MAGIC;
	pBlockHeader->pNext = 0;

	void *pResult = pBlockHeader->Data;
	assert (((uintptr) pResult & HEAP_ALIGN_MASK) == 0);

	return pResult;
}

THeapBlockHeader *CHeapAllocator::TakeLargeBlock (size_t nSize)
{
	// the room of a large block is its request rounded up to HEAP_BLOCK_ALIGN,
	// so no block fits better than one with this room, and the search ends there
	size_t nBestFit = (nSize + HEAP_ALIGN_MASK) & ~HEAP_ALIGN_MASK;

	THeapBlockHeader *pBest = 0;
	THeapBlockHeader *pBeforeBest = 0;
	THeapBlockHeader *pBefore = 0;
	for (THeapBlockHeader *pBlock = m_pLargeFreeList; pBlock != 0; pBlock = pBlock->pNext)
	{
		assert (pBlock->nMagic == HEAP_BLOCK_FREE_MAGIC);
		if (   pBlock->nSize >= nSize
		    && (pBest == 0 || pBlock->nSize < pBest->nSize))
		{
			pBest = pBlock;
			pBeforeBest = pBefore;

			if (pBlock->nSize == nBestFit)
			{
				break;
			}
		}
		pBefore = pBlock;
	}

	if (pBest == 0)
	{
		return 0;
	}

	if (pBeforeBest == 0)
	{
		m_pLargeFreeList = pBest->pNext;
	}
	else
	{
		pBeforeBest->pNext = pBest->pNext;
	}

	return pBest;
}

void *CHeapAllocator::ReAllocate (void *pBlock, size_t nSize)
{
#ifdef KASAN_SUPPORTED
	return KasanReAllocateHook (*this, pBlock, nSize);
#else
	return DoReAllocate (pBlock, nSize);
#endif
}

void *CHeapAllocator::DoReAllocate (void *pBlock, size_t nSize)
{
	if (pBlock == 0)
	{
		return Allocate (nSize);
	}

	if (nSize == 0)
	{
		Free (pBlock);

		return 0;
	}

	THeapBlockHeader *pBlockHeader =
		(THeapBlockHeader *) ((uintptr) pBlock - sizeof (THeapBlockHeader));
	assert (pBlockHeader->nMagic == HEAP_BLOCK_ALLOC_MAGIC);
	if (pBlockHeader->nSize >= nSize)
	{
		return pBlock;
	}

	void *pNewBlock = Allocate (nSize);
	if (pNewBlock == 0)
	{
		return 0;
	}

	memcpy (pNewBlock, pBlock, pBlockHeader->nSize);

	Free (pBlock);

	return pNewBlock;
}

void CHeapAllocator::Free (void *pBlock)
{
#ifdef KASAN_SUPPORTED
	KasanFreeHook (*this, pBlock);
#else
	DoFree (pBlock);
#endif
}

void CHeapAllocator::DoFree (void *pBlock)
{
	if (pBlock == 0)
	{
		return;
	}

	THeapBlockHeader *pBlockHeader =
		(THeapBlockHeader *) ((uintptr) pBlock - sizeof (THeapBlockHeader));
	assert (pBlockHeader->nMagic == HEAP_BLOCK_ALLOC_MAGIC);
	pBlockHeader->nMagic = HEAP_BLOCK_FREE_MAGIC;

	for (THeapBlockBucket *pBucket = m_Bucket; pBucket->nSize > 0; pBucket++)
	{
		if (pBlockHeader->nSize == pBucket->nSize)
		{
			m_SpinLock.Acquire ();

			pBlockHeader->pNext = pBucket->pFreeList;
			pBucket->pFreeList = pBlockHeader;

#ifdef HEAP_DEBUG
			pBucket->nCount--;
#endif

			m_SpinLock.Release ();

			return;
		}
	}

	m_SpinLock.Acquire ();

	pBlockHeader->pNext = m_pLargeFreeList;
	m_pLargeFreeList = pBlockHeader;

	m_SpinLock.Release ();
}

#ifdef HEAP_DEBUG

void CHeapAllocator::DumpStatus (void)
{
	for (THeapBlockBucket *pBucket = m_Bucket; pBucket->nSize > 0; pBucket++)
	{
		CLogger::Get ()->Write (m_pHeapName, LogDebug, "malloc(%lu): %u blocks (max %u)",
					pBucket->nSize, pBucket->nCount, pBucket->nMaxCount);
	}

	m_SpinLock.Acquire ();

	unsigned nLargeBlocks = 0;
	size_t nLargeBytes = 0;
	for (THeapBlockHeader *pBlock = m_pLargeFreeList; pBlock != 0; pBlock = pBlock->pNext)
	{
		nLargeBlocks++;
		nLargeBytes += pBlock->nSize;
	}

	m_SpinLock.Release ();

	CLogger::Get ()->Write (m_pHeapName, LogDebug, "large blocks free: %u (%lu bytes)",
				nLargeBlocks, (unsigned long) nLargeBytes);
}

#endif
