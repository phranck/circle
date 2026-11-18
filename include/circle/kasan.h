/*
 * Copyright 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _circle_kasan_h
#define _circle_kasan_h

#ifdef __cplusplus
// These functions are only relevant for the Address Sanitizer 
// integration in C++ code.
void KasanInitialize (void);

class CHeapAllocator;
void *KasanAllocateHook (CHeapAllocator& rHeapAllocator, size_t nSize);
void *KasanReAllocateHook (CHeapAllocator& rHeapAllocator, void *pBlock, ssize_t nSize);
void KasanFreeHook (CHeapAllocator& rHeapAllocator, void *pBlock);

// The memset() and memcpy() of util_fast.S, which KASan does not instrument.
// kasan.cpp writes the shadow memory with them and calls them from its checking
// wrappers, and CHeapAllocator moves a block, which holds KASan's redzones,
// with __kasan_memcpy().
extern "C" void *__kasan_memset (void *pBuffer, int nValue, size_t nLength);
extern "C" void *__kasan_memcpy (void *pDest, const void *pSrc, size_t nLength);
#endif

#endif