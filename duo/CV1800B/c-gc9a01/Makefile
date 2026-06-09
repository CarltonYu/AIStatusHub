# Makefile for GC9A01 driver on MilkV Duo (CV1800B)
# macOS -> zig cc cross-compilation for riscv64-linux-musl

CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static
TARGET = gc9a01-demo
HW_SPI_TARGET = test-hw-spi
EYE_ANIM_TARGET = eye-anim

EYE_RES ?= 240
EYE_ANIM_BIN = ../eyes-resources/parsed/eye_animation_$(EYE_RES)x$(EYE_RES).bin

OBJS = main.o gc9a01.o

all: $(TARGET) $(HW_SPI_TARGET) $(EYE_ANIM_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(HW_SPI_TARGET) $(EYE_ANIM_TARGET) $(OBJS)

test-hw-spi: test_hw_spi.c
	$(CC) $(CFLAGS) -o $(HW_SPI_TARGET) test_hw_spi.c

$(EYE_ANIM_TARGET): eye_anim.c
	$(CC) $(CFLAGS) -o $(EYE_ANIM_TARGET) eye_anim.c

# Transfer to Duo (use -O for Dropbear compatibility)
scp: $(TARGET)
	scp -O $(TARGET) root@192.168.42.1:/root/

scp-hw-spi: test-hw-spi
	scp -O $(HW_SPI_TARGET) root@192.168.42.1:/root/

scp-eye-anim: $(EYE_ANIM_TARGET)
	sshpass -p milkv scp -O $(EYE_ANIM_TARGET) root@192.168.42.1:/root/
	sshpass -p milkv scp -O $(EYE_ANIM_BIN) root@192.168.42.1:/root/eye_animation.bin

# Convenience targets for different resolutions
scp-eye-anim-240: ; $(MAKE) EYE_RES=240 scp-eye-anim
scp-eye-anim-160: ; $(MAKE) EYE_RES=160 scp-eye-anim
scp-eye-anim-128: ; $(MAKE) EYE_RES=128 scp-eye-anim

.PHONY: all clean scp scp-hw-spi scp-eye-anim scp-eye-anim-240 scp-eye-anim-160 scp-eye-anim-128 test-hw-spi
