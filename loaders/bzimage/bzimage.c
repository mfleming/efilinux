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

struct initrd {
	UINT64 size;
	struct file *file;
};

static void parse_initrd(EFI_LOADED_IMAGE *image, struct boot_params *buf, char *cmdline)
{
	EFI_PHYSICAL_ADDRESS addr;
	struct initrd *initrds;
	int nr_initrds;
	EFI_STATUS err;
	UINT64 size;
	char *initrd;
	int i, j;

	/*
	 * Has there been an initrd specified on the cmdline?
	 */
	buf->hdr.ramdisk_start = 0;
	buf->hdr.ramdisk_len = 0;

	initrd = cmdline;
	for (nr_initrds = 0; *initrd; nr_initrds++) {
		initrd = strstr(initrd, "initrd=");
		if (!initrd)
			break;

		initrd += strlen("initrd=");

		/* Consume filename */
		while (*initrd && *initrd != ' ')
			initrd++;

		/* Consume space */
		while (*initrd == ' ')
			initrd++;
	}

	if (!nr_initrds)
		return;

	initrds = malloc(sizeof(*initrds) * nr_initrds);
	if (!initrds)
		return;

	initrd = cmdline;
	for (i = 0; i < nr_initrds; i++) {
		CHAR16 filename[MAX_FILENAME], *n;
		struct initrd *rd = &initrds[i];
		struct file *rdfile;
		char *o, *p;

		initrd = strstr(initrd, "initrd=");
		if (!initrd)
			break;

		initrd += strlen("initrd=");
		p = initrd;
		while (*p && *p != ' ')
			p++;

		for (o = initrd, n = filename;
		     o != p; o++, n++)
			*n = *o;

		*n = '\0';

		err = file_open(image, filename, &rdfile);
		if (err != EFI_SUCCESS)
			goto close_handles;

		file_size(rdfile, &size);

		rd->size = size;
		rd->file = rdfile;

		buf->hdr.ramdisk_len += size;
	}

	size = buf->hdr.ramdisk_len;
	err = emalloc(size, 0x1000, &addr);
	if (err != EFI_SUCCESS)
		goto close_handles;

	if ((UINTN)addr > buf->hdr.ramdisk_max) {
		Print(L"ramdisk address is too high!\n");
		efree(addr, size);
		goto close_handles;
	}

	buf->hdr.ramdisk_start = (UINT32)(UINTN)addr;

	for (j = 0; j < nr_initrds; j++) {
		struct initrd *rd = &initrds[j];

		size = rd->size;
		err = file_read(rd->file, (UINTN *)&size, (void *)(UINTN)addr);
		if (err != EFI_SUCCESS) {
			efree(addr, size);
			goto close_handles;
		}

		addr += size;
	}

close_handles:
	for (j = 0; j < i; j++) {
		struct initrd *rd = &initrds[j];
		file_close(rd->file);
	}

	free(initrds);
}

/**
 * load_kernel - Load a kernel image into memory from the boot device
 */
