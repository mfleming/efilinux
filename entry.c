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
#include <efilib.h>
#include "efilinux.h"
#include "fs.h"
#include "protocol.h"

#define ERROR_STRING_LENGTH	32

static CHAR16 *banner = L"efilinux loader\n";

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
	   UINTN *map_key, UINTN *desc_size, UINT32 *desc_version)
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

static EFI_STATUS print_memory_map(void)
{
	EFI_MEMORY_DESCRIPTOR *buf;
	UINTN desc_size;
	UINT32 desc_version;
	UINTN size, map_key;
	EFI_MEMORY_DESCRIPTOR *desc;
	EFI_STATUS err;
	int i;

	err = memory_map(&buf, &size, &map_key,
			 &desc_size, &desc_version);
	if (err != EFI_SUCCESS)
		return err;

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

	return err;
}

static EFI_STATUS
parse_args(CHAR16 *options, UINT32 size, CHAR16 **name, char **cmdline)
{
	CHAR16 *n, *filename = NULL;
	int i;

	if (!options || size == 0)
		goto fail;

	/* Skip the first word, that's our name. */
	for (i = 0; i < size && options[i] != ' '; i++)
		;

	/* No arguments */
	if (i == size)
		goto fail;

	n = &options[++i];

	while (n <= &options[size]) {
		if (*n == '-') {
			switch (*++n) {
			case 'h':
				goto fail;
			case 'f':
				n++;	/* Skip 'f' */

				/* Skip whitespace */
				while (*n == ' ')
					n++;

				filename = n;
				i = 0;	
				while (*n && *n != ' ' && *n != '\n') {
					i++;
					n++;
				}
				*n++ = '\0';

				*name = malloc(i + 1);
				if (!*name) {
					Print(L"Unable to alloc filename memory\n");
					goto fail;
				}
				*name[i--] = '\0';

				StrCpy(*name, filename);
				break;
			case 'l':
				list_boot_devices();
				goto out;
			case 'm':
				print_memory_map();
				n++;
				break;
			default:
				Print(L"Unknown command-line switch\n");
				goto fail;
			}
		} else {
			char *s1;
			CHAR16 *s2;
			int j;

			j = StrLen(n);
			*cmdline = malloc(j + 1);
			if (!*cmdline) {
				Print(L"Unable to alloc cmdline memory\n");
				goto fail;
			}
			
			s1 = *cmdline;
			s2 = n;

			while (j--)
				*s1++ = *s2++;

			*s1 = '\0';

			/* Consume the rest of the args */
			n = &options[size] + 1;
		}
	}

	if (!filename)
		goto fail;

	return EFI_SUCCESS;

fail:
	Print(L"usage: efilinux [-hlm] -f <filename> <args>\n\n");
	Print(L"\t-h:             display this help menu\n");
	Print(L"\t-l:             list boot devices\n");
	Print(L"\t-m:             print memory map\n");
	Print(L"\t-f <filename>:  image to load\n");
	Print(L"Error");

out:
	return EFI_INVALID_PARAMETER;
}

/**
 * efi_main - The entry point for the OS loader image.
 * @image: firmware-allocated handle that identifies the image
 * @sys_table: EFI system table
 */
EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *_table)
{
	WCHAR *error_buf;
	EFI_STATUS err;
	EFI_LOADED_IMAGE *info;
	CHAR16 *name;
	char *cmdline;

	InitializeLib(image, _table);
	sys_table = _table;
	boot = sys_table->BootServices;
	runtime = sys_table->RuntimeServices;

	if (CheckCrc(sys_table->Hdr.HeaderSize, &sys_table->Hdr) != TRUE)
		return EFI_LOAD_ERROR;

	Print(banner);

	err = fs_init();
	if (err != EFI_SUCCESS)
		goto failed;

	err = handle_protocol(image, &LoadedImageProtocol, (void **)&info);
	if (err != EFI_SUCCESS)
		goto failed;

	err = parse_args(info->LoadOptions, info->LoadOptionsSize,
			 &name, &cmdline);
	if (err != EFI_SUCCESS)
		goto failed;

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
