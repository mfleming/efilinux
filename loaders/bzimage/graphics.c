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
#include "protocol.h"
#include "stdlib.h"

static void find_bits(unsigned long mask, UINT8 *pos, UINT8 *size)
{
	UINT8 first, len;

	first = 0;
	len = 0;

	if (mask) {
		while (!(mask & 0x1)) {
			mask = mask >> 1;
			first++;
		}

		while (mask & 0x1) {
			mask = mask >> 1;
			len++;
		}
	}
        *pos = first;
        *size = len;
}

EFI_STATUS setup_graphics(struct boot_params *buf)
{
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	EFI_GUID graphics_proto = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_HANDLE *gop_handle = NULL;
	struct screen_info *si;
	EFI_STATUS err;
	UINTN nr_gops;
	UINTN size;
	int i;

	/* See if we have graphics output protocol */
	size = 0;
	err = locate_handle(ByProtocol, &graphics_proto, NULL,
			    &size, (void **)gop_handle);
	if (err != EFI_SUCCESS && err != EFI_BUFFER_TOO_SMALL)
		goto out;

	gop_handle = malloc(size);
	if (!gop_handle)
		goto out;

	err = locate_handle(ByProtocol, &graphics_proto, NULL,
			    &size, (void **)gop_handle);
	if (err != EFI_SUCCESS)
		goto out;

	nr_gops = size / sizeof(EFI_HANDLE);
	for (i = 0; i < nr_gops; i++) {
		EFI_HANDLE *h = &gop_handle[i];

		err = handle_protocol(*h, &graphics_proto, (void **)&gop);
		if (err != EFI_SUCCESS)
			continue;

		err = uefi_call_wrapper(gop->QueryMode, 4, gop,
					gop->Mode->Mode, &size, &info);
		if (err == EFI_SUCCESS)
			break;
	}

	/* We found a GOP */
	if (i != nr_gops) {
		si = &buf->screen_info;

		/* EFI framebuffer */
		si->orig_video_isVGA = 0x70;

		si->orig_x = 0;
		si->orig_y = 0;
		si->orig_video_page = 0;
		si->orig_video_mode = 0;
		si->orig_video_cols = 0;
		si->orig_video_lines = 0;
		si->orig_video_ega_bx = 0;
		si->orig_video_points = 0;

		si->lfb_base = gop->Mode->FrameBufferBase;
		si->lfb_size = gop->Mode->FrameBufferSize;
		si->lfb_width = info->HorizontalResolution;
		si->lfb_height = info->VerticalResolution;
		si->pages = 1;
		si->vesapm_seg = 0;
		si->vesapm_off = 0;

		if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
			si->lfb_depth = 32;
			si->red_size = 8;
			si->red_pos = 0;
			si->green_size = 8;
			si->green_pos = 8;
			si->blue_size = 8;
			si->blue_pos = 16;
			si->rsvd_size = 8;
			si->rsvd_pos = 24;
			si->lfb_linelength = info->PixelsPerScanLine * 4;

		} else if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
			si->lfb_depth = 32;
			si->red_size = 8;
			si->red_pos = 16;
			si->green_size = 8;
			si->green_pos = 8;
			si->blue_size = 8;
			si->blue_pos = 0;
			si->rsvd_size = 8;
			si->rsvd_pos = 24;
			si->lfb_linelength = info->PixelsPerScanLine * 4;
		} else if (info->PixelFormat == PixelBitMask) {
			find_bits(info->PixelInformation.RedMask,
				  &si->red_pos, &si->red_size);
			find_bits(info->PixelInformation.GreenMask,
				  &si->green_pos, &si->green_size);
			find_bits(info->PixelInformation.BlueMask,
				  &si->blue_pos, &si->blue_size);
			find_bits(info->PixelInformation.ReservedMask,
				  &si->rsvd_pos, &si->rsvd_size);
			si->lfb_depth = si->red_size + si->green_size +
				si->blue_size + si->rsvd_size;
			si->lfb_linelength = (info->PixelsPerScanLine * si->lfb_depth) / 8;
		} else {
			si->lfb_depth = 4;
			si->red_size = 0;
			si->red_pos = 0;
			si->green_size = 0;
			si->green_pos = 0;
			si->blue_size = 0;
			si->blue_pos = 0;
			si->rsvd_size = 0;
			si->rsvd_pos = 0;
			si->lfb_linelength = si->lfb_width / 2;
		}
	}

out:
	return err;
}
