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

#ifndef __X86_64_H__
#define __X86_64_H__

#define EFI_LOADER_SIGNATURE	"EL64"

typedef void(*kernel_func)(void *, struct boot_params *);
typedef void(*handover_func)(void *, EFI_SYSTEM_TABLE *, struct boot_params *);

static inline void kernel_jump(EFI_PHYSICAL_ADDRESS kernel_start,
			       struct boot_params *boot_params)
{
	kernel_func kf;

	asm volatile ("cli");

	/* The 64-bit kernel entry is 512 bytes after the start. */
	kf = (kernel_func)kernel_start + 512;

	/*
	 * The first parameter is a dummy because the kernel expects
	 * boot_params in %[re]si.
	 */
	kf(NULL, boot_params);
}

static inline void handover_jump(UINT16 kernel_version, EFI_HANDLE image,
				 struct boot_params *bp,
				 EFI_PHYSICAL_ADDRESS kernel_start)
{
	UINT32 offset = bp->hdr.handover_offset;
	handover_func hf;

	asm volatile ("cli");

	/* The 64-bit kernel entry is 512 bytes after the start. */
	kernel_start += 512;

	hf = (handover_func)(kernel_start + offset);
	hf(image, ST, bp);
}

#endif /* __X86_64_H__ */
