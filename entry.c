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
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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
#include "efilinux.h"

#define ERROR_STRING_LENGTH	32

EFI_SYSTEM_TABLE *sys_table;
EFI_BOOT_SERVICES *boot;
EFI_RUNTIME_SERVICES *runtime;

/**
 * memory_map - Allocate and fill out an array of memory descriptors
 * @map_buf: buffer containing the memory map
 * @map_size: size of the buffer containing the memory map
 * @map_key: key for the current memory map
 * @desc_size: size of the desc
 * @desc_version: memory descriptor version
 *
 * On success, @map_size contains the size of the memory map pointed
 * to by @map_buf and @map_key, @desc_size and @desc_version are
 * updated.
 */
static EFI_STATUS
memory_map(EFI_MEMORY_DESCRIPTOR **map_buf, UINTN *map_size,
	   UINTN *map_key, UINTN *desc_size, UINTN *desc_version)
{
	EFI_STATUS err;

	*map_size = sizeof(**map_buf) * 31;
get_map:

	/*
	 * Because we're about to allocate memory, we may
	 * potentially create a new memory descriptor, thereby
	 * increasing the size of the memory map. So increase
	 * the buffer size by the size of one memory
	 * descriptor, just in case.
	 */
	*map_size += sizeof(**map_buf);

	err = allocate_pool(EfiLoaderData, *map_size,
			    (void **)map_buf);
	if (err != EFI_SUCCESS) {
		Print(L"Failed to allocate pool for memory map");
		goto failed;
	}

	err = get_memory_map(map_size, *map_buf, map_key,
			     desc_size, desc_version);
	if (err != EFI_SUCCESS) {
		if (err == EFI_BUFFER_TOO_SMALL) {
			/*
			 * 'map_size' has been updated to reflect the
			 * required size of a map buffer.
			 */
			free_pool((void *)*map_buf);
			goto get_map;
		}

		Print(L"Failed to get memory map");
		goto failed;
	}

failed:
	return err;
}

static void
print_memory_map(EFI_MEMORY_DESCRIPTOR *buf, UINTN size,
		 UINTN key, UINTN desc_size, UINTN desc_version)
{
	EFI_MEMORY_DESCRIPTOR *desc;
	int i;

	Print(L"System Memory Map\n");
	Print(L"System Memory Map Size: %d\n", size);
	Print(L"Descriptor Version: %d\n", desc_version);
	Print(L"Descriptor Size: %d\n", desc_size);

	desc = buf;
	i = 0;

	while ((void *)desc < (void *)buf + size) {
		UINTN mapping_size;

		mapping_size = desc->NumberOfPages * PAGE_SIZE;

		Print(L"[#%.2d] Type: %s\n", i,
		      memory_type_to_str(desc->Type));

		Print(L"      Attr: 0x%016llx\n", desc->Attribute);

		Print(L"      Phys: [0x%016llx - 0x%016llx]\n",
		      desc->PhysicalStart,
		      desc->PhysicalStart + mapping_size);

		Print(L"      Virt: [0x%016llx - 0x%016llx]",
		      desc->VirtualStart,
		      desc->VirtualStart + mapping_size);

		Print(L"\n");
		desc = (void *)desc + desc_size;
		i++;
	}
}

/**
 * efi_main - The entry point for the OS loader image.
 * @image: firmware-allocated handle that identifies the image
 * @sys_table: EFI system table
 */
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *_table)
{
	EFI_MEMORY_DESCRIPTOR *map_buf;
	UINTN desc_size, desc_version;
	UINTN size, map_key, prev_size;
	WCHAR *error_buf;
	EFI_STATUS err;

	InitializeLib(image, _table);
	sys_table = _table;
	boot = sys_table->BootServices;
	runtime = sys_table->RuntimeServices;

	if (CheckCrc(sys_table->Hdr.HeaderSize, &sys_table->Hdr) != TRUE)
		return EFI_LOAD_ERROR;

	Print(L"efilinux loader\n");


	err = memory_map(&map_buf, &size, &map_key,
			 &desc_size, &desc_version);
	if (err != EFI_SUCCESS)
		goto failed;

	print_memory_map(map_buf, size, map_key, desc_size, desc_version);

	return EFI_SUCCESS;

failed:
	/*
	 * We need to be careful not to trash 'err' here. If we fail
	 * to allocate enough memory to hold the error string fallback
	 * to returning 'err'.
	 */
	if (allocate_pool(EfiLoaderData, ERROR_STRING_LENGTH,
			  (void **)&error_buf) != EFI_SUCCESS) {
		Print(L"Couldn't allocate pages for error string\n");
		return err;
	}

	StatusToString(error_buf, err);
	Print(L": %s\n", error_buf);
	return exit(image, err, ERROR_STRING_LENGTH, error_buf);
}
