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

#ifndef __I386_H__
#define __I386_H__

#define EFI_LOADER_SIGNATURE	"EL32"

static inline void kernel_jump(EFI_PHYSICAL_ADDRESS kernel_start,
			       struct boot_params *boot_params)
{
	asm volatile ("cli		\n"
		      "movl %0, %%esi	\n"
		      "movl %1, %%ecx	\n"
		      "jmp *%%ecx	\n"
		      :: "m" (boot_params), "m" (kernel_start));
}

typedef void(*handover_func)(void *, EFI_SYSTEM_TABLE *,
			     struct boot_params *) __attribute__((regparm(0)));

static inline void handover_jump(UINT16 kernel_version, EFI_HANDLE image,
				 struct boot_params *bp,
				 EFI_PHYSICAL_ADDRESS kernel_start)
{
	kernel_start += bp->hdr.handover_offset;

	if (kernel_version == 0x20b) {
		asm volatile ("cli		\n"
			      "pushl %0         \n"
			      "pushl %1         \n"
			      "pushl %2         \n"
			      "movl %3, %%ecx	\n"
			      "jmp *%%ecx	\n"
			      :: "m" (bp), "m" (ST),
			      "m" (image), "m" (kernel_start));
	} else {
		handover_func hf = (handover_func)(UINTN)kernel_start;
		hf(image, ST, bp);
	}
}

#endif /* __I386_H__ */
