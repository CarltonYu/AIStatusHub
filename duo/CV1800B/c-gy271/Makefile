# GY-271 magnetometer reader for Milk-V Duo CV1800B.
# macOS -> riscv64-linux-musl cross-compilation via zig cc.

CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static
TARGET = gy271-read
DUO_HOST ?= root@192.168.42.1

all: $(TARGET)

$(TARGET): gy271_read.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f $(TARGET)

scp: $(TARGET)
	scp -O $(TARGET) $(DUO_HOST):/root/

.PHONY: all clean scp
