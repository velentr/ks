#!/bin/sh

set -e

ragel cli.rl
gcc -Wall -Wextra -std=gnu99 ks.c cli.c `pkg-config --libs --cflags sqlite3` -o ks
