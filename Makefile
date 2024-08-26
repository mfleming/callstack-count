SRCDIR:=src
HDRDIR:=src/include
ARCHDIR:=src/arch/x86/include/
UAPIDIR:=src/include/uapi
CFLAGS:=-Wall -Werror -g2 -O0

all: main

main: $(SRCDIR)/*.c $(SRCDIR)/lib/linux/*.c
	$(CC) -lm $(CFLAGS) -I $(HDRDIR) -I $(ARCHDIR) -I $(UAPIDIR) $^ -o $@

clean:
	rm main
