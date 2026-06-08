#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct color {
	const char *name;
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

static uint32_t chan(uint8_t value, const struct fb_bitfield *field)
{
	if (!field->length)
		return 0;

	return (uint32_t)(value >> (8 - field->length)) << field->offset;
}

static uint32_t pack_color(const struct fb_var_screeninfo *vinfo,
			   uint8_t r, uint8_t g, uint8_t b)
{
	return chan(r, &vinfo->red) |
	       chan(g, &vinfo->green) |
	       chan(b, &vinfo->blue);
}

static void put_pixel(uint8_t *line, int x, int bpp, uint32_t pixel)
{
	uint8_t *dst = line + x * (bpp / 8);

	if (bpp == 16) {
		uint16_t p = (uint16_t)pixel;
		memcpy(dst, &p, sizeof(p));
	} else if (bpp == 32) {
		memcpy(dst, &pixel, sizeof(pixel));
	}
}

static int fill_frame(int fd, const struct fb_fix_screeninfo *finfo,
		      const struct fb_var_screeninfo *vinfo,
		      const struct color *color)
{
	size_t bytes = (size_t)finfo->line_length * vinfo->yres;
	uint8_t *frame = calloc(1, bytes);
	uint32_t pixel = pack_color(vinfo, color->r, color->g, color->b);
	ssize_t written;
	size_t offset = 0;
	int x;
	int y;

	if (!frame) {
		perror("calloc");
		return -1;
	}

	for (y = 0; y < (int)vinfo->yres; y++) {
		uint8_t *line = frame + (size_t)y * finfo->line_length;

		for (x = 0; x < (int)vinfo->xres; x++)
			put_pixel(line, x, vinfo->bits_per_pixel, pixel);
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek");
		free(frame);
		return -1;
	}

	while (offset < bytes) {
		written = write(fd, frame + offset, bytes - offset);
		if (written < 0) {
			perror("write");
			free(frame);
			return -1;
		}
		offset += (size_t)written;
	}

	free(frame);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
	const struct color colors[] = {
		{ "red", 255, 0, 0 },
		{ "green", 0, 255, 0 },
		{ "blue", 0, 0, 255 },
		{ "white", 255, 255, 255 },
		{ "black", 0, 0, 0 },
	};
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
	int fd;
	size_t i;

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}

	if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		perror("FBIOGET_FSCREENINFO");
		close(fd);
		return 1;
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		perror("FBIOGET_VSCREENINFO");
		close(fd);
		return 1;
	}

	printf("%s: %ux%u, %u bpp, line_length=%u\n",
	       dev, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
	       finfo.line_length);

	if (vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 32) {
		fprintf(stderr, "unsupported bits_per_pixel: %u\n",
			vinfo.bits_per_pixel);
		close(fd);
		return 1;
	}

	for (i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
		printf("fill %s\n", colors[i].name);
		if (fill_frame(fd, &finfo, &vinfo, &colors[i]) < 0) {
			close(fd);
			return 1;
		}
		usleep(700000);
	}

	close(fd);
	return 0;
}
