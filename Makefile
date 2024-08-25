SRCDIR:=src
HDRDIR:=src/include
CFLAGS:=-Wall -Werror -g2 -O0

all: main

main: $(SRCDIR)/main.c $(SRCDIR)/callstack.c
	$(CC) $(CFLAGS) -I $(HDRDIR) $^ -o $@

clean:
	rm main