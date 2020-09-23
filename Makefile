CFLAGS += -Wall -Wextra -std=gnu99 `pkg-config --cflags sqlite3`
LDFLAGS += `pkg-config --libs sqlite3`
RAGEL ?= ragel
RLFLAGS +=

all: ks

cli.c: cli.rl
	$(RAGEL) $(RLFLAGS) $<

%.o: %.c ks.h
	$(CC) $(CFLAGS) -c $<

ks: ks.o cli.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	@rm -rf *.o ks cli.c

.PHONY: all clean
