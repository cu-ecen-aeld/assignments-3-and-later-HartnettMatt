###############################################################################
# Makefile for "aesdsocket"
#
# Usage:
#  make              (build for native)
#  make clean        (remove object files and the "aesdsocket" executable)
#  make CROSS_COMPILE=aarch64-none-linux-gnu- (build for aarch64 cross-compile)
#
# Author: Matt Hartnett
###############################################################################

# If CROSS_COMPILE is not passed in, it defaults to empty (native build)
CROSS_COMPILE ?=

# The compiler and linker commands
CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  ?= -Wall -Werror -std=gnu11
LDFLAGS ?= -lpthread -lrt

# The target application and its object files
TARGET  ?= aesdsocket
OBJS    = aesdsocket.o

###############################################################################
# Default target: builds the aesdsocket application
###############################################################################
all: $(TARGET)

###############################################################################
# Rules to build the target application
###############################################################################
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

###############################################################################
# Clean target: remove build artifacts
###############################################################################
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
