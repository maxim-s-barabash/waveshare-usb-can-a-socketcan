CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

all: aa55cand

aa55cand: aa55cand.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f aa55cand

.PHONY: all clean
