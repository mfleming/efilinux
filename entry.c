/*
 * The entry point for efilinux.
 *
 * Copyright (c) 2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 */

#include <efi.h>
#include "efilinux.h"

#define ERROR_STRING_LENGTH	32

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
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *sys_table)
{
	EFI_MEMORY_DESCRIPTOR *map_buf;
	UINTN desc_size, desc_version;
	UINTN size, map_key, prev_size;
	WCHAR *error_buf;
	EFI_STATUS err;

	InitializeLib(image, sys_table);
	if (register_table(sys_table) != TRUE)
		return EFI_LOAD_ERROR;

	Print(L"efilinux loader\n");

	size = sizeof(*map_buf) * 31;

get_map:
	/*
	 * Because we're about to allocate memory, we may potentially
	 * create a new memory descriptor, thereby increasing the size
	 * of the memory map. So increase the buffer size by the size
	 * of one memory descriptor, just in case.
	 */
	size += sizeof(*map_buf);

	err = allocate_pool(EfiLoaderData, size, (void **)&map_buf);
	if (err != EFI_SUCCESS) {
		Print(L"Failed to allocate pool for memory map");
		goto failed;
	}

	err = get_memory_map(&size, map_buf, &map_key,
			     &desc_size, &desc_version);
	if (err != EFI_SUCCESS) {
		if (err == EFI_BUFFER_TOO_SMALL) {
			/*
			 * 'size' has been updated to reflect the
			 * required size of a map buffer.
			 */
			free_pool((void *)map_buf);
			Print(L"Failed to get map, retry size=%d\n", size);
			goto get_map;
		}

		Print(L"Failed to get memory map");
		goto failed;
	}

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
	return exit(image, err, size, error_buf);
}
