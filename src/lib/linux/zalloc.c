// SPDX-License-Identifier: LGPL-2.1

#include <stdlib.h>
#include <linux/zalloc.h>

extern unsigned long num_allocs;
void *zalloc(size_t size)
{
	num_allocs++;
	return calloc(1, size);
}

void __zfree(void **ptr)
{
	free(*ptr);
	*ptr = NULL;
}