EFI_STATUS
load_kernel(EFI_HANDLE image, CHAR16 *name, char *_cmdline)
{
	UINTN map_size, _map_size, map_key;
	EFI_PHYSICAL_ADDRESS kernel_start, addr;
	EFI_PHYSICAL_ADDRESS pref_address;
	struct boot_params *boot_params;
	EFI_MEMORY_DESCRIPTOR *map_buf;
	EFI_LOADED_IMAGE *info = NULL;
	struct e820_entry *e820_map;
	UINT64 setup_sz, init_size;
	struct boot_params *buf;
	struct efi_info *efi;
	UINT32 desc_version;
	UINT8 nr_setup_secs;
	struct file *file;
	UINTN desc_size;
	EFI_STATUS err;
	char *cmdline;
	UINT64 size;
	int i, j = 0;

	err = handle_protocol(image, &LoadedImageProtocol, (void **)&info);
	if (err != EFI_SUCCESS)
		info = NULL;

	err = file_open(info, name, &file);

	if (err != EFI_SUCCESS)
		goto out;

	err = file_set_position(file, (UINT64)0x1F1);
	if (err != EFI_SUCCESS)
		goto out;

	size = 1;
	err = file_read(file, (UINTN *)&size, &nr_setup_secs);
	if (err != EFI_SUCCESS)
		goto out;

	nr_setup_secs++;	/* Add the boot sector */
	setup_sz = nr_setup_secs * 512;

	buf = malloc(setup_sz);
	if (!buf)
		goto out;

	err = file_set_position(file, (UINT64)0);
	if (err != EFI_SUCCESS)
		goto out;

	err = file_read(file, (UINTN *)&setup_sz, buf);
	if (err != EFI_SUCCESS)
		goto out;

	err = file_size(file, &size);
	if (err != EFI_SUCCESS)
		goto out;

	size -= setup_sz;

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

	if (!buf->hdr.relocatable_kernel) {
		Print(L"Expected relocatable kernel");
		err = EFI_INVALID_PARAMETER;
		goto out;
	}

	if (buf->hdr.version >= 0x20a) {
		pref_address = buf->hdr.pref_address;
		init_size = buf->hdr.init_size;
	} else {
		pref_address = 0x100000;

		/*
		 * We need to account for the fact that the kernel
		 * needs room for decompression, otherwise we could
		 * end up trashing other chunks of allocated memory.
		 */
		init_size = size * 3;
	}

	/* Don't need an allocated ID, we're a prototype */
	buf->hdr.loader_id = 0x1;

	/*
	 * The kernel expects cmdline to be allocated pretty low,
	 * Documentation/x86/boot.txt says,
	 *
	 *	"The kernel command line can be located anywhere
	 *	between the end of the setup heap and 0xA0000"
	 */
	addr = 0xA0000;
	err = allocate_pages(AllocateMaxAddress, EfiLoaderData,
			     EFI_SIZE_TO_PAGES(strlen(_cmdline) + 1),
			     &addr);
	if (err != EFI_SUCCESS)
		goto out;
	cmdline = (char *)(UINTN)addr;
	memcpy(cmdline, _cmdline, strlen(_cmdline) + 1);

	parse_initrd(info, buf, cmdline);

	buf->hdr.cmd_line_ptr = (UINT32)(UINTN)cmdline;

	memset((char *)&buf->screen_info, 0x0, sizeof(buf->screen_info));

	addr = pref_address;
	err = allocate_pages(AllocateAddress, EfiLoaderData,
			     EFI_SIZE_TO_PAGES(init_size), &addr);
	if (err != EFI_SUCCESS) {
		/*
		 * We failed to allocate the preferred address, so
		 * just allocate some memory and hope for the best.
		 */
		err = emalloc(init_size, buf->hdr.kernel_alignment, &addr);
		if (err != EFI_SUCCESS)
			goto out;
	}

	kernel_start = addr;

	/*
	 * Read the rest of the kernel image.
	 */
	err = file_read(file, (UINTN *)&size, (void *)(UINTN)kernel_start);
	if (err != EFI_SUCCESS)
		goto out;

	addr = 0x3fffffff;
	err = allocate_pages(AllocateMaxAddress, EfiLoaderData,
			     EFI_SIZE_TO_PAGES(16384), &addr);
	if (err != EFI_SUCCESS)
		goto out;

	boot_params = (struct boot_params *)(UINTN)addr;

	memset((void *)boot_params, 0x0, 16384);

	/* Copy first two sectors to boot_params */
	memcpy((char *)boot_params, (char *)buf, 2 * 512);
	boot_params->hdr.code32_start = (UINT32)((UINT64)kernel_start);

	/*
	 * Use the kernel's EFI boot stub by invoking the handover
	 * protocol.
	 */
	if (buf->hdr.version >= 0x20b) {
		handover_jump(buf->hdr.version, image,
			      boot_params, kernel_start);
		goto out;
	}

	err = setup_graphics(buf);
	if (err != EFI_SUCCESS)
		goto out;

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

	/* We're just interested in the map's size for now */
	map_size = 0;
	err = get_memory_map(&map_size, NULL, NULL, NULL, NULL);
	if (err != EFI_SUCCESS && err != EFI_BUFFER_TOO_SMALL)
		goto out;

again:
	_map_size = map_size;
	err = emalloc(map_size, 1, &addr);
	if (err != EFI_SUCCESS)
		goto out;

	map_buf = (EFI_MEMORY_DESCRIPTOR *)(UINTN)addr;
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
			efree((UINTN)map_buf, _map_size);
			goto again;
		}
		goto out;
	}

	/* Close all open file handles */
	fs_close();

	err = exit_boot_services(image, map_key);
	if (err != EFI_SUCCESS)
		goto out;

	efi = &boot_params->efi_info;
	efi->efi_systab = (UINT32)(UINTN)sys_table;
	efi->efi_memdesc_size = desc_size;
	efi->efi_memdesc_version = desc_version;
	efi->efi_memmap = (UINT32)(UINTN)map_buf;
	efi->efi_memmap_size = map_size;
#ifdef x86_64
	efi->efi_systab_hi = (unsigned long)sys_table >> 32;
	efi->efi_memmap_hi = (unsigned long)map_buf >> 32;
#endif

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
		case EfiRuntimeServicesCode:
		case EfiRuntimeServicesData:
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
		case EfiPalCode:
			e820_type = E820_RESERVED;
			break;

		case EfiUnusableMemory:
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
			continue;
		}

		if (j && e820_map[j-1].type == e820_type &&
			(e820_map[j-1].addr + e820_map[j-1].size) == d->PhysicalStart) {
			e820_map[j-1].size += d->NumberOfPages << EFI_PAGE_SHIFT;
		} else {
			e820_map[j].addr = d->PhysicalStart;
			e820_map[j].size = d->NumberOfPages << EFI_PAGE_SHIFT;
			e820_map[j].type = e820_type;
			j++;
		}
	}

	boot_params->e820_entries = j;

	asm volatile ("lidt %0" :: "m" (idt));
	asm volatile ("lgdt %0" :: "m" (gdt));

	kernel_jump(kernel_start, boot_params);
out:
	return err;
}

struct loader bzimage_loader = {
	load_kernel,
};
