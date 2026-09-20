CC = gcc
CFLAGS = -std=c23 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror -Wno-unused-parameter -g
LDLIBS =

SRCS = $(wildcard *.c)

tempest: $(SRCS)
	$(CC) $(CFLAGS) -o tempest $(SRCS) $(LDLIBS)

clean:
	rm -f tempest

.PHONY: clean
