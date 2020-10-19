CFLAGS += -Wall -Wextra -std=c99 -pedantic `pkg-config --cflags sqlite3`
LDFLAGS += `pkg-config --libs sqlite3`
RAGEL ?= ragel
RLFLAGS +=
A2X ?= a2x
DOCFLAGS += -f manpage -d manpage -L

all: ks ks.1

cli.c: cli.rl
	@echo "RL	cli"
	$(RAGEL) $(RLFLAGS) $<

%.o: %.c ks.h
	@echo "CC	$*"
	$(CC) $(CFLAGS) -c $<

ks: ks.o cli.o
	@echo "LD	ks"
	$(CC) -o $@ $^ $(LDFLAGS)

ks.1: ks.1.adoc asciidoc.conf
	@echo "DOC	ks"
	$(A2X) $(DOCFLAGS) $<

check: cli.c
	cppcheck --enable=all ks.c cli.c
	splint -weak -castfcnptr ks.c

clean:
	@echo CLEANING
	rm -rf *.o ks ks.1 cli.c *.gcda *.gcno *.gcov *.db TAGS

TAGS: cli.c ks.c
	@echo TAGS
	ctags -e ks.c cli.c

tags: TAGS

.PHONY: all check clean tags

$(V).SILENT:
