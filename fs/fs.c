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
 * handle_to_dev - Return the device number for a handle
 * @handle: the device handle to search for
 */
int
handle_to_dev(EFI_HANDLE *handle)
{
	int i;

	for (i = 0; i < nr_fs_devices; i++) {
		if (fs_devices[i].handle == handle)
			break;
	}

	if (i == nr_fs_devices)
		return -1;

	return i;
}

/**
 * file_open - Open a file on a volume
 * @name: pathname of the file to open
 * @file: used to return a pointer to the allocated file on success
 */
EFI_STATUS
file_open(EFI_LOADED_IMAGE *image, CHAR16 *name, struct file **file)
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

	for (dev_len = 0; name[dev_len]; ++dev_len) {
		if (name[dev_len] == ':')
			break;
	}

	if (!name[dev_len] || !dev_len) {
		dev_len = 0;
		if (!image)
			goto notfound;

		i = handle_to_dev(image->DeviceHandle);
		if (i < 0 || i >= nr_fs_devices)
			goto notfound;

		f->handle = fs_devices[i].fh;
		goto found;
	} else
		name[dev_len++] = 0;

	if (name[0] >= '0' && name[0] <= '9') {
		i = Atoi(name);
		if (i >= nr_fs_devices)
			goto notfound;

		f->handle = fs_devices[i].fh;
		goto found;
	}

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_DEVICE_PATH *path;
		CHAR16 *dev;

		path = DevicePathFromHandle(fs_devices[i].handle);
		dev = DevicePathToStr(path);

		if (!StriCmp(dev, name)) {
			f->handle = fs_devices[i].fh;
			free_pool(dev);
			break;
		}

		free_pool(dev);
	}

	if (i == nr_fs_devices)
		goto notfound;

found:
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

notfound:
	err = EFI_NOT_FOUND;
fail:
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
	EFI_HANDLE *buf;
	EFI_STATUS err;
	UINTN size = 0;
	int i, j;

	size = 0;
	err = locate_handle(ByProtocol, &FileSystemProtocol,
			    NULL, &size, NULL);

	if (err != EFI_SUCCESS && size == 0) {
		Print(L"No devices support filesystems\n");
		return err;
	}

	buf = malloc(size);
	if (!buf)
		return EFI_OUT_OF_RESOURCES;

	nr_fs_devices = size / sizeof(EFI_HANDLE);
	fs_devices = malloc(sizeof(*fs_devices) * nr_fs_devices);
	if (!fs_devices) {
		err = EFI_OUT_OF_RESOURCES;
		goto out;
	}

	err = locate_handle(ByProtocol, &FileSystemProtocol,
			    NULL, &size, (void **)buf);

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_FILE_IO_INTERFACE *io;
		EFI_FILE_HANDLE fh;
		EFI_HANDLE dev_handle;

		dev_handle = buf[i];
		err = handle_protocol(dev_handle, &FileSystemProtocol,
				      (void **)&io);
		if (err != EFI_SUCCESS)
			goto close_handles;

		err = volume_open(io, &fh);
		if (err != EFI_SUCCESS)
			goto close_handles;

		fs_devices[i].handle = dev_handle;
		fs_devices[i].fh = fh;
	}

out:
	free(buf);
	return err;

close_handles:
	for (j = 0; j < i; j++) {
		EFI_FILE_HANDLE fh;

		fh = fs_devices[j].fh;
		uefi_call_wrapper(fh->Close, 1, fh);
	}

	free(fs_devices);
	goto out;
}

void fs_close(void)
{
	int i;

	for (i = 0; i < nr_fs_devices; i++) {
		EFI_FILE_HANDLE fh;

		fh = fs_devices[i].fh;
		uefi_call_wrapper(fh->Close, 1, fh);
	}
}

void fs_exit(void)
{
	fs_close();
	free(fs_devices);
}
