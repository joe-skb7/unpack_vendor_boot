APP = unpack_vendor_boot
CC = gcc
CFLAGS = -Wall -O2
SOURCES = main.c

all: $(SOURCES)
	$(CC) $(CFLAGS) $(SOURCES) -o $(APP)

clean:
	-rm -f $(APP)

.PHONY: all clean
