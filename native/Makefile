# Builds the native helper library that ships next to moonfall_mayhem.nrm.
#   make          -> moonfall_net.dll (Windows, cross-compiled with MinGW)
#   make linux    -> moonfall_net.so  (saving only, no networking)

MINGW ?= x86_64-w64-mingw32-gcc
CC ?= gcc
CFLAGS := -O2 -Wall -Wextra -std=gnu11

all: moonfall_net.dll

moonfall_net.dll: moonfall_net.c
	$(MINGW) $(CFLAGS) -shared -o $@ $< -lwinhttp -static-libgcc -Wl,--no-insert-timestamp

linux: moonfall_net.so

moonfall_net.so: moonfall_net.c
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $< -lpthread

clean:
	rm -f moonfall_net.dll moonfall_net.so

.PHONY: all linux clean
