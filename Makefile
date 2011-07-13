#
# Copyright (c) 2011, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above
#      copyright notice, this list of conditions and the following
#      disclaimer in the documentation and/or other materials provided
#      with the distribution.
#    * Neither the name of Intel Corporation nor the names of its
#      contributors may be used to endorse or promote products derived
#      from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

%.efi: %.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel \
		-j .rela -j .reloc --target=$(FORMAT) $*.so $@

OBJCOPY=objcopy

MACHINE=$(shell $(CC) -dumpmachine | sed "s/\(-\).*$$//")

ifeq ($(MACHINE),x86_64)
	ARCH=$(MACHINE)
	LIBDIR=/usr/lib64
	FORMAT=efi-app-x86-64
else
	ARCH=ia32
	LIBDIR=/usr/lib
	FORMAT=efi-app-$(ARCH)
endif

CRT0=$(LIBDIR)/gnuefi/crt0-efi-$(ARCH).o
LDSCRIPT=$(LIBDIR)/gnuefi/elf_$(ARCH)_efi.lds

CFLAGS=-I. -I/usr/include/efi -I/usr/include/efi/$(ARCH) \
		-DEFI_FUNCTION_WRAPPER -fPIC -fshort-wchar -ffreestanding \
		-Wall -Ifs/ -Iloaders/ -D$(ARCH) -Werror
LDFLAGS=-T $(LDSCRIPT) -Bsymbolic -shared -nostdlib -L$(LIBDIR) $(CRT0)

IMAGE=efilinux.efi
OBJS = entry.o malloc.o
FS = fs/fs.o

LOADERS = loaders/loader.o \
	  loaders/bzimage/bzimage.o \
	  loaders/bzimage/graphics.o

all: $(IMAGE)

efilinux.efi: efilinux.so

efilinux.so: $(OBJS) $(FS) $(LOADERS)
	$(LD) $(LDFLAGS) -o $@ $^  -lgnuefi -lefi $(shell $(CC) -print-libgcc-file-name)

clean:
	rm -f $(IMAGE) efilinux.so $(OBJS) $(FS) $(LOADERS)
