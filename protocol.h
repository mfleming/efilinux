/*
 * Copyright (c) 2011, Intel Corporation.
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

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

/**
 * handle_protocol - Query @handle to see if it supports @protocol
 * @handle: the handle being queried
 * @protocol: the GUID of the protocol
 * @interface: used to return the protocol interface
 *
 * Query @handle to see if @protocol is supported. If it is supported,
 * @interface contains the protocol interface.
 */
static inline EFI_STATUS
handle_protocol(EFI_HANDLE handle, EFI_GUID *protocol, void **interface)
{
	return uefi_call_wrapper(boot->HandleProtocol, 3,
				 handle, protocol, interface);
}

/**
 * locate_handle - Search for handles that support @protocol
 * @type: the search type, which handles are returned
 * @protocol: the protocol to search by (only valid if @type is ByProtocol)
 * @key: the search key
 * @size: on input the size in bytes of @buffer, on output the size of
 *        the returned array or the required size to store the array
 *        in @buffer if it was not large enough
 * @buffer: buffere where the array of handles is returned
 */
static inline EFI_STATUS
locate_handle(EFI_LOCATE_SEARCH_TYPE type, EFI_GUID *protocol, void *key,
	      UINTN *size, EFI_HANDLE *buffer)
{
	return uefi_call_wrapper(boot->LocateHandle, 5, type, protocol,
				 key, size, buffer);
}

#endif /* __PROTOCOL_H__ */
