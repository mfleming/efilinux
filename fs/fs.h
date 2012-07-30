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
 *
 */

#ifndef __FS_H__
#define __FS_H__

#define MAX_FILENAME	256

struct file {
	EFI_FILE_HANDLE handle;
	EFI_FILE_HANDLE fh;
};

/**
 * volume_open - Open the root directory on a volume
 * @vol: the volume to open
 * @fh: place to return the open file handle for the root directory
 */
static inline EFI_STATUS
volume_open(EFI_FILE_IO_INTERFACE *vol, EFI_FILE_HANDLE *fh)
{
	return uefi_call_wrapper(vol->OpenVolume, 2, vol, fh);
}

/**
 * file_read - Read from an open file
 * @f: the file to read
 * @size: size in bytes to read from @f
 * @buf: place to store the data read
 */
static inline EFI_STATUS
file_read(struct file *f, UINTN *size, void *buf)
{
	return uefi_call_wrapper(f->handle->Read, 3, f->fh, size, buf);
}

/**
 * file_set_position - Set the current offset of a file
 * @f: the file on which we're changing current file position
 * @pos: the file offset to set the current position to
 */
static inline EFI_STATUS
file_set_position(struct file *f, UINT64 pos)
{
	return uefi_call_wrapper(f->fh->SetPosition, 2, f->fh, pos);
}

/**
 * file_size - Get the size (in bytes) of @file
 * @f: the file to query
 * @size: where to store the size of the file
 */
static inline EFI_STATUS
file_size(struct file *f, UINT64 *size)
{
	EFI_FILE_INFO *info;

	info = LibFileInfo(f->fh);

	if (!info)
		return EFI_UNSUPPORTED;

	*size = info->FileSize;

	free_pool(info);

	return EFI_SUCCESS;
}

extern EFI_STATUS file_open(EFI_LOADED_IMAGE *image, CHAR16 *name, struct file **file);
extern EFI_STATUS file_close(struct file *f);

extern void list_boot_devices(void);
extern int handle_to_dev(EFI_HANDLE *handle);

extern void fs_close(void);

extern EFI_STATUS fs_init(void);
extern void fs_exit(void);

#endif /* __FS_H__ */
