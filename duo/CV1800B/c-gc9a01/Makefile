# Makefile for GC9A01 driver on MilkV Duo (CV1800B)
# macOS -> zig cc cross-compilation for riscv64-linux-musl

CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static
TARGET = gc9a01-demo
HW_SPI_TARGET = test-hw-spi

OBJS = main.o gc9a01.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(HW_SPI_TARGET) test-init test-full-init $(OBJS)

test-hw-spi: test_hw_spi.c
	$(CC) $(CFLAGS) -o $(HW_SPI_TARGET) test_hw_spi.c

# Transfer to Duo (use -O for Dropbear compatibility)
scp: $(TARGET)
	scp -O $(TARGET) root@192.168.42.1:/root/

scp-hw-spi: test-hw-spi
	scp -O $(HW_SPI_TARGET) root@192.168.42.1:/root/

.PHONY: all clean scp scp-hw-spi test-hw-spi
