CFLAGS += -Wall -Wextra -std=gnu99 `pkg-config --cflags sqlite3`
LDFLAGS += `pkg-config --libs sqlite3`
RAGEL ?= ragel
RLFLAGS +=

all: ks

cli.c: cli.rl
	@echo "RL	cli"
	$(RAGEL) $(RLFLAGS) $<

%.o: %.c ks.h
	@echo "CC	$*"
	$(CC) $(CFLAGS) -c $<

ks: ks.o cli.o
	@echo "LD	ks"
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	@echo CLEANING
	rm -rf *.o ks cli.c *.gcda *.gcno *.gcov *.db

.PHONY: all clean

$(V).SILENT:
