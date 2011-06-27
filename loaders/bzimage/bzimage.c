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
#include "bzimage.h"
#include "fs.h"
#include "loader.h"
#include "protocol.h"
#include "stdlib.h"

#ifdef x86_64
#include "x86_64.h"
#else
#include "i386.h"
#endif

dt_addr_t gdt = { 0x800, (UINT64 *)0 };
dt_addr_t idt = { 0, 0 };

/**
 * load_kernel - Load a kernel image into memory from the boot device
 */
EFI_STATUS
load_kernel(EFI_HANDLE image, CHAR16 *name, char *cmdline)
{
	UINTN map_size, _map_size, map_key;
	EFI_PHYSICAL_ADDRESS kernel_start;
	struct boot_params *boot_params;
	EFI_MEMORY_DESCRIPTOR *map_buf;
	struct e820_entry *e820_map;
	struct boot_params *buf;
	struct efi_info *efi;
	UINT32 desc_version;
	UINT8 nr_setup_secs;
	struct file *file;
	UINTN desc_size;
	EFI_STATUS err;
	UINTN size = 0;
	char *initrd;
	int i;

	err = file_open(name, &file);
	if (err != EFI_SUCCESS)
		goto out;

	err = file_set_position(file, (UINT64)0x1F1);
	if (err != EFI_SUCCESS)
		goto out;

	size = 1;
	err = file_read(file, &size, &nr_setup_secs);
	if (err != EFI_SUCCESS)
		goto out;

	nr_setup_secs++;	/* Add the boot sector */
	size = nr_setup_secs * 512;

	buf = malloc(size);
	if (!buf)
		goto out;

	err = file_set_position(file, (UINT64)0);
	if (err != EFI_SUCCESS)
		goto out;

	err = file_read(file, &size, buf);
	if (err != EFI_SUCCESS)
		goto out;

	/* Check boot sector signature */
	if (buf->hdr.signature != 0xAA55) {
		Print(L"bzImage kernel corrupt");
		err = EFI_INVALID_PARAMETER;
		goto out;
	}

	if (buf->hdr.header != SETUP_HDR) {
		Print(L"Setup code version is invalid");
		err = EFI_INVALID_PARAMETER;
		goto out;
	}

	/*
	 * Which setup code version?
	 *
	 * We only support relocatable kernels which require a setup
	 * code version >= 2.05.
	 */
	if (buf->hdr.version < 0x205) {
		Print(L"Setup code version unsupported (too old)");
		err = EFI_INVALID_PARAMETER;
		goto out;
	}

	/* Don't need an allocated ID, we're a prototype */
	buf->hdr.loader_id = 0x1;

	/*
	 * Has there been an initrd specified on the cmdline?
	 */
	initrd = strstr(cmdline, "initrd");
	if (initrd) {
		initrd += strlen("initrd");

		/* Make sure the initrd string is valid */
		if (*initrd++ == '=') {
			CHAR16 filename[MAX_FILENAME], *n;
			struct file *file;
			char *o, *p;
			UINT64 size;
			void *rd;

			p = initrd;
			while (*p && *p != ' ')
				p++;

			for (o = initrd, n = filename; o != p; o++, n++)
				*n = *o;

			*n = '\0';
			err = file_open(filename, &file);
			if (err != EFI_SUCCESS)
				goto out;

			file_size(file, &size);
			err = emalloc(size, 1, (EFI_PHYSICAL_ADDRESS *)&rd);
			if (err != EFI_SUCCESS) {
				file_close(file);
				goto out;
			}

			if ((UINT32)(UINT64)rd > buf->hdr.ramdisk_max) {
				Print(L"ramdisk address is too high!\n");
				efree((EFI_PHYSICAL_ADDRESS)rd, size);
				file_close(file);
				goto out;
			}

			err = file_read(file, &size, rd);
			file_close(file);

			if (err != EFI_SUCCESS)
				goto out;

			buf->hdr.ramdisk_start = (UINT32)(UINT64)rd;
			buf->hdr.ramdisk_len = size;
		}
	} else {
		buf->hdr.ramdisk_start = 0;
		buf->hdr.ramdisk_len = 0;
	}
	
	buf->hdr.cmd_line_ptr = (UINT32)(UINT64)cmdline;
	buf->hdr.cmdline_size = strlen(cmdline);

	memset((char *)&buf->screen_info, 0x0, sizeof(buf->screen_info));

	err = setup_graphics(buf);
	if (err != EFI_SUCCESS)
		goto out;

	/*
	 * Time to allocate our memory.
	 *
	 * Because the kernel needs to decompress itself we first
	 * allocate boot_params, gdt and space for the memory map
	 * under the assumption that they'll be allocated at lower
	 * addresses than the kernel. If we dont't allocate these data
	 * structures first there is the potential for them to be
	 * trashed when the kernel is decompressed! Allocating them
	 * underneath the kernel should be safe.
	 *
	 * Max kernel size is 8MB
	 */
	err = emalloc(16384, 1, (EFI_PHYSICAL_ADDRESS *)&boot_params);
	if (err != EFI_SUCCESS)
		goto out;

	memset((void *)boot_params, 0x0, 16384);

	/* Copy first two sectors to boot_params */
	memcpy((char *)boot_params, (char *)buf, 2 * 512);

	err = emalloc(gdt.limit, 8, (EFI_PHYSICAL_ADDRESS *)&gdt.base);
	if (err != EFI_SUCCESS)
		goto out;

	memset((char *)gdt.base, 0x0, gdt.limit);

	/*
	 * 4Gb - (0x100000*0x1000 = 4Gb)
	 * base address=0
	 * code read/exec
	 * granularity=4096, 386 (+5th nibble of limit)
	 */
	gdt.base[2] = 0x00cf9a000000ffff;

	/*
	 * 4Gb - (0x100000*0x1000 = 4Gb)
	 * base address=0
	 * data read/write
	 * granularity=4096, 386 (+5th nibble of limit)
	 */
	gdt.base[3] = 0x00cf92000000ffff;

	/* Task segment value */
	gdt.base[4] = 0x0080890000000000;

	/*
	 * Free any memory that we will no longer need once we jump to
	 * the kernel entry point.
	 */
	fs_exit();

	/* We're just interested in the map's size for now */
	map_size = 0;
	err = get_memory_map(&map_size, NULL, NULL, NULL, NULL);
	if (err != EFI_SUCCESS && err != EFI_BUFFER_TOO_SMALL)
		goto out;

again:
	_map_size = map_size;
	err = emalloc(map_size, 1, (EFI_PHYSICAL_ADDRESS *)&map_buf);
	if (err != EFI_SUCCESS)
		goto out;

	size = 0x800000;
	err = emalloc(size, buf->hdr.kernel_alignment, &kernel_start);
	if (err != EFI_SUCCESS)
		goto out;

	/*
	 * If the firmware doesn't sort the memory map by increasing
	 * address it's possible that kernel_start may have been
	 * allocated below boot_params or gdt.base.
	 *
	 * Print a warning and hope for the best.
	 */
	if (kernel_start < (EFI_PHYSICAL_ADDRESS)boot_params ||
	    kernel_start < (EFI_PHYSICAL_ADDRESS)map_buf ||
	    kernel_start < (EFI_PHYSICAL_ADDRESS)gdt.base)
	    Print(L"Warning: kernel_start is too low.\n");

	/*
	 * Read the rest of the kernel image.
	 */
	err = file_read(file, &size, (void *)kernel_start);
	if (err != EFI_SUCCESS)
		goto out;

	boot_params->hdr.code32_start = (UINT32)((UINT64)kernel_start);

	/*
	 * Remember! We've already allocated map_buf with emalloc (and
	 * 'map_size' contains its size) which means that it should be
	 * positioned below our allocation for the kernel. Use that
	 * space for the memory map.
	 */
	err = get_memory_map(&map_size, map_buf, &map_key,
			     &desc_size, &desc_version);
	if (err != EFI_SUCCESS) {
		if (err == EFI_BUFFER_TOO_SMALL) {
			/*
			 * Argh! The buffer that we allocated further
			 * up wasn't large enough which means we need
			 * to allocate them again, but this time
			 * larger. 'map_size' has been updated by the
			 * call to memory_map().
			 */
			efree(kernel_start, 0x800000);
			efree((EFI_PHYSICAL_ADDRESS)map_buf, _map_size);
			file_set_position(file, (UINT64)nr_setup_secs * 512);
			goto again;
		}
		goto out;
	}

	err = exit_boot_services(image, map_key);
	if (err != EFI_SUCCESS)
		goto out;

	efi = &boot_params->efi_info;
	efi->efi_systab = (UINT32)(UINT64)sys_table;
	efi->efi_memdesc_size = desc_size;
	efi->efi_memdesc_version = desc_version;
	efi->efi_memmap = (UINT32)(UINT64)map_buf;
	efi->efi_memmap_size = map_size;
	efi->efi_systab_hi = (unsigned long)sys_table >> 32;
	efi->efi_memmap_hi = (unsigned long)map_buf >> 32;

	memcpy((char *)&efi->efi_loader_signature,
	       EFI_LOADER_SIGNATURE, sizeof(UINT32));

	boot_params->alt_mem_k = 32 * 1024;

	e820_map = &boot_params->e820_map[0];

	/*
	 * Convert the EFI memory map to E820.
	 */
	for (i = 0; i < map_size / desc_size; i++) {
		EFI_MEMORY_DESCRIPTOR *d;
		unsigned int e820_type = 0;

		d = (EFI_MEMORY_DESCRIPTOR *)((unsigned long)map_buf + (i * desc_size));
		switch(d->Type) {
		case EfiReservedMemoryType:
		case EfiRuntimeServicesData:
		case EfiUnusableMemory:
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
		case EfiPalCode:
			e820_type = E820_RESERVED;
			break;

		case EfiRuntimeServicesCode:
			e820_type = E820_UNUSABLE;
			break;

		case EfiACPIReclaimMemory:
			e820_type = E820_ACPI;
			break;

		case EfiLoaderCode:
		case EfiLoaderData:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiConventionalMemory:
			e820_type = E820_RAM;
			break;

		case EfiACPIMemoryNVS:
			e820_type = E820_NVS;
			break;

		default:
			Print(L"Invalid EFI memory descriptor type!\n");
			continue;
		}

		e820_map->addr = d->PhysicalStart;
		e820_map->size = d->NumberOfPages << EFI_PAGE_SHIFT;
		e820_map->type = e820_type;
		e820_map++;
	}

	boot_params->e820_entries = i;

	asm volatile ("lidt %0" :: "m" (idt));
	asm volatile ("lgdt %0" :: "m" (gdt));

	kernel_jump(kernel_start, boot_params);

	Print(L"Error: bzImage returned control to efilinux!");
out:
	return err;
}

struct loader bzimage_loader = {
	load_kernel,
};