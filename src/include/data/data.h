#ifndef __DATA_H__
#define __DATA_H__

#include "callstack.h"

#define MAX_STACK_ENTRIES 256

struct record {
	unsigned long id;
	struct callstack_entry stack[MAX_STACK_ENTRIES];
};


#endif /* __DATA_H__ */
