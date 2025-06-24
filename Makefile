CC := gcc
CFLAGS := -O3 -march=native -std=c23 -ggdb -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wformat-security -Wnull-dereference -Wstack-protector -Wtrampolines -Walloca -Wvla -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wcast-qual -Wstringop-overflow=4 -Warith-conversion -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wformat-signedness -Wshadow -Wstrict-overflow=4 -Wundef -Wstrict-prototypes -Wswitch-default -Wswitch-enum -Wstack-usage=1000000 -Wcast-align=strict

DEBUG := -fsanitize=address,undefined

LDFLAGS := -lSDL2

.PHONY: all clean fmt

all: gb

clean:
	rm -f gb

fmt:
	clang-format --style='{IndentWidth: 4, AllowShortFunctionsOnASingleLine: false}' -i *.c

gb: main.c gb.c
	$(CC) $(CFLAGS) $(DEBUG) $(LDFLAGS) $^ -o $@
