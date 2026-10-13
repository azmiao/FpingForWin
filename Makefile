# fping for Windows - Makefile (MinGW-w64 / llvm-mingw)
#
# Build with:  make                          (produces fping.exe for the host arch)
#              make TRIPLE=aarch64-w64-mingw32   (cross-compile for ARM64)
#              make clean
#
# TRIPLE selects the target triple.  With llvm-mingw the same toolchain
# provides <triple>-clang for x86_64 / aarch64 / i686 / armv7.
# With classic MinGW-w64 (egor-tensin/setup-mingw) leave TRIPLE empty and
# set CC=gcc instead.

TRIPLE  ?=
ifeq ($(TRIPLE),)
CC      ?= gcc
else
# Use an override so that a stray CC environment variable (llvm-mingw sets
# CC=cc for the host triple) does not silently produce a host-arch binary.
# Pass CC=... on the command line to override explicitly.
CC      = $(TRIPLE)-clang
endif

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
