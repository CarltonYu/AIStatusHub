CC = zig cc
CFLAGS ?= -target riscv64-linux-musl -O2 -Wall -Wextra -static
BUILD_DIR := build
BIN := $(BUILD_DIR)/fb-fill

.PHONY: all clean scp

all: $(BIN)

$(BIN): fb_fill.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

scp: $(BIN)
	scp -O $(BIN) root@192.168.42.1:/root/

clean:
	rm -rf $(BUILD_DIR)
