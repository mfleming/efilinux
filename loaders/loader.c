#include <efi.h>
#include "loader.h"

extern struct loader bzimage_loader;

struct loader *loaders[] = {
	&bzimage_loader,
	NULL,
};

/**
 * load_image - Attempt to load a new image
 * @handle: firmware-allocated handle that identifies the efilinux image
 * @name: filename of the new image to load
 * @cmdline: ascii command-line argument
 *
 * Try all of the registered loaders to see if any of them want to
 * load @name. If a loader successfully loads @name, it may not return
 * control to load_image(), for example see the bzImage loader.
 */
EFI_STATUS
load_image(EFI_HANDLE handle, CHAR16 *name, char *cmdline)
{
	struct loader **loader;
	EFI_STATUS err;

	err = EFI_UNSUPPORTED;
	for (loader = loaders; *loader != NULL; loader++) {
		err = (*loader)->load(handle, name, cmdline);
		if (err == EFI_SUCCESS)
			break;
	}

	return err;
}
