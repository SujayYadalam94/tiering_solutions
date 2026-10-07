CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -mavx2 -pthread
LDLIBS  = -lnuma -lm -pthread

bwlat: bwlat.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f bwlat

.PHONY: clean
