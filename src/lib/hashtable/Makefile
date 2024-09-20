CFLAGS=-g2 -O0 -I../../include

all: tests

.PHONY: tests
tests:
	$(CC) $(CFLAGS) ../../ccalloc.c test.c -o test
	./test
