SRCDIR:=src
HDRDIR:=src/include
ARCHDIR:=src/arch/x86/include/
UAPIDIR:=src/include/uapi
CFLAGS:=-Wall -Werror -g2 -ggdb -O2

all: main

main: $(SRCDIR)/*.c $(SRCDIR)/lib/linux/*.c $(SRCDIR)/lib/art/ops.c $(SRCDIR)/lib/hot/callstack.c
	$(CC) -lm $(CFLAGS) -I $(HDRDIR) -I $(ARCHDIR) -I $(UAPIDIR) $^ -o $@

clean:
	rm main
