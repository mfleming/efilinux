#
# Copyright (c) 2011, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#
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
		-DEFI_FUNCTION_WRAPPER -fPIC -fshort-wchar
LDFLAGS=-T $(LDSCRIPT) -Bsymbolic -shared -nostdlib -L$(LIBDIR) $(CRT0)

IMAGE=efilinux.efi
OBJS = entry.o

all: $(IMAGE)

efilinux.efi: efilinux.so

efilinux.so: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^  -lgnuefi -lefi $(shell $(CC) -print-libgcc-file-name)

clean:
	rm -f $(IMAGE) efilinux.so $(OBJS)
