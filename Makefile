# fping for Windows - Makefile (MinGW-w64)
#
# Build with:  make            (produces fping.exe)
#              make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c99 -D_WIN32_WINNT=0x0600
LDFLAGS ?=
LIBS    = -lws2_32 -liphlpapi

SRCS = src/fping.c \
       src/optparse.c \
       src/seqmap.c \
       src/stats.c \
       src/socket4_win.c \
       src/socket6_win.c

OBJS = $(SRCS:.c=.o)

fping.exe: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

clean:
	rm -f $(OBJS) fping.exe

.PHONY: clean
