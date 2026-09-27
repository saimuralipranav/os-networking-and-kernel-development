CC = gcc
STD = $(shell $(CC) -std=c23 -E -x c /dev/null -o /dev/null >/dev/null 2>&1 && echo "-std=c23" || echo "-std=c2x")
CFLAGS = $(STD) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror -Wno-unused-parameter -g
LDLIBS = -lm

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

tempest: $(OBJS)
	$(CC) $(CFLAGS) -o tempest $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f tempest $(OBJS)

.PHONY: clean
