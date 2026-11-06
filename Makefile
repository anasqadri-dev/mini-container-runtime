# Simple Makefile so the project builds without CMake.
# CMake (see CMakeLists.txt) is the primary build system.

CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -g -O0
CFLAGS  += -Iinclude

BUILD   := build-make
SRCS    := $(filter-out src/main.c,$(wildcard src/*.c))
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

.PHONY: all clean

all: $(BUILD)/mcr

$(BUILD)/mcr: $(BUILD)/main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) build
