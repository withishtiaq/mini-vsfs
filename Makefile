# Makefile for MiniVSFS
# Usage:
#   make build            # compile both tools
#   make test             # run tests/tests.sh
#   make clean            # remove binaries and images
#
# If your sources are not in ./src, override SRCDIR:
#   make build SRCDIR=.

CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Werror
LDFLAGS ?=

# Change SRCDIR to '.' if you keep sources at repo root.
SRCDIR  ?= src
BINDIR  ?= .
EXDIR   ?= examples

BUILDER := $(BINDIR)/mkfs_builder
ADDER   := $(BINDIR)/mkfs_adder

BUILDER_SRC := $(SRCDIR)/mkfs_builder.c
ADDER_SRC   := $(SRCDIR)/mkfs_adder.c

.PHONY: all build test clean lint dirs

all: build

dirs:
	@mkdir -p $(EXDIR)

build: $(BUILDER) $(ADDER) | dirs

$(BUILDER): $(BUILDER_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(ADDER): $(ADDER_SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

test: build
	@chmod +x tests/tests.sh
	@tests/tests.sh

lint:
	@echo "No linter configured. Consider adding clang-format/clang-tidy."

clean:
	@rm -f $(BUILDER) $(ADDER) *.o *.img
	@rm -f $(EXDIR)/*.txt $(EXDIR)/*.bin || true
