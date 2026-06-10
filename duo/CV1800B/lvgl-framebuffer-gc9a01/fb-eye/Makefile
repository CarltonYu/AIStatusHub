CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static

TARGET = fb-eye-anim

all: $(TARGET)

$(TARGET): fb_eye_anim.c
	$(CC) $(CFLAGS) -o $@ $<

scp: $(TARGET)
	sshpass -p milkv scp -O $(TARGET) root@192.168.42.1:/root/

clean:
	rm -f $(TARGET)

.PHONY: all clean scp
