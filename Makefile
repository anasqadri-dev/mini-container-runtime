# Simple Makefile so the project builds without CMake.
# CMake (see CMakeLists.txt) is the primary build system.

CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -g -O0
CFLAGS  += -Iinclude

BUILD   := build-make
SRCS    := $(filter-out src/main.c,$(wildcard src/*.c))
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

TESTS   := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))

.PHONY: all clean test

all: $(BUILD)/mcr

# Build and run the unit tests (the integration tests need root, see tests/integration.sh)
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; $$t || exit 1; done

$(BUILD)/test_%: tests/test_%.c $(OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itests -o $@ $< $(OBJS)

$(BUILD)/mcr: $(BUILD)/main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) build
