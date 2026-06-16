CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static
TARGET = udp-console-input-daemon

all: $(TARGET)

$(TARGET): udp_console_input_daemon.o
	$(CC) $(CFLAGS) -s -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	mkdir -p ../duo-buildroot-sdk/device/milkv-duo-sd/overlay/usr/bin
	cp $(TARGET) ../duo-buildroot-sdk/device/milkv-duo-sd/overlay/usr/bin/

clean:
	rm -f $(TARGET) *.o
