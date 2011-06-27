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
#include "stdlib.h"
#include "protocol.h"

struct fs_device {
	EFI_HANDLE handle;
	EFI_FILE_HANDLE fh;
	struct fs_ops *ops;
};

static struct fs_device *fs_devices;
static UINTN nr_fs_devices;

/**
 * file_open - Open a file on a volume
 * @name: pathname of the file to open
 * @file: used to return a pointer to the allocated file on success
 */
EFI_STATUS
file_open(CHAR16 *name, struct file **file)
{
	EFI_FILE_HANDLE fh;
	struct file *f;
	CHAR16 *filename;
	EFI_STATUS err;
	int dev_len;
	int i;

	f = malloc(sizeof(*f));
	if (!f)
		return EFI_OUT_OF_RESOURCES;

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_DEVICE_PATH *path;
		CHAR16 *dev;

		path = DevicePathFromHandle(fs_devices[i].handle);
		dev = DevicePathToStr(path);

		if (!StrnCmp(dev, name, StrLen(dev))) {
			f->handle = fs_devices[i].fh;
			dev_len = StrLen(dev);
			free_pool(dev);
			break;
		}

		free_pool(dev);
	}

	if (i == nr_fs_devices) {
		err = EFI_NOT_FOUND;
		goto fail;
	}

	/* Strip the device name */
	filename = name + dev_len;

	/* skip any path separators */
	while (*filename == ':' || *filename == '\\')
		filename++;

	err = uefi_call_wrapper(f->handle->Open, 5, f->handle, &fh,
				filename, EFI_FILE_MODE_READ, (UINT64)0);
	if (err != EFI_SUCCESS)
		goto fail;

	f->fh = fh;
	*file = f;

	return err;
fail:
	Print(L"Unable to open file \"%s\"", name);
	free(f);
	return err;
}

/**
 * file_close - Close a file handle
 * @f: the file to close
 */
EFI_STATUS
file_close(struct file *f)
{
	UINTN err;

	err = uefi_call_wrapper(f->handle->Close, 1, f->fh);

	if (err == EFI_SUCCESS)
		free(f);

	return err;
}

/**
 * list_boot_devices - Print a list of all disks with filesystems
 */
void list_boot_devices(void)
{
	int i;

	Print(L"Devices:\n\n");

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_DEVICE_PATH *path;
		EFI_HANDLE dev_handle;
		CHAR16 *dev;

		dev_handle = fs_devices[i].handle;

		path = DevicePathFromHandle(dev_handle);
		dev = DevicePathToStr(path);

		Print(L"\t%d. \"%s\"\n", i, dev);
		free_pool(dev);
	}

	Print(L"\n");
}

/*
 * Initialise filesystem protocol.
 */
EFI_STATUS
fs_init(void)
{
	EFI_STATUS err;
	UINTN size = 0;
	int i;

	size = 0;
	err = locate_handle(ByProtocol, &FileSystemProtocol,
			    NULL, &size, NULL);

	if (err != EFI_SUCCESS && size == 0) {
		Print(L"No devices support filesystems\n");
		goto out;
	}

	nr_fs_devices = size / sizeof(EFI_HANDLE);
	fs_devices = malloc(sizeof(*fs_devices) * nr_fs_devices);
	if (!fs_devices)
		goto out;

	err = locate_handle(ByProtocol, &FileSystemProtocol,
			    NULL, &size, (void **)fs_devices);

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_FILE_IO_INTERFACE *io;
		EFI_FILE_HANDLE fh;
		EFI_HANDLE dev_handle;

		dev_handle = fs_devices[i].handle;
		err = handle_protocol(dev_handle, &FileSystemProtocol,
				      (void **)&io);
		if (err != EFI_SUCCESS)
			goto out;

		err = volume_open(io, &fh);
		if (err != EFI_SUCCESS)
			goto out;

		fs_devices[i].fh = fh;
	}

out:
	return err;
}

void fs_exit(void)
{
	free(fs_devices);
}
