/*
 * Copyright (c) 2011, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <efi.h>
#include <efilib.h>
#include "efilinux.h"

/**
 * emalloc - Allocate memory with a strict alignment requirement
 * @size: size in bytes of the requested allocation
 * @align: the required alignment of the allocation
 * @addr: a pointer to the allocated address on success
 *
 * If we cannot satisfy @align we return 0.
 */
EFI_STATUS emalloc(UINTN size, UINTN align, EFI_PHYSICAL_ADDRESS *addr)
{
	UINTN map_size, map_key, desc_size;
	EFI_MEMORY_DESCRIPTOR *map_buf;
	UINTN d, map_end;
	UINT32 desc_version;
	EFI_STATUS err;
	UINTN nr_pages = EFI_SIZE_TO_PAGES(size);

	err = memory_map(&map_buf, &map_size, &map_key,
			 &desc_size, &desc_version);
	if (err != EFI_SUCCESS)
		goto fail;

	d = (UINTN)map_buf;
	map_end = (UINTN)map_buf + map_size;

	for (; d < map_end; d += desc_size) {
		EFI_MEMORY_DESCRIPTOR *desc;
		EFI_PHYSICAL_ADDRESS start, end, aligned;

		desc = (EFI_MEMORY_DESCRIPTOR *)d;
		if (desc->Type != EfiConventionalMemory)
			continue;

		if (desc->NumberOfPages < nr_pages)
			continue;

		start = desc->PhysicalStart;
		end = start + (desc->NumberOfPages << EFI_PAGE_SHIFT);

		/* Low-memory is super-precious! */
		if (end <= 1 << 20)
			continue;
		if (start < 1 << 20) {
			size -= (1 << 20) - start;
			start = (1 << 20);
		}

		aligned = (start + align -1) & ~(align -1);

		if ((aligned + size) <= end) {
			err = allocate_pages(AllocateAddress, EfiLoaderData,
					     nr_pages, &aligned);
			if (err == EFI_SUCCESS) {
				*addr = aligned;
				break;
			}
		}
	}

	if (d == map_end)
		err = EFI_OUT_OF_RESOURCES;

	free_pool(map_buf);
fail:
	return err;
}

/**
 * efree - Return memory allocated with emalloc
 * @memory: the address of the emalloc() allocation
 * @size: the size of the allocation
 */
void efree(EFI_PHYSICAL_ADDRESS memory, UINTN size)
{
	UINTN nr_pages = EFI_SIZE_TO_PAGES(size);

	free_pages(memory, nr_pages);
}

/**
 * malloc - Allocate memory from the EfiLoaderData pool
 * @size: size in bytes of the requested allocation
 *
 * Return a pointer to an allocation of @size bytes of type
 * EfiLoaderData.
 */
void *malloc(UINTN size)
{
	EFI_STATUS err;
	void *buffer;

	err = allocate_pool(EfiLoaderData, size, &buffer);
	if (err != EFI_SUCCESS)
		buffer = NULL;

	return buffer;
}

/**
 * free - Release memory to the EfiLoaderData pool
 * @buffer: pointer to the malloc() allocation to free
 */
void free(void *buffer)
{
	free_pool(buffer);
}
