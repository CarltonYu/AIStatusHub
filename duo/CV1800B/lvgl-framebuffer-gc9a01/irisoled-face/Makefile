CC = zig cc
CFLAGS = -target riscv64-linux-musl -O2 -Wall -Wextra -static

TARGET = irisoled-face

IRISOLED_DIR ?= ../../eyes-resources/Irisoled
IRISOLED_COMMIT = $(shell cat $(IRISOLED_DIR)/SOURCE_COMMIT.txt 2>/dev/null || git -C $(IRISOLED_DIR) rev-parse HEAD 2>/dev/null || echo unknown)

all: $(TARGET)

irisoled_bitmaps.h: tools/extract_irisoled_bitmaps.py $(IRISOLED_DIR)/src/Irisoled.h $(IRISOLED_DIR)/src/Irisoled.cpp
	python3 tools/extract_irisoled_bitmaps.py $(IRISOLED_DIR)/src/Irisoled.h $(IRISOLED_DIR)/src/Irisoled.cpp $(IRISOLED_COMMIT) $@

$(TARGET): irisoled_face.c irisoled_bitmaps.h
	$(CC) $(CFLAGS) -o $@ irisoled_face.c

previews:
	../../eyes-resources/.venv/bin/python tools/render_irisoled_gifs.py

scp: $(TARGET)
	sshpass -p milkv scp -O $(TARGET) install-init.sh root@192.168.42.1:/root/

clean:
	rm -f $(TARGET)

.PHONY: all clean previews scp
