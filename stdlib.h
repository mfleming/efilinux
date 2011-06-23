#ifndef __STDLIB_H__
#define __STDLIB_H__

extern void *malloc(UINTN size);
extern void free(void *buf);

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

#endif /* __STDLIB_H__ */
