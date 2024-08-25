# callstack-count

`perf` in the Linux kernel needs to process and count "callchains" (callstacks or call graphs) when reporting on profiles captured with `perf record -g`. Currently (as of August 2024) these are maintained in a kind of augmented red-black tree  / radix tree with nodes that contain a singly-linked list of callstack entries (shared object pointer and instruction pointer pairs). Common prefixes in callstacks share nodes and split at the point where the callstacks differ.

When reading large `perf.data` files this design suffers from the usual linked list problem in that most of the execution time is eaten up by cache misses due to the random nature of following a list's `->next` pointer. It's not unusual for 20-30% of startup time of `perf report` to be spent traversing these lists.
