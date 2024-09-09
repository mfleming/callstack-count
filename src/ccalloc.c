#include "callstack.h"

unsigned long num_allocs = 0;
void *ccalloc(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    num_allocs++;
    return ptr;
}

unsigned long num_frees = 0;
unsigned long leaf_frees = 0;
void cfree(void *ptr, bool leaf)
{
    num_frees++;
    if (leaf) leaf_frees++;
    free(ptr);
}