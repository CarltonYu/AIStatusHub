# Makefile for GC9A01 SPI face daemon on MilkV Duo (CV1800B)
# Cross-compiles a static RISC-V binary on macOS using zig cc.

CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static
LDFLAGS = -s
TARGET = gc9a01-face-daemon
OBJS = gc9a01_hw.o gc9a01_face_daemon.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

scp: $(TARGET)
	scp -O $(TARGET) root@192.168.42.1:/root/

install-on-duo: $(TARGET)
	scp -O $(TARGET) install-init.sh root@192.168.42.1:/root/
	ssh root@192.168.42.1 'sh /root/install-init.sh'

.PHONY: all clean scp install-on-duo
