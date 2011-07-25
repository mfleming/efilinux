#ifndef __STDLIB_H__
#define __STDLIB_H__

extern void *malloc(UINTN size);
extern void free(void *buf);

extern EFI_STATUS emalloc(UINTN, UINTN, EFI_PHYSICAL_ADDRESS *);
extern void efree(EFI_PHYSICAL_ADDRESS, UINTN);

static inline void memset(char *dst, char ch, UINTN size)
{
	int i;

	for (i = 0; i < size; i++)
		dst[i] = ch;
}

static inline void memcpy(char *dst, char *src, UINTN size)
{
	int i;

	for (i = 0; i < size; i++)
		*dst++ = *src++;
}

static inline int strlen(char *str)
{
	int len;

	len = 0;
	while (*str++)
		len++;

	return len;
}

static inline char *strstr(char *haystack, char *needle)
{
	char *p;
	char *word = NULL;
	int len = strlen(needle);

	if (!len)
		return NULL;

	p = haystack;
	while (*p) {
		word = p;
		if (!strncmpa((CHAR8 *)p, (CHAR8 *)needle, len))
			break;
		p++;
		word = NULL;
	}

	return word;
}

#endif /* __STDLIB_H__ */
