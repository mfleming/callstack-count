#include <stdio.h>
#include <stdlib.h>
#include "callstack.h"

void __die(const char *func_name, int lineno)
{
    fprintf(stderr, "Dying @ %s:%d!!!\n", func_name, lineno);
    exit(1);
}